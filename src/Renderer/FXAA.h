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
    const float EDGE_THRESHOLD = 0.08f;        // 경계선 감지 민감도 (낮을수록 민감)
    const float EDGE_THRESHOLD_MIN = 0.03125f; // 어두운 곳에서의 최소 민감도
    const int MAX_SPAN = 16;                   // 경계선 최대 탐색 거리
    const float EPS = 1e-6f;                   // 0에 가까운 값 비교용
	const float BLEND_FACTOR = 0.2f;           // 블렌딩 강도 조절 (0.0 ~ 1.0)

    // lumaBuffer를 병렬 영역 '바깥'에서 선언 (올바른 구조)
    std::vector<float> lumaBuffer(width * height);

#pragma omp parallel
    {
        // 1. 휘도(Luma) 맵 만들기 (기존과 동일)
#pragma omp for schedule(static)
        for (int i = 0; i < width * height; ++i) {
            SRMath::Color tempColor;
            unpackColor(inBuffer[i], tempColor);
            lumaBuffer[i] = RGBToLuma(tempColor);
        }

        // --- 모든 픽셀을 순회 ---
#pragma omp for schedule(static)
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                const int idx = y * width + x;

                // 2. 경계선 감지 (기존과 동일)
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

                // 3. 경계선 방향 탐지 (기존과 동일)
                const float diffH = std::abs(lW - lE);
                const float diffV = std::abs(lN - lS);
                const bool isVerticalEdge = (diffV >= diffH); // 이름 명확화: isVerticalEdge

                // 4. 경계선 끝점 탐색 (기존과 동일)
                const float gradient = isVerticalEdge ? diffV : diffH;
                if (gradient <= EPS) { outBuffer[idx] = inBuffer[idx]; continue; }
                float negDist = 0.0f, posDist = 0.0f;
                // (-) 방향 탐색
                for (int i = 0; i < MAX_SPAN; ++i) {
                    const int nx = isVerticalEdge ? x : x - i - 1;
                    const int ny = isVerticalEdge ? y - i - 1 : y;
                    if (nx < 0 || ny < 0) break;
                    const float dl = std::abs(lumaBuffer[ny * width + nx] - lC);
                    if (dl / gradient >= 0.4f) break; // 0.5f 보다 약간 관대한 기준
                    negDist = float(i + 1);
                }
                // (+) 방향 탐색
                for (int i = 0; i < MAX_SPAN; ++i) {
                    const int nx = isVerticalEdge ? x : x + i + 1;
                    const int ny = isVerticalEdge ? y + i + 1 : y;
                    if (nx >= width || ny >= height) break;
                    const float dl = std::abs(lumaBuffer[ny * width + nx] - lC);
                    if (dl / gradient >= 0.4f) break;
                    posDist = float(i + 1);
                }

                // =======================================================================
                // 5. 최종 블렌딩 (이 부분이 표준 FXAA 방식으로 변경됨)
                // =======================================================================
                const float edgeLength = posDist + negDist;
                if (edgeLength < 1.0f) { // 유의미한 길이가 아니면 무시
                    outBuffer[idx] = inBuffer[idx];
                    continue;
                }

                // 5-1. 경계선의 중심이 현재 픽셀 중심에서 얼마나 벗어났는지 계산
                const float pixelOffset = (posDist - negDist) / edgeLength; // 결과: [-1.0 ~ 1.0]

                // 5-2. 블렌딩 강도 계산
                // pixelOffset의 BLEND_FACTOR만큼 이동시킨다고 가정
                const float blendFactor = BLEND_FACTOR * std::abs(pixelOffset);

                // 5-3. 섞을 이웃 픽셀 선택 (경계선에 '수직인' 방향)
                int neighborIdx;
                if (isVerticalEdge) { // 수직 경계선 -> 위/아래 픽셀과 섞음
                    neighborIdx = pixelOffset > 0 ? (y - 1) * width + x : (y + 1) * width + x;
                }
                else { // 수평 경계선 -> 좌/우 픽셀과 섞음
                    neighborIdx = pixelOffset > 0 ? (y * width + x - 1) : (y * width + x + 1);
                }

                // 5-4. 최종 색상 계산
                SRMath::Color colorCenter, neighborColor;
                unpackColor(inBuffer[idx], colorCenter);
                unpackColor(inBuffer[neighborIdx], neighborColor);

                const SRMath::Color finalColor = LerpColor(colorCenter, neighborColor, blendFactor);
                outBuffer[idx] = packColor(finalColor);
            }
        }
    }

    // 가장자리는 원본 복사
    for (int y = 0; y < height; ++y) {
        outBuffer[y * width] = inBuffer[y * width];
        outBuffer[y * width + width - 1] = inBuffer[y * width + width - 1];
    }
    for (int x = 0; x < width; ++x) {
        outBuffer[x] = inBuffer[x];
        outBuffer[(height - 1) * width + x] = inBuffer[(height - 1) * width + x];
    }
}