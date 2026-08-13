#include "application.h"
#include "adc_battery_monitor.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "es3c28p_display.h"
#include "es3c28p_web_server.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "sd_music_player.h"
#include "settings.h"
#include "theme_package.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include <wifi_station.h>

#include <algorithm>
#include <array>

#define TAG "ES3C28P"

class Es3c28pBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    Es3c28pDisplay* display_ = nullptr;
    AdcBatteryMonitor* battery_monitor_ = nullptr;
    SdMusicPlayer* music_player_ = nullptr;
    Es3c28pWebServer web_server_;
    lv_indev_t* touch_input_ = nullptr;
    lv_indev_read_cb_t touch_read_cb_ = nullptr;
    lv_obj_t* calibration_overlay_ = nullptr;
    lv_obj_t* calibration_target_ = nullptr;
    lv_obj_t* calibration_hint_ = nullptr;
    bool touch_calibrated_ = false;
    bool calibrating_touch_ = false;
    bool calibration_just_finished_ = false;
    int32_t touch_raw_left_ = 0;
    int32_t touch_raw_right_ = DISPLAY_WIDTH - 1;
    int32_t touch_raw_top_ = 0;
    int32_t touch_raw_bottom_ = DISPLAY_HEIGHT - 1;
    lv_point_t last_raw_touch_ = {};
    bool last_raw_touch_valid_ = false;
    std::array<lv_point_t, 4> calibration_points_ = {};
    size_t calibration_step_ = 0;

    static constexpr std::array<lv_point_t, 4> kCalibrationTargets = {{
        {18, 18},
        {DISPLAY_WIDTH - 19, 18},
        {DISPLAY_WIDTH - 19, DISPLAY_HEIGHT - 19},
        {18, DISPLAY_HEIGHT - 19},
    }};

    static void CalibratedTouchRead(lv_indev_t* indev,
        lv_indev_data_t* data) {
        auto* board = static_cast<Es3c28pBoard*>(lv_indev_get_user_data(indev));
        board->touch_read_cb_(indev, data);
        if (data->state != LV_INDEV_STATE_PRESSED) {
            return;
        }
        board->last_raw_touch_ = data->point;
        board->last_raw_touch_valid_ = true;
        if (!board->touch_calibrated_ || board->calibrating_touch_) {
            return;
        }
        const int32_t raw_width = board->touch_raw_right_ -
            board->touch_raw_left_;
        const int32_t raw_height = board->touch_raw_bottom_ -
            board->touch_raw_top_;
        if (raw_width <= 0 || raw_height <= 0) {
            return;
        }
        const int32_t mapped_x = kCalibrationTargets[0].x +
            (static_cast<int32_t>(data->point.x) - board->touch_raw_left_) *
                (kCalibrationTargets[1].x - kCalibrationTargets[0].x) /
                raw_width;
        const int32_t mapped_y = kCalibrationTargets[0].y +
            (static_cast<int32_t>(data->point.y) - board->touch_raw_top_) *
                (kCalibrationTargets[3].y - kCalibrationTargets[0].y) /
                raw_height;
        data->point.x = std::clamp<int32_t>(mapped_x, 0, DISPLAY_WIDTH - 1);
        data->point.y = std::clamp<int32_t>(mapped_y, 0, DISPLAY_HEIGHT - 1);
    }

    void LoadTouchCalibration() {
        Settings settings("touch_cal", false);
        if (settings.GetInt("version", 0) != 2) {
            return;
        }
        touch_raw_left_ = settings.GetInt("left", 0);
        touch_raw_right_ = settings.GetInt("right", DISPLAY_WIDTH - 1);
        touch_raw_top_ = settings.GetInt("top", 0);
        touch_raw_bottom_ = settings.GetInt("bottom", DISPLAY_HEIGHT - 1);
        touch_calibrated_ = touch_raw_right_ > touch_raw_left_ + 50 &&
            touch_raw_bottom_ > touch_raw_top_ + 50;
        if (touch_calibrated_) {
            ESP_LOGI(TAG, "Touch calibration loaded: x=%ld..%ld y=%ld..%ld",
                static_cast<long>(touch_raw_left_),
                static_cast<long>(touch_raw_right_),
                static_cast<long>(touch_raw_top_),
                static_cast<long>(touch_raw_bottom_));
        }
    }

    void PositionCalibrationTarget() {
        const auto& target = kCalibrationTargets[calibration_step_];
        lv_obj_set_pos(calibration_target_, target.x - 14, target.y - 14);
        char hint[48];
        snprintf(hint, sizeof(hint), "TOUCH THE CROSS  %u/4",
            static_cast<unsigned>(calibration_step_ + 1));
        lv_label_set_text(calibration_hint_, hint);
    }

    void StartTouchCalibration() {
        calibrating_touch_ = true;
        touch_calibrated_ = false;
        calibration_step_ = 0;
        last_raw_touch_valid_ = false;
        lvgl_port_lock(0);
        calibration_overlay_ = lv_obj_create(lv_screen_active());
        lv_obj_set_pos(calibration_overlay_, 0, 0);
        lv_obj_set_size(calibration_overlay_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_style_radius(calibration_overlay_, 0, 0);
        lv_obj_set_style_border_width(calibration_overlay_, 0, 0);
        lv_obj_set_style_pad_all(calibration_overlay_, 0, 0);
        lv_obj_set_style_bg_color(calibration_overlay_, lv_color_hex(0x101315), 0);
        lv_obj_set_style_bg_opa(calibration_overlay_, LV_OPA_COVER, 0);
        lv_obj_add_flag(calibration_overlay_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(calibration_overlay_, LV_OBJ_FLAG_SCROLLABLE);

        auto* title = lv_label_create(calibration_overlay_);
        lv_label_set_text(title, "TOUCH CALIBRATION");
        lv_obj_set_style_text_color(title, lv_color_hex(0xF0F0EC), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 72);
        calibration_hint_ = lv_label_create(calibration_overlay_);
        lv_obj_set_style_text_color(calibration_hint_, lv_color_hex(0x69D8DE), 0);
        lv_obj_align(calibration_hint_, LV_ALIGN_TOP_MID, 0, 98);

        calibration_target_ = lv_obj_create(calibration_overlay_);
        lv_obj_set_size(calibration_target_, 28, 28);
        lv_obj_set_style_bg_opa(calibration_target_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(calibration_target_, 0, 0);
        lv_obj_set_style_pad_all(calibration_target_, 0, 0);
        lv_obj_remove_flag(calibration_target_, LV_OBJ_FLAG_SCROLLABLE);
        auto* horizontal = lv_obj_create(calibration_target_);
        lv_obj_set_pos(horizontal, 4, 13);
        lv_obj_set_size(horizontal, 20, 2);
        lv_obj_set_style_bg_color(horizontal, lv_color_hex(0xF6BE4A), 0);
        lv_obj_set_style_bg_opa(horizontal, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(horizontal, 0, 0);
        lv_obj_set_style_radius(horizontal, 0, 0);
        auto* vertical = lv_obj_create(calibration_target_);
        lv_obj_set_pos(vertical, 13, 4);
        lv_obj_set_size(vertical, 2, 20);
        lv_obj_set_style_bg_color(vertical, lv_color_hex(0xF6BE4A), 0);
        lv_obj_set_style_bg_opa(vertical, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(vertical, 0, 0);
        lv_obj_set_style_radius(vertical, 0, 0);
        PositionCalibrationTarget();
        lv_obj_move_foreground(calibration_overlay_);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Starting first-run touch calibration");
    }

    void HandleCalibrationTouch() {
        if (!calibrating_touch_ || !last_raw_touch_valid_ ||
            calibration_step_ >= calibration_points_.size()) {
            return;
        }
        calibration_points_[calibration_step_++] = last_raw_touch_;
        last_raw_touch_valid_ = false;
        if (calibration_step_ < calibration_points_.size()) {
            PositionCalibrationTarget();
            return;
        }

        touch_raw_left_ = (calibration_points_[0].x +
            calibration_points_[3].x) / 2;
        touch_raw_right_ = (calibration_points_[1].x +
            calibration_points_[2].x) / 2;
        touch_raw_top_ = (calibration_points_[0].y +
            calibration_points_[1].y) / 2;
        touch_raw_bottom_ = (calibration_points_[2].y +
            calibration_points_[3].y) / 2;
        if (touch_raw_right_ <= touch_raw_left_ + 50 ||
            touch_raw_bottom_ <= touch_raw_top_ + 50) {
            calibration_step_ = 0;
            lv_label_set_text(calibration_hint_, "TRY AGAIN - TOUCH THE CROSS  1/4");
            PositionCalibrationTarget();
            return;
        }

        Settings settings("touch_cal", true);
        settings.SetInt("left", touch_raw_left_);
        settings.SetInt("right", touch_raw_right_);
        settings.SetInt("top", touch_raw_top_);
        settings.SetInt("bottom", touch_raw_bottom_);
        settings.SetInt("version", 2);
        touch_calibrated_ = true;
        calibrating_touch_ = false;
        calibration_just_finished_ = true;
        lv_obj_delete(calibration_overlay_);
        calibration_overlay_ = nullptr;
        calibration_target_ = nullptr;
        calibration_hint_ = nullptr;
        lv_indev_reset(touch_input_, nullptr);
        ESP_LOGI(TAG, "Touch calibrated: x=%ld..%ld y=%ld..%ld",
            static_cast<long>(touch_raw_left_),
            static_cast<long>(touch_raw_right_),
            static_cast<long>(touch_raw_top_),
            static_cast<long>(touch_raw_bottom_));
    }

    void InitializeI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = DISPLAY_MOSI_PIN;
        bus_config.miso_io_num = DISPLAY_MISO_PIN;
        bus_config.sclk_io_num = DISPLAY_SCLK_PIN;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RESET_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new Es3c28pDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        esp_lcd_panel_io_handle_t touch_io = nullptr;
        esp_lcd_touch_handle_t touch = nullptr;

        esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        touch_io_config.scl_speed_hz = 400 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &touch_io_config, &touch_io));

        esp_lcd_touch_config_t touch_config = {
            .x_max = DISPLAY_HEIGHT,
            .y_max = DISPLAY_WIDTH,
            .rst_gpio_num = TOUCH_RESET_PIN,
            // Polling is more reliable with the FT6336G on this board. Using
            // the interrupt pin switches LVGL to event mode and can leave the
            // controller unread when no compatible interrupt edge is emitted.
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 1,
                // The controller is read in portrait coordinates before swap_xy.
                // Mirroring X here corrects the final landscape Y axis.
                .mirror_x = 1,
                .mirror_y = 0,
            },
        };
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(touch_io, &touch_config, &touch));

        const lvgl_port_touch_cfg_t lvgl_touch_config = {
            .disp = lv_display_get_default(),
            .handle = touch,
        };
        touch_input_ = lvgl_port_add_touch(&lvgl_touch_config);
        ESP_ERROR_CHECK(touch_input_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
        touch_read_cb_ = lv_indev_get_read_cb(touch_input_);
        lv_indev_set_user_data(touch_input_, this);
        lv_indev_set_read_cb(touch_input_, CalibratedTouchRead);
        LoadTouchCalibration();
        lv_indev_add_event_cb(touch_input_, [](lv_event_t* event) {
            auto* board = static_cast<Es3c28pBoard*>(lv_event_get_user_data(event));
            board->HandleCalibrationTouch();
        }, LV_EVENT_RELEASED, this);
        lv_indev_add_event_cb(touch_input_, [](lv_event_t* event) {
            auto* board = static_cast<Es3c28pBoard*>(lv_event_get_user_data(event));
            if (board->calibrating_touch_ || board->calibration_just_finished_) {
                board->calibration_just_finished_ = false;
                return;
            }
            lv_point_t point;
            lv_indev_get_point(board->touch_input_, &point);
            ESP_LOGI(TAG, "Touch released at x=%ld y=%ld",
                static_cast<long>(point.x), static_cast<long>(point.y));
            if (board->display_->IsWifiPageActive()) {
                return;
            }
            if (board->display_->IsNavigationBarPoint(point.y)) {
                board->display_->ShowMusicPage(point.x >= DISPLAY_WIDTH / 2);
                return;
            }
            if (board->display_->IsMusicPageActive()) {
                return;
            }
        }, LV_EVENT_CLICKED, this);
        if (!touch_calibrated_) {
            StartTouchCalibration();
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting &&
                !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        boot_button_.OnLongPress([this]() {
            Settings settings("touch_cal", true);
            settings.EraseAll();
            ESP_LOGI(TAG, "Touch calibration cleared; restarting");
            esp_restart();
        });
    }

    void InitializeThemeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddUserOnlyTool("self.theme.get_info",
            "Get the active ES3C28P UI theme and whether it came from an external package.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                return Es3c28pThemePackage::GetInstance().Describe();
            });
        mcp_server.AddUserOnlyTool("self.theme.install",
            "Download and validate an ES3C28P .theme.bin package from an HTTP(S) URL. The device restarts only after validation succeeds.",
            PropertyList({Property("url", kPropertyTypeString)}),
            [](const PropertyList& properties) -> ReturnValue {
                const auto url = properties["url"].value<std::string>();
                auto& package = Es3c28pThemePackage::GetInstance();
                const bool ok = package.Download(url,
                    [](int progress, size_t speed) {
                        ESP_LOGI(TAG, "Theme update: %d%% (%u B/s)", progress,
                            static_cast<unsigned>(speed));
                    });
                if (!ok) {
                    return std::string("Theme package download or validation failed; the built-in fallback remains available.");
                }
                Application::GetInstance().Schedule([]() {
                    vTaskDelay(pdMS_TO_TICKS(800));
                    esp_restart();
                });
                return std::string("Theme installed and validated. Restarting device.");
            });
    }

public:
    Es3c28pBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        battery_monitor_ = new AdcBatteryMonitor(
            BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, 100000, 100000);
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeThemeTools();
        music_player_ = new SdMusicPlayer(
            Application::GetInstance().GetAudioService(), display_);
        GetBacklight()->RestoreBrightness();
        ESP_LOGI(TAG, "ES3C28P board initialized");
    }

    AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, true, true);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        return display_;
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = battery_monitor_->IsCharging();
        discharging = battery_monitor_->IsDischarging();
        level = battery_monitor_->GetBatteryLevel();
        return true;
    }

    void StartNetwork() override {
        WifiBoard::StartNetwork();
        if (web_server_.Start()) {
            ESP_LOGI(TAG, "Web configuration: %s", web_server_.url().c_str());
        }
    }
};

DECLARE_BOARD(Es3c28pBoard);
