#ifndef ES3C28P_THEME_PACKAGE_H_
#define ES3C28P_THEME_PACKAGE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <esp_partition.h>

struct Es3c28pThemeTokens {
    std::string id = "studio-precision";
    std::string name = "Studio Precision";
    bool kids_mode = false;
    uint32_t screen = 0x17191B;
    uint32_t panel = 0x222528;
    uint32_t line = 0x383D41;
    uint32_t text = 0xF4F1E9;
    uint32_t muted = 0x999FA2;
    uint32_t primary = 0xF0B84A;
    uint32_t secondary = 0x69C8CF;
    uint32_t positive = 0x78BD76;
    uint32_t highlight = 0xF5C84E;
    uint32_t role = 0xA25245;
    int radius = 3;
};

class Es3c28pThemePackage {
public:
    static Es3c28pThemePackage& GetInstance();

    bool Load();
    bool Download(const std::string& url,
        std::function<void(int progress, size_t speed)> progress_callback = {});
    bool InstallFromStream(size_t content_length,
        const std::function<int(char* buffer, size_t size)>& reader,
        std::function<void(int progress, size_t speed)> progress_callback = {});
    const Es3c28pThemeTokens& tokens() const { return tokens_; }
    bool package_valid() const { return package_valid_; }
    std::string Describe() const;

private:
    Es3c28pThemePackage();
    void LoadFallback();
    bool ParseJson(const char* json, size_t length);
    bool ValidatePartition(size_t* package_size = nullptr);

    const esp_partition_t* partition_ = nullptr;
    Es3c28pThemeTokens tokens_;
    bool package_valid_ = false;
    std::mutex install_mutex_;
};

#endif  // ES3C28P_THEME_PACKAGE_H_
