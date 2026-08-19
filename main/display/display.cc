#include "display.h"
#include <esp_err.h>
#include <esp_log.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "settings.h"

#include <sdkconfig.h>

#ifndef CONFIG_IDLE_CLOCK_TIMEOUT_S
#define CONFIG_IDLE_CLOCK_TIMEOUT_S 1800
#endif
#ifndef CONFIG_STANDBY_DIM_TIMEOUT_S
#define CONFIG_STANDBY_DIM_TIMEOUT_S 60
#endif
#ifndef CONFIG_STANDBY_DIM_BRIGHTNESS
#define CONFIG_STANDBY_DIM_BRIGHTNESS 10
#endif

#define TAG "Display"

namespace {

int ClampInt(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

}  // namespace

Display::Display() {
    Settings settings("display");
    clock_timeout_s_ =
        ClampInt(settings.GetInt("clock_timeout", CONFIG_IDLE_CLOCK_TIMEOUT_S), 0, 86400);
    dim_timeout_s_ =
        ClampInt(settings.GetInt("dim_timeout", CONFIG_STANDBY_DIM_TIMEOUT_S), 0, 86400);
    dim_brightness_ =
        ClampInt(settings.GetInt("dim_brightness", CONFIG_STANDBY_DIM_BRIGHTNESS), 0, 100);
}

Display::~Display() {}

void Display::SetStatus(const char* status) { ESP_LOGW(TAG, "SetStatus: %s", status); }

void Display::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms) {
    ESP_LOGW(TAG, "ShowNotification: %s", notification);
}

void Display::UpdateStatusBar(bool update_all) {}

void Display::SetEmotion(const char* emotion) { ESP_LOGW(TAG, "SetEmotion: %s", emotion); }

void Display::SetChatMessage(const char* role, const char* content) {
    ESP_LOGW(TAG, "Role:%s", role);
    ESP_LOGW(TAG, "     %s", content);
}

void Display::ClearChatMessages() {
    // Default empty implementation, override in subclasses if needed
}

void Display::SetTheme(Theme* theme) {
    current_theme_ = theme;
    Settings settings("display", true);
    settings.SetString("theme", theme->name());
}

void Display::SetPowerSaveMode(bool on) { ESP_LOGW(TAG, "SetPowerSaveMode: %d", on); }

void Display::OnClockTick() {
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle || !app.CanEnterSleepMode()) {
        if (idle_seconds_ != 0 || screen_dimmed_ || clock_visible_) {
            DismissIdleEffects();
        }
        return;
    }

    if (idle_seconds_ < 86400) {
        idle_seconds_++;
    }

    bool dim = dim_timeout_s_ > 0 && idle_seconds_ >= dim_timeout_s_;
    bool clock = clock_timeout_s_ > 0 && idle_seconds_ >= clock_timeout_s_;
    bool overlay_changed = false;

    if (dim && !screen_dimmed_) {
        if (auto backlight = Board::GetInstance().GetBacklight()) {
            if (backlight->brightness() > static_cast<uint8_t>(dim_brightness_)) {
                backlight->SetBrightness(static_cast<uint8_t>(dim_brightness_));
                screen_dimmed_ = true;
                overlay_changed = true;
                ESP_LOGI(TAG, "Standby dim after %d s, brightness=%d", idle_seconds_,
                         dim_brightness_);
            }
        }
    }

    if (clock && !clock_visible_) {
        clock_visible_ = true;
        overlay_changed = true;
        ESP_LOGI(TAG, "Idle clock after %d s", idle_seconds_);
    }

    if (overlay_changed || clock_visible_) {
        UpdateIdleOverlay();
    }
}

void Display::DismissIdleEffects() {
    bool restore_backlight = screen_dimmed_;
    bool hide_overlay = screen_dimmed_ || clock_visible_;
    idle_seconds_ = 0;
    screen_dimmed_ = false;
    clock_visible_ = false;

    if (hide_overlay) {
        UpdateIdleOverlay();
    }
    if (restore_backlight) {
        if (auto backlight = Board::GetInstance().GetBacklight()) {
            backlight->RestoreBrightness();
        }
    }
}

void Display::UpdateIdleOverlay() {}

void Display::SetIdleClockTimeout(int seconds) {
    clock_timeout_s_ = ClampInt(seconds, 0, 86400);
    Settings settings("display", true);
    settings.SetInt("clock_timeout", clock_timeout_s_);
    if (clock_timeout_s_ == 0 && clock_visible_) {
        clock_visible_ = false;
        UpdateIdleOverlay();
    }
}

void Display::SetStandbyDimTimeout(int seconds) {
    dim_timeout_s_ = ClampInt(seconds, 0, 86400);
    Settings settings("display", true);
    settings.SetInt("dim_timeout", dim_timeout_s_);
    if (dim_timeout_s_ == 0 && screen_dimmed_) {
        screen_dimmed_ = false;
        UpdateIdleOverlay();
        if (auto backlight = Board::GetInstance().GetBacklight()) {
            backlight->RestoreBrightness();
        }
    }
}

void Display::SetStandbyDimBrightness(int brightness) {
    dim_brightness_ = ClampInt(brightness, 0, 100);
    Settings settings("display", true);
    settings.SetInt("dim_brightness", dim_brightness_);
    if (screen_dimmed_) {
        if (auto backlight = Board::GetInstance().GetBacklight()) {
            backlight->SetBrightness(static_cast<uint8_t>(dim_brightness_));
        }
    }
}
