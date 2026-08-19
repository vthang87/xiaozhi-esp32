#include "sd_music_player.h"

#include "application.h"
#include "audio/audio_service.h"
#include "config.h"
#include "es3c28p_display.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>

#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <micro_mp3/mp3_decoder.h>

#define TAG "SdMusicPlayer"

namespace {
constexpr size_t kInputBufferSize = 4096;
constexpr int kMaxScanDepth = 4;
constexpr size_t kMaxTracks = 256;
constexpr size_t kMaxUploadBytes = 100 * 1024 * 1024;
constexpr size_t kUploadChunkSize = 16 * 1024;
constexpr size_t kMaxPathLength = 240;
constexpr char kPlaybackStateFile[] = SDCARD_MOUNT_POINT "/.xiaozhi_music_state";
constexpr char kPlaybackStateTempFile[] = SDCARD_MOUNT_POINT "/.xiaozhi_music_state.tmp";
constexpr char kPlaylistFile[] = SDCARD_MOUNT_POINT "/.xiaozhi_playlist";
constexpr char kPlaylistTempFile[] = SDCARD_MOUNT_POINT "/.xiaozhi_playlist.tmp";

bool IsMp3File(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    std::string extension = name.substr(name.size() - 4);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".mp3";
}

bool IsHiddenOrTemporary(const char* name) {
    return name == nullptr || name[0] == '.' || name[0] == '_';
}

bool HasInvalidFatChar(const std::string& name) {
    return name.find_first_of("\\:*?\"<>|") != std::string::npos;
}

const char* BaseName(const std::string& path) {
    auto separator = path.find_last_of('/');
    return separator == std::string::npos ? path.c_str() : path.c_str() + separator + 1;
}

std::string RelativePath(const std::string& full_path) {
    constexpr size_t kPrefix = sizeof(SDCARD_MOUNT_POINT) - 1;
    if (full_path.size() <= kPrefix) {
        return "/";
    }
    return full_path.substr(kPrefix);
}

std::string CurrentTrackPath(const std::vector<std::string>& tracks, size_t index) {
    return index < tracks.size() ? tracks[index] : std::string();
}

bool NormalizeRelativeDirectory(const std::string& input, std::string& relative,
    std::string& error) {
    relative.clear();
    std::string current;
    for (size_t i = 0; i <= input.size(); ++i) {
        const char ch = i < input.size() ? input[i] : '/';
        if (ch == '\\' || ch == '\0') {
            error = "That folder path is not allowed";
            return false;
        }
        if (ch != '/') {
            current.push_back(ch);
            continue;
        }
        if (current.empty() || current == ".") {
            current.clear();
            continue;
        }
        if (current == ".." || IsHiddenOrTemporary(current.c_str()) ||
            HasInvalidFatChar(current)) {
            error = "That folder path is not allowed";
            return false;
        }
        relative.append("/").append(current);
        current.clear();
    }
    if (relative.size() > kMaxPathLength) {
        error = "That folder path is too long";
        return false;
    }
    return true;
}

bool SanitizeEntryName(const std::string& input, std::string& name, bool require_mp3,
    std::string& error) {
    name = input;
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name.empty() || IsHiddenOrTemporary(name.c_str()) ||
        HasInvalidFatChar(name) || name.find("..") != std::string::npos) {
        error = require_mp3 ? "Choose an MP3 file with a regular name"
                            : "Choose a folder name without special characters";
        return false;
    }
    if (require_mp3 && !IsMp3File(name)) {
        error = "Only MP3 files can be uploaded";
        return false;
    }
    if (!require_mp3 && IsMp3File(name)) {
        error = "A folder name cannot end with .mp3";
        return false;
    }
    if (name.size() > 64) {
        error = "That name is too long";
        return false;
    }
    return true;
}

bool SanitizeFileName(const std::string& input, std::string& filename, std::string& error) {
    return SanitizeEntryName(input, filename, true, error);
}

int PathDepth(const std::string& relative) {
    if (relative.empty() || relative == "/") {
        return 0;
    }
    int depth = 0;
    for (char ch : relative) {
        if (ch == '/') {
            ++depth;
        }
    }
    return depth;
}

std::string ParentRelative(const std::string& relative) {
    if (relative.empty() || relative == "/") {
        return {};
    }
    const auto separator = relative.find_last_of('/');
    if (separator == std::string::npos || separator == 0) {
        return {};
    }
    return relative.substr(0, separator);
}

bool IsSelfOrDescendant(const std::string& parent, const std::string& child) {
    return child == parent || (child.size() > parent.size() &&
        child.compare(0, parent.size(), parent) == 0 &&
        child[parent.size()] == '/');
}

std::string RemapFullPath(const std::string& path, const std::string& old_full,
    const std::string& new_full) {
    if (path == old_full) {
        return new_full;
    }
    if (path.size() > old_full.size() &&
        path.compare(0, old_full.size(), old_full) == 0 &&
        path[old_full.size()] == '/') {
        return new_full + path.substr(old_full.size());
    }
    return path;
}

bool WriteLinesAtomically(const char* path, const char* temp_path,
    const std::function<bool(FILE*)>& write_body, const char* failure) {
    FILE* file = fopen(temp_path, "wb");
    if (file == nullptr) {
        return false;
    }
    bool write_ok = write_body(file);
    write_ok = fflush(file) == 0 && write_ok;
    write_ok = fsync(fileno(file)) == 0 && write_ok;
    write_ok = fclose(file) == 0 && write_ok;
    if (!write_ok) {
        ESP_LOGW(TAG, "%s", failure);
        remove(temp_path);
        return false;
    }
    remove(path);
    if (rename(temp_path, path) != 0) {
        ESP_LOGW(TAG, "%s", failure);
        remove(temp_path);
        return false;
    }
    return true;
}
}  // namespace

