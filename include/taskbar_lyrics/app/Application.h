#pragma once

#include "taskbar_lyrics/app/TrayManager.h"
#include "taskbar_lyrics/core/ConfigManager.h"
#include "taskbar_lyrics/media/MediaSessionWorker.h"
#include "taskbar_lyrics/ui/LyricsWindow.h"

#include <windows.h>

namespace taskbar_lyrics::app {

class Application {
public:
    int Run(HINSTANCE instance);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND window,
                                      LONG idObject, LONG idChild, DWORD eventThread, DWORD eventTime);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    static Application* s_instance;

    HINSTANCE instance_{};
    HWND controller_{};
    UINT taskbarCreatedMessage_{};
    HWINEVENTHOOK winEventHook_{};
    core::ConfigManager configManager_;
    core::AppConfig config_{};
    TrayManager tray_;
    ui::LyricsWindow lyricsWindow_;
    media::MediaSessionWorker mediaWorker_;
};

}  // namespace taskbar_lyrics::app
