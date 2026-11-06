#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod core;
mod lyrics;
mod media;
mod ui;

use crate::core::{AppConfig, ConfigManager, SettingsUpdate};
use crate::media::{MediaSessionWorker, MediaUpdatePayload};
use crate::ui::TaskbarManager;
use std::sync::atomic::{AtomicBool, AtomicIsize, Ordering};
use std::sync::{Arc, Mutex};
use tauri::menu::{CheckMenuItemBuilder, ContextMenu, MenuBuilder, MenuItemBuilder};
use tauri::tray::TrayIconBuilder;
use tauri::{
    Emitter, LogicalSize, Manager, PhysicalPosition, WebviewUrl, WebviewWindowBuilder,
};

#[cfg(target_os = "windows")]
static OVERLAY_HWND: AtomicIsize = AtomicIsize::new(0);
#[cfg(target_os = "windows")]
static ALWAYS_ON_TOP: AtomicBool = AtomicBool::new(true);

struct TrayMenuChecks {
    wrap: tauri::menu::CheckMenuItem<tauri::Wry>,
    always_on_top: tauri::menu::CheckMenuItem<tauri::Wry>,
}

#[cfg(target_os = "windows")]
unsafe extern "system" fn winevent_proc(
    _hook: windows::Win32::UI::Accessibility::HWINEVENTHOOK,
    _event: u32,
    hwnd: windows::Win32::Foundation::HWND,
    _id_object: i32,
    _id_child: i32,
    _event_thread: u32,
    _event_time: u32,
) {
    let overlay_raw = OVERLAY_HWND.load(Ordering::Relaxed);
    if overlay_raw != 0 && ALWAYS_ON_TOP.load(Ordering::Relaxed) {
        let overlay = windows::Win32::Foundation::HWND(overlay_raw as *mut _);
        if hwnd != overlay {
            TaskbarManager::ensure_topmost(overlay, true);
        }
    }
}

#[tauri::command]
fn get_config(config: tauri::State<Arc<Mutex<AppConfig>>>) -> AppConfig {
    config.lock().unwrap().clone()
}

fn open_settings_window(app: &tauri::AppHandle) {
    let app = app.clone();
    std::thread::spawn(move || {
        let creation_lock = app.state::<Arc<Mutex<()>>>();
        let Ok(_guard) = creation_lock.lock() else {
            return;
        };

        let window = match app.get_webview_window("settings") {
            Some(window) => window,
            None => match WebviewWindowBuilder::new(
                &app,
                "settings",
                WebviewUrl::App("settings.html".into()),
            )
            .title("Lyrix Settings")
            .inner_size(520.0, 520.0)
            .decorations(true)
            .transparent(false)
            .always_on_top(false)
            .skip_taskbar(false)
            .resizable(true)
            .shadow(true)
            .additional_browser_args(
                "--disable-features=msWebOOUI,msPdfOOUI,msSmartScreenProtection --disk-cache-size=33554432 --media-cache-size=16777216",
            )
            .visible(false)
            .build()
            {
                Ok(window) => window,
                Err(error) => {
                    eprintln!("Failed to create Settings window: {error}");
                    return;
                }
            },
        };

        let _ = window.show();
        let _ = window.unminimize();
        let _ = window.set_focus();
        let _ = window.emit("settings-opened", ());
    });
}

#[tauri::command]
async fn get_current_media() -> MediaUpdatePayload {
    tokio::task::spawn_blocking(|| {
        crate::media::gsmtc::get_current_track().unwrap_or_default()
    })
    .await
    .unwrap_or_default()
}

#[tauri::command]
fn move_window_by(delta_x: i32, delta_y: i32, app: tauri::AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        if let Ok(pos) = window.outer_position() {
            let new_x = pos.x + delta_x;
            let new_y = pos.y + delta_y;
            let _ = window.set_position(PhysicalPosition::new(new_x, new_y));
        }
    }
}

#[tauri::command]
fn save_window_position(
    app: tauri::AppHandle,
    config_state: tauri::State<Arc<Mutex<AppConfig>>>,
) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("main") {
        if let Ok(pos) = window.outer_position() {
            let mut cfg = config_state.lock().unwrap();
            let bounds = TaskbarManager::widget_bounds(cfg.width, cfg.height);
            cfg.taskbar_offset_x = pos.x - bounds.x;
            cfg.taskbar_offset_y = pos.y - bounds.y;
            ConfigManager::save(&cfg);
        }
    }
    Ok(())
}