SdMusicPlayer::SdMusicPlayer(AudioService& audio_service, Es3c28pDisplay* display)
    : audio_service_(audio_service), display_(display),
      browser_directory_(SDCARD_MOUNT_POINT) {
    display_->SetMusicPlayer(this);
    command_queue_ = xQueueCreate(8, sizeof(Command));
    if (command_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create command queue");
        display_->SetMusicInfo("Player unavailable", "Not enough memory");
        return;
    }
    if (!Mount()) {
        return;
    }

    RebuildPlaylist();
    ESP_LOGI(TAG, "Found %u MP3 track(s)", static_cast<unsigned>(tracks_.size()));
    if (tracks_.empty()) {
        display_->SetMusicInfo("No MP3 files", "Copy MP3 files to the SD card");
        display_->ShowNotification("SD card: no MP3 files");
        return;
    }

    RestorePlaybackState();
    char ready_text[32];
    snprintf(ready_text, sizeof(ready_text), "Ready - %u tracks",
        static_cast<unsigned>(tracks_.size()));
    display_->SetMusicInfo(ready_text, BaseName(tracks_[current_track_]), false,
        current_track_ + 1, tracks_.size());
    display_->SetMusicProgress(displayed_elapsed_seconds_ == UINT32_MAX
        ? 0 : displayed_elapsed_seconds_, 0);
    display_->ShowNotification("SD music ready");
    xTaskCreatePinnedToCore(TaskEntry, "sd_music", 8192, this, 3, &task_, 1);
}

SdMusicPlayer::~SdMusicPlayer() {
    display_->SetMusicPlayer(nullptr);
    if (task_ != nullptr) {
        SendCommand(Command::kShutdown);
        while (task_ != nullptr) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (mounted_) {
        esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, card_);
    }
    if (command_queue_ != nullptr) {
        vQueueDelete(command_queue_);
    }
}

bool SdMusicPlayer::Mount() {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = SDCARD_CLK_PIN;
    slot.cmd = SDCARD_CMD_PIN;
    slot.d0 = SDCARD_D0_PIN;
    slot.d1 = SDCARD_D1_PIN;
    slot.d2 = SDCARD_D2_PIN;
    slot.d3 = SDCARD_D3_PIN;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t result = esp_vfs_fat_sdmmc_mount(
        SDCARD_MOUNT_POINT, &host, &slot, &mount_config, &card_);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(result));
        display_->SetMusicInfo("SD card unavailable", "Insert FAT32 card and reset");
        display_->ShowNotification("SD card not found");
        return false;
    }
    mounted_ = true;
    sdmmc_card_print_info(stdout, card_);
    return true;
}

void SdMusicPlayer::ScanDirectory(const std::string& directory, int depth) {
    if (depth > kMaxScanDepth || tracks_.size() >= kMaxTracks) {
        return;
    }
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        return;
    }

    while (auto* entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (IsHiddenOrTemporary(entry->d_name)) {
            continue;
        }
        std::string path = directory + "/" + entry->d_name;
        struct stat info = {};
        if (stat(path.c_str(), &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            ScanDirectory(path, depth + 1);
        } else if (S_ISREG(info.st_mode) && IsMp3File(path)) {
            tracks_.push_back(std::move(path));
            if (tracks_.size() >= kMaxTracks) {
                break;
            }
        }
    }
    closedir(dir);
}

void SdMusicPlayer::HandleTouch(int x, int display_width) {
    if (!HasTracks()) {
        return;
    }
    if (x < display_width / 3) {
        Previous();
    } else if (x >= display_width * 2 / 3) {
        Next();
    } else {
        Toggle();
    }
}

void SdMusicPlayer::Toggle() {
    if (!CanControlMusic()) {
        return;
    }
    SendCommand(Command::kToggle);
}

void SdMusicPlayer::Next() {
    if (!CanControlMusic()) {
        return;
    }
    SendCommand(Command::kNext);
}

void SdMusicPlayer::Previous() {
    if (!CanControlMusic()) {
        return;
    }
    SendCommand(Command::kPrevious);
}

void SdMusicPlayer::OpenBrowser() {
    LoadBrowserDirectory(browser_directory_);
    display_->ShowMusicBrowser(true);
}

void SdMusicPlayer::BrowserUp() {
    if (browser_directory_ == SDCARD_MOUNT_POINT) return;
    auto separator = browser_directory_.find_last_of('/');
    LoadBrowserDirectory(separator <= strlen(SDCARD_MOUNT_POINT)
        ? SDCARD_MOUNT_POINT : browser_directory_.substr(0, separator));
}

void SdMusicPlayer::SelectBrowserEntry(size_t index) {
    if (index >= browser_paths_.size()) return;
    if (browser_directories_[index]) {
        LoadBrowserDirectory(browser_paths_[index]);
        return;
    }
    if (!CanControlMusic()) return;
    auto track = std::find(tracks_.begin(), tracks_.end(), browser_paths_[index]);
    if (track == tracks_.end()) return;
    selected_track_ = static_cast<size_t>(track - tracks_.begin());
    SendCommand(Command::kPlaySelected);
    display_->ShowMusicBrowser(false);
}

