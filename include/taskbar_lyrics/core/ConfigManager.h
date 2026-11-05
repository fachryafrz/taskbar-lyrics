#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>

namespace taskbar_lyrics::core {

struct AppConfig {
    int width{480};
    int height{36};
    int taskbarOffsetX{180};
    int taskbarOffsetY{0};
    int lyricOffsetMilliseconds{300};
    int fontSize{12};
    bool locked{true};
    bool wrapLongLyrics{true};
    bool visible{true};
    bool alwaysOnTop{true};
};

class ConfigManager {
public:
    [[nodiscard]] AppConfig Load() const;
    bool Save(const AppConfig& config) const;

private:
    [[nodiscard]] std::filesystem::path Path() const;
};

}  // namespace taskbar_lyrics::core
