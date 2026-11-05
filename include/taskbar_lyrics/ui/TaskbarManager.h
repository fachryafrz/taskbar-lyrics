#pragma once

#include <windows.h>

namespace taskbar_lyrics::ui {

class TaskbarManager {
public:
    [[nodiscard]] HWND TaskbarWindow() const;
    [[nodiscard]] RECT WidgetBounds(int width, int height) const;
};

}  // namespace taskbar_lyrics::ui