#[tauri::command]
fn show_context_menu(
    app: tauri::AppHandle,
    config_state: tauri::State<Arc<Mutex<AppConfig>>>,
) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("main") {
        let cfg = config_state.lock().unwrap();
        let toggle_item = CheckMenuItemBuilder::with_id("toggle", "Show Taskbar Window")
            .checked(cfg.visible)
            .build(&app)
            .map_err(|e| e.to_string())?;
        let lock_item = CheckMenuItemBuilder::with_id("lock", "Lock Position")
            .checked(cfg.locked)
            .build(&app)
            .map_err(|e| e.to_string())?;
        let wrap_item = CheckMenuItemBuilder::with_id("wrap", "Wrap Long Lyrics")
            .checked(cfg.wrap_long_lyrics)
            .build(&app)
            .map_err(|e| e.to_string())?;
        let aot_item = CheckMenuItemBuilder::with_id("always_on_top", "Always on top")
            .checked(cfg.always_on_top)
            .build(&app)
            .map_err(|e| e.to_string())?;
        let settings_item = MenuItemBuilder::with_id("settings", "Settings")
            .build(&app)
            .map_err(|e| e.to_string())?;
        let reset_item = MenuItemBuilder::with_id("reset", "Reset Position")
            .build(&app)
            .map_err(|e| e.to_string())?;
        let clear_cache_item = MenuItemBuilder::with_id("clear_cache", "Clear Cache")
            .build(&app)
            .map_err(|e| e.to_string())?;
        let exit_item = MenuItemBuilder::with_id("exit", "Exit")
            .build(&app)
            .map_err(|e| e.to_string())?;

        let context_menu = MenuBuilder::new(&app)
            .item(&toggle_item)
            .item(&lock_item)
            .item(&wrap_item)
            .item(&aot_item)
            .separator()
            .item(&settings_item)
            .separator()
            .item(&reset_item)
            .item(&clear_cache_item)
            .separator()
            .item(&exit_item)
            .build()
            .map_err(|e| e.to_string())?;

        context_menu.popup(window.as_ref().window().clone()).map_err(|e| e.to_string())?;
    }
    Ok(())
}

#[tauri::command]
fn clear_cache(app: tauri::AppHandle) -> Result<(), String> {
    if let Some(base) = dirs::data_local_dir() {
        // 1. Clear lyrics cache
        let cache_dir = base.join("Lyrix").join("lyrics_cache");
        if cache_dir.exists() {
            let _ = std::fs::remove_dir_all(&cache_dir);
            let _ = std::fs::create_dir_all(&cache_dir);
        }

        // 2. Clear WebView2 browser/EBWebView cache
        let webview_cache = base.join("com.lyrix.taskbar").join("EBWebView");
        for sub in &["Default/Cache", "Default/Code Cache", "Default/GPUCache"] {
            let path = webview_cache.join(sub);
            if path.exists() {
                let _ = std::fs::remove_dir_all(&path);
            }
        }
    }
    let _ = app.emit("clear-cache", ());
    Ok(())
}

#[tauri::command]
fn save_config(
    new_config: AppConfig,
    config_state: tauri::State<Arc<Mutex<AppConfig>>>,
    app: tauri::AppHandle,
) -> Result<(), String> {
    ConfigManager::save(&new_config);
    *config_state.lock().unwrap() = new_config.clone();

    #[cfg(target_os = "windows")]
    ALWAYS_ON_TOP.store(new_config.always_on_top, Ordering::Relaxed);

    // Update window positioning / visibility
    if let Some(window) = app.get_webview_window("main") {
        if new_config.visible {
            let _ = window.show();
        } else {
            let _ = window.hide();
        }
        let _ = window.set_always_on_top(new_config.always_on_top);

        let bounds = TaskbarManager::widget_bounds(new_config.width, new_config.height);
        let x = bounds.x + new_config.taskbar_offset_x;
        let y = bounds.y + new_config.taskbar_offset_y;
        let _ = window.set_position(PhysicalPosition::new(x, y));
        let _ = window.set_size(LogicalSize::new(new_config.width as f64, new_config.height as f64));

        #[cfg(target_os = "windows")]
        if let Ok(hwnd) = window.hwnd() {
            let hwnd = windows::Win32::Foundation::HWND(hwnd.0);
            TaskbarManager::attach_to_taskbar(hwnd);
            TaskbarManager::ensure_topmost(hwnd, new_config.always_on_top);
        }
    }

    Ok(())
}

