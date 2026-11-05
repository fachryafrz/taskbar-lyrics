#include "taskbar_lyrics/sync/LyricsSyncEngine.h"

#include <algorithm>

namespace taskbar_lyrics::sync {

void LyricsSyncEngine::SetLyrics(std::vector<lyrics::TimedLine> lines) {
    std::sort(lines.begin(), lines.end(), [](const auto& left, const auto& right) {
        return left.timestamp < right.timestamp;
    });
    lines_ = std::move(lines);
}

void LyricsSyncEngine::UpdateTimeline(std::chrono::milliseconds position, PlaybackState state,
                                      std::chrono::steady_clock::time_point observedAt) {
    position_ = position;
    state_ = state;
    observedAt_ = observedAt;
}

void LyricsSyncEngine::SetOffset(std::chrono::milliseconds offset) {
    offset_ = offset;
}

std::optional<lyrics::TimedLine> LyricsSyncEngine::ActiveLine(std::chrono::steady_clock::time_point now) const {
    if (lines_.empty()) {
        return std::nullopt;
    }

    auto effectivePosition = position_ + offset_;
    if (state_ == PlaybackState::Playing) {
        effectivePosition += std::chrono::duration_cast<std::chrono::milliseconds>(now - observedAt_);
    }
    const auto firstAfter = std::upper_bound(lines_.begin(), lines_.end(), effectivePosition,
        [](const auto& position, const lyrics::TimedLine& line) { return position < line.timestamp; });
    if (firstAfter == lines_.begin()) {
        return std::nullopt;
    }
    return *std::prev(firstAfter);
}

}  // namespace taskbar_lyrics::sync
