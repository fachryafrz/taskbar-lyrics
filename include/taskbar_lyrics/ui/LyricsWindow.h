#pragma once

#include "taskbar_lyrics/core/ConfigManager.h"
#include "taskbar_lyrics/ui/LyricsRenderer.h"
#include "taskbar_lyrics/ui/TaskbarManager.h"

#include <windows.h>
#include <chrono>

namespace taskbar_lyrics::ui {

class LyricsWindow {
public:
    static constexpr UINT_PTR kAnimTimerId = 1002;
    static constexpr float kAnimDurationMs = 220.0f;
    static constexpr UINT kPositionChangedMessage = WM_APP + 3;

    bool Create(HINSTANCE instance, const core::AppConfig& config, HWND controller = nullptr);
    [[nodiscard]] HRESULT CreationError() const;
    void Show(bool visible);
    void UpdateText(DisplayText text);
    void SetTaskbarOffset(int x, int y);
    void SetAlwaysOnTop(bool alwaysOnTop);
    void SetLocked(bool locked);
    void SetWrapLongLyrics(bool wrap);
    void ApplyConfig(const core::AppConfig& newConfig);
    void EnsureTopmost();
    [[nodiscard]] bool IsUnderTaskbar() const;
    void Reposition();
    [[nodiscard]] std::wstring StatusText() const;
    [[nodiscard]] HWND GetHwnd() const noexcept { return window_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND window_{};
    HWND controller_{};
    core::AppConfig config_{};
    DisplayText text_{L"♪", {}};
    std::wstring previousLyric_;
    std::chrono::steady_clock::time_point animStart_{};
    float animProgress_{1.0f};
    bool isAnimating_{false};
    bool isDragging_{false};
    POINT dragStartCursor_{};
    POINT dragStartWindowPos_{};
    LyricsRenderer renderer_;
    TaskbarManager taskbar_;
    HRESULT creationError_{S_OK};
};

}  // namespace taskbar_lyrics::ui
