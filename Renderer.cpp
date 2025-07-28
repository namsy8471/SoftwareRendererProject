#include "Renderer.h"
#include <thread>
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include "Model.h"
#include <omp.h>

constexpr float FLOATINF = std::numeric_limits<float>::infinity();

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
        
        int idx = y0 * m_width + x0;
        if (idx >= 0 && idx < m_depthBuffer.size())
        {
            DrawPixel(x0, y0, color);
        }

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
void Renderer::DrawFilledTriangle(const SRMath::vec2& v0, const SRMath::vec2& v1, const SRMath::vec2& v2,
    const SRMath::vec3& n0_view, const SRMath::vec3& n1_view, const SRMath::vec3& n2_view,
    float z0, float z1, float z2, const SRMath::vec3& light_dir)
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

    // 바운딩 박스가 밖으로 넘어갈 경우 에러가 남으로 최소값 및 최댓값 설정
    minX = std::max(0, minX);
    minY = std::max(0, minY);
    maxX = std::min(m_width - 1, maxX);
    maxY = std::min(m_height - 1, maxY);

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
                float total_w = static_cast<float>(w0 + w1 + w2);

                if (std::abs(total_w) < 1e-5f) continue;

                float w_bary = w0 / total_w;
                float u_bary = w1 / total_w;
                float v_bary = w2 / total_w;
                
                float z_interpolated = z0 * w_bary + z1 * u_bary + z2 * v_bary;
                
                int idx = y * m_width + x;
                if (z_interpolated < m_depthBuffer[idx])
                {
                    SRMath::vec3 normal_interpolated =
                        SRMath::normalize(n0_view * w_bary + n1_view * u_bary + n2_view * v_bary);

                    float intensity = std::max(0.0f, SRMath::dot(normal_interpolated, light_dir * -1));

                    SRMath::vec3 base_color = { 1.f, 1.f, 1.f };
                    SRMath::vec3 final_color_vec = base_color * intensity;

                    unsigned int final_color = 
                        RGB(static_cast<int>(final_color_vec.x * 255.f),
                        static_cast<int>(final_color_vec.y * 255.f),
                        static_cast<int>(final_color_vec.z * 255.f));

                    m_depthBuffer[idx] = z_interpolated;
                    DrawPixel(x, y, final_color);
                }
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

void Renderer::SetDebugNormal()
{
    m_isNrmDebug = !m_isNrmDebug;
}

