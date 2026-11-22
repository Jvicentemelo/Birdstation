// Birdstation - portable settings (config/birdstation.ini next to the exe)
#pragma once
#include <string>
#include <vector>

constexpr int kNumButtons = 12;  // same order as libretro RETRO_DEVICE_ID_JOYPAD_*
extern const char* const kButtonNames[kNumButtons];

struct Config {
  std::vector<std::string> romDirs;  // extra ROM folders (roms/ next to the exe is always scanned)
  bool fullscreen = false;
  bool linearFilter = true;
  bool integerScale = false;
  bool showFps = false;
  bool autoSaveSram = true;
  int volume = 100;      // 0..100
  int coverSize = 190;   // pixels
  int slot = 0;          // active save-state slot 0..9
  int kb[kNumButtons];   // SDL_Scancode per button (player 1 keyboard)
  int pad[kNumButtons];  // SDL_GameControllerButton per button (all gamepads)

  Config() { SetDefaults(); }
  void SetDefaults();
  void ResetControls();
  bool Load(const std::string& path);
  bool Save(const std::string& path) const;
};
