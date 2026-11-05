#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace taskbar_lyrics::lyrics {

struct TimedLine {
    std::chrono::milliseconds timestamp{};
    std::wstring text;

    auto operator<=>(const TimedLine&) const = default;
};

[[nodiscard]] std::vector<TimedLine> ParseLrc(std::wstring_view source);

}  // namespace taskbar_lyrics::lyrics
