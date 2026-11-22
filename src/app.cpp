#include "app.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static const char* kCoreFile = "snes9x_libretro.dll";
#else
static const char* kCoreFile = "snes9x_libretro.so";
#endif

static fs::path P(const std::string& s) { return fs::u8path(s); }
static std::string Join(const std::string& a, const std::string& b) { return (P(a) / P(b)).u8string(); }
static std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
static bool IsRomExt(const std::string& ext) {
  std::string e = Lower(ext);
  return e == ".sfc" || e == ".smc" || e == ".swc" || e == ".fig" || e == ".bs";
}
static bool IsImageExt(const std::string& ext) {
  std::string e = Lower(ext);
  return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp";
}

static std::string PickFolder() {
#ifdef _WIN32
  BROWSEINFOW bi{};
  bi.lpszTitle = L"Escolha a pasta com as suas ROMs de SNES";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) return "";
  wchar_t buf[MAX_PATH];
  std::string out;
  if (SHGetPathFromIDListW(pidl, buf)) {
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) {
      out.resize(size_t(n) - 1);
      WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), n, nullptr, nullptr);
    }
  }
  CoTaskMemFree(pidl);
  return out;
#else
  return "";
#endif
}

// ---------------------------------------------------------------------------
App::App() : input_(cfg_), lib_("") {}
App::~App() { Shutdown(); }

bool App::Init(int argc, char** argv) {
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Birdstation", SDL_GetError(), nullptr);
    return false;
  }
#ifdef _WIN32
  OleInitialize(nullptr);
#endif

  // Portable layout: everything lives next to the executable.
  char* bp = SDL_GetBasePath();
  base_ = bp ? bp : "./";
  SDL_free(bp);
  romsDir_ = Join(base_, "roms");
  coversDir_ = Join(base_, "covers");
  savesDir_ = Join(base_, "saves");
  statesDir_ = Join(base_, "states");
  coresDir_ = Join(base_, "cores");
  systemDir_ = Join(base_, "system");
  configPath_ = Join(Join(base_, "config"), "birdstation.ini");
  std::error_code ec;
  for (auto* d : {&romsDir_, &coversDir_, &savesDir_, &statesDir_, &coresDir_, &systemDir_})
    fs::create_directories(P(*d), ec);
  fs::create_directories(P(Join(base_, "config")), ec);
  lib_.SetCoversDir(coversDir_);
  cfg_.Load(configPath_);

  float ddpi = 96.f;
  if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0) ddpi = 96.f;
  dpi_ = std::clamp(ddpi / 96.f, 1.f, 3.f);

  win_ = SDL_CreateWindow("Birdstation", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                          int(1180 * dpi_), int(720 * dpi_), SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
  if (!win_) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Birdstation", SDL_GetError(), nullptr);
    return false;
  }
  ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
  if (!ren_) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Birdstation", SDL_GetError(), nullptr);
    return false;
  }
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
  logoTex_ = Library::LoadTexture(ren_, Join(Join(base_, "assets"), "birdstation.png"), nullptr, nullptr);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  const float fontPx = 17.f * dpi_;
  bool fontOk = false;
#ifdef _WIN32
  if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf", ec))
    fontOk = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", fontPx) != nullptr;
#endif
  if (!fontOk) {
    ImFontConfig fc;
    fc.SizePixels = fontPx;
    io.Fonts->AddFontDefault(&fc);
  }
  SetupStyle();
  ImGui_ImplSDL2_InitForSDLRenderer(win_, ren_);
  ImGui_ImplSDLRenderer2_Init(ren_);

  input_.Init();
  core_.SetInput([this](unsigned port, unsigned id) { return input_.State(port, id); });
  core_.SetAudio([this](const int16_t* s, size_t n) { QueueAudio(s, n); });

  Rescan();
  if (cfg_.fullscreen) SetFullscreen(true);
  if (argc > 1 && argv[1] && IsRomExt(P(argv[1]).extension().u8string())) startupRom_ = argv[1];
  return true;
}

void App::SetupStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  ImGui::StyleColorsDark(&s);
  s.WindowRounding = 8.f;
  s.ChildRounding = 8.f;
  s.FrameRounding = 6.f;
  s.PopupRounding = 6.f;
  s.GrabRounding = 6.f;
  s.ScrollbarRounding = 8.f;
  s.WindowPadding = ImVec2(12, 10);
  s.FramePadding = ImVec2(10, 6);
  s.ItemSpacing = ImVec2(10, 8);
  s.ScaleAllSizes(dpi_);
  ImVec4* c = s.Colors;
  const ImVec4 blue(0.27f, 0.47f, 0.82f, 1.f), blueHi(0.35f, 0.56f, 0.92f, 1.f);
  c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.082f, 0.105f, 1.f);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
  c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.15f, 0.19f, 1.f);
  c[ImGuiCol_Button] = ImVec4(0.17f, 0.19f, 0.25f, 1.f);
  c[ImGuiCol_ButtonHovered] = blue;
  c[ImGuiCol_ButtonActive] = blueHi;
  c[ImGuiCol_Header] = ImVec4(0.17f, 0.19f, 0.25f, 1.f);
  c[ImGuiCol_HeaderHovered] = blue;
  c[ImGuiCol_HeaderActive] = blueHi;
  c[ImGuiCol_SliderGrab] = blue;
  c[ImGuiCol_SliderGrabActive] = blueHi;
  c[ImGuiCol_CheckMark] = ImVec4(0.96f, 0.82f, 0.28f, 1.f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.20f, 1.f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.065f, 0.085f, 0.92f);
}

