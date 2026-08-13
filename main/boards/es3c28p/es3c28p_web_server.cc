#include "es3c28p_web_server.h"

#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "settings.h"
#include "system_info.h"
#include "theme_package.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr char kTag[] = "ES3C28PWeb";

constexpr char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>ES3C28P Device</title>
<style>
:root{--bg:#d9f0e7;--surface:#fffaf0;--surface-2:#edf7f2;--text:#24423d;--muted:#57716b;--line:#a7cfc3;--accent:#d95845;--accent-ink:#fffaf0;--ok:#397b4b;--error:#a63d32;--radius:16px;--shadow:0 16px 40px rgba(36,66,61,.12)}
@media(prefers-color-scheme:dark){:root{--bg:#142825;--surface:#1d3530;--surface-2:#25413b;--text:#eff8f3;--muted:#b5cbc3;--line:#446b61;--accent:#f18470;--accent-ink:#172a26;--ok:#8fd29a;--error:#ff9b90;--shadow:0 16px 40px rgba(0,0,0,.24)}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 ui-rounded,"SF Pro Rounded","Avenir Next",system-ui,sans-serif}button,input{font:inherit}button{cursor:pointer}.shell{width:min(920px,calc(100% - 32px));margin:0 auto;padding:32px 0 56px}.top{display:grid;grid-template-columns:1fr auto;gap:20px;align-items:end;margin-bottom:24px}.brand{margin:0;font-size:clamp(28px,6vw,52px);line-height:1;letter-spacing:-.045em}.sub{margin:8px 0 0;color:var(--muted);max-width:42ch}.live{padding:9px 12px;border:1px solid var(--line);border-radius:999px;color:var(--ok);font-weight:700;white-space:nowrap}.status{display:grid;grid-template-columns:repeat(4,1fr);gap:1px;overflow:hidden;border:1px solid var(--line);border-radius:var(--radius);background:var(--line);box-shadow:var(--shadow);margin-bottom:20px}.metric{background:var(--surface);padding:16px;min-width:0}.metric span{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.06em;text-transform:uppercase}.metric strong{display:block;margin-top:5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.grid{display:grid;grid-template-columns:1fr 1.15fr;gap:20px}.panel{background:var(--surface);border:1px solid var(--line);border-radius:var(--radius);padding:22px;box-shadow:var(--shadow)}h2{margin:0 0 5px;font-size:21px;letter-spacing:-.02em}.help{margin:0 0 20px;color:var(--muted);font-size:14px}.field{display:grid;gap:8px;margin-top:18px}.field label{font-weight:750}.range-row{display:grid;grid-template-columns:1fr 54px;gap:12px;align-items:center}input[type=range]{width:100%;accent-color:var(--accent)}input[type=url],input[type=file]{width:100%;min-height:48px;border:1px solid var(--line);border-radius:12px;background:var(--surface-2);color:var(--text);padding:11px 12px}input:focus-visible,button:focus-visible{outline:3px solid color-mix(in srgb,var(--accent),transparent 55%);outline-offset:2px}.value{text-align:right;font-variant-numeric:tabular-nums;font-weight:750}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.button{min-height:46px;border:1px solid var(--line);border-radius:12px;padding:10px 16px;background:var(--surface-2);color:var(--text);font-weight:800;white-space:nowrap}.button.primary{border-color:var(--accent);background:var(--accent);color:var(--accent-ink)}.button:active{transform:translateY(1px)}.button:disabled{cursor:wait;opacity:.55}.tabs{display:grid;grid-template-columns:1fr 1fr;gap:6px;padding:5px;background:var(--surface-2);border-radius:14px;margin:18px 0}.tab{border:0;border-radius:10px;padding:10px;background:transparent;color:var(--muted);font-weight:800}.tab.active{background:var(--surface);color:var(--text);box-shadow:0 2px 8px rgba(36,66,61,.09)}.tab-panel[hidden]{display:none}.notice{min-height:24px;margin-top:14px;color:var(--muted);font-size:14px}.notice.ok{color:var(--ok)}.notice.error{color:var(--error)}.maintenance{grid-column:1/-1;display:flex;align-items:center;justify-content:space-between;gap:16px}.maintenance .help{margin:4px 0 0}.danger{color:var(--error)}
@media(max-width:720px){.shell{width:min(100% - 20px,560px);padding-top:20px}.top{grid-template-columns:1fr;align-items:start}.live{justify-self:start}.status{grid-template-columns:1fr 1fr}.grid{grid-template-columns:1fr}.maintenance{align-items:flex-start;flex-direction:column}.maintenance .actions{margin-top:0;width:100%}.maintenance .button{flex:1}.panel{padding:18px}}
@media(prefers-reduced-motion:no-preference){.button,.tab{transition:transform .12s ease,background-color .18s ease,color .18s ease}}
</style>
</head>
<body>
<main class="shell">
  <header class="top">
    <div><h1 class="brand">Your ES3C28P</h1><p class="sub">Device settings and independent theme updates on your local network.</p></div>
    <div class="live" id="connection">Connecting</div>
  </header>
  <section class="status" aria-label="Device status">
    <div class="metric"><span>IP address</span><strong id="ip">...</strong></div>
    <div class="metric"><span>Wi-Fi</span><strong id="ssid">...</strong></div>
    <div class="metric"><span>Theme</span><strong id="theme">...</strong></div>
    <div class="metric"><span>Battery</span><strong id="battery">...</strong></div>
  </section>
  <div class="grid">
    <section class="panel">
      <h2>Device controls</h2>
      <p class="help">Changes are saved on the device.</p>
      <div class="field"><label for="volume">Speaker volume</label><div class="range-row"><input id="volume" type="range" min="0" max="100" step="5"><output id="volumeValue" class="value">0%</output></div></div>
      <div class="field"><label for="brightness">Screen brightness</label><div class="range-row"><input id="brightness" type="range" min="5" max="100" step="5"><output id="brightnessValue" class="value">0%</output></div></div>
      <div class="actions"><button class="button primary" id="saveSettings">Save controls</button></div>
      <p class="notice" id="settingsNotice" role="status"></p>
    </section>
    <section class="panel">
      <h2>Install a theme</h2>
      <p class="help">Packages are validated before the device restarts.</p>
      <div class="tabs" role="tablist">
        <button class="tab active" data-tab="file" role="tab">Theme file</button>
        <button class="tab" data-tab="url" role="tab">Direct URL</button>
      </div>
      <div class="tab-panel" id="filePanel">
        <div class="field"><label for="themeFile">Select .theme.bin</label><input id="themeFile" type="file" accept=".bin,.theme.bin,application/octet-stream"></div>
        <div class="actions"><button class="button primary" id="uploadFile">Upload file</button></div>
      </div>
      <div class="tab-panel" id="urlPanel" hidden>
        <div class="field"><label for="themeUrl">Package URL</label><input id="themeUrl" type="url" inputmode="url" placeholder="https://example.com/theme.bin"></div>
        <div class="actions"><button class="button primary" id="installUrl">Install URL</button></div>
      </div>
      <p class="notice" id="themeNotice" role="status"></p>
    </section>
    <section class="panel maintenance">
      <div><h2>Network and restart</h2><p class="help">Wi-Fi setup restarts the device in configuration mode.</p></div>
      <div class="actions"><button class="button" id="wifiReset">Set up Wi-Fi</button><button class="button danger" id="reboot">Restart</button></div>
    </section>
  </div>
</main>
<script>
const $=id=>document.getElementById(id);let token='';
function notice(id,text,type=''){const el=$(id);el.textContent=text;el.className='notice '+type}
function busy(button,on,label){if(!button.dataset.label)button.dataset.label=button.textContent;button.disabled=on;button.textContent=on?label:button.dataset.label}
async function api(path,options={}){options.headers={...(options.headers||{}),'X-Device-Token':token};const response=await fetch(path,options);const data=await response.json().catch(()=>({success:false,error:'Invalid device response'}));if(!response.ok||data.success===false)throw new Error(data.error||'Request failed');return data}
async function load(){try{const data=await fetch('/api/status',{cache:'no-store'}).then(r=>r.json());token=data.token;$('ip').textContent=data.ip||'Not connected';$('ssid').textContent=data.ssid||'Not connected';$('theme').textContent=data.theme_name;$('battery').textContent=data.battery_level+'%';$('volume').value=data.volume;$('brightness').value=data.brightness;$('volumeValue').textContent=data.volume+'%';$('brightnessValue').textContent=data.brightness+'%';$('connection').textContent='Connected';$('connection').classList.add('ok')}catch(error){$('connection').textContent='Offline';notice('settingsNotice',error.message,'error')}}
for(const id of ['volume','brightness'])$(id).addEventListener('input',event=>$(id+'Value').textContent=event.target.value+'%');
document.querySelectorAll('.tab').forEach(tab=>tab.addEventListener('click',()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('active',x===tab));$('filePanel').hidden=tab.dataset.tab!=='file';$('urlPanel').hidden=tab.dataset.tab!=='url'}));
$('saveSettings').addEventListener('click',async()=>{const button=$('saveSettings');busy(button,true,'Saving');notice('settingsNotice','Saving controls...');try{await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({volume:Number($('volume').value),brightness:Number($('brightness').value)})});notice('settingsNotice','Controls saved.','ok')}catch(error){notice('settingsNotice',error.message,'error')}finally{busy(button,false)}});
$('uploadFile').addEventListener('click',async()=>{const file=$('themeFile').files[0];if(!file){notice('themeNotice','Choose a theme package first.','error');return}const button=$('uploadFile');busy(button,true,'Uploading');notice('themeNotice','Uploading and validating '+file.name+'...');try{await api('/api/theme/file',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:await file.arrayBuffer()});notice('themeNotice','Theme installed. Device is restarting.','ok')}catch(error){notice('themeNotice',error.message,'error');busy(button,false)}});
$('installUrl').addEventListener('click',async()=>{const url=$('themeUrl').value.trim();if(!url){notice('themeNotice','Enter a direct package URL.','error');return}const button=$('installUrl');busy(button,true,'Installing');notice('themeNotice','Downloading and validating the package...');try{await api('/api/theme/url',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url})});notice('themeNotice','Theme installed. Device is restarting.','ok')}catch(error){notice('themeNotice',error.message,'error');busy(button,false)}});
async function maintenance(path,button,message){busy(button,true,'Working');try{await api(path,{method:'POST'});notice('settingsNotice',message,'ok')}catch(error){notice('settingsNotice',error.message,'error');busy(button,false)}}
$('wifiReset').addEventListener('click',()=>{if(confirm('Restart and enter Wi-Fi setup mode?'))maintenance('/api/wifi/reset',$('wifiReset'),'Restarting in Wi-Fi setup mode.')});
$('reboot').addEventListener('click',()=>{if(confirm('Restart the device now?'))maintenance('/api/reboot',$('reboot'),'Restarting device.')});
load();
</script>
</body>
</html>)HTML";
}  // namespace

