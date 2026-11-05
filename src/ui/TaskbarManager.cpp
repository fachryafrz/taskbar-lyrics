#include "taskbar_lyrics/ui/TaskbarManager.h"

#include <shellapi.h>

namespace taskbar_lyrics::ui {

HWND TaskbarManager::TaskbarWindow() const {
    return FindWindowW(L"Shell_TrayWnd", nullptr);
}

RECT TaskbarManager::WidgetBounds(int width, int height) const {
    APPBARDATA appBar{.cbSize = sizeof(APPBARDATA)};
    bool gotAppBar = (SHAppBarMessage(ABM_GETTASKBARPOS, &appBar) != 0);
    RECT taskbarRect = appBar.rc;
    UINT uEdge = appBar.uEdge;

    // Validate appBar rect; fallback to FindWindow if needed
    if (!gotAppBar || (taskbarRect.right <= taskbarRect.left) || (taskbarRect.bottom <= taskbarRect.top)) {
        HWND taskbarWnd = TaskbarWindow();
        if (taskbarWnd != nullptr && GetWindowRect(taskbarWnd, &taskbarRect) &&
            taskbarRect.right > taskbarRect.left && taskbarRect.bottom > taskbarRect.top) {
            HMONITOR monitor = MonitorFromWindow(taskbarWnd, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{.cbSize = sizeof(MONITORINFO)};
            if (GetMonitorInfoW(monitor, &mi)) {
                if (taskbarRect.top <= mi.rcMonitor.top && taskbarRect.bottom < mi.rcMonitor.bottom) {
                    uEdge = ABE_TOP;
                } else if (taskbarRect.bottom >= mi.rcMonitor.bottom && taskbarRect.top > mi.rcMonitor.top) {
                    uEdge = ABE_BOTTOM;
                } else if (taskbarRect.left <= mi.rcMonitor.left && taskbarRect.right < mi.rcMonitor.right) {
                    uEdge = ABE_LEFT;
                } else {
                    uEdge = ABE_RIGHT;
                }
                gotAppBar = true;
            }
        }
    }

    // Fallback to work area vs screen metrics if taskbar rect is still empty
    if (!gotAppBar || (taskbarRect.right <= taskbarRect.left) || (taskbarRect.bottom <= taskbarRect.top)) {
        RECT workArea{};
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) &&
            workArea.right > workArea.left && workArea.bottom > workArea.top) {
            if (workArea.bottom < screenH) {
                taskbarRect = {0, workArea.bottom, screenW, screenH};
                uEdge = ABE_BOTTOM;
            } else if (workArea.top > 0) {
                taskbarRect = {0, 0, screenW, workArea.top};
                uEdge = ABE_TOP;
            } else if (workArea.right < screenW) {
                taskbarRect = {workArea.right, 0, screenW, screenH};
                uEdge = ABE_RIGHT;
            } else if (workArea.left > 0) {
                taskbarRect = {0, 0, workArea.left, screenH};
                uEdge = ABE_LEFT;
            } else {
                taskbarRect = {0, screenH - 48, screenW, screenH};
                uEdge = ABE_BOTTOM;
            }
        } else {
            taskbarRect = {0, screenH - 48, screenW, screenH};
            uEdge = ABE_BOTTOM;
        }
    }

    RECT bounds{};
    const int taskbarWidth = taskbarRect.right - taskbarRect.left;
    const int taskbarHeight = taskbarRect.bottom - taskbarRect.top;
    switch (uEdge) {
    case ABE_BOTTOM:
    case ABE_TOP:
        bounds.left = taskbarRect.left;
        bounds.top = taskbarRect.top + (taskbarHeight - height) / 2;
        break;
    case ABE_LEFT:
    case ABE_RIGHT:
        bounds.left = taskbarRect.left + (taskbarWidth - width) / 2;
        bounds.top = taskbarRect.top;
        break;
    default:
        bounds.left = taskbarRect.left;
        bounds.top = taskbarRect.top + (taskbarHeight - height) / 2;
        break;
    }
    bounds.right = bounds.left + width;
    bounds.bottom = bounds.top + height;
    return bounds;
}

}  // namespace taskbar_lyrics::ui