void App::Rescan() {
  std::vector<std::string> dirs{romsDir_};
  for (auto& d : cfg_.romDirs) dirs.push_back(d);
  lib_.Scan(dirs);
  selected_ = 0;
}

void App::SaveConfig() { cfg_.Save(configPath_); }

void App::Shutdown() {
  if (!win_) return;
  if (mode_ == Mode::Playing) Stop();
  SaveConfig();
  lib_.StopCoverDownload();
  lib_.FreeTextures();
  core_.Unload();
  CloseAudio();
  if (videoTex_) SDL_DestroyTexture(videoTex_);
  if (logoTex_) SDL_DestroyTexture(logoTex_);
  videoTex_ = logoTex_ = nullptr;
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(ren_);
  SDL_DestroyWindow(win_);
  win_ = nullptr;
  ren_ = nullptr;
#ifdef _WIN32
  OleUninitialize();
#endif
  SDL_Quit();
}

// ---- main loop -------------------------------------------------------------
void App::Run() {
  while (running_) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) HandleEvent(e);
    input_.Update();
    if (mode_ == Mode::Playing && !input_.Capturing() && !showControls_ && !showSettings_) {
      if (input_.ConsumeSaveTrigger()) SaveStateSlot();   // R2
      if (input_.ConsumeLoadTrigger()) LoadStateSlot();   // L2
    } else {
      input_.ConsumeSaveTrigger();
      input_.ConsumeLoadTrigger();
    }
    if (mode_ == Mode::Playing) RunEmulation();
    Render();
    if (!pendingRom_.empty()) {
      std::string p = std::move(pendingRom_);
      pendingRom_.clear();
      Launch(p);
    } else if (!startupRom_.empty()) {
      std::string p = std::move(startupRom_);
      startupRom_.clear();
      Launch(p);
    }
  }
}

void App::HandleEvent(const SDL_Event& e) {
  ImGui_ImplSDL2_ProcessEvent(&e);
  const bool wasCapturing = input_.Capturing();
  input_.HandleEvent(e);
  switch (e.type) {
    case SDL_QUIT:
      running_ = false;
      break;
    case SDL_MOUSEMOTION:
      lastMouseMove_ = SDL_GetTicks();
      break;
    case SDL_KEYDOWN:
      if (!wasCapturing && !e.key.repeat) OnKeyDown(e.key);
      break;
    case SDL_CONTROLLERBUTTONDOWN:
      if (mode_ == Mode::Playing && !wasCapturing && e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)
        menuPinned_ = !menuPinned_;
      break;
    case SDL_DROPFILE: {
      std::string f = e.drop.file;
      SDL_free(e.drop.file);
      std::string ext = P(f).extension().u8string();
      if (IsRomExt(ext)) {
        pendingRom_ = f;
      } else if (IsImageExt(ext) && mode_ == Mode::Library) {
        // dropping an image over the library sets the cover of the selected game
        auto& games = lib_.Games();
        std::string q = Lower(search_);
        int pos = 0;
        for (auto& g : games) {
          if (!q.empty() && Lower(g.title).find(q) == std::string::npos) continue;
          if (pos++ == selected_) {
            std::error_code ec;
            fs::create_directories(P(coversDir_), ec);
            for (auto* old : {".png", ".jpg", ".jpeg", ".bmp"}) fs::remove(P(coversDir_) / P(g.stem + old), ec);
            fs::copy_file(P(f), P(coversDir_) / P(g.stem + Lower(ext)), fs::copy_options::overwrite_existing, ec);
            if (g.cover) SDL_DestroyTexture(g.cover);
            g.cover = nullptr;
            g.coverTried = false;
            break;
          }
        }
      }
      break;
    }
    default: break;
  }
}

