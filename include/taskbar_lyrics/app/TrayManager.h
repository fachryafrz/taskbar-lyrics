#pragma once

#include <windows.h>
#include <shellapi.h>

namespace taskbar_lyrics::app {

class TrayManager {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 1;
    static constexpr UINT kToggleCommand = 1001;
    static constexpr UINT kExitCommand = 1002;
    static constexpr UINT kLockPositionCommand = 1003;
    static constexpr UINT kWrapLyricsCommand = 1004;
    static constexpr UINT kStatusCommand = 1005;
    static constexpr UINT kAlwaysOnTopCommand = 1006;
    static constexpr UINT kResetPositionCommand = 1008;

    bool Create(HWND owner);
    void Destroy();

private:
    HWND owner_{};
    NOTIFYICONDATAW icon_{};
};

}  // namespace taskbar_lyrics::app
