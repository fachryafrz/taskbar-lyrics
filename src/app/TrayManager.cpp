#include "taskbar_lyrics/app/TrayManager.h"

#include <shellapi.h>

namespace taskbar_lyrics::app {

bool TrayManager::Create(HWND owner) {
    owner_ = owner;
    icon_.cbSize = sizeof(icon_);
    icon_.hWnd = owner;
    icon_.uID = 1;
    icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon_.uCallbackMessage = kCallbackMessage;
    icon_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(icon_.szTip, L"Lyrix");
    return Shell_NotifyIconW(NIM_ADD, &icon_) == TRUE;
}

void TrayManager::Destroy() {
    if (icon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &icon_);
        icon_ = {};
    }
}

}  // namespace taskbar_lyrics::app
