# Implementation Plan — Windows Taskbar Lyrics

C++20

Win32 API

DirectWrite

Windows 11

## 1. Project overview

Membangun aplikasi Windows native yang menampilkan synchronized song lyrics secara real-time langsung di area taskbar, terinspirasi dari TrafficMonitor.

Aplikasi dirancang agar ringan, tidak membutuhkan browser atau WebView, berjalan di background, dan otomatis mendeteksi musik dari aplikasi yang mendukung Windows Media Session.

### Target MVP

- Native Windows application berbasis C++20.
- Menampilkan satu baris lirik aktif di taskbar.
- Mendeteksi Spotify, browser, dan media player melalui Windows GSMTC.
- Sinkronisasi lirik dengan posisi playback.
- Mengambil lirik dari LRCLIB dan menyimpannya secara lokal.
- System tray dengan opsi Show/Hide, Settings, dan Exit.
- Bisa berjalan otomatis saat Windows startup.
- Tidak mengganggu tombol Start, aplikasi yang sedang dibuka, atau system tray.

### Non-goals untuk MVP

Belum mencakup karaoke word-by-word, terjemahan lirik, integrasi Spotify OAuth, multi-line floating lyrics, atau embedding langsung ke proses Explorer.

## 2. Technology stack

| Layer         | Technology                        |
| ------------- | --------------------------------- |
| Language      | C++20                             |
| Platform      | Windows 11 x64                    |
| Build         | CMake + MSVC                      |
| Native window | Win32 API                         |
| Graphics      | Direct2D + DirectWrite            |
| Media session | C++/WinRT + GSMTC                 |
| HTTP          | WinHTTP                           |
| JSON          | nlohmann/json                     |
| Lyrics source | LRCLIB                            |
| Configuration | JSON                              |
| Lyrics cache  | Local filesystem                  |
| Testing       | GoogleTest atau Catch2            |
| Packaging     | Portable EXE + optional installer |

Arsitektur: single-process, event-driven, modular monolith. Tidak perlu server, database eksternal, atau framework frontend.

DirectWrite digunakan untuk typography, sedangkan Direct2D menangani rendering teks dan grafis. Keduanya memang dirancang untuk bekerja bersama dalam aplikasi Win32.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://learn.microsoft.com\&sz=32)

Microsoft Learn+1



## 3. High-level architecture

Media Applications

Spotify · YouTube Music · Browser · Local Player

Media Session Manager

Windows GSMTC / C++/WinRT

Track · Artist · Duration · Position · Playback State

Lyrics Service

LRCLIB + Cache + LRC Parser

Sync Engine

Playback clock + lyric matching

Taskbar Widget

Win32 Window + Direct2D + DirectWrite

Configuration Manager + System Tray

## 4. Implementation milestones

Phase 1 — Native application foundation

P0

Membuat CMake project, Win32 application entry point, message loop, hidden controller window, logging, configuration, dan system tray.

Acceptance: aplikasi dapat dibuild sebagai EXE, tampil di tray, serta ditutup sepenuhnya tanpa orphan process.

Phase 2 — Media session integration

P0

Implementasi GSMTC untuk membaca sesi media dan mendengarkan perubahan metadata, playback state, serta timeline.

Acceptance: aplikasi mendeteksi lagu dan pergerakan playback pada setidaknya Spotify dan satu browser, dengan penanganan saat media session menghilang.

Phase 3 — Lyrics retrieval & parsing

P0

Integrasi LRCLIB, parsing LRC timestamp, pencocokan track, serta cache lokal.

Acceptance: lirik dapat diambil, di-parse, dan digunakan kembali dari cache. Kondisi 404, instrumental, dan tidak ada synced lyrics ditangani.

Phase 4 — Lyrics synchronization engine

P0

Membuat playback clock, sinkronisasi timestamp, dan state management.

Acceptance: lirik bergerak sesuai playback, berhenti ketika pause, berpindah saat seek, dan reset ketika berganti lagu.

Phase 5 — Native lyrics renderer

P0