void App::OnKeyDown(const SDL_KeyboardEvent& k) {
  const SDL_Scancode sc = k.keysym.scancode;
  if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (k.keysym.mod & KMOD_ALT))) {
    SetFullscreen(!cfg_.fullscreen);
    return;
  }
  if (mode_ != Mode::Playing || ImGui::GetIO().WantTextInput) return;
  switch (sc) {
    case SDL_SCANCODE_F1: menuPinned_ = !menuPinned_; break;
    case SDL_SCANCODE_F5: SaveStateSlot(); break;
    case SDL_SCANCODE_F7: LoadStateSlot(); break;
    case SDL_SCANCODE_F2:
      cfg_.slot = (cfg_.slot + 9) % 10;
      Osd("Slot de estado: " + std::to_string(cfg_.slot + 1));
      break;
    case SDL_SCANCODE_F3:
      cfg_.slot = (cfg_.slot + 1) % 10;
      Osd("Slot de estado: " + std::to_string(cfg_.slot + 1));
      break;
    case SDL_SCANCODE_F10: core_.Reset(); Osd("Jogo reiniciado"); break;
    case SDL_SCANCODE_P: SetPaused(!paused_); break;
    case SDL_SCANCODE_ESCAPE:
      if (showControls_ || showSettings_) { showControls_ = showSettings_ = false; input_.CancelCapture(); }
      else Stop();
      break;
    default: break;
  }
}