void SdMusicPlayer::LoadBrowserDirectory(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) return;
    struct Entry { std::string name; std::string path; bool directory; };
    std::vector<Entry> entries;
    while (auto* item = readdir(dir)) {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        if (IsHiddenOrTemporary(item->d_name)) continue;
        std::string path = directory + "/" + item->d_name;
        struct stat info = {};
        if (stat(path.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode) || (S_ISREG(info.st_mode) && IsMp3File(path))) {
            entries.push_back({item->d_name, std::move(path), S_ISDIR(info.st_mode)});
        }
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return a.name < b.name;
    });
    browser_directory_ = directory;
    browser_paths_.clear();
    browser_directories_.clear();
    std::vector<std::string> names;
    for (auto& entry : entries) {
        names.push_back(entry.name);
        browser_paths_.push_back(std::move(entry.path));
        browser_directories_.push_back(entry.directory);
    }
    std::string shown_path = directory.substr(strlen(SDCARD_MOUNT_POINT));
    display_->SetBrowserEntries(shown_path.empty() ? "/" : shown_path,
        names, browser_directories_);
}

bool SdMusicPlayer::CanControlMusic() {
    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        return true;
    }
    display_->ShowNotification("AI chat is active", 1500);
    return false;
}

void SdMusicPlayer::GetPlaybackStatus(SdPlaybackStatus& status) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    status = {};
    status.mounted = mounted_;
    status.playing = playing_;
    status.controllable =
        Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
    status.track_count = tracks_.size();
    if (!tracks_.empty() && current_track_ < tracks_.size()) {
        status.track_index = current_track_ + 1;
        status.path = RelativePath(tracks_[current_track_]);
        status.name = BaseName(tracks_[current_track_]);
        status.elapsed_seconds = resume_elapsed_seconds_ +
            (playback_sample_rate_ > 0
                ? static_cast<uint32_t>(playback_samples_ / playback_sample_rate_)
                : 0);
        status.duration_seconds = total_duration_seconds_;
    }
}

bool SdMusicPlayer::ControlPlayback(const std::string& action,
    const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    if (tracks_.empty()) {
        error = "Copy MP3 files to the SD card first";
        return false;
    }
    if (command_queue_ == nullptr) {
        error = "Music player is unavailable";
        return false;
    }
    if (!CanControlMusic()) {
        error = "Playback is locked while AI chat is active";
        return false;
    }
    EnsureTask();

    if (action == "next") {
        SendCommand(Command::kNext);
        return true;
    }
    if (action == "previous") {
        SendCommand(Command::kPrevious);
        return true;
    }
    if (action == "toggle") {
        SendCommand(Command::kToggle);
        return true;
    }
    if (action == "pause") {
        if (playing_) {
            SendCommand(Command::kToggle);
        }
        return true;
    }
    if (action == "play") {
        if (!path.empty()) {
            std::string relative;
            if (!NormalizeRelativeDirectory(path, relative, error) ||
                relative.empty() || !IsMp3File(relative)) {
                error = "Choose an MP3 file to play";
                return false;
            }
            const std::string full = std::string(SDCARD_MOUNT_POINT) + relative;
            auto track = std::find(tracks_.begin(), tracks_.end(), full);
            if (track == tracks_.end()) {
                error = "That track is not in the playlist";
                return false;
            }
            const size_t index = static_cast<size_t>(track - tracks_.begin());
            if (current_track_ == index) {
                if (!playing_) {
                    SendCommand(Command::kToggle);
                }
                return true;
            }
            selected_track_ = index;
            SendCommand(Command::kPlaySelected);
            return true;
        }
        if (!playing_) {
            SendCommand(Command::kToggle);
        }
        return true;
    }

    error = "Use play, pause, toggle, next, or previous";
    return false;
}

void SdMusicPlayer::SendCommand(Command command) {
    if (command_queue_ != nullptr) {
        xQueueSend(command_queue_, &command, 0);
    }
}

void SdMusicPlayer::TaskEntry(void* arg) {
    auto* player = static_cast<SdMusicPlayer*>(arg);
    player->Run();
    player->task_ = nullptr;
    vTaskDelete(nullptr);
}

void SdMusicPlayer::Run() {
    while (true) {
        if (!playing_) {
            if (!HandlePendingCommand(true)) {
                break;
            }
            continue;
        }
        if (!PlayCurrentTrack()) {
            break;
        }
    }
}

bool SdMusicPlayer::HandlePendingCommand(bool wait) {
    Command command;
    TickType_t timeout = wait ? portMAX_DELAY : 0;
    if (xQueueReceive(command_queue_, &command, timeout) != pdTRUE) {
        return true;
    }

    switch (command) {
        case Command::kToggle:
            playing_ = !playing_;
            ShowTrack(playing_ ? "Playing" : "Paused");
            SavePlaybackState(true);
            break;
        case Command::kNext:
            SelectNext(1);
            playing_ = true;
            ShowTrack("Playing");
            SavePlaybackState(true);
            break;
        case Command::kPrevious:
            SelectNext(-1);
            playing_ = true;
            ShowTrack("Playing");
            SavePlaybackState(true);
            break;
        case Command::kPlaySelected:
            current_track_ = selected_track_;
            resume_offset_ = 0;
            resume_elapsed_seconds_ = 0;
            playback_samples_ = 0;
            playback_sample_rate_ = 0;
            total_duration_seconds_ = 0;
            displayed_elapsed_seconds_ = UINT32_MAX;
            playing_ = true;
            ShowTrack("Playing");
            SavePlaybackState(true);
            break;
        case Command::kResume:
            if (!tracks_.empty()) {
                playing_ = true;
                ShowTrack("Playing");
            }
            break;
        case Command::kShutdown:
            playing_ = false;
            SavePlaybackState(true);
            return false;
    }
    return true;
}

void SdMusicPlayer::SelectNext(int direction) {
    resume_offset_ = 0;
    resume_elapsed_seconds_ = 0;
    playback_samples_ = 0;
    playback_sample_rate_ = 0;
    total_duration_seconds_ = 0;
    displayed_elapsed_seconds_ = UINT32_MAX;
    last_saved_elapsed_seconds_ = UINT32_MAX;
    if (direction > 0) {
        current_track_ = (current_track_ + 1) % tracks_.size();
    } else {
        current_track_ = (current_track_ + tracks_.size() - 1) % tracks_.size();
    }
}

