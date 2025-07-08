#pragma once

#include "pch.h"

class Renderer
{
private:
	// ����۸��� �ʿ��� GDI ��ü �ڵ�
	HDC     m_hMemDC;       // ������� Device Context
	HBITMAP m_hBitmap;      // ����ۿ� ��Ʈ��
	HBITMAP m_hOldBitmap;   // m_hMemDC�� ���� ���õǾ� �ִ� ��Ʈ��

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

