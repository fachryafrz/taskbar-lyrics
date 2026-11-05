# Taskbar Lyrics

Native Windows 11 C++20 taskbar-adjacent lyric overlay. It is intentionally compact: album-art space, track title, artist, and the active lyric.

## Current foundation

- Win32 controller, system-tray Show/Hide and Exit commands, and clean message-loop shutdown.
- Direct2D/DirectWrite overlay positioned next to the taskbar without taking keyboard focus or mouse input.
- Local configuration in `%LOCALAPPDATA%\TaskbarLyrics\config.json`.
- Windows media-session detection for track title, artist, and playback position.
- LRCLIB retrieval on a worker thread, with an in-memory cache for tracks seen during this run.
- Pure LRC parsing and playback-clock synchronization, each covered by CTest tests.

The square at the left is still a placeholder for album art. Lyrics are shown only when LRCLIB returns synchronized lyrics for the detected title and artist. The overlay reports missing lyrics, instrumental tracks, and network failures in its lyric line. Persistent disk cache and album-art rendering remain future work.

## Build

Open a **Visual Studio 2022 Developer PowerShell** with the Windows 11 SDK installed:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

The executable is generated under `out/build/windows-x64/Debug/`.
