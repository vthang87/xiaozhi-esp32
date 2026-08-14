#ifndef ES3C28P_WEB_SERVER_H_
#define ES3C28P_WEB_SERVER_H_

#include <esp_http_server.h>

#include <string>

class SdMusicPlayer;

class Es3c28pWebServer {
public:
    Es3c28pWebServer();
    ~Es3c28pWebServer();

    void SetMusicPlayer(SdMusicPlayer* music_player);
    bool Start();
    void Stop();
    bool running() const { return server_ != nullptr; }
    std::string url() const;

private:
    static esp_err_t HandleIndex(httpd_req_t* req);
    static esp_err_t HandleStatus(httpd_req_t* req);
    static esp_err_t HandleSettings(httpd_req_t* req);
    static esp_err_t HandleThemeFile(httpd_req_t* req);
    static esp_err_t HandleThemeUrl(httpd_req_t* req);
    static esp_err_t HandleMedia(httpd_req_t* req);
    static esp_err_t HandleMediaUpload(httpd_req_t* req);
    static esp_err_t HandleMediaDownload(httpd_req_t* req);
    static esp_err_t HandleMediaRemove(httpd_req_t* req);
    static esp_err_t HandleMediaMkdir(httpd_req_t* req);
    static esp_err_t HandleMediaRename(httpd_req_t* req);
    static esp_err_t HandleMediaMove(httpd_req_t* req);
    static esp_err_t HandleMediaOrder(httpd_req_t* req);
    static esp_err_t HandleMediaPlayer(httpd_req_t* req);
    static esp_err_t HandleWifiReset(httpd_req_t* req);
    static esp_err_t HandleReboot(httpd_req_t* req);

    bool Authorize(httpd_req_t* req) const;
    static bool ReceiveJson(httpd_req_t* req, std::string& body,
        size_t max_length = 2048);
    static void SendJson(httpd_req_t* req, const char* json,
        const char* status = "200 OK");
    static void SendError(httpd_req_t* req, const char* message,
        const char* status = "400 Bad Request");
    static void ScheduleRestart();
    SdMusicPlayer* MusicPlayer(httpd_req_t* req) const;

    httpd_handle_t server_ = nullptr;
    std::string access_token_;
    SdMusicPlayer* music_player_ = nullptr;
};

#endif  // ES3C28P_WEB_SERVER_H_
