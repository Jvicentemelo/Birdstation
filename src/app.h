// Birdstation - application (window, library screen, emulation screen, menus)
#pragma once
#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "core.h"
#include "input.h"
#include "library.h"

class App {
public:
  App();
  ~App();
  bool Init(int argc, char** argv);
  void Run();
  void Shutdown();

private:
  enum class Mode { Library, Playing };
  struct Running { std::string path, stem, title; };

  // setup
  void SetupStyle();
  void Rescan();
  void SaveConfig();

  // events / emulation
  void HandleEvent(const SDL_Event& e);
  void OnKeyDown(const SDL_KeyboardEvent& k);
  void RunEmulation();
  bool Launch(const std::string& romPath);
  void Stop();
  bool EnsureAudio();
  void CloseAudio();
  void QueueAudio(const int16_t* samples, size_t frames);
  void SetFullscreen(bool fs);
  void SetPaused(bool p);

  // save states
  std::string StatePath(int slot) const;
  std::string SlotLabel(int slot) const;
  void SaveStateSlot();
  void LoadStateSlot();
  void Osd(const std::string& msg);
  std::string SramPath() const;

  // drawing
  void Render();
  void DrawVideo();
  void DrawLibrary();
  void DrawGameMenu();
  void DrawControlsWindow();
  void DrawSettingsWindow();
  void DrawOverlays();
  

  // paths (portable layout: everything lives next to Birdstation.exe)
  std::string base_, romsDir_, coversDir_, savesDir_, statesDir_, coresDir_, systemDir_, configPath_;

  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture* videoTex_ = nullptr;
  SDL_Texture* logoTex_ = nullptr;
  int videoW_ = 0, videoH_ = 0;
  SDL_AudioDeviceID audio_ = 0;
  float dpi_ = 1.f;

  Config cfg_;
  Input input_;
  Library lib_;
  Core core_;

  Mode mode_ = Mode::Library;
  Running cur_;
  bool running_ = true;
  bool paused_ = false;
  bool fastForward_ = false;
  bool menuPinned_ = false;
  bool showControls_ = false;
  bool showSettings_ = false;
  Uint32 lastMouseMove_ = 0;
  Uint32 lastSramSave_ = 0;
  std::string osd_;
  Uint32 osdUntil_ = 0;
  std::string error_;
  bool openError_ = false;

  // library UI state
  char search_[128] = {0};
  int selected_ = 0;       // index in the filtered list
  int gridCols_ = 1;
  bool scrollToSel_ = false;
  int coverBudget_ = 0;
  std::string startupRom_;   // ROM passed on the command line
  std::string pendingRom_;   // ROM to launch at the end of the frame
  std::vector<int16_t> abuf_;
};