Es3c28pWebServer::Es3c28pWebServer() {
    char token[17];
    snprintf(token, sizeof(token), "%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()));
    access_token_ = token;
}

Es3c28pWebServer::~Es3c28pWebServer() {
    Stop();
}

std::string Es3c28pWebServer::url() const {
    const auto ip = WifiManager::GetInstance().GetIpAddress();
    return ip.empty() ? std::string() : "http://" + ip + "/";
}

bool Es3c28pWebServer::Start() {
    if (server_ != nullptr) {
        return true;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.recv_wait_timeout = 20;
    config.send_wait_timeout = 20;
    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start device web server");
        server_ = nullptr;
        return false;
    }
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = HandleIndex, .user_ctx = this},
        {.uri = "/api/status", .method = HTTP_GET, .handler = HandleStatus, .user_ctx = this},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = HandleSettings, .user_ctx = this},
        {.uri = "/api/theme/file", .method = HTTP_POST, .handler = HandleThemeFile, .user_ctx = this},
        {.uri = "/api/theme/url", .method = HTTP_POST, .handler = HandleThemeUrl, .user_ctx = this},
        {.uri = "/api/wifi/reset", .method = HTTP_POST, .handler = HandleWifiReset, .user_ctx = this},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = HandleReboot, .user_ctx = this},
    };
    for (const auto& handler : handlers) {
        if (httpd_register_uri_handler(server_, &handler) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to register %s", handler.uri);
            Stop();
            return false;
        }
    }
    ESP_LOGI(kTag, "Device web server started at %s", url().c_str());
    return true;
}

