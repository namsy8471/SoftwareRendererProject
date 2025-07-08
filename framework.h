#pragma once

#include "pch.h"

#define MAX_LOADSTRING 100

class Renderer;

class Framework
{
private:
	Renderer* m_pRenderer;

	// Windows Variables
	HWND m_hWnd;                                // 윈도우 핸들
	HINSTANCE m_hInstance;						// 핸들 인스턴스

	WCHAR m_szTitle[MAX_LOADSTRING];              // 제목 표시줄 텍스트입니다.
	WCHAR m_szWindowClass[MAX_LOADSTRING];        // 기본 창 클래스 이름입니다.

public:
	Framework();
	~Framework();

	bool Initialize(HINSTANCE, int);
	void Run();
	void Update();
	void Shutdown();
	LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

public:
	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	static INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};
