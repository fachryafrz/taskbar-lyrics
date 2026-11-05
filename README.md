# Lyrix (Taskbar Lyrics)

Native Windows 11 taskbar-adjacent lyric overlay built with **Tauri v2 (Rust + Web Frontend)**. It is intentionally compact: album art, track title, artist, and the active synchronized lyric.

## Features

- **Windows Media Session (GSMTC):** Real-time playback detection (Spotify, Apple Music, YouTube/browsers) with cover art extraction.
- **Synchronized Lyrics:** Automatic fetching from LRCLIB with in-memory and local disk caching (`%LOCALAPPDATA%\Lyrix\lyrics_cache\`).
- **LRC Sync Engine:** Millisecond-accurate timestamp interpolation with user offset adjustment.
- **Taskbar Docking:** Native AppBar positioning next to the Windows 11 taskbar without taking focus or stealing keyboard input (`WS_EX_NOACTIVATE`, `WS_EX_TOOLWINDOW`, `WS_EX_LAYERED`).
- **System Tray Controls:** Native tray menu to toggle visibility, lock/unlock position, wrap lyrics, toggle always-on-top, and reset position.
- **Minimalist Web UI:** Clean, translucent Segoe UI design with smooth crossfading lyric transitions.

## Project Structure

- `src-tauri/`: Rust backend handling WinRT GSMTC, Win32 window styles, LRC sync engine, and system tray.
- `ui/`: Lightweight frontend (HTML, CSS, JavaScript) embedded directly into the binary.

## Development & Build

### Prerequisites
- [Rust](https://www.rust-lang.org/) (Cargo & rustc)
- Windows 10/11 with WebView2 Runtime (installed by default on Windows)

### Run & Test

Run unit tests:
```powershell
cargo test --manifest-path src-tauri/Cargo.toml
```

Run in development mode:
```powershell
cargo run --manifest-path src-tauri/Cargo.toml
```

Build release binary:
```powershell
cargo build --release --manifest-path src-tauri/Cargo.toml
```
The optimized executable will be located at `src-tauri/target/release/lyrix.exe`.