#[tauri::command]
fn apply_settings(
    update: SettingsUpdate,
    config_state: tauri::State<Arc<Mutex<AppConfig>>>,
    tray_menu_checks: tauri::State<Arc<Mutex<Option<TrayMenuChecks>>>>,
    app: tauri::AppHandle,
) -> Result<AppConfig, String> {
    let mut current = config_state
        .lock()
        .map_err(|_| "Configuration is unavailable.".to_string())?;
    ConfigManager::apply_settings_update(&mut current, &update)?;
    let updated = current.clone();
    drop(current);

    #[cfg(target_os = "windows")]
    ALWAYS_ON_TOP.store(updated.always_on_top, Ordering::Relaxed);

    if let Some(window) = app.get_webview_window("main") {
        let bounds = TaskbarManager::widget_bounds(updated.width, updated.height);
        let x = bounds.x + updated.taskbar_offset_x;
        let y = bounds.y + updated.taskbar_offset_y;
        let _ = window.set_position(PhysicalPosition::new(x, y));
        let _ = window.set_size(LogicalSize::new(
            updated.width as f64,
            updated.height as f64,
        ));
        let _ = window.set_always_on_top(updated.always_on_top);

        #[cfg(target_os = "windows")]
        if let Ok(hwnd) = window.hwnd() {
            let hwnd = windows::Win32::Foundation::HWND(hwnd.0);
            TaskbarManager::attach_to_taskbar(hwnd);
            TaskbarManager::ensure_topmost(hwnd, updated.always_on_top);
        }
    }

    sync_tray_menu_checks(&tray_menu_checks, &updated);
    let _ = app.emit("config-updated", updated.clone());
    Ok(updated)
}

fn apply_win32_window_styles(window: &tauri::WebviewWindow, _locked: bool) {
    #[cfg(target_os = "windows")]
    {
        use windows::Win32::Foundation::HWND;
        use windows::Win32::UI::WindowsAndMessaging::{
            GetWindowLongPtrW, SetWindowLongPtrW, SetWindowPos, GWL_EXSTYLE, HWND_TOPMOST,
            SWP_FRAMECHANGED, SWP_NOACTIVATE, SWP_NOCOPYBITS, SWP_NOMOVE, SWP_NOOWNERZORDER,
            SWP_NOSENDCHANGING, SWP_NOSIZE, WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW,
            WS_EX_TOPMOST,
        };

        if let Ok(hwnd) = window.hwnd() {
            let hwnd = HWND(hwnd.0);
            unsafe {
                let cur_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                let new_style = cur_style
                    | (WS_EX_TOOLWINDOW.0 as isize)
                    | (WS_EX_NOACTIVATE.0 as isize)
                    | (WS_EX_LAYERED.0 as isize)
                    | (WS_EX_TOPMOST.0 as isize);

                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, new_style);

                // Attach to Shell_TrayWnd as parent/owner so clicking taskbar keeps overlay on top
                TaskbarManager::attach_to_taskbar(hwnd);

                // Refresh window frame & ensure topmost
                let _ = SetWindowPos(
                    hwnd,
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
                        | SWP_NOCOPYBITS
                        | SWP_FRAMECHANGED,
                );
            }
        }
    }
}

fn build_tray_menu(
    app: &tauri::AppHandle,
    config: &AppConfig,
) -> tauri::Result<(tauri::menu::Menu<tauri::Wry>, TrayMenuChecks)> {
    let toggle_item = CheckMenuItemBuilder::with_id("toggle", "Show Taskbar Window")
        .checked(config.visible)
        .build(app)?;
    let lock_item = CheckMenuItemBuilder::with_id("lock", "Lock Position")
        .checked(config.locked)
        .build(app)?;
    let wrap_item = CheckMenuItemBuilder::with_id("wrap", "Wrap Long Lyrics")
        .checked(config.wrap_long_lyrics)
        .build(app)?;
    let aot_item = CheckMenuItemBuilder::with_id("always_on_top", "Always on top")
        .checked(config.always_on_top)
        .build(app)?;
    let settings_item = MenuItemBuilder::with_id("settings", "Settings").build(app)?;
    let reset_item = MenuItemBuilder::with_id("reset", "Reset Position").build(app)?;
    let clear_cache_item = MenuItemBuilder::with_id("clear_cache", "Clear Cache").build(app)?;
    let exit_item = MenuItemBuilder::with_id("exit", "Exit").build(app)?;

    let checks = TrayMenuChecks {
        wrap: wrap_item.clone(),
        always_on_top: aot_item.clone(),
    };
    let menu = MenuBuilder::new(app)
        .item(&toggle_item)
        .item(&lock_item)
        .item(&wrap_item)
        .item(&aot_item)
        .separator()
        .item(&settings_item)
        .separator()
        .item(&reset_item)
        .item(&clear_cache_item)
        .separator()
        .item(&exit_item)
        .build()?;

    Ok((menu, checks))
}

