#include "taskbar_lyrics/ui/LyricsRenderer.h"

#include <algorithm>
#include <cmath>

namespace taskbar_lyrics::ui {
namespace {

template <typename T>
void Release(T*& pointer) {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

}  // namespace

LyricsRenderer::~LyricsRenderer() {
    DiscardDeviceResources();
    Release(ellipsisSignWrap_);
    Release(ellipsisSign_);
    Release(lyricFormatWrap_);
    Release(lyricFormat_);
    Release(writeFactory_);
    Release(d2dFactory_);
    Release(wicFactory_);
}

bool LyricsRenderer::Initialize(HWND window, float fontSize) {
    window_ = window;
    fontSize_ = fontSize > 0.0f ? fontSize : 12.0f;

    if (d2dFactory_ == nullptr) {
        initializationError_ = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
        if (FAILED(initializationError_)) {
            return false;
        }
    }
    if (writeFactory_ == nullptr) {
        initializationError_ = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                    reinterpret_cast<IUnknown**>(&writeFactory_));
        if (FAILED(initializationError_)) {
            return false;
        }
    }
    if (wicFactory_ == nullptr) {
        initializationError_ = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(&wicFactory_));
        if (initializationError_ == CO_E_NOTINITIALIZED) {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            initializationError_ = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                    IID_PPV_ARGS(&wicFactory_));
        }
        if (FAILED(initializationError_)) {
            return false;
        }
    }

    if (lyricFormat_ == nullptr) {
        initializationError_ = writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                                 fontSize_, L"", &lyricFormat_);
        if (FAILED(initializationError_)) {
            return false;
        }
        lyricFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        lyricFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (ellipsisSign_ == nullptr) {
        writeFactory_->CreateEllipsisTrimmingSign(lyricFormat_, &ellipsisSign_);
    }

    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    lyricFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    lyricFormat_->SetTrimming(&trimming, ellipsisSign_);

    // 2-line wrapped format for long lyrics: keep the same font size (no shrinking)
    const float wrapFontSize = fontSize_ <= 13.0f ? fontSize_ : (std::max)(12.0f, fontSize_ - 1.5f);
    if (lyricFormatWrap_ == nullptr) {
        initializationError_ = writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                                 wrapFontSize, L"", &lyricFormatWrap_);
        if (FAILED(initializationError_)) {
            return false;
        }
        lyricFormatWrap_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        lyricFormatWrap_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (ellipsisSignWrap_ == nullptr) {
        writeFactory_->CreateEllipsisTrimmingSign(lyricFormatWrap_, &ellipsisSignWrap_);
    }
    lyricFormatWrap_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    lyricFormatWrap_->SetTrimming(&trimming, ellipsisSignWrap_);

    return CreateDeviceResources();
}

bool LyricsRenderer::CreateDeviceResources() {
    if (target_ != nullptr) {
        return true;
    }
    if (d2dFactory_ == nullptr || window_ == nullptr) {
        return false;
    }

    RECT rect{};
    GetClientRect(window_, &rect);
    const UINT width = rect.right > rect.left ? static_cast<UINT>(rect.right - rect.left) : 1u;
    const UINT height = rect.bottom > rect.top ? static_cast<UINT>(rect.bottom - rect.top) : 1u;
    const auto properties = D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(width, height));

    initializationError_ = d2dFactory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), properties, &target_);
    if (SUCCEEDED(initializationError_)) {
        initializationError_ = target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &whiteBrush_);
    }
    if (SUCCEEDED(initializationError_)) {
        initializationError_ = target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &transitionBrush_);
    }
    if (SUCCEEDED(initializationError_)) {
        // Dark placeholder for album art when no thumbnail is available
        initializationError_ = target_->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f), &placeholderBrush_);
    }
    if (SUCCEEDED(initializationError_)) {
        // Subtle border around album cover
        initializationError_ = target_->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.3f, 0.3f, 0.7f), &borderBrush_);
    }
    if (FAILED(initializationError_)) {
        DiscardDeviceResources();
        return false;
    }

    // If we have cached album art bytes, re-create the bitmap brush
    if (!currentArtBytes_.empty()) {
        const auto bytes = currentArtBytes_;
        currentArtBytes_.clear();
        UpdateAlbumArt(bytes);
    }
    return true;
}

