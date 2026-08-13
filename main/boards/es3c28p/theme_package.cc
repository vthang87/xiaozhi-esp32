#include "theme_package.h"

#include "board.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

namespace {
constexpr char kMagic[4] = {'X', 'T', 'H', 'M'};
constexpr uint16_t kFormatVersion = 1;
constexpr size_t kHeaderSize = 16;
constexpr size_t kMaxJsonSize = 64 * 1024;
constexpr char kTag[] = "ThemePackage";

uint16_t ReadU16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0]) |
        (static_cast<uint16_t>(value[1]) << 8);
}

uint32_t ReadU32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

uint32_t Crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320 &
                static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1))));
        }
    }
    return ~crc;
}

uint32_t JsonColor(cJSON* palette, const char* key, uint32_t fallback) {
    auto* item = cJSON_GetObjectItemCaseSensitive(palette, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return fallback;
    }
    const char* value = item->valuestring;
    if (*value == '#') {
        ++value;
    }
    char* end = nullptr;
    unsigned long color = strtoul(value, &end, 16);
    return end != value && *end == '\0' && color <= 0xFFFFFF
        ? static_cast<uint32_t>(color) : fallback;
}
}  // namespace

Es3c28pThemePackage& Es3c28pThemePackage::GetInstance() {
    static Es3c28pThemePackage instance;
    return instance;
}

Es3c28pThemePackage::Es3c28pThemePackage() {
    LoadFallback();
    partition_ = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "theme");
}

void Es3c28pThemePackage::LoadFallback() {
    tokens_ = {};
#if CONFIG_ES3C28P_DEFAULT_THEME_STORYBOOK_GARDEN
    tokens_.id = "storybook-garden";
    tokens_.name = "Storybook Garden";
    tokens_.kids_mode = true;
    tokens_.screen = 0xD9F0E7;
    tokens_.panel = 0xFFFAF0;
    tokens_.line = 0xA7CFC3;
    tokens_.text = 0x24423D;
    tokens_.muted = 0x57716B;
    tokens_.primary = 0xEF765F;
    tokens_.secondary = 0x8FD6DA;
    tokens_.positive = 0x78BD76;
    tokens_.highlight = 0xF5C84E;
    tokens_.role = 0xA25245;
    tokens_.radius = 14;
#endif
    package_valid_ = false;
}

bool Es3c28pThemePackage::ParseJson(const char* json, size_t length) {
    auto* root = cJSON_ParseWithLength(json, length);
    if (root == nullptr) {
        ESP_LOGE(kTag, "Theme manifest is not valid JSON");
        return false;
    }
    auto next = tokens_;
    auto* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    auto* name = cJSON_GetObjectItemCaseSensitive(root, "name");
    auto* layout = cJSON_GetObjectItemCaseSensitive(root, "layout");
    auto* radius = cJSON_GetObjectItemCaseSensitive(root, "radius");
    auto* palette = cJSON_GetObjectItemCaseSensitive(root, "palette");
    if (!cJSON_IsString(id) || !cJSON_IsObject(palette)) {
        cJSON_Delete(root);
        ESP_LOGE(kTag, "Theme manifest requires id and palette");
        return false;
    }
    next.id = id->valuestring;
    if (cJSON_IsString(name)) {
        next.name = name->valuestring;
    }
    next.kids_mode = cJSON_IsString(layout) &&
        std::string(layout->valuestring) == "kids-storybook";
    if (cJSON_IsNumber(radius)) {
        next.radius = std::clamp(radius->valueint, 0, 20);
    }
    next.screen = JsonColor(palette, "screen", next.screen);
    next.panel = JsonColor(palette, "panel", next.panel);
    next.line = JsonColor(palette, "line", next.line);
    next.text = JsonColor(palette, "text", next.text);
    next.muted = JsonColor(palette, "muted", next.muted);
    next.primary = JsonColor(palette, "primary", next.primary);
    next.secondary = JsonColor(palette, "secondary", next.secondary);
    next.positive = JsonColor(palette, "positive", next.positive);
    next.highlight = JsonColor(palette, "highlight", next.highlight);
    next.role = JsonColor(palette, "role", next.role);
    cJSON_Delete(root);
    tokens_ = std::move(next);
    return true;
}

