#include <esp_err.h>
#include <esp_log.h>
#include <material_symbols.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "dynamic_glyph_cache.h"
#include "jpg/image_to_jpeg.h"
#include "lvgl_display.h"
#include "lvgl_theme.h"
#include "settings.h"

#define TAG "Display"

LvglDisplay::LvglDisplay() {
    dynamic_glyph_cache_ = std::make_unique<DynamicGlyphCache>();
    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
        .callback =
            [](void* arg) {
                LvglDisplay* display = static_cast<LvglDisplay*>(arg);
                DisplayLockGuard lock(display);
                lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

bool LvglDisplay::SetTextFont(std::shared_ptr<LvglFont> text_font) {
    if (text_font == nullptr || text_font->font() == nullptr) {
        return false;
    }

    DisplayLockGuard lock(this);
    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");
    if (light_theme == nullptr && dark_theme == nullptr) {
        return false;
    }

    // LVGL styles keep raw lv_font_t pointers. Keep the previous font owners alive until the
    // current theme has rebound every style to the new font.
    auto previous_light_font = light_theme != nullptr ? light_theme->text_font() : nullptr;
    auto previous_dark_font = dark_theme != nullptr ? dark_theme->text_font() : nullptr;
    if (light_theme != nullptr) {
        light_theme->set_text_font(text_font);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_text_font(text_font);
    }
    if (current_theme_ != nullptr) {
        SetTheme(current_theme_);
    }
    previous_light_font.reset();
    previous_dark_font.reset();
    return true;
}

bool LvglDisplay::AddTextGlyphs(const std::vector<TextGlyph>& glyphs, uint8_t bpp) {
    if (dynamic_glyph_cache_ == nullptr) {
        return false;
    }
    if (glyphs.empty()) {
        if (!TextGlyphStorageUsesPsram()) {
            ClearTextGlyphs();
        }
        return false;
    }
    if (bpp != 1 && bpp != 4) {
        return false;
    }

    DisplayLockGuard lock(this);
    auto theme = dynamic_cast<LvglTheme*>(current_theme_);
    if (theme == nullptr || theme->text_font() == nullptr) {
        return false;
    }

    auto fallback = dynamic_glyph_cache_->EnsureFont(theme->text_font()->font(), bpp);
    if (fallback == nullptr) {
        return false;
    }
    theme->text_font()->SetFallback(fallback);
    return dynamic_glyph_cache_->AddGlyphs(glyphs);
}

void LvglDisplay::ClearTextGlyphs() {
    if (dynamic_glyph_cache_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    dynamic_glyph_cache_->Clear();
}

LvglDisplay::~LvglDisplay() {
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }

    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
    }
    if (notification_label_ != nullptr) {
        lv_obj_del(notification_label_);
    }
    if (status_label_ != nullptr) {
        lv_obj_del(status_label_);
    }
    if (mute_label_ != nullptr) {
        lv_obj_del(mute_label_);
    }
    if (battery_label_ != nullptr) {
        lv_obj_del(battery_label_);
    }
    if (low_battery_popup_ != nullptr) {
        lv_obj_del(low_battery_popup_);
    }
    if (idle_overlay_ != nullptr) {
        lv_obj_del(idle_overlay_);
        idle_overlay_ = nullptr;
        idle_clock_label_ = nullptr;
        idle_date_label_ = nullptr;
    }
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

void LvglDisplay::SetStatus(const char* status) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetStatus('%s') called before SetupUI() - message will be lost!", status);
    }
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetStatus('%s') failed: status_label_ is nullptr (SetupUI() was called but "
                     "label not created)",
                     status);
        }
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    last_status_update_time_ = std::chrono::system_clock::now();
}

void LvglDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void LvglDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "ShowNotification('%s') called before SetupUI() - message will be lost!",
                 notification);
    }
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "ShowNotification('%s') failed: notification_label_ is nullptr (SetupUI() was "
                     "called but label not created)",
                     notification);
        }
        return;
    }
    lv_label_set_text(notification_label_, notification);
    lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

