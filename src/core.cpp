#include "core.h"

#include <SDL.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

Core* Core::instance_ = nullptr;

// ---------------------------------------------------------------------------
template <typename T>
bool Core::Sym(T& fn, const char* name) {
  fn = reinterpret_cast<T>(SDL_LoadFunction(lib_, name));
  return fn != nullptr;
}

Core::~Core() { Unload(); }

bool Core::Load(const std::string& libPath, std::string& err) {
  Unload();
  lib_ = SDL_LoadObject(libPath.c_str());
  if (!lib_) {
    err = "Nao foi possivel carregar o nucleo:\n" + libPath + "\n" + SDL_GetError();
    return false;
  }
  bool ok = Sym(retro_init_, "retro_init") && Sym(retro_deinit_, "retro_deinit") &&
            Sym(retro_api_version_, "retro_api_version") &&
            Sym(retro_get_system_info_, "retro_get_system_info") &&
            Sym(retro_get_system_av_info_, "retro_get_system_av_info") &&
            Sym(retro_set_environment_, "retro_set_environment") &&
            Sym(retro_set_video_refresh_, "retro_set_video_refresh") &&
            Sym(retro_set_audio_sample_, "retro_set_audio_sample") &&
            Sym(retro_set_audio_sample_batch_, "retro_set_audio_sample_batch") &&
            Sym(retro_set_input_poll_, "retro_set_input_poll") &&
            Sym(retro_set_input_state_, "retro_set_input_state") &&
            Sym(retro_set_controller_port_device_, "retro_set_controller_port_device") &&
            Sym(retro_reset_, "retro_reset") && Sym(retro_run_, "retro_run") &&
            Sym(retro_serialize_size_, "retro_serialize_size") &&
            Sym(retro_serialize_, "retro_serialize") &&
            Sym(retro_unserialize_, "retro_unserialize") &&
            Sym(retro_load_game_, "retro_load_game") &&
            Sym(retro_unload_game_, "retro_unload_game") &&
            Sym(retro_get_memory_data_, "retro_get_memory_data") &&
            Sym(retro_get_memory_size_, "retro_get_memory_size");
  if (!ok) {
    err = "O arquivo nao e um nucleo libretro valido:\n" + libPath;
    Unload();
    return false;
  }
  if (retro_api_version_() != RETRO_API_VERSION) {
    err = "Versao da API libretro incompativel.";
    Unload();
    return false;
  }
  instance_ = this;
  retro_set_environment_(&Core::EnvCb);
  retro_init_();
  retro_set_video_refresh_(&Core::VideoCb);
  retro_set_audio_sample_(&Core::AudioSampleCb);
  retro_set_audio_sample_batch_(&Core::AudioBatchCb);
  retro_set_input_poll_(&Core::PollCb);
  retro_set_input_state_(&Core::InputCb);

  retro_system_info si{};
  retro_get_system_info_(&si);
  libName_ = std::string(si.library_name ? si.library_name : "core") + " " +
             (si.library_version ? si.library_version : "");
  needFullPath_ = si.need_fullpath;
  return true;
}

void Core::Unload() {
  if (gameLoaded_) UnloadGame();
  if (lib_) {
    if (retro_deinit_) retro_deinit_();
    SDL_UnloadObject(lib_);
    lib_ = nullptr;
  }
  if (instance_ == this) instance_ = nullptr;
}

bool Core::LoadGame(const std::string& romPath, const std::string& saveDir,
                    const std::string& systemDir, std::string& err) {
  if (!lib_) { err = "Nucleo nao carregado."; return false; }
  if (gameLoaded_) UnloadGame();
  saveDir_ = saveDir;
  systemDir_ = systemDir;
  instance_ = this;

  retro_game_info gi{};
  gi.path = romPath.c_str();
  if (!needFullPath_) {
    std::ifstream f(fs::u8path(romPath), std::ios::binary);
    if (!f) { err = "Nao foi possivel abrir a ROM:\n" + romPath; return false; }
    romData_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    gi.data = romData_.data();
    gi.size = romData_.size();
  }
  retro_set_controller_port_device_(0, RETRO_DEVICE_JOYPAD);
  retro_set_controller_port_device_(1, RETRO_DEVICE_JOYPAD);

  if (!retro_load_game_(&gi)) {
    err = "O nucleo recusou esta ROM:\n" + romPath;
    romData_.clear();
    return false;
  }
  gameLoaded_ = true;

  retro_system_av_info av{};
  retro_get_system_av_info_(&av);
  fps_ = av.timing.fps;
  sampleRate_ = av.timing.sample_rate;
  width_ = av.geometry.base_width ? av.geometry.base_width : 256;
  height_ = av.geometry.base_height ? av.geometry.base_height : 224;
  aspect_ = av.geometry.aspect_ratio;
  pixels_.assign(size_t(width_) * height_, 0xFF000000u);
  frameChanged_ = true;
  return true;
}

void Core::UnloadGame() {
  if (gameLoaded_ && retro_unload_game_) retro_unload_game_();
  gameLoaded_ = false;
  romData_.clear();
  romData_.shrink_to_fit();
}

void Core::Run() {
  if (gameLoaded_) retro_run_();
}

void Core::Reset() {
  if (gameLoaded_) retro_reset_();
}

float Core::AspectRatio() const {
  if (aspect_ > 0.1f) return aspect_;
  return height_ ? float(width_) / float(height_) : 4.f / 3.f;
}

// ---- save states / SRAM ----------------------------------------------------
bool Core::SaveState(const std::string& path) {
  if (!gameLoaded_) return false;
  size_t n = retro_serialize_size_();
  if (!n) return false;
  std::vector<uint8_t> buf(n);
  if (!retro_serialize_(buf.data(), n)) return false;
  std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(n));
  return bool(f);
}

