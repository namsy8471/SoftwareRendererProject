#pragma once
#include "Renderer/ShaderVertices.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct MeshRenderCommand;

// 타일의 크기는 binning과 rasterizer가 함께 사용하는 픽셀 단위 상수다.
inline constexpr int tileSize = 16; // 타일의 크기 (16x16 픽셀)

// 클리핑 fan마다 한 번 준비하고 모든 타일이 읽기 전용으로 공유한다.
// 주소가 바뀌지 않는 worker pool이 선/타일 패스 종료까지 수명을 보장한다.
struct PreparedTriangle
{
    const MeshRenderCommand *sourceCommand = nullptr; // 수명은 해당 프레임의 RenderQueue가 소유한다.
    RasterizerVertex rv0, rv1, rv2;
    // 화면으로 clamp한 inclusive 픽셀 경계. 타일과 교집합을 구해 사용한다.
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    float dx01 = 0.f, dy01 = 0.f, dx12 = 0.f, dy12 = 0.f, dx20 = 0.f, dy20 = 0.f;
    // 기존 float * 256 절삭 규칙은 유지하되 큰 화면의 누적 overflow를 피한다.
    std::int64_t dx01Fixed = 0, dy01Fixed = 0, dx12Fixed = 0, dy12Fixed = 0;
    std::int64_t dx20Fixed = 0, dy20Fixed = 0;
    std::uint64_t primitiveId = 0;
    float maximumInverseW = 0.f;
};

using TriangleRef = PreparedTriangle;

// worker 하나가 한 프레임 동안 준비한 삼각형을 담는다. 블록 단위로 늘어나므로 이미 넣은
// 삼각형의 주소가 바뀌지 않는다(bin이 포인터를 들고 있다). 한 worker만 쓰므로
// concurrent_vector의 atomic 증가와 segment table 조회가 필요 없다. clear는 블록을 남겨 재사용한다.
class TrianglePool
{
  public:
    static constexpr std::size_t blockSize = 1024;

    // 다음 빈 자리. Commit하기 전에는 size에 들어가지 않으며 다음 Slot 호출이 같은 자리를 준다.
    // 준비가 실패한 삼각형은 Commit하지 않으면 되므로 따로 복사할 임시 객체가 필요 없다.
    TriangleRef &Slot()
    {
        if (m_size == m_blocks.size() * blockSize)
            m_blocks.push_back(std::make_unique<TriangleRef[]>(blockSize));
        return at(m_size);
    }
    // 직전 Slot이 준 자리를 확정하고 그 주소를 돌려준다.
    TriangleRef *Commit() { return &at(m_size++); }
    TriangleRef &emplace_back(const TriangleRef &triangle)
    {
        Slot() = triangle;
        return *Commit();
    }
    void reserve(std::size_t count)
    {
        while (capacity() < count)
            m_blocks.push_back(std::make_unique<TriangleRef[]>(blockSize));
    }
    void clear() noexcept { m_size = 0; }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_blocks.size() * blockSize; }
    TriangleRef &operator[](std::size_t index) { return at(index); }
    const TriangleRef &operator[](std::size_t index) const
    {
        return m_blocks[index / blockSize][index % blockSize];
    }

  private:
    TriangleRef &at(std::size_t index) { return m_blocks[index / blockSize][index % blockSize]; }

    std::vector<std::unique_ptr<TriangleRef[]>> m_blocks;
    std::size_t m_size = 0;
};
