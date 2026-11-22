#include "covermatch.h"

#include <algorithm>
#include <cctype>

namespace covermatch {

static std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

static std::string HtmlUnescape(std::string s) {
  struct R { const char* a; const char* b; };
  static const R rs[] = {{"&amp;", "&"}, {"&#39;", "'"}, {"&apos;", "'"}, {"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"}};
  for (auto& r : rs) {
    size_t p = 0;
    std::string a = r.a;
    while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), r.b); ++p; }
  }
  return s;
}

static std::string UrlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
      out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else if (s[i] == '+') {
      out += '+';
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string Key(const std::string& name) {
  size_t cut = name.find_first_of("([");
  std::string t = cut == std::string::npos ? name : name.substr(0, cut);
  std::string out, tok;
  auto flush = [&] {
    if (!tok.empty() && tok != "the" && tok != "and") { if (!out.empty()) out += ' '; out += tok; }
    tok.clear();
  };
  for (unsigned char c : t) {
    if (std::isalnum(c)) tok += char(std::tolower(c));
    else if (c >= 0x80) tok += char(c);  // keep UTF-8 bytes (accents)
    else flush();
  }
  flush();
  return out;
}

std::vector<Entry> ParseIndex(const std::string& html) {
  std::vector<Entry> v;
  size_t p = 0;
  while ((p = html.find("href=\"", p)) != std::string::npos) {
    p += 6;
    size_t q = html.find('"', p);
    if (q == std::string::npos) break;
    std::string href = HtmlUnescape(html.substr(p, q - p));
    p = q;
    if (href.size() < 5 || Lower(href.substr(href.size() - 4)) != ".png") continue;
    if (href.find('/') != std::string::npos && href.back() == '/') continue;
    Entry e;
    e.href = href;
    std::string dec = UrlDecode(href);
    e.name = dec.substr(0, dec.size() - 4);
    e.key = Key(e.name);
    v.push_back(std::move(e));
  }
  return v;
}

static int RegionOf(const std::string& tags) {  // 0 USA, 1 World, 2 Europe, 3 Japan, 4 other
  std::string t = Lower(tags);
  if (t.find("usa") != std::string::npos) return 0;
  if (t.find("world") != std::string::npos) return 1;
  if (t.find("europe") != std::string::npos) return 2;
  if (t.find("japan") != std::string::npos) return 3;
  return 4;
}

// Region hint from a ROM name: "(USA)", "(U)", "[U]", "(Europe)", "(E)", "(J)", ...
static int HintOf(const std::string& stem) {
  std::string t = Lower(stem);
  auto has = [&](const char* s) { return t.find(s) != std::string::npos; };
  if (has("(usa") || has("(u)") || has("[u]") || has("(u ") || has("(us)")) return 0;
  if (has("(world")) return 1;
  if (has("(europe") || has("(e)") || has("[e]") || has("(eur") || has("(pal")) return 2;
  if (has("(japan") || has("(j)") || has("[j]") || has("(jp")) return 3;
  return -1;
}

static int Penalty(const std::string& name) {
  std::string t = Lower(name);
  int p = 0;
  for (const char* bad : {"(beta", "(proto", "(demo", "(sample", "(unl", "(pirate", "(kiosk", "(promo", "(possible", "(sgb"})
    if (t.find(bad) != std::string::npos) p += 20;
  if (t.find("(rev") != std::string::npos) p += 2;
  if (t.find("(v1.") != std::string::npos) p += 2;
  return p;
}

int Pick(const std::vector<Entry>& entries, const std::string& romStem) {
  // drop "[...]" dump tags: "Game (U) [!]" -> "Game (U)"
  std::string clean;
  int depth = 0;
  for (char c : romStem) {
    if (c == '[') { ++depth; continue; }
    if (c == ']') { if (depth) --depth; continue; }
    if (!depth) clean += c;
  }
  while (!clean.empty() && clean.back() == ' ') clean.pop_back();
  const std::string cleanLow = Lower(clean);
  const std::string key = Key(romStem);
  if (key.empty()) return -1;
  const int hint = HintOf(romStem);

  int best = -1, bestScore = 1 << 30;
  for (int i = 0; i < int(entries.size()); ++i) {
    const Entry& e = entries[i];
    if (Lower(e.name) == cleanLow) return i;  // exact
    if (e.key != key) continue;
    size_t par = e.name.find('(');
    int region = par == std::string::npos ? 4 : RegionOf(e.name.substr(par));
    int score = Penalty(e.name) + (hint >= 0 && region == hint ? 0 : 1 + region) * 3;
    if (score < bestScore) { bestScore = score; best = i; }
  }
  return best;
}

std::string UrlName(std::string name) {
  for (char& c : name)
    if (std::string("&*/:`<>?\\|\"").find(c) != std::string::npos) c = '_';
  std::string out;
  static const char* hex = "0123456789ABCDEF";
  for (unsigned char c : name) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '(' || c == ')' || c == '!' || c == ',' || c == '\'')
      out += char(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

}  // namespace covermatch
