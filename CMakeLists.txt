cmake_minimum_required(VERSION 3.25)

project(taskbar_lyrics VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(taskbar_lyrics_core
    src/lyrics/LrcParser.cpp
    src/sync/LyricsSyncEngine.cpp
)

target_include_directories(taskbar_lyrics_core PUBLIC include)
target_compile_definitions(taskbar_lyrics_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(taskbar-lyrics WIN32
    src/main.cpp
    src/app/Application.cpp
    src/app/TrayManager.cpp
    src/media/MediaSessionWorker.cpp
    src/lyrics/LyricsProvider.cpp
    src/core/ConfigManager.cpp
    src/ui/LyricsRenderer.cpp
    src/ui/LyricsWindow.cpp
    src/ui/TaskbarManager.cpp
)

target_link_libraries(taskbar-lyrics PRIVATE taskbar_lyrics_core d2d1 dwrite windowscodecs shell32 windowsapp winhttp)
target_include_directories(taskbar-lyrics PRIVATE include)
target_compile_definitions(taskbar-lyrics PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

include(CTest)
if(BUILD_TESTING)
    add_executable(taskbar_lyrics_tests
        tests/LrcParserTests.cpp
        tests/LyricsSyncTests.cpp
    )
    target_link_libraries(taskbar_lyrics_tests PRIVATE taskbar_lyrics_core)
    target_include_directories(taskbar_lyrics_tests PRIVATE include)
    add_test(NAME taskbar_lyrics_tests COMMAND taskbar_lyrics_tests)
endif()
