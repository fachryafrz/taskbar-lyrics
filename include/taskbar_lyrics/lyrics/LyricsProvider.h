#pragma once

#include "taskbar_lyrics/lyrics/LrcParser.h"

#include <string>
#include <vector>

namespace taskbar_lyrics::lyrics {

struct LyricsResult {
    std::vector<TimedLine> lines;
    std::wstring status;
};

class LyricsProvider {
public:
    [[nodiscard]] LyricsResult Fetch(const std::wstring& title, const std::wstring& artist,
                                     const std::wstring& album, int durationSeconds) const;
};

}  // namespace taskbar_lyrics::lyrics