void LyricsRenderer::UpdateAlbumArt(const std::vector<uint8_t>& bytes) {
    if (bytes == currentArtBytes_ && albumArtBrush_ != nullptr) {
        return;
    }
    currentArtBytes_ = bytes;
    Release(albumArtBrush_);
    Release(albumArtBitmap_);

    if (bytes.empty() || target_ == nullptr || wicFactory_ == nullptr) {
        return;
    }

    IWICStream* stream = nullptr;
    if (FAILED(wicFactory_->CreateStream(&stream))) {
        return;
    }
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))) {
        stream->Release();
        return;
    }

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(wicFactory_->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
        stream->Release();
        return;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        decoder->Release();
        stream->Release();
        return;
    }

    IWICFormatConverter* converter = nullptr;
    if (FAILED(wicFactory_->CreateFormatConverter(&converter))) {
        frame->Release();
        decoder->Release();
        stream->Release();
        return;
    }

    if (SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom))) {
        if (SUCCEEDED(target_->CreateBitmapFromWicBitmap(converter, nullptr, &albumArtBitmap_))) {
            const D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(
                D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            target_->CreateBitmapBrush(albumArtBitmap_, brushProps, &albumArtBrush_);
        }
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
}

HRESULT LyricsRenderer::InitializationError() const {
    return initializationError_;
}

void LyricsRenderer::Resize(UINT width, UINT height) {
    if (target_ != nullptr && width > 0 && height > 0) {
        target_->Resize(D2D1::SizeU(width, height));
    }
}

void LyricsRenderer::Render(const DisplayText& text, const std::wstring& previousLyric, float transitionProgress) {
    if (target_ == nullptr) {
        if (!CreateDeviceResources()) {
            return;
        }
    }

    // Check if album art has changed
    if (text.albumArtBytes != currentArtBytes_) {
        UpdateAlbumArt(text.albumArtBytes);
    }

    const auto size = target_->GetSize();

    target_->BeginDraw();
    // Pure black key is masked out completely by LWA_COLORKEY on WS_EX_LAYERED window
    target_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // 1. Draw Album Art (Music Cover) on the left
    constexpr float coverSize = 26.0f;
    constexpr float coverLeft = 4.0f;
    const float coverTop = (size.height - coverSize) / 2.0f;
    const D2D1_RECT_F coverRect = D2D1::RectF(coverLeft, coverTop, coverLeft + coverSize, coverTop + coverSize);
    const D2D1_ROUNDED_RECT roundedCover = D2D1::RoundedRect(coverRect, 4.0f, 4.0f);

    if (albumArtBrush_ != nullptr && albumArtBitmap_ != nullptr) {
        const auto bmpSize = albumArtBitmap_->GetSize();
        if (bmpSize.width > 0.0f && bmpSize.height > 0.0f) {
            albumArtBrush_->SetTransform(
                D2D1::Matrix3x2F::Scale(coverSize / bmpSize.width, coverSize / bmpSize.height) *
                D2D1::Matrix3x2F::Translation(coverLeft, coverTop)
            );
        }
        target_->FillRoundedRectangle(roundedCover, albumArtBrush_);
    } else if (placeholderBrush_ != nullptr) {
        target_->FillRoundedRectangle(roundedCover, placeholderBrush_);
    }
    if (borderBrush_ != nullptr) {
        target_->DrawRoundedRectangle(roundedCover, borderBrush_, 0.8f);
    }

    // 2. Clean Lyric Text
    std::wstring line = text.lyric.empty() ? L"♪" : text.lyric;
    const auto lStart = line.find_first_not_of(L" \t\r\n");
    if (lStart != std::wstring::npos) {
        const auto lEnd = line.find_last_not_of(L" \t\r\n");
        line = line.substr(lStart, lEnd - lStart + 1);
    } else if (!text.lyric.empty()) {
        line.clear();
    }

    // 3. Lyrics Layout (Dynamic 2-Line Wrap or Single Line) & Animation (Pure White - No Blue)
    const float textLeft = coverLeft + coverSize + 10.0f;
    const float textRight = (std::max)(textLeft + 10.0f, size.width - 4.0f);
    const float availableWidth = textRight - textLeft;
    constexpr float textLineHeight = 28.0f;
    const float lineTop = (size.height - textLineHeight) / 2.0f;

    auto selectFormat = [this, availableWidth](const std::wstring& str) -> std::pair<IDWriteTextFormat*, bool> {
        if (!wrapLongLyrics_ || lyricFormatWrap_ == nullptr || writeFactory_ == nullptr || str.empty()) {
            return {lyricFormat_, false};
        }
        IDWriteTextLayout* layout = nullptr;
        bool shouldWrap = false;
        if (SUCCEEDED(writeFactory_->CreateTextLayout(str.c_str(), static_cast<UINT32>(str.size()),
                                                      lyricFormat_, 10000.0f, 100.0f, &layout)) && layout != nullptr) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                if (metrics.widthIncludingTrailingWhitespace > availableWidth) {
                    shouldWrap = true;
                }
            }
            layout->Release();
        }
        return {shouldWrap ? lyricFormatWrap_ : lyricFormat_, shouldWrap};
    };

    const auto [lineFmt, isLineWrap] = selectFormat(line);
    const float lineTargetTop = isLineWrap ? 2.0f : lineTop;
    const float lineTargetBottom = isLineWrap ? (size.height - 2.0f) : (lineTop + textLineHeight);

    if (transitionProgress < 1.0f && !previousLyric.empty() && transitionBrush_ != nullptr) {
        // Smooth ease-out cubic curve
        const float t = 1.0f - std::pow(1.0f - transitionProgress, 3.0f);

        // Outgoing lyric: floats up by 5px and fades out in pure white
        std::wstring prevClean = previousLyric;
        const auto pStart = prevClean.find_first_not_of(L" \t\r\n");
        if (pStart != std::wstring::npos) {
            const auto pEnd = prevClean.find_last_not_of(L" \t\r\n");
            prevClean = prevClean.substr(pStart, pEnd - pStart + 1);
        }
        const auto [prevFmt, isPrevWrap] = selectFormat(prevClean);
        const float prevTargetTop = isPrevWrap ? 2.0f : lineTop;
        const float prevTargetBottom = isPrevWrap ? (size.height - 2.0f) : (lineTop + textLineHeight);
        const float outTop = prevTargetTop - 5.0f * t;
        const float outBottom = prevTargetBottom - 5.0f * t;
        const float outAlpha = (std::max)(0.0f, 1.0f - t);
        if (outAlpha > 0.05f) {
            transitionBrush_->SetColor(D2D1::ColorF(outAlpha, outAlpha, outAlpha, 1.0f));
            target_->DrawTextW(prevClean.c_str(), static_cast<UINT32>(prevClean.size()), prevFmt,
                               D2D1::RectF(textLeft, outTop, textRight, outBottom), transitionBrush_);
        }

        // Incoming lyric: rises up 5px from below and fades in in pure white
        const float inTop = lineTargetTop + 5.0f * (1.0f - t);
        const float inBottom = lineTargetBottom + 5.0f * (1.0f - t);
        const float inAlpha = (std::min)(1.0f, (std::max)(0.0f, t));
        transitionBrush_->SetColor(D2D1::ColorF(inAlpha, inAlpha, inAlpha, 1.0f));
        target_->DrawTextW(line.c_str(), static_cast<UINT32>(line.size()), lineFmt,
                           D2D1::RectF(textLeft, inTop, textRight, inBottom), transitionBrush_);
    } else {
        // Static active lyric in pure white
        target_->DrawTextW(line.c_str(), static_cast<UINT32>(line.size()), lineFmt,
                           D2D1::RectF(textLeft, lineTargetTop, textRight, lineTargetBottom), whiteBrush_);
    }

    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void LyricsRenderer::DiscardDeviceResources() {
    Release(albumArtBrush_);
    Release(albumArtBitmap_);
    Release(whiteBrush_);
    Release(transitionBrush_);
    Release(placeholderBrush_);
    Release(borderBrush_);
    Release(target_);
}

}  // namespace taskbar_lyrics::ui
