#ifndef ES3C28P_SD_MUSIC_PLAYER_H_
#define ES3C28P_SD_MUSIC_PLAYER_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

class AudioService;
class Es3c28pDisplay;

class SdMusicPlayer {
public:
    SdMusicPlayer(AudioService& audio_service, Es3c28pDisplay* display);
    ~SdMusicPlayer();

    bool HasTracks() const { return !tracks_.empty(); }
    void HandleTouch(int x, int display_width);
    void Toggle();
    void Next();
    void Previous();
    void OpenBrowser();
    void BrowserUp();
    void SelectBrowserEntry(size_t index);

private:
    enum class Command {
        kToggle,
        kNext,
        kPrevious,
        kPlaySelected,
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
    static void TaskEntry(void* arg);
};

#endif  // ES3C28P_SD_MUSIC_PLAYER_H_
