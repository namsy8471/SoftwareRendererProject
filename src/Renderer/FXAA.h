#pragma once
#include "Math/SRMath.h"
#include <limits>
#include <tbb/tbb.h>

// 32��Ʈ unsigned int(0x00BBGGRR)�� Color ����ü�� ��ȯ
inline void unpackColor(unsigned int p, SRMath::Color& c) {
    c.b = static_cast<float>((p >> 16) & 0xFF) / 255.0f;
    c.g = static_cast<float>((p >> 8) & 0xFF) / 255.0f;
    c.r = static_cast<float>(p & 0xFF) / 255.0f;
}

// Color ����ü�� 32��Ʈ unsigned int(0x00BBGGRR)�� ��ȯ
inline unsigned int packColor(const SRMath::Color& c) {
    unsigned int r = static_cast<unsigned int>(std::min(c.r, 1.0f) * 255.0f);
    unsigned int g = static_cast<unsigned int>(std::min(c.g, 1.0f) * 255.0f);
    unsigned int b = static_cast<unsigned int>(std::min(c.b, 1.0f) * 255.0f);
    return (b << 16) | (g << 8) | r;
}

// ���� ����
SRMath::Color LerpColor(const SRMath::Color& a, const SRMath::Color& b, float t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t
    };
}

float RGBToLuma(const SRMath::Color& color) {
	// NTSC ǥ�� ����ġ
	return color.r * 0.299f + color.g * 0.587f + color.b * 0.114f;
}

