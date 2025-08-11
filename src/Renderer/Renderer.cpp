#include "Renderer.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <omp.h>
#include <optional>

#include "Graphics/Mesh.h"
#include "Graphics/Texture.h"
#include "Math/AABB.h"
#include "Graphics/Octree.h"
#include "Math/Frustum.h"
#include "Scene/Camera.h"
#include "Graphics/light.h"
#include "Renderer/RenderQueue.h"
#include "Graphics/Material.h"

constexpr float FLOATINF = std::numeric_limits<float>::infinity();

// Vertex Shading
struct ShadedVertex {
    SRMath::vec3 pos_world;
    SRMath::vec4 pos_clip;
    SRMath::vec3 normal_world;
    SRMath::vec2 texcoord;

    // 생성자 추가 (컴파일 오류 해결)
    ShadedVertex() = default;
    ShadedVertex(const SRMath::vec3& posWorld, const SRMath::vec4& posClip, const SRMath::vec3& normalWorld, const SRMath::vec2& tex)
        : pos_world(posWorld), pos_clip(posClip), normal_world(normalWorld), texcoord(tex) { }
};

// --- 래스터화를 위한 최종 정점 데이터 준비 ---
struct RasterizerVertex {
    SRMath::vec2 screen_pos;          // 최종 화면 좌표
    float one_over_w;                 // 원근 보간을 위한 1/w
    SRMath::vec3 normal_world_over_w; // 원근 보정된 법선
    SRMath::vec2 texcoord_over_w;     // 원근 보정된 UV
    SRMath::vec3 world_pos_over_w;    // 월드 좌표
};

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

void Renderer::drawPixel(int x, int y, unsigned int color)
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
            drawPixel(x0, y0, color);
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
        drawPixel(x0, y0, color);
        return;
    }

    const double x_inc = dx / (double)steps;
    const double y_inc = dy / (double)steps;

    double x = x0;
    double y = y0;
    
    for (int i = 0; i <= steps; i++) {
        drawPixel(round(x), round(y), color);
        x += x_inc;
        y += y_inc;
    }
    
}


void Renderer::drawLine(int x0, int y0, int x1, int y1, unsigned int color)
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

void Renderer::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color)
{
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void Renderer::drawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color)
{
    drawTriangle(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, color);
}

void Renderer::SetLineAlgorithm(ELineAlgorithm eLineAlgorithm)
{
    m_currentLineAlgorithm = eLineAlgorithm;
}

void Renderer::Clear()
{
	memset(m_pPixelData, 0, m_width * m_height * sizeof(unsigned int));
	std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), std::numeric_limits<float>::lowest());   
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