void Renderer::Clear()
{
    // 1. Get Thread
    const unsigned int numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;

    // 2. divide height by Threads.
    const int linesPerThread = m_height / numThreads;

    for (unsigned int i = 0; i < numThreads; ++i)
    {
        // 3. calculate line for height.
        const int startY = i * linesPerThread;
        const int endY = (i == numThreads - 1) ? m_height : startY + linesPerThread;

        // 4. Call the DrawPixel in each Thread
        threads.emplace_back([this, startY, endY]() {

            for (int y = startY; y < endY; y++)
            {
                int rowOffset = y * m_width;
                for (int x = 0; x < m_width; x++)
                {
                    int idx = x + rowOffset;
                    m_pPixelData[idx] = 0;
                    m_depthBuffer[idx] = FLOATINF; // Z Buffer Clearing
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


//void Renderer::Render()
//{
//    
//
//    /*switch (m_currentLineAlgorithm) {
//    case ELineAlgorithm::Bresenham:
//        DrawTriangle(100, 200, 100, 300, 200, 300, RGB(255, 0, 0));
//        break;
//    case ELineAlgorithm::DDA:
//        DrawTriangle(100, 200, 100, 300, 200, 300, RGB(0, 255, 0));
//        break;
//    }
//
//    DrawFilledTriangle(SRMath::vec2(200, 1000), SRMath::vec2(500, 100), SRMath::vec2(800, 1000), RGB(0, 0, 255));*/
//}

void Renderer::Render(const std::vector<std::shared_ptr<Model>>& m_models,
    SRMath::mat4& projectionMatrix, SRMath::mat4& viewMatrix, SRMath::vec3& light_dir)
{
    // TODO: Draw something here

    for(const auto& m_model : m_models)
    {
        SRMath::mat4 scaleMatrix = SRMath::scale({ 0.04f, 0.04f, 0.04f });
        SRMath::mat4 rotationMatrix = SRMath::rotate({ 0.f, 0.f, 0.0f }); // 모델 뒤집기
        SRMath::mat4 translationMatrix = SRMath::translate({ 0.0f, -1.5f, 10.0f });
        SRMath::mat4 modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;

        // Final MVP Matrix
        SRMath::mat4 vp = projectionMatrix * viewMatrix;
        SRMath::mat4 mv = viewMatrix * modelMatrix;
        SRMath::mat4 mvp = vp * modelMatrix;

        const auto& positions = m_model->GetPositions();
        const auto& pos_indices = m_model->GetPositionIndices();
        const auto& normals = m_model->Getnormals();
        const auto& nrm_indices = m_model->GetNormalIndices();

        if (nrm_indices.size() != pos_indices.size()) continue;

        for (size_t i = 0; i < pos_indices.size(); i += 3)
        {
            SRMath::vec3 p_model[] = { positions[pos_indices[i]], positions[pos_indices[i + 1]], positions[pos_indices[i + 2]] };
            SRMath::vec3 n_model[] = { normals[nrm_indices[i]], normals[nrm_indices[i + 1]] , normals[nrm_indices[i + 2]] };

            SRMath::vec3 v0_view = mv * p_model[0];
            SRMath::vec3 v1_view = mv * p_model[1];
            SRMath::vec3 v2_view = mv * p_model[2];

            SRMath::vec3 face_normal = SRMath::normalize(SRMath::cross(v1_view - v0_view, v2_view - v0_view));
            SRMath::vec3 view_dir = SRMath::normalize(v0_view * -1.f);

            // Back face culling
            if (SRMath::dot(face_normal, view_dir) <= 0.f)
            {
                continue;
            }

            SRMath::vec4 p0_clip = mvp * p_model[0];
            SRMath::vec4 p1_clip = mvp * p_model[1];
            SRMath::vec4 p2_clip = mvp * p_model[2];
            
            SRMath::vec4 n0_clip = SRMath::normalize(mvp * SRMath::vec4(n_model[0], 0.f));
            SRMath::vec4 n1_clip = SRMath::normalize(mvp * SRMath::vec4(n_model[1], 0.f));
            SRMath::vec4 n2_clip = SRMath::normalize(mvp * SRMath::vec4(n_model[2], 0.f));

            std::vector<SRMath::vec4> clipped_vertices;
            std::vector<SRMath::vec4> clipped_vertices_nrm;
            SRMath::vec4 vertices[] = { p0_clip, p1_clip, p2_clip };
            SRMath::vec4 vertices_nrm[] = { n0_clip, n1_clip, n2_clip };

            for (int j = 0; j < 3; j++)
            {
                const SRMath::vec4& start_v = vertices[j];
                const SRMath::vec4& end_v = vertices[(j + 1) % 3];

                SRMath::vec4 start_normal = SRMath::vec4(vertices_nrm[j], 0.f);
                SRMath::vec4 end_normal = SRMath::vec4(vertices_nrm[(j + 1) % 3], 0.f);

                bool isStartInside = start_v.w > 0.1f;
                bool isEndInside = end_v.w > 0.1f;

                if (isStartInside && isEndInside) {
                    clipped_vertices.emplace_back(end_v);
                    clipped_vertices_nrm.emplace_back(end_normal);
                }
                else if (isStartInside != isEndInside)
                {
                    float w_diff = start_v.w - end_v.w;
                    if (std::abs(w_diff) > 1e-6f)
                    {
                        float t = (start_v.w - 0.1f) / (start_v.w - end_v.w);
                        SRMath::vec4 intersection = start_v + (end_v - start_v) * t;
                        SRMath::vec3 intersection_normal = SRMath::normalize(start_normal + (end_normal - start_normal) * t);

                        if (isStartInside){
                            clipped_vertices.emplace_back(intersection);
                            clipped_vertices_nrm.emplace_back(SRMath::vec4(intersection_normal, 0.f));
                        }
                        else {
                            clipped_vertices.emplace_back(intersection);
                            clipped_vertices.emplace_back(end_v);
                            clipped_vertices_nrm.emplace_back(SRMath::vec4(intersection_normal, 0.f));
                            clipped_vertices_nrm.emplace_back(end_normal);
                        }
                    }
                }
            }

            if (clipped_vertices.size() < 3)
                continue;

            std::vector<SRMath::vec2> screen_coords(clipped_vertices.size());
            std::vector<SRMath::vec3> view_normals(clipped_vertices.size());
            std::vector<float> z_depth(clipped_vertices.size()); // Z-버퍼에 사용할 깊이 값 저장용

            for (size_t j = 0; j < clipped_vertices.size() - 2; j++)
            {
                SRMath::vec4 clipped_v0 = clipped_vertices[j];
                SRMath::vec4 clipped_v1 = clipped_vertices[j + 1];
                SRMath::vec4 clipped_v2 = clipped_vertices[j + 2];

                // 8-1. 원근 분할 (Perspective Division): 클립 공간 -> NDC 공간
                // x, y, z를 w로 나누어 원근을 적용하고 -1 ~ 1 범위로 정규화합니다.
                clipped_v0.x /= clipped_v0.w;
                clipped_v0.y /= clipped_v0.w;
                clipped_v0.z /= clipped_v0.w;

                clipped_v1.x /= clipped_v1.w;
                clipped_v1.y /= clipped_v1.w;
                clipped_v1.z /= clipped_v1.w;

                clipped_v2.x /= clipped_v2.w;
                clipped_v2.y /= clipped_v2.w;
                clipped_v2.z /= clipped_v2.w;

                // 8-2. 뷰포트 변환 (Viewport Transform): NDC 공간 -> 화면 공간
                // -1 ~ 1 범위의 좌표를 실제 창 픽셀 좌표(0~width, 0~height)로 변환합니다.
                screen_coords[j].x = (clipped_v0.x + 1.0f) * 0.5f * m_width;
                screen_coords[j].y = (1.0f - clipped_v0.y) * 0.5f * m_height; // Y축 뒤집기

                screen_coords[j + 1].x = (clipped_v1.x + 1.0f) * 0.5f * m_width;
                screen_coords[j + 1].y = (1.0f - clipped_v1.y) * 0.5f * m_height;

                screen_coords[j + 2].x = (clipped_v2.x + 1.0f) * 0.5f * m_width;
                screen_coords[j + 2].y = (1.0f - clipped_v2.y) * 0.5f * m_height;

                // 8-3. Z-버퍼에 사용할 깊이 값을 저장해 둡니다.
                z_depth[j] = clipped_v0.z;
                z_depth[j + 1] = clipped_v1.z;
                z_depth[j + 2] = clipped_v2.z;

                SRMath::vec4 clipped_n0 = clipped_vertices_nrm[j];
                SRMath::vec4 clipped_n1 = clipped_vertices_nrm[j + 1];
                SRMath::vec4 clipped_n2 = clipped_vertices_nrm[j + 2];

                DrawFilledTriangle(screen_coords[j], screen_coords[j + 1], screen_coords[j + 2],
                    clipped_n0, clipped_n1, clipped_n2, z_depth[j], z_depth[j + 1], z_depth[j + 2],
                    light_dir);
            }

            SRMath::vec3 v0_world = modelMatrix * p_model[0];
            SRMath::vec3 v1_world = modelMatrix * p_model[1];
            SRMath::vec3 v2_world = modelMatrix * p_model[2];

            if (m_isNrmDebug)
            {
                SRMath::vec3 n0_world = SRMath::normalize(modelMatrix * SRMath::vec4(n_model[0], 0.f));
                SRMath::vec3 n1_world = SRMath::normalize(modelMatrix * SRMath::vec4(n_model[1], 0.f));
                SRMath::vec3 n2_world = SRMath::normalize(modelMatrix * SRMath::vec4(n_model[2], 0.f));
                DebugNormalVector(v0_world, v1_world, v2_world, n0_world, n1_world, n2_world, vp);
            }
        }
    }
}

void Renderer::DebugNormalVector(const SRMath::vec3& v0_World, const SRMath::vec3& v1_World, const SRMath::vec3& v2_World, 
    const SRMath::vec3& n0_World, const SRMath::vec3& n1_World, const SRMath::vec3& n2_World, SRMath::mat4& vp)
{
    // 각 정점에서 법선 방향으로 뻗어 나가는 짧은 선을 그립니다.
    SRMath::vec3 vertices[] = { v0_World, v1_World, v2_World };
    SRMath::vec3 normals[] = { n0_World, n1_World, n2_World };

    for (int j = 0; j < 3; ++j)
    {
        SRMath::vec3 line_start = vertices[j];
        SRMath::vec3 line_end = vertices[j] + normals[j] * 0.1f;

        // 선의 시작점과 끝점을 화면 좌표로 변환
        SRMath::vec4 start_clip = vp * line_start;
        SRMath::vec4 end_clip = vp * line_end;

        if (start_clip.w <= 0.1f || end_clip.w <= 0.1f) continue;

        start_clip.x /= start_clip.w;
        start_clip.y /= start_clip.w;
        end_clip.x /= end_clip.w;
        end_clip.y /= end_clip.w;

        int startX = (start_clip.x + 1.0f) * 0.5f * m_width;
        int startY = (1.0f - start_clip.y) * 0.5f * m_height;
        int endX = (end_clip.x + 1.0f) * 0.5f * m_width;
        int endY = (1.0f - end_clip.y) * 0.5f * m_height;

        // 법선을 노란색 선으로 그림
        DrawLine(startX, startY, endX, endY, RGB(255, 255, 0));
    }
}

void Renderer::OnResize(HWND hWnd)
{
    Shutdown();
    Initialize(hWnd);
}
