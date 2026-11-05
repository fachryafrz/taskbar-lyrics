#pragma once

#include "taskbar_lyrics/lyrics/LyricsProvider.h"
#include "taskbar_lyrics/sync/LyricsSyncEngine.h"
#include "taskbar_lyrics/ui/LyricsRenderer.h"

#include <windows.h>

#include <thread>
#include <unordered_map>

namespace taskbar_lyrics::media {

constexpr UINT kMediaUpdateMessage = WM_APP + 2;

class MediaSessionWorker {
public:
    void Start(HWND destination, int lyricOffsetMilliseconds);
    void Stop();

private:
    void Run(std::stop_token stop);
    void Publish(ui::DisplayText text);

    HWND destination_{};
    std::jthread thread_;
    ui::DisplayText lastPublished_;
    lyrics::LyricsProvider provider_;
    sync::LyricsSyncEngine sync_;
    std::unordered_map<std::wstring, lyrics::LyricsResult> cache_;
};

}  // namespace taskbar_lyrics::media