void App::SetFullscreen(bool fsOn) {
  cfg_.fullscreen = fsOn;
  SDL_SetWindowFullscreen(win_, fsOn ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void App::SetPaused(bool p) {
  paused_ = p;
  if (audio_) SDL_PauseAudioDevice(audio_, p ? 1 : 0);
}

// ---- audio -----------------------------------------------------------------
bool App::EnsureAudio() {
  CloseAudio();
  SDL_AudioSpec want{}, have{};
  want.freq = int(core_.SampleRate() + 0.5);
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 512;
  audio_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!audio_) return false;
  SDL_PauseAudioDevice(audio_, 0);
  return true;
}

void App::CloseAudio() {
  if (audio_) SDL_CloseAudioDevice(audio_);
  audio_ = 0;
}

void App::QueueAudio(const int16_t* samples, size_t frames) {
  if (!audio_ || fastForward_ || paused_ || !frames) return;
  // never let the queue grow beyond ~300 ms
  if (SDL_GetQueuedAudioSize(audio_) > Uint32(core_.SampleRate() * 4 * 0.3)) return;
  const size_t n = frames * 2;
  if (cfg_.volume >= 100) {
    SDL_QueueAudio(audio_, samples, Uint32(n * sizeof(int16_t)));
    return;
  }
  abuf_.resize(n);
  for (size_t i = 0; i < n; ++i) abuf_[i] = int16_t(samples[i] * cfg_.volume / 100);
  SDL_QueueAudio(audio_, abuf_.data(), Uint32(n * sizeof(int16_t)));
}

// ---- game start / stop -----------------------------------------------------
std::string App::SramPath() const { return Join(savesDir_, cur_.stem + ".srm"); }

bool App::Launch(const std::string& romPath) {
  if (mode_ == Mode::Playing) Stop();
  auto fail = [&](const std::string& m) {
    error_ = m;
    openError_ = true;
    return false;
  };
  std::error_code ec;
  if (!fs::exists(P(romPath), ec)) return fail("ROM nao encontrada:\n" + romPath);
  if (!core_.IsLoaded()) {
    std::string corePath = Join(coresDir_, kCoreFile);
    if (!fs::exists(P(corePath), ec))
      return fail(std::string("O nucleo Snes9x nao foi encontrado:\n") + corePath +
                  "\n\nExecute scripts\\fetch-core.ps1 ou copie o arquivo " + kCoreFile +
                  " para a pasta cores\\ ao lado do Birdstation.exe.");
    std::string err;
    if (!core_.Load(corePath, err)) return fail(err);
  }
  std::string err;
  if (!core_.LoadGame(romPath, savesDir_, systemDir_, err)) return fail(err);

  cur_.path = romPath;
  cur_.stem = P(romPath).stem().u8string();
  cur_.title = cur_.stem;
  for (auto& g : lib_.Games())
    if (g.path == romPath) { cur_.title = g.title; break; }

  core_.LoadSram(SramPath());
  EnsureAudio();
  mode_ = Mode::Playing;
  paused_ = false;
  menuPinned_ = false;
  lastMouseMove_ = 0;
  lastSramSave_ = SDL_GetTicks();
  input_.ClearEdges();
  SDL_SetWindowTitle(win_, ("Birdstation - " + cur_.title).c_str());
  Osd("Slot de estado: " + std::to_string(cfg_.slot + 1) + "   (F5/R2 salva, F7/L2 carrega)");
  return true;
}

void App::Stop() {
  if (core_.GameLoaded()) {
    if (cfg_.autoSaveSram) core_.SaveSram(SramPath());
    core_.UnloadGame();
  }
  CloseAudio();
  if (videoTex_) SDL_DestroyTexture(videoTex_);
  videoTex_ = nullptr;
  videoW_ = videoH_ = 0;
  mode_ = Mode::Library;
  paused_ = false;
  SDL_ShowCursor(SDL_ENABLE);
  SDL_SetWindowTitle(win_, "Birdstation");
  SaveConfig();
}

void App::RunEmulation() {
  if (!core_.GameLoaded()) return;
  input_.ClearEdges();
  const Uint8* ks = SDL_GetKeyboardState(nullptr);
  fastForward_ = ks[SDL_SCANCODE_TAB] && !input_.Capturing();
  if (paused_) return;

  const double fps = core_.Fps();
  if (audio_) {
    // Audio-driven pacing: keep ~3 frames of sound queued.
    const Uint32 perFrame = Uint32(core_.SampleRate() / fps) * 4;
    const Uint32 target = perFrame * 3;
    const int maxRuns = fastForward_ ? 8 : 4;
    for (int i = 0; i < maxRuns; ++i) {
      if (!fastForward_ && SDL_GetQueuedAudioSize(audio_) >= target) break;
      core_.Run();
    }
  } else {
    static Uint64 last = SDL_GetPerformanceCounter();
    static double acc = 0;
    Uint64 now = SDL_GetPerformanceCounter();
    acc += double(now - last) / double(SDL_GetPerformanceFrequency());
    last = now;
    const double step = 1.0 / fps / (fastForward_ ? 4.0 : 1.0);
    int runs = 0;
    while (acc >= step && runs < 6) { core_.Run(); acc -= step; ++runs; }
    if (acc > 0.25) acc = 0;
  }

  if (cfg_.autoSaveSram && SDL_GetTicks() - lastSramSave_ > 30000) {
    core_.SaveSram(SramPath());
    lastSramSave_ = SDL_GetTicks();
  }
}

// ---- save states -----------------------------------------------------------
std::string App::StatePath(int slot) const {
  return Join(statesDir_, cur_.stem + ".state" + std::to_string(slot));
}

std::string App::SlotLabel(int slot) const {
  std::string s = "Slot " + std::to_string(slot + 1);
  std::error_code ec;
  fs::path p = P(StatePath(slot));
  if (!fs::exists(p, ec)) return s + "  (vazio)";
  auto ft = fs::last_write_time(p, ec);
  if (ec) return s;
  auto sctp = std::chrono::system_clock::now() +
              std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
  std::time_t t = std::chrono::system_clock::to_time_t(sctp);
  char buf[64];
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  std::strftime(buf, sizeof buf, "%d/%m %H:%M", &tmv);
  return s + "  (" + buf + ")";
}

void App::SaveStateSlot() {
  if (!core_.GameLoaded()) return;
  bool ok = core_.SaveState(StatePath(cfg_.slot));
  Osd(ok ? "Estado salvo no slot " + std::to_string(cfg_.slot + 1)
         : "Falha ao salvar o estado");
}

void App::LoadStateSlot() {
  if (!core_.GameLoaded()) return;
  std::error_code ec;
  if (!fs::exists(P(StatePath(cfg_.slot)), ec)) {
    Osd("Slot " + std::to_string(cfg_.slot + 1) + " esta vazio");
    return;
  }
  bool ok = core_.LoadState(StatePath(cfg_.slot));
  Osd(ok ? "Estado carregado do slot " + std::to_string(cfg_.slot + 1)
         : "Falha ao carregar o estado");
}

void App::Osd(const std::string& msg) {
  osd_ = msg;
  osdUntil_ = SDL_GetTicks() + 2200;
}

// ---- rendering -------------------------------------------------------------
void App::Render() {
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  SDL_SetRenderDrawColor(ren_, 12, 13, 18, 255);
  SDL_RenderClear(ren_);

  coverBudget_ = 3;
  lib_.PollReady();

  if (mode_ == Mode::Playing) {
    DrawVideo();
    DrawGameMenu();
    const bool hideCursor = cfg_.fullscreen && !menuPinned_ && !showControls_ && !showSettings_ &&
                            (SDL_GetTicks() - lastMouseMove_ > 2500);
    SDL_ShowCursor(hideCursor ? SDL_DISABLE : SDL_ENABLE);
  } else {
    DrawLibrary();
  }
  DrawControlsWindow();
  DrawSettingsWindow();
  DrawOverlays();

  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren_);
  SDL_RenderPresent(ren_);
}

