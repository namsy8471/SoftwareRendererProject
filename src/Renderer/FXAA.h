#pragma once
#include "Math/SRMath.h"
#include <limits>
#include <omp.h>

// 32비트 unsigned int(0x00BBGGRR)를 Color 구조체로 변환
inline void unpackColor(unsigned int p, SRMath::Color& c) {
    c.b = static_cast<float>((p >> 16) & 0xFF) / 255.0f;
    c.g = static_cast<float>((p >> 8) & 0xFF) / 255.0f;
    c.r = static_cast<float>(p & 0xFF) / 255.0f;
}

// Color 구조체를 32비트 unsigned int(0x00BBGGRR)로 변환
inline unsigned int packColor(const SRMath::Color& c) {
    unsigned int r = static_cast<unsigned int>(std::min(c.r, 1.0f) * 255.0f);
    unsigned int g = static_cast<unsigned int>(std::min(c.g, 1.0f) * 255.0f);
    unsigned int b = static_cast<unsigned int>(std::min(c.b, 1.0f) * 255.0f);
    return (b << 16) | (g << 8) | r;
}

// 선형 보간
SRMath::Color LerpColor(const SRMath::Color& a, const SRMath::Color& b, float t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t
    };
}

float RGBToLuma(const SRMath::Color& color) {
	// NTSC 표준 가중치
	return color.r * 0.299f + color.g * 0.587f + color.b * 0.114f;
}

void ApplyFXAA(const unsigned int* inBuffer, unsigned int* outBuffer, int width, int height) {
    // --- 튜닝 가능한 상수들 ---
    const float EDGE_THRESHOLD = 0.125f;
    const float EDGE_THRESHOLD_MIN = 0.03125f;
    const int MAX_SPAN = 12;
	const float EPS = 1e-6f; // 작은 값 비교용

    // 휘도(Luma) 버퍼를 미리 계산
    std::vector<float> lumaBuffer(width * height);

#pragma omp parallel
    {    
#pragma omp for schedule(static)
        for (int i = 0; i < width * height; ++i) {
            SRMath::Color tempColor;
            unpackColor(inBuffer[i], tempColor); // Unpack
            lumaBuffer[i] = RGBToLuma(tempColor);
        }

        // --- 모든 픽셀을 순회 ---
        #pragma omp for schedule(static)
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {

                const int idx = y * width + x;

                const float lC = lumaBuffer[idx];
                const float lN = lumaBuffer[(y - 1) * width + x];
                const float lS = lumaBuffer[(y + 1) * width + x];
                const float lW = lumaBuffer[y * width + (x - 1)];
                const float lE = lumaBuffer[y * width + (x + 1)];

                const float lMin = std::min({ lC, lN, lS, lW, lE });
                const float lMax = std::max({ lC, lN, lS, lW, lE });
                const float contrast = lMax - lMin;
                const float thresh = std::max(EDGE_THRESHOLD_MIN, lMax * EDGE_THRESHOLD);
                if (contrast < thresh) { outBuffer[idx] = inBuffer[idx]; continue; }

                const float diffH = std::fabs(lW - lE);
                const float diffV = std::fabs(lN - lS);

                // 기울기 수직(=에지 가로)면 x축 방향으로 탐색
                const bool edgeHorizontal = (diffV >= diffH);

                // 기울기 샘플(에지 수직 방향의 두 점)
                const float g1 = edgeHorizontal ? lN : lW;
                const float g2 = edgeHorizontal ? lS : lE;
                const float gradient = std::fabs(g1 - g2);
                if (gradient <= EPS) { outBuffer[idx] = inBuffer[idx]; continue; }

                // 에지 방향(수평/수직)으로 좌우/상하 탐색
                float negDist = 0.0f, posDist = 0.0f;

                // 음(-) 방향
                for (int i = 0; i < MAX_SPAN; ++i) {
                    const int nx = edgeHorizontal ? (x - i - 1) : x;
                    const int ny = edgeHorizontal ? y : (y - i - 1);
                    if (nx < 0 || ny < 0) break;

                    const float dl = std::fabs(lumaBuffer[ny * width + nx] - lC) / gradient;
                    if (dl >= 0.5f) break;
                    negDist = float(i + 1);
                }
                // 양(+) 방향
                for (int i = 0; i < MAX_SPAN; ++i) {
                    const int nx = edgeHorizontal ? (x + i + 1) : x;
                    const int ny = edgeHorizontal ? y : (y + i + 1);
                    if (nx >= width || ny >= height) break;

                    const float dl = std::fabs(lumaBuffer[ny * width + nx] - lC) / gradient;
                    if (dl >= 0.5f) break;
                    posDist = float(i + 1);
                }

                const float edgeLen = negDist + posDist;
                if (edgeLen <= EPS) { outBuffer[idx] = inBuffer[idx]; continue; }

                // 에지 방향의 양 옆 두 픽셀 평균을 샘플로 사용 (간단한 FXAA-like)
                const int eIdx1 = edgeHorizontal ? (y * width + (x - 1))
                    : ((y - 1) * width + x);
                const int eIdx2 = edgeHorizontal ? (y * width + (x + 1))
                    : ((y + 1) * width + x);

                SRMath::Color cC, cE1, cE2;
                unpackColor(inBuffer[idx], cC);
                unpackColor(inBuffer[eIdx1], cE1);
                unpackColor(inBuffer[eIdx2], cE2);
                const SRMath::Color cEdge = LerpColor(cE1, cE2, 0.5f);

                // 블렌드 강도: 에지 길이와 대비 기반 (0~0.5 정도)
                const float spanNorm = std::min(1.0f, edgeLen / float(MAX_SPAN));
                const float strength = std::min(1.0f, (contrast - thresh) / (thresh + EPS));
                const float blend = 0.5f * spanNorm * strength;

                const SRMath::Color finalColor = LerpColor(cC, cEdge, blend);
                outBuffer[idx] = packColor(finalColor);
            }
        }

        // 가장자리는 원본 복사
#pragma omp for schedule(static)
        for (int y = 0; y < height; ++y) {
            outBuffer[y * width] = inBuffer[y * width];
            outBuffer[y * width + width - 1] = inBuffer[y * width + width - 1];
        }
#pragma omp for schedule(static)
        for (int x = 0; x < width; ++x) {
            outBuffer[x] = inBuffer[x];
            outBuffer[(height - 1) * width + x] = inBuffer[(height - 1) * width + x];
        }
    }
}