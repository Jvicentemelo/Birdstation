// Birdstation - libretro core host (loads snes9x_libretro.dll at run time)
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "libretro.h"

class Core {
public:
  using InputFn = std::function<int16_t(unsigned port, unsigned id)>;
  using AudioFn = std::function<void(const int16_t* interleavedStereo, size_t frames)>;

  Core() = default;
  ~Core();
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  // Loads the core library (snes9x_libretro.dll).
  bool Load(const std::string& libPath, std::string& err);
  // Loads a ROM. saveDir / systemDir are handed to the core.
  bool LoadGame(const std::string& romPath, const std::string& saveDir,
                const std::string& systemDir, std::string& err);
  void UnloadGame();
  void Unload();

  bool IsLoaded() const { return lib_ != nullptr; }
  bool GameLoaded() const { return gameLoaded_; }

  void Run();     // emulates exactly one frame
  void Reset();

  bool SaveState(const std::string& path);
  bool LoadState(const std::string& path);
  bool SaveSram(const std::string& path);
  bool LoadSram(const std::string& path);

  void SetInput(InputFn fn) { input_ = std::move(fn); }
  void SetAudio(AudioFn fn) { audio_ = std::move(fn); }

  // Latest video frame, converted to 0xAARRGGBB (XRGB8888).
  const std::vector<uint32_t>& Pixels() const { return pixels_; }
  unsigned Width() const { return width_; }
  unsigned Height() const { return height_; }
  bool FrameChanged() const { return frameChanged_; }
  void ClearFrameChanged() { frameChanged_ = false; }
  float AspectRatio() const;
  double Fps() const { return fps_ > 1.0 ? fps_ : 60.0988; }
  double SampleRate() const { return sampleRate_ > 1.0 ? sampleRate_ : 32040.0; }
  const std::string& LibraryName() const { return libName_; }

private:
  // libretro callbacks (static trampolines -> instance_)
  static bool RETRO_CALLCONV EnvCb(unsigned cmd, void* data);
  static void RETRO_CALLCONV VideoCb(const void* data, unsigned w, unsigned h, size_t pitch);
  static void RETRO_CALLCONV AudioSampleCb(int16_t l, int16_t r);
  static size_t RETRO_CALLCONV AudioBatchCb(const int16_t* data, size_t frames);
  static void RETRO_CALLCONV PollCb();
  static int16_t RETRO_CALLCONV InputCb(unsigned port, unsigned device, unsigned index, unsigned id);

  bool Environment(unsigned cmd, void* data);
  void OnVideo(const void* data, unsigned w, unsigned h, size_t pitch);
  template <typename T> bool Sym(T& fn, const char* name);

  static Core* instance_;

  void* lib_ = nullptr;
  bool gameLoaded_ = false;
  std::string libName_;
  std::string saveDir_, systemDir_;

  // libretro entry points
  void (*retro_init_)() = nullptr;
  void (*retro_deinit_)() = nullptr;
  unsigned (*retro_api_version_)() = nullptr;
  void (*retro_get_system_info_)(retro_system_info*) = nullptr;
  void (*retro_get_system_av_info_)(retro_system_av_info*) = nullptr;
  void (*retro_set_environment_)(retro_environment_t) = nullptr;
  void (*retro_set_video_refresh_)(retro_video_refresh_t) = nullptr;
  void (*retro_set_audio_sample_)(retro_audio_sample_t) = nullptr;
  void (*retro_set_audio_sample_batch_)(retro_audio_sample_batch_t) = nullptr;
  void (*retro_set_input_poll_)(retro_input_poll_t) = nullptr;
  void (*retro_set_input_state_)(retro_input_state_t) = nullptr;
  void (*retro_set_controller_port_device_)(unsigned, unsigned) = nullptr;
  void (*retro_reset_)() = nullptr;
  void (*retro_run_)() = nullptr;
  size_t (*retro_serialize_size_)() = nullptr;
  bool (*retro_serialize_)(void*, size_t) = nullptr;
  bool (*retro_unserialize_)(const void*, size_t) = nullptr;
  bool (*retro_load_game_)(const retro_game_info*) = nullptr;
  void (*retro_unload_game_)() = nullptr;
  void* (*retro_get_memory_data_)(unsigned) = nullptr;
  size_t (*retro_get_memory_size_)(unsigned) = nullptr;

  retro_pixel_format pixFmt_ = RETRO_PIXEL_FORMAT_0RGB1555;
  std::vector<uint32_t> pixels_;
  unsigned width_ = 256, height_ = 224;
  float aspect_ = 0.f;
  double fps_ = 60.0988, sampleRate_ = 32040.0;
  bool frameChanged_ = false;
  bool needFullPath_ = false;
  std::vector<uint8_t> romData_;
  std::vector<int16_t> sampleAccum_;

  InputFn input_;
  AudioFn audio_;
};
