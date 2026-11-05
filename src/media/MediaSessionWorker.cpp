#include "taskbar_lyrics/media/MediaSessionWorker.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>

namespace taskbar_lyrics::media {
namespace {
using namespace winrt::Windows::Media::Control;

std::wstring TrackKey(const std::wstring& title, const std::wstring& artist,
                      const std::wstring& album, int duration) {
    return title + L"\n" + artist + L"\n" + album + L"\n" + std::to_wstring(duration);
}

std::chrono::milliseconds FromTimeSpan(winrt::Windows::Foundation::TimeSpan value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(value);
}

}  // namespace

void MediaSessionWorker::Start(HWND destination, int lyricOffsetMilliseconds) {
    destination_ = destination;
    sync_.SetOffset(std::chrono::milliseconds{lyricOffsetMilliseconds});
    thread_ = std::jthread{[this](std::stop_token stop) { Run(stop); }};
}

void MediaSessionWorker::Stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void MediaSessionWorker::Publish(ui::DisplayText text) {
    if (text.lyric == lastPublished_.lyric && text.albumArtBytes == lastPublished_.albumArtBytes) return;
    lastPublished_ = text;
    auto* update = new ui::DisplayText{std::move(text)};
    if (!PostMessageW(destination_, kMediaUpdateMessage, 0, reinterpret_cast<LPARAM>(update))) delete update;
}

void MediaSessionWorker::Run(std::stop_token stop) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        std::wstring activeKey;
        std::vector<uint8_t> activeCoverBytes;
        auto lastTick = std::chrono::steady_clock::now();
        std::chrono::milliseconds localPosition{0};
        std::chrono::milliseconds lastReportedPosition{0};

        while (!stop.stop_requested()) {
            try {
                auto selected = manager.GetCurrentSession();
                const auto sessions = manager.GetSessions();
                for (const auto& session : sessions) {
                    const auto playback = session.GetPlaybackInfo();
                    if (playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                        selected = session;
                        break;
                    }
                }
                if (!selected) {
                    activeKey.clear();
                    activeCoverBytes.clear();
                    sync_.SetLyrics({});
                    localPosition = std::chrono::milliseconds{0};
                    lastReportedPosition = std::chrono::milliseconds{0};
                    lastTick = std::chrono::steady_clock::now();
                    Publish({L"♪", {}});
                } else {
                    const auto properties = selected.TryGetMediaPropertiesAsync().get();
                    const std::wstring title{properties.Title().c_str()};
                    const std::wstring artist{properties.Artist().c_str()};
                    const std::wstring album{properties.AlbumTitle().c_str()};
                    const auto timeline = selected.GetTimelineProperties();
                    const auto duration = FromTimeSpan(timeline.EndTime() - timeline.StartTime());
                    const int durationSeconds = static_cast<int>(duration.count() / 1000);
                    const auto key = TrackKey(title, artist, album, durationSeconds);

                    std::wstring trackDisplay;
                    if (!title.empty() && !artist.empty()) {
                        trackDisplay = title + L" - " + artist;
                    } else if (!title.empty()) {
                        trackDisplay = title;
                    } else if (!artist.empty()) {
                        trackDisplay = artist;
                    } else {
                        trackDisplay = L"♪";
                    }

                    if (key != activeKey) {
                        activeKey = key;
                        activeCoverBytes.clear();
                        try {
                            if (const auto thumb = properties.Thumbnail()) {
                                const auto stream = thumb.OpenReadAsync().get();
                                const auto size = static_cast<size_t>(stream.Size());
                                if (size > 0 && size < 5 * 1024 * 1024) {
                                    activeCoverBytes.resize(size);
                                    winrt::Windows::Storage::Streams::DataReader reader{stream};
                                    reader.LoadAsync(static_cast<uint32_t>(size)).get();
                                    reader.ReadBytes(winrt::array_view<uint8_t>{activeCoverBytes});
                                }
                            }
                        } catch (...) {
                            activeCoverBytes.clear();
                        }

                        sync_.SetLyrics({});
                        localPosition = FromTimeSpan(timeline.Position());
                        lastReportedPosition = localPosition;
                        lastTick = std::chrono::steady_clock::now();
                        Publish({trackDisplay, activeCoverBytes});
                        if (cache_.find(key) == cache_.end()) {
                            auto fetched = provider_.Fetch(title, artist, album, durationSeconds);
                            cache_.emplace(key, std::move(fetched));
                        }
                        if (stop.stop_requested()) break;
                        sync_.SetLyrics(cache_.at(key).lines);
                    }
                    const auto playback = selected.GetPlaybackInfo();
                    const auto state = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing
                        ? sync::PlaybackState::Playing : sync::PlaybackState::Paused;

                    const auto nowClock = std::chrono::steady_clock::now();
                    const auto tickDelta = std::chrono::duration_cast<std::chrono::milliseconds>(nowClock - lastTick);
                    lastTick = nowClock;

                    const auto reportedPosition = FromTimeSpan(timeline.Position());
                    const auto lastUpdated = timeline.LastUpdatedTime();
                    const auto nowWinRt = winrt::clock::now();

                    std::chrono::milliseconds currentPositionMs{0};
                    if (lastUpdated.time_since_epoch().count() > 0) {
                        auto posTimeSpan = timeline.Position();
                        if (state == sync::PlaybackState::Playing) {
                            const auto elapsed = nowWinRt - lastUpdated;
                            if (elapsed.count() > 0) {
                                posTimeSpan = posTimeSpan + elapsed;
                            }
                        }
                        const auto endTime = timeline.EndTime();
                        if (endTime.count() > 0 && posTimeSpan > endTime) {
                            posTimeSpan = endTime;
                        }
                        if (posTimeSpan.count() < 0) {
                            posTimeSpan = winrt::Windows::Foundation::TimeSpan{0};
                        }
                        currentPositionMs = FromTimeSpan(posTimeSpan);
                    } else {
                        // Fallback for players that do not supply LastUpdatedTime
                        if (reportedPosition != lastReportedPosition) {
                            localPosition = reportedPosition;
                            lastReportedPosition = reportedPosition;
                        } else if (state == sync::PlaybackState::Playing) {
                            localPosition += tickDelta;
                        }
                        currentPositionMs = localPosition;
                    }

                    sync_.UpdateTimeline(currentPositionMs, state, nowClock);
                    const auto line = sync_.ActiveLine(nowClock);
                    const auto& result = cache_.at(key);
                    std::wstring displayText;
                    if (line && !line->text.empty()) {
                        displayText = line->text;
                    } else if (!line) {
                        // Before first lyric starts (song intro) or if no lyrics found: show Title - Artist
                        displayText = trackDisplay;
                    } else {
                        // Empty line during song (instrumental interlude): show ♪
                        displayText = L"♪";
                    }
                    Publish({std::move(displayText), activeCoverBytes});
                }
            } catch (const winrt::hresult_error&) {
                Publish({L"♪", {}});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
        }
    } catch (const winrt::hresult_error& e) {
        const std::wstring msg = e.message().c_str();
        Publish({msg.empty() ? L"Windows media session access failed" : (L"Error: " + msg), {}});
    } catch (const std::exception& e) {
        const std::string s = e.what();
        Publish({s.empty() ? L"Unexpected error" : (L"Error: " + std::wstring(s.begin(), s.end())), {}});
    } catch (...) {
        Publish({L"Unknown error occurred", {}});
    }
}

}  // namespace taskbar_lyrics::media
