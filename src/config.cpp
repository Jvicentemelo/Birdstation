#include "config.h"

#include <SDL.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

const char* const kButtonNames[kNumButtons] = {"B", "Y",    "Select", "Start", "Cima", "Baixo",
                                               "Esquerda", "Direita", "A",   "X",     "L",  "R"};

void Config::ResetControls() {
  static const int kbDef[kNumButtons] = {
      SDL_SCANCODE_Z,    SDL_SCANCODE_A,    SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RETURN,
      SDL_SCANCODE_UP,   SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,   SDL_SCANCODE_RIGHT,
      SDL_SCANCODE_X,    SDL_SCANCODE_S,    SDL_SCANCODE_Q,      SDL_SCANCODE_W};
  // SNES layout mapped onto an Xbox-style pad (SDL: A = bottom, B = right, X = left, Y = top)
  static const int padDef[kNumButtons] = {
      SDL_CONTROLLER_BUTTON_A,         SDL_CONTROLLER_BUTTON_X,
      SDL_CONTROLLER_BUTTON_BACK,      SDL_CONTROLLER_BUTTON_START,
      SDL_CONTROLLER_BUTTON_DPAD_UP,   SDL_CONTROLLER_BUTTON_DPAD_DOWN,
      SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
      SDL_CONTROLLER_BUTTON_B,         SDL_CONTROLLER_BUTTON_Y,
      SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
  std::copy(kbDef, kbDef + kNumButtons, kb);
  std::copy(padDef, padDef + kNumButtons, pad);
}

void Config::SetDefaults() {
  fullscreen = integerScale = showFps = false;
  linearFilter = true;
  autoSaveSram = true;
  volume = 100;
  coverSize = 190;
  slot = 0;
  romDirs.clear();
  ResetControls();
}

static std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool Config::Load(const std::string& path) {
  std::ifstream f(path);
  if (!f) return false;
  std::string line;
  bool firstDirs = true;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
    int n = std::atoi(v.c_str());
    if (k == "rom_dir") { if (firstDirs) { romDirs.clear(); firstDirs = false; } if (!v.empty()) romDirs.push_back(v); }
    else if (k == "fullscreen") fullscreen = n != 0;
    else if (k == "linear_filter") linearFilter = n != 0;
    else if (k == "integer_scale") integerScale = n != 0;
    else if (k == "show_fps") showFps = n != 0;
    else if (k == "auto_save_sram") autoSaveSram = n != 0;
    else if (k == "volume") volume = std::clamp(n, 0, 100);
    else if (k == "cover_size") coverSize = std::clamp(n, 100, 400);
    else if (k == "slot") slot = std::clamp(n, 0, 9);
    else if (k.rfind("kb.", 0) == 0 || k.rfind("pad.", 0) == 0) {
      bool isKb = k[0] == 'k';
      std::string name = k.substr(isKb ? 3 : 4);
      for (int i = 0; i < kNumButtons; ++i)
        if (name == kButtonNames[i]) (isKb ? kb : pad)[i] = n;
    }
  }
  return true;
}

bool Config::Save(const std::string& path) const {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << "# Birdstation settings\n";
  for (auto& d : romDirs) f << "rom_dir=" << d << "\n";
  f << "fullscreen=" << fullscreen << "\nlinear_filter=" << linearFilter
    << "\ninteger_scale=" << integerScale << "\nshow_fps=" << showFps
    << "\nauto_save_sram=" << autoSaveSram << "\nvolume=" << volume
    << "\ncover_size=" << coverSize << "\nslot=" << slot << "\n";
  f << "# kb.* = SDL scancode, pad.* = SDL game controller button\n";
  for (int i = 0; i < kNumButtons; ++i) f << "kb." << kButtonNames[i] << "=" << kb[i] << "\n";
  for (int i = 0; i < kNumButtons; ++i) f << "pad." << kButtonNames[i] << "=" << pad[i] << "\n";
  return bool(f);
}
