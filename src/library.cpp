#include "library.h"

#include "covermatch.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace fs = std::filesystem;

static fs::path P(const std::string& s) { return fs::u8path(s); }

static std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

// "Super Mario World (USA) [!]" -> "Super Mario World"
static std::string CleanTitle(const std::string& stem) {
  std::string out;
  int depth = 0;
  for (char c : stem) {
    if (c == '(' || c == '[') { ++depth; continue; }
    if (c == ')' || c == ']') { if (depth > 0) --depth; continue; }
    if (depth == 0) out += c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  return out.empty() ? stem : out;
}

Library::~Library() {
  StopCoverDownload();
  FreeTextures();
}

void Library::FreeTextures() {
  for (auto& g : games_) {
    if (g.cover) SDL_DestroyTexture(g.cover);
    g.cover = nullptr;
    g.coverTried = false;
  }
}

void Library::Scan(const std::vector<std::string>& dirs) {
  FreeTextures();
  games_.clear();
  static const char* exts[] = {".sfc", ".smc", ".swc", ".fig", ".bs"};
  for (const auto& d : dirs) {
    std::error_code ec;
    if (!fs::is_directory(P(d), ec)) continue;
    for (fs::recursive_directory_iterator it(P(d), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string ext = Lower(it->path().extension().u8string());
      if (std::find(std::begin(exts), std::end(exts), ext) == std::end(exts)) continue;
      Game g;
      g.path = it->path().u8string();
      g.stem = it->path().stem().u8string();
      g.title = CleanTitle(g.stem);
      bool dup = std::any_of(games_.begin(), games_.end(), [&](const Game& o) { return o.path == g.path; });
      if (!dup) games_.push_back(std::move(g));
    }
  }
  std::sort(games_.begin(), games_.end(),
            [](const Game& a, const Game& b) { return Lower(a.title) < Lower(b.title); });
}

// ---- cover textures --------------------------------------------------------
SDL_Texture* Library::LoadTexture(SDL_Renderer* r, const std::string& file, int* ow, int* oh) {
  std::ifstream f(P(file), std::ios::binary);
  if (!f) return nullptr;
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 4);
  if (!px) return nullptr;

  // Box-downscale huge images so a big library doesn't eat video memory.
  std::vector<unsigned char> scaled;
  const int maxDim = 320;
  int f2 = std::max(1, std::max(w, h) / maxDim);
  const unsigned char* src = px;
  if (f2 > 1) {
    int nw = w / f2, nh = h / f2;
    scaled.resize(size_t(nw) * nh * 4);
    for (int y = 0; y < nh; ++y)
      for (int x = 0; x < nw; ++x)
        for (int c = 0; c < 4; ++c) {
          int sum = 0;
          for (int dy = 0; dy < f2; ++dy)
            for (int dx = 0; dx < f2; ++dx) sum += px[((y * f2 + dy) * w + (x * f2 + dx)) * 4 + c];
          scaled[(size_t(y) * nw + x) * 4 + c] = (unsigned char)(sum / (f2 * f2));
        }
    w = nw; h = nh; src = scaled.data();
  }
  SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
  if (t) {
    SDL_UpdateTexture(t, nullptr, src, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    if (ow) *ow = w;
    if (oh) *oh = h;
  }
  stbi_image_free(px);
  return t;
}

void Library::PollReady() {
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto& s : ready_)
    for (auto& g : games_)
      if (g.stem == s && !g.cover) g.coverTried = false;
  ready_.clear();
}

bool Library::LoadCover(Game& g, SDL_Renderer* r) {
  if (g.coverTried) return g.cover != nullptr;
  g.coverTried = true;
  static const char* exts[] = {".png", ".jpg", ".jpeg", ".bmp"};
  for (auto* e : exts) {
    std::string file = (P(coversDir_) / P(g.stem + e)).u8string();
    std::error_code ec;
    if (!fs::exists(P(file), ec)) continue;
    g.cover = LoadTexture(r, file, &g.cw, &g.ch);
    if (g.cover) break;
  }
  return g.cover != nullptr;
}

// ---- cover downloader (libretro-thumbnails) --------------------------------
#ifdef _WIN32
namespace {
struct Http {
  HINTERNET ses = nullptr, con = nullptr;
  DWORD status = 0, err = 0;  // last HTTP status / Win32 error
  Http() {
    ses = WinHttpOpen(L"Birdstation/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (ses) {
      WinHttpSetTimeouts(ses, 10000, 10000, 15000, 30000);
      con = WinHttpConnect(ses, L"thumbnails.libretro.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    }
    if (!con) err = GetLastError();
  }
  ~Http() {
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
  }
  bool Get(const std::string& path, std::vector<unsigned char>& out) {
    out.clear();
    status = 0;
    err = 0;
    if (!con) return false;
    std::wstring wpath(path.begin(), path.end());
    HINTERNET rq = WinHttpOpenRequest(con, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!rq) { err = GetLastError(); return false; }
    bool ok = false;
    if (WinHttpSendRequest(rq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(rq, nullptr)) {
      DWORD code = 0, sz = sizeof code;
      WinHttpQueryHeaders(rq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
      status = code;
      if (code == 200) {
        ok = true;
        for (;;) {
          DWORD avail = 0;
          if (!WinHttpQueryDataAvailable(rq, &avail) || avail == 0) break;
          size_t old = out.size();
          out.resize(old + avail);
          DWORD got = 0;
          if (!WinHttpReadData(rq, out.data() + old, avail, &got)) { err = GetLastError(); ok = false; break; }
          out.resize(old + got);
        }
      }
    } else {
      err = GetLastError();
    }
    WinHttpCloseHandle(rq);
    return ok && !out.empty();
  }
};
}  // namespace
#endif

void Library::SetNote(const std::string& s) {
  std::lock_guard<std::mutex> lk(mtx_);
  note_ = s;
}

std::string Library::Note() {
  std::lock_guard<std::mutex> lk(mtx_);
  return note_;
}

void Library::StartCoverDownload() {
  if (dlRunning_) return;
  if (dlThread_.joinable()) dlThread_.join();
  std::vector<std::string> need;
  for (auto& g : games_) {
    bool have = false;
    for (auto* e : {".png", ".jpg", ".jpeg", ".bmp"}) {
      std::error_code ec;
      if (fs::exists(P(coversDir_) / P(g.stem + e), ec)) { have = true; break; }
    }
    if (!have) need.push_back(g.stem);
  }
  dlTotal_ = int(need.size());
  dlDone_ = 0;
  dlFound_ = 0;
  dlCancel_ = false;
  if (need.empty()) { SetNote("Todos os jogos ja tem capa."); return; }
  SetNote("");
  dlRunning_ = true;
  dlThread_ = std::thread(&Library::DownloadThread, this, std::move(need));
}

void Library::StopCoverDownload() {
  dlCancel_ = true;
  if (dlThread_.joinable()) dlThread_.join();
  dlRunning_ = false;
}

void Library::DownloadThread(std::vector<std::string> stems) {
#ifdef _WIN32
  std::error_code ec;
  fs::create_directories(P(coversDir_), ec);
  std::ofstream log;
  if (!logPath_.empty()) {
    fs::create_directories(P(logPath_).parent_path(), ec);
    log.open(P(logPath_), std::ios::trunc);
  }
  auto L = [&](const std::string& s) { if (log) log << s << "\n"; };

  Http http;
  const std::string base = "/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/";
  std::vector<unsigned char> data;

  // 1) directory listing -> fuzzy matching by title
  std::vector<covermatch::Entry> index;
  if (http.Get(base, data)) {
    index = covermatch::ParseIndex(std::string(data.begin(), data.end()));
    L("Indice do servidor: " + std::to_string(index.size()) + " capas");
  } else {
    L("Nao foi possivel ler o indice (HTTP " + std::to_string(http.status) + ", erro Win32 " + std::to_string(http.err) + ")");
  }
  if (index.empty() && http.status == 0) {
    SetNote("Sem acesso ao servidor de capas (erro " + std::to_string(http.err) + "). Verifique a internet/firewall.");
  }

  int missing = 0;
  for (const auto& stem : stems) {
    if (dlCancel_) break;
    std::vector<std::string> paths;  // URL paths to try, best first
    std::string matched;
    int pick = index.empty() ? -1 : covermatch::Pick(index, stem);
    if (pick >= 0) { paths.push_back(base + index[pick].href); matched = index[pick].name; }
    // 2) fallbacks: exact name, then name + " (USA)"
    std::string clean = stem;
    for (size_t b; (b = clean.find('[')) != std::string::npos;) {
      size_t e = clean.find(']', b);
      clean.erase(b, e == std::string::npos ? std::string::npos : e - b + 1);
    }
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    paths.push_back(base + covermatch::UrlName(clean) + ".png");
    if (clean.find('(') == std::string::npos) paths.push_back(base + covermatch::UrlName(clean + " (USA)") + ".png");

    bool got = false;
    for (const auto& pth : paths) {
      if (dlCancel_) break;
      if (http.Get(pth, data) && data.size() > 64) {
        std::ofstream f(P(coversDir_) / P(stem + ".png"), std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        bool wrote = bool(f);
        f.close();
        if (wrote) {
          got = true;
          ++dlFound_;
          L("OK     " + stem + (matched.empty() ? "" : "  ->  " + matched));
          std::lock_guard<std::mutex> lk(mtx_);
          ready_.push_back(stem);
        } else {
          L("ERRO   nao foi possivel gravar " + (P(coversDir_) / P(stem + ".png")).u8string());
        }
        break;
      }
    }
    if (!got && !dlCancel_) {
      ++missing;
      L("FALTA  " + stem + "  (HTTP " + std::to_string(http.status) + ", erro " + std::to_string(http.err) + ")");
    }
    ++dlDone_;
  }
  dlMissing_ = missing;
  if (Note().empty()) {
    SetNote(std::to_string(dlFound_.load()) + " capa(s) baixada(s)" +
            (missing ? ", " + std::to_string(missing) + " nao encontrada(s) (veja config\\covers.log)" : "."));
  }
#else
  (void)stems;
#endif
  dlRunning_ = false;
}
