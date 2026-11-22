#include "input.h"

#include <algorithm>

Input::~Input() {
  for (auto& p : pads_) SDL_GameControllerClose(p.c);
}

void Input::Init() {
  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i)) Open(i);
}

void Input::Open(int idx) {
  SDL_GameController* c = SDL_GameControllerOpen(idx);
  if (!c) return;
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
  for (auto& p : pads_)
    if (p.id == id) { SDL_GameControllerClose(c); return; }
  pads_.push_back({c, id});
}

void Input::HandleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_CONTROLLERDEVICEADDED:
      Open(e.cdevice.which);
      break;
    case SDL_CONTROLLERDEVICEREMOVED:
      for (auto it = pads_.begin(); it != pads_.end(); ++it)
        if (it->id == e.cdevice.which) { SDL_GameControllerClose(it->c); pads_.erase(it); break; }
      break;
    case SDL_KEYDOWN:
      if (capBtn_ >= 0 && !capPad_ && !e.key.repeat) {
        if (e.key.keysym.scancode != SDL_SCANCODE_ESCAPE)
          cfg_.kb[capBtn_] = e.key.keysym.scancode;
        capBtn_ = -1;
      }
      break;
    case SDL_CONTROLLERBUTTONDOWN:
      if (capBtn_ >= 0 && capPad_) {
        cfg_.pad[capBtn_] = e.cbutton.button;
        capBtn_ = -1;
      } else {
        pressed_.push_back(SDL_GameControllerButton(e.cbutton.button));
      }
      break;
    default: break;
  }
}

bool Input::PadDown(const Pad& p, unsigned id) const {
  if (id >= unsigned(kNumButtons)) return false;
  if (SDL_GameControllerGetButton(p.c, SDL_GameControllerButton(cfg_.pad[id]))) return true;
  // left stick acts as the D-pad
  const int dz = 16000;
  int x = SDL_GameControllerGetAxis(p.c, SDL_CONTROLLER_AXIS_LEFTX);
  int y = SDL_GameControllerGetAxis(p.c, SDL_CONTROLLER_AXIS_LEFTY);
  switch (id) {
    case 4: return y < -dz;  // up
    case 5: return y > dz;   // down
    case 6: return x < -dz;  // left
    case 7: return x > dz;   // right
  }
  return false;
}

int16_t Input::State(unsigned port, unsigned id) const {
  if (id >= unsigned(kNumButtons)) return 0;
  if (port == 0 && !Capturing()) {
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    if (ks[cfg_.kb[id]]) return 1;
  }
  if (Capturing()) return 0;
  if (port < pads_.size() && PadDown(pads_[port], id)) return 1;
  return 0;
}

std::string Input::PadName(int i) const {
  if (i < 0 || i >= int(pads_.size())) return "";
  const char* n = SDL_GameControllerName(pads_[i].c);
  return n ? n : "Gamepad";
}

bool Input::PadPressed(SDL_GameControllerButton b) {
  auto it = std::find(pressed_.begin(), pressed_.end(), b);
  if (it == pressed_.end()) return false;
  pressed_.erase(it);
  return true;
}

bool Input::AnyPadButton(SDL_GameControllerButton b) const {
  for (auto& p : pads_)
    if (SDL_GameControllerGetButton(p.c, b)) return true;
  return false;
}

std::string Input::KeyName(int sc) {
  const char* n = SDL_GetScancodeName(SDL_Scancode(sc));
  return (n && *n) ? n : "?";
}

std::string Input::PadButtonName(int b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return "A (baixo)";
    case SDL_CONTROLLER_BUTTON_B: return "B (direita)";
    case SDL_CONTROLLER_BUTTON_X: return "X (esquerda)";
    case SDL_CONTROLLER_BUTTON_Y: return "Y (cima)";
    case SDL_CONTROLLER_BUTTON_BACK: return "Back/Select";
    case SDL_CONTROLLER_BUTTON_GUIDE: return "Guide";
    case SDL_CONTROLLER_BUTTON_START: return "Start";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "D-Pad cima";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "D-Pad baixo";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "D-Pad esquerda";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-Pad direita";
    default: return "Botao " + std::to_string(b);
  }
}
