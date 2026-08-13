#include "es3c28p_display.h"

#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "sd_music_player.h"
#include "theme_package.h"
#include "wifi_board.h"

#include <font_awesome.h>

LV_FONT_DECLARE(font_awesome_14_1);
#include <esp_wifi.h>
#include <ssid_manager.h>
#include <wifi_station.h>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {
uint32_t kScreenColor = 0x17191B;
uint32_t kPanelColor = 0x222528;
uint32_t kLineColor = 0x383D41;
uint32_t kTextColor = 0xF4F1E9;
uint32_t kMutedColor = 0x999FA2;
uint32_t kAmberColor = 0xF0B84A;
uint32_t kCyanColor = 0x69C8CF;
uint32_t kPositiveColor = 0x78BD76;
uint32_t kHighlightColor = 0xF5C84E;
uint32_t kRoleColor = 0xA25245;
int kButtonRadius = 3;
bool kKidsTheme = false;

lv_obj_t* CreatePanel(lv_obj_t* parent, int x, int y, int width, int height,
    uint32_t background = kScreenColor) {
    auto* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

void StyleLabel(lv_obj_t* label, const lv_font_t* font, uint32_t color,
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
}

void StyleButton(lv_obj_t* button, uint32_t background, uint32_t border,
    int radius = -1) {
    if (radius < 0) {
        radius = kButtonRadius;
    }
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(border), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(border), LV_STATE_PRESSED);
}

void StyleStorybookButton(lv_obj_t* button, uint32_t background,
    int radius = 14) {
    StyleButton(button, background, kLineColor, radius);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 3, 0);
    lv_obj_set_style_shadow_offset_y(button, 3, 0);
    lv_obj_set_style_translate_y(button, 2, LV_STATE_PRESSED);
}