void Es3c28pWebServer::Stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

bool Es3c28pWebServer::Authorize(httpd_req_t* req) const {
    const size_t length = httpd_req_get_hdr_value_len(req, "X-Device-Token");
    if (length != access_token_.size()) {
        SendJson(req, "{\"success\":false,\"error\":\"Unauthorized request\"}",
            "403 Forbidden");
        return false;
    }
    std::vector<char> token(length + 1, 0);
    if (httpd_req_get_hdr_value_str(req, "X-Device-Token",
            token.data(), token.size()) != ESP_OK || access_token_ != token.data()) {
        SendJson(req, "{\"success\":false,\"error\":\"Unauthorized request\"}",
            "403 Forbidden");
        return false;
    }
    return true;
}

void Es3c28pWebServer::SendJson(httpd_req_t* req, const char* json,
    const char* status) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

bool Es3c28pWebServer::ReceiveJson(httpd_req_t* req, std::string& body,
    size_t max_length) {
    if (req->content_len == 0 || req->content_len > max_length) {
        return false;
    }
    body.resize(req->content_len);
    size_t received = 0;
    while (received < body.size()) {
        const int count = httpd_req_recv(req, body.data() + received,
            body.size() - received);
        if (count <= 0) {
            return false;
        }
        received += count;
    }
    return true;
}

