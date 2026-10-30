#include "Graphics/MeshSimplifier.h"
#include "Graphics/Mesh.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace
{
constexpr std::uint32_t invalidVertex = ~0u;

// 평면 거리 제곱의 합을 나타내는 대칭 4x4 행렬의 위 삼각형 10개 + 면적 가중치 합.
// 정밀도: IronMan 로컬 좌표는 수백 단위라 제곱이 float 유효 자릿수를 넘으므로 double로 둔다.
struct Quadric
{
    double a00 = 0, a01 = 0, a02 = 0, a03 = 0;
    double a11 = 0, a12 = 0, a13 = 0;
    double a22 = 0, a23 = 0;
    double a33 = 0;
    double weight = 0;

    void AddPlane(double nx, double ny, double nz, double d, double w) noexcept
    {
        a00 += w * nx * nx; a01 += w * nx * ny; a02 += w * nx * nz; a03 += w * nx * d;
        a11 += w * ny * ny; a12 += w * ny * nz; a13 += w * ny * d;
        a22 += w * nz * nz; a23 += w * nz * d;
        a33 += w * d * d;
        weight += w;
    }
    void Add(const Quadric &q) noexcept
    {
        a00 += q.a00; a01 += q.a01; a02 += q.a02; a03 += q.a03;
        a11 += q.a11; a12 += q.a12; a13 += q.a13;
        a22 += q.a22; a23 += q.a23;
        a33 += q.a33;
        weight += q.weight;
    }
    // 점 p에서 두 quadric 합의 면적 가중 평균 거리 제곱. 합을 실제로 만들지 않고 바로 계산한다.
    [[nodiscard]] static double Error(const Quadric &q, const Quadric &r, const SRMath::vec3 &p) noexcept
    {
        const double x = p.x, y = p.y, z = p.z;
        const double e =
            (q.a00 + r.a00) * x * x + 2 * (q.a01 + r.a01) * x * y + 2 * (q.a02 + r.a02) * x * z +
            2 * (q.a03 + r.a03) * x + (q.a11 + r.a11) * y * y + 2 * (q.a12 + r.a12) * y * z +
            2 * (q.a13 + r.a13) * y + (q.a22 + r.a22) * z * z + 2 * (q.a23 + r.a23) * z + (q.a33 + r.a33);
        const double w = q.weight + r.weight;
        return w > 0 ? std::max(e, 0.0) / w : 0.0;
    }
};

struct PositionKey
{
    std::uint32_t x, y, z;
    bool operator==(const PositionKey &) const noexcept = default;
};
// -0과 +0을 같은 위치로 본다(+0.f를 더하면 -0이 +0이 된다).
PositionKey KeyOf(const SRMath::vec3 &p) noexcept
{
    return {std::bit_cast<std::uint32_t>(p.x + 0.f), std::bit_cast<std::uint32_t>(p.y + 0.f),
            std::bit_cast<std::uint32_t>(p.z + 0.f)};
}

// 키 12바이트 → 값 32비트의 열린 주소(선형 탐사) 해시 표. 크기를 처음에 정하고 지우지 않는다.
// std::unordered_map은 항목마다 노드를 할당해 IronMan 로드에서 malloc/free가 단순화 시간의 큰 몫이었다.
class PositionTable
{
  public:
    explicit PositionTable(std::size_t expected)
    {
        std::size_t capacity = 16;
        while (capacity < expected * 2)
            capacity <<= 1;
        m_mask = capacity - 1;
        m_slots.resize(capacity, Slot{{}, invalidVertex});
    }
    // key가 없으면 value를 넣고 {value, true}, 있으면 {기존 값, false}.
    std::pair<std::uint32_t &, bool> TryEmplace(const PositionKey &key, std::uint32_t value) noexcept
    {
        std::uint64_t h = key.x * 0x9E3779B1ull;
        h ^= (h >> 15) ^ (key.y * 0x85EBCA77ull);
        h ^= (h >> 13) ^ (key.z * 0xC2B2AE3Dull);
        for (std::size_t i = static_cast<std::size_t>(h ^ (h >> 16)) & m_mask;; i = (i + 1) & m_mask)
        {
            Slot &slot = m_slots[i];
            if (slot.value == invalidVertex)
            {
                slot = Slot{key, value};
                return {slot.value, true};
            }
            if (slot.key == key)
                return {slot.value, false};
        }
    }

  private:
    struct Slot
    {
        PositionKey key;
        std::uint32_t value;
    };
    std::vector<Slot> m_slots;
    std::size_t m_mask = 0;
};

struct Collapse
{
    std::uint32_t from, to; // 위치 정점(대표 번호) from을 to 위로 접는다
    double cost;
};

SRMath::vec3 Cross(const SRMath::vec3 &a, const SRMath::vec3 &b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Dot(const SRMath::vec3 &a, const SRMath::vec3 &b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

class Simplifier
{
  public:
    Simplifier(std::span<const Vertex> vertices, std::span<const unsigned int> indices,
               std::span<const std::uint8_t> lockedVertices)
        : m_vertices(vertices), m_remap(vertices.size(), invalidVertex), m_wedgeNext(vertices.size())
    {
        weldPositions(indices);
        for (std::size_t v = 0; v < lockedVertices.size() && v < vertices.size(); ++v)
            if (lockedVertices[v] && m_remap[v] != invalidVertex)
                m_kind[m_remap[v]] = Kind::Locked;
        m_indices.reserve(indices.size());
        m_corners.reserve(indices.size());
        // 같은 위치를 두 번 쓰는 퇴화 삼각형은 면적이 0이라 아무것도 그리지 않는다. 처음부터 뺀다.
        // 같은 세 위치를 같은 방향으로 다시 쓰는 중복 삼각형(IronMan에 약 3%)도 뺀다. 첫 삼각형과 같은 자리를
        // 덮을 뿐이고(깊이가 같아 법선만 다르게 섞인 점무늬를 만든다), 남겨 두면 그 변을 세 면 이상이
        // 공유하는 비다양체로 보여 주변 정점이 모두 고정된다.
        PositionTable seenTriangles(indices.size() / 3);
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const unsigned int v0 = indices[i], v1 = indices[i + 1], v2 = indices[i + 2];
            if (v0 >= vertices.size() || v1 >= vertices.size() || v2 >= vertices.size())
                continue;
            const auto c0 = m_remap[v0], c1 = m_remap[v1], c2 = m_remap[v2];
            if (c0 == c1 || c1 == c2 || c0 == c2)
                continue;
            // 회전만 다른 같은 삼각형이 같은 키가 되도록 가장 작은 번호부터 적는다.
            const PositionKey key = c0 < c1 && c0 < c2 ? PositionKey{c0, c1, c2}
                                    : c1 < c2          ? PositionKey{c1, c2, c0}
                                                       : PositionKey{c2, c0, c1};
            if (!seenTriangles.TryEmplace(key, 0).second)
                continue;
            m_indices.push_back(v0);
            m_indices.push_back(v1);
            m_indices.push_back(v2);
            m_corners.push_back(c0);
            m_corners.push_back(c1);
            m_corners.push_back(c2);
        }
        classifyVertices();
        buildQuadrics();
    }

    std::vector<MeshLod> Run(const sr::lod::LodChainSettings &settings)
    {
        std::vector<MeshLod> lods;
        const std::size_t sourceTriangles = m_indices.size() / 3;
        std::size_t previousTriangles = sourceTriangles;
        for (const float ratio : settings.triangleRatios)
        {
            const auto target = static_cast<std::size_t>(static_cast<double>(sourceTriangles) * ratio);
            while (m_indices.size() / 3 > target)
                if (!runPass(target))
                    break;
            const std::size_t triangles = m_indices.size() / 3;
            if (static_cast<float>(triangles) > static_cast<float>(previousTriangles) * settings.minReduction)
                break; // 더 줄일 수 없다(나머지가 전부 고정 정점이거나 오차가 한계를 넘는다).
            lods.push_back(MeshLod{.indices = m_indices, .error = static_cast<float>(std::sqrt(m_maxError))});
            previousTriangles = triangles;
        }
        return lods;
    }

  private:
    std::span<const Vertex> m_vertices;
    std::vector<std::uint32_t> m_remap;   // 정점 → 같은 위치 정점들의 대표 번호
    std::vector<std::uint32_t> m_wedgeNext; // 같은 위치 정점들의 원형 목록(wedge). 혼자면 자기 자신
    enum class Kind : std::uint8_t
    {
        Locked,   // 움직이지 않는다(이음새, 비다양체, 다른 메시와 공유, 이미 접혀 사라짐)
        Manifold, // 내부 정점
        Border,   // 열린 경계 고리 위 정점. 경계 이웃 위로만 접는다
    };
    std::vector<Kind> m_kind;             // 대표 번호별
    // 경계 정점의 경계 고리 양쪽 이웃(대표 번호). 면 방향(winding)이 섞인 모델이 있어 앞뒤 구분 없이 둘을 둔다.
    std::vector<std::array<std::uint32_t, 2>> m_borderLinks;
    std::vector<Quadric> m_quadrics;      // 대표 번호별
    std::vector<unsigned int> m_indices;  // 현재 단계의 삼각형(원본 정점 번호)
    std::vector<std::uint32_t> m_corners; // m_indices와 같은 자리의 대표 번호(m_remap을 미리 적용해 간접 참조 한 번을 줄인다)
    double m_maxError = 0;                // 지금까지 받아들인 접기 오차의 최댓값(거리 제곱)
    double m_errorLimit = 0;              // 메시 대각선 길이의 제곱. 이보다 큰 오차는 받지 않는다.

    // 작업 버퍼. 패스마다 다시 쓰지만 할당은 재사용한다.
    std::vector<std::uint32_t> m_adjOffsets, m_adjTriangles;
    std::vector<Collapse> m_collapses;
    std::vector<std::uint64_t> m_order; // 정렬 키(비용 비트 << 32 | 후보 번호)
    std::vector<std::uint32_t> m_vertexTo;    // 원본 정점별: 이번 패스에 바뀔 원본 정점 번호(평소에는 invalid)
    std::vector<std::uint8_t> m_passLocked;   // 대표 번호별(평소에는 0)
    std::vector<std::uint32_t> m_touched;     // 이번 패스에 위 두 배열에서 바꾼 칸. 패스 끝에 이것만 되돌린다
    std::vector<std::uint32_t> m_fill;

    void weldPositions(std::span<const unsigned int> indices)
    {
        // 인덱스가 실제로 쓰는 정점만 묶는다. 안 쓰는 중복 정점이 이음새로 잘못 잡히지 않게 한다.
        PositionTable firstAt(m_vertices.size());
        std::vector<std::uint8_t> uvSeam(m_vertices.size(), 0);
        for (std::uint32_t v = 0; v < m_vertices.size(); ++v)
            m_wedgeNext[v] = v;
        AABB bounds;
        for (const unsigned int v : indices)
        {
            if (v >= m_vertices.size() || m_remap[v] != invalidVertex)
                continue;
            const auto &p = m_vertices[v].position;
            const auto [first, inserted] = firstAt.TryEmplace(KeyOf(p), v);
            const std::uint32_t c = first;
            m_remap[v] = c;
            if (!inserted)
            {
                m_wedgeNext[v] = m_wedgeNext[c];
                m_wedgeNext[c] = v;
                const auto &uv = m_vertices[v].texcoord, &uvc = m_vertices[c].texcoord;
                if (uv.x != uvc.x || uv.y != uvc.y)
                    uvSeam[c] = 1;
            }
            bounds.Encapsulate(p);
        }
        // 같은 위치에 법선만 다른 정점(각진 모서리, 면마다 법선을 따로 둔 모델)은 함께 옮길 수 있다.
        // UV가 다른 정점(텍스처 이음새)은 옮기면 텍스처가 번지므로 고정한다.
        m_kind.assign(m_vertices.size(), Kind::Locked);
        for (std::size_t v = 0; v < m_vertices.size(); ++v)
            if (m_remap[v] == v && !uvSeam[v])
                m_kind[v] = Kind::Manifold;
        if (bounds.IsValid())
        {
            const auto e = bounds.max - bounds.min;
            m_errorLimit = static_cast<double>(e.x) * e.x + static_cast<double>(e.y) * e.y + static_cast<double>(e.z) * e.z;
        }
    }

    // 위치 기준으로 모든 변을 방향 없이 모아 공유하는 삼각형 수를 센다. 1개면 열린 경계 변, 2개면 내부 변,
    // 3개 이상이면 비다양체 변(양 끝 고정)이다. 방향을 보지 않는 이유: IronMan처럼 이웃 면의 감김 방향이
    // 서로 반대인 모델이 많고, 렌더러는 양면을 그리므로 화면에서는 같은 표면이다.
    // 경계 정점은 경계 변이 정확히 2개일 때만(단순한 경계 고리) 움직일 수 있다.
    void classifyVertices()
    {
        m_borderLinks.assign(m_vertices.size(), {invalidVertex, invalidVertex});
        std::vector<std::uint8_t> borderCount(m_vertices.size(), 0);
        // 정점 a의 삼각형만 보면 a에서 나가는 변을 모두 안다. 변 a-b(a < b)를 쓰는 삼각형 수를 작은 표로 센다.
        // 전체 변 목록을 정렬하는 것보다 훨씬 싸다(정점 차수는 보통 10 안팎).
        buildAdjacency();
        std::array<std::uint32_t, 64> other{};
        std::array<std::uint8_t, 64> uses{};
        for (std::uint32_t a = 0; a < m_vertices.size(); ++a)
        {
            std::size_t count = 0;
            bool overflow = false;
            for (const auto t : trianglesOf(a))
                for (int k = 0; k < 3; ++k)
                {
                    const auto b = corner(t, k);
                    if (b <= a)
                        continue;
                    std::size_t slot = 0;
                    while (slot < count && other[slot] != b)
                        ++slot;
                    if (slot == count)
                    {
                        if (count == other.size())
                        {
                            overflow = true;
                            continue;
                        }
                        other[count] = b;
                        uses[count++] = 0;
                    }
                    uses[slot] = static_cast<std::uint8_t>(std::min(uses[slot] + 1, 3));
                }
            if (overflow)
                m_kind[a] = Kind::Locked; // 차수가 비정상적으로 큰 정점은 건드리지 않는다
            for (std::size_t slot = 0; slot < count; ++slot)
            {
                const auto b = other[slot];
                if (uses[slot] > 2)
                    m_kind[a] = m_kind[b] = Kind::Locked; // 세 면 이상이 공유하는 변
                else if (uses[slot] == 1)
                    for (const auto &[v, w] : {std::pair{a, b}, std::pair{b, a}})
                    {
                        if (borderCount[v] < 2)
                            m_borderLinks[v][borderCount[v]] = w;
                        borderCount[v] = static_cast<std::uint8_t>(std::min(borderCount[v] + 1, 3));
                    }
            }
        }
        for (std::size_t c = 0; c < m_vertices.size(); ++c)
        {
            if (borderCount[c] == 0)
                continue;
            if (borderCount[c] == 2 && m_kind[c] == Kind::Manifold)
                m_kind[c] = Kind::Border;
            else
                m_kind[c] = Kind::Locked;
        }
    }

    [[nodiscard]] bool isBorderEdge(std::uint32_t a, std::uint32_t b) const noexcept
    {
        return m_borderLinks[a][0] == b || m_borderLinks[a][1] == b;
    }
    [[nodiscard]] std::uint32_t otherBorderLink(std::uint32_t a, std::uint32_t b) const noexcept
    {
        return m_borderLinks[a][0] == b ? m_borderLinks[a][1] : m_borderLinks[a][0];
    }
    // a를 b 위로 접어도 되는 종류인가. 경계 정점은 자기 경계 고리의 이웃 위로만 간다.
    [[nodiscard]] bool canCollapse(std::uint32_t a, std::uint32_t b) const noexcept
    {
        switch (m_kind[a])
        {
        case Kind::Manifold:
            return true;
        case Kind::Border:
            // 고리가 삼각형(변 3개)이면 더 접으면 경계가 사라진다(to와 반대쪽 이웃이 이미 경계 변으로 이어져 있다).
            return isBorderEdge(a, b) && !isBorderEdge(b, otherBorderLink(a, b));
        default:
            return false;
        }
    }

    void buildQuadrics()
    {
        m_quadrics.assign(m_vertices.size(), Quadric{});
        for (std::size_t i = 0; i < m_indices.size(); i += 3)
        {
            const auto &p0 = m_vertices[m_indices[i]].position;
            const auto &p1 = m_vertices[m_indices[i + 1]].position;
            const auto &p2 = m_vertices[m_indices[i + 2]].position;
            const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
            const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
            double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (length <= 0)
                continue;
            nx /= length; ny /= length; nz /= length;
            const double d = -(nx * p0.x + ny * p0.y + nz * p0.z);
            const double area = length * 0.5;
            for (int k = 0; k < 3; ++k)
                m_quadrics[m_corners[i + k]].AddPlane(nx, ny, nz, d, area);

            // 경계 변에는 변을 품고 면에 수직인 평면을 더한다. 경계 정점이 경계선을 벗어나 안쪽으로
            // 움직이면 이 평면에서 멀어져 비용이 커진다. 가중치는 길이 제곱(면적과 같은 차원)에 배수를 둔다.
            constexpr double borderWeight = 10.0;
            for (int k = 0; k < 3; ++k)
            {
                const auto a = m_corners[i + k], b = m_corners[i + (k + 1) % 3];
                if (!isBorderEdge(a, b))
                    continue;
                const auto &pa = m_vertices[a].position;
                const auto &pb = m_vertices[b].position;
                const double ex = pb.x - pa.x, ey = pb.y - pa.y, ez = pb.z - pa.z;
                double px = ey * nz - ez * ny, py = ez * nx - ex * nz, pz = ex * ny - ey * nx;
                const double plength = std::sqrt(px * px + py * py + pz * pz);
                if (plength <= 0)
                    continue;
                px /= plength; py /= plength; pz /= plength;
                const double pd = -(px * pa.x + py * pa.y + pz * pa.z);
                const double w = (ex * ex + ey * ey + ez * ez) * borderWeight;
                m_quadrics[a].AddPlane(px, py, pz, pd, w);
                m_quadrics[b].AddPlane(px, py, pz, pd, w);
            }
        }
    }

    void buildAdjacency()
    {
        m_adjOffsets.assign(m_vertices.size() + 1, 0);
        for (const std::uint32_t c : m_corners)
            ++m_adjOffsets[c + 1];
        for (std::size_t i = 1; i < m_adjOffsets.size(); ++i)
            m_adjOffsets[i] += m_adjOffsets[i - 1];
        m_adjTriangles.resize(m_indices.size());
        m_fill.assign(m_adjOffsets.begin(), m_adjOffsets.end() - 1);
        for (std::size_t i = 0; i < m_corners.size(); ++i)
            m_adjTriangles[m_fill[m_corners[i]]++] = static_cast<std::uint32_t>(i / 3);
    }

    [[nodiscard]] std::span<const std::uint32_t> trianglesOf(std::uint32_t c) const noexcept
    {
        return std::span<const std::uint32_t>{m_adjTriangles}.subspan(m_adjOffsets[c], m_adjOffsets[c + 1] - m_adjOffsets[c]);
    }
    [[nodiscard]] std::uint32_t corner(std::uint32_t triangle, int k) const noexcept
    {
        return m_corners[triangle * 3 + k];
    }
    [[nodiscard]] bool hasCorner(std::uint32_t triangle, std::uint32_t c) const noexcept
    {
        return corner(triangle, 0) == c || corner(triangle, 1) == c || corner(triangle, 2) == c;
    }

    // 링크 조건: a와 b 둘 다의 이웃인 정점이 변 a-b를 공유하는 삼각형의 맞은편 정점뿐이어야 한다.
    // 아니면 접은 뒤 같은 변을 세 면이 공유하는(비다양체) 모양이 생긴다.
    [[nodiscard]] bool linkConditionHolds(std::uint32_t a, std::uint32_t b) const
    {
        std::array<std::uint32_t, 64> neighborsA{};
        std::size_t countA = 0;
        int shared = 0;
        for (const auto t : trianglesOf(a))
        {
            if (hasCorner(t, b))
                ++shared;
            for (int k = 0; k < 3; ++k)
            {
                const auto c = corner(t, k);
                if (c == a || c == b)
                    continue;
                if (std::find(neighborsA.begin(), neighborsA.begin() + countA, c) == neighborsA.begin() + countA)
                {
                    if (countA == neighborsA.size())
                        return false; // 정점 차수가 비정상적으로 크다. 건드리지 않는다.
                    neighborsA[countA++] = c;
                }
            }
        }
        // 내부 변은 삼각형 2개, 경계 변은 1개가 공유한다. 공통 이웃 수도 그와 같아야 한다.
        const int expected = isBorderEdge(a, b) ? 1 : 2;
        if (shared != expected)
            return false;
        int common = 0;
        std::array<std::uint32_t, 64> seen{};
        std::size_t seenCount = 0;
        for (const auto t : trianglesOf(b))
            for (int k = 0; k < 3; ++k)
            {
                const auto c = corner(t, k);
                if (c == a || c == b || std::find(seen.begin(), seen.begin() + seenCount, c) != seen.begin() + seenCount)
                    continue;
                if (seenCount < seen.size())
                    seen[seenCount++] = c;
                if (std::find(neighborsA.begin(), neighborsA.begin() + countA, c) != neighborsA.begin() + countA)
                    ++common;
            }
        return common == expected;
    }

    // a를 b 위치로 옮겼을 때 a 주변 삼각형(b를 포함하지 않는 것)이 뒤집히거나 납작해지면 거절한다.
    [[nodiscard]] bool flipsTriangles(std::uint32_t a, std::uint32_t b) const noexcept
    {
        const auto &target = m_vertices[b].position;
        for (const auto t : trianglesOf(a))
        {
            if (hasCorner(t, b))
                continue;
            std::array<SRMath::vec3, 3> p{};
            for (std::size_t k = 0; k < p.size(); ++k)
                p[k] = m_vertices[m_indices[t * 3 + k]].position;
            const auto before = Cross(p[1] - p[0], p[2] - p[0]);
            // 옮길 꼭짓점 번호를 변수로 들고 있다가 p[moved]로 쓰면 MSVC /analyze가 범위를 증명하지 못한다(C28020).
            for (std::size_t k = 0; k < p.size(); ++k)
                if (corner(t, static_cast<int>(k)) == a)
                    p[k] = target;
            const auto after = Cross(p[1] - p[0], p[2] - p[0]);
            // 법선이 약 78도 넘게 돌거나 면적이 0이 되면 모양이 무너진 것으로 본다.
            const float lengths = std::sqrt(Dot(before, before) * Dot(after, after));
            if (!(Dot(before, after) > 0.2f * lengths))
                return true;
        }
        return false;
    }

    // a 위치의 정점(wedge)마다 b 위치 정점 중 법선 방향이 가장 가까운 것으로 옮긴다.
    // a와 b 모두 UV 이음새가 아니므로 UV는 같고, 법선이 갈리는 각진 모서리는 같은 쪽 면의 법선을 따라간다.
    void mapWedges(std::uint32_t a, std::uint32_t b) noexcept
    {
        std::uint32_t wa = a;
        do
        {
            const auto &na = m_vertices[wa].normal;
            std::uint32_t best = b;
            float bestCos = -2.f;
            std::uint32_t wb = b;
            do
            {
                const auto &nb = m_vertices[wb].normal;
                const float lengths = std::sqrt(Dot(na, na) * Dot(nb, nb));
                const float cosine = lengths > 0.f ? Dot(na, nb) / lengths : -1.f;
                if (cosine > bestCos)
                {
                    bestCos = cosine;
                    best = wb;
                }
                wb = m_wedgeNext[wb];
            } while (wb != b);
            m_vertexTo[wa] = best;
            m_touched.push_back(wa);
            wa = m_wedgeNext[wa];
        } while (wa != a);
    }

    // 비용이 낮은 접기부터 서로 겹치지 않게 한 번에 여러 개 적용한다. 접은 수가 0이면 false.
    bool runPass(std::size_t targetTriangles)
    {
        buildAdjacency();
        m_collapses.clear();
        for (std::size_t i = 0; i < m_indices.size(); i += 3)
            for (int k = 0; k < 3; ++k)
            {
                const auto a = m_corners[i + k], b = m_corners[i + (k + 1) % 3];
                if (a > b)
                    continue; // 내부 변은 양쪽 삼각형에 한 번씩 나온다. 한 번만 본다.
                const bool aMovable = canCollapse(a, b), bMovable = canCollapse(b, a);
                if (!aMovable && !bMovable)
                    continue;
                const double ab = aMovable ? Quadric::Error(m_quadrics[a], m_quadrics[b], m_vertices[b].position) : 1e300;
                const double ba = bMovable ? Quadric::Error(m_quadrics[a], m_quadrics[b], m_vertices[a].position) : 1e300;
                m_collapses.push_back(ab <= ba ? Collapse{a, b, ab} : Collapse{b, a, ba});
            }
        // 내부 변 하나를 접으면 삼각형이 2개(경계 변은 1개) 줄어든다. 목표를 크게 넘지 않도록 한 패스의 접기 수를 제한한다.
        const std::size_t triangles = m_indices.size() / 3;
        const std::size_t budget = std::max<std::size_t>(1, (triangles - targetTriangles) / 2);

        // 정렬은 8바이트 키 하나로 한다: 위 32비트 = float로 줄인 비용(0 이상인 float는 비트 순서 = 크기 순서),
        // 아래 32비트 = 후보 번호. 비용이 같으면 후보 번호(삼각형 순서)로 갈리므로 어느 컴파일러에서나 같은 순서다.
        // 24바이트 구조체를 3단 비교로 정렬하던 것보다 비교·이동이 싸다.
        m_order.resize(m_collapses.size());
        for (std::size_t i = 0; i < m_collapses.size(); ++i)
            m_order[i] = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(static_cast<float>(m_collapses[i].cost)))
                             << 32 |
                         i;
        // 한 패스가 쓰는 것은 앞쪽 일부(접기 수 한도의 몇 배)뿐이다. 전부 정렬하지 않고 그만큼씩
        // nth_element로 앞으로 모아 그 부분만 정렬한다(모자라면 다음 묶음).
        const std::size_t chunk = budget * 4;
        std::size_t sortedEnd = 0;
        m_vertexTo.resize(m_vertices.size(), invalidVertex);
        m_passLocked.resize(m_vertices.size(), 0);
        m_touched.clear();
        std::size_t applied = 0;
        for (std::size_t index = 0; index < m_order.size(); ++index)
        {
            if (index == sortedEnd)
            {
                sortedEnd = std::min(m_order.size(), index + chunk);
                std::nth_element(m_order.begin() + index, m_order.begin() + sortedEnd, m_order.end());
                std::sort(m_order.begin() + index, m_order.begin() + sortedEnd);
            }
            const Collapse &c = m_collapses[static_cast<std::uint32_t>(m_order[index])];
            if (applied >= budget || c.cost > m_errorLimit)
                break;
            if (m_passLocked[c.from] || m_passLocked[c.to] || !canCollapse(c.from, c.to))
                continue;
            if (!linkConditionHolds(c.from, c.to) || flipsTriangles(c.from, c.to))
                continue;
            mapWedges(c.from, c.to);
            m_quadrics[c.to].Add(m_quadrics[c.from]);
            if (m_kind[c.from] == Kind::Border)
            {
                // 고리에서 from을 뺀다: other - from - to 가 other - to 가 된다.
                const auto other = otherBorderLink(c.from, c.to);
                auto &toLinks = m_borderLinks[c.to];
                toLinks[toLinks[0] == c.from ? 0 : 1] = other;
                auto &otherLinks = m_borderLinks[other];
                otherLinks[otherLinks[0] == c.from ? 0 : 1] = c.to;
            }
            m_kind[c.from] = Kind::Locked; // 사라진 정점
            // 같은 패스의 다른 접기가 이 접기의 삼각형을 보고 판정했다면 그 판정이 낡는다. 주변을 잠근다.
            for (const auto t : trianglesOf(c.from))
                for (int k = 0; k < 3; ++k)
                {
                    const auto locked = corner(t, k);
                    if (!m_passLocked[locked])
                    {
                        m_passLocked[locked] = 1;
                        m_touched.push_back(locked);
                    }
                }
            m_maxError = std::max(m_maxError, c.cost);
            ++applied;
        }
        if (applied == 0)
        {
            resetTouched();
            return false;
        }

        // 접힌 정점을 새 정점으로 바꾸고 퇴화 삼각형(변 a-b를 공유하던 두 개)을 지운다. 남는 순서는 원본 순서다.
        std::size_t write = 0;
        for (std::size_t i = 0; i < m_indices.size(); i += 3)
        {
            std::array<unsigned int, 3> v{m_indices[i], m_indices[i + 1], m_indices[i + 2]};
            for (auto &x : v)
                if (const auto to = m_vertexTo[x]; to != invalidVertex)
                    x = to;
            const std::array<std::uint32_t, 3> c{m_remap[v[0]], m_remap[v[1]], m_remap[v[2]]};
            if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2])
                continue;
            for (int k = 0; k < 3; ++k)
            {
                m_indices[write + k] = v[k];
                m_corners[write + k] = c[k];
            }
            write += 3;
        }
        m_indices.resize(write);
        m_corners.resize(write);
        resetTouched();
        return true;
    }

    // m_vertexTo와 m_passLocked를 다음 패스를 위해 되돌린다. 두 배열 모두 번호 범위가 같아(정점 수) 한 목록으로 충분하다.
    void resetTouched() noexcept
    {
        for (const auto i : m_touched)
        {
            m_vertexTo[i] = invalidVertex;
            m_passLocked[i] = 0;
        }
    }
};
} // namespace

