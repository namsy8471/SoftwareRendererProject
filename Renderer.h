#pragma once
#include <vector>
#include "pch.h"
#include "Math.h"

enum class ELineAlgorithm
{
	Bresenham,
	DDA
};

class Renderer
{
private:
	// 백버퍼링에 필요한 GDI 객체 핸들
	HDC     m_hMemDC;       // 백버퍼의 Device Context
	HBITMAP m_hBitmap;      // 백버퍼용 비트맵
	HBITMAP m_hOldBitmap;   // m_hMemDC에 원래 선택되어 있던 비트맵

	int m_width;
	int m_height;

	unsigned int* m_pPixelData;
	std::vector<float> m_depthBuffer;

	ELineAlgorithm m_currentLineAlgorithm = 
		ELineAlgorithm::Bresenham;	// 선 그리기 알고리즘

	void drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color);
	void drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color);

public:
	Renderer();
	~Renderer();

	bool Initialize(HWND hWnd);
	void Shutdown() const;

	void DrawPixel(int x, int y, unsigned int color);
	void DrawLine(int x0, int y0, int x1, int y1, unsigned int color);
	void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color);
	void DrawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color);
	void DrawFilledTriangle(const SRMath::vec2& v0, const SRMath::vec2& v1, const SRMath::vec2& v2, unsigned int color);

	void SetLineAlgorithm(ELineAlgorithm eLineAlgorithm);

	void Clear();
	void Present(HDC hScreenDC) const;
	void Render();

	void OnResize(HWND hWnd);

	// Getter
	const int GetWidth() const { return m_width; }
	const int GetHeight() const { return m_height; }
};