Membuat window tanpa border dan renderer DirectWrite untuk satu baris lirik, dengan clipping, ellipsis, dan DPI awareness.

Acceptance: teks tampil tajam pada scaling Windows 100–150%, panjang teks tidak keluar dari window, dan animasi tidak menyebabkan flicker.

Phase 6 — Taskbar integration

P0 · High risk

Membuat taskbar-aware overlay yang mengikuti lokasi taskbar, menghindari area interaktif, dan memperbarui posisi saat display berubah.

Acceptance: widget tetap berada pada area yang ditetapkan tanpa menutup kontrol penting taskbar dan dapat dipulihkan setelah Explorer restart.

Phase 7 — Settings & persistence

P1

Settings sederhana untuk ukuran teks, warna, lebar widget, lyrics offset, auto-start, dan visibilitas.

Acceptance: semua pengaturan dapat disimpan dan dipulihkan setelah restart.

Phase 8 — Hardening & release

P1

Error handling, testing resource usage, graceful shutdown, packaging, dan dokumentasi.

Acceptance: aplikasi dapat digunakan secara normal dalam sesi panjang tanpa pertumbuhan memori yang terus-menerus atau kehilangan fungsi setelah media player restart.

## 5. Proposed project structure

```
taskbar-lyrics/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── assets/
│   ├── app.ico
│   └── resources.rc
├── include/
│   └── taskbar_lyrics/
│       ├── app/
│       ├── media/
│       ├── lyrics/
│       ├── sync/
│       ├── ui/
│       └── core/
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── Application.cpp
│   │   └── TrayManager.cpp
│   ├── media/
│   │   └── MediaSessionManager.cpp
│   ├── lyrics/
│   │   ├── LyricsProvider.cpp
│   │   ├── LrcParser.cpp
│   │   └── LyricsCache.cpp
│   ├── sync/
│   │   └── LyricsSyncEngine.cpp
│   ├── ui/
│   │   ├── LyricsWindow.cpp
│   │   ├── LyricsRenderer.cpp
│   │   └── TaskbarManager.cpp
│   └── core/
│       ├── ConfigManager.cpp
│       ├── HttpClient.cpp
│       └── Logger.cpp
├── tests/
│   ├── LrcParserTests.cpp
│   ├── LyricsSyncTests.cpp
│   └── TrackMatchingTests.cpp
└── .github/
    └── workflows/
        └── build.yml
```

Pisahkan media detection, lyrics retrieval, synchronization, dan rendering supaya masing-masing bisa diuji secara independen.

## 6. Critical implementation details

### A. Media detection

Gunakan event dari GSMTC:

- `MediaPropertiesChanged`
- `PlaybackInfoChanged`
- `TimelinePropertiesChanged`
- `SessionsChanged` pada session manager

GSMTC menyediakan metadata, playback state, dan informasi timeline, tetapi update posisi tidak selalu datang setiap saat.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://learn.microsoft.com\&sz=32)

Microsoft Learn



Karena itu, implementasikan interpolation clock berbasis `std::chrono::steady_clock` dan lakukan koreksi dari timeline terbaru. Ketika seek, pause, atau track berubah, clock harus di-reset atau disesuaikan.

### B. Lyrics synchronization

Contoh format LRC:

```
[00:12.40]First lyric line
[00:16.80]Second lyric line
[00:21.25]Third lyric line
```

Algoritma:

```
effectivePosition = playbackPosition
                  + elapsedSinceSnapshot
                  + userOffset;

activeLine = findLastTimestampLessOrEqual(
    effectivePosition
);
```

Penambahan `elapsedSinceSnapshot` hanya berlaku ketika status playing, dan perlu memperhitungkan playback rate.

Gunakan binary search (`std::upper_bound`) untuk memilih baris yang aktif. Tidak perlu memindai seluruh array pada setiap update.

### C. Lyrics provider

Gunakan endpoint `GET /api/get` dengan title, artist, dan jika tersedia album serta duration. Simpan hasil menggunakan cache key berbasis identitas track, bukan title saja.