void App::DrawVideo() {
  if (!core_.GameLoaded()) return;
  const int w = int(core_.Width()), h = int(core_.Height());
  bool fresh = false;
  if (!videoTex_ || videoW_ != w || videoH_ != h) {
    if (videoTex_) SDL_DestroyTexture(videoTex_);
    videoTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    videoW_ = w;
    videoH_ = h;
    fresh = true;
  }
  if (!videoTex_) return;
  SDL_SetTextureScaleMode(videoTex_, cfg_.linearFilter ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
  if ((core_.FrameChanged() || fresh) && core_.Pixels().size() >= size_t(w) * size_t(h)) {
    SDL_UpdateTexture(videoTex_, nullptr, core_.Pixels().data(), w * 4);
    core_.ClearFrameChanged();
  }
  int ow = 0, oh = 0;
  SDL_GetRendererOutputSize(ren_, &ow, &oh);
  const float aspect = core_.AspectRatio();
  float scale = std::min(float(ow) / (h * aspect), float(oh) / float(h));
  if (cfg_.integerScale && scale >= 1.f) scale = std::floor(scale);
  const float dw = h * aspect * scale, dh = h * scale;
  SDL_FRect dst{(ow - dw) * 0.5f, (oh - dh) * 0.5f, dw, dh};
  SDL_RenderCopyF(ren_, videoTex_, nullptr, &dst);
}

void App::DrawGameMenu() {
  const bool recentMouse = lastMouseMove_ && SDL_GetTicks() - lastMouseMove_ < 2500;
  if (!(menuPinned_ || recentMouse || showControls_ || showSettings_)) return;
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("Jogo")) {
    if (ImGui::MenuItem(paused_ ? "Continuar" : "Pausar", "P")) SetPaused(!paused_);
    if (ImGui::MenuItem("Reiniciar", "F10")) { core_.Reset(); Osd("Jogo reiniciado"); }
    ImGui::Separator();
    if (ImGui::MenuItem("Salvar estado", "F5")) SaveStateSlot();
    if (ImGui::MenuItem("Carregar estado", "F7")) LoadStateSlot();
    if (ImGui::BeginMenu("Slot do estado")) {
      for (int i = 0; i < 10; ++i)
        if (ImGui::MenuItem(SlotLabel(i).c_str(), i == 0 ? "F2/F3" : nullptr, cfg_.slot == i)) cfg_.slot = i;
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Parar e voltar a biblioteca", "Esc")) Stop();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Opcoes")) {
    bool fsOn = cfg_.fullscreen;
    if (ImGui::MenuItem("Tela cheia", "F11", &fsOn)) SetFullscreen(fsOn);
    ImGui::MenuItem("Filtro suave (bilinear)", nullptr, &cfg_.linearFilter);
    ImGui::MenuItem("Escala inteira", nullptr, &cfg_.integerScale);
    ImGui::MenuItem("Mostrar FPS", nullptr, &cfg_.showFps);
    ImGui::SetNextItemWidth(160 * dpi_);
    ImGui::SliderInt("Volume", &cfg_.volume, 0, 100);
    ImGui::Separator();
    if (ImGui::MenuItem("Controles...")) showControls_ = true;
    if (ImGui::MenuItem("Configuracoes...")) showSettings_ = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Atalhos")) {
    ImGui::TextUnformatted("F1  mostrar/ocultar menu (ou botao Guide)");
    ImGui::TextUnformatted("F5 / R2  salvar estado      F7 / L2  carregar estado");
    ImGui::TextUnformatted("F2 / F3  slot anterior / proximo");
    ImGui::TextUnformatted("F10  reiniciar         P  pausar");
    ImGui::TextUnformatted("Tab (segurar)  avanco rapido");
    ImGui::TextUnformatted("F11 ou Alt+Enter  tela cheia");
    ImGui::TextUnformatted("Esc  voltar a biblioteca");
    ImGui::EndMenu();
  }
  ImGui::SameLine(ImGui::GetWindowWidth() - 260 * dpi_);
  ImGui::TextDisabled("%s  |  slot %d", cur_.title.c_str(), cfg_.slot + 1);
  ImGui::EndMainMenuBar();
}

