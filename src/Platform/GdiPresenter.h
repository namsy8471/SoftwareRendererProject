#pragma once

#include "Platform/GdiBackBuffer.h"
#include "Renderer/RenderTarget.h"

#include <expected>

namespace sr::platform
{
enum class PresentationError
{
    InvalidDimensions,
    SizeOverflow,
    BufferCreationFailed
};

// 창 크기와 GDI 자원을 소유한다. 렌더러는 Surface의 DIB 메모리에 직접 써서 추가 복사가 없다.
class GdiPresenter
{
  public:
    GdiPresenter() = default;
    GdiPresenter(const GdiPresenter &) = delete;
    GdiPresenter &operator=(const GdiPresenter &) = delete;
    GdiPresenter(GdiPresenter &&) = delete;
    GdiPresenter &operator=(GdiPresenter &&) = delete;

    // 호출 전에 이 Surface를 빌린 모든 RenderTarget을 Detach해야 한다.
    // 실패와 0 크기는 빈 presenter로 남는다. 이후 Present는 아무것도 하지 않는다.
    [[nodiscard]] std::expected<void, PresentationError> Resize(HWND window, int width, int height) noexcept;
    [[nodiscard]] PixelSurfaceView Surface() noexcept { return m_surface; }
    [[nodiscard]] ConstPixelSurfaceView Surface() const noexcept
    {
        return {m_surface.pixels, m_surface.width, m_surface.height, m_surface.stridePixels};
    }
    void Present(HDC screenDc) const noexcept;
    void Reset() noexcept;

  private:
    GdiBackBuffer m_backBuffer;
    PixelSurfaceView m_surface;
};
} // namespace sr::platform