LRCLIB menyediakan `syncedLyrics`, tetapi tidak semua lagu memilikinya. API juga mensyaratkan identifikasi client melalui header dan penghormatan terhadap rate limit.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://lrclib.net\&sz=32)

LRCLIB



HTTP request harus berjalan asynchronous di worker thread, dengan pembatalan atau pengabaian hasil request lama saat track berganti.

### D. Taskbar window

Untuk MVP, gunakan:

- `WS_POPUP` untuk window tanpa border.
- `WS_EX_TOOLWINDOW` supaya tidak muncul di Alt+Tab.
- `WS_EX_NOACTIVATE` agar widget tidak mengambil fokus.
- Transparency dan hit-testing sesuai kebutuhan.
- `SHAppBarMessage(ABM_GETTASKBARPOS)` untuk mencari posisi taskbar utama.
- `SetWinEventHook` atau window notifications untuk perubahan lingkungan desktop.
- `TaskbarCreated` registered message untuk menangani Explorer restart.

Penting: overlay tidak otomatis membuat taskbar menyediakan ruang kosong. Jadi, MVP harus punya positioning yang dapat dikonfigurasi dan tidak mengasumsikan bisa menggeser ikon atau system tray. Embedded taskbar seperti TrafficMonitor bisa menjadi eksperimen setelah MVP stabil.

## 7. Testing & performance targets

Target berikut adalah engineering goals, bukan jaminan hasil sebelum profiling.

| Metric                      | Target                                                     |
| --------------------------- | ---------------------------------------------------------- |
| Idle CPU                    | Di bawah 1% rata-rata                                      |
| RAM usage                   | Di bawah 30 MB jika memungkinkan                           |
| Lyrics display timing error | Di bawah 150 ms pada sesi yang menyediakan timeline akurat |
| UI responsiveness           | Tidak blocking saat HTTP request                           |
| Network calls               | Hanya ketika track berubah/cache miss                      |
| Offline support             | Cached lyrics tetap tersedia                               |
| Startup                     | Tidak membutuhkan hak Administrator                        |

Test scenario minimum: Spotify desktop, YouTube Music di Brave, pause/resume, seek maju-mundur, next track, lagu instrumental, tidak ada koneksi internet, multiple media sessions, perubahan DPI, fullscreen game, dan Explorer restart.

## 8. Development execution checklist

Implementation progress

Checklist interaktif berdasarkan milestone

## 0%

1\. Foundation

Initialize CMake + MSVC project

Create native Win32 message loop

Implement system tray and clean shutdown

2\. Media & Lyrics

Integrate C++/WinRT GSMTC

Implement media session selection and events

Implement LRCLIB HTTP client

Parse LRC and cache results

3\. Synchronization

Build playback clock and timestamp interpolation

Implement active lyric selection

Handle pause, seek and track switching

4\. Native UI

Create Direct2D/DirectWrite renderer

Implement frameless lyrics window

Implement taskbar-aware overlay positioning

Handle DPI, fullscreen and Explorer restart

5\. Release

Add configuration and startup support

Write unit and integration tests

Profile CPU, RAM and timing accuracy

Build portable release EXE

&#x20;Copy checklist Markdown

## 9. Recommended execution order

Walaupun Phase 6 adalah integrasi taskbar, saya menyarankan membuat technical spike taskbar positioning sejak awal setelah foundation selesai. Alasannya, bagian itu adalah risiko teknis paling tinggi, dan kita perlu tahu keterbatasan Windows 11 sebelum menghabiskan waktu membangun UI.

Untuk versi pertama, definisi done yang saya sarankan adalah:

Spotify atau YouTube Music → GSMTC → LRCLIB → synced lyric → satu baris teks pada taskbar, berjalan tanpa WebView dan tanpa hak Administrator.

Setelah alur tersebut stabil, barulah menambahkan styling, animasi, pengaturan posisi, dan dukungan multi-monitor.

Dengan scope ini, proyek tetap fokus pada satu kegunaan utama: lyrics di taskbar yang ringan, akurat, dan tidak mengganggu aktivitas Windows.