// ---- library screen --------------------------------------------------------
void App::DrawLibrary() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  const ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                              ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("##library", nullptr, wf);
  ImGuiIO& io = ImGui::GetIO();
  const float lineH = ImGui::GetFrameHeight();

  // ---- header ----
  if (logoTex_) {
    ImGui::Image((ImTextureID)logoTex_, ImVec2(lineH * 1.5f, lineH * 1.5f));
    ImGui::SameLine();
  }
  ImGui::SetWindowFontScale(1.45f);
  ImGui::TextColored(ImVec4(0.96f, 0.82f, 0.28f, 1.f), "Birdstation");
  ImGui::SetWindowFontScale(1.f);
  ImGui::SameLine(0, 24 * dpi_);
  ImGui::SetNextItemWidth(230 * dpi_);
  ImGui::InputTextWithHint("##search", "Buscar jogo...", search_, sizeof search_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120 * dpi_);
  ImGui::SliderInt("##size", &cfg_.coverSize, 110, 360, "Capas %d");
  ImGui::SameLine();
  if (lib_.Downloading()) {
    if (ImGui::Button("Cancelar")) lib_.StopCoverDownload();
    ImGui::SameLine();
    ImGui::TextDisabled("Baixando capas %d/%d (%d ok)", lib_.DlDone(), lib_.DlTotal(), lib_.DlFound());
  } else {
    if (ImGui::Button("Baixar capas")) lib_.StartCoverDownload();
  }
  ImGui::SameLine();
  if (ImGui::Button("Controles")) showControls_ = true;
  ImGui::SameLine();
  if (ImGui::Button("Configuracoes")) showSettings_ = true;
  ImGui::Separator();

  // ---- filter ----
  std::vector<int> idx;
  {
    const std::string q = Lower(search_);
    auto& games = lib_.Games();
    for (int i = 0; i < int(games.size()); ++i)
      if (q.empty() || Lower(games[i].title).find(q) != std::string::npos) idx.push_back(i);
  }
  const int n = int(idx.size());
  selected_ = n ? std::clamp(selected_, 0, n - 1) : 0;

  // ---- navigation (keyboard + gamepad) ----
  int launch = -1;
  if (n > 0 && !io.WantTextInput && !showControls_ && !showSettings_) {
    auto move = [&](int d) { selected_ = std::clamp(selected_ + d, 0, n - 1); scrollToSel_ = true; };
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) move(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) move(1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) move(-gridCols_);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) move(gridCols_);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) move(-gridCols_ * 3);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) move(gridCols_ * 3);
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) move(-n);
    if (ImGui::IsKeyPressed(ImGuiKey_End)) move(n);
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) launch = selected_;
    if (input_.PadPressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) move(-1);
    if (input_.PadPressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) move(1);
    if (input_.PadPressed(SDL_CONTROLLER_BUTTON_DPAD_UP)) move(-gridCols_);
    if (input_.PadPressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) move(gridCols_);
    if (input_.PadPressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) move(-gridCols_ * 3);
    if (input_.PadPressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) move(gridCols_ * 3);
    if (input_.PadPressed(SDL_CONTROLLER_BUTTON_A) || input_.PadPressed(SDL_CONTROLLER_BUTTON_START)) launch = selected_;
  }
  input_.ClearEdges();

  // ---- grid ----
  const float footerH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  ImGui::BeginChild("##grid", ImVec2(0, -footerH), ImGuiChildFlags_None);
  if (n == 0) {
    ImGui::Dummy(ImVec2(0, 60 * dpi_));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    if (lib_.Games().empty()) {
      ImGui::TextUnformatted("Nenhuma ROM encontrada.");
      ImGui::Spacing();
      ImGui::TextDisabled("Coloque suas ROMs (.sfc, .smc, .swc, .fig) na pasta:");
      ImGui::TextUnformatted(romsDir_.c_str());
      ImGui::TextDisabled("ou clique em Configuracoes > Adicionar pasta. Voce tambem pode arrastar uma ROM para esta janela.");
    } else {
      ImGui::TextDisabled("Nenhum jogo corresponde a busca.");
    }
    ImGui::PopTextWrapPos();
  } else {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float cellW = cfg_.coverSize * dpi_;
    const float imgH = cellW * 0.86f;
    const float textH = ImGui::GetFontSize() * 2.4f;
    const float cellH = imgH + textH + 6 * dpi_;
    const float gap = st.ItemSpacing.x * 1.6f;
    const float avail = ImGui::GetContentRegionAvail().x - st.ScrollbarSize;
    gridCols_ = std::max(1, int((avail + gap) / (cellW + gap)));
    const float used = gridCols_ * cellW + (gridCols_ - 1) * gap;
    const float x0 = std::max(0.f, (avail - used) * 0.5f);
    const float y0 = ImGui::GetCursorPosY() + 6 * dpi_;
    const int rows = (n + gridCols_ - 1) / gridCols_;

    if (scrollToSel_) {
      const float rowTop = y0 + (selected_ / gridCols_) * (cellH + gap);
      const float viewH = ImGui::GetWindowHeight();
      if (rowTop < ImGui::GetScrollY()) ImGui::SetScrollY(std::max(0.f, rowTop - gap));
      else if (rowTop + cellH > ImGui::GetScrollY() + viewH) ImGui::SetScrollY(rowTop + cellH - viewH + gap);
      scrollToSel_ = false;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    auto& games = lib_.Games();
    for (int p = 0; p < n; ++p) {
      Game& g = games[idx[p]];
      const int row = p / gridCols_, col = p % gridCols_;
      ImGui::SetCursorPos(ImVec2(x0 + col * (cellW + gap), y0 + row * (cellH + gap)));
      ImGui::PushID(p);
      ImGui::InvisibleButton("cell", ImVec2(cellW, cellH));
      const bool hovered = ImGui::IsItemHovered();
      if (ImGui::IsItemClicked(0)) selected_ = p;
      if (hovered && ImGui::IsMouseDoubleClicked(0)) launch = p;
      if (ImGui::BeginPopupContextItem("ctx")) {
        selected_ = p;
        if (ImGui::MenuItem("Jogar")) launch = p;
        ImGui::EndPopup();
      }
      ImGui::PopID();

      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      if (!ImGui::IsRectVisible(mn, mx)) continue;

      const bool sel = p == selected_;
      const float r = 8 * dpi_;
      dl->AddRectFilled(mn, mx, sel ? IM_COL32(40, 56, 92, 255) : (hovered ? IM_COL32(30, 34, 46, 255) : IM_COL32(21, 24, 33, 255)), r);

      if (!g.cover && coverBudget_ > 0) {
        if (!g.coverTried) --coverBudget_;
        lib_.LoadCover(g, ren_);
      }
      const ImVec2 areaMin(mn.x + 6 * dpi_, mn.y + 6 * dpi_), areaMax(mx.x - 6 * dpi_, mn.y + imgH);
      const float aw = areaMax.x - areaMin.x, ah = areaMax.y - areaMin.y;
      if (g.cover && g.cw > 0 && g.ch > 0) {
        const float s = std::min(aw / g.cw, ah / g.ch);
        const float iw = g.cw * s, ih = g.ch * s;
        const ImVec2 a(areaMin.x + (aw - iw) * 0.5f, areaMin.y + (ah - ih) * 0.5f);
        dl->AddImageRounded((ImTextureID)g.cover, a, ImVec2(a.x + iw, a.y + ih), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, 4 * dpi_);
      } else if (logoTex_) {
        const float s = std::min(aw, ah) * 0.62f;
        const ImVec2 a(areaMin.x + (aw - s) * 0.5f, areaMin.y + (ah - s) * 0.5f);
        dl->AddImage((ImTextureID)logoTex_, a, ImVec2(a.x + s, a.y + s), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 70));
      }

      const ImVec2 tp(mn.x + 8 * dpi_, mn.y + imgH + 4 * dpi_);
      dl->PushClipRect(tp, ImVec2(mx.x - 6 * dpi_, mx.y - 2 * dpi_), true);
      dl->AddText(font, ImGui::GetFontSize(), tp, sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(205, 210, 225, 255),
                  g.title.c_str(), nullptr, cellW - 16 * dpi_);
      dl->PopClipRect();
      if (sel) dl->AddRect(mn, mx, IM_COL32(245, 210, 70, 255), r, 0, 2.f * dpi_);
    }
    ImGui::SetCursorPos(ImVec2(0, y0 + rows * (cellH + gap)));
    ImGui::Dummy(ImVec2(1, 1));
  }
  ImGui::EndChild();

  // ---- footer ----
  ImGui::TextDisabled("%d jogo(s)   |   Setas/D-Pad: navegar   Enter/A: jogar   Arraste uma ROM ou imagem para a janela   |   %s",
                      int(lib_.Games().size()),
                      input_.NumPads() ? input_.PadName(0).c_str() : "sem gamepad");
  ImGui::End();

  if (launch >= 0 && launch < n) pendingRom_ = lib_.Games()[idx[launch]].path;
}

