#pragma once

#include "pch.h"

class Renderer
{
private:
	// ����۸��� �ʿ��� GDI ��ü �ڵ�
	HDC     m_hMemDC;       // ������� Device Context
	HBITMAP m_hBitmap;      // ����ۿ� ��Ʈ��
	HBITMAP m_hOldBitmap;   // m_hMemDC�� ���� ���õǾ� �ִ� ��Ʈ��

	int m_width;
	int m_height;

	unsigned int* m_pPixelData;
public:
	Renderer();
	~Renderer();

	bool Initialize(HWND hWnd);
	void Shutdown() const;

	void DrawPixel(int x, int y, unsigned int color);
	void DrawLine(int x0, int y0, int x1, int y1, unsigned int color);
	void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color);

	void Clear() const;
	void Present(HDC hScreenDC) const;
	void Render();

	void OnResize(HWND hWnd);


};

