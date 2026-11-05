#include "taskbar_lyrics/lyrics/LyricsProvider.h"

#include <windows.h>
#include <winhttp.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <array>
#include <memory>
#include <string_view>

namespace taskbar_lyrics::lyrics {
namespace {

struct InternetHandleCloser {
    void operator()(void* handle) const {
        if (handle != nullptr) WinHttpCloseHandle(handle);
    }
};
using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

std::wstring EncodeQuery(std::wstring_view value) {
    if (value.empty()) return {};
    const int bytesNeeded = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytesNeeded <= 0) return {};
    std::string utf8(static_cast<std::size_t>(bytesNeeded), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        utf8.data(), bytesNeeded, nullptr, nullptr);
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring encoded;
    for (const unsigned char byte : utf8) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            encoded.push_back(static_cast<wchar_t>(byte));
        } else {
            encoded.push_back(L'%');
            encoded.push_back(hex[byte >> 4]);
            encoded.push_back(hex[byte & 15]);
        }
    }
    return encoded;
}

}  // namespace

LyricsResult LyricsProvider::Fetch(const std::wstring& title, const std::wstring& artist,
                                   const std::wstring& album, int durationSeconds) const {
    if (title.empty() || artist.empty()) return {{}, L"Track metadata incomplete"};

    std::wstring path = L"/api/get?track_name=" + EncodeQuery(title) + L"&artist_name=" + EncodeQuery(artist);
    if (!album.empty()) path += L"&album_name=" + EncodeQuery(album);
    if (durationSeconds > 0 && durationSeconds <= 3600) path += L"&duration=" + std::to_wstring(durationSeconds);

    InternetHandle session{WinHttpOpen(L"Lyrix/1.0 (Windows desktop application)",
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) return {{}, L"Could not connect to lyrics service"};
    WinHttpSetTimeouts(session.get(), 3000, 3000, 5000, 5000);
    InternetHandle connection{WinHttpConnect(session.get(), L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection) return {{}, L"Could not connect to lyrics service"};
    InternetHandle request{WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE)};
    if (!request || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        return {{}, L"Lyrics service unavailable"};
    }

    DWORD status{};
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        return {{}, L"Lyrics service unavailable"};
    }
    if (status == 404) return {{}, L"No lyrics found for this track"};
    if (status == 429) return {{}, L"Lyrics service rate limit reached"};
    if (status != 200) return {{}, L"Lyrics service unavailable"};

    std::string response;
    std::array<char, 4096> buffer{};
    DWORD bytesRead{};
    while (true) {
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead)) {
            return {{}, L"Lyrics service unavailable"};
        }
        if (bytesRead == 0) break;
        if (response.size() + bytesRead > 1024 * 1024) return {{}, L"Lyrics response too large"};
        response.append(buffer.data(), bytesRead);
    }
    if (response.empty()) return {{}, L"Lyrics service unavailable"};

    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, response.data(),
                                               static_cast<int>(response.size()), nullptr, 0);
    if (wideLength <= 0) return {{}, L"Invalid lyrics response"};
    std::wstring wideResponse(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, response.data(), static_cast<int>(response.size()),
                        wideResponse.data(), wideLength);
    try {
        const auto record = winrt::Windows::Data::Json::JsonObject::Parse(winrt::hstring{wideResponse});
        if (record.HasKey(L"instrumental") && record.GetNamedBoolean(L"instrumental")) {
            return {{}, L"Instrumental track"};
        }
        if (!record.HasKey(L"syncedLyrics") ||
            record.GetNamedValue(L"syncedLyrics").ValueType() != winrt::Windows::Data::Json::JsonValueType::String) {
            return {{}, L"No synchronized lyrics available"};
        }
        auto lines = ParseLrc(record.GetNamedString(L"syncedLyrics").c_str());
        if (lines.empty()) return {{}, L"No synchronized lyrics available"};
        return {std::move(lines), {}};
    } catch (const winrt::hresult_error&) {
        return {{}, L"Invalid lyrics response"};
    }
}

}  // namespace taskbar_lyrics::lyrics