void SdMusicPlayer::ShowTrack(const char* state) {
    if (tracks_.empty()) {
        return;
    }
    display_->SetMusicInfo(state, BaseName(tracks_[current_track_]), playing_,
        current_track_ + 1, tracks_.size());
    UpdateProgress(true);
}

void SdMusicPlayer::UpdateProgress(bool force) {
    const uint32_t elapsed = resume_elapsed_seconds_ +
        (playback_sample_rate_ > 0
            ? static_cast<uint32_t>(playback_samples_ / playback_sample_rate_)
            : 0);
    if (!force && elapsed == displayed_elapsed_seconds_) {
        return;
    }
    displayed_elapsed_seconds_ = elapsed;
    display_->SetMusicProgress(elapsed, total_duration_seconds_);
    SavePlaybackState(false);
}

void SdMusicPlayer::RestorePlaybackState() {
    FILE* file = fopen(kPlaybackStateFile, "rb");
    if (file == nullptr) {
        return;
    }

    long saved_offset = 0;
    unsigned saved_elapsed = 0;
    char saved_track_buffer[1024] = {};
    const bool header_valid = fscanf(file, "%ld\n%u\n", &saved_offset,
        &saved_elapsed) == 2;
    const bool track_valid = header_valid &&
        fgets(saved_track_buffer, sizeof(saved_track_buffer), file) != nullptr;
    fclose(file);
    if (!track_valid) {
        ESP_LOGW(TAG, "Invalid playback state on SD card");
        return;
    }
    saved_track_buffer[strcspn(saved_track_buffer, "\r\n")] = '\0';
    const std::string saved_track(saved_track_buffer);
    auto match = std::find(tracks_.begin(), tracks_.end(), saved_track);
    if (match == tracks_.end()) {
        return;
    }
    current_track_ = static_cast<size_t>(match - tracks_.begin());
    resume_offset_ = std::max<long>(0, saved_offset);
    displayed_elapsed_seconds_ = static_cast<uint32_t>(saved_elapsed);
    resume_elapsed_seconds_ = displayed_elapsed_seconds_;
    playback_samples_ = 0;
    playback_sample_rate_ = 0;
    last_saved_elapsed_seconds_ = displayed_elapsed_seconds_;
    ESP_LOGI(TAG, "Restored track %u at %u seconds",
        static_cast<unsigned>(current_track_ + 1),
        static_cast<unsigned>(displayed_elapsed_seconds_));
}

void SdMusicPlayer::SavePlaybackState(bool force) {
    if (tracks_.empty()) {
        return;
    }
    const uint32_t elapsed = resume_elapsed_seconds_ +
        (playback_sample_rate_ > 0
            ? static_cast<uint32_t>(playback_samples_ / playback_sample_rate_)
            : 0);
    if (!force && last_saved_elapsed_seconds_ != UINT32_MAX &&
        elapsed / 30 == last_saved_elapsed_seconds_ / 30) {
        return;
    }
    last_saved_elapsed_seconds_ = elapsed;
    FILE* file = fopen(kPlaybackStateTempFile, "wb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "Failed to create playback state on SD card");
        return;
    }
    const int written = fprintf(file, "%ld\n%u\n%s\n",
        std::max<long>(0, resume_offset_), static_cast<unsigned>(elapsed),
        tracks_[current_track_].c_str());
    bool write_ok = written >= 0;
    write_ok = fflush(file) == 0 && write_ok;
    write_ok = fsync(fileno(file)) == 0 && write_ok;
    write_ok = fclose(file) == 0 && write_ok;
    if (!write_ok) {
        ESP_LOGW(TAG, "Failed to write playback state on SD card");
        remove(kPlaybackStateTempFile);
        return;
    }
    remove(kPlaybackStateFile);
    if (rename(kPlaybackStateTempFile, kPlaybackStateFile) != 0) {
        ESP_LOGW(TAG, "Failed to replace playback state on SD card");
        remove(kPlaybackStateTempFile);
    }
}

