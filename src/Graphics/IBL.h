#pragma once
#include "Graphics/Texture.h"
#include "Math/SRMath.h"
#include <array>
#include <memory>
#include <cmath>
#include <algorithm>

namespace sr {

// 2차 구면 조화 함수 (L2 Spherical Harmonics, 9개 기저 계수)
// Ramamoorthi & Hanrahan (2001) "An Efficient Representation for Irradiance Environment Maps"
struct SphericalHarmonicsL2
{
    // 9개 계수 (각각 RGB 3성분, 총 27 floats)
    // 인덱스: 0: L00, 1: L1-1, 2: L10, 3: L11, 4: L2-2, 5: L2-1, 6: L20, 7: L21, 8: L22
    std::array<SRMath::vec3, 9> coeffs{};

    // 임의의 법선 벡터 n에 대한 확산 조도 E(n) 평가 (O(1), 부동소수점 곱/합 15회)
    [[nodiscard]] SRMath::vec3 Evaluate(const SRMath::vec3 &n) const noexcept
    {
        constexpr float c1 = 0.429043f;
        constexpr float c2 = 0.511664f;
        constexpr float c3 = 0.743125f;
        constexpr float c4 = 0.886227f;
        constexpr float c5 = 0.247708f;

        const float x = n.x;
        const float y = n.y;
        const float z = n.z;

        // +Y가 월드 Up인 좌표계 다항식 평가
        SRMath::vec3 result = coeffs[8] * (c1 * (x * x - z * z))
                            + coeffs[6] * (c3 * (y * y) - c5)
                            + coeffs[0] * c4
                            + (coeffs[4] * (x * z) + coeffs[7] * (x * y) + coeffs[5] * (y * z)) * (2.0f * c1)
                            + (coeffs[3] * x + coeffs[1] * z + coeffs[2] * y) * (2.0f * c2);

        result.x = std::max(0.0f, result.x);
        result.y = std::max(0.0f, result.y);
        result.z = std::max(0.0f, result.z);
        return result;
    }

    // 자연스러운 야외 하늘/대지 그라데이션 기본 SH 계수 생성
    static SphericalHarmonicsL2 CreateDefaultSky() noexcept;
};

// 6면체 큐브맵 텍스처 (PX, NX, PY, NY, PZ, NZ)
class CubeMap
{
  public:
    enum Face : int
    {
        PositiveX = 0, // Right
        NegativeX = 1, // Left
        PositiveY = 2, // Top (Up)
        NegativeY = 3, // Bottom (Down)
        PositiveZ = 4, // Front
        NegativeZ = 5  // Back
    };

  private:
    std::array<Texture, 6> m_faces;

  public:
    CubeMap() = default;
    explicit CubeMap(std::array<Texture, 6> faces);

    void SetFace(int faceIndex, Texture texture) noexcept
    {
        if (faceIndex >= 0 && faceIndex < 6)
            m_faces[static_cast<std::size_t>(faceIndex)] = std::move(texture);
    }

    [[nodiscard]] const Texture &GetFace(int faceIndex) const noexcept
    {
        return m_faces[static_cast<std::size_t>(std::clamp(faceIndex, 0, 5))];
    }

    [[nodiscard]] Texture &GetFace(int faceIndex) noexcept
    {
        return m_faces[static_cast<std::size_t>(std::clamp(faceIndex, 0, 5))];
    }

    // 3차원 방향 벡터 dir로부터 큐브맵 면(Face) 및 정규화 UV 좌표(0~1) 산출
    static void DirectionToFaceUV(const SRMath::vec3 &dir, int &faceIndex, float &u, float &v) noexcept
    {
        const float absX = std::abs(dir.x);
        const float absY = std::abs(dir.y);
        const float absZ = std::abs(dir.z);

        if (absX >= absY && absX >= absZ)
        {
            faceIndex = (dir.x >= 0.0f) ? PositiveX : NegativeX;
            u = (dir.x >= 0.0f) ? -dir.z : dir.z;
            v = -dir.y;
            const float inv = (absX > 1e-6f) ? (0.5f / absX) : 0.5f;
            u = u * inv + 0.5f;
            v = v * inv + 0.5f;
        }
        else if (absY >= absX && absY >= absZ)
        {
            faceIndex = (dir.y >= 0.0f) ? PositiveY : NegativeY;
            u = dir.x;
            v = (dir.y >= 0.0f) ? dir.z : -dir.z;
            const float inv = (absY > 1e-6f) ? (0.5f / absY) : 0.5f;
            u = u * inv + 0.5f;
            v = v * inv + 0.5f;
        }
        else
        {
            faceIndex = (dir.z >= 0.0f) ? PositiveZ : NegativeZ;
            u = (dir.z >= 0.0f) ? dir.x : -dir.x;
            v = -dir.y;
            const float inv = (absZ > 1e-6f) ? (0.5f / absZ) : 0.5f;
            u = u * inv + 0.5f;
            v = v * inv + 0.5f;
        }
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);
    }

