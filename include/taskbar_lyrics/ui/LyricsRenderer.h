#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>

namespace taskbar_lyrics::ui {

struct DisplayText {
    std::wstring lyric;
    std::vector<uint8_t> albumArtBytes;
};

class LyricsRenderer {
public:
    LyricsRenderer() = default;
    ~LyricsRenderer();

    LyricsRenderer(const LyricsRenderer&) = delete;
    LyricsRenderer& operator=(const LyricsRenderer&) = delete;

    bool Initialize(HWND window, float fontSize = 12.0f);
    [[nodiscard]] HRESULT InitializationError() const;
    void Resize(UINT width, UINT height);
    void SetWrapLongLyrics(bool wrap) { wrapLongLyrics_ = wrap; }
    void Render(const DisplayText& text, const std::wstring& previousLyric = L"", float transitionProgress = 1.0f);
    void DiscardDeviceResources();

private:
    bool CreateDeviceResources();
    void UpdateAlbumArt(const std::vector<uint8_t>& bytes);

    HWND window_{};
    ID2D1Factory* d2dFactory_{};
    IDWriteFactory* writeFactory_{};
    IWICImagingFactory* wicFactory_{};
    ID2D1HwndRenderTarget* target_{};
    ID2D1SolidColorBrush* whiteBrush_{};
    ID2D1SolidColorBrush* transitionBrush_{};
    ID2D1SolidColorBrush* placeholderBrush_{};
    ID2D1SolidColorBrush* borderBrush_{};
    ID2D1Bitmap* albumArtBitmap_{};
    ID2D1BitmapBrush* albumArtBrush_{};
    IDWriteTextFormat* lyricFormat_{};
    IDWriteTextFormat* lyricFormatWrap_{};
    IDWriteInlineObject* ellipsisSign_{};
    IDWriteInlineObject* ellipsisSignWrap_{};
    std::vector<uint8_t> currentArtBytes_{};
    float fontSize_{12.0f};
    bool wrapLongLyrics_{true};
    HRESULT initializationError_{S_OK};
};

}  // namespace taskbar_lyrics::ui