namespace sr::lod
{
std::vector<std::vector<std::uint8_t>> FindSharedPositions(std::span<const Mesh> meshes)
{
    constexpr std::uint32_t shared = invalidVertex - 1;
    std::size_t vertexCount = 0;
    for (const auto &mesh : meshes)
        vertexCount += mesh.vertices.size();
    PositionTable owner(vertexCount); // 위치 → 처음 쓴 메시 번호, 둘 이상이면 shared
    for (std::uint32_t m = 0; m < meshes.size(); ++m)
        for (const auto &vertex : meshes[m].vertices)
        {
            auto [first, inserted] = owner.TryEmplace(KeyOf(vertex.position), m);
            if (!inserted && first != m)
                first = shared;
        }
    std::vector<std::vector<std::uint8_t>> locked(meshes.size());
    for (std::uint32_t m = 0; m < meshes.size(); ++m)
    {
        locked[m].resize(meshes[m].vertices.size());
        for (std::size_t v = 0; v < meshes[m].vertices.size(); ++v)
            locked[m][v] = owner.TryEmplace(KeyOf(meshes[m].vertices[v].position), m).first == shared ? 1 : 0;
    }
    return locked;
}

std::vector<MeshLod> BuildLodChain(std::span<const Vertex> vertices, std::span<const unsigned int> indices,
                                   const LodChainSettings &settings, std::span<const std::uint8_t> lockedVertices)
{
    if (indices.size() / 3 < settings.minTriangles || settings.triangleRatios.empty())
        return {};
    Simplifier simplifier(vertices, indices, lockedVertices);
    return simplifier.Run(settings);
}
} // namespace sr::lod
