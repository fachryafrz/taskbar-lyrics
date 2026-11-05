#include "taskbar_lyrics/app/Application.h"

#include <algorithm>
#include <memory>
#include <shellapi.h>

namespace taskbar_lyrics::app {
namespace {
constexpr wchar_t kControllerClass[] = L"LyrixController";
}

Application* Application::s_instance = nullptr;

void CALLBACK Application::WinEventProc(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
    if (s_instance == nullptr || !s_instance->config_.alwaysOnTop) {
        return;
    }
    if (window == s_instance->controller_ || window == s_instance->lyricsWindow_.GetHwnd()) {
        return;
    }
    s_instance->lyricsWindow_.EnsureTopmost();
}

int Application::Run(HINSTANCE instance) {
    instance_ = instance;
    s_instance = this;
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    winEventHook_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    // Taskbar embedding crosses process boundaries; a shared DPI context avoids
    // unnecessary coordinate virtualization while positioning the child window.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kControllerClass;
    windowClass.lpfnWndProc = WindowProc;
    if (RegisterClassW(&windowClass) == 0) {
        MessageBoxW(nullptr, L"The controller window class could not be registered.", L"Lyrix", MB_ICONERROR | MB_OK);
        return 11;
    }

    controller_ = CreateWindowExW(0, kControllerClass, L"Lyrix Controller", WS_OVERLAPPED,
                                  0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (controller_ == nullptr) {
        MessageBoxW(nullptr, L"The controller window could not be created.", L"Lyrix", MB_ICONERROR | MB_OK);
        return 12;
    }

    config_ = configManager_.Load();
    if (!lyricsWindow_.Create(instance, config_, controller_)) {
        wchar_t errorText[256]{};
        swprintf_s(errorText, L"Lyrix could not create its overlay window.\n\nError code: 0x%08X",
                   static_cast<unsigned int>(lyricsWindow_.CreationError()));
        MessageBoxW(nullptr,
                    errorText,
                    L"Lyrix", MB_ICONERROR | MB_OK);
        return 13;
    }
    if (!tray_.Create(controller_)) {
        MessageBoxW(nullptr,
                    L"The lyric overlay is running, but Windows did not accept the tray icon.\n\n"
                    L"Restart Explorer or sign out and back in, then launch Lyrix again.",
                    L"Lyrix", MB_ICONWARNING | MB_OK);
    }
    mediaWorker_.Start(controller_, config_.lyricOffsetMilliseconds);
    SetTimer(controller_, 1001, 300, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return message.message == WM_QUIT ? static_cast<int>(message.wParam) : 14;
}

LRESULT CALLBACK Application::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    const auto self = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        tray_.Create(controller_);
        lyricsWindow_.Reposition();
        return 0;
    }
    if (message == media::kMediaUpdateMessage) {
        std::unique_ptr<ui::DisplayText> text{reinterpret_cast<ui::DisplayText*>(lParam)};
        lyricsWindow_.UpdateText(std::move(*text));
        lyricsWindow_.EnsureTopmost();
        return 0;
    }
    if (message == TrayManager::kCallbackMessage && lParam == WM_RBUTTONUP) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (config_.visible ? MF_CHECKED : MF_UNCHECKED),
                    TrayManager::kToggleCommand, L"Show Taskbar Window");
        AppendMenuW(menu, MF_STRING | (config_.locked ? MF_CHECKED : MF_UNCHECKED),
                    TrayManager::kLockPositionCommand, L"Lock Position");
        AppendMenuW(menu, MF_STRING | (config_.wrapLongLyrics ? MF_CHECKED : MF_UNCHECKED),
                    TrayManager::kWrapLyricsCommand, L"Wrap Long Lyrics");
        AppendMenuW(menu, MF_STRING | (config_.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED),
                    TrayManager::kAlwaysOnTopCommand, L"Always on top");
        AppendMenuW(menu, MF_STRING, TrayManager::kStatusCommand, L"Status...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, TrayManager::kExitCommand, L"Exit");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(controller_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, controller_, nullptr);
        DestroyMenu(menu);
        return 0;
    }

    switch (message) {
    case ui::LyricsWindow::kPositionChangedMessage:
        config_.taskbarOffsetX = static_cast<int>(static_cast<intptr_t>(wParam));
        config_.taskbarOffsetY = static_cast<int>(static_cast<intptr_t>(lParam));
        lyricsWindow_.SetTaskbarOffset(config_.taskbarOffsetX, config_.taskbarOffsetY);
        configManager_.Save(config_);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case TrayManager::kToggleCommand:
            config_.visible = !config_.visible;
            lyricsWindow_.Show(config_.visible);
            configManager_.Save(config_);
            return 0;
        case TrayManager::kLockPositionCommand:
            config_.locked = !config_.locked;
            lyricsWindow_.SetLocked(config_.locked);
            configManager_.Save(config_);
            return 0;
        case TrayManager::kWrapLyricsCommand:
            config_.wrapLongLyrics = !config_.wrapLongLyrics;
            lyricsWindow_.SetWrapLongLyrics(config_.wrapLongLyrics);
            configManager_.Save(config_);
            return 0;
        case TrayManager::kAlwaysOnTopCommand:
            config_.alwaysOnTop = !config_.alwaysOnTop;
            lyricsWindow_.SetAlwaysOnTop(config_.alwaysOnTop);
            configManager_.Save(config_);
            return 0;
        case TrayManager::kExitCommand:
            DestroyWindow(controller_);
            return 0;
        case TrayManager::kStatusCommand: {
            const auto details = lyricsWindow_.StatusText();
            MessageBoxW(nullptr, details.c_str(), L"Lyrix Status", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        default:
            break;
        }
        break;
    case WM_TIMER:
        if (wParam == 1001) {
            lyricsWindow_.EnsureTopmost();
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(controller_, 1001);
        if (winEventHook_ != nullptr) {
            UnhookWinEvent(winEventHook_);
            winEventHook_ = nullptr;
        }
        s_instance = nullptr;
        mediaWorker_.Stop();
        configManager_.Save(config_);
        tray_.Destroy();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(controller_, message, wParam, lParam);
}

}  // namespace taskbar_lyrics::app