fn sync_tray_menu_checks(
    tray_menu_checks: &Arc<Mutex<Option<TrayMenuChecks>>>,
    config: &AppConfig,
) {
    if let Ok(checks) = tray_menu_checks.lock() {
        if let Some(checks) = checks.as_ref() {
            let _ = checks.wrap.set_checked(config.wrap_long_lyrics);
            let _ = checks.always_on_top.set_checked(config.always_on_top);
        }
    }
}

fn handle_menu_action(
    app: &tauri::AppHandle,
    id: &str,
    config_state: &Arc<Mutex<AppConfig>>,
    tray_menu_checks: &Arc<Mutex<Option<TrayMenuChecks>>>,
) {
    if id == "settings" {
        open_settings_window(app);
        return;
    }

    let mut cfg = config_state.lock().unwrap();
    let window = app.get_webview_window("main");

    match id {
        "toggle" => {
            cfg.visible = !cfg.visible;
            ConfigManager::save(&cfg);
            if let Some(w) = window {
                if cfg.visible {
                    let _ = w.show();
                } else {
                    let _ = w.hide();
                }
            }
        }
        "lock" => {
            cfg.locked = !cfg.locked;
            ConfigManager::save(&cfg);
            if let Some(w) = window {
                apply_win32_window_styles(&w, cfg.locked);
            }
            let _ = app.emit("config-updated", cfg.clone());
        }
        "wrap" => {
            cfg.wrap_long_lyrics = !cfg.wrap_long_lyrics;
            ConfigManager::save(&cfg);
            let _ = app.emit("config-updated", cfg.clone());
        }
        "always_on_top" => {
            cfg.always_on_top = !cfg.always_on_top;
            ConfigManager::save(&cfg);
            #[cfg(target_os = "windows")]
            ALWAYS_ON_TOP.store(cfg.always_on_top, Ordering::Relaxed);
            if let Some(w) = window {
                let _ = w.set_always_on_top(cfg.always_on_top);
                #[cfg(target_os = "windows")]
                if let Ok(hwnd) = w.hwnd() {
                    let hwnd = windows::Win32::Foundation::HWND(hwnd.0);
                    TaskbarManager::ensure_topmost(hwnd, cfg.always_on_top);
                }
            }
            let _ = app.emit("config-updated", cfg.clone());
        }
        "reset" => {
            cfg.taskbar_offset_x = 0;
            cfg.taskbar_offset_y = 0;
            ConfigManager::save(&cfg);
            if let Some(w) = window {
                let bounds = TaskbarManager::widget_bounds(cfg.width, cfg.height);
                let _ = w.set_position(PhysicalPosition::new(bounds.x, bounds.y));
                #[cfg(target_os = "windows")]
                if let Ok(hwnd) = w.hwnd() {
                    let hwnd = windows::Win32::Foundation::HWND(hwnd.0);
                    TaskbarManager::attach_to_taskbar(hwnd);
                    TaskbarManager::ensure_topmost(hwnd, cfg.always_on_top);
                }
            }
        }
        "clear_cache" => {
            if let Some(base) = dirs::data_local_dir() {
                // 1. Clear lyrics cache
                let cache_dir = base.join("Lyrix").join("lyrics_cache");
                if cache_dir.exists() {
                    let _ = std::fs::remove_dir_all(&cache_dir);
                    let _ = std::fs::create_dir_all(&cache_dir);
                }

                // 2. Clear WebView2 browser/EBWebView cache
                let webview_cache = base.join("com.lyrix.taskbar").join("EBWebView");
                for sub in &["Default/Cache", "Default/Code Cache", "Default/GPUCache"] {
                    let path = webview_cache.join(sub);
                    if path.exists() {
                        let _ = std::fs::remove_dir_all(&path);
                    }
                }
            }
            let _ = app.emit("clear-cache", ());
        }
        "exit" => {
            std::process::exit(0);
        }
        _ => {}
    }

    let updated = cfg.clone();
    drop(cfg);
    sync_tray_menu_checks(tray_menu_checks, &updated);
}