    // 방향 및 LOD에 따른 선형 보간 샘플링
    [[nodiscard]] SRMath::Color Sample(const SRMath::vec3 &dir, float lod = 0.0f) const noexcept
    {
        int faceIndex = 0;
        float u = 0.5f, v = 0.5f;
        DirectionToFaceUV(dir, faceIndex, u, v);
        return m_faces[static_cast<std::size_t>(faceIndex)].Sample(u, v, lod);
    }

    void GenerateMipmaps()
    {
        for (auto &face : m_faces)
            face.GenerateMipmaps();
    }

    [[nodiscard]] int GetMaxLOD() const noexcept
    {
        return std::max(0, m_faces[0].GetMipCount() - 1);
    }

    // 자연스러운 스튜디오/야외 환경광 절차적 큐브맵 생성 (기본 해상도 64x64/면)
    static std::shared_ptr<CubeMap> CreateDefaultStudio(int faceSize = 64);
};

// IBL 맵 (구면 조화 함수 확산광 + 사전 필터링된 환경 큐브맵 반사광)
class IBLMap
{
  public:
    SphericalHarmonicsL2 sh;
    std::shared_ptr<CubeMap> prefilteredEnvMap;

    IBLMap() = default;
    IBLMap(SphericalHarmonicsL2 inSh, std::shared_ptr<CubeMap> inEnvMap)
        : sh(inSh), prefilteredEnvMap(std::move(inEnvMap))
    {
    }

    // 반사 벡터 R과 거칠기 roughness에 따른 환경 반사색 샘플링
    [[nodiscard]] SRMath::vec3 SampleSpecular(const SRMath::vec3 &R, float roughness) const noexcept
    {
        if (!prefilteredEnvMap)
            return {0.0f, 0.0f, 0.0f};
        const float maxLod = static_cast<float>(prefilteredEnvMap->GetMaxLOD());
        const float lod = std::clamp(roughness, 0.0f, 1.0f) * maxLod;
        const auto color = prefilteredEnvMap->Sample(R, lod);
        return {color.r, color.g, color.b};
    }

    // 기본 스튜디오/야외 IBL 맵 생성
    static std::shared_ptr<IBLMap> CreateDefault();
};

// Karis 2014 해석적 BRDF 적분 근사 (Split-Sum Term 2)
// 반환값: x = Scale (A), y = Bias (B)
inline SRMath::vec2 EnvBRDFApprox(float NdotV, float roughness) noexcept
{
    const SRMath::vec4 c0{-1.0f, -0.0275f, -0.572f, 0.022f};
    const SRMath::vec4 c1{1.0f, 0.0425f, 1.04f, -0.04f};
    const SRMath::vec4 r = roughness * c0 + c1;
    const float a004 = std::min(r.x * r.x, std::exp2(-9.28f * NdotV)) * r.x + r.y;
    const auto res = SRMath::vec2(-1.04f, 0.04f) * a004 + SRMath::vec2(r.z, r.w);
    return {std::max(0.0f, res.x), std::max(0.0f, res.y)};
}

// ACES Narkowicz Film Tone Mapping
inline SRMath::vec3 ACESFilm(const SRMath::vec3 &x) noexcept
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    auto map = [a, b, c, d, e](float v) {
        v = std::max(0.0f, v);
        return std::clamp((v * (a * v + b)) / (v * (c * v + d) + e), 0.0f, 1.0f);
    };
    return {map(x.x), map(x.y), map(x.z)};
}

} // namespace sr
