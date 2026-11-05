#include "taskbar_lyrics/ui/LyricsWindow.h"

namespace taskbar_lyrics::ui {
namespace {
constexpr wchar_t kWindowClass[] = L"LyrixOverlay";
}

bool LyricsWindow::Create(HINSTANCE instance, const core::AppConfig& config, HWND controller) {
    creationError_ = S_OK;
    config_ = config;
    controller_ = controller;
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&windowClass);

    const RECT bounds = taskbar_.WidgetBounds(config.width, config.height);
    const int x = bounds.left + config.taskbarOffsetX;
    const int y = bounds.top + config.taskbarOffsetY;

    HWND taskbarHwnd = FindWindowW(L"Shell_TrayWnd", nullptr);

    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kWindowClass, L"Lyrix", WS_POPUP,
        x, y, config.width, config.height, taskbarHwnd, nullptr, instance, this);
    if (window_ == nullptr && taskbarHwnd != nullptr) {
        window_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            kWindowClass, L"Lyrix", WS_POPUP,
            x, y, config.width, config.height, nullptr, nullptr, instance, this);
        if (window_ != nullptr) {
            SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(taskbarHwnd));
        }
    }
    if (window_ == nullptr) {
        creationError_ = HRESULT_FROM_WIN32(GetLastError());
        return false;
    }
    SetLayeredWindowAttributes(window_, RGB(0, 0, 0), 0, LWA_COLORKEY);
    if (!renderer_.Initialize(window_, static_cast<float>(config.fontSize))) {
        creationError_ = renderer_.InitializationError();
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    renderer_.SetWrapLongLyrics(config.wrapLongLyrics);
    Reposition();
    Show(config.visible);
    return true;
}

HRESULT LyricsWindow::CreationError() const {
    return creationError_;
}

void LyricsWindow::Show(bool visible) {
    config_.visible = visible;
    if (window_ != nullptr) {
        ShowWindow(window_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (visible) {
            Reposition();
        }
    }
}

void LyricsWindow::UpdateText(DisplayText text) {
    if (window_ != nullptr && !text_.lyric.empty() && text.lyric != text_.lyric) {
        previousLyric_ = text_.lyric;
        animStart_ = std::chrono::steady_clock::now();
        animProgress_ = 0.0f;
        isAnimating_ = true;
        SetTimer(window_, kAnimTimerId, 16, nullptr);
    }
    text_ = std::move(text);
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
    }
}

void LyricsWindow::SetTaskbarOffset(int x, int y) {
    config_.taskbarOffsetX = x;
    config_.taskbarOffsetY = y;
}

void LyricsWindow::SetAlwaysOnTop(bool alwaysOnTop) {
    config_.alwaysOnTop = alwaysOnTop;
    if (window_ != nullptr) {
        if (alwaysOnTop) {
            SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOCOPYBITS);
        } else {
            SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOCOPYBITS);
        }
    }
}

void LyricsWindow::SetLocked(bool locked) {
    config_.locked = locked;
}

void LyricsWindow::SetWrapLongLyrics(bool wrap) {
    config_.wrapLongLyrics = wrap;
    renderer_.SetWrapLongLyrics(wrap);
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
    }
}

bool LyricsWindow::IsUnderTaskbar() const {
    if (window_ == nullptr) {
        return false;
    }
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    HWND secondaryTaskbar = FindWindowW(L"Shell_SecondaryTrayWnd", nullptr);
    if (taskbar == nullptr && secondaryTaskbar == nullptr) {
        return false;
    }
    HWND current = GetWindow(window_, GW_HWNDPREV);
    while (current != nullptr) {
        if ((taskbar != nullptr && current == taskbar) ||
            (secondaryTaskbar != nullptr && current == secondaryTaskbar)) {
            return true;
        }
        current = GetWindow(current, GW_HWNDPREV);
    }
    return false;
}

void LyricsWindow::EnsureTopmost() {
    if (window_ == nullptr || !config_.visible || !config_.alwaysOnTop) {
        return;
    }
    if (!IsUnderTaskbar()) {
        return;
    }
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOCOPYBITS);
}

void LyricsWindow::Reposition() {
    if (window_ == nullptr) {
        return;
    }
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar != nullptr && GetWindow(window_, GW_OWNER) != taskbar) {
        SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(taskbar));
    }
    const RECT bounds = taskbar_.WidgetBounds(config_.width, config_.height);
    SetWindowPos(window_, config_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 bounds.left + config_.taskbarOffsetX,
                 bounds.top + config_.taskbarOffsetY,
                 config_.width, config_.height,
                 SWP_NOACTIVATE | SWP_NOCOPYBITS | (config_.visible ? SWP_SHOWWINDOW : 0));
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
}

