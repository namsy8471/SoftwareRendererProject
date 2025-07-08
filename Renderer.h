#pragma once

#include "pch.h"

class Renderer
{
private:
	// 백버퍼링에 필요한 GDI 객체 핸들
	HDC     m_hMemDC;       // 백버퍼의 Device Context
	HBITMAP m_hBitmap;      // 백버퍼용 비트맵
	HBITMAP m_hOldBitmap;   // m_hMemDC에 원래 선택되어 있던 비트맵

public:
	Renderer();
	~Renderer();

	bool Initialize(HWND hWnd);
	void Shutdown();

	void DrawPixel(int x, int y, COLORREF color);
	void DrawLine(int x0, int y0, int x1, int y1, COLORREF color);

	void Clear();
	void Present();
	void Render();

};

