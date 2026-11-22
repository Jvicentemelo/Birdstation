// Birdstation - keyboard + SDL2 game controller input with remapping
#pragma once
#include <SDL.h>
#include <string>
#include <vector>
#include "config.h"

class Input {
public:
  explicit Input(Config& cfg) : cfg_(cfg) {}
  ~Input();

  void Init();                          // opens already-connected controllers
  void HandleEvent(const SDL_Event& e); // hot-plug + remap capture

  // libretro joypad state for a player (port 0 = keyboard + pad 1, port 1 = pad 2)
  int16_t State(unsigned port, unsigned id) const;

  // Remapping: next key / pad button pressed becomes the binding.
  void BeginCapture(int button, bool gamepad) { capBtn_ = button; capPad_ = gamepad; }
  void CancelCapture() { capBtn_ = -1; }
  bool Capturing() const { return capBtn_ >= 0; }
  int CaptureButton() const { return capBtn_; }
  bool CaptureIsPad() const { return capPad_; }

  int NumPads() const { return int(pads_.size()); }
  std::string PadName(int i) const;

  // Menu navigation helpers (library screen): returns true once per press.
  bool PadPressed(SDL_GameControllerButton b);
  bool AnyPadButton(SDL_GameControllerButton b) const;
  void ClearEdges() { pressed_.clear(); }

  static std::string KeyName(int scancode);
  static std::string PadButtonName(int button);

private:
  struct Pad { SDL_GameController* c; SDL_JoystickID id; };
  void Open(int deviceIndex);
  bool PadDown(const Pad& p, unsigned id) const;

  Config& cfg_;
  std::vector<Pad> pads_;
  int capBtn_ = -1;
  bool capPad_ = false;
  std::vector<SDL_GameControllerButton> pressed_;  // edge events since last PadPressed poll
};
