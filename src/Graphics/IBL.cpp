#include "Graphics/IBL.h"
#include <cmath>
#include <numbers>

namespace sr {

SphericalHarmonicsL2 SphericalHarmonicsL2::CreateDefaultSky() noexcept
{
    SphericalHarmonicsL2 sh;
    // Y-up 자연스러운 야외 하늘/대지 그라데이션 및 부드러운 주광원 계수
    sh.coeffs[0] = {0.451f, 0.496f, 0.564f}; // L00: 평균 주변광
    sh.coeffs[1] = {0.060f, 0.050f, 0.040f}; // L1-1: Z 성분
    sh.coeffs[2] = {0.161f, 0.220f, 0.327f}; // L10: Y 수직 그라데이션 (천정 vs 지면)
    sh.coeffs[3] = {0.080f, 0.070f, 0.060f}; // L11: X 성분
    sh.coeffs[4] = {0.015f, 0.015f, 0.010f}; // L2-2
    sh.coeffs[5] = {0.020f, 0.025f, 0.030f}; // L2-1
    sh.coeffs[6] = {0.040f, 0.045f, 0.050f}; // L20
    sh.coeffs[7] = {0.025f, 0.030f, 0.035f}; // L21
    sh.coeffs[8] = {0.010f, 0.010f, 0.015f}; // L22
    return sh;
}

CubeMap::CubeMap(std::array<Texture, 6> faces) : m_faces(std::move(faces))
{
}

std::shared_ptr<CubeMap> CubeMap::CreateDefaultStudio(int faceSize)
{
    faceSize = std::clamp(faceSize, 16, 256);
    std::array<Texture, 6> faces;

    const SRMath::vec3 sunDir = SRMath::normalize(SRMath::vec3(0.4f, 0.8f, 0.3f));
    const SRMath::vec3 fillDir = SRMath::normalize(SRMath::vec3(-0.5f, 0.3f, -0.4f));

    const SRMath::vec3 zenithColor{0.25f, 0.48f, 0.90f};
    const SRMath::vec3 horizonColor{0.75f, 0.82f, 0.90f};
    const SRMath::vec3 groundColor{0.18f, 0.16f, 0.14f};

    for (int f = 0; f < 6; ++f)
    {
        std::vector<unsigned char> pixels(static_cast<std::size_t>(faceSize * faceSize * 4));

        for (int y = 0; y < faceSize; ++y)
        {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize);

            for (int x = 0; x < faceSize; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize);

                // 면과 UV로부터 3D 방향 벡터 역산
                const float nx = 2.0f * u - 1.0f;
                const float ny = 2.0f * v - 1.0f;
                SRMath::vec3 dir{};
                switch (f)
                {
                case PositiveX: dir = {1.0f, -ny, -nx}; break;
                case NegativeX: dir = {-1.0f, -ny, nx}; break;
                case PositiveY: dir = {nx, 1.0f, ny}; break;
                case NegativeY: dir = {nx, -1.0f, -ny}; break;
                case PositiveZ: dir = {nx, -ny, 1.0f}; break;
                case NegativeZ: dir = {-nx, -ny, -1.0f}; break;
                default: break;
                }
                dir = SRMath::normalize(dir);

                // 하늘/지면 그라데이션
                SRMath::vec3 color{};
                if (dir.y >= 0.0f)
                {
                    const float t = std::pow(dir.y, 0.7f);
                    color = horizonColor * (1.0f - t) + zenithColor * t;
                }
                else
                {
                    const float t = std::pow(-dir.y, 0.7f);
                    color = horizonColor * (1.0f - t) + groundColor * t;
                }

                // 주광원(태양/스튜디오 조명) 하이라이트
                const float sunDot = std::max(0.0f, SRMath::dot(dir, sunDir));
                if (sunDot > 0.0f)
                {
                    const float sunGlow = std::pow(sunDot, 32.0f) * 1.5f;
                    const float sunDisc = std::pow(sunDot, 256.0f) * 3.5f;
                    color += SRMath::vec3(1.0f, 0.95f, 0.85f) * (sunGlow + sunDisc);
                }

                // 보조광(Fill light)
                const float fillDot = std::max(0.0f, SRMath::dot(dir, fillDir));
                if (fillDot > 0.0f)
                {
                    color += SRMath::vec3(0.3f, 0.35f, 0.45f) * std::pow(fillDot, 16.0f);
                }

                // [0, 1] 클램핑 후 RGBA8 저장
                color.clamp(0.0f, 1.0f);
                const std::size_t idx = (static_cast<std::size_t>(y) * faceSize + x) * 4;
                pixels[idx] = static_cast<unsigned char>(color.x * 255.0f);
                pixels[idx + 1] = static_cast<unsigned char>(color.y * 255.0f);
                pixels[idx + 2] = static_cast<unsigned char>(color.z * 255.0f);
                pixels[idx + 3] = 255;
            }
        }

        faces[static_cast<std::size_t>(f)] = Texture(faceSize, faceSize, std::move(pixels));
        faces[static_cast<std::size_t>(f)].SetFilter(TextureFilter::Bilinear);
        faces[static_cast<std::size_t>(f)].GenerateMipmaps();
    }

    return std::make_shared<CubeMap>(std::move(faces));
}

std::shared_ptr<IBLMap> IBLMap::CreateDefault()
{
    auto sh = SphericalHarmonicsL2::CreateDefaultSky();
    auto env = CubeMap::CreateDefaultStudio(64);
    return std::make_shared<IBLMap>(sh, std::move(env));
}

} // namespace sr
