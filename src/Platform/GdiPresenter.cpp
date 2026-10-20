#include "Platform/GdiPresenter.h"

#include <limits>

std::expected<void, sr::platform::PresentationError>
sr::platform::GdiPresenter::Resize(HWND window, int width, int height) noexcept
{
    Reset();
    if (width < 0 || height < 0)
        return std::unexpected(PresentationError::InvalidDimensions);
    if (width == 0 || height == 0)
        return {};

    const auto rowSize = static_cast<std::size_t>(width);
    const auto rowCount = static_cast<std::size_t>(height);
    if (rowSize > std::numeric_limits<std::size_t>::max() / rowCount / sizeof(unsigned int))
        return std::unexpected(PresentationError::SizeOverflow);

    unsigned int *pixels = nullptr;
    if (!m_backBuffer.Create(window, width, height, pixels) || !pixels)
    {
        Reset();
        return std::unexpected(PresentationError::BufferCreationFailed);
    }

    m_surface = {{pixels, rowSize * rowCount}, width, height, rowSize};
    return {};
}

void sr::platform::GdiPresenter::Present(HDC screenDc) const noexcept
{
    if (m_surface.pixels.empty() || !screenDc)
        return;
    BitBlt(screenDc, 0, 0, m_surface.width, m_surface.height, m_backBuffer.Get(), 0, 0, SRCCOPY);
    // GDI batch가 DIB를 읽는 동안 다음 프레임의 CPU 쓰기가 시작되지 않게 한다.
    // CreateDIBSection의 직접 메모리 접근 계약이며 추가 화면 복사는 발생하지 않는다.
    GdiFlush();
}

void sr::platform::GdiPresenter::Reset() noexcept
{
    m_surface = {};
    m_backBuffer.Reset();
}