bool SdMusicPlayer::PlayCurrentTrack() {
    const size_t track_index = current_track_;
    FILE* file = fopen(tracks_[track_index].c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open %s", tracks_[track_index].c_str());
        SelectNext(1);
        return true;
    }
    file_open_ = true;
    if (resume_offset_ > 0) {
        fseek(file, resume_offset_, SEEK_SET);
    }
    const long playback_start = ftell(file);
    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, playback_start, SEEK_SET);

    ShowTrack("Playing");
    micro_mp3::Mp3Decoder decoder;
    std::vector<uint8_t> input(kInputBufferSize);
    std::vector<int16_t> decoded(
        micro_mp3::MP3_MIN_OUTPUT_BUFFER_BYTES / sizeof(int16_t));
    uint32_t configured_rate = 0;
    uint32_t resample_phase = 0;
    int16_t previous_sample = 0;
    bool has_previous_sample = false;
    long last_consumed_offset = resume_offset_;
    bool keep_running = true;

    while (playing_ && current_track_ == track_index) {
        if (!HandlePendingCommand(false)) {
            keep_running = false;
            break;
        }
        if (!playing_ || current_track_ != track_index) {
            break;
        }

        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t bytes_read = fread(input.data(), 1, input.size(), file);
        if (bytes_read == 0) {
            if (feof(file)) {
                SelectNext(1);
                ShowTrack("Playing");
            } else {
                ESP_LOGE(TAG, "Read failed for %s", tracks_[track_index].c_str());
                playing_ = false;
            }
            break;
        }

        const long chunk_start = ftell(file) - static_cast<long>(bytes_read);
        size_t offset = 0;
        while (offset < bytes_read && playing_ && current_track_ == track_index) {
            size_t consumed = 0;
            size_t samples = 0;
            auto result = decoder.decode(input.data() + offset, bytes_read - offset,
                reinterpret_cast<uint8_t*>(decoded.data()),
                decoded.size() * sizeof(int16_t), consumed, samples);
            offset += consumed;
            last_consumed_offset = chunk_start + static_cast<long>(offset);
            resume_offset_ = last_consumed_offset;

            if (result == micro_mp3::MP3_STREAM_INFO_READY ||
                result == micro_mp3::MP3_STREAM_INFO_CHANGED) {
                configured_rate = decoder.get_sample_rate();
                playback_sample_rate_ = configured_rate;
                const uint32_t bitrate_kbps = decoder.get_bitrate();
                if (file_size > 0 && bitrate_kbps > 0) {
                    total_duration_seconds_ = static_cast<uint32_t>(
                        static_cast<uint64_t>(file_size) * 8 /
                        (static_cast<uint64_t>(bitrate_kbps) * 1000));
                }
                UpdateProgress(true);
                resample_phase = 0;
                has_previous_sample = false;
                continue;
            }
            if (result < 0 && result != micro_mp3::MP3_DECODE_ERROR) {
                ESP_LOGE(TAG, "MP3 decode error %d in %s", result,
                    tracks_[track_index].c_str());
                playing_ = false;
                break;
            }

            if (samples > 0) {
                playback_samples_ += samples;
                UpdateProgress();
                uint8_t channels = decoder.get_channels();
                std::vector<int16_t> mono(samples);
                if (channels == 2) {
                    for (size_t i = 0; i < samples; ++i) {
                        int32_t mixed = static_cast<int32_t>(decoded[i * 2]) + decoded[i * 2 + 1];
                        mono[i] = static_cast<int16_t>(mixed / 2);
                    }
                } else {
                    std::copy_n(decoded.begin(), samples, mono.begin());
                }

                if (configured_rate != 0 && configured_rate != AUDIO_OUTPUT_SAMPLE_RATE &&
                    !mono.empty()) {
                    std::vector<int16_t> converted;
                    converted.reserve(
                        mono.size() * AUDIO_OUTPUT_SAMPLE_RATE / configured_rate + 2);

                    for (int16_t sample : mono) {
                        if (!has_previous_sample) {
                            previous_sample = sample;
                            has_previous_sample = true;
                            converted.push_back(sample);
                            continue;
                        }

                        resample_phase += AUDIO_OUTPUT_SAMPLE_RATE;
                        while (resample_phase >= configured_rate) {
                            const uint32_t overshoot = resample_phase - configured_rate;
                            const float fraction = 1.0f -
                                static_cast<float>(overshoot) / AUDIO_OUTPUT_SAMPLE_RATE;
                            const float interpolated = previous_sample +
                                (sample - previous_sample) * fraction;
                            converted.push_back(static_cast<int16_t>(interpolated));
                            resample_phase -= configured_rate;
                        }
                        previous_sample = sample;
                    }
                    mono = std::move(converted);
                }
                if (!mono.empty() &&
                    !audio_service_.QueuePcmForPlayback(std::move(mono), true)) {
                    keep_running = false;
                    break;
                }
            }

            if (consumed == 0 && samples == 0) {
                break;
            }
        }
    }

    if (!playing_ && current_track_ == track_index) {
        resume_offset_ = last_consumed_offset;
        SavePlaybackState(true);
    }
    fclose(file);
    file_open_ = false;
    return keep_running;
}

bool SdMusicPlayer::ListMedia(const std::string& directory, const std::string& sort,
    std::vector<SdMediaEntry>& entries, std::string& current_directory,
    std::string& parent, std::vector<std::string>& playlist, std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    std::string relative;
    if (!NormalizeRelativeDirectory(directory, relative, error)) {
        return false;
    }
    const std::string full_directory = std::string(SDCARD_MOUNT_POINT) + relative;
    DIR* dir = opendir(full_directory.c_str());
    if (dir == nullptr) {
        error = "That folder could not be opened";
        return false;
    }

    entries.clear();
    while (auto* item = readdir(dir)) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0 ||
            IsHiddenOrTemporary(item->d_name)) {
            continue;
        }
        std::string path = full_directory + "/" + item->d_name;
        struct stat info = {};
        if (stat(path.c_str(), &info) != 0) {
            continue;
        }
        const bool is_directory = S_ISDIR(info.st_mode);
        if (!is_directory && (!S_ISREG(info.st_mode) || !IsMp3File(path))) {
            continue;
        }
        SdMediaEntry entry;
        entry.name = item->d_name;
        entry.path = RelativePath(path);
        entry.directory = is_directory;
        entry.size = is_directory ? 0 : static_cast<size_t>(info.st_size);
        entries.push_back(std::move(entry));
    }
    closedir(dir);

    auto name_less = [](const std::string& left, const std::string& right) {
        return std::lexicographical_compare(left.begin(), left.end(),
            right.begin(), right.end(), [](unsigned char a, unsigned char b) {
                return std::tolower(a) < std::tolower(b);
            });
    };
    std::sort(entries.begin(), entries.end(),
        [&](const SdMediaEntry& left, const SdMediaEntry& right) {
            if (left.directory != right.directory) {
                return left.directory > right.directory;
            }
            if (!left.directory && sort == "size" && left.size != right.size) {
                return left.size > right.size;
            }
            if (!left.directory && sort == "playlist") {
                const std::string left_full = std::string(SDCARD_MOUNT_POINT) + left.path;
                const std::string right_full = std::string(SDCARD_MOUNT_POINT) + right.path;
                const auto left_index = std::find(tracks_.begin(), tracks_.end(), left_full);
                const auto right_index = std::find(tracks_.begin(), tracks_.end(), right_full);
                if (left_index != right_index) {
                    return left_index < right_index;
                }
            }
            return name_less(left.name, right.name);
        });

    current_directory = relative.empty() ? "/" : relative;
    if (relative.empty()) {
        parent.clear();
    } else {
        const auto separator = relative.find_last_of('/');
        parent = separator == 0 ? "/" : relative.substr(0, separator);
    }
    playlist.clear();
    playlist.reserve(tracks_.size());
    for (const auto& track : tracks_) {
        playlist.push_back(RelativePath(track));
    }
    return true;
}

