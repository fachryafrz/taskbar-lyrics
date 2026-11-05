use windows::core::w;
use windows::Win32::Foundation::{HWND, RECT};
use windows::Win32::Graphics::Gdi::{
    GetMonitorInfoW, MonitorFromWindow, MONITORINFO, MONITOR_DEFAULTTOPRIMARY,
};
use windows::Win32::UI::Shell::{
    SHAppBarMessage, APPBARDATA, ABE_BOTTOM, ABE_LEFT, ABE_RIGHT, ABE_TOP, ABM_GETTASKBARPOS,
};
use windows::Win32::UI::WindowsAndMessaging::{
    FindWindowW, GetSystemMetrics, GetWindow, GetWindowRect, SetWindowLongPtrW, SetWindowPos,
    SystemParametersInfoW, GWLP_HWNDPARENT, GW_HWNDPREV, GW_OWNER, HWND_TOPMOST, SM_CXSCREEN,
    SM_CYSCREEN, SPI_GETWORKAREA, SWP_NOACTIVATE, SWP_NOCOPYBITS, SWP_NOMOVE, SWP_NOOWNERZORDER,
    SWP_NOSENDCHANGING, SWP_NOSIZE, SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct WidgetBounds {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub edge: u32,
}

pub struct TaskbarManager;

impl TaskbarManager {
    pub fn taskbar_window() -> Option<HWND> {
        let hwnd = unsafe { FindWindowW(w!("Shell_TrayWnd"), None) }.ok()?;
        if hwnd.0.is_null() {
            None
        } else {
            Some(hwnd)
        }
    }

    pub fn secondary_taskbar_window() -> Option<HWND> {
        let hwnd = unsafe { FindWindowW(w!("Shell_SecondaryTrayWnd"), None) }.ok()?;
        if hwnd.0.is_null() {
            None
        } else {
            Some(hwnd)
        }
    }

    pub fn attach_to_taskbar(window: HWND) {
        if let Some(taskbar) = Self::taskbar_window() {
            unsafe {
                let owner = GetWindow(window, GW_OWNER).unwrap_or(HWND(std::ptr::null_mut()));
                if owner != taskbar {
                    SetWindowLongPtrW(window, GWLP_HWNDPARENT, taskbar.0 as isize);
                }
            }
        }
    }

    pub fn is_under_taskbar(window: HWND) -> bool {
        let taskbar = Self::taskbar_window();
        let secondary = Self::secondary_taskbar_window();
        if taskbar.is_none() && secondary.is_none() {
            return false;
        }

        let mut current = unsafe { GetWindow(window, GW_HWNDPREV) };
        while let Ok(cur_hwnd) = current {
            if cur_hwnd.0.is_null() {
                break;
            }
            if Some(cur_hwnd) == taskbar || Some(cur_hwnd) == secondary {
                return true;
            }
            current = unsafe { GetWindow(cur_hwnd, GW_HWNDPREV) };
        }
        false
    }

    pub fn ensure_topmost(window: HWND, always_on_top: bool) {
        if !always_on_top {
            return;
        }
        // If the window is below the taskbar in the z-order, bring it above
        if Self::is_under_taskbar(window) {
            unsafe {
                let _ = SetWindowPos(
                    window,
                    HWND_TOPMOST,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE
                        | SWP_NOSIZE
                        | SWP_NOACTIVATE
                        | SWP_NOOWNERZORDER
                        | SWP_NOSENDCHANGING
                        | SWP_NOCOPYBITS,
                );
            }
        }
    }

    pub fn widget_bounds(width: i32, height: i32) -> WidgetBounds {
        let mut app_bar = APPBARDATA {
            cbSize: std::mem::size_of::<APPBARDATA>() as u32,
            ..Default::default()
        };

        let mut got_app_bar = unsafe { SHAppBarMessage(ABM_GETTASKBARPOS, &mut app_bar) != 0 };
        let mut taskbar_rect = app_bar.rc;
        let mut edge = app_bar.uEdge;

        // Fallback to FindWindow if needed
        if !got_app_bar
            || (taskbar_rect.right <= taskbar_rect.left)
            || (taskbar_rect.bottom <= taskbar_rect.top)
        {
            if let Some(taskbar_hwnd) = Self::taskbar_window() {
                let mut rect = RECT::default();
                if unsafe { GetWindowRect(taskbar_hwnd, &mut rect).is_ok() }
                    && rect.right > rect.left
                    && rect.bottom > rect.top
                {
                    taskbar_rect = rect;
                    let monitor = unsafe {
                        MonitorFromWindow(taskbar_hwnd, MONITOR_DEFAULTTOPRIMARY)
                    };
                    let mut mi = MONITORINFO {
                        cbSize: std::mem::size_of::<MONITORINFO>() as u32,
                        ..Default::default()
                    };
                    if unsafe { GetMonitorInfoW(monitor, &mut mi).as_bool() } {
                        if taskbar_rect.top <= mi.rcMonitor.top
                            && taskbar_rect.bottom < mi.rcMonitor.bottom
                        {
                            edge = ABE_TOP;
                        } else if taskbar_rect.bottom >= mi.rcMonitor.bottom
                            && taskbar_rect.top > mi.rcMonitor.top
                        {
                            edge = ABE_BOTTOM;
                        } else if taskbar_rect.left <= mi.rcMonitor.left
                            && taskbar_rect.right < mi.rcMonitor.right
                        {
                            edge = ABE_LEFT;
                        } else {
                            edge = ABE_RIGHT;
                        }
                        got_app_bar = true;
                    }
                }
            }
        }

        // Final fallback: Work area vs screen metrics
        if !got_app_bar
            || (taskbar_rect.right <= taskbar_rect.left)
            || (taskbar_rect.bottom <= taskbar_rect.top)
        {
            let mut work_area = RECT::default();
            let screen_w = unsafe { GetSystemMetrics(SM_CXSCREEN) };
            let screen_h = unsafe { GetSystemMetrics(SM_CYSCREEN) };
            let spi_ok = unsafe {
                SystemParametersInfoW(
                    SPI_GETWORKAREA,
                    0,
                    Some(&mut work_area as *mut _ as *mut _),
                    SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS(0),
                )
            };

            if spi_ok.is_ok()
                && work_area.right > work_area.left
                && work_area.bottom > work_area.top
            {
                if work_area.bottom < screen_h {
                    taskbar_rect = RECT {
                        left: 0,
                        top: work_area.bottom,
                        right: screen_w,
                        bottom: screen_h,
                    };
                    edge = ABE_BOTTOM;
                } else if work_area.top > 0 {
                    taskbar_rect = RECT {
                        left: 0,
                        top: 0,
                        right: screen_w,
                        bottom: work_area.top,
                    };
                    edge = ABE_TOP;
                } else if work_area.right < screen_w {
                    taskbar_rect = RECT {
                        left: work_area.right,
                        top: 0,
                        right: screen_w,
                        bottom: screen_h,
                    };
                    edge = ABE_RIGHT;
                } else if work_area.left > 0 {
                    taskbar_rect = RECT {
                        left: 0,
                        top: 0,
                        right: work_area.left,
                        bottom: screen_h,
                    };
                    edge = ABE_LEFT;
                } else {
                    taskbar_rect = RECT {
                        left: 0,
                        top: screen_h - 48,
                        right: screen_w,
                        bottom: screen_h,
                    };
                    edge = ABE_BOTTOM;
                }
            }
        }

        let taskbar_h = taskbar_rect.bottom - taskbar_rect.top;
        let (x, y) = match edge {
            ABE_LEFT => (taskbar_rect.left + 4, taskbar_rect.top + 100),
            ABE_RIGHT => (taskbar_rect.right - width - 4, taskbar_rect.top + 100),
            ABE_TOP => (taskbar_rect.left + 180, taskbar_rect.top + ((taskbar_h - height) / 2)),
            _ => (taskbar_rect.left + 180, taskbar_rect.top + ((taskbar_h - height) / 2)),
        };

        WidgetBounds {
            x,
            y,
            width,
            height,
            edge,
        }
    }
}
