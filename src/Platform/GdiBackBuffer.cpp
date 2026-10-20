#include "Platform/GdiBackBuffer.h"
#include <utility>

namespace sr::platform
{
GdiBackBuffer::GdiBackBuffer(GdiBackBuffer &&other) noexcept
    : m_memoryDc(std::exchange(other.m_memoryDc, nullptr)), m_bitmap(std::exchange(other.m_bitmap, nullptr)),
      m_previousBitmap(std::exchange(other.m_previousBitmap, nullptr))
{
}

GdiBackBuffer &GdiBackBuffer::operator=(GdiBackBuffer &&other) noexcept
{
    if (this == &other)
        return *this;
    Reset();
    m_memoryDc = std::exchange(other.m_memoryDc, nullptr);
    m_bitmap = std::exchange(other.m_bitmap, nullptr);
    m_previousBitmap = std::exchange(other.m_previousBitmap, nullptr);
    return *this;
}

bool GdiBackBuffer::Create(HWND window, int width, int height, unsigned int *&pixels) noexcept
{
    Reset();
    pixels = nullptr;

    HDC screenDc = GetDC(window);
    if (!screenDc)
        return false;

    struct ScreenDcGuard
    {
        HWND window;
        HDC dc;
        ~ScreenDcGuard() { ReleaseDC(window, dc); }
    };
    const ScreenDcGuard releaseScreenDc{window, screenDc};

    m_memoryDc = CreateCompatibleDC(screenDc);
    if (!m_memoryDc)
        return false;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void *rawPixels = nullptr;
    m_bitmap = CreateDIBSection(m_memoryDc, &bitmapInfo, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!m_bitmap)
    {
        Reset();
        return false;
    }

    m_previousBitmap = SelectObject(m_memoryDc, m_bitmap);
    if (!m_previousBitmap || m_previousBitmap == HGDI_ERROR)
    {
        Reset();
        return false;
    }

    pixels = static_cast<unsigned int *>(rawPixels);
    return true;
}

void GdiBackBuffer::Reset() noexcept
{
    if (m_memoryDc && m_previousBitmap && m_previousBitmap != HGDI_ERROR)
        SelectObject(m_memoryDc, m_previousBitmap);
    if (m_bitmap)
        DeleteObject(m_bitmap);
    if (m_memoryDc)
        DeleteDC(m_memoryDc);

    m_previousBitmap = nullptr;
    m_bitmap = nullptr;
    m_memoryDc = nullptr;
}

} // namespace sr::platform