bool SdMusicPlayer::UploadMedia(const std::string& directory,
    const std::string& filename, size_t content_length,
    const std::function<int(char*, size_t)>& reader, std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    if (reader == nullptr || content_length > kMaxUploadBytes) {
        error = "Choose an MP3 file that is 100 MB or smaller";
        return false;
    }
    std::string relative_dir;
    std::string safe_name;
    if (!NormalizeRelativeDirectory(directory, relative_dir, error) ||
        !SanitizeFileName(filename, safe_name, error)) {
        return false;
    }
    if (tracks_.size() >= kMaxTracks) {
        error = "The playlist is full (256 tracks)";
        return false;
    }

    const bool was_playing = playing_;
    const std::string current_path = CurrentTrackPath(tracks_, current_track_);
    if (!PauseForMediaEdit()) {
        error = "Stop playback and try again";
        if (was_playing) {
            SendCommand(Command::kResume);
        }
        return false;
    }

    const std::string dest_dir = std::string(SDCARD_MOUNT_POINT) + relative_dir;
    const std::string destination = dest_dir + "/" + safe_name;
    const std::string temporary = dest_dir + "/._" + safe_name + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    if (file == nullptr) {
        error = "The SD card could not be written";
        ResumeAfterMediaEdit(was_playing, current_path);
        return false;
    }
    setvbuf(file, nullptr, _IOFBF, kUploadChunkSize);

    std::vector<char> buffer(kUploadChunkSize);
    size_t received = 0;
    size_t pending = 0;
    bool write_ok = true;
    const bool known_length = content_length > 0;
    const int64_t started = esp_timer_get_time();
    while (!known_length || received < content_length) {
        if (received >= kMaxUploadBytes) {
            write_ok = false;
            break;
        }
        const size_t remaining = known_length ? content_length - received
            : kMaxUploadBytes - received;
        const int count = reader(buffer.data() + pending,
            std::min(buffer.size() - pending, remaining));
        if (count < 0 || (count == 0 && known_length && received < content_length)) {
            write_ok = false;
            break;
        }
        if (count == 0) {
            break;
        }
        pending += static_cast<size_t>(count);
        received += static_cast<size_t>(count);
        if (pending == buffer.size() || (known_length && received == content_length)) {
            if (fwrite(buffer.data(), 1, pending, file) != pending) {
                write_ok = false;
                break;
            }
            pending = 0;
        }
    }
    if (write_ok && pending > 0) {
        write_ok = fwrite(buffer.data(), 1, pending, file) == pending;
    }
    write_ok = received > 0 && write_ok;
    write_ok = fflush(file) == 0 && write_ok;
    write_ok = fsync(fileno(file)) == 0 && write_ok;
    write_ok = fclose(file) == 0 && write_ok;
    if (!write_ok) {
        remove(temporary.c_str());
        error = "Transfer was interrupted";
        ResumeAfterMediaEdit(was_playing, current_path);
        return false;
    }
    remove(destination.c_str());
    if (rename(temporary.c_str(), destination.c_str()) != 0) {
        remove(temporary.c_str());
        error = "The file could not be saved";
        ResumeAfterMediaEdit(was_playing, current_path);
        return false;
    }

    ResumeAfterMediaEdit(was_playing, current_path);
    const int64_t elapsed_us = std::max<int64_t>(esp_timer_get_time() - started, 1);
    ESP_LOGI(TAG, "Saved %s (%u KB, %u KB/s)", safe_name.c_str(),
        static_cast<unsigned>(received / 1024),
        static_cast<unsigned>((static_cast<uint64_t>(received) * 1000000ull) /
            (static_cast<uint64_t>(elapsed_us) * 1024ull)));
    display_->ShowNotification("Track saved");
    return true;
}

bool SdMusicPlayer::RemoveMedia(const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    std::string relative;
    if (!NormalizeRelativeDirectory(path, relative, error) || relative.empty()) {
        error = "Choose a file or folder to remove";
        return false;
    }
    const std::string full_path = std::string(SDCARD_MOUNT_POINT) + relative;
    struct stat info = {};
    if (stat(full_path.c_str(), &info) != 0) {
        error = "That item was not found";
        return false;
    }
    if (!S_ISDIR(info.st_mode) && !IsMp3File(full_path)) {
        error = "Only MP3 files can be removed";
        return false;
    }

    const bool was_playing = playing_;
    const std::string current_path = CurrentTrackPath(tracks_, current_track_);
    if (!PauseForMediaEdit()) {
        error = "Stop playback and try again";
        if (was_playing) {
            SendCommand(Command::kResume);
        }
        return false;
    }

    if (S_ISDIR(info.st_mode)) {
        if (!RemoveTree(full_path, 0, error)) {
            ResumeAfterMediaEdit(was_playing, current_path);
            return false;
        }
    } else if (remove(full_path.c_str()) != 0) {
        error = "The file could not be removed";
        ResumeAfterMediaEdit(was_playing, current_path);
        return false;
    }

    ResumeAfterMediaEdit(was_playing, current_path);
    display_->ShowNotification("Item removed");
    return true;
}

