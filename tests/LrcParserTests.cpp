#include "taskbar_lyrics/lyrics/LrcParser.h"

#include <cstdlib>
#include <iostream>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

void RunLrcParserTests() {
    const auto lines = taskbar_lyrics::lyrics::ParseLrc(
        L"[ar:Artist]\n[00:16.80]Second lyric line\n[00:12.40][00:21.25]Repeated line\n[00:25.00]   Indented lyric with spaces   \ninvalid");
    Expect(lines.size() == 4, "Expected timestamped lines only.");
    Expect(lines[0].timestamp.count() == 12400, "Expected sorted timestamps.");
    Expect(lines[1].text == L"Second lyric line", "Expected text after timestamp.");
    Expect(lines[2].timestamp.count() == 21250, "Expected repeated timestamp parsing.");
    Expect(lines[3].timestamp.count() == 25000, "Expected timestamp.");
    Expect(lines[3].text == L"Indented lyric with spaces", "Expected trimmed lyric text without leading/trailing spaces.");
}
