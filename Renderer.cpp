#include "Renderer.h"
#include <thread>
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <omp.h>
#include "Mesh.h"
#include "Texture.h"
#include "AABB.h"
#include "Octree.h"

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

    // Load Texture
    std::string texName = "assets/default.png";
    texNames.emplace_back(texName);

    m_ptexture = TextureLoader::LoadImageFile(texName);

    if (!m_ptexture)
    {
        MessageBox(hWnd, L"Failed to load Texture.", L"Texture Load Error", MB_OK);
        return false;
    }

    m_textures[texName] = m_ptexture;

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
    const int dx = x1 - x0;
    const int dy = y1 - y0;

    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    // steps가 0인 경우(시작점과 끝점이 같음) 즉시 픽셀을 그리고 종료
    if (steps == 0) {
        DrawPixel(x0, y0, color);
        return;
    }

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
void Renderer::DrawMesh(const SRMath::vec2& v0, const SRMath::vec2& v1, const SRMath::vec2& v2,
    const float one_over_w0, const float one_over_w1, const float one_over_w2, 
    const SRMath::vec3& n0_clipped, const SRMath::vec3& n1_clipped, const SRMath::vec3& n2_clipped,
    const SRMath::vec2& uv0_clipped, const SRMath::vec2& uv1_clipped, 
    const SRMath::vec2& uv2_clipped, const SRMath::vec3& light_dir, const std::shared_ptr<Texture> texture)
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
    const float dx01 = p0.x - p1.x;
    const float dy01 = p0.y - p1.y;
    const float dx12 = p1.x - p2.x;
    const float dy12 = p1.y - p2.y;
    const float dx20 = p2.x - p0.x;
    const float dy20 = p2.y - p0.y;

    // 경계 상자의 시작점(minX, minY)에서의 바리센트릭 좌표 값을 계산합니다.
    float w0_row = dy12 * (minX - p1.x) - dx12 * (minY - p1.y);
    float w1_row = dy20 * (minX - p2.x) - dx20 * (minY - p2.y);
    float w2_row = dy01 * (minX - p0.x) - dx01 * (minY - p0.y);

    // --- 래스터화 루프 ---
    for (int y = minY; y <= maxY; ++y)
    {
        // 현재 행의 시작 값을 복사
        float w0 = w0_row;
        float w1 = w1_row;
        float w2 = w2_row;

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
                
                float interpolated_one_over_w = 
                    one_over_w0 * w_bary + one_over_w1 * u_bary + one_over_w2 * v_bary;
                
                int idx = y * m_width + x;
                if (interpolated_one_over_w > m_depthBuffer[idx])
                {
                    SRMath::vec3 normal_interpolated =
                        SRMath::normalize((n0_clipped * w_bary + n1_clipped * u_bary + n2_clipped * v_bary) / interpolated_one_over_w);

                    SRMath::vec2 uv_over_w_interpolated = uv0_clipped * w_bary + uv1_clipped * u_bary + uv2_clipped * v_bary;
                    SRMath::vec2 uv_interpolated = uv_over_w_interpolated / interpolated_one_over_w;

                    //float intensity = std::max(0.0f, SRMath::dot(normal_interpolated, light_dir));

                    unsigned int texel_color = texture->GetPixels(uv_interpolated.x, uv_interpolated.y);

                    // 1. 난반사(diffuse) 조명을 계산합니다.
                    float diffuse_intensity = std::max(0.0f, dot(normal_interpolated, light_dir));

                    // 2. 💡 아주 작은 값의 주변광(ambient)을 추가합니다.
                    float ambient_intensity = 0.1f; // 10%의 주변광

                    // 3. 최종 빛의 세기는 난반사 + 주변광입니다. (최대 1.0을 넘지 않도록)
                    float final_intensity = std::min(1.0f, diffuse_intensity + ambient_intensity);

                    SRMath::vec3 base_color = { 1.f, 1.f, 1.f };
                    SRMath::vec3 color = base_color * final_intensity;

                    unsigned int final_color = RGB(
                        color.x * 255.f,
                        color.y * 255.f,
                        color.z * 255.f
                    );

                    unsigned int final_color2 = texel_color * final_intensity;
                    
                    m_depthBuffer[idx] = interpolated_one_over_w;
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
                    m_depthBuffer[idx] = std::numeric_limits<float>::lowest(); // Z Buffer Clearing
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

float x;

// Vertex Shading
struct ShadedVertex {
    SRMath::vec3 pos_world;
    SRMath::vec4 pos_clip;
    SRMath::vec3 pos_view;
    SRMath::vec3 normal_world;
    SRMath::vec2 texcoord;
};

// --- 래스터화를 위한 최종 정점 데이터 준비 ---
struct RasterizerVertex {
    SRMath::vec2 screen_pos;          // 최종 화면 좌표
    float one_over_w;                 // 원근 보간을 위한 1/w
    SRMath::vec3 normal_world_over_w; // 원근 보정된 법선
    SRMath::vec2 texcoord_over_w;     // 원근 보정된 UV
};

void Renderer::Render(const SRMath::mat4& projectionMatrix, const SRMath::mat4& viewMatrix, SRMath::vec3& light_dir)
{
    // TODO: Draw something here
    x += PI / 360;

    // --- 1. 절두체 평면 추출 (프레임 당 한 번만) ---
    Frustum frustum;
    SRMath::mat4 vp = projectionMatrix * viewMatrix;

//    for(const auto& m_model : m_models)
//    {
//        SRMath::mat4 scaleMatrix = SRMath::scale({ 0.04f, 0.04f, 0.04f });
//        SRMath::mat4 rotationMatrix = SRMath::rotate({ 0.f, x, 0.0f }); // 모델 뒤집기
//        SRMath::mat4 translationMatrix = SRMath::translate({ 0.0f, -1.5f, 10.0f });
//        SRMath::mat4 modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;
//
//        SRMath::mat4 normal_matrix_world = rotationMatrix;
//
//        // Final MVP Matrix
//        SRMath::mat4 mv = viewMatrix * modelMatrix;
//        SRMath::mat4 mvp = projectionMatrix * mv;
//
//        for (const auto& mesh : m_model->GetMeshes())
//        {
//            const auto* rootNode = mesh.octree->GetRoot();
//
//            if (rootNode == nullptr) continue;
//
//            //renderOctreeNode(rootNode, frustum, mesh, modelMatrix, mv, vp, mvp, normal_matrix_world, light_dir);
//
//            // Vertex Shading
//            const auto& vertices = mesh.vertices;
//            const auto& indices = mesh.indices;
//
//            std::vector<ShadedVertex> shaded_vertices(vertices.size());
//
//            for (int i = 0; i < vertices.size(); ++i) {
//                const auto& v = vertices[i];
//                shaded_vertices[i].pos_world = modelMatrix * v.position;
//                shaded_vertices[i].pos_clip = mvp * v.position;
//                shaded_vertices[i].pos_view = mv * v.position;
//                shaded_vertices[i].normal_world = normal_matrix_world * SRMath::vec4(v.normal, 0.f);
//                shaded_vertices[i].texcoord = v.texcoord;
//            }
//
//            // --- 4. Triangle Processing 단계 ---
//#pragma omp parallel for
//            for (int i = 0; i < indices.size(); i += 3)
//            {
//                // 미리 변환된 정점 데이터를 인덱스로 바로 가져옴
//                const ShadedVertex& v0 = shaded_vertices[indices[i]];
//                const ShadedVertex& v1 = shaded_vertices[indices[i + 1]];
//                const ShadedVertex& v2 = shaded_vertices[indices[i + 2]];
//
//                // --- Back-face Culling (뷰 공간) ---
//                SRMath::vec3 face_normal = SRMath::normalize(SRMath::cross(v1.pos_view - v0.pos_view, v2.pos_view - v0.pos_view));
//                if (SRMath::dot(face_normal, v0.pos_view) <= 0.f) {
//                    continue;
//                }
//
//                // clipping
//                std::vector<ShadedVertex> clipped_vertices = clipTriangle(v0, v1, v2);
//                if (clipped_vertices.size() < 3) continue;
//
//                // Resterization 
//                std::vector<RasterizerVertex> final_vertices(clipped_vertices.size());
//                resterization(clipped_vertices, final_vertices, light_dir);
//
//                // DEBUG CODE
//                if (m_isNrmDebug)
//                {
//                    DebugNormalVector(v0.pos_world, v1.pos_world, v2.pos_world,
//                        SRMath::normalize(v0.normal_world), SRMath::normalize(v1.normal_world), SRMath::normalize(v2.normal_world), vp);
//                }
//            }
//        }
//    }
}

//void Renderer::renderOctreeNode(const OctreeNode* node, const Frustum& frustum, const Mesh& mesh, const SRMath::mat4& modelMatrix,
//    const SRMath::mat4& mv, const SRMath::mat4& vp, const SRMath::mat4& mvp, const SRMath::mat4& normal_matrix, const SRMath::vec3& light_dir)
//{
//    // --- 1. 모델 공간 AABB를 월드 공간 AABB로 변환 ---
//    AABB world_aabb;
//    {
//        // AABB의 8개 꼭짓점을 만듭니다.
//        SRMath::vec3 corners[8] = {
//            {node->bounds.min.x, node->bounds.min.y, node->bounds.min.z},
//            {node->bounds.max.x, node->bounds.min.y, node->bounds.min.z},
//            {node->bounds.min.x, node->bounds.max.y, node->bounds.min.z},
//            {node->bounds.max.x, node->bounds.max.y, node->bounds.min.z},
//            {node->bounds.min.x, node->bounds.min.y, node->bounds.max.z},
//            {node->bounds.max.x, node->bounds.min.y, node->bounds.max.z},
//            {node->bounds.min.x, node->bounds.max.y, node->bounds.max.z},
//            {node->bounds.max.x, node->bounds.max.y, node->bounds.max.z}
//        };
//
//        // 8개 꼭짓점을 모두 월드 공간으로 변환합니다.
//        for (int i = 0; i < 8; ++i) {
//            corners[i] = modelMatrix * corners[i];
//        }
//
//        // 변환된 8개 꼭짓점을 모두 포함하는 새로운 AABB를 찾습니다.
//        world_aabb.min = SRMath::vec3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
//        world_aabb.max = SRMath::vec3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
//        for (int i = 0; i < 8; ++i) {
//            world_aabb.min.x = std::min(world_aabb.min.x, corners[i].x);
//            world_aabb.min.y = std::min(world_aabb.min.y, corners[i].y);
//            world_aabb.min.z = std::min(world_aabb.min.z, corners[i].z);
//            world_aabb.max.x = std::max(world_aabb.max.x, corners[i].x);
//            world_aabb.max.y = std::max(world_aabb.max.y, corners[i].y);
//            world_aabb.max.z = std::max(world_aabb.max.z, corners[i].z);
//        }
//    }
//
//    // 1. 현재 노드의 경계 상자가 절두체 밖에 있으면 즉시 반환 (컬링)
//    if (!AABB::IsAABBInFrustum(frustum, world_aabb)) {
//        return;
//    }
//
//    // 2. 현재 노드가 리프 노드이면, 포함된 삼각형들을 렌더링
//    if (node->children[0] == nullptr) {
//
//        for (unsigned int triangle_index : node->triangleIndices) {
//            // 이 삼각형을 렌더링하는 로직 (Vertex Shading, Clipping, Rasterization...)
//            // --- A. 원본 정점 데이터 가져오기 ---
//
//            unsigned int i0 = mesh.indices[triangle_index];
//            unsigned int i1 = mesh.indices[triangle_index + 1];
//            unsigned int i2 = mesh.indices[triangle_index + 2];
//
//            const Vertex& v0_model = mesh.vertices[i0];
//            const Vertex& v1_model = mesh.vertices[i1];
//            const Vertex& v2_model = mesh.vertices[i2];
//
//            // --- B. Just-In-Time 정점 셰이딩 ---
//            // 이 삼각형을 구성하는 3개의 정점만 '즉시' 변환합니다.
//            ShadedVertex sv[3];
//
//            sv[0] = { modelMatrix * v0_model.position,  mvp * v0_model.position, mv * v0_model.position, normal_matrix * SRMath::vec4(v0_model.normal, 0.f), v0_model.texcoord };
//            sv[1] = { modelMatrix * v1_model.position, mvp * v1_model.position, mv * v1_model.position, normal_matrix * SRMath::vec4(v1_model.normal, 0.f), v1_model.texcoord };
//            sv[2] = { modelMatrix * v2_model.position, mvp * v2_model.position, mv * v2_model.position, normal_matrix * SRMath::vec4(v2_model.normal, 0.f), v2_model.texcoord };
//
//            // --- C. Back-face Culling ---
//            SRMath::vec3 face_normal = SRMath::normalize(SRMath::cross(sv[1].pos_view - sv[0].pos_view, sv[2].pos_view - sv[0].pos_view));
//            if (SRMath::dot(face_normal, sv[0].pos_view) <= 0.f) {
//                continue;
//            }
//
//            // --- D. 클리핑 ---
//            std::vector<ShadedVertex> clipped_vertices = clipTriangle(sv[0], sv[1], sv[2]);
//            if (clipped_vertices.size() < 3) {
//                continue;
//            }
//
//            // Resterization 
//            std::vector<RasterizerVertex> final_vertices(clipped_vertices.size());
//            resterization(clipped_vertices, final_vertices, light_dir);
//
//            // DEBUG CODE
//            if (m_isNrmDebug)
//            {
//                DebugNormalVector(sv[0].pos_world, sv[1].pos_world, sv[2].pos_world,
//                    SRMath::normalize(sv[0].normal_world), SRMath::normalize(sv[1].normal_world), SRMath::normalize(sv[2].normal_world), vp);
//            }
//        }
//    }
//    else // 리프 노드가 아니면, 자식 노드에 대해 재귀 호출
//    {
//        for (int i = 0; i < 8; ++i) {
//            if (node->children[i] != nullptr) {
//                renderOctreeNode(node->children[i].get(), frustum, mesh, modelMatrix, mv, vp, mvp, normal_matrix, light_dir);
//            }
//        }
//    }
//}

void Renderer::resterization(const std::vector<ShadedVertex>& clipped_vertices, std::vector<RasterizerVertex>& final_vertices, const SRMath::vec3& light_dir)
{
    // 1. 모든 클리핑된 정점에 대해 원근 분할 및 뷰포트 변환을 먼저 수행합니다.
    for (size_t j = 0; j < clipped_vertices.size(); ++j)
    {
        const auto& v_clip = clipped_vertices[j];
        const float one_over_w = 1.0f / v_clip.pos_clip.w;

        // 1. 원근 분할 (w로 나누기)
        SRMath::vec3 pos_ndc = SRMath::vec3(v_clip.pos_clip) * one_over_w;

        // 2. 뷰포트 변환 (NDC -> Screen)
        final_vertices[j].screen_pos.x = (pos_ndc.x + 1.0f) * 0.5f * m_width;
        final_vertices[j].screen_pos.y = (1.0f - pos_ndc.y) * 0.5f * m_height; // Y축 뒤집기

        // 3. 원근 보정(Perspective Correction)을 위한 속성 준비
        final_vertices[j].one_over_w = one_over_w;
        final_vertices[j].normal_world_over_w = v_clip.normal_world * one_over_w;
        final_vertices[j].texcoord_over_w = v_clip.texcoord * one_over_w;
    }

    // --- 래스터화 (Fan Triangulation) ---
    // 클리핑된 폴리곤을 삼각형 팬으로 쪼개어 그립니다.
    for (size_t j = 1; j < final_vertices.size() - 1; ++j)
    {
        const auto& rv0 = final_vertices[0];
        const auto& rv1 = final_vertices[j];
        const auto& rv2 = final_vertices[j + 1];

        // 래스터라이저는 이제 화면 좌표와 원근 보정된 속성들을 받습니다.
        DrawMesh(
            rv0.screen_pos, rv1.screen_pos, rv2.screen_pos,
            rv0.one_over_w, rv1.one_over_w, rv2.one_over_w,
            rv0.normal_world_over_w, rv1.normal_world_over_w, rv2.normal_world_over_w,
            rv0.texcoord_over_w, rv1.texcoord_over_w, rv2.texcoord_over_w,
            light_dir, m_ptexture // (TODO: 재질에 맞는 텍스처 전달)
        );
    }
}


bool Renderer::isSphereInFrustum(const Frustum& frustum, const SRMath::vec3& sphere_center, float sphere_radius)
{
    for (int i = 0; i < 6; ++i)
    {
        // 평면과 구의 중심 사이의 거리를 계산
        float dist = frustum.planes[i].GetSignedDistance(sphere_center);

        // 만약 거리가 -반지름보다 작으면, 구는 평면의 '완전히 바깥쪽'에 있는 것임
        if (dist < -sphere_radius)
        {
            // 6개 평면 중 하나라도 완전히 바깥에 있으면, 구는 절두체 밖에 있음
            return false;
        }
    }
    // 모든 평면 테스트를 통과하면, 구는 절두체 안에 있거나 걸쳐 있음
    return true;
}


// 속성(Attribute) 보간 함수
ShadedVertex Renderer::interpolate(const ShadedVertex& v0, const ShadedVertex& v1, float t)
{
    ShadedVertex result;
    // 모든 속성을 선형 보간합니다.
    result.pos_clip = v0.pos_clip + (v1.pos_clip - v0.pos_clip) * t;
    result.normal_world = v0.normal_world + (v1.normal_world - v0.normal_world) * t;
    result.texcoord = v0.texcoord + (v1.texcoord - v0.texcoord) * t;
    return result;
}

// 하나의 평면으로 폴리곤을 클리핑하는 함수
std::vector<ShadedVertex> Renderer::clipPolygonAgainstPlane(const std::vector<ShadedVertex>& vertices, int plane_axis, int plane_sign)
{
    if (vertices.empty()) return {};

    std::vector<ShadedVertex> clipped_vertices;

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const ShadedVertex& current_v = vertices[i];
        const ShadedVertex& prev_v = vertices[(i + vertices.size() - 1) % vertices.size()];

        // 각 정점의 w 값에 plane_sign을 곱한 값과, 특정 축(axis) 값을 비교합니다.
        // 예: Left Plane (w + x >= 0) -> axis=0(x), sign=1. dist = w + x
        float current_dist = current_v.pos_clip.w + plane_sign * current_v.pos_clip.data[plane_axis];
        float prev_dist    = prev_v.pos_clip.w    + plane_sign * prev_v.pos_clip.data[plane_axis];

        bool is_current_inside = current_dist >= 0;
        bool is_prev_inside = prev_dist >= 0;

        if (is_current_inside != is_prev_inside)
        {
            // 두 정점이 평면을 기준으로 서로 다른 쪽에 있으면 교차점을 계산
            float t = prev_dist / (prev_dist - current_dist);
            ShadedVertex intersection = interpolate(prev_v, current_v, t);
            clipped_vertices.push_back(intersection);
        }

        if (is_current_inside)
        {
            // 현재 정점이 평면 안쪽에 있으면 결과에 추가
            clipped_vertices.push_back(current_v);
        }
    }
    return clipped_vertices;
}


// 삼각형을 6개의 절두체 평면으로 클리핑하는 메인 함수
std::vector<ShadedVertex> Renderer::clipTriangle(const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2)
{
    std::vector<ShadedVertex> vertices = { v0, v1, v2 };

    // 1. Left Plane  ( w + x >= 0 )
    vertices = clipPolygonAgainstPlane(vertices, 0, 1);
    // 2. Right Plane ( w - x >= 0 )
    vertices = clipPolygonAgainstPlane(vertices, 0, -1);
    // 3. Bottom Plane( w + y >= 0 )
    vertices = clipPolygonAgainstPlane(vertices, 1, 1);
    // 4. Top Plane   ( w - y >= 0 )
    vertices = clipPolygonAgainstPlane(vertices, 1, -1);
    // 5. Near Plane  ( w + z >= 0 )  (OpenGL 기준)
    vertices = clipPolygonAgainstPlane(vertices, 2, 1);
    // 6. Far Plane   ( w - z >= 0 )  (OpenGL 기준)
    vertices = clipPolygonAgainstPlane(vertices, 2, -1);

    return vertices;
}

void Renderer::DebugNormalVector(const SRMath::vec3& v0_World, const SRMath::vec3& v1_World, const SRMath::vec3& v2_World, 
    const SRMath::vec3& n0_World, const SRMath::vec3& n1_World, const SRMath::vec3& n2_World, const SRMath::mat4& vp)
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