void LvglDisplay::UpdateStatusBar(bool update_all) {
    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    // Update mute icon
    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) {
            return;
        }

        // Update icon if mute state changes
        if (codec->output_volume() == 0 && !muted_) {
            muted_ = true;
            lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_OFF);
        } else if (codec->output_volume() > 0 && muted_) {
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }

    // Update time
    if (app.GetDeviceState() == kDeviceStateIdle) {
        if (last_status_update_time_ + std::chrono::seconds(10) <
            std::chrono::system_clock::now()) {
            // Set status to clock "HH:MM"
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            // Check if the we have already set the time
            if (tm->tm_year >= 2025 - 1900) {
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M", tm);
                SetStatus(time_str);
            } else {
                ESP_LOGW(TAG, "System time is not set, tm_year: %d", tm->tm_year);
            }
        }
    }

    esp_pm_lock_acquire(pm_lock_);
    // Update battery icon
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        if (charging) {
            icon = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT;
        } else {
            const char* levels[] = {
                MATERIAL_SYMBOLS_BATTERY_ANDROID_0,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_1,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_2,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_3,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_4,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_5,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_6,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL,
            };
            int level_index = battery_level <= 0
                                  ? 0
                                  : (battery_level >= 100 ? 7 : 1 + ((battery_level - 1) * 6 / 99));
            icon = levels[level_index];
        }
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && battery_icon_ != icon) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, battery_icon_);
        }

        // Check low battery popup only when clock tick event is triggered
        // Because when initializing, the battery level is not ready yet.
        if (low_battery_popup_ != nullptr && !update_all) {
            if (strcmp(icon, MATERIAL_SYMBOLS_BATTERY_ANDROID_0) == 0 && discharging) {
                if (lv_obj_has_flag(low_battery_popup_,
                                    LV_OBJ_FLAG_HIDDEN)) {  // Show if low battery popup is hidden
                    lv_obj_remove_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                    app.Schedule([&app]() { app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY); });
                }
            } else {
                // Hide the low battery popup when the battery is not empty
                if (!lv_obj_has_flag(low_battery_popup_,
                                     LV_OBJ_FLAG_HIDDEN)) {  // Hide if low battery popup is shown
                    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // Update network icon every 10 seconds
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        // Don't read 4G network status during firmware upgrade to avoid occupying UART resources
        auto device_state = Application::GetInstance().GetDeviceState();
        static const std::vector<DeviceState> allowed_states = {
            kDeviceStateIdle,      kDeviceStateStarting,   kDeviceStateWifiConfiguring,
            kDeviceStateListening, kDeviceStateActivating,
        };
        if (std::find(allowed_states.begin(), allowed_states.end(), device_state) !=
            allowed_states.end()) {
            icon = board.GetNetworkStateIcon();
            if (network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
                DisplayLockGuard lock(this);
                network_icon_ = icon;
                lv_label_set_text(network_label_, network_icon_);
            }
        }
    }

    esp_pm_lock_release(pm_lock_);
}

void LvglDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {}

void LvglDisplay::SetPowerSaveMode(bool on) {
    if (on) {
        SetChatMessage("system", "");
        SetEmotion("sleepy");
    } else {
        SetChatMessage("system", "");
        SetEmotion("neutral");
    }
}