bool SdMusicPlayer::CreateFolder(const std::string& directory, const std::string& name,
    std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    std::string relative_dir;
    std::string folder_name;
    if (!NormalizeRelativeDirectory(directory, relative_dir, error) ||
        !SanitizeEntryName(name, folder_name, false, error)) {
        return false;
    }
    if (PathDepth(relative_dir) >= kMaxScanDepth) {
        error = "Folders can only be four levels deep";
        return false;
    }
    const std::string full_path =
        std::string(SDCARD_MOUNT_POINT) + relative_dir + "/" + folder_name;
    if (mkdir(full_path.c_str(), 0775) != 0) {
        error = errno == EEXIST ? "A folder with that name already exists"
                                : "The folder could not be created";
        return false;
    }
    display_->ShowNotification("Folder created");
    return true;
}

bool SdMusicPlayer::RenameMedia(const std::string& path, const std::string& name,
    std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    std::string relative;
    if (!NormalizeRelativeDirectory(path, relative, error) || relative.empty()) {
        error = "Choose a file or folder to rename";
        return false;
    }
    const std::string full_path = std::string(SDCARD_MOUNT_POINT) + relative;
    struct stat info = {};
    if (stat(full_path.c_str(), &info) != 0) {
        error = "That item was not found";
        return false;
    }
    const bool is_directory = S_ISDIR(info.st_mode);
    if (!is_directory && !IsMp3File(full_path)) {
        error = "Only MP3 files can be renamed";
        return false;
    }
    std::string new_name;
    if (!SanitizeEntryName(name, new_name, !is_directory, error)) {
        return false;
    }
    const std::string destination = std::string(SDCARD_MOUNT_POINT) +
        ParentRelative(relative) + "/" + new_name;
    return RelocateMedia(relative, destination, error);
}

bool SdMusicPlayer::MoveMedia(const std::string& path, const std::string& directory,
    std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    std::string relative;
    std::string dest_dir;
    if (!NormalizeRelativeDirectory(path, relative, error) || relative.empty()) {
        error = "Choose a file or folder to move";
        return false;
    }
    if (!NormalizeRelativeDirectory(directory, dest_dir, error)) {
        return false;
    }
    if (IsSelfOrDescendant(relative, dest_dir)) {
        error = "A folder cannot be moved into itself";
        return false;
    }
    const std::string destination = std::string(SDCARD_MOUNT_POINT) + dest_dir +
        "/" + BaseName(relative);
    const std::string dest_full_dir = std::string(SDCARD_MOUNT_POINT) + dest_dir;
    struct stat dest_info = {};
    if (stat(dest_full_dir.c_str(), &dest_info) != 0 || !S_ISDIR(dest_info.st_mode)) {
        error = "That destination folder was not found";
        return false;
    }
    struct stat info = {};
    const std::string full_path = std::string(SDCARD_MOUNT_POINT) + relative;
    if (stat(full_path.c_str(), &info) != 0) {
        error = "That item was not found";
        return false;
    }
    const int dest_depth = PathDepth(dest_dir) + (S_ISDIR(info.st_mode) ? 1 : 0);
    if (S_ISDIR(info.st_mode) && dest_depth > kMaxScanDepth) {
        error = "Folders can only be four levels deep";
        return false;
    }
    return RelocateMedia(relative, destination, error);
}

bool SdMusicPlayer::RelocateMedia(const std::string& relative,
    const std::string& destination, std::string& error) {
    const std::string source = std::string(SDCARD_MOUNT_POINT) + relative;
    if (source == destination) {
        return true;
    }
    struct stat existing = {};
    if (stat(destination.c_str(), &existing) == 0) {
        error = "An item with that name already exists";
        return false;
    }
    const bool was_playing = playing_;
    const std::string current_path = CurrentTrackPath(tracks_, current_track_);
    if (!PauseForMediaEdit()) {
        error = "Stop playback and try again";
        if (was_playing) {
            SendCommand(Command::kResume);
        }
        return false;
    }
    if (rename(source.c_str(), destination.c_str()) != 0) {
        error = "The item could not be moved";
        ResumeAfterMediaEdit(was_playing, current_path);
        return false;
    }
    const std::string new_relative = RelativePath(destination);
    if (!RewritePlaylistPrefix(relative, new_relative, error)) {
        ResumeAfterMediaEdit(was_playing,
            RemapFullPath(current_path, source, destination));
        return false;
    }
    ResumeAfterMediaEdit(was_playing, RemapFullPath(current_path, source, destination));
    display_->ShowNotification("Library updated");
    return true;
}

bool SdMusicPlayer::SetPlaylistOrder(const std::vector<std::string>& paths,
    std::string& error) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (!mounted_) {
        error = "Insert a FAT32 MicroSD card and restart the device";
        return false;
    }
    if (paths.size() > kMaxTracks) {
        error = "The playlist is too long";
        return false;
    }
    std::vector<std::string> relative_paths;
    relative_paths.reserve(paths.size());
    for (const auto& path : paths) {
        std::string relative;
        if (!NormalizeRelativeDirectory(path, relative, error) || relative.empty() ||
            !IsMp3File(relative)) {
            error = "The playlist contains an invalid track";
            return false;
        }
        relative_paths.push_back(std::move(relative));
    }

    const bool was_playing = playing_;
    const std::string current_path = CurrentTrackPath(tracks_, current_track_);
    if (!PauseForMediaEdit()) {
        error = "Stop playback and try again";
        if (was_playing) {
            SendCommand(Command::kResume);
        }
        return false;
    }
    if (!SavePlaylistOrder(relative_paths, error)) {
        ResumeAfterMediaEdit(was_playing, current_path);
        return false;
    }
    ResumeAfterMediaEdit(was_playing, current_path);
    display_->ShowNotification("Playlist updated");
    return true;
}

