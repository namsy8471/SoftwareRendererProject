#include "Renderer.h"

Renderer::Renderer() : m_hBitmap(nullptr), m_hMemDC(nullptr),
m_hOldBitmap(nullptr)
{
}

Renderer::~Renderer()
{
}

bool Renderer::Initialize(HWND hWnd)
{
    // 윈도우의 클라이언트 영역 크기를 얻어옵니다.
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    // 윈도우의 DC(Screen DC)를 얻어옵니다.
    HDC hScreenDC = GetDC(hWnd);

    // ✨ 백버퍼 역할을 할 메모리 DC와 비트맵을 생성합니다.
    // Screen DC와 호환되는 메모리 DC를 만듭니다.
    m_hMemDC = CreateCompatibleDC(hScreenDC);
    if (!m_hMemDC)
    {
        // 실패 시 Screen DC를 해제하고 종료
        ReleaseDC(hWnd, hScreenDC);
        return false;
    }

    // 메모리 DC에 그려질 비트맵(백버퍼)을 생성합니다.
    m_hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!m_hBitmap)
    {
        // 실패 시 메모리 DC와 Screen DC를 해제하고 종료
        DeleteDC(m_hMemDC);
        ReleaseDC(hWnd, hScreenDC);
        return false;
    }

    // 생성한 비트맵을 메모리 DC에 선택시킵니다.
    // 이 시점부터 m_hMemDC에 그리는 모든 것은 m_hBitmap에 그려집니다.
    // 원래 있던 기본 비트맵 핸들은 나중에 복구시키기 위해 보관합니다.
    m_hOldBitmap = (HBITMAP)SelectObject(m_hMemDC, m_hBitmap);

    // 이제 Screen DC는 필요 없으므로 바로 해제합니다.
    ReleaseDC(hWnd, hScreenDC);

    // 이 예제에서는 백버퍼를 흰색으로 초기화합니다.
    PatBlt(m_hMemDC, 0, 0, width, height, WHITENESS);

    return true;
}

void Renderer::Shutdown()
{
    // 생성의 역순으로 GDI 객체들을 해제합니다.
    if (m_hMemDC)
    {
        // 1. 보관해둔 원래 비트맵으로 되돌립니다.
        SelectObject(m_hMemDC, m_hOldBitmap);

        // 2. 우리가 만든 비트맵을 삭제합니다.
        DeleteObject(m_hBitmap);

        // 3. 우리가 만든 메모리 DC를 삭제합니다.
        DeleteDC(m_hMemDC);
    }
}

void Renderer::DrawPixel(int x, int y, COLORREF color)
{
}

void Renderer::DrawLine(int x0, int y0, int x1, int y1, COLORREF color)
{
}

void Renderer::Clear()
{
}

void Renderer::Present()
{
}

void Renderer::Render()
{
    this->Clear();

    // TODO: Draw something here



    this->Present();
}
