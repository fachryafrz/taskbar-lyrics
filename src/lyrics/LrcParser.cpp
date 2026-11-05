#include "taskbar_lyrics/lyrics/LrcParser.h"

#include <algorithm>
#include <cwchar>
#include <sstream>

namespace taskbar_lyrics::lyrics {
namespace {

std::optional<std::chrono::milliseconds> ParseTimestamp(std::wstring_view token) {
    const auto colon = token.find(L':');
    if (colon == std::wstring_view::npos) {
        return std::nullopt;
    }

    const std::wstring minutesText{token.substr(0, colon)};
    const std::wstring secondsText{token.substr(colon + 1)};
    wchar_t* end{};
    const long minutes = std::wcstol(minutesText.c_str(), &end, 10);
    if (*end != L'\0' || minutes < 0) {
        return std::nullopt;
    }

    const double seconds = std::wcstod(secondsText.c_str(), &end);
    if (*end != L'\0' || seconds < 0.0 || seconds >= 60.0) {
        return std::nullopt;
    }

    return std::chrono::minutes{minutes} + std::chrono::milliseconds{static_cast<long long>(seconds * 1000.0)};
}

}  // namespace

std::vector<TimedLine> ParseLrc(std::wstring_view source) {
    std::vector<TimedLine> lines;
    std::wistringstream input{std::wstring{source}};
    std::wstring line;

    while (std::getline(input, line)) {
        std::vector<std::chrono::milliseconds> timestamps;
        std::size_t cursor{};
        while (cursor < line.size() && line[cursor] == L'[') {
            const auto close = line.find(L']', cursor + 1);
            if (close == std::wstring::npos) {
                break;
            }
            const auto timestamp = ParseTimestamp(std::wstring_view{line}.substr(cursor + 1, close - cursor - 1));
            if (!timestamp.has_value()) {
                break;
            }
            timestamps.push_back(*timestamp);
            cursor = close + 1;
        }

        if (timestamps.empty()) {
            continue;
        }
        const auto start = line.find_first_not_of(L" \t\r\n", cursor);
        const auto end = line.find_last_not_of(L" \t\r\n");
        const std::wstring text = (start != std::wstring::npos && end != std::wstring::npos && start <= end)
            ? line.substr(start, end - start + 1)
            : std::wstring{};
        for (const auto timestamp : timestamps) {
            lines.push_back({timestamp, text});
        }
    }

    std::sort(lines.begin(), lines.end(), [](const TimedLine& left, const TimedLine& right) {
        return left.timestamp < right.timestamp;
    });
    return lines;
}

}  // namespace taskbar_lyrics::lyrics