// ---- controls (remap) window -----------------------------------------------
void App::DrawControlsWindow() {
  if (!showControls_) return;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::Begin("Controles - remapeamento", &showControls_, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
    ImGui::TextUnformatted("Clique em um botao e pressione a tecla (ou botao do gamepad) desejada. Esc cancela.");
    ImGui::PopTextWrapPos();
    if (input_.NumPads() == 0) ImGui::TextDisabled("Nenhum gamepad detectado. Conecte um controle (XInput/DualShock/etc.).");
    for (int i = 0; i < input_.NumPads(); ++i)
      ImGui::TextDisabled("Jogador %d: %s", i + 1, input_.PadName(i).c_str());
    ImGui::Spacing();
    if (ImGui::BeginTable("map", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn("Botao SNES", ImGuiTableColumnFlags_WidthFixed, 110 * dpi_);
      ImGui::TableSetupColumn("Teclado (J1)", ImGuiTableColumnFlags_WidthFixed, 190 * dpi_);
      ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthFixed, 190 * dpi_);
      ImGui::TableHeadersRow();
      for (int i = 0; i < kNumButtons; ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kButtonNames[i]);
        for (int pad = 0; pad < 2; ++pad) {
          ImGui::TableNextColumn();
          ImGui::PushID(i * 2 + pad);
          const bool cap = input_.Capturing() && input_.CaptureButton() == i && input_.CaptureIsPad() == bool(pad);
          std::string label = cap ? "Pressione..." : (pad ? Input::PadButtonName(cfg_.pad[i]) : Input::KeyName(cfg_.kb[i]));
          if (cap) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.55f, 0.1f, 1.f));
          if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) input_.BeginCapture(i, pad != 0);
          if (cap) ImGui::PopStyleColor();
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("O analogico esquerdo tambem funciona como direcional. R2 salva e L2 carrega o estado (slot atual). O 2o gamepad controla o Jogador 2.");
    if (ImGui::Button("Restaurar padrao")) { input_.CancelCapture(); cfg_.ResetControls(); }
    ImGui::SameLine();
    if (ImGui::Button("Fechar")) showControls_ = false;
  }
  ImGui::End();
  if (!showControls_) { input_.CancelCapture(); SaveConfig(); }
}

