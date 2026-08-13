#ifndef ES3C28P_DISPLAY_H_
#define ES3C28P_DISPLAY_H_

#include "display/lcd_display.h"

class SdMusicPlayer;

class Es3c28pDisplay : public SpiLcdDisplay {
public:
    static constexpr int kNavigationHeight = 40;

    Es3c28pDisplay(esp_lcd_panel_io_handle_t panel_io,
        esp_lcd_panel_handle_t panel, int width, int height, int offset_x,
        int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);
    ~Es3c28pDisplay() override;

    bool IsMusicPageActive() const { return music_page_active_; }
    bool IsWifiPageActive() const { return wifi_page_active_; }
    bool IsNavigationBarPoint(int y) const {
        return y >= height_ - kNavigationHeight;
    }
    void ShowMusicPage(bool show);
    void SetMusicInfo(const char* state, const char* track, bool playing = false,
        size_t track_index = 0, size_t track_count = 0);
    void SetMusicProgress(uint32_t elapsed_seconds, uint32_t total_seconds);
    void SetMusicPlayer(SdMusicPlayer* player) { music_player_ = player; }
    void ShowMusicBrowser(bool show);
    void SetBrowserEntries(const std::string& path,
        const std::vector<std::string>& names, const std::vector<bool>& directories);
    void SetupUI() override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetEmotion(const char* emotion) override;
    bool SetTextFont(std::shared_ptr<LvglFont> text_font) override;
    void SetTheme(Theme* theme) override;
    void UpdateStatusBar(bool update_all = false) override;

private:
    lv_obj_t* chat_page_ = nullptr;
    lv_obj_t* chat_state_label_ = nullptr;
    lv_obj_t* chat_assistant_label_ = nullptr;
    lv_obj_t* chat_user_label_ = nullptr;
    lv_obj_t* chat_assistant_role_label_ = nullptr;
    lv_obj_t* chat_voice_title_label_ = nullptr;
    lv_obj_t* chat_voice_hint_label_ = nullptr;
    lv_obj_t* chat_volume_label_ = nullptr;
    lv_obj_t* chat_wifi_icon_ = nullptr;
    lv_obj_t* chat_battery_icon_ = nullptr;
    lv_obj_t* chat_battery_label_ = nullptr;
    lv_obj_t* chat_volume_down_button_ = nullptr;
    lv_obj_t* chat_volume_up_button_ = nullptr;
    lv_obj_t* music_page_ = nullptr;
    lv_obj_t* music_state_label_ = nullptr;
    lv_obj_t* music_track_label_ = nullptr;
    lv_obj_t* music_track_index_label_ = nullptr;
    lv_obj_t* music_track_meta_label_ = nullptr;
    lv_obj_t* music_progress_bar_ = nullptr;
    lv_obj_t* music_elapsed_label_ = nullptr;
    lv_obj_t* music_total_label_ = nullptr;
    lv_obj_t* music_volume_label_ = nullptr;
    lv_obj_t* music_wifi_icon_ = nullptr;
    lv_obj_t* music_battery_icon_ = nullptr;
    lv_obj_t* music_battery_label_ = nullptr;
    lv_obj_t* music_play_icon_ = nullptr;
    lv_obj_t* browser_page_ = nullptr;
    lv_obj_t* browser_path_label_ = nullptr;
    lv_obj_t* browser_list_ = nullptr;
    lv_obj_t* browser_volume_label_ = nullptr;
    lv_obj_t* browser_wifi_icon_ = nullptr;
    lv_obj_t* browser_battery_icon_ = nullptr;
    lv_obj_t* browser_battery_label_ = nullptr;
    lv_obj_t* navigation_bar_ = nullptr;
    lv_obj_t* wifi_page_ = nullptr;
    lv_obj_t* wifi_list_ = nullptr;
    lv_obj_t* wifi_status_label_ = nullptr;
    lv_obj_t* wifi_password_page_ = nullptr;
    lv_obj_t* wifi_password_textarea_ = nullptr;
    lv_obj_t* wifi_keyboard_ = nullptr;
    lv_obj_t* wifi_info_page_ = nullptr;
    lv_obj_t* wifi_info_ssid_label_ = nullptr;
    lv_obj_t* wifi_info_ip_label_ = nullptr;
    lv_obj_t* wifi_info_signal_label_ = nullptr;
    lv_obj_t* wifi_info_web_label_ = nullptr;
    lv_obj_t* wifi_reset_button_ = nullptr;
    lv_obj_t* wifi_reset_label_ = nullptr;
    lv_obj_t* chat_tab_ = nullptr;
    lv_obj_t* music_tab_ = nullptr;
    lv_obj_t* chat_mic_button_ = nullptr;
    lv_obj_t* chat_mic_icon_ = nullptr;
    lv_obj_t* volume_label_ = nullptr;
    SdMusicPlayer* music_player_ = nullptr;
    bool music_page_active_ = false;
    bool wifi_page_active_ = false;
    bool wifi_reset_armed_ = false;
    int displayed_volume_ = -1;
    int displayed_battery_ = -1;
    bool displayed_charging_ = false;
    std::string displayed_network_icon_;
    std::vector<std::string> wifi_ssids_;
    std::string selected_wifi_ssid_;
    int displayed_chat_state_ = -1;

    void SetupMusicUi();
    void SetupStorybookUi();
    void SetupWifiInfoUi();
    void ApplyMusicTheme();
    void UpdateTabStyles();
    void ShowWifiPage(bool show);
    void ShowWifiInfo();
    void ScanWifiNetworks();
    void SetWifiNetworks(const std::vector<std::string>& ssids,
        const std::vector<int>& signal_levels);
    static void OnChatTabClicked(lv_event_t* event);
    static void OnMusicTabClicked(lv_event_t* event);
    static void OnChatMicClicked(lv_event_t* event);
    static void OnVolumeDownClicked(lv_event_t* event);
    static void OnVolumeUpClicked(lv_event_t* event);
    static void OnWifiClicked(lv_event_t* event);
    static void OnWifiCloseClicked(lv_event_t* event);
    static void OnWifiResetClicked(lv_event_t* event);
    static void OnWifiEntryClicked(lv_event_t* event);
    static void OnWifiKeyboardReady(lv_event_t* event);
    static void WifiScanTask(void* arg);
    static void OnBrowseClicked(lv_event_t* event);
    static void OnPlayerClicked(lv_event_t* event);
    static void OnBrowserUpClicked(lv_event_t* event);
    static void OnBrowserEntryClicked(lv_event_t* event);
    static void OnPreviousClicked(lv_event_t* event);
    static void OnPlayClicked(lv_event_t* event);
    static void OnNextClicked(lv_event_t* event);
};

#endif  // ES3C28P_DISPLAY_H_