void Renderer::drawFilledTriangle(const RasterizerVertex& v0, const RasterizerVertex& v1, const RasterizerVertex& v2, const Material* material, const std::vector<DirectionalLight>& lights, const SRMath::vec3& camPos)
{
    // 정점 좌표를 정수로 변환 (화면 픽셀 기준)
    const SRMath::vec2 p0 = { v0.screen_pos.x, v0.screen_pos.y };
    const SRMath::vec2 p1 = { v1.screen_pos.x, v1.screen_pos.y };
    const SRMath::vec2 p2 = { v2.screen_pos.x, v2.screen_pos.y };

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
                    v0.one_over_w * w_bary + v1.one_over_w * u_bary + v2.one_over_w * v_bary;

                int idx = y * m_width + x;
                if (interpolated_one_over_w > m_depthBuffer[idx])
                {
                    SRMath::vec3 normal_interpolated =
                        SRMath::normalize((v0.normal_world_over_w * w_bary + v1.normal_world_over_w * u_bary + v2.normal_world_over_w * v_bary) / interpolated_one_over_w);

                    SRMath::vec2 uv_over_w_interpolated = v0.texcoord_over_w * w_bary + v1.texcoord_over_w * u_bary + v2.texcoord_over_w * v_bary;
                    SRMath::vec2 uv_interpolated = uv_over_w_interpolated / interpolated_one_over_w;

                    SRMath::vec3 base_color;

                    if (material->diffuseTexture)
                    {
                        //base_color = material->diffuseTexture->GetPixels(uv_interpolated.x, uv_interpolated.y);
                    }
                    else {
                        base_color = material->kd; // 재질의 기본 난반사 색상
                    }

                    // 주변광 조명 계산
                    SRMath::vec3 ambient_color = material->ka; // 재질의 기본 주변광 색상

                    // 2. 여러 빛의 난반사/정반사 효과를 누적할 변수를 0으로 초기화합니다.
                    SRMath::vec3 total_diffuse_color = { 0.0f, 0.0f, 0.0f };
                    SRMath::vec3 total_specular_color = { 0.0f, 0.0f, 0.0f };

                    SRMath::vec3 interpolated_world_pos = 
                        (v0.world_pos_over_w * w_bary + v1.world_pos_over_w * u_bary + v2.world_pos_over_w * v_bary) / interpolated_one_over_w;
                    SRMath::vec3 view_dir = SRMath::normalize(camPos - interpolated_world_pos);

                    for (const auto& light : lights)
                    {
                        // 난반사 조명 계산
                        float diffuse_intensity = std::max(0.0f, dot(normal_interpolated, light.direction));
                        total_diffuse_color += base_color * diffuse_intensity * light.color;

                        // 정반사 조명 계산
                        SRMath::vec3 reflect_dir = SRMath::reflect(-1 * light.direction, normal_interpolated);
                        float spec_dot = SRMath::dot(view_dir, reflect_dir);
                        float spec_factor = std::pow(std::max(0.0f, spec_dot), material->Ns);
                        total_specular_color += material->ks * spec_factor * light.color;
                    }

                    SRMath::vec3 color = ambient_color + total_diffuse_color + total_specular_color;
                    
                    // 최종 색상의 각 채널(R, G, B)을 0.0과 1.0 사이로 클램핑합니다.
					color = color.clamp(0.f, 1.0f);

                    unsigned int final_color = RGB(
                        color.x * 255.f,
                        color.y * 255.f,
                        color.z * 255.f
                    );

                    m_depthBuffer[idx] = interpolated_one_over_w;
                    drawPixel(x, y, final_color);
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


void Renderer::resterization(const std::vector<ShadedVertex>& clipped_vertices,
    const Material* material, const std::vector<DirectionalLight>& lights, const SRMath::vec3 camPos, const MeshRenderCommand& cmd)
{
    std::vector<RasterizerVertex> final_vertices(clipped_vertices.size());

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
        if (cmd.rasterizeMode == ERasterizeMode::Fill)
            drawFilledTriangle(rv0, rv1, rv2, material, lights, camPos);
        else
            drawTriangle(rv0.screen_pos, rv1.screen_pos, rv2.screen_pos, RGB(255, 255, 255));
    }
}

void Renderer::drawDebugPrimitive(const DebugPrimitiveCommand& cmd, const SRMath::mat4& vp, const Camera& camera)
{
    const SRMath::mat4 modelMatrix = cmd.worldTransform;
    SRMath::mat4 mvp = vp * modelMatrix;

    switch (cmd.type)
    {
        // 선분 그리기
    case DebugPrimitiveType::Line:
    {
        SRMath::vec3 color = cmd.vertices[0].color;

        for (size_t i = 0; i < cmd.vertices.size() - 1; i += 2)
        {
            if (i + 1 >= cmd.vertices.size()) break;

            const DebugVertex& start_pos_world = cmd.vertices[i];
            const DebugVertex& end_pos_world = cmd.vertices[i + 1];

            auto start_clip = mvp * start_pos_world.position;
            auto end_clip = mvp * end_pos_world.position;

            if (start_clip.w <= 0.1f || end_clip.w <= 0.1f) continue;

            start_clip.x /= start_clip.w;
            start_clip.y /= start_clip.w;
            end_clip.x /= end_clip.w;
            end_clip.y /= end_clip.w;

            int startX = (start_clip.x + 1.0f) * 0.5f * m_width;
            int startY = (1.0f - start_clip.y) * 0.5f * m_height;
            int endX = (end_clip.x + 1.0f) * 0.5f * m_width;
            int endY = (1.0f - end_clip.y) * 0.5f * m_height;

            drawLine(startX, startY, endX, endY,
                RGB(color.x * 255.f, color.y * 255.f, color.z * 255.f));
        }
        break;
    }
    }

    // 다른 디버그 프리미티브 타입도 추가 가능
}

void Renderer::drawMesh(const MeshRenderCommand& cmd, const SRMath::mat4& viewMatrix, const SRMath::mat4& projectionMatrix, 
    const SRMath::vec3& camPos, const std::vector<DirectionalLight> lights)
{
    // 행렬 준비
	const SRMath::mat4& modelMatrix = cmd.worldTransform;
	SRMath::mat4 mvp = projectionMatrix * viewMatrix * modelMatrix;
    SRMath::mat4 normal_matrix_world;

    if (auto inv_t_opt = SRMath::inverse_transpose(modelMatrix))
    {
		normal_matrix_world = *inv_t_opt;
    }
    else
		normal_matrix_world = SRMath::mat4(modelMatrix); // 모델 행렬로 초기화

	// 삼각형 처리 단계
	const std::vector<unsigned int>& indices = *cmd.indicesToDraw;
    const std::vector<Vertex>& vertices = cmd.sourceMesh->vertices;

    for (size_t i = 0; i < indices.size(); i += 3)
    {
        unsigned int i0 = indices[i];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];

        const Vertex& v0_model = vertices[indices[i]];
        const Vertex& v1_model = vertices[indices[i + 1]];
        const Vertex& v2_model = vertices[indices[i + 2]];

        ShadedVertex sv[3];
        
        sv[0].pos_clip = mvp * v0_model.position;
        sv[1].pos_clip = mvp * v1_model.position;
        sv[2].pos_clip = mvp * v2_model.position;

        // 클립 공간 백페이스 컬링 로직
        SRMath::vec3 ndc0 = sv[0].pos_clip / sv[0].pos_clip.w;
        SRMath::vec3 ndc1 = sv[1].pos_clip / sv[1].pos_clip.w;
        SRMath::vec3 ndc2 = sv[2].pos_clip / sv[2].pos_clip.w;

        float area = (ndc1.x - ndc0.x) * (ndc2.y - ndc0.y) - (ndc1.y - ndc0.y) * (ndc2.x - ndc0.x);

        // CCW가 앞면일 때, 오른손->왼손 투영 변환을 거치면 NDC에서는 CW가 되므로 area가 음수가 됩니다.
        // 따라서 area가 0 이상(CW가 아니거나 퇴화)이면 컬링합니다.
        if (area >= 0.f) {
            continue;
        }

        // 3. 컬링을 통과한 후에만 나머지 속성들을 계산합니다.
        sv[0].pos_world = modelMatrix * v0_model.position;
        sv[0].normal_world = SRMath::normalize(normal_matrix_world * SRMath::vec4(v0_model.normal, 0.f));
        sv[0].texcoord = v0_model.texcoord;

        sv[1].pos_world = modelMatrix * v1_model.position;
        sv[1].normal_world = SRMath::normalize(normal_matrix_world * SRMath::vec4(v1_model.normal, 0.f));
        sv[1].texcoord = v1_model.texcoord;

        sv[2].pos_world = modelMatrix * v2_model.position;
        sv[2].normal_world = SRMath::normalize(normal_matrix_world * SRMath::vec4(v2_model.normal, 0.f));
        sv[2].texcoord = v2_model.texcoord;

        // 클리핑
        std::vector<ShadedVertex> clipped_vertices = clipTriangle(sv[0], sv[1], sv[2]);
        if (clipped_vertices.size() < 3) continue;
        
        // 래스터화 단계
        resterization(clipped_vertices, cmd.material, lights, camPos, cmd);
	}
}


// 속성(Attribute) 보간 함수
ShadedVertex Renderer::interpolate(const ShadedVertex& v0, const ShadedVertex& v1, float t)
{
    ShadedVertex result;
    // 모든 속성을 선형 보간합니다.
	result.pos_world = v0.pos_world + (v1.pos_world - v0.pos_world) * t;
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


void Renderer::RenderScene(const RenderQueue& queue, const Camera& camera, const std::vector<DirectionalLight>& lights)
{
    const SRMath::mat4& viewMatrix = camera.GetViewMatrix();
    const SRMath::mat4& projectionMatrix = camera.GetProjectionMatrix();
	const SRMath::mat4& vp = projectionMatrix * viewMatrix;

    for (const auto& cmd : queue.GetRenderCommands())
    {
        drawMesh(cmd, viewMatrix, projectionMatrix, camera.GetCameraPos(), lights);
    }

    for (const auto& cmd : queue.GetDebugCommands())
    {
        drawDebugPrimitive(cmd, vp, camera);
    }
}

void Renderer::OnResize(HWND hWnd)
{
    Shutdown();
    Initialize(hWnd);
}
