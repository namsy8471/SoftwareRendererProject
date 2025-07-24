#include "Renderer.h"
#include <thread>
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <omp.h>

Renderer::Renderer() : m_hBitmap(nullptr), m_hMemDC(nullptr),
m_hOldBitmap(nullptr), m_height(), m_width(), m_pPixelData(nullptr)
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
    m_width = clientRect.right - clientRect.left;
    m_height = clientRect.bottom - clientRect.top;

    // 윈도우의 DC(Screen DC)를 얻어옵니다.
    HDC hScreenDC = GetDC(hWnd);

    // 백버퍼 역할을 할 메모리 DC와 비트맵을 생성합니다.
    // Screen DC와 호환되는 메모리 DC를 만듭니다.
    m_hMemDC = CreateCompatibleDC(hScreenDC);
    if (!m_hMemDC)
    {
        // 실패 시 Screen DC를 해제하고 종료
        ReleaseDC(hWnd, hScreenDC);
        return false;
    }

    // DIB 정보를 담을 BITMAPINFO 구조체를 설정합니다.
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -m_height; //  중요: 높이를 음수로 설정! [y * width + x]
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;      // 32비트 컬러
    bmi.bmiHeader.biCompression = BI_RGB; // 압축 안 함

    // 메모리 DC에 그려질 비트맵(백버퍼)을 생성합니다.
    m_hBitmap = CreateCompatibleBitmap(hScreenDC, m_width, m_height);
    
    m_hBitmap = CreateDIBSection(m_hMemDC, &bmi, DIB_RGB_COLORS,
        (void**)&m_pPixelData, NULL, 0);

    if (!m_hBitmap)
    {
        // 실패 시 메모리 DC와 Screen DC를 해제하고 종료
        DeleteDC(m_hMemDC);
        ReleaseDC(hWnd, hScreenDC);
        return false;
    }

    m_depthBuffer.resize(m_height * m_width);

    // 생성한 비트맵을 메모리 DC에 선택시킵니다.
    // 이 시점부터 m_hMemDC에 그리는 모든 것은 m_hBitmap에 그려집니다.
    // 원래 있던 기본 비트맵 핸들은 나중에 복구시키기 위해 보관합니다.
    m_hOldBitmap = (HBITMAP)SelectObject(m_hMemDC, m_hBitmap);

    // 이제 Screen DC는 필요 없으므로 바로 해제합니다.
    ReleaseDC(hWnd, hScreenDC);

    // 이 예제에서는 백버퍼를 흰색으로 초기화합니다.
    PatBlt(m_hMemDC, 0, 0, m_width, m_height, WHITENESS);

    return true;
}

void Renderer::Shutdown() const
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

void Renderer::DrawPixel(int x, int y, unsigned int color)
{
    // Checking Boundary
    // 화면 밖을 침범해서 메모리를 오염시키는 것을 방지합니다.
    if (x < 0 || x >= m_width || y < 0 || y >= m_height)
    {
        return;
    }

    // !! Important
    // Color exchange to RGB from BGR
    unsigned char r = (color >> 16) & 0xFF;
    unsigned char g = (color >> 8) & 0xFF;
    unsigned char b = (color >> 0) & 0xFF;

    unsigned int newColor = (r << 0) | (g << 8) | (b << 16);

    // 1차원 배열 인덱스를 계산해서 픽셀 값을 직접 씁니다.
    m_pPixelData[y * m_width + x] = newColor;
}