bool Es3c28pThemePackage::ValidatePartition(size_t* package_size) {
    if (partition_ == nullptr) {
        ESP_LOGW(kTag, "Theme partition is missing; using built-in theme");
        return false;
    }
    std::array<uint8_t, kHeaderSize> header{};
    if (esp_partition_read(partition_, 0, header.data(), header.size()) != ESP_OK ||
        !std::equal(std::begin(kMagic), std::end(kMagic), header.begin()) ||
        ReadU16(header.data() + 4) != kFormatVersion) {
        return false;
    }
    const size_t json_length = ReadU32(header.data() + 8);
    if (json_length == 0 || json_length > kMaxJsonSize ||
        kHeaderSize + json_length > partition_->size) {
        ESP_LOGE(kTag, "Theme manifest length is invalid: %u",
            static_cast<unsigned>(json_length));
        return false;
    }
    std::vector<uint8_t> json(json_length + 1, 0);
    if (esp_partition_read(partition_, kHeaderSize, json.data(), json_length) != ESP_OK ||
        Crc32(json.data(), json_length) != ReadU32(header.data() + 12) ||
        !ParseJson(reinterpret_cast<const char*>(json.data()), json_length)) {
        ESP_LOGE(kTag, "Theme package failed validation");
        return false;
    }
    if (package_size != nullptr) {
        *package_size = kHeaderSize + json_length;
    }
    return true;
}

bool Es3c28pThemePackage::Load() {
    LoadFallback();
    package_valid_ = ValidatePartition();
    ESP_LOGI(kTag, "Using theme '%s' (%s)", tokens_.id.c_str(),
        package_valid_ ? "package" : "built-in fallback");
    return package_valid_;
}

bool Es3c28pThemePackage::Download(const std::string& url,
    std::function<void(int progress, size_t speed)> progress_callback) {
    if (partition_ == nullptr || url.empty()) {
        return false;
    }
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (!http->Open("GET", url) || http->GetStatusCode() != 200) {
        ESP_LOGE(kTag, "Cannot download theme from %s", url.c_str());
        return false;
    }
    const size_t content_length = http->GetBodyLength();
    return InstallFromStream(content_length,
        [&http](char* buffer, size_t size) {
            return http->Read(buffer, size);
        }, std::move(progress_callback));
}

bool Es3c28pThemePackage::InstallFromStream(size_t content_length,
    const std::function<int(char* buffer, size_t size)>& reader,
    std::function<void(int progress, size_t speed)> progress_callback) {
    std::lock_guard<std::mutex> lock(install_mutex_);
    if (partition_ == nullptr || !reader || content_length < kHeaderSize ||
        content_length > partition_->size) {
        ESP_LOGE(kTag, "Theme package size is invalid: %u",
            static_cast<unsigned>(content_length));
        return false;
    }
    const size_t sector_size = esp_partition_get_main_flash_sector_size();
    const size_t erase_size = (content_length + sector_size - 1) /
        sector_size * sector_size;
    if (esp_partition_erase_range(partition_, 0, erase_size) != ESP_OK) {
        return false;
    }
    std::array<char, 1024> buffer{};
    size_t written = 0;
    size_t recent = 0;
    int64_t last_time = esp_timer_get_time();
    while (written < content_length) {
        int count = reader(buffer.data(),
            std::min(buffer.size(), content_length - written));
        if (count <= 0 || esp_partition_write(
                partition_, written, buffer.data(), count) != ESP_OK) {
            ESP_LOGE(kTag, "Theme download stopped at %u bytes",
                static_cast<unsigned>(written));
            return false;
        }
        written += count;
        recent += count;
        const int64_t now = esp_timer_get_time();
        if (progress_callback && (now - last_time >= 500000 ||
                written == content_length)) {
            const size_t speed = recent * 1000000 / (now - last_time);
            progress_callback(static_cast<int>(written * 100 / content_length), speed);
            recent = 0;
            last_time = now;
        }
    }
    LoadFallback();
    package_valid_ = ValidatePartition();
    return package_valid_;
}

std::string Es3c28pThemePackage::Describe() const {
    return tokens_.name + " [" + tokens_.id + "] - " +
        (package_valid_ ? "external package" : "built-in fallback");
}