std::wstring LyricsWindow::StatusText() const {
    RECT rect{};
    if (window_ != nullptr) GetWindowRect(window_, &rect);
    wchar_t details[512]{};
    swprintf_s(details,
               L"Taskbar window: %s\nVisible: %s\nAlways on top: %s\nPosition locked: %s\nWrap lyrics: %s\nTopmost style: %s\n"
               L"Screen bounds: %ld, %ld to %ld, %ld\nTaskbar offset: %d, %d\nLyric offset: %d ms\n\n",
               window_ != nullptr && IsWindow(window_) ? L"created" : L"missing",
               window_ != nullptr && IsWindowVisible(window_) ? L"yes" : L"no",
               config_.alwaysOnTop ? L"enabled" : L"disabled",
               config_.locked ? L"locked" : L"unlocked (drag mode)",
               config_.wrapLongLyrics ? L"enabled" : L"disabled",
               window_ != nullptr && (GetWindowLongPtrW(window_, GWL_EXSTYLE) & WS_EX_TOPMOST) ? L"yes" : L"no",
               rect.left, rect.top, rect.right, rect.bottom,
               config_.taskbarOffsetX, config_.taskbarOffsetY,
               config_.lyricOffsetMilliseconds);
    return std::wstring{details} + L"Lyric/status: " + text_.lyric;
}

LRESULT CALLBACK LyricsWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    const auto self = reinterpret_cast<LyricsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT LyricsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR: {
        if (!config_.locked) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }
    case WM_LBUTTONDOWN: {
        if (!config_.locked) {
            isDragging_ = true;
            SetCapture(window_);
            GetCursorPos(&dragStartCursor_);
            RECT rc{};
            GetWindowRect(window_, &rc);
            dragStartWindowPos_ = {rc.left, rc.top};
            return 0;
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (isDragging_) {
            POINT pt{};
            GetCursorPos(&pt);
            const int dx = pt.x - dragStartCursor_.x;
            const int dy = pt.y - dragStartCursor_.y;
            SetWindowPos(window_, nullptr, dragStartWindowPos_.x + dx, dragStartWindowPos_.y + dy, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }
    case WM_LBUTTONUP: {
        if (isDragging_) {
            isDragging_ = false;
            ReleaseCapture();
            RECT rc{};
            GetWindowRect(window_, &rc);
            const RECT bounds = taskbar_.WidgetBounds(config_.width, config_.height);
            config_.taskbarOffsetX = rc.left - bounds.left;
            config_.taskbarOffsetY = rc.top - bounds.top;
            if (controller_ != nullptr) {
                PostMessageW(controller_, kPositionChangedMessage,
                             static_cast<WPARAM>(static_cast<intptr_t>(config_.taskbarOffsetX)),
                             static_cast<LPARAM>(static_cast<intptr_t>(config_.taskbarOffsetY)));
            }
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }
    case WM_CAPTURECHANGED: {
        isDragging_ = false;
        return 0;
    }
    case WM_RBUTTONUP: {
        if (controller_ != nullptr) {
            PostMessageW(controller_, WM_APP + 1, 0, WM_RBUTTONUP);
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }
    case WM_WINDOWPOSCHANGING: {
        auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
        pos->flags |= SWP_NOCOPYBITS;
        return DefWindowProcW(window_, message, wParam, lParam);
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        renderer_.Render(text_, previousLyric_, animProgress_);
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_TIMER: {
        if (wParam == kAnimTimerId) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - animStart_).count();
            animProgress_ = (std::min)(1.0f, static_cast<float>(elapsed) / kAnimDurationMs);
            InvalidateRect(window_, nullptr, FALSE);
            UpdateWindow(window_);
            if (animProgress_ >= 1.0f) {
                KillTimer(window_, kAnimTimerId);
                isAnimating_ = false;
                previousLyric_.clear();
            }
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }
    case WM_DESTROY: {
        KillTimer(window_, kAnimTimerId);
        return 0;
    }
    case WM_SIZE:
        renderer_.Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DPICHANGED: {
        const auto suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window_, config_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        Reposition();
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

}  // namespace taskbar_lyrics::ui
