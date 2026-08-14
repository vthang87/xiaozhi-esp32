#ifndef ES3C28P_SD_MUSIC_PLAYER_H_
#define ES3C28P_SD_MUSIC_PLAYER_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

class AudioService;
class Es3c28pDisplay;

struct SdMediaEntry {
    std::string name;
    std::string path;
    bool directory = false;
    size_t size = 0;
};

struct SdPlaybackStatus {
    bool mounted = false;
    bool playing = false;
    bool controllable = false;
    std::string name;
    std::string path;
    size_t track_index = 0;
    size_t track_count = 0;
    uint32_t elapsed_seconds = 0;
    uint32_t duration_seconds = 0;
};

class SdMusicPlayer {
public:
    SdMusicPlayer(AudioService& audio_service, Es3c28pDisplay* display);
    ~SdMusicPlayer();

    bool HasTracks() const { return !tracks_.empty(); }
    bool mounted() const { return mounted_; }
    size_t track_count() const { return tracks_.size(); }
    void HandleTouch(int x, int display_width);
    void Toggle();
    void Next();
    void Previous();
    void OpenBrowser();
    void BrowserUp();
    void SelectBrowserEntry(size_t index);

    bool ListMedia(const std::string& directory, const std::string& sort,
        std::vector<SdMediaEntry>& entries, std::string& current_directory,
        std::string& parent, std::vector<std::string>& playlist,
        std::string& error);
    bool UploadMedia(const std::string& directory, const std::string& filename,
        size_t content_length, const std::function<int(char*, size_t)>& reader,
        std::string& error);
    bool RemoveMedia(const std::string& path, std::string& error);
    bool CreateFolder(const std::string& directory, const std::string& name,
        std::string& error);
    bool RenameMedia(const std::string& path, const std::string& name,
        std::string& error);
    bool MoveMedia(const std::string& path, const std::string& directory,
        std::string& error);
    bool SetPlaylistOrder(const std::vector<std::string>& paths, std::string& error);
    void GetPlaybackStatus(SdPlaybackStatus& status);
    bool ControlPlayback(const std::string& action, const std::string& path,
        std::string& error);

private:
    enum class Command {
        kToggle,
        kNext,
        kPrevious,
        kPlaySelected,
        kResume,
        kShutdown,
    };

    AudioService& audio_service_;
    Es3c28pDisplay* display_;
    sdmmc_card_t* card_ = nullptr;
    std::vector<std::string> tracks_;
    size_t current_track_ = 0;
    long resume_offset_ = 0;
    QueueHandle_t command_queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> playing_{false};
    std::atomic<size_t> selected_track_{0};
    bool mounted_ = false;
    uint64_t playback_samples_ = 0;
    uint32_t resume_elapsed_seconds_ = 0;
    uint32_t playback_sample_rate_ = 0;
    uint32_t total_duration_seconds_ = 0;
    uint32_t displayed_elapsed_seconds_ = UINT32_MAX;
    uint32_t last_saved_elapsed_seconds_ = UINT32_MAX;
    std::string browser_directory_;
    std::vector<std::string> browser_paths_;
    std::vector<bool> browser_directories_;

    std::atomic<bool> file_open_{false};
    std::mutex media_mutex_;

    bool Mount();
    void ScanDirectory(const std::string& directory, int depth = 0);
    void SendCommand(Command command);
    bool CanControlMusic();
    void LoadBrowserDirectory(const std::string& directory);
    void Run();
    bool PlayCurrentTrack();
    bool HandlePendingCommand(bool wait);
    void SelectNext(int direction);
    void ShowTrack(const char* state);
    void UpdateProgress(bool force = false);
    void RestorePlaybackState();
    void SavePlaybackState(bool force = false);
    bool PauseForMediaEdit();
    void ResumeAfterMediaEdit(bool was_playing, const std::string& current_path);
    void RebuildPlaylist();
    void ApplySavedOrder();
    bool SavePlaylistOrder(const std::vector<std::string>& relative_paths,
        std::string& error);
    bool RewritePlaylistPrefix(const std::string& old_relative,
        const std::string& new_relative, std::string& error);
    bool RemoveTree(const std::string& full_path, int depth, std::string& error);
    bool RelocateMedia(const std::string& relative, const std::string& destination,
        std::string& error);
    void EnsureTask();
    void RefreshDisplay();
    static void TaskEntry(void* arg);
};

#endif  // ES3C28P_SD_MUSIC_PLAYER_H_