// Bresenham Algorithm
void Renderer::drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color)
{
    const int dx = abs(x1 - x0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int dy = -abs(y1 - y0); // y는 음수인 것이 계산에 편리
    const int sy = (y0 < y1) ? 1 : -1;

    int err = dx + dy;

    while (true)
    {
        DrawPixel(x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

// DDA(Digital Differential Analyzer) Algorithm
void Renderer::drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color)
{
    const double dy = abs(y1 - y0);
    const double dx = abs(x1 - x0);

    int steps = dx > dy ? dx : dy;

    const double x_inc = dx / (double)steps;
    const double y_inc = dy / (double)steps;

    double x = x0;
    double y = y0;
    
    for (int i = 0; i <= steps; i++) {
        DrawPixel(round(x), round(y), color);
        x += x_inc;
        y += y_inc;
    }
    
}

void Renderer::DrawLine(int x0, int y0, int x1, int y1, unsigned int color)
{
    switch (m_currentLineAlgorithm)
    {
    case ELineAlgorithm::Bresenham:
        drawLineByBresenham(x0, y0, x1, y1, color);
        break;
    
    case ELineAlgorithm::DDA:
        drawLineByDDA(x0, y0, x1, y1, color);
        break;
    }
}

void Renderer::DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color)
{
    DrawLine(x0, y0, x1, y1, color);
    DrawLine(x1, y1, x2, y2, color);
    DrawLine(x2, y2, x0, y0, color);
}

void Renderer::DrawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color)
{
    DrawTriangle(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, color);
}

// Using Barycentric Coordinates
void Renderer::DrawFilledTriangle(const SRMath::vec2& v0, const SRMath::vec2& v1, const SRMath::vec2& v2, unsigned int color)
{
    // 정점 좌표를 정수로 변환 (화면 픽셀 기준)
    const SRMath::vec2 p0 = { v0.x, v0.y };
    const SRMath::vec2 p1 = { v1.x, v1.y };
    const SRMath::vec2 p2 = { v2.x, v2.y };

    // 1. 경계 상자(Bounding Box)를 계산합니다.
    int minX = static_cast<int>(std::min({ p0.x, p1.x, p2.x }));
    int minY = static_cast<int>(std::min({ p0.y, p1.y, p2.y }));
    int maxX = static_cast<int>(std::max({ p0.x, p1.x, p2.x }));
    int maxY = static_cast<int>(std::max({ p0.y, p1.y, p2.y }));

    // --- 사전 계산 단계 ---
    // 각 변(edge)의 x, y 변화량을 미리 계산해 둡니다.
    const int dx01 = p0.x - p1.x;
    const int dy01 = p0.y - p1.y;
    const int dx12 = p1.x - p2.x;
    const int dy12 = p1.y - p2.y;
    const int dx20 = p2.x - p0.x;
    const int dy20 = p2.y - p0.y;

    // 경계 상자의 시작점(minX, minY)에서의 바리센트릭 좌표 값을 계산합니다.
    int w0_row = dy12 * (minX - p1.x) - dx12 * (minY - p1.y);
    int w1_row = dy20 * (minX - p2.x) - dx20 * (minY - p2.y);
    int w2_row = dy01 * (minX - p0.x) - dx01 * (minY - p0.y);

    // --- 래스터화 루프 ---
    for (int y = minY; y <= maxY; ++y)
    {
        // 현재 행의 시작 값을 복사
        int w0 = w0_row;
        int w1 = w1_row;
        int w2 = w2_row;

        for (int x = minX; x <= maxX; ++x)
        {
            // 바리센트릭 좌표가 모두 양수이면 삼각형 내부에 있는 것입니다.
            if (w0 >= 0 && w1 >= 0 && w2 >= 0)
            {
                DrawPixel(x, y, color);
            }

            // x가 1 증가했으므로, y의 변화량만큼 더해줍니다. (점진적 계산)
            w0 += dy12;
            w1 += dy20;
            w2 += dy01;
        }

        // y가 1 증가했으므로, 다음 행의 시작 값을 x의 변화량만큼 더해서 갱신합니다.
        w0_row -= dx12;
        w1_row -= dx20;
        w2_row -= dx01;
    }
}

void Renderer::SetLineAlgorithm(ELineAlgorithm eLineAlgorithm)
{
    m_currentLineAlgorithm = eLineAlgorithm;
}

void Renderer::Clear()
{
    // 1. Get Thread
    const unsigned int numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;

    // 2. divide height by Threads.
    const int linesPerThread = m_height / numThreads;

    const float INF = std::numeric_limits<float>::infinity();   // Z Buffer Clearing

    for (unsigned int i = 0; i < numThreads; ++i)
    {
        // 3. calculate line for height.
        const int startY = i * linesPerThread;
        const int endY = (i == numThreads - 1) ? m_height : startY + linesPerThread;

        // 4. Call the DrawPixel in each Thread
        threads.emplace_back([this, startY, endY, INF]() {

            for (int y = startY; y < endY; y++)
            {
                int rowOffset = y * m_width;
                for (int x = 0; x < m_width; x++)
                {
                    int idx = x + rowOffset;
                    m_pPixelData[idx] = 0;
                    m_depthBuffer[idx] = INF;
                }
            }
        });
    }

    // 5. await for complete
    for (auto& t : threads)
    {
        t.join();
    }
}

void Renderer::Present(HDC hScreenDC) const
{
    // BitBlt API를 사용해 메모리 DC의 내용을 화면 DC로 복사합니다.
    BitBlt(hScreenDC,      // 복사 대상 DC (화면)
        0, 0,           // 대상의 시작 좌표 (x, y)
        m_width,        // 복사할 너비
        m_height,       // 복사할 높이
        m_hMemDC,       // 복사 원본 DC (백버퍼)
        0, 0,           // 원본의 시작 좌표 (x, y)
        SRCCOPY);       // 복사 방식 (그대로 복사)
}

void Renderer::Render()
{
    // TODO: Draw something here

    switch (m_currentLineAlgorithm) {
    case ELineAlgorithm::Bresenham:
        DrawTriangle(100, 200, 100, 300, 200, 300, RGB(255, 0, 0));
        break;
    case ELineAlgorithm::DDA:
        DrawTriangle(100, 200, 100, 300, 200, 300, RGB(0, 255, 0));
        break;
    }

    DrawFilledTriangle(SRMath::vec2(200, 1000), SRMath::vec2(500, 100), SRMath::vec2(800, 1000), RGB(0, 0, 255));
}

void Renderer::OnResize(HWND hWnd)
{
    Shutdown();
    Initialize(hWnd);
}
