// Birdstation - ROM library, cover art cache and cover downloader
#pragma once
#include <SDL.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct Game {
  std::string path;   // full path (UTF-8)
  std::string stem;   // file name without extension (also the cover / save name)
  std::string title;  // display name (region/revision tags removed)
  SDL_Texture* cover = nullptr;
  int cw = 0, ch = 0;
  bool coverTried = false;
};

class Library {
public:
  explicit Library(std::string coversDir) : coversDir_(std::move(coversDir)) {}
  ~Library();

  void Scan(const std::vector<std::string>& dirs);
  std::vector<Game>& Games() { return games_; }

  void SetCoversDir(std::string d) { coversDir_ = std::move(d); }
  const std::string& CoversDir() const { return coversDir_; }
  // Picks up covers that the downloader finished (call once per frame).
  void PollReady();
  // Loads the cover texture of one game from covers/<stem>.png|jpg (once). Returns true if loaded.
  bool LoadCover(Game& g, SDL_Renderer* r);
  void FreeTextures();

  // Downloads missing box art from the libretro-thumbnails server in a background thread.
  void SetLogPath(std::string p) { logPath_ = std::move(p); }
  std::string Note();  // last download result / error message
  int DlMissing() const { return dlMissing_; }
  void StartCoverDownload();
  void StopCoverDownload();
  bool Downloading() const { return dlRunning_; }
  int DlDone() const { return dlDone_; }
  int DlTotal() const { return dlTotal_; }
  int DlFound() const { return dlFound_; }

  static SDL_Texture* LoadTexture(SDL_Renderer* r, const std::string& file, int* w, int* h);

private:
  void DownloadThread(std::vector<std::string> stems);

  void SetNote(const std::string& s);

  std::string coversDir_, logPath_, note_;
  std::vector<Game> games_;
  std::thread dlThread_;
  std::atomic<bool> dlRunning_{false}, dlCancel_{false};
  std::atomic<int> dlDone_{0}, dlTotal_{0}, dlFound_{0}, dlMissing_{0};
  std::mutex mtx_;
  std::vector<std::string> ready_;  // stems whose cover file just arrived
};
