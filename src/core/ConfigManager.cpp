#include "taskbar_lyrics/core/ConfigManager.h"

#include <shlobj.h>
#include <fstream>
#include <string>

namespace taskbar_lyrics::core {
namespace {

std::optional<int> ReadInteger(std::wstring_view contents, std::wstring_view key) {
    const auto marker = L"\"" + std::wstring{key} + L"\":";
    const auto start = contents.find(marker);
    if (start == std::wstring_view::npos) {
        return std::nullopt;
    }
    const auto numberStart = start + marker.size();
    wchar_t* end{};
    const auto value = std::wcstol(contents.data() + numberStart, &end, 10);
    return end == contents.data() + numberStart ? std::nullopt : std::optional<int>{static_cast<int>(value)};
}

}  // namespace

std::filesystem::path ConfigManager::Path() const {
    PWSTR directory{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &directory))) {
        return {};
    }
    const std::filesystem::path base{directory};
    CoTaskMemFree(directory);

    const std::filesystem::path newPath = base / L"Lyrix" / L"config.json";
    const std::filesystem::path oldPath = base / L"TaskbarLyrics" / L"config.json";
    if (!std::filesystem::exists(newPath) && std::filesystem::exists(oldPath)) {
        std::error_code ec;
        std::filesystem::create_directories(newPath.parent_path(), ec);
        std::filesystem::copy_file(oldPath, newPath, std::filesystem::copy_options::overwrite_existing, ec);
    }
    return newPath;
}

AppConfig ConfigManager::Load() const {
    AppConfig config;
    const auto path = Path();
    std::wifstream file{path};
    if (!file) {
        return config;
    }
    const std::wstring contents{std::istreambuf_iterator<wchar_t>{file}, {}};
    if (const auto value = ReadInteger(contents, L"width"); value && *value >= 200 && *value <= 800) config.width = *value;
    if (const auto value = ReadInteger(contents, L"height"); value && *value >= 36 && *value <= 120) config.height = *value;
    if (const auto value = ReadInteger(contents, L"taskbarOffsetX"); value && *value >= 0 && *value <= 10000) config.taskbarOffsetX = *value;
    if (const auto value = ReadInteger(contents, L"taskbarOffsetY"); value && *value >= -200 && *value <= 200) config.taskbarOffsetY = *value;
    if (const auto value = ReadInteger(contents, L"lyricOffsetMilliseconds"); value && *value >= -5000 && *value <= 5000) config.lyricOffsetMilliseconds = *value;
    if (const auto value = ReadInteger(contents, L"fontSize"); value && *value >= 8 && *value <= 32) config.fontSize = *value;
    if (const auto value = ReadInteger(contents, L"locked")) config.locked = *value != 0;
    if (const auto value = ReadInteger(contents, L"wrapLongLyrics")) config.wrapLongLyrics = *value != 0;
    if (const auto value = ReadInteger(contents, L"visible")) config.visible = *value != 0;
    if (const auto value = ReadInteger(contents, L"alwaysOnTop")) config.alwaysOnTop = *value != 0;
    return config;
}

bool ConfigManager::Save(const AppConfig& config) const {
    const auto path = Path();
    if (path.empty()) {
        return false;
    }
    std::filesystem::create_directories(path.parent_path());
    std::wofstream file{path, std::ios::trunc};
    if (!file) {
        return false;
    }
    file << L"{\n"
         << L"  \"width\": " << config.width << L",\n"
         << L"  \"height\": " << config.height << L",\n"
         << L"  \"taskbarOffsetX\": " << config.taskbarOffsetX << L",\n"
         << L"  \"taskbarOffsetY\": " << config.taskbarOffsetY << L",\n"
         << L"  \"lyricOffsetMilliseconds\": " << config.lyricOffsetMilliseconds << L",\n"
         << L"  \"fontSize\": " << config.fontSize << L",\n"
         << L"  \"locked\": " << (config.locked ? 1 : 0) << L",\n"
         << L"  \"wrapLongLyrics\": " << (config.wrapLongLyrics ? 1 : 0) << L",\n"
         << L"  \"visible\": " << (config.visible ? 1 : 0) << L",\n"
         << L"  \"alwaysOnTop\": " << (config.alwaysOnTop ? 1 : 0) << L"\n"
         << L"}\n";
    return static_cast<bool>(file);
}

}  // namespace taskbar_lyrics::core
