#ifndef ES3C28P_WEB_SERVER_H_
#define ES3C28P_WEB_SERVER_H_

#include <esp_http_server.h>

#include <string>

class Es3c28pWebServer {
public:
    Es3c28pWebServer();
    ~Es3c28pWebServer();

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
    static esp_err_t HandleWifiReset(httpd_req_t* req);
    static esp_err_t HandleReboot(httpd_req_t* req);

    bool Authorize(httpd_req_t* req) const;
    static bool ReceiveJson(httpd_req_t* req, std::string& body,
        size_t max_length = 2048);
    static void SendJson(httpd_req_t* req, const char* json,
        const char* status = "200 OK");
    static void ScheduleRestart();

    httpd_handle_t server_ = nullptr;
    std::string access_token_;
};

#endif  // ES3C28P_WEB_SERVER_H_
