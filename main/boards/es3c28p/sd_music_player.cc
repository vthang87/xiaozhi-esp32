#include "sd_music_player.h"

#include "application.h"
#include "audio/audio_service.h"
#include "config.h"
#include "es3c28p_display.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <micro_mp3/mp3_decoder.h>

#define TAG "SdMusicPlayer"

namespace {
constexpr size_t kInputBufferSize = 4096;
constexpr int kMaxScanDepth = 4;
constexpr size_t kMaxTracks = 256;
constexpr char kPlaybackStateFile[] = SDCARD_MOUNT_POINT "/.xiaozhi_music_state";
constexpr char kPlaybackStateTempFile[] = SDCARD_MOUNT_POINT "/.xiaozhi_music_state.tmp";

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

const char* BaseName(const std::string& path) {
    auto separator = path.find_last_of('/');
    return separator == std::string::npos ? path.c_str() : path.c_str() + separator + 1;
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

    ScanDirectory(SDCARD_MOUNT_POINT);
    std::sort(tracks_.begin(), tracks_.end());
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
        .max_files = 5,
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
    return keep_running;
}
