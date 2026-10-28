#pragma once
#include "Math/SRMath.h"
#include <cstdint>
#include <span>

struct DirectionalLight;
struct PointLight;
struct SpotLight;
struct ShadowMap;

enum class ELineAlgorithm
{
    Bresenham,
    DDA
};
enum class EAAAlgorithm
{
    None,
    FXAA
};
enum class RasterPass
{
    Shaded,
    DepthOnly,
    ShadeVisible,
    // 깊이/ID만 갱신하고 색을 쓸 삼각형을 픽셀마다 기록한다. 셰이딩은 ShadeOwners에서 픽셀당 한 번 한다.
    DepthOwner
};

struct TileBounds
{
    // 반개구간이다. 서로 다른 tile worker의 쓰기 영역은 겹치지 않는다.
    int minX, minY, maxX, maxY;
};

// 입력 광원의 방향/색을 한 프레임 동안 고정한 셰이딩 전용 값이다.
// 픽셀과 무관한 정규화를 프레임 준비로 옮기되 원래 부호와 normalize 식을 유지한다.
struct PreparedDirectionalLight
{
    SRMath::vec3 directionToLight;
    SRMath::vec3 color;

    [[nodiscard]] static PreparedDirectionalLight From(const DirectionalLight &light) noexcept;
};

struct PreparedPointLight
{
    SRMath::vec3 position;
    SRMath::vec3 color;
    float invRange = 0.f;
    float constant = 1.f, linear = 0.09f, quadratic = 0.032f;

    [[nodiscard]] static PreparedPointLight From(const PointLight &light) noexcept;
};

struct PreparedSpotLight
{
    SRMath::vec3 position;
    SRMath::vec3 directionToLight;
    SRMath::vec3 color;
    float invRange = 0.f;
    float innerConeCos = 0.9063f;
    float outerConeCos = 0.8191f;
    float invConeCosDiff = 1.f;
    float constant = 1.f, linear = 0.09f, quadratic = 0.032f;

    [[nodiscard]] static PreparedSpotLight From(const SpotLight &light) noexcept;
};

struct RenderFrameContext
{
    SRMath::mat4 viewProjection;
    SRMath::vec3 cameraPosition;
    // 준비 저장소는 동기 프레임 호출과 모든 worker join이 끝날 때까지 유효해야 한다.
    std::span<const PreparedDirectionalLight> lights;
    int tilesX = 0, tilesY = 0;
    std::span<const PreparedPointLight> pointLights{};
    std::span<const PreparedSpotLight> spotLights{};
    bool bilinearFiltering = false;
    bool mipmapping = false;
    bool srgbGamma = false;
    const ShadowMap *shadowMap = nullptr;
};

struct RenderOptions
{
    // 정렬/Hi-Z는 장면에 따라 비용이 더 클 수 있어 명시적으로 선택한다.
    bool frontToBack = false;
    bool hiZ = false;
    bool collectStats = false;
    // 기본 와이어는 기존 X-ray 표시다. 숨은 선 제거를 선택한 경우에만 면 깊이를 만든다.
    bool hiddenWireframe = false;
    bool bilinearFiltering = false;
    bool mipmapping = false;
    bool srgbGamma = false;
    bool shadows = false;
};

struct RenderStats
{
    // binReferences는 논리적인 triangle→tile 참조 수다. private/병합 복사본을 두 번 세지 않는다.
    std::uint64_t preparedTriangles = 0, binReferences = 0;
    std::uint64_t coveragePassed = 0, depthRejected = 0, shadedPixels = 0;
    std::uint64_t finalFilledPixels = 0, hiZTests = 0, hiZRejected = 0, lineDepthRejected = 0;
    // retainedBinBytes: private flat 참조/최종 포인터/wire/debug bin capacity와
    // count·cursor·prefix offset·컨테이너 헤더·touched/활성 worker 목록.
    // allocator 내부 오버헤드 및 RSS는 별도다.
    std::uint64_t liveTriangleBytes = 0, retainedTriangleBytes = 0, retainedBinBytes = 0;
    std::uint64_t liveDebugLineBytes = 0, retainedDebugLineBytes = 0;
    // binMs는 private frontend, rasterMs에는 prefix/scatter/타일 정렬도 포함된다.
    double prepareMs = 0, binMs = 0, rasterMs = 0, lineMs = 0, postMs = 0;

    // 각 worker가 자기 카운터에 기록한 뒤 join 경계에서만 합한다.
    void MergeCounters(const RenderStats &other) noexcept
    {
        coveragePassed += other.coveragePassed;
        depthRejected += other.depthRejected;
        shadedPixels += other.shadedPixels;
        hiZTests += other.hiZTests;
        hiZRejected += other.hiZRejected;
        lineDepthRejected += other.lineDepthRejected;
    }
};