void ApplyFXAA(const unsigned int* inBuffer, unsigned int* outBuffer, int width, int height) {
    // --- Ʃ�� ������ ����� ---
    const float EDGE_THRESHOLD = 0.08f;        // ��輱 ���� �ΰ��� (�������� �ΰ�)
    const float EDGE_THRESHOLD_MIN = 0.03125f; // ��ο� �������� �ּ� �ΰ���
    const int MAX_SPAN = 16;                   // ��輱 �ִ� Ž�� �Ÿ�
    const float EPS = 1e-6f;                   // 0�� ����� �� �񱳿�
	const float BLEND_FACTOR = 0.2f;           // ����� ���� ���� (0.0 ~ 1.0)

    // lumaBuffer�� ���� ���� '�ٱ�'���� ���� (�ùٸ� ����)
    std::vector<float> lumaBuffer(width * height);


    // --- ��� �ȼ��� ��ȸ ---
    tbb::parallel_for(tbb::blocked_range(1, height - 1),
        [&](const tbb::blocked_range<int>& r) {

            //    Luma ��� ���� ����: ���� �����尡 ���� ��(r)�� ���Ʒ� 1�ȼ����� ����
            //    FXAA ���� �� �̿� �ȼ��� luma ���� �ʿ��ϱ� �����Դϴ�.
            const int luma_y_start = std::max(0, r.begin() - 1);
            const int luma_y_end = std::min(height, r.end() + 1);

            //   '�ʿ��� ��ŭ��' Luma �� �����
            //    �� �����ʹ� ���� �������� CPU ĳ�ÿ� ����� Ȯ���� �ſ� ����ϴ�.
            for (int y = luma_y_start; y < luma_y_end; ++y) {
                for (int x = 0; x < width; ++x) {
                    SRMath::Color tempColor;
                    unpackColor(inBuffer[y * width + x], tempColor);
                    lumaBuffer[y * width + x] = RGBToLuma(tempColor);
                }
            }

            for (int y = r.begin(); y < r.end(); ++y) {
                for (int x = 1; x < width - 1; ++x) {
                    const int idx = y * width + x;

                    // 2. ��輱 ���� (������ ����)
                    const float lC = lumaBuffer[idx];
                    const float lN = lumaBuffer[(y - 1) * width + x];
                    const float lS = lumaBuffer[(y + 1) * width + x];
                    const float lW = lumaBuffer[y * width + (x - 1)];
                    const float lE = lumaBuffer[y * width + (x + 1)];

                    const float lMin = std::min(lC, std::min(lN, (std::min(lS, std::min(lW, lE)))));
                    const float lMax = std::max(lC, std::max(lN, (std::max(lS, std::max(lW, lE)))));
                    const float contrast = lMax - lMin;
                    const float thresh = std::max(EDGE_THRESHOLD_MIN, lMax * EDGE_THRESHOLD);
                    if (contrast < thresh) { outBuffer[idx] = inBuffer[idx]; continue; }

                    // 3. ��輱 ���� Ž�� (������ ����)
                    const float diffH = std::abs(lW - lE);
                    const float diffV = std::abs(lN - lS);
                    const bool isVerticalEdge = (diffV >= diffH); // �̸� ��Ȯȭ: isVerticalEdge

                    // 4. ��輱 ���� Ž�� (������ ����)
                    const float gradient = isVerticalEdge ? diffV : diffH;
                    if (gradient <= EPS) { outBuffer[idx] = inBuffer[idx]; continue; }
                    float negDist = 0.0f, posDist = 0.0f;
                    // (-) ���� Ž��
                    for (int i = 0; i < MAX_SPAN; ++i) {
                        const int nx = isVerticalEdge ? x : x - i - 1;
                        const int ny = isVerticalEdge ? y - i - 1 : y;
                        if (nx < 0 || ny < 0) break;
                        const float dl = std::abs(lumaBuffer[ny * width + nx] - lC);
                        if (dl / gradient >= 0.4f) break; // 0.5f ���� �ణ ������ ����
                        negDist = float(i + 1);
                    }
                    // (+) ���� Ž��
                    for (int i = 0; i < MAX_SPAN; ++i) {
                        const int nx = isVerticalEdge ? x : x + i + 1;
                        const int ny = isVerticalEdge ? y + i + 1 : y;
                        if (nx >= width || ny >= height) break;
                        const float dl = std::abs(lumaBuffer[ny * width + nx] - lC);
                        if (dl / gradient >= 0.4f) break;
                        posDist = float(i + 1);
                    }

                    // =======================================================================
                    // 5. ���� ����� (�� �κ��� ǥ�� FXAA ������� �����)
                    // =======================================================================
                    const float edgeLength = posDist + negDist;
                    if (edgeLength < 1.0f) { // ���ǹ��� ���̰� �ƴϸ� ����
                        outBuffer[idx] = inBuffer[idx];
                        continue;
                    }

                    // 5-1. ��輱�� �߽��� ���� �ȼ� �߽ɿ��� �󸶳� ������� ���
                    const float pixelOffset = (posDist - negDist) / edgeLength; // ���: [-1.0 ~ 1.0]

                    // 5-2. ����� ���� ���
                    // pixelOffset�� BLEND_FACTOR��ŭ �̵���Ų�ٰ� ����
                    const float blendFactor = BLEND_FACTOR * std::abs(pixelOffset);

                    // 5-3. ���� �̿� �ȼ� ���� (��輱�� '������' ����)
                    int neighborIdx;
                    if (isVerticalEdge) { // ���� ��輱 -> ��/�Ʒ� �ȼ��� ����
                        neighborIdx = pixelOffset > 0 ? (y - 1) * width + x : (y + 1) * width + x;
                    }
                    else { // ���� ��輱 -> ��/�� �ȼ��� ����
                        neighborIdx = pixelOffset > 0 ? (y * width + x - 1) : (y * width + x + 1);
                    }

                    // 5-4. ���� ���� ���
                    SRMath::Color colorCenter, neighborColor;
                    unpackColor(inBuffer[idx], colorCenter);
                    unpackColor(inBuffer[neighborIdx], neighborColor);

                    const SRMath::Color finalColor = LerpColor(colorCenter, neighborColor, blendFactor);
                    outBuffer[idx] = packColor(finalColor);
                }
			}});
    

    // �����ڸ��� ���� ����
    for (int y = 0; y < height; ++y) {
        outBuffer[y * width] = inBuffer[y * width];
        outBuffer[y * width + width - 1] = inBuffer[y * width + width - 1];
    }
    for (int x = 0; x < width; ++x) {
        outBuffer[x] = inBuffer[x];
        outBuffer[(height - 1) * width + x] = inBuffer[(height - 1) * width + x];
    }
}