#pragma once

#include "taskbar_lyrics/lyrics/LrcParser.h"

#include <chrono>
#include <optional>
#include <vector>

namespace taskbar_lyrics::sync {

enum class PlaybackState { Stopped, Paused, Playing };

class LyricsSyncEngine {
public:
    void SetLyrics(std::vector<lyrics::TimedLine> lines);
    void UpdateTimeline(std::chrono::milliseconds position, PlaybackState state,
                        std::chrono::steady_clock::time_point observedAt = std::chrono::steady_clock::now());
    void SetOffset(std::chrono::milliseconds offset);
    [[nodiscard]] std::optional<lyrics::TimedLine> ActiveLine(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

private:
    std::vector<lyrics::TimedLine> lines_;
    std::chrono::milliseconds position_{};
    std::chrono::milliseconds offset_{};
    std::chrono::steady_clock::time_point observedAt_{};
    PlaybackState state_{PlaybackState::Stopped};
};

}  // namespace taskbar_lyrics::sync
