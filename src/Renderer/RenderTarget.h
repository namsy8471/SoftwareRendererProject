#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <vector>

// 픽셀은 top-down BGRX 메모리(정수 값 0x00RRGGBB)이며 stride 단위는 바이트가 아닌 픽셀이다.
// 뷰는 메모리를 소유하지 않는다. 소유자는 연결된 target을 Detach한 뒤에만 저장소를 해제한다.
struct PixelSurfaceView
{
    std::span<unsigned int> pixels;
    int width = 0;
    int height = 0;
    std::size_t stridePixels = 0;
};

struct ConstPixelSurfaceView
{
    std::span<const unsigned int> pixels;
    int width = 0;
    int height = 0;
    std::size_t stridePixels = 0;
};

static_assert(sizeof(unsigned int) == 4, "Render targets require 32-bit BGRX pixels.");

enum class RenderTargetError
{
    InvalidDimensions,
    InvalidStride,
    InsufficientStorage,
    SizeOverflow,
    AllocationFailed
};

// 색상 저장소만 빌리고 깊이와 동일 깊이 판정용 ID는 직접 소유한다.
// 깊이/ID는 width * height로 촘촘하게 배치되며 색상 stride를 사용하지 않는다.
class RenderTarget
{
  public:
    static constexpr std::uint64_t emptyOwner = std::numeric_limits<std::uint64_t>::max();
    static constexpr float emptyDepth = std::numeric_limits<float>::lowest();

    RenderTarget() = default;
    RenderTarget(const RenderTarget &) = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;
    RenderTarget(RenderTarget &&) = delete;
    RenderTarget &operator=(RenderTarget &&) = delete;

    // 연결/해제는 렌더 작업이 없는 때에만 호출한다. 실패 시 이전 뷰도 남기지 않는다.
    // 한 축이 0이면 성공한 빈 target으로 정규화한다. 음수 크기는 오류다.
    [[nodiscard]] std::expected<void, RenderTargetError> Attach(PixelSurfaceView surface);
    void Detach() noexcept;
    // 색상 padding은 건드리지 않는다. 큰 1/w가 가까우며 같은 깊이는 작은 ID가 이긴다.
    void Clear() noexcept;

    [[nodiscard]] PixelSurfaceView Surface() noexcept { return m_surface; }
    [[nodiscard]] ConstPixelSurfaceView Surface() const noexcept
    {
        return {m_surface.pixels, m_surface.width, m_surface.height, m_surface.stridePixels};
    }
    [[nodiscard]] std::span<float> Depth() noexcept { return m_depth; }
    [[nodiscard]] std::span<const float> Depth() const noexcept { return m_depth; }
    [[nodiscard]] std::span<std::uint64_t> PrimitiveIds() noexcept { return m_primitiveIds; }
    [[nodiscard]] std::span<const std::uint64_t> PrimitiveIds() const noexcept { return m_primitiveIds; }
    [[nodiscard]] int Width() const noexcept { return m_surface.width; }
    [[nodiscard]] int Height() const noexcept { return m_surface.height; }
    [[nodiscard]] bool Empty() const noexcept { return m_surface.pixels.empty(); }

  private:
    void clearRows(int firstRow, int lastRow) noexcept;

    PixelSurfaceView m_surface;
    std::vector<float> m_depth;
    std::vector<std::uint64_t> m_primitiveIds;
};