bool Core::LoadState(const std::string& path) {
  if (!gameLoaded_) return false;
  std::ifstream f(fs::u8path(path), std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (buf.empty()) return false;
  return retro_unserialize_(buf.data(), buf.size());
}

bool Core::SaveSram(const std::string& path) {
  if (!gameLoaded_) return false;
  size_t n = retro_get_memory_size_(RETRO_MEMORY_SAVE_RAM);
  void* p = retro_get_memory_data_(RETRO_MEMORY_SAVE_RAM);
  if (!n || !p) return true;  // cartridge without battery
  std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(static_cast<const char*>(p), std::streamsize(n));
  return bool(f);
}

bool Core::LoadSram(const std::string& path) {
  if (!gameLoaded_) return false;
  size_t n = retro_get_memory_size_(RETRO_MEMORY_SAVE_RAM);
  void* p = retro_get_memory_data_(RETRO_MEMORY_SAVE_RAM);
  if (!n || !p) return false;
  std::ifstream f(fs::u8path(path), std::ios::binary);
  if (!f) return false;
  f.read(static_cast<char*>(p), std::streamsize(n));
  return true;
}

// ---- callbacks -------------------------------------------------------------
static void RETRO_CALLCONV LogCb(enum retro_log_level level, const char* fmt, ...) {
  if (level < RETRO_LOG_WARN) return;
  va_list ap;
  va_start(ap, fmt);
  char buf[1024];
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  SDL_Log("[core] %s", buf);
}

bool RETRO_CALLCONV Core::EnvCb(unsigned cmd, void* data) {
  return instance_ ? instance_->Environment(cmd, data) : false;
}

bool Core::Environment(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *static_cast<bool*>(data) = true;
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      auto fmt = *static_cast<const retro_pixel_format*>(data);
      if (fmt == RETRO_PIXEL_FORMAT_XRGB8888 || fmt == RETRO_PIXEL_FORMAT_RGB565 ||
          fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
        pixFmt_ = fmt;
        return true;
      }
      return false;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *static_cast<const char**>(data) = systemDir_.c_str();
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *static_cast<const char**>(data) = saveDir_.c_str();
      return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      static_cast<retro_log_callback*>(data)->log = &LogCb;
      return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_ROTATION:
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *static_cast<unsigned*>(data) = 0;  // legacy variables only; core uses defaults
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *static_cast<bool*>(data) = false;
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
      auto* g = static_cast<const retro_game_geometry*>(data);
      aspect_ = g->aspect_ratio;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
      auto* av = static_cast<const retro_system_av_info*>(data);
      fps_ = av->timing.fps;
      sampleRate_ = av->timing.sample_rate;
      aspect_ = av->geometry.aspect_ratio;
      return true;
    }
    default:
      return false;  // GET_VARIABLE etc.: let the core use its default options
  }
}

void RETRO_CALLCONV Core::VideoCb(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (instance_) instance_->OnVideo(data, w, h, pitch);
}

void Core::OnVideo(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (!data || !w || !h) return;  // duplicated frame: keep the previous image
  width_ = w;
  height_ = h;
  pixels_.resize(size_t(w) * h);
  const auto* src = static_cast<const uint8_t*>(data);
  for (unsigned y = 0; y < h; ++y) {
    uint32_t* dst = &pixels_[size_t(y) * w];
    const uint8_t* row = src + y * pitch;
    switch (pixFmt_) {
      case RETRO_PIXEL_FORMAT_XRGB8888: {
        const uint32_t* s = reinterpret_cast<const uint32_t*>(row);
        for (unsigned x = 0; x < w; ++x) dst[x] = s[x] | 0xFF000000u;
        break;
      }
      case RETRO_PIXEL_FORMAT_RGB565: {
        const uint16_t* s = reinterpret_cast<const uint16_t*>(row);
        for (unsigned x = 0; x < w; ++x) {
          uint32_t p = s[x];
          uint32_t r = ((p >> 11) & 31) * 255 / 31;
          uint32_t g = ((p >> 5) & 63) * 255 / 63;
          uint32_t b = (p & 31) * 255 / 31;
          dst[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        break;
      }
      default: {  // 0RGB1555
        const uint16_t* s = reinterpret_cast<const uint16_t*>(row);
        for (unsigned x = 0; x < w; ++x) {
          uint32_t p = s[x];
          uint32_t r = ((p >> 10) & 31) * 255 / 31;
          uint32_t g = ((p >> 5) & 31) * 255 / 31;
          uint32_t b = (p & 31) * 255 / 31;
          dst[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
      }
    }
  }
  frameChanged_ = true;
}

void RETRO_CALLCONV Core::AudioSampleCb(int16_t l, int16_t r) {
  if (!instance_ || !instance_->audio_) return;
  int16_t s[2] = {l, r};
  instance_->audio_(s, 1);
}

size_t RETRO_CALLCONV Core::AudioBatchCb(const int16_t* data, size_t frames) {
  if (instance_ && instance_->audio_) instance_->audio_(data, frames);
  return frames;
}

void RETRO_CALLCONV Core::PollCb() {}

int16_t RETRO_CALLCONV Core::InputCb(unsigned port, unsigned device, unsigned /*index*/, unsigned id) {
  if (!instance_ || !instance_->input_ || device != RETRO_DEVICE_JOYPAD) return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
    int16_t mask = 0;
    for (unsigned i = 0; i < 16; ++i)
      if (instance_->input_(port, i)) mask |= int16_t(1 << i);
    return mask;
  }
  return instance_->input_(port, id);
}
