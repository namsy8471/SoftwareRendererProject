#include "Renderer/RenderTarget.h"

#include <algorithm>
#include <new>
#include <stdexcept>

std::expected<void, RenderTargetError> RenderTarget::Attach(PixelSurfaceView surface)
{
    Detach();
    if (surface.width < 0 || surface.height < 0)
        return std::unexpected(RenderTargetError::InvalidDimensions);
    if (surface.width == 0 || surface.height == 0)
        return {};

    const auto width = static_cast<std::size_t>(surface.width);
    const auto height = static_cast<std::size_t>(surface.height);
    if (surface.stridePixels < width)
        return std::unexpected(RenderTargetError::InvalidStride);

    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (height - 1 > (maximum - width) / surface.stridePixels || width > maximum / height)
        return std::unexpected(RenderTargetError::SizeOverflow);
    const auto requiredStorage = (height - 1) * surface.stridePixels + width;
    const auto pixelCount = width * height;
    if (requiredStorage > maximum / sizeof(unsigned int) || pixelCount > m_depth.max_size() ||
        pixelCount > m_primitiveIds.max_size())
        return std::unexpected(RenderTargetError::SizeOverflow);
    if (surface.pixels.size() < requiredStorage)
        return std::unexpected(RenderTargetError::InsufficientStorage);

    try
    {
        m_depth.assign(pixelCount, emptyDepth);
        m_primitiveIds.assign(pixelCount, emptyOwner);
    }
    catch (const std::bad_alloc &)
    {
        Detach();
        return std::unexpected(RenderTargetError::AllocationFailed);
    }
    catch (const std::length_error &)
    {
        Detach();
        return std::unexpected(RenderTargetError::SizeOverflow);
    }

    // 모든 실패 가능한 단계가 끝난 뒤에만 빌린 메모리를 공개한다.
    m_surface = surface;
    return {};
}

void RenderTarget::Detach() noexcept
{
    m_surface = {};
    m_depth.clear();
    m_primitiveIds.clear();
}

void RenderTarget::Clear() noexcept
{
    for (int y = 0; y < m_surface.height; ++y)
    {
        const auto row = static_cast<std::size_t>(y) * m_surface.stridePixels;
        std::ranges::fill(m_surface.pixels.subspan(row, static_cast<std::size_t>(m_surface.width)), 0u);
    }
    std::ranges::fill(m_depth, emptyDepth);
    std::ranges::fill(m_primitiveIds, emptyOwner);
}
