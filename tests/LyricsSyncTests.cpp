#include "taskbar_lyrics/sync/LyricsSyncEngine.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

void RunLrcParserTests();

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    RunLrcParserTests();

    using namespace std::chrono_literals;
    const auto observedAt = std::chrono::steady_clock::now();
    taskbar_lyrics::sync::LyricsSyncEngine engine;
    engine.SetLyrics({{1s, L"First"}, {5s, L"Second"}});
    engine.UpdateTimeline(2s, taskbar_lyrics::sync::PlaybackState::Playing, observedAt);
    Expect(engine.ActiveLine(observedAt + 2500ms)->text == L"First", "Expected interpolation while playing.");
    Expect(engine.ActiveLine(observedAt + 3s)->text == L"Second", "Expected binary-search line change.");
    engine.UpdateTimeline(2s, taskbar_lyrics::sync::PlaybackState::Paused, observedAt);
    Expect(engine.ActiveLine(observedAt + 10s)->text == L"First", "Expected pause to freeze position.");
    engine.SetOffset(4s);
    Expect(engine.ActiveLine(observedAt)->text == L"Second", "Expected configured lyric offset.");
}