void LvglDisplay::EnsureIdleOverlay() {
    if (idle_overlay_ != nullptr || !setup_ui_called_) {
        return;
    }

    auto screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    auto lvgl_theme = dynamic_cast<LvglTheme*>(current_theme_);
    const lv_font_t* text_font =
        (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr)
            ? lvgl_theme->text_font()->font()
            : nullptr;

    idle_overlay_ = lv_obj_create(screen);
    lv_obj_set_size(idle_overlay_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(idle_overlay_, 0, 0);
    lv_obj_set_style_border_width(idle_overlay_, 0, 0);
    lv_obj_set_style_pad_all(idle_overlay_, 0, 0);
    lv_obj_set_scrollbar_mode(idle_overlay_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(idle_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(idle_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        idle_overlay_,
        [](lv_event_t* e) {
            auto display = static_cast<LvglDisplay*>(lv_event_get_user_data(e));
            Application::GetInstance().Schedule([display]() {
                if (display != nullptr) {
                    display->DismissIdleEffects();
                }
            });
        },
        LV_EVENT_CLICKED, this);

    idle_clock_label_ = lv_label_create(idle_overlay_);
    lv_label_set_text(idle_clock_label_, "--:--");
    lv_obj_set_style_text_align(idle_clock_label_, LV_TEXT_ALIGN_CENTER, 0);
    if (text_font != nullptr) {
        lv_obj_set_style_text_font(idle_clock_label_, text_font, 0);
    }
    const int clock_offset_y = height_ > 32 ? -12 : 0;
    lv_obj_align(idle_clock_label_, LV_ALIGN_CENTER, 0, clock_offset_y);
    if (height_ >= 160) {
        lv_obj_set_style_transform_pivot_x(idle_clock_label_, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(idle_clock_label_, lv_pct(50), 0);
        lv_obj_set_style_transform_scale(idle_clock_label_, 512, 0);
    }

    idle_date_label_ = lv_label_create(idle_overlay_);
    lv_label_set_text(idle_date_label_, "");
    lv_obj_set_style_text_align(idle_date_label_, LV_TEXT_ALIGN_CENTER, 0);
    if (text_font != nullptr) {
        lv_obj_set_style_text_font(idle_date_label_, text_font, 0);
    }
    lv_obj_align(idle_date_label_, LV_ALIGN_CENTER, 0, height_ >= 160 ? 36 : 12);
    if (height_ <= 32) {
        lv_obj_add_flag(idle_date_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LvglDisplay::RefreshIdleClockText() {
    if (idle_clock_label_ == nullptr) {
        return;
    }

    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    if (tm == nullptr || tm->tm_year < 2025 - 1900) {
        lv_label_set_text(idle_clock_label_, "--:--");
        if (idle_date_label_ != nullptr) {
            lv_label_set_text(idle_date_label_, "");
        }
        return;
    }

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M", tm);
    lv_label_set_text(idle_clock_label_, time_str);

    if (idle_date_label_ != nullptr && height_ > 32) {
        char date_str[16];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm);
        lv_label_set_text(idle_date_label_, date_str);
    }
}

void LvglDisplay::UpdateIdleOverlay() {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) {
        return;
    }

    if (!screen_dimmed_ && !clock_visible_) {
        if (idle_overlay_ != nullptr) {
            lv_obj_add_flag(idle_overlay_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    EnsureIdleOverlay();
    if (idle_overlay_ == nullptr) {
        return;
    }

    auto lvgl_theme = dynamic_cast<LvglTheme*>(current_theme_);
    lv_color_t bg = lv_color_black();
    lv_color_t fg = lv_color_white();
    if (lvgl_theme != nullptr) {
        bg = lvgl_theme->background_color();
        fg = lvgl_theme->text_color();
    }

    if (clock_visible_) {
        lv_obj_set_style_bg_opa(idle_overlay_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(idle_overlay_, bg, 0);
        if (idle_clock_label_ != nullptr) {
            lv_obj_set_style_text_color(idle_clock_label_, fg, 0);
            lv_obj_remove_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (idle_date_label_ != nullptr) {
            lv_obj_set_style_text_color(idle_date_label_, fg, 0);
            if (height_ > 32) {
                lv_obj_remove_flag(idle_date_label_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        RefreshIdleClockText();
    } else {
        lv_obj_set_style_bg_opa(idle_overlay_, LV_OPA_TRANSP, 0);
        if (idle_clock_label_ != nullptr) {
            lv_obj_add_flag(idle_clock_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (idle_date_label_ != nullptr) {
            lv_obj_add_flag(idle_date_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_remove_flag(idle_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(idle_overlay_);
}

bool LvglDisplay::SnapshotToJpeg(std::string& jpeg_data, int quality) {
#if CONFIG_LV_USE_SNAPSHOT
    DisplayLockGuard lock(this);

    lv_obj_t* screen = lv_screen_active();
    // RGB888 avoids RGB565 endian issues. JPEG encoding uses 4:4:4 chroma.
    v4l2_pix_fmt_t fmt = V4L2_PIX_FMT_RGB24;
    lv_draw_buf_t* draw_buffer = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB888);
    if (draw_buffer == nullptr) {
        fmt = V4L2_PIX_FMT_RGB565;
        draw_buffer = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    }
    if (draw_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to take snapshot, draw_buffer is nullptr");
        return false;
    }

    const uint8_t px_size =
        lv_color_format_get_size(static_cast<lv_color_format_t>(draw_buffer->header.cf));
    const uint16_t width = draw_buffer->header.w;
    const uint16_t height = draw_buffer->header.h;
    const uint32_t stride = draw_buffer->header.stride ? draw_buffer->header.stride : static_cast<uint32_t>(width) * px_size;
    const uint32_t packed_stride = static_cast<uint32_t>(width) * px_size;
    const size_t packed_len = packed_stride * height;

    // LVGL RGB888 is stored B,G,R. JPEG RGB24 expects R,G,B.
    std::string packed;
    const uint8_t* pixels = draw_buffer->data;
    if (fmt == V4L2_PIX_FMT_RGB24) {
        packed.resize(packed_len);
        for (uint16_t y = 0; y < height; y++) {
            const uint8_t* src = draw_buffer->data + static_cast<size_t>(y) * stride;
            uint8_t* dst = reinterpret_cast<uint8_t*>(packed.data()) + y * packed_stride;
            for (uint16_t x = 0; x < width; x++) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                src += 3;
                dst += 3;
            }
        }
        pixels = reinterpret_cast<const uint8_t*>(packed.data());
    } else if (stride != packed_stride) {
        packed.resize(packed_len);
        for (uint16_t y = 0; y < height; y++) {
            memcpy(packed.data() + y * packed_stride,
                   draw_buffer->data + static_cast<size_t>(y) * stride, packed_stride);
        }
        pixels = reinterpret_cast<const uint8_t*>(packed.data());
    }

    jpeg_data.clear();
    bool ret = image_to_jpeg_cb(
        const_cast<uint8_t*>(pixels), packed_len, width, height, fmt, quality,
        [](void* arg, size_t /*index*/, const void* data, size_t len) -> size_t {
            std::string* output = static_cast<std::string*>(arg);
            if (data && len > 0) {
                output->append(static_cast<const char*>(data), len);
            }
            return len;
        },
        &jpeg_data);
    if (!ret) {
        ESP_LOGE(TAG, "Failed to convert image to JPEG");
    }

    lv_draw_buf_destroy(draw_buffer);
    return ret;
#else
    ESP_LOGE(TAG, "LV_USE_SNAPSHOT is not enabled");
    return false;
#endif
}