bool SdMusicPlayer::PauseForMediaEdit() {
    if (playing_) {
        playing_ = false;
    }
    int waits = 0;
    while (file_open_ && waits++ < 250) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return !file_open_;
}

void SdMusicPlayer::ResumeAfterMediaEdit(bool was_playing,
    const std::string& current_path) {
    RebuildPlaylist();
    current_track_ = 0;
    if (!current_path.empty()) {
        auto match = std::find(tracks_.begin(), tracks_.end(), current_path);
        if (match != tracks_.end()) {
            current_track_ = static_cast<size_t>(match - tracks_.begin());
        } else {
            resume_offset_ = 0;
            resume_elapsed_seconds_ = 0;
            playback_samples_ = 0;
            displayed_elapsed_seconds_ = UINT32_MAX;
        }
    }
    EnsureTask();
    RefreshDisplay();
    if (was_playing && !tracks_.empty()) {
        SendCommand(Command::kResume);
    }
}

void SdMusicPlayer::RebuildPlaylist() {
    tracks_.clear();
    ScanDirectory(SDCARD_MOUNT_POINT);
    ApplySavedOrder();
    if (current_track_ >= tracks_.size()) {
        current_track_ = 0;
    }
}

void SdMusicPlayer::ApplySavedOrder() {
    FILE* file = fopen(kPlaylistFile, "rb");
    if (file == nullptr) {
        std::sort(tracks_.begin(), tracks_.end());
        return;
    }
    std::vector<std::string> ordered;
    char line[1024] = {};
    while (fgets(line, sizeof(line), file) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        const std::string full_path = strncmp(line, SDCARD_MOUNT_POINT,
            sizeof(SDCARD_MOUNT_POINT) - 1) == 0
            ? std::string(line)
            : std::string(SDCARD_MOUNT_POINT) + line;
        if (std::find(tracks_.begin(), tracks_.end(), full_path) != tracks_.end() &&
            std::find(ordered.begin(), ordered.end(), full_path) == ordered.end()) {
            ordered.push_back(full_path);
        }
    }
    fclose(file);
    std::vector<std::string> remainder;
    for (const auto& track : tracks_) {
        if (std::find(ordered.begin(), ordered.end(), track) == ordered.end()) {
            remainder.push_back(track);
        }
    }
    std::sort(remainder.begin(), remainder.end());
    ordered.insert(ordered.end(), remainder.begin(), remainder.end());
    tracks_ = std::move(ordered);
}

bool SdMusicPlayer::SavePlaylistOrder(const std::vector<std::string>& relative_paths,
    std::string& error) {
    if (!WriteLinesAtomically(kPlaylistFile, kPlaylistTempFile, [&](FILE* file) {
            for (const auto& path : relative_paths) {
                if (fprintf(file, "%s\n", path.c_str()) < 0) {
                    return false;
                }
            }
            return true;
        }, "Failed to write playlist")) {
        error = "The playlist could not be saved";
        return false;
    }
    return true;
}

bool SdMusicPlayer::RewritePlaylistPrefix(const std::string& old_relative,
    const std::string& new_relative, std::string& error) {
    if (old_relative == new_relative) {
        return true;
    }
    FILE* file = fopen(kPlaylistFile, "rb");
    if (file == nullptr) {
        return true;
    }
    std::vector<std::string> paths;
    char line[1024] = {};
    while (fgets(line, sizeof(line), file) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        std::string relative = line;
        if (strncmp(line, SDCARD_MOUNT_POINT, sizeof(SDCARD_MOUNT_POINT) - 1) == 0) {
            relative = RelativePath(line);
        }
        if (relative == old_relative) {
            relative = new_relative;
        } else if (relative.size() > old_relative.size() &&
            relative.compare(0, old_relative.size(), old_relative) == 0 &&
            relative[old_relative.size()] == '/') {
            relative = new_relative + relative.substr(old_relative.size());
        }
        paths.push_back(std::move(relative));
    }
    fclose(file);
    return SavePlaylistOrder(paths, error);
}

bool SdMusicPlayer::RemoveTree(const std::string& full_path, int depth,
    std::string& error) {
    if (depth > kMaxScanDepth + 1) {
        error = "That folder is too deep to remove";
        return false;
    }
    DIR* dir = opendir(full_path.c_str());
    if (dir == nullptr) {
        error = "That folder could not be opened";
        return false;
    }
    std::vector<std::string> directories;
    std::vector<std::string> files;
    while (auto* item = readdir(dir)) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        std::string child = full_path + "/" + item->d_name;
        struct stat info = {};
        if (stat(child.c_str(), &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            directories.push_back(std::move(child));
        } else {
            files.push_back(std::move(child));
        }
    }
    closedir(dir);
    for (const auto& file : files) {
        if (remove(file.c_str()) != 0) {
            error = "The folder could not be emptied";
            return false;
        }
    }
    for (const auto& directory : directories) {
        if (!RemoveTree(directory, depth + 1, error)) {
            return false;
        }
    }
    if (rmdir(full_path.c_str()) != 0) {
        error = "The folder could not be removed";
        return false;
    }
    return true;
}

void SdMusicPlayer::EnsureTask() {
    if (task_ != nullptr || command_queue_ == nullptr || tracks_.empty()) {
        return;
    }
    xTaskCreatePinnedToCore(TaskEntry, "sd_music", 8192, this, 3, &task_, 1);
}

void SdMusicPlayer::RefreshDisplay() {
    if (tracks_.empty()) {
        display_->SetMusicInfo("No MP3 files", "Copy MP3 files to the SD card");
        display_->SetMusicProgress(0, 0);
        return;
    }
    ShowTrack(playing_ ? "Playing" : "Paused");
}