esp_err_t Es3c28pWebServer::HandleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
        "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

esp_err_t Es3c28pWebServer::HandleStatus(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    auto& board = Board::GetInstance();
    auto& wifi = WifiManager::GetInstance();
    int battery = 0;
    bool charging = false;
    bool discharging = false;
    board.GetBatteryLevel(battery, charging, discharging);
    auto* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "token", self->access_token_.c_str());
    cJSON_AddStringToObject(root, "ip", wifi.GetIpAddress().c_str());
    cJSON_AddStringToObject(root, "ssid", wifi.GetSsid().c_str());
    cJSON_AddNumberToObject(root, "rssi", wifi.GetRssi());
    cJSON_AddStringToObject(root, "mac", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root, "theme_name",
        Es3c28pThemePackage::GetInstance().tokens().name.c_str());
    cJSON_AddBoolToObject(root, "theme_package",
        Es3c28pThemePackage::GetInstance().package_valid());
    cJSON_AddNumberToObject(root, "volume", board.GetAudioCodec()->output_volume());
    cJSON_AddNumberToObject(root, "brightness", board.GetBacklight()->brightness());
    cJSON_AddNumberToObject(root, "battery_level", battery);
    cJSON_AddBoolToObject(root, "charging", charging);
    char* json = cJSON_PrintUnformatted(root);
    SendJson(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleSettings(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendJson(req, "{\"success\":false,\"error\":\"Invalid settings payload\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* volume = root ? cJSON_GetObjectItemCaseSensitive(root, "volume") : nullptr;
    auto* brightness = root ? cJSON_GetObjectItemCaseSensitive(root, "brightness") : nullptr;
    if (!cJSON_IsNumber(volume) || !cJSON_IsNumber(brightness)) {
        if (root) cJSON_Delete(root);
        SendJson(req, "{\"success\":false,\"error\":\"Volume and brightness are required\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    auto& board = Board::GetInstance();
    board.GetAudioCodec()->SetOutputVolume(std::clamp(volume->valueint, 0, 100));
    board.GetBacklight()->SetBrightness(
        static_cast<uint8_t>(std::clamp(brightness->valueint, 5, 100)), true);
    board.GetDisplay()->UpdateStatusBar(true);
    cJSON_Delete(root);
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleThemeFile(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    const bool ok = Es3c28pThemePackage::GetInstance().InstallFromStream(
        req->content_len, [req](char* buffer, size_t size) {
            return httpd_req_recv(req, buffer, size);
        });
    if (!ok) {
        SendJson(req, "{\"success\":false,\"error\":\"Theme file failed CRC or manifest validation\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleThemeUrl(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendJson(req, "{\"success\":false,\"error\":\"Invalid URL payload\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* url = root ? cJSON_GetObjectItemCaseSensitive(root, "url") : nullptr;
    if (!cJSON_IsString(url) || url->valuestring == nullptr ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        if (root) cJSON_Delete(root);
        SendJson(req, "{\"success\":false,\"error\":\"Enter a direct HTTP or HTTPS URL\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    const std::string package_url = url->valuestring;
    cJSON_Delete(root);
    if (!Es3c28pThemePackage::GetInstance().Download(package_url)) {
        SendJson(req, "{\"success\":false,\"error\":\"Theme download or validation failed\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleWifiReset(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    Settings settings("wifi", true);
    settings.SetInt("force_ap", 1);
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleReboot(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

void Es3c28pWebServer::ScheduleRestart() {
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(700));
        esp_restart();
    }, "web_restart", 2048, nullptr, 4, nullptr);
}