fn main() {
    let config = ConfigManager::load();
    let config_state = Arc::new(Mutex::new(config.clone()));
    let tray_menu_checks: Arc<Mutex<Option<TrayMenuChecks>>> = Arc::new(Mutex::new(None));
    let settings_window_creation = Arc::new(Mutex::new(()));
    let media_worker = Arc::new(MediaSessionWorker::new());

    let config_for_setup = config.clone();
    let config_state_for_tray = Arc::clone(&config_state);
    let tray_menu_checks_for_setup = Arc::clone(&tray_menu_checks);
    let worker_for_run = Arc::clone(&media_worker);

    tauri::Builder::default()
        .manage(config_state)
        .manage(Arc::clone(&tray_menu_checks))
        .manage(settings_window_creation)
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![
            get_config,
            get_current_media,
            save_config,
            apply_settings,
            move_window_by,
            save_window_position,
            show_context_menu,
            clear_cache
        ])
        .on_window_event(|window, event| {
            if window.label() == "settings" {
                if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                    api.prevent_close();
                    let _ = window.hide();
                }
            }
        })
        .setup(move |app| {
            let window = app.get_webview_window("main").expect("main window missing");

            // Compute taskbar dock position
            let bounds = TaskbarManager::widget_bounds(config_for_setup.width, config_for_setup.height);
            let x = bounds.x + config_for_setup.taskbar_offset_x;
            let y = bounds.y + config_for_setup.taskbar_offset_y;

            let _ = window.set_position(PhysicalPosition::new(x, y));
            let _ = window.set_size(LogicalSize::new(
                config_for_setup.width as f64,
                config_for_setup.height as f64,
            ));
            let _ = window.set_always_on_top(config_for_setup.always_on_top);

            apply_win32_window_styles(&window, config_for_setup.locked);

            #[cfg(target_os = "windows")]
            if let Ok(hwnd) = window.hwnd() {
                OVERLAY_HWND.store(hwnd.0 as isize, Ordering::Relaxed);
                ALWAYS_ON_TOP.store(config_for_setup.always_on_top, Ordering::Relaxed);

                unsafe {
                    use windows::Win32::UI::Accessibility::SetWinEventHook;
                    use windows::Win32::UI::WindowsAndMessaging::EVENT_SYSTEM_FOREGROUND;
                    const WINEVENT_OUTOFCONTEXT: u32 = 0x0000;
                    const WINEVENT_SKIPOWNPROCESS: u32 = 0x0002;

                    let _ = SetWinEventHook(
                        EVENT_SYSTEM_FOREGROUND,
                        EVENT_SYSTEM_FOREGROUND,
                        None,
                        Some(winevent_proc),
                        0,
                        0,
                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS,
                    );
                }

                // Background watchdog thread for persistent taskbar z-order
                std::thread::spawn(move || {
                    loop {
                        std::thread::sleep(std::time::Duration::from_millis(300));
                        let raw = OVERLAY_HWND.load(Ordering::Relaxed);
                        if raw != 0 && ALWAYS_ON_TOP.load(Ordering::Relaxed) {
                            let overlay = windows::Win32::Foundation::HWND(raw as *mut _);
                            TaskbarManager::ensure_topmost(overlay, true);
                        }
                    }
                });
            }

            if config_for_setup.visible {
                let _ = window.show();
            }

            // Setup System Tray
            let (tray_menu, check_items) = build_tray_menu(app.handle(), &config_for_setup)?;
            if let Ok(mut checks) = tray_menu_checks_for_setup.lock() {
                *checks = Some(check_items);
            }

            let config_state_for_menu = Arc::clone(&config_state_for_tray);
            let tray_checks_for_menu = Arc::clone(&tray_menu_checks_for_setup);
            app.on_menu_event(move |app_handle, event| {
                handle_menu_action(
                    app_handle,
                    event.id().as_ref(),
                    &config_state_for_menu,
                    &tray_checks_for_menu,
                );
            });

            let mut tray_builder = TrayIconBuilder::new()
                .menu(&tray_menu)
                .tooltip("Lyrix - Taskbar Lyrics");

            if let Some(icon) = app.default_window_icon() {
                tray_builder = tray_builder.icon(icon.clone());
            }
            let _tray = tray_builder.build(app)?;

            // Start Media Session Worker
            let app_handle = app.handle().clone();
            worker_for_run.start(move |payload| {
                let _ = app_handle.emit("media-update", payload);
                #[cfg(target_os = "windows")]
                {
                    let raw = OVERLAY_HWND.load(Ordering::Relaxed);
                    if raw != 0 && ALWAYS_ON_TOP.load(Ordering::Relaxed) {
                        let overlay = windows::Win32::Foundation::HWND(raw as *mut _);
                        TaskbarManager::ensure_topmost(overlay, true);
                    }
                }
            });

            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