void CreateBatteryIndicator(lv_obj_t* parent, const lv_font_t* icon_font,
    const lv_font_t* text_font, lv_obj_t** icon, lv_obj_t** percentage) {
    auto* indicator = lv_obj_create(parent);
    lv_obj_set_pos(indicator, 241, 0);
    lv_obj_set_size(indicator, 70, 24);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_radius(indicator, 0, 0);
    lv_obj_set_style_pad_all(indicator, 0, 0);
    lv_obj_set_style_pad_column(indicator, 2, 0);
    lv_obj_set_flex_flow(indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(indicator, LV_FLEX_ALIGN_END,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);

    *icon = lv_label_create(indicator);
    lv_obj_set_size(*icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text(*icon, FONT_AWESOME_BATTERY_FULL);
    StyleLabel(*icon, icon_font, kCyanColor, LV_TEXT_ALIGN_CENTER);

    *percentage = lv_label_create(indicator);
    lv_obj_set_size(*percentage, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text(*percentage, "--%");
    StyleLabel(*percentage, text_font, kMutedColor, LV_TEXT_ALIGN_LEFT);
}

lv_obj_t* CreateWifiIndicator(lv_obj_t* parent) {
    auto* icon = lv_label_create(parent);
    lv_obj_set_pos(icon, 140, 2);
    lv_obj_set_size(icon, 20, 20);
    lv_label_set_text(icon, FONT_AWESOME_WIFI_SLASH);
    StyleLabel(icon, &font_awesome_14_1, kMutedColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_opa(icon, LV_OPA_COVER, 0);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    return icon;
}
}  // namespace

Es3c28pDisplay::Es3c28pDisplay(esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel, int width, int height, int offset_x,
    int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
          mirror_x, mirror_y, swap_xy) {
    auto& theme_package = Es3c28pThemePackage::GetInstance();
    theme_package.Load();
    const auto& tokens = theme_package.tokens();
    kScreenColor = tokens.screen;
    kPanelColor = tokens.panel;
    kLineColor = tokens.line;
    kTextColor = tokens.text;
    kMutedColor = tokens.muted;
    kAmberColor = tokens.primary;
    kCyanColor = tokens.secondary;
    kPositiveColor = tokens.positive;
    kHighlightColor = tokens.highlight;
    kRoleColor = tokens.role;
    kButtonRadius = tokens.radius;
    kKidsTheme = tokens.kids_mode;
    SetupMusicUi();
}

Es3c28pDisplay::~Es3c28pDisplay() {
    DisplayLockGuard lock(this);
    if (navigation_bar_ != nullptr) {
        lv_obj_delete(navigation_bar_);
        navigation_bar_ = nullptr;
    }
    if (music_page_ != nullptr) {
        lv_obj_delete(music_page_);
        music_page_ = nullptr;
    }
    if (browser_page_ != nullptr) {
        lv_obj_delete(browser_page_);
        browser_page_ = nullptr;
    }
    if (wifi_page_ != nullptr) {
        lv_obj_delete(wifi_page_);
        wifi_page_ = nullptr;
        wifi_password_page_ = nullptr;
    }
    if (wifi_info_page_ != nullptr) {
        lv_obj_delete(wifi_info_page_);
        wifi_info_page_ = nullptr;
    }
    if (chat_page_ != nullptr) {
        lv_obj_delete(chat_page_);
        chat_page_ = nullptr;
        chat_mic_button_ = nullptr;
        chat_mic_icon_ = nullptr;
    }
}

void Es3c28pDisplay::SetupStorybookUi() {
    auto* screen = lv_screen_active();
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    auto* text_font = theme->text_font()->font();
    auto* small_font = &lv_font_montserrat_14;
    auto* icon_font = theme->icon_font()->font();

    if (bottom_bar_ != nullptr) {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    if (top_bar_ != nullptr) {
        lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    auto create_storybook_status = [&](lv_obj_t* parent, const char* title,
                                       lv_obj_t** volume, lv_obj_t** wifi,
                                       lv_obj_t** battery_icon,
                                       lv_obj_t** battery_label) {
        auto* status = CreatePanel(parent, 0, 0, width_, 25, kCyanColor);
        auto* title_label = lv_label_create(status);
        lv_obj_set_pos(title_label, 9, 3);
        lv_label_set_text(title_label, title);
        StyleLabel(title_label, small_font, kTextColor);

        *volume = lv_label_create(status);
        lv_obj_set_pos(*volume, 143, 3);
        lv_obj_set_width(*volume, 66);
        lv_label_set_text(*volume, "VOL --%");
        StyleLabel(*volume, small_font, kTextColor, LV_TEXT_ALIGN_RIGHT);

        *wifi = CreateWifiIndicator(status);
        lv_obj_set_pos(*wifi, 211, 2);
        lv_obj_set_size(*wifi, 24, 21);
        lv_obj_set_style_text_color(*wifi, lv_color_hex(kTextColor), 0);
        lv_obj_add_event_cb(*wifi, OnWifiClicked, LV_EVENT_DOUBLE_CLICKED, this);

        CreateBatteryIndicator(status, icon_font, small_font,
            battery_icon, battery_label);
        auto* battery = lv_obj_get_parent(*battery_icon);
        lv_obj_set_pos(battery, 236, 0);
        lv_obj_set_size(battery, 76, 25);
        lv_obj_set_style_text_color(*battery_icon, lv_color_hex(kTextColor), 0);
        lv_obj_set_style_text_color(*battery_label, lv_color_hex(kTextColor), 0);
    };

    auto create_scenery = [&](lv_obj_t* page) {
        auto* hill = CreatePanel(page, -22, 153, 364, 88, kPositiveColor);
        lv_obj_set_style_radius(hill, 100, 0);
        lv_obj_set_style_border_width(hill, 2, 0);
        lv_obj_set_style_border_color(hill, lv_color_hex(kLineColor), 0);
        auto* sun = CreatePanel(page, 280, 37, 31, 31, kHighlightColor);
        lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(sun, 2, 0);
        lv_obj_set_style_border_color(sun, lv_color_hex(kLineColor), 0);
    };

    chat_page_ = CreatePanel(screen, 0, 0, width_,
        height_ - kNavigationHeight, kScreenColor);
    create_storybook_status(chat_page_, "HI, BUDDY", &chat_volume_label_,
        &chat_wifi_icon_, &chat_battery_icon_, &chat_battery_label_);
    create_scenery(chat_page_);

    auto* friend_panel = CreatePanel(chat_page_, 14, 48, 80, 84,
        kAmberColor);
    lv_obj_set_style_radius(friend_panel, 36, 0);
    lv_obj_set_style_border_width(friend_panel, 2, 0);
    lv_obj_set_style_border_color(friend_panel, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_shadow_color(friend_panel, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_shadow_opa(friend_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(friend_panel, 0, 0);
    lv_obj_set_style_shadow_offset_y(friend_panel, 3, 0);
    auto* friend_face = lv_label_create(friend_panel);
    lv_label_set_text(friend_face, "o  u  o");
    StyleLabel(friend_face, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(friend_face);

    auto* message_panel = CreatePanel(chat_page_, 103, 50, 207, 84,
        kPanelColor);
    lv_obj_set_style_radius(message_panel, 18, 0);
    lv_obj_set_style_border_width(message_panel, 2, 0);
    lv_obj_set_style_border_color(message_panel, lv_color_hex(kLineColor), 0);
    chat_assistant_role_label_ = lv_label_create(message_panel);
    lv_obj_set_pos(chat_assistant_role_label_, 10, 8);
    lv_label_set_text(chat_assistant_role_label_, "YOUR AI FRIEND");
    StyleLabel(chat_assistant_role_label_, small_font, kRoleColor);
    chat_assistant_label_ = lv_label_create(message_panel);
    chat_user_label_ = chat_assistant_label_;
    lv_obj_set_pos(chat_assistant_label_, 10, 29);
    lv_obj_set_size(chat_assistant_label_, 187, 48);
    lv_label_set_long_mode(chat_assistant_label_, LV_LABEL_LONG_DOT);
    lv_label_set_text(chat_assistant_label_,
        "Tell me about your favorite animal.");
    StyleLabel(chat_assistant_label_, text_font, kTextColor);

    auto* voice_panel = CreatePanel(chat_page_, 10, 145, 169, 47,
        kHighlightColor);
    lv_obj_set_style_radius(voice_panel, 14, 0);
    lv_obj_set_style_border_width(voice_panel, 2, 0);
    lv_obj_set_style_border_color(voice_panel, lv_color_hex(kLineColor), 0);
    chat_voice_title_label_ = lv_label_create(voice_panel);
    lv_obj_set_pos(chat_voice_title_label_, 8, 5);
    lv_label_set_text(chat_voice_title_label_, "LET'S TALK");
    StyleLabel(chat_voice_title_label_, small_font, kTextColor);
    chat_voice_hint_label_ = lv_label_create(voice_panel);
    lv_obj_set_pos(chat_voice_hint_label_, 8, 25);
    lv_label_set_text(chat_voice_hint_label_, "Tap and speak");
    StyleLabel(chat_voice_hint_label_, small_font, kTextColor);
    chat_state_label_ = chat_voice_title_label_;

    chat_volume_down_button_ = lv_button_create(chat_page_);
    lv_obj_set_pos(chat_volume_down_button_, 183, 145);
    lv_obj_set_size(chat_volume_down_button_, 41, 47);
    StyleStorybookButton(chat_volume_down_button_, kPanelColor);
    lv_obj_add_event_cb(chat_volume_down_button_, OnVolumeDownClicked,
        LV_EVENT_CLICKED, this);
    auto* down_label = lv_label_create(chat_volume_down_button_);
    lv_label_set_text(down_label, "-");
    StyleLabel(down_label, text_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(down_label);

    chat_volume_up_button_ = lv_button_create(chat_page_);
    lv_obj_set_pos(chat_volume_up_button_, 228, 145);
    lv_obj_set_size(chat_volume_up_button_, 41, 47);
    StyleStorybookButton(chat_volume_up_button_, kPanelColor);
    lv_obj_add_event_cb(chat_volume_up_button_, OnVolumeUpClicked,
        LV_EVENT_CLICKED, this);
    auto* up_label = lv_label_create(chat_volume_up_button_);
    lv_label_set_text(up_label, "+");
    StyleLabel(up_label, text_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(up_label);

    chat_mic_button_ = lv_button_create(chat_page_);
    lv_obj_set_pos(chat_mic_button_, 273, 145);
    lv_obj_set_size(chat_mic_button_, 37, 47);
    StyleStorybookButton(chat_mic_button_, kHighlightColor);
    lv_obj_add_event_cb(chat_mic_button_, OnChatMicClicked,
        LV_EVENT_CLICKED, this);
    chat_mic_icon_ = lv_label_create(chat_mic_button_);
    lv_label_set_text(chat_mic_icon_, FONT_AWESOME_MICROPHONE);
    StyleLabel(chat_mic_icon_, theme->large_icon_font()->font(), kTextColor,
        LV_TEXT_ALIGN_CENTER);
    lv_obj_center(chat_mic_icon_);

    music_page_ = CreatePanel(screen, 0, 0, width_,
        height_ - kNavigationHeight, kScreenColor);
    create_storybook_status(music_page_, "STORY SONGS", &music_volume_label_,
        &music_wifi_icon_, &music_battery_icon_, &music_battery_label_);
    create_scenery(music_page_);

    auto* album = CreatePanel(music_page_, 10, 35, 68, 68, kAmberColor);
    lv_obj_set_style_radius(album, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(album, 2, 0);
    lv_obj_set_style_border_color(album, lv_color_hex(kLineColor), 0);
    auto* album_icon = lv_label_create(album);
    lv_label_set_text(album_icon, FONT_AWESOME_MUSIC);
    StyleLabel(album_icon, theme->large_icon_font()->font(), kTextColor,
        LV_TEXT_ALIGN_CENTER);
    lv_obj_center(album_icon);
    music_track_index_label_ = lv_label_create(album);
    lv_obj_set_pos(music_track_index_label_, 43, 43);
    lv_label_set_text(music_track_index_label_, "--");
    StyleLabel(music_track_index_label_, small_font, kPanelColor,
        LV_TEXT_ALIGN_CENTER);

    music_track_meta_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_track_meta_label_, 88, 35);
    lv_obj_set_width(music_track_meta_label_, 222);
    lv_label_set_text(music_track_meta_label_, "STORY SONG");
    StyleLabel(music_track_meta_label_, small_font, kRoleColor);
    music_track_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_track_label_, 88, 56);
    lv_obj_set_width(music_track_label_, 222);
    lv_label_set_long_mode(music_track_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(music_track_label_, "Pick a song");
    StyleLabel(music_track_label_, text_font, kTextColor);

    auto* browse_button = lv_button_create(music_page_);
    lv_obj_set_pos(browse_button, 88, 82);
    lv_obj_set_size(browse_button, 140, 30);
    StyleStorybookButton(browse_button, kPanelColor, 11);
    lv_obj_add_event_cb(browse_button, OnBrowseClicked, LV_EVENT_CLICKED, this);
    auto* browse_label = lv_label_create(browse_button);
    lv_label_set_text(browse_label, "PICK A SONG");
    StyleLabel(browse_label, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(browse_label);
    music_state_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_state_label_, 235, 89);
    lv_obj_set_width(music_state_label_, 75);
    lv_label_set_text(music_state_label_, "READY");
    StyleLabel(music_state_label_, small_font, kRoleColor,
        LV_TEXT_ALIGN_RIGHT);

    music_elapsed_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_elapsed_label_, 10, 115);
    lv_obj_set_width(music_elapsed_label_, 45);
    lv_label_set_text(music_elapsed_label_, "0:00");
    StyleLabel(music_elapsed_label_, small_font, kTextColor);
    music_total_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_total_label_, 265, 115);
    lv_obj_set_width(music_total_label_, 45);
    lv_label_set_text(music_total_label_, "--:--");
    StyleLabel(music_total_label_, small_font, kTextColor,
        LV_TEXT_ALIGN_RIGHT);
    music_progress_bar_ = lv_bar_create(music_page_);
    lv_obj_set_pos(music_progress_bar_, 56, 120);
    lv_obj_set_size(music_progress_bar_, 203, 8);
    lv_bar_set_range(music_progress_bar_, 0, 1000);
    lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(music_progress_bar_, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(music_progress_bar_, 6, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(music_progress_bar_, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(music_progress_bar_,
        lv_color_hex(kLineColor), LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(kPanelColor),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(kHighlightColor),
        LV_PART_INDICATOR);

    const char* controls[] = {FONT_AWESOME_BACKWARD_STEP,
        FONT_AWESOME_PLAY, FONT_AWESOME_FORWARD_STEP};
    lv_event_cb_t callbacks[] = {OnPreviousClicked, OnPlayClicked,
        OnNextClicked};
    for (int i = 0; i < 3; ++i) {
        auto* button = lv_button_create(music_page_);
        const int x = i == 0 ? 10 : (i == 1 ? 132 : 200);
        const int w = i == 1 ? 58 : 110;
        lv_obj_set_pos(button, x, 143);
        lv_obj_set_size(button, w, 47);
        StyleStorybookButton(button, i == 1 ? kHighlightColor : kPanelColor);
        lv_obj_add_event_cb(button, callbacks[i], LV_EVENT_CLICKED, this);
        auto* label = lv_label_create(button);
        lv_label_set_text(label, controls[i]);
        StyleLabel(label, i == 1 ? theme->large_icon_font()->font() : icon_font,
            kTextColor, LV_TEXT_ALIGN_CENTER);
        lv_obj_center(label);
        if (i == 1) {
            music_play_icon_ = label;
        }
    }

    browser_page_ = CreatePanel(screen, 0, 0, width_,
        height_ - kNavigationHeight, kScreenColor);
    create_storybook_status(browser_page_, "PICK A SONG",
        &browser_volume_label_, &browser_wifi_icon_, &browser_battery_icon_,
        &browser_battery_label_);
    auto* browser_header = CreatePanel(browser_page_, 0, 25, width_, 36,
        kPanelColor);
    auto* up_button = lv_button_create(browser_header);
    lv_obj_set_pos(up_button, 6, 4);
    lv_obj_set_size(up_button, 48, 28);
    StyleStorybookButton(up_button, kPanelColor, 10);
    lv_obj_add_event_cb(up_button, OnBrowserUpClicked, LV_EVENT_CLICKED, this);
    auto* up_text = lv_label_create(up_button);
    lv_label_set_text(up_text, "UP");
    StyleLabel(up_text, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(up_text);
    browser_path_label_ = lv_label_create(browser_header);
    lv_obj_set_pos(browser_path_label_, 62, 8);
    lv_obj_set_width(browser_path_label_, 176);
    lv_label_set_long_mode(browser_path_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(browser_path_label_, "/");
    StyleLabel(browser_path_label_, small_font, kMutedColor);
    auto* player_button = lv_button_create(browser_header);
    lv_obj_set_pos(player_button, 244, 4);
    lv_obj_set_size(player_button, 70, 28);
    StyleStorybookButton(player_button, kHighlightColor, 10);
    lv_obj_add_event_cb(player_button, OnPlayerClicked, LV_EVENT_CLICKED, this);
    auto* player_text = lv_label_create(player_button);
    lv_label_set_text(player_text, "SONGS");
    StyleLabel(player_text, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(player_text);
    browser_list_ = lv_obj_create(browser_page_);
    lv_obj_set_pos(browser_list_, 0, 61);
    lv_obj_set_size(browser_list_, width_, 139);
    lv_obj_set_style_bg_color(browser_list_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_bg_opa(browser_list_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(browser_list_, 0, 0);
    lv_obj_set_style_radius(browser_list_, 0, 0);
    lv_obj_set_style_pad_all(browser_list_, 6, 0);
    lv_obj_set_style_pad_row(browser_list_, 4, 0);
    lv_obj_set_flex_flow(browser_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(browser_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(browser_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(browser_page_, LV_OBJ_FLAG_HIDDEN);

    wifi_page_ = CreatePanel(screen, 0, 0, width_, height_, kScreenColor);
    auto* wifi_header = CreatePanel(wifi_page_, 0, 0, width_, 32,
        kCyanColor);
    auto* wifi_title = lv_label_create(wifi_header);
    lv_obj_set_pos(wifi_title, 9, 7);
    lv_label_set_text(wifi_title, "WI-FI NETWORKS");
    StyleLabel(wifi_title, small_font, kTextColor);
    wifi_status_label_ = lv_label_create(wifi_header);
    lv_obj_set_pos(wifi_status_label_, 150, 7);
    lv_obj_set_width(wifi_status_label_, 112);
    lv_label_set_text(wifi_status_label_, "SCANNING...");
    StyleLabel(wifi_status_label_, small_font, kTextColor,
        LV_TEXT_ALIGN_RIGHT);
    auto* wifi_close = lv_button_create(wifi_header);
    lv_obj_set_pos(wifi_close, 270, 3);
    lv_obj_set_size(wifi_close, 44, 26);
    StyleStorybookButton(wifi_close, kPanelColor, 10);
    lv_obj_add_event_cb(wifi_close, OnWifiCloseClicked, LV_EVENT_CLICKED, this);
    auto* close_text = lv_label_create(wifi_close);
    lv_label_set_text(close_text, "X");
    StyleLabel(close_text, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(close_text);
    wifi_list_ = lv_obj_create(wifi_page_);
    lv_obj_set_pos(wifi_list_, 0, 32);
    lv_obj_set_size(wifi_list_, width_, height_ - 32);
    lv_obj_set_style_bg_color(wifi_list_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_bg_opa(wifi_list_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_list_, 0, 0);
    lv_obj_set_style_radius(wifi_list_, 0, 0);
    lv_obj_set_style_pad_all(wifi_list_, 6, 0);
    lv_obj_set_style_pad_row(wifi_list_, 4, 0);
    lv_obj_set_flex_flow(wifi_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(wifi_list_, LV_DIR_VER);
    wifi_password_page_ = CreatePanel(wifi_page_, 0, 0, width_, height_,
        kScreenColor);
    auto* password_title = lv_label_create(wifi_password_page_);
    lv_obj_set_pos(password_title, 8, 5);
    lv_label_set_text(password_title, "ENTER WI-FI PASSWORD");
    StyleLabel(password_title, small_font, kTextColor);
    wifi_password_textarea_ = lv_textarea_create(wifi_password_page_);
    lv_obj_set_pos(wifi_password_textarea_, 6, 27);
    lv_obj_set_size(wifi_password_textarea_, 308, 35);
    lv_textarea_set_one_line(wifi_password_textarea_, true);
    lv_textarea_set_password_mode(wifi_password_textarea_, true);
    lv_obj_set_style_text_font(wifi_password_textarea_, small_font, 0);
    wifi_keyboard_ = lv_keyboard_create(wifi_password_page_);
    lv_obj_set_pos(wifi_keyboard_, 0, 65);
    lv_obj_set_size(wifi_keyboard_, width_, height_ - 65);
    lv_obj_set_style_text_font(wifi_keyboard_, small_font, LV_PART_ITEMS);
    lv_keyboard_set_textarea(wifi_keyboard_, wifi_password_textarea_);
    lv_obj_add_event_cb(wifi_keyboard_, OnWifiKeyboardReady,
        LV_EVENT_READY, this);
    lv_obj_add_event_cb(wifi_keyboard_, OnWifiCloseClicked,
        LV_EVENT_CANCEL, this);
    lv_obj_add_flag(wifi_password_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_page_, LV_OBJ_FLAG_HIDDEN);
    SetupWifiInfoUi();

    navigation_bar_ = lv_obj_create(screen);
    lv_obj_set_size(navigation_bar_, width_, kNavigationHeight);
    lv_obj_align(navigation_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(navigation_bar_, 0, 0);
    lv_obj_set_style_border_width(navigation_bar_, 2, 0);
    lv_obj_set_style_border_side(navigation_bar_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(navigation_bar_, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_pad_all(navigation_bar_, 0, 0);
    lv_obj_set_style_pad_column(navigation_bar_, 0, 0);
    lv_obj_set_flex_flow(navigation_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(navigation_bar_, LV_OBJ_FLAG_SCROLLABLE);
    chat_tab_ = lv_button_create(navigation_bar_);
    lv_obj_set_size(chat_tab_, 0, kNavigationHeight);
    lv_obj_set_flex_grow(chat_tab_, 1);
    lv_obj_set_style_radius(chat_tab_, 0, 0);
    lv_obj_set_style_shadow_width(chat_tab_, 0, 0);
    lv_obj_add_event_cb(chat_tab_, OnChatTabClicked, LV_EVENT_CLICKED, this);
    auto* chat_label = lv_label_create(chat_tab_);
    lv_label_set_text(chat_label, "TALK");
    StyleLabel(chat_label, text_font, kPanelColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(chat_label);
    music_tab_ = lv_button_create(navigation_bar_);
    lv_obj_set_size(music_tab_, 0, kNavigationHeight);
    lv_obj_set_flex_grow(music_tab_, 1);
    lv_obj_set_style_radius(music_tab_, 0, 0);
    lv_obj_set_style_shadow_width(music_tab_, 0, 0);
    lv_obj_add_event_cb(music_tab_, OnMusicTabClicked, LV_EVENT_CLICKED, this);
    auto* music_label = lv_label_create(music_tab_);
    lv_label_set_text(music_label, "SONGS");
    StyleLabel(music_label, text_font, kMutedColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(music_label);

    lv_obj_add_flag(music_page_, LV_OBJ_FLAG_HIDDEN);
    ApplyMusicTheme();
    UpdateTabStyles();
}

void Es3c28pDisplay::SetupWifiInfoUi() {
    auto* screen = lv_screen_active();
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    auto* text_font = theme->text_font()->font();
    auto* small_font = &lv_font_montserrat_14;
    const uint32_t header_color = kKidsTheme ? kCyanColor : kPanelColor;

    wifi_info_page_ = CreatePanel(screen, 0, 0, width_, height_, kScreenColor);
    auto* header = CreatePanel(wifi_info_page_, 0, 0, width_, 36,
        header_color);
    auto* title = lv_label_create(header);
    lv_obj_set_pos(title, 10, 8);
    lv_label_set_text(title, "WI-FI INFO");
    StyleLabel(title, small_font, kKidsTheme ? kTextColor : kCyanColor);
    auto* close_button = lv_button_create(header);
    lv_obj_set_pos(close_button, 270, 4);
    lv_obj_set_size(close_button, 44, 28);
    if (kKidsTheme) {
        StyleStorybookButton(close_button, kPanelColor, 10);
    } else {
        StyleButton(close_button, kScreenColor, kLineColor, 2);
    }
    lv_obj_add_event_cb(close_button, OnWifiCloseClicked,
        LV_EVENT_CLICKED, this);
    auto* close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "X");
    StyleLabel(close_label, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(close_label);

    auto* wifi_icon = lv_label_create(wifi_info_page_);
    lv_obj_set_pos(wifi_icon, 15, 51);
    lv_obj_set_size(wifi_icon, 48, 42);
    lv_label_set_text(wifi_icon, FONT_AWESOME_WIFI);
    StyleLabel(wifi_icon, theme->large_icon_font()->font(), kCyanColor,
        LV_TEXT_ALIGN_CENTER);

    auto create_info = [&](int y, const char* label, lv_obj_t** value) {
        auto* key = lv_label_create(wifi_info_page_);
        lv_obj_set_pos(key, 72, y);
        lv_obj_set_width(key, 55);
        lv_label_set_text(key, label);
        StyleLabel(key, small_font, kMutedColor);
        *value = lv_label_create(wifi_info_page_);
        lv_obj_set_pos(*value, 128, y);
        lv_obj_set_width(*value, 178);
        lv_label_set_long_mode(*value, LV_LABEL_LONG_DOT);
        lv_label_set_text(*value, "--");
        StyleLabel(*value, small_font, kTextColor);
    };
    create_info(47, "SSID", &wifi_info_ssid_label_);
    create_info(75, "IP", &wifi_info_ip_label_);
    create_info(103, "SIGNAL", &wifi_info_signal_label_);
    create_info(131, "WEB", &wifi_info_web_label_);

    wifi_reset_button_ = lv_button_create(wifi_info_page_);
    lv_obj_set_pos(wifi_reset_button_, 10, 178);
    lv_obj_set_size(wifi_reset_button_, 300, 48);
    if (kKidsTheme) {
        StyleStorybookButton(wifi_reset_button_, kHighlightColor, 14);
    } else {
        StyleButton(wifi_reset_button_, kPanelColor, kAmberColor, 3);
    }
    lv_obj_add_event_cb(wifi_reset_button_, OnWifiResetClicked,
        LV_EVENT_CLICKED, this);
    wifi_reset_label_ = lv_label_create(wifi_reset_button_);
    lv_label_set_text(wifi_reset_label_, "RESET WI-FI");
    StyleLabel(wifi_reset_label_, text_font, kTextColor,
        LV_TEXT_ALIGN_CENTER);
    lv_obj_center(wifi_reset_label_);
    lv_obj_add_flag(wifi_info_page_, LV_OBJ_FLAG_HIDDEN);
}

void Es3c28pDisplay::SetupMusicUi() {
    DisplayLockGuard lock(this);
    auto* screen = lv_screen_active();
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    auto* text_font = theme->text_font()->font();
    auto* small_font = &lv_font_montserrat_14;
    auto* icon_font = theme->icon_font()->font();

    if (kKidsTheme) {
        SetupStorybookUi();
        return;
    }

    if (bottom_bar_ != nullptr) {
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, -kNavigationHeight);
        lv_obj_set_style_pad_right(bottom_bar_, 56, 0);
        if (chat_message_label_ != nullptr) {
            lv_obj_set_width(chat_message_label_, width_ - 72);
            lv_obj_align(chat_message_label_, LV_ALIGN_LEFT_MID, 8, 0);
        }
    }

    if (mute_label_ != nullptr) {
        auto* right_icons = lv_obj_get_parent(mute_label_);
        volume_label_ = lv_label_create(right_icons);
        lv_obj_set_style_text_font(volume_label_, text_font, 0);
        lv_obj_set_style_text_color(volume_label_, theme->text_color(), 0);
        lv_label_set_text(volume_label_, "--%");
        lv_obj_move_to_index(volume_label_, 0);
    }

    // The external theme package changes palette, geometry and copy without
    // replacing the firmware or speech/music assets.
    chat_page_ = CreatePanel(screen, 0, 0, width_,
        height_ - kNavigationHeight);

    auto* chat_status = CreatePanel(chat_page_, 0, 0, width_, 24);
    lv_obj_set_style_border_width(chat_status, 1, 0);
    lv_obj_set_style_border_side(chat_status, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(chat_status, lv_color_hex(kLineColor), 0);
    auto* ai_badge = lv_label_create(chat_status);
    lv_obj_set_pos(ai_badge, 8, 3);
    lv_obj_set_size(ai_badge, 30, 18);
    lv_label_set_text(ai_badge, kKidsTheme ? "HI" : "AI");
    StyleLabel(ai_badge, small_font, kCyanColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ai_badge, lv_color_hex(kPanelColor), 0);
    lv_obj_set_style_bg_opa(ai_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ai_badge, 1, 0);
    lv_obj_set_style_border_color(ai_badge, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_radius(ai_badge, kKidsTheme ? 8 : 2, 0);
    auto* chat_header_label = lv_label_create(chat_status);
    lv_obj_set_pos(chat_header_label, 45, 4);
    lv_label_set_text(chat_header_label, kKidsTheme ? "BUDDY" : "CHAT");
    StyleLabel(chat_header_label, small_font, kMutedColor);
    chat_volume_label_ = lv_label_create(chat_status);
    lv_obj_set_pos(chat_volume_label_, 164, 4);
    lv_obj_set_width(chat_volume_label_, 74);
    lv_label_set_text(chat_volume_label_, "VOL --%");
    StyleLabel(chat_volume_label_, small_font, kMutedColor, LV_TEXT_ALIGN_RIGHT);
    chat_wifi_icon_ = CreateWifiIndicator(chat_status);
    lv_obj_add_event_cb(chat_wifi_icon_, OnWifiClicked, LV_EVENT_DOUBLE_CLICKED, this);
    CreateBatteryIndicator(chat_status, icon_font, small_font,
        &chat_battery_icon_, &chat_battery_label_);

    auto* conversation_label = lv_label_create(chat_page_);
    lv_obj_set_pos(conversation_label, 9, 31);
    lv_label_set_text(conversation_label,
        kKidsTheme ? "LET'S TALK" : "CONVERSATION");
    StyleLabel(conversation_label, small_font, kTextColor);
    chat_state_label_ = lv_label_create(chat_page_);
    lv_obj_set_pos(chat_state_label_, 174, 31);
    lv_obj_set_width(chat_state_label_, 136);
    lv_label_set_text(chat_state_label_, kKidsTheme ? "READY!" : "READY");
    StyleLabel(chat_state_label_, small_font, kCyanColor, LV_TEXT_ALIGN_RIGHT);

    auto create_message = [&](int y, const char* role, const char* initial,
                              uint32_t accent, lv_obj_t** output) {
        auto* stripe = CreatePanel(chat_page_, 9, y + 3, 2, 34, accent);
        lv_obj_set_style_bg_color(stripe, lv_color_hex(accent), 0);
        auto* meta = lv_label_create(chat_page_);
        lv_obj_set_pos(meta, 17, y);
        lv_label_set_text(meta, role);
        StyleLabel(meta, small_font, accent);
        *output = lv_label_create(chat_page_);
        lv_obj_set_pos(*output, 17, y + 16);
        lv_obj_set_size(*output, 292, 23);
        lv_label_set_long_mode(*output, LV_LABEL_LONG_DOT);
        lv_label_set_text(*output, initial);
        StyleLabel(*output, text_font, kTextColor);
    };
    create_message(54, kKidsTheme ? "FRIEND" : "ASSISTANT",
        kKidsTheme ? "Hello! What shall we discover?" :
            "Tap the microphone to start.",
        kCyanColor, &chat_assistant_label_);
    create_message(96, "YOU", "Your message appears here.",
        kAmberColor, &chat_user_label_);

    auto* voice_panel = CreatePanel(chat_page_, 9, 145, 169, 47,
        kPanelColor);
    lv_obj_set_style_radius(voice_panel, kKidsTheme ? kButtonRadius : 0, 0);
    lv_obj_set_style_border_width(voice_panel, 1, 0);
    lv_obj_set_style_border_color(voice_panel, lv_color_hex(kLineColor), 0);
    chat_voice_title_label_ = lv_label_create(voice_panel);
    lv_obj_set_pos(chat_voice_title_label_, 8, 5);
    lv_label_set_text(chat_voice_title_label_,
        kKidsTheme ? "LET'S TALK" : "VOICE READY");
    StyleLabel(chat_voice_title_label_, small_font, kTextColor);
    chat_voice_hint_label_ = lv_label_create(voice_panel);
    lv_obj_set_pos(chat_voice_hint_label_, 8, 25);
    lv_label_set_text(chat_voice_hint_label_,
        kKidsTheme ? "Tap and speak" : "Tap mic to speak");
    StyleLabel(chat_voice_hint_label_, small_font, kMutedColor);

    chat_volume_down_button_ = lv_button_create(chat_page_);
    lv_obj_set_pos(chat_volume_down_button_, 181, 145);
    lv_obj_set_size(chat_volume_down_button_, 42, 47);
    StyleButton(chat_volume_down_button_, kPanelColor, kLineColor);
    lv_obj_add_event_cb(chat_volume_down_button_, OnVolumeDownClicked,
        LV_EVENT_CLICKED, this);
    auto* down_label = lv_label_create(chat_volume_down_button_);
    // Use ASCII with the UI text font: the board's icon font does not contain
    // LVGL's plus/minus symbol code points.
    lv_label_set_text(down_label, "-");
    StyleLabel(down_label, text_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(down_label);

    chat_volume_up_button_ = lv_button_create(chat_page_);
    lv_obj_set_pos(chat_volume_up_button_, 226, 145);
    lv_obj_set_size(chat_volume_up_button_, 42, 47);
    StyleButton(chat_volume_up_button_, kPanelColor, kLineColor);
    lv_obj_add_event_cb(chat_volume_up_button_, OnVolumeUpClicked,
        LV_EVENT_CLICKED, this);
    auto* up_volume_label = lv_label_create(chat_volume_up_button_);
    lv_label_set_text(up_volume_label, "+");
    StyleLabel(up_volume_label, text_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(up_volume_label);

    chat_mic_button_ = lv_button_create(chat_page_);
    lv_obj_set_pos(chat_mic_button_, 271, 145);
    lv_obj_set_size(chat_mic_button_, 40, 47);
    StyleButton(chat_mic_button_, kAmberColor, kAmberColor);
    lv_obj_remove_flag(chat_mic_button_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(chat_mic_button_, OnChatMicClicked, LV_EVENT_CLICKED,
        this);
    chat_mic_icon_ = lv_label_create(chat_mic_button_);
    lv_obj_set_style_text_font(chat_mic_icon_, theme->large_icon_font()->font(),
        0);
    lv_obj_set_style_text_color(chat_mic_icon_,
        lv_color_hex(kKidsTheme ? kTextColor : kScreenColor), 0);
    lv_label_set_text(chat_mic_icon_, FONT_AWESOME_MICROPHONE);
    lv_obj_center(chat_mic_icon_);

    music_page_ = lv_obj_create(screen);
    lv_obj_set_size(music_page_, width_, height_ - kNavigationHeight);
    lv_obj_align(music_page_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(music_page_, 0, 0);
    lv_obj_set_style_border_width(music_page_, 0, 0);
    lv_obj_set_style_pad_all(music_page_, 0, 0);
    lv_obj_set_style_layout(music_page_, LV_LAYOUT_NONE, 0);
    lv_obj_set_scrollbar_mode(music_page_, LV_SCROLLBAR_MODE_OFF);

    // Compact status bar: visually replaces the generic bar while Music is open.
    auto* player_status = CreatePanel(music_page_, 0, 0, width_, 24);
    lv_obj_set_style_border_width(player_status, 1, 0);
    lv_obj_set_style_border_side(player_status, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(player_status, lv_color_hex(kLineColor), 0);
    auto* sd_badge = lv_label_create(player_status);
    lv_obj_set_pos(sd_badge, 8, 3);
    lv_obj_set_size(sd_badge, 30, 18);
    lv_label_set_text(sd_badge, "SD");
    StyleLabel(sd_badge, small_font, kCyanColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_bg_color(sd_badge, lv_color_hex(kPanelColor), 0);
    lv_obj_set_style_bg_opa(sd_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sd_badge, 1, 0);
    lv_obj_set_style_border_color(sd_badge, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_radius(sd_badge, 2, 0);
    auto* player_label = lv_label_create(player_status);
    lv_obj_set_pos(player_label, 45, 4);
    lv_label_set_text(player_label, kKidsTheme ? "SONGS" : "PLAYER");
    StyleLabel(player_label, small_font, kMutedColor);
    music_volume_label_ = lv_label_create(player_status);
    lv_obj_set_pos(music_volume_label_, 164, 4);
    lv_obj_set_width(music_volume_label_, 74);
    lv_label_set_text(music_volume_label_, "VOL --%");
    StyleLabel(music_volume_label_, small_font, kMutedColor,
        LV_TEXT_ALIGN_RIGHT);
    music_wifi_icon_ = CreateWifiIndicator(player_status);
    lv_obj_add_event_cb(music_wifi_icon_, OnWifiClicked, LV_EVENT_DOUBLE_CLICKED, this);
    CreateBatteryIndicator(player_status, icon_font, small_font,
        &music_battery_icon_, &music_battery_label_);

    music_state_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_state_label_, 10, 32);
    lv_obj_set_width(music_state_label_, 205);
    lv_label_set_text(music_state_label_,
        kKidsTheme ? "PICK A SONG" : "CHECKING SD CARD");
    StyleLabel(music_state_label_, small_font, kAmberColor);

    auto* browse_button = lv_button_create(music_page_);
    lv_obj_set_pos(browse_button, 238, 30);
    lv_obj_set_size(browse_button, 72, 29);
    StyleButton(browse_button, kPanelColor, kLineColor);
    lv_obj_add_event_cb(browse_button, OnBrowseClicked, LV_EVENT_CLICKED, this);
    auto* browse_label = lv_label_create(browse_button);
    lv_label_set_text(browse_label, kKidsTheme ? "PICK" : "BROWSE");
    StyleLabel(browse_label, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(browse_label);

    auto* track_index_panel = CreatePanel(music_page_, 10, 64, 38, 38,
        kScreenColor);
    lv_obj_set_style_border_width(track_index_panel, 1, 0);
    lv_obj_set_style_border_color(track_index_panel, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_radius(track_index_panel, kButtonRadius, 0);
    music_track_index_label_ = lv_label_create(track_index_panel);
    lv_label_set_text(music_track_index_label_, "--");
    StyleLabel(music_track_index_label_, text_font, kCyanColor,
        LV_TEXT_ALIGN_CENTER);
    lv_obj_center(music_track_index_label_);

    music_track_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_track_label_, 58, 60);
    lv_obj_set_width(music_track_label_, 252);
    lv_label_set_long_mode(music_track_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(music_track_label_, "Insert a FAT32 card");
    StyleLabel(music_track_label_, text_font, kTextColor);

    music_track_meta_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_track_meta_label_, 58, 86);
    lv_obj_set_width(music_track_meta_label_, 252);
    lv_label_set_text(music_track_meta_label_, "SD MUSIC");
    StyleLabel(music_track_meta_label_, small_font, kMutedColor);

    music_progress_bar_ = lv_bar_create(music_page_);
    lv_obj_set_pos(music_progress_bar_, 56, 119);
    lv_obj_set_size(music_progress_bar_, 208, 5);
    lv_bar_set_range(music_progress_bar_, 0, 1000);
    lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(music_progress_bar_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(music_progress_bar_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(kLineColor),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(kAmberColor),
        LV_PART_INDICATOR);

    music_elapsed_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_elapsed_label_, 10, 111);
    lv_obj_set_width(music_elapsed_label_, 42);
    lv_label_set_text(music_elapsed_label_, "0:00");
    StyleLabel(music_elapsed_label_, small_font, kTextColor);

    music_total_label_ = lv_label_create(music_page_);
    lv_obj_set_pos(music_total_label_, 270, 111);
    lv_obj_set_width(music_total_label_, 40);
    lv_label_set_text(music_total_label_, "--:--");
    StyleLabel(music_total_label_, small_font, kMutedColor, LV_TEXT_ALIGN_RIGHT);

    const char* icons[] = {
        FONT_AWESOME_BACKWARD_STEP,
        FONT_AWESOME_PLAY,
        FONT_AWESOME_FORWARD_STEP,
    };
    lv_event_cb_t callbacks[] = {
        OnPreviousClicked,
        OnPlayClicked,
        OnNextClicked,
    };
    for (int i = 0; i < 3; ++i) {
        auto* button = lv_button_create(music_page_);
        const int x = i == 0 ? 10 : (i == 1 ? 112 : 220);
        const int width = i == 1 ? 96 : 90;
        const int height = i == 1 ? 50 : 44;
        lv_obj_set_pos(button, x, i == 1 ? 137 : 140);
        lv_obj_set_size(button, width, height);
        StyleButton(button, i == 1 ? kAmberColor : kPanelColor,
            i == 1 ? kAmberColor : kLineColor);
        lv_obj_add_event_cb(button, callbacks[i], LV_EVENT_CLICKED, this);
        auto* icon = lv_label_create(button);
        lv_obj_set_style_text_font(icon,
            i == 1 ? theme->large_icon_font()->font() : icon_font, 0);
        lv_obj_set_style_text_color(icon,
            lv_color_hex(i == 1 && kKidsTheme ? kTextColor :
                (i == 1 ? kScreenColor : kTextColor)), 0);
        lv_label_set_text(icon, icons[i]);
        lv_obj_center(icon);
        if (i == 1) {
            music_play_icon_ = icon;
        }
    }

    browser_page_ = lv_obj_create(screen);
    lv_obj_set_size(browser_page_, width_, height_ - kNavigationHeight);
    lv_obj_align(browser_page_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(browser_page_, 0, 0);
    lv_obj_set_style_border_width(browser_page_, 0, 0);
    lv_obj_set_style_pad_all(browser_page_, 0, 0);
    lv_obj_set_style_layout(browser_page_, LV_LAYOUT_NONE, 0);

    auto* browser_status = CreatePanel(browser_page_, 0, 0, width_, 24);
    lv_obj_set_style_border_width(browser_status, 1, 0);
    lv_obj_set_style_border_side(browser_status, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(browser_status, lv_color_hex(kLineColor), 0);
    auto* browser_title = lv_label_create(browser_status);
    lv_obj_set_pos(browser_title, 9, 4);
    lv_label_set_text(browser_title, "SD BROWSER");
    StyleLabel(browser_title, small_font, kCyanColor);
    browser_volume_label_ = lv_label_create(browser_status);
    lv_obj_set_pos(browser_volume_label_, 164, 4);
    lv_obj_set_width(browser_volume_label_, 74);
    lv_label_set_text(browser_volume_label_, "VOL --%");
    StyleLabel(browser_volume_label_, small_font, kMutedColor,
        LV_TEXT_ALIGN_RIGHT);
    browser_wifi_icon_ = CreateWifiIndicator(browser_status);
    lv_obj_add_event_cb(browser_wifi_icon_, OnWifiClicked, LV_EVENT_DOUBLE_CLICKED, this);
    CreateBatteryIndicator(browser_status, icon_font, small_font,
        &browser_battery_icon_, &browser_battery_label_);

    auto* browser_header = CreatePanel(browser_page_, 0, 24, width_, 36,
        kScreenColor);
    lv_obj_set_style_border_width(browser_header, 1, 0);
    lv_obj_set_style_border_side(browser_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(browser_header, lv_color_hex(kLineColor), 0);

    auto* up_button = lv_button_create(browser_header);
    lv_obj_set_pos(up_button, 6, 4);
    lv_obj_set_size(up_button, 48, 28);
    StyleButton(up_button, kPanelColor, kLineColor);
    lv_obj_add_event_cb(up_button, OnBrowserUpClicked, LV_EVENT_CLICKED, this);
    auto* up_label = lv_label_create(up_button);
    lv_label_set_text(up_label, "UP");
    StyleLabel(up_label, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(up_label);

    browser_path_label_ = lv_label_create(browser_header);
    lv_obj_set_pos(browser_path_label_, 62, 8);
    lv_obj_set_width(browser_path_label_, 180);
    lv_label_set_long_mode(browser_path_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(browser_path_label_, "/");
    StyleLabel(browser_path_label_, small_font, kMutedColor);

    auto* player_button = lv_button_create(browser_header);
    lv_obj_set_pos(player_button, 246, 6);
    lv_obj_set_size(player_button, 68, 28);
    StyleButton(player_button, kAmberColor, kAmberColor);
    lv_obj_add_event_cb(player_button, OnPlayerClicked, LV_EVENT_CLICKED, this);
    auto* return_player_label = lv_label_create(player_button);
    lv_label_set_text(return_player_label, kKidsTheme ? "SONGS" : "PLAYER");
    StyleLabel(return_player_label, small_font, kScreenColor,
        LV_TEXT_ALIGN_CENTER);
    lv_obj_center(return_player_label);

    browser_list_ = lv_obj_create(browser_page_);
    lv_obj_set_pos(browser_list_, 0, 60);
    lv_obj_set_size(browser_list_, width_, 140);
    lv_obj_set_style_bg_color(browser_list_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_bg_opa(browser_list_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(browser_list_, 0, 0);
    lv_obj_set_style_radius(browser_list_, 0, 0);
    lv_obj_set_style_pad_all(browser_list_, 6, 0);
    lv_obj_set_style_pad_row(browser_list_, 4, 0);
    lv_obj_set_flex_flow(browser_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(browser_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(browser_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(browser_page_, LV_OBJ_FLAG_HIDDEN);

    wifi_page_ = CreatePanel(screen, 0, 0, width_, height_);
    auto* wifi_header = CreatePanel(wifi_page_, 0, 0, width_, 32,
        kPanelColor);
    lv_obj_set_style_border_width(wifi_header, 1, 0);
    lv_obj_set_style_border_side(wifi_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(wifi_header, lv_color_hex(kLineColor), 0);
    auto* wifi_title = lv_label_create(wifi_header);
    lv_obj_set_pos(wifi_title, 9, 7);
    lv_label_set_text(wifi_title, "WI-FI NETWORKS");
    StyleLabel(wifi_title, small_font, kCyanColor);
    wifi_status_label_ = lv_label_create(wifi_header);
    lv_obj_set_pos(wifi_status_label_, 150, 7);
    lv_obj_set_width(wifi_status_label_, 112);
    lv_label_set_text(wifi_status_label_, "SCANNING...");
    StyleLabel(wifi_status_label_, small_font, kMutedColor,
        LV_TEXT_ALIGN_RIGHT);
    auto* wifi_close = lv_button_create(wifi_header);
    lv_obj_set_pos(wifi_close, 270, 3);
    lv_obj_set_size(wifi_close, 44, 26);
    StyleButton(wifi_close, kScreenColor, kLineColor, 2);
    lv_obj_add_event_cb(wifi_close, OnWifiCloseClicked, LV_EVENT_CLICKED, this);
    auto* wifi_close_label = lv_label_create(wifi_close);
    lv_label_set_text(wifi_close_label, "X");
    StyleLabel(wifi_close_label, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(wifi_close_label);

    wifi_list_ = lv_obj_create(wifi_page_);
    lv_obj_set_pos(wifi_list_, 0, 32);
    lv_obj_set_size(wifi_list_, width_, height_ - 32);
    lv_obj_set_style_bg_color(wifi_list_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_bg_opa(wifi_list_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_list_, 0, 0);
    lv_obj_set_style_radius(wifi_list_, 0, 0);
    lv_obj_set_style_pad_all(wifi_list_, 6, 0);
    lv_obj_set_style_pad_row(wifi_list_, 4, 0);
    lv_obj_set_flex_flow(wifi_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(wifi_list_, LV_DIR_VER);

    wifi_password_page_ = CreatePanel(wifi_page_, 0, 0, width_, height_);
    auto* password_title = lv_label_create(wifi_password_page_);
    lv_obj_set_pos(password_title, 8, 5);
    lv_obj_set_width(password_title, 304);
    lv_label_set_text(password_title, "ENTER WI-FI PASSWORD");
    StyleLabel(password_title, small_font, kCyanColor);
    wifi_password_textarea_ = lv_textarea_create(wifi_password_page_);
    lv_obj_set_pos(wifi_password_textarea_, 6, 27);
    lv_obj_set_size(wifi_password_textarea_, 308, 35);
    lv_textarea_set_one_line(wifi_password_textarea_, true);
    lv_textarea_set_password_mode(wifi_password_textarea_, true);
    lv_obj_set_style_text_font(wifi_password_textarea_, small_font, 0);
    wifi_keyboard_ = lv_keyboard_create(wifi_password_page_);
    lv_obj_set_pos(wifi_keyboard_, 0, 65);
    lv_obj_set_size(wifi_keyboard_, width_, height_ - 65);
    lv_obj_set_style_text_font(wifi_keyboard_, small_font, LV_PART_ITEMS);
    lv_keyboard_set_textarea(wifi_keyboard_, wifi_password_textarea_);
    lv_obj_add_event_cb(wifi_keyboard_, OnWifiKeyboardReady,
        LV_EVENT_READY, this);
    lv_obj_add_event_cb(wifi_keyboard_, OnWifiCloseClicked,
        LV_EVENT_CANCEL, this);
    lv_obj_add_flag(wifi_password_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_page_, LV_OBJ_FLAG_HIDDEN);
    SetupWifiInfoUi();

    navigation_bar_ = lv_obj_create(screen);
    lv_obj_set_size(navigation_bar_, width_, kNavigationHeight);
    lv_obj_align(navigation_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(navigation_bar_, 0, 0);
    lv_obj_set_style_border_width(navigation_bar_, 1, 0);
    lv_obj_set_style_border_side(navigation_bar_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(navigation_bar_, lv_color_hex(kLineColor), 0);
    lv_obj_set_style_pad_all(navigation_bar_, 0, 0);
    lv_obj_set_style_pad_column(navigation_bar_, 0, 0);
    lv_obj_set_flex_flow(navigation_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(navigation_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(navigation_bar_, LV_OBJ_FLAG_SCROLLABLE);

    chat_tab_ = lv_button_create(navigation_bar_);
    lv_obj_set_size(chat_tab_, 0, kNavigationHeight);
    lv_obj_set_flex_grow(chat_tab_, 1);
    lv_obj_set_style_radius(chat_tab_, 0, 0);
    lv_obj_set_style_shadow_width(chat_tab_, 0, 0);
    lv_obj_remove_flag(chat_tab_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(chat_tab_, OnChatTabClicked, LV_EVENT_CLICKED, this);
    auto* chat_label = lv_label_create(chat_tab_);
    lv_label_set_text(chat_label, kKidsTheme ? "TALK" : "AI CHAT");
    StyleLabel(chat_label, small_font, kMutedColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(chat_label);

    music_tab_ = lv_button_create(navigation_bar_);
    lv_obj_set_size(music_tab_, 0, kNavigationHeight);
    lv_obj_set_flex_grow(music_tab_, 1);
    lv_obj_set_style_radius(music_tab_, 0, 0);
    lv_obj_set_style_shadow_width(music_tab_, 0, 0);
    lv_obj_remove_flag(music_tab_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(music_tab_, OnMusicTabClicked, LV_EVENT_CLICKED, this);
    auto* music_label = lv_label_create(music_tab_);
    lv_label_set_text(music_label, kKidsTheme ? "SONGS" : "MUSIC");
    StyleLabel(music_label, small_font, kTextColor, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(music_label);

    lv_obj_add_flag(music_page_, LV_OBJ_FLAG_HIDDEN);
    ApplyMusicTheme();
    UpdateTabStyles();
}

void Es3c28pDisplay::ShowMusicPage(bool show) {
    DisplayLockGuard lock(this);
    if (wifi_page_ != nullptr) {
        lv_obj_add_flag(wifi_page_, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_info_page_ != nullptr) {
        lv_obj_add_flag(wifi_info_page_, LV_OBJ_FLAG_HIDDEN);
    }
    wifi_page_active_ = false;
    music_page_active_ = show;
    if (show) {
        lv_obj_add_flag(chat_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(music_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(music_page_);
        lv_obj_move_foreground(navigation_bar_);
    } else {
        lv_obj_add_flag(music_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(browser_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(chat_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(chat_page_);
        lv_obj_move_foreground(navigation_bar_);
    }
    UpdateTabStyles();
}

void Es3c28pDisplay::ShowWifiPage(bool show) {
    {
        DisplayLockGuard lock(this);
        if (wifi_page_ == nullptr) {
            return;
        }
        if (!show) {
            wifi_page_active_ = false;
            lv_obj_add_flag(wifi_page_, LV_OBJ_FLAG_HIDDEN);
            if (wifi_info_page_ != nullptr) {
                lv_obj_add_flag(wifi_info_page_, LV_OBJ_FLAG_HIDDEN);
            }
            wifi_reset_armed_ = false;
            if (wifi_reset_label_ != nullptr) {
                lv_label_set_text(wifi_reset_label_, "RESET WI-FI");
            }
            return;
        }
        wifi_page_active_ = true;
        lv_obj_add_flag(wifi_password_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(wifi_list_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(wifi_status_label_, "SCANNING...");
        lv_obj_remove_flag(wifi_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(wifi_page_);
    }
    ScanWifiNetworks();
}

void Es3c28pDisplay::ShowWifiInfo() {
    DisplayLockGuard lock(this);
    if (wifi_info_page_ == nullptr) {
        return;
    }
    auto& station = WifiStation::GetInstance();
    const bool connected = station.IsConnected();
    const std::string ssid = connected ? station.GetSsid() : "Not connected";
    const std::string ip = connected ? station.GetIpAddress() : "No IP address";
    char signal[24];
    if (connected) {
        snprintf(signal, sizeof(signal), "%d dBm",
            static_cast<int>(station.GetRssi()));
    } else {
        snprintf(signal, sizeof(signal), "Unavailable");
    }
    const std::string web = connected ? "http://" + ip : "Unavailable";
    lv_label_set_text(wifi_info_ssid_label_, ssid.c_str());
    lv_label_set_text(wifi_info_ip_label_, ip.c_str());
    lv_label_set_text(wifi_info_signal_label_, signal);
    lv_label_set_text(wifi_info_web_label_, web.c_str());
    wifi_reset_armed_ = false;
    lv_label_set_text(wifi_reset_label_, "RESET WI-FI");
    wifi_page_active_ = true;
    lv_obj_add_flag(wifi_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_info_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(wifi_info_page_);
}

void Es3c28pDisplay::ScanWifiNetworks() {
    xTaskCreate(WifiScanTask, "wifi_ui_scan", 4096, this, 3, nullptr);
}

void Es3c28pDisplay::WifiScanTask(void* arg) {
    auto* display = static_cast<Es3c28pDisplay*>(arg);
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    std::vector<std::string> ssids;
    std::vector<int> signals;
    if (esp_wifi_scan_start(&scan_config, true) == ESP_OK) {
        uint16_t count = 0;
        esp_wifi_scan_get_ap_num(&count);
        count = std::min<uint16_t>(count, 24);
        std::vector<wifi_ap_record_t> records(count);
        if (count > 0 && esp_wifi_scan_get_ap_records(&count,
                records.data()) == ESP_OK) {
            for (uint16_t i = 0; i < count; ++i) {
                const char* ssid = reinterpret_cast<const char*>(
                    records[i].ssid);
                if (ssid[0] == '\0' ||
                    std::find(ssids.begin(), ssids.end(), ssid) != ssids.end()) {
                    continue;
                }
                ssids.emplace_back(ssid);
                signals.push_back(records[i].rssi);
            }
        }
    }
    display->SetWifiNetworks(ssids, signals);
    vTaskDelete(nullptr);
}

void Es3c28pDisplay::SetWifiNetworks(const std::vector<std::string>& ssids,
    const std::vector<int>& signal_levels) {
    DisplayLockGuard lock(this);
    wifi_ssids_ = ssids;
    lv_obj_clean(wifi_list_);
    char status[24];
    snprintf(status, sizeof(status), "%u FOUND",
        static_cast<unsigned>(ssids.size()));
    lv_label_set_text(wifi_status_label_, status);
    if (ssids.empty()) {
        auto* empty = lv_label_create(wifi_list_);
        lv_obj_set_width(empty, width_ - 20);
        lv_label_set_text(empty, "NO NETWORKS FOUND");
        StyleLabel(empty, &lv_font_montserrat_14, kMutedColor,
            LV_TEXT_ALIGN_CENTER);
        return;
    }
    for (size_t i = 0; i < ssids.size(); ++i) {
        auto* row = lv_button_create(wifi_list_);
        lv_obj_set_size(row, width_ - 12, 40);
        StyleButton(row, kPanelColor, kLineColor, 2);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(i + 1));
        lv_obj_add_event_cb(row, OnWifiEntryClicked, LV_EVENT_CLICKED, this);
        auto* name = lv_label_create(row);
        lv_obj_set_pos(name, 8, 10);
        lv_obj_set_width(name, 230);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_label_set_text(name, ssids[i].c_str());
        StyleLabel(name, &lv_font_montserrat_14, kTextColor);
        auto* signal = lv_label_create(row);
        lv_obj_set_pos(signal, 246, 10);
        lv_obj_set_width(signal, 48);
        char signal_text[16];
        snprintf(signal_text, sizeof(signal_text), "%d dB",
            i < signal_levels.size() ? signal_levels[i] : 0);
        lv_label_set_text(signal, signal_text);
        StyleLabel(signal, &lv_font_montserrat_14, kMutedColor,
            LV_TEXT_ALIGN_RIGHT);
    }
}

void Es3c28pDisplay::ShowMusicBrowser(bool show) {
    DisplayLockGuard lock(this);
    if (show) {
        lv_obj_add_flag(music_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(browser_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(browser_page_);
        lv_obj_move_foreground(navigation_bar_);
    } else {
        lv_obj_add_flag(browser_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(music_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(navigation_bar_);
    }
}

void Es3c28pDisplay::SetBrowserEntries(const std::string& path,
    const std::vector<std::string>& names, const std::vector<bool>& directories) {
    DisplayLockGuard lock(this);
    lv_label_set_text(browser_path_label_, path.c_str());
    lv_obj_clean(browser_list_);
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    if (names.empty()) {
        auto* empty_label = lv_label_create(browser_list_);
        lv_obj_set_width(empty_label, width_ - 24);
        lv_label_set_text(empty_label, "EMPTY FOLDER");
        StyleLabel(empty_label, &lv_font_montserrat_14, kMutedColor,
            LV_TEXT_ALIGN_CENTER);
        return;
    }
    for (size_t i = 0; i < names.size(); ++i) {
        auto* row = lv_button_create(browser_list_);
        lv_obj_set_size(row, width_ - 12, 38);
        StyleButton(row, kPanelColor,
            directories[i] ? kCyanColor : kLineColor, 3);
        lv_obj_set_style_border_width(row, directories[i] ? 3 : 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(i + 1));
        lv_obj_add_event_cb(row, OnBrowserEntryClicked, LV_EVENT_CLICKED, this);
        auto* label = lv_label_create(row);
        lv_obj_set_width(label, width_ - 42);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        std::string text = directories[i] ? "[DIR] " + names[i] : names[i];
        lv_label_set_text(label, text.c_str());
        StyleLabel(label, theme->text_font()->font(), kTextColor);
        lv_obj_center(label);
    }
}

void Es3c28pDisplay::SetMusicInfo(const char* state, const char* track,
    bool playing, size_t track_index, size_t track_count) {
    DisplayLockGuard lock(this);
    if (music_state_label_ == nullptr || music_track_label_ == nullptr) {
        return;
    }

    std::string state_text = state != nullptr ? state : "";
    std::transform(state_text.begin(), state_text.end(), state_text.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    lv_label_set_text(music_state_label_, state_text.c_str());
    lv_label_set_text(music_track_label_, track != nullptr ? track : "");

    char index_text[16];
    char meta_text[48];
    if (track_index > 0 && track_count > 0) {
        snprintf(index_text, sizeof(index_text), "%02u",
            static_cast<unsigned>(track_index));
        if (kKidsTheme) {
            snprintf(meta_text, sizeof(meta_text), "STORY SONG  %02u/%02u",
                static_cast<unsigned>(track_index),
                static_cast<unsigned>(track_count));
        } else {
            snprintf(meta_text, sizeof(meta_text), "TRACK %02u / %02u  |  %s",
                static_cast<unsigned>(track_index),
                static_cast<unsigned>(track_count), state_text.c_str());
        }
    } else {
        snprintf(index_text, sizeof(index_text), "--");
        if (kKidsTheme) {
            snprintf(meta_text, sizeof(meta_text), "STORY SONG");
        } else {
            snprintf(meta_text, sizeof(meta_text), "SD MUSIC  |  %s",
                state_text.c_str());
        }
    }
    lv_label_set_text(music_track_index_label_, index_text);
    lv_label_set_text(music_track_meta_label_, meta_text);
    lv_label_set_text(music_play_icon_,
        playing ? FONT_AWESOME_PAUSE : FONT_AWESOME_PLAY);
}

void Es3c28pDisplay::SetMusicProgress(uint32_t elapsed_seconds,
    uint32_t total_seconds) {
    DisplayLockGuard lock(this);
    if (music_elapsed_label_ == nullptr || music_total_label_ == nullptr ||
        music_progress_bar_ == nullptr) {
        return;
    }

    char elapsed_text[16];
    char total_text[16];
    snprintf(elapsed_text, sizeof(elapsed_text), "%u:%02u",
        static_cast<unsigned>(elapsed_seconds / 60),
        static_cast<unsigned>(elapsed_seconds % 60));
    if (total_seconds > 0) {
        snprintf(total_text, sizeof(total_text), "%u:%02u",
            static_cast<unsigned>(total_seconds / 60),
            static_cast<unsigned>(total_seconds % 60));
        const int progress = static_cast<int>(std::min<uint64_t>(1000,
            static_cast<uint64_t>(elapsed_seconds) * 1000 / total_seconds));
        lv_bar_set_value(music_progress_bar_, progress, LV_ANIM_ON);
    } else {
        snprintf(total_text, sizeof(total_text), "--:--");
        lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
    }
    lv_label_set_text(music_elapsed_label_, elapsed_text);
    lv_label_set_text(music_total_label_, total_text);
}

void Es3c28pDisplay::SetChatMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr || content[0] == '\0') {
        return;
    }

    DisplayLockGuard lock(this);
    const std::string role_text(role);
    if (kKidsTheme) {
        lv_label_set_text(chat_assistant_role_label_,
            role_text == "user" ? "YOU" : "YOUR AI FRIEND");
        lv_label_set_text(chat_assistant_label_, content);
        return;
    }
    if (role_text == "user") {
        lv_label_set_text(chat_user_label_, content);
    } else if (role_text == "assistant" || role_text == "system") {
        lv_label_set_text(chat_assistant_label_, content);
    }
}

void Es3c28pDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);
    DisplayLockGuard lock(this);
    ApplyMusicTheme();
    UpdateTabStyles();
}

void Es3c28pDisplay::UpdateStatusBar(bool update_all) {
    SpiLcdDisplay::UpdateStatusBar(update_all);

    auto* codec = Board::GetInstance().GetAudioCodec();
    const int volume = codec->output_volume();
    const char* network_icon = Board::GetInstance().GetNetworkStateIcon();
    const std::string network_icon_text =
        network_icon != nullptr && network_icon[0] != '\0'
            ? network_icon : FONT_AWESOME_WIFI_SLASH;
    int battery = -1;
    bool charging = false;
    bool discharging = false;
    Board::GetInstance().GetBatteryLevel(battery, charging, discharging);
    battery = std::clamp(battery, 0, 100);
    const int chat_state = static_cast<int>(
        Application::GetInstance().GetDeviceState());
    if (!update_all && volume == displayed_volume_ &&
        battery == displayed_battery_ && charging == displayed_charging_ &&
        network_icon_text == displayed_network_icon_ &&
        chat_state == displayed_chat_state_) {
        return;
    }

    DisplayLockGuard lock(this);
    if (update_all || volume != displayed_volume_) {
        displayed_volume_ = volume;
        char text[8];
        snprintf(text, sizeof(text), "%d%%", volume);
        if (volume_label_ != nullptr) {
            lv_label_set_text(volume_label_, text);
        }
        char player_text[16];
        snprintf(player_text, sizeof(player_text), "VOL %d%%", volume);
        if (chat_volume_label_ != nullptr) {
            lv_label_set_text(chat_volume_label_, player_text);
        }
        if (music_volume_label_ != nullptr) {
            lv_label_set_text(music_volume_label_, player_text);
        }
        if (browser_volume_label_ != nullptr) {
            lv_label_set_text(browser_volume_label_, player_text);
        }
    }

    if (update_all || network_icon_text != displayed_network_icon_) {
        displayed_network_icon_ = network_icon_text;
        const bool connected = network_icon_text != FONT_AWESOME_WIFI_SLASH;
        lv_obj_t* wifi_icons[] = {
            chat_wifi_icon_, music_wifi_icon_, browser_wifi_icon_};
        for (auto* icon : wifi_icons) {
            if (icon != nullptr) {
                lv_label_set_text(icon, network_icon_text.c_str());
                lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_text_opa(icon, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(icon,
                    lv_color_hex(connected ?
                        (kKidsTheme ? kTextColor : kCyanColor) :
                        (kKidsTheme ? kRoleColor : kMutedColor)), 0);
                lv_obj_move_foreground(icon);
            }
        }
    }

    if (update_all || battery != displayed_battery_ ||
        charging != displayed_charging_) {
        displayed_battery_ = battery;
        displayed_charging_ = charging;

        const char* battery_icon = FONT_AWESOME_BATTERY_FULL;
        if (charging) {
            battery_icon = FONT_AWESOME_BATTERY_BOLT;
        } else if (battery < 20) {
            battery_icon = FONT_AWESOME_BATTERY_EMPTY;
        } else if (battery < 40) {
            battery_icon = FONT_AWESOME_BATTERY_QUARTER;
        } else if (battery < 60) {
            battery_icon = FONT_AWESOME_BATTERY_HALF;
        } else if (battery < 80) {
            battery_icon = FONT_AWESOME_BATTERY_THREE_QUARTERS;
        }

        char battery_text[8];
        snprintf(battery_text, sizeof(battery_text), "%d%%", battery);
        const uint32_t battery_color = charging ? kAmberColor :
            (battery < 20 ? 0xE5685E : kCyanColor);
        lv_obj_t* battery_icons[] = {
            chat_battery_icon_, music_battery_icon_, browser_battery_icon_};
        lv_obj_t* battery_labels[] = {
            chat_battery_label_, music_battery_label_, browser_battery_label_};
        for (auto* icon : battery_icons) {
            if (icon != nullptr) {
                lv_label_set_text(icon, battery_icon);
                lv_obj_set_style_text_color(icon,
                    lv_color_hex(battery_color), 0);
            }
        }
        for (auto* label : battery_labels) {
            if (label != nullptr) {
                lv_label_set_text(label, battery_text);
            }
        }
    }

    if (update_all || chat_state != displayed_chat_state_) {
        displayed_chat_state_ = chat_state;
        const bool active = chat_state == kDeviceStateConnecting ||
            chat_state == kDeviceStateListening ||
            chat_state == kDeviceStateSpeaking;
        const bool available = chat_state == kDeviceStateIdle || active;
        const char* state_text = "BUSY";
        const char* voice_title = "VOICE UNAVAILABLE";
        const char* voice_hint = "Please wait";
        switch (chat_state) {
            case kDeviceStateIdle:
                state_text = "READY";
                voice_title = "VOICE READY";
                voice_hint = "Tap mic to speak";
                break;
            case kDeviceStateConnecting:
                state_text = "CONNECTING";
                voice_title = "CONNECTING";
                voice_hint = "Tap mic to cancel";
                break;
            case kDeviceStateListening:
                state_text = "LISTENING";
                voice_title = "LISTENING";
                voice_hint = "Tap mic when done";
                break;
            case kDeviceStateSpeaking:
                state_text = "SPEAKING";
                voice_title = "ASSISTANT SPEAKING";
                voice_hint = "Tap mic to interrupt";
                break;
            case kDeviceStateStarting:
                state_text = "STARTING";
                voice_title = "STARTING";
                voice_hint = "Preparing device";
                break;
            case kDeviceStateWifiConfiguring:
                state_text = "SETUP";
                voice_title = "WI-FI SETUP";
                voice_hint = "Connect to continue";
                break;
            case kDeviceStateUpgrading:
                state_text = "UPDATING";
                voice_title = "UPDATING";
                voice_hint = "Keep power connected";
                break;
            default:
                break;
        }
        if (chat_state_label_ != nullptr) {
            lv_label_set_text(chat_state_label_, state_text);
        }
        if (chat_voice_title_label_ != nullptr) {
            lv_label_set_text(chat_voice_title_label_, voice_title);
        }
        if (chat_voice_hint_label_ != nullptr) {
            lv_label_set_text(chat_voice_hint_label_, voice_hint);
        }
        lv_label_set_text(chat_mic_icon_, active
            ? FONT_AWESOME_MICROPHONE_SLASH : FONT_AWESOME_MICROPHONE);
        lv_obj_set_style_bg_color(chat_mic_button_,
            lv_color_hex(active ? kPanelColor : kAmberColor), 0);
        lv_obj_set_style_border_color(chat_mic_button_,
            lv_color_hex(available ? kAmberColor : kMutedColor), 0);
        lv_obj_set_style_text_color(chat_mic_icon_,
            lv_color_hex(active ? kAmberColor :
                (available ? (kKidsTheme ? kTextColor : kScreenColor) :
                    kMutedColor)), 0);
    }
}

void Es3c28pDisplay::ApplyMusicTheme() {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    lv_obj_set_style_bg_color(chat_page_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_text_color(chat_page_, lv_color_hex(kTextColor), 0);
    lv_obj_set_style_bg_color(music_page_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_text_color(music_page_, lv_color_hex(kTextColor), 0);
    lv_obj_set_style_bg_color(browser_page_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_text_color(browser_page_, lv_color_hex(kTextColor), 0);
    lv_obj_set_style_bg_color(navigation_bar_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_text_color(navigation_bar_, lv_color_hex(kTextColor), 0);
    if (volume_label_ != nullptr) {
        lv_obj_set_style_text_color(volume_label_, theme->text_color(), 0);
        lv_obj_set_style_text_font(volume_label_, theme->text_font()->font(), 0);
    }
}

void Es3c28pDisplay::UpdateTabStyles() {
    lv_obj_set_style_bg_color(chat_tab_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_bg_color(music_tab_, lv_color_hex(kScreenColor), 0);
    lv_obj_set_style_border_side(chat_tab_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_side(music_tab_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(chat_tab_, music_page_active_ ? 0 : 3, 0);
    lv_obj_set_style_border_width(music_tab_, music_page_active_ ? 3 : 0, 0);
    lv_obj_set_style_border_color(chat_tab_, lv_color_hex(kAmberColor), 0);
    lv_obj_set_style_border_color(music_tab_, lv_color_hex(kAmberColor), 0);

    if (kKidsTheme) {
        lv_obj_set_style_bg_color(chat_tab_,
            lv_color_hex(music_page_active_ ? kPanelColor : kAmberColor), 0);
        lv_obj_set_style_bg_color(music_tab_,
            lv_color_hex(music_page_active_ ? kAmberColor : kPanelColor), 0);
        lv_obj_set_style_border_color(chat_tab_, lv_color_hex(kPositiveColor), 0);
        lv_obj_set_style_border_color(music_tab_, lv_color_hex(kPositiveColor), 0);
    }

    auto* chat_label = lv_obj_get_child(chat_tab_, 0);
    auto* music_label = lv_obj_get_child(music_tab_, 0);
    lv_obj_set_style_text_color(chat_label, lv_color_hex(kKidsTheme
        ? (music_page_active_ ? kMutedColor : kPanelColor)
        : (music_page_active_ ? kMutedColor : kTextColor)), 0);
    lv_obj_set_style_text_color(music_label, lv_color_hex(kKidsTheme
        ? (music_page_active_ ? kPanelColor : kMutedColor)
        : (music_page_active_ ? kTextColor : kMutedColor)), 0);
}

void Es3c28pDisplay::OnChatTabClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    display->ShowMusicPage(false);
}

void Es3c28pDisplay::OnMusicTabClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    display->ShowMusicPage(true);
}

void Es3c28pDisplay::OnChatMicClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (display->music_page_active_) {
        return;
    }
    Application::GetInstance().ToggleChatState();
}

void Es3c28pDisplay::OnVolumeDownClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    auto* codec = Board::GetInstance().GetAudioCodec();
    codec->SetOutputVolume(std::max(0, codec->output_volume() - 10));
    display->UpdateStatusBar(true);
}

void Es3c28pDisplay::OnVolumeUpClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    auto* codec = Board::GetInstance().GetAudioCodec();
    codec->SetOutputVolume(std::min(100, codec->output_volume() + 10));
    display->UpdateStatusBar(true);
}

void Es3c28pDisplay::OnWifiClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_DOUBLE_CLICKED) {
        display->ShowWifiInfo();
    }
}

void Es3c28pDisplay::OnWifiCloseClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    display->ShowWifiPage(false);
}

void Es3c28pDisplay::OnWifiResetClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (!display->wifi_reset_armed_) {
        display->wifi_reset_armed_ = true;
        lv_label_set_text(display->wifi_reset_label_, "TAP AGAIN TO RESET");
        return;
    }
    lv_label_set_text(display->wifi_reset_label_, "RESTARTING...");
    lv_obj_add_state(display->wifi_reset_button_, LV_STATE_DISABLED);
    xTaskCreate([](void*) {
        static_cast<WifiBoard&>(Board::GetInstance()).ResetWifiConfiguration();
        vTaskDelete(nullptr);
    }, "wifi_ui_reset", 3072, nullptr, 4, nullptr);
}

void Es3c28pDisplay::OnWifiEntryClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    auto* row = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const size_t index = reinterpret_cast<size_t>(lv_obj_get_user_data(row)) - 1;
    if (index >= display->wifi_ssids_.size()) {
        return;
    }
    display->selected_wifi_ssid_ = display->wifi_ssids_[index];
    std::string saved_password;
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        if (item.ssid == display->selected_wifi_ssid_) {
            saved_password = item.password;
            break;
        }
    }
    lv_textarea_set_text(display->wifi_password_textarea_,
        saved_password.c_str());
    lv_obj_add_flag(display->wifi_list_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(display->wifi_password_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(display->wifi_password_page_);
}

void Es3c28pDisplay::OnWifiKeyboardReady(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (display->selected_wifi_ssid_.empty()) {
        return;
    }
    const char* password = lv_textarea_get_text(display->wifi_password_textarea_);
    SsidManager::GetInstance().AddSsid(display->selected_wifi_ssid_,
        password != nullptr ? password : "");
    display->ShowNotification("Wi-Fi saved. Restarting...", 2000);
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    }, "wifi_ui_restart", 2048, nullptr, 3, nullptr);
}

void Es3c28pDisplay::OnBrowseClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (display->music_player_ != nullptr) display->music_player_->OpenBrowser();
}

void Es3c28pDisplay::OnPlayerClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    display->ShowMusicBrowser(false);
}

void Es3c28pDisplay::OnBrowserUpClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (display->music_player_ != nullptr) display->music_player_->BrowserUp();
}

void Es3c28pDisplay::OnBrowserEntryClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    auto* row = static_cast<lv_obj_t*>(lv_event_get_target(event));
    size_t index = reinterpret_cast<size_t>(lv_obj_get_user_data(row)) - 1;
    if (display->music_player_ != nullptr) display->music_player_->SelectBrowserEntry(index);
}

void Es3c28pDisplay::OnPreviousClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (display->music_player_ != nullptr) display->music_player_->Previous();
}

void Es3c28pDisplay::OnPlayClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (display->music_player_ != nullptr) display->music_player_->Toggle();
}

void Es3c28pDisplay::OnNextClicked(lv_event_t* event) {
    auto* display = static_cast<Es3c28pDisplay*>(lv_event_get_user_data(event));
    if (display->music_player_ != nullptr) display->music_player_->Next();
}