// ---- settings window -------------------------------------------------------
void App::DrawSettingsWindow() {
  if (!showSettings_) return;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560 * dpi_, 0), ImGuiCond_Appearing);
  if (ImGui::Begin("Configuracoes", &showSettings_, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SeparatorText("Pastas de ROMs");
    ImGui::TextDisabled("Sempre incluida: %s", romsDir_.c_str());
    int removeIdx = -1;
    for (int i = 0; i < int(cfg_.romDirs.size()); ++i) {
      ImGui::PushID(i);
      if (ImGui::SmallButton("Remover")) removeIdx = i;
      ImGui::SameLine();
      ImGui::TextUnformatted(cfg_.romDirs[i].c_str());
      ImGui::PopID();
    }
    if (removeIdx >= 0) { cfg_.romDirs.erase(cfg_.romDirs.begin() + removeIdx); Rescan(); SaveConfig(); }
    if (ImGui::Button("Adicionar pasta...")) {
      std::string d = PickFolder();
      if (!d.empty() && std::find(cfg_.romDirs.begin(), cfg_.romDirs.end(), d) == cfg_.romDirs.end()) {
        cfg_.romDirs.push_back(d);
        Rescan();
        SaveConfig();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reescanear")) Rescan();

    ImGui::SeparatorText("Video e audio");
    bool fsOn = cfg_.fullscreen;
    if (ImGui::Checkbox("Tela cheia (F11)", &fsOn)) SetFullscreen(fsOn);
    ImGui::Checkbox("Filtro suave (bilinear)", &cfg_.linearFilter);
    ImGui::Checkbox("Escala inteira (pixels perfeitos)", &cfg_.integerScale);
    ImGui::Checkbox("Mostrar FPS", &cfg_.showFps);
    ImGui::SliderInt("Volume", &cfg_.volume, 0, 100);

    ImGui::SeparatorText("Saves");
    ImGui::Checkbox("Salvar a memoria da bateria (SRAM) automaticamente", &cfg_.autoSaveSram);
    ImGui::TextDisabled("SRAM: %s", savesDir_.c_str());
    ImGui::TextDisabled("Save states: %s", statesDir_.c_str());
    ImGui::TextDisabled("Capas: %s", coversDir_.c_str());

    ImGui::SeparatorText("Nucleo");
    std::error_code ec;
    const bool coreOk = fs::exists(P(Join(coresDir_, kCoreFile)), ec);
    if (core_.IsLoaded()) ImGui::Text("Carregado: %s", core_.LibraryName().c_str());
    else if (coreOk) ImGui::TextUnformatted("Snes9x encontrado (carrega ao abrir o primeiro jogo).");
    else ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s nao encontrado em cores\\ - rode scripts\\fetch-core.ps1", kCoreFile);

    ImGui::Spacing();
    if (ImGui::Button("Fechar")) showSettings_ = false;
  }
  ImGui::End();
  if (!showSettings_) SaveConfig();
}

// ---- overlays (OSD, FPS, errors) -------------------------------------------
void App::DrawOverlays() {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 sz = ImGui::GetIO().DisplaySize;
  ImFont* font = ImGui::GetFont();
  if (mode_ == Mode::Playing) {
    if (SDL_GetTicks() < osdUntil_) {
      const ImVec2 ts = ImGui::CalcTextSize(osd_.c_str());
      const ImVec2 p(16 * dpi_, sz.y - ts.y - 24 * dpi_);
      dl->AddRectFilled(ImVec2(p.x - 8 * dpi_, p.y - 5 * dpi_), ImVec2(p.x + ts.x + 8 * dpi_, p.y + ts.y + 5 * dpi_),
                        IM_COL32(0, 0, 0, 170), 6 * dpi_);
      dl->AddText(font, ImGui::GetFontSize(), p, IM_COL32(255, 255, 255, 255), osd_.c_str());
    }
    if (cfg_.showFps) {
      char b[32];
      snprintf(b, sizeof b, "%.0f FPS", ImGui::GetIO().Framerate);
      dl->AddText(font, ImGui::GetFontSize(), ImVec2(sz.x - 90 * dpi_, 36 * dpi_), IM_COL32(120, 255, 140, 255), b);
    }
    if (paused_) {
      const char* t = "PAUSADO";
      const ImVec2 ts = ImGui::CalcTextSize(t);
      dl->AddText(font, ImGui::GetFontSize() * 2.2f, ImVec2((sz.x - ts.x * 2.2f) * 0.5f, sz.y * 0.45f),
                  IM_COL32(255, 220, 80, 230), t);
    }
    if (fastForward_) dl->AddText(font, ImGui::GetFontSize(), ImVec2(sz.x - 120 * dpi_, sz.y - 40 * dpi_),
                                  IM_COL32(255, 220, 80, 255), ">> avanco rapido");
  }

  if (openError_) {
    ImGui::OpenPopup("Erro##err");
    openError_ = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Erro##err", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    ImGui::TextUnformatted(error_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120 * dpi_, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
