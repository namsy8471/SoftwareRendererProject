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
#include "Renderer/Tile.h"

// 부동소수 무한대 상수 (가독성용)
constexpr float FLOATINF = std::numeric_limits<float>::infinity();

// Vertex Shading
// 설명: 정점 셰이딩 결과(월드/클립 좌표, 법선, UV)를 담는 구조체
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
// 설명: 원근 분할 및 뷰포트 변환 이후, 픽셀 셰이딩에 필요한 데이터
struct RasterizerVertex {
    SRMath::vec2 screen_pos;          // 최종 화면 좌표
    float one_over_w;                 // 원근 보간을 위한 1/w
    SRMath::vec3 normal_world_over_w; // 원근 보정된 법선
    SRMath::vec2 texcoord_over_w;     // 원근 보정된 UV
    SRMath::vec3 world_pos_over_w;    // 원근 보정된 월드 좌표
};

// 설명: GDI 백버퍼/DC 초기 상태 설정
Renderer::Renderer() : m_hBitmap(nullptr), m_hMemDC(nullptr),
m_hOldBitmap(nullptr), m_height(), m_width(), m_pPixelData(nullptr)
{
    // 기본 멤버 초기화만 수행
}

// 설명: 리소스 해제는 Shutdown()에서 수행
Renderer::~Renderer()
{
    // 의도적으로 비움
}

// 설명: 렌더러 초기화 (윈도우 크기, 백버퍼/DIB 섹션 생성 등)
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
    
    // DIB 섹션 생성: CPU에서 직접 픽셀 접근 가능
    m_hBitmap = CreateDIBSection(m_hMemDC, &bmi, DIB_RGB_COLORS,
        (void**)&m_pPixelData, NULL, 0);

    if (!m_hBitmap)
    {
        // 실패 시 메모리 DC와 Screen DC를 해제하고 종료
        DeleteDC(m_hMemDC);
        ReleaseDC(hWnd, hScreenDC);
        return false;
    }

    // 깊이 버퍼 크기 재할당 (width * height)
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

// 설명: 생성의 역순으로 GDI 리소스 해제
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

// 설명: 화면 경계 검사 후 지정 좌표에 픽셀 기록
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
// 설명: 정수 기반 오차 누적 방식으로 선분을 그립니다.
void Renderer::drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color)
{
    const int dx = abs(x1 - x0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int dy = -abs(y1 - y0); // y는 음수인 것이 계산에 편리
    const int sy = (y0 < y1) ? 1 : -1;

    int err = dx + dy;

    while (true)
    {
        // 안전 인덱스 검사 후 픽셀 쓰기
        int idx = y0 * m_width + x0;
        if (idx >= 0 && idx < m_depthBuffer.size())
        {
            drawPixel(x0, y0, color);
        }

        // 종료 조건
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;

        // x 스텝 진행
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        // y 스텝 진행
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

// DDA(Digital Differential Analyzer) Algorithm
// 설명: 실수 증분 기반으로 선분을 그립니다.
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
        // 반올림하여 정수 픽셀에 기록
        drawPixel(round(x), round(y), color);
        x += x_inc;
        y += y_inc;
    }
    
}

// 설명: 현재 설정된 알고리즘으로 선분을 그립니다.
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

// 설명: 3개의 점으로 삼각형 외곽선 렌더링 (정수 좌표)
void Renderer::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color)
{
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

// 설명: 3개의 점으로 삼각형 외곽선 렌더링 (vec2 좌표)
void Renderer::drawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color)
{
    drawTriangle(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, color);
}

// 설명: 선 그리기 알고리즘 선택자 설정
void Renderer::SetLineAlgorithm(ELineAlgorithm eLineAlgorithm)
{
    m_currentLineAlgorithm = eLineAlgorithm;
}

// 설명: 컬러 버퍼/깊이 버퍼 클리어
void Renderer::Clear()
{
	// 화면을 검정(0)으로 초기화
	memset(m_pPixelData, 0, m_width * m_height * sizeof(unsigned int));
	// 깊이 버퍼는 가장 낮은 값으로 초기화 (더 큰 z^-1만 패스)
	std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), std::numeric_limits<float>::lowest());   
}

// 설명: 백버퍼(m_hMemDC)의 내용을 화면 DC로 복사 (Present)
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

// 설명: 바리센트릭/Phong 조명으로 채워진 삼각형을 그립니다. (전체 화면 대상)
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
                // 바리센트릭 합 (퇴화/정규화용)
                float total_w = static_cast<float>(w0 + w1 + w2);

                if (std::abs(total_w) < 1e-5f) continue;

                // 정규화된 가중치
                float w_bary = w0 / total_w;
                float u_bary = w1 / total_w;
                float v_bary = w2 / total_w;

                // 깊이 테스트용 1/w 보간
                float interpolated_one_over_w =
                    v0.one_over_w * w_bary + v1.one_over_w * u_bary + v2.one_over_w * v_bary;

                int idx = y * m_width + x;

                if (interpolated_one_over_w > m_depthBuffer[idx])
                {
                    // 법선 보간 후 정규화
                    SRMath::vec3 normal_interpolated =
                        SRMath::normalize((v0.normal_world_over_w * w_bary + v1.normal_world_over_w * u_bary + v2.normal_world_over_w * v_bary) / interpolated_one_over_w);

                    // UV 보간
                    SRMath::vec2 uv_over_w_interpolated = v0.texcoord_over_w * w_bary + v1.texcoord_over_w * u_bary + v2.texcoord_over_w * v_bary;
                    SRMath::vec2 uv_interpolated = uv_over_w_interpolated / interpolated_one_over_w;

                    SRMath::vec3 base_color;

                    if (material->diffuseTexture)
                    {
                        // 텍스처 샘플링이 필요할 경우 주석 해제
                        //base_color = material->diffuseTexture->GetPixels(uv_interpolated.x, uv_interpolated.y);
                    }
                    else {
                        base_color = material->kd; // 재질의 기본 난반사 색상
                    }

                    // 주변광 조명 계산
                    SRMath::vec3 ambient_color = material->ka; // 재질의 기본 주변광 색상

                    // 누적 조명 요소 초기화
                    SRMath::vec3 total_diffuse_color = { 0.0f, 0.0f, 0.0f };
                    SRMath::vec3 total_specular_color = { 0.0f, 0.0f, 0.0f };

                    // 월드 좌표 보간 및 시선 벡터 계산
                    SRMath::vec3 interpolated_world_pos = 
                        (v0.world_pos_over_w * w_bary + v1.world_pos_over_w * u_bary + v2.world_pos_over_w * v_bary) / interpolated_one_over_w;
                    SRMath::vec3 view_dir = SRMath::normalize(camPos - interpolated_world_pos);

                    // 방향성 광원 반복
                    for (const auto& light : lights)
                    {
                        // 난반사 조명 계산
                        float diffuse_intensity = std::max(0.0f, dot(normal_interpolated, light.direction));
                        total_diffuse_color += base_color * diffuse_intensity * light.color;

                        // 정반사 조명 계산 (Phong)
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
        final_vertices[j].world_pos_over_w = v_clip.pos_world * one_over_w;
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

        for (int i = 0; i < cmd.vertices.size() - 1; i += 2)
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

void Renderer::drawMesh(const MeshRenderCommand& cmd, const SRMath::mat4& vp, 
    const SRMath::vec3& camPos, const std::vector<DirectionalLight> lights)
{
    // 행렬 준비
	const SRMath::mat4& modelMatrix = cmd.worldTransform;
	SRMath::mat4 mvp = vp * modelMatrix;
    SRMath::mat4 normalMatrixWorld;

    if (auto inv_t_opt = SRMath::inverse_transpose(modelMatrix))
    {
		normalMatrixWorld = *inv_t_opt;
    }
    else
		normalMatrixWorld = SRMath::mat4(modelMatrix); // 모델 행렬로 초기화

	// 삼각형 처리 단계
	const std::vector<unsigned int>& indices = *cmd.indicesToDraw;
    const std::vector<Vertex>& vertices = cmd.sourceMesh->vertices;

    for (int i = 0; i < indices.size(); i += 3)
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
        sv[0].normal_world = SRMath::normalize(normalMatrixWorld * SRMath::vec4(v0_model.normal, 0.f));
        sv[0].texcoord = v0_model.texcoord;

        sv[1].pos_world = modelMatrix * v1_model.position;
        sv[1].normal_world = SRMath::normalize(normalMatrixWorld * SRMath::vec4(v1_model.normal, 0.f));
        sv[1].texcoord = v1_model.texcoord;

        sv[2].pos_world = modelMatrix * v2_model.position;
        sv[2].normal_world = SRMath::normalize(normalMatrixWorld * SRMath::vec4(v2_model.normal, 0.f));
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
	// Sutherland-Hodgman 알고리즘을 사용하여 두 정점 사이의 속성을 선형 보간합니다.

    ShadedVertex result;
    // 모든 속성을 선형 보간합니다.
    // 클립 공간 좌표는 t를 사용해 그대로 선형 보간합니다.
    // 결과 정점의 w 값은 이 보간을 통해 자연스럽게 계산됩니다.
    result.pos_clip = v0.pos_clip + (v1.pos_clip - v0.pos_clip) * t;

    // --- 원근 보정 시작 ---

    // 1. 각 정점의 1/w 값을 계산합니다.
    float inv_w0 = 1.0f / v0.pos_clip.w;
    float inv_w1 = 1.0f / v1.pos_clip.w;

    // 2. 1/w 값을 t를 이용해 선형 보간합니다.
    float interp_inv_w = inv_w0 + (inv_w1 - inv_w0) * t;

    // 3. 보간된 1/w의 역수를 취해, 새 정점의 w값에 해당하는 보정 계수를 구합니다.
    //    (사실 이 값은 result.pos_clip.w와 거의 동일합니다)
    float interp_w = 1.0f / interp_inv_w;

    // 4. 나머지 모든 속성들은 (속성/w) 값을 보간한 뒤, 보정된 w를 곱해줍니다.
    //    아래 수식은 (v0.attr/v0.w * (1-t) + v1.attr/v1.w * t) / (1/v0.w * (1-t) + 1/v1.w * t)
    //    를 정리한 형태입니다.
    result.pos_world = (v0.pos_world * inv_w0 * (1.0f - t) + v1.pos_world * inv_w1 * t) * interp_w;
    result.normal_world = (v0.normal_world * inv_w0 * (1.0f - t) + v1.normal_world * inv_w1 * t) * interp_w;
    result.texcoord = (v0.texcoord * inv_w0 * (1.0f - t) + v1.texcoord * inv_w1 * t) * interp_w;
	// --- 원근 보정 끝 ---

    return result;
}

// 하나의 평면으로 폴리곤을 클리핑하는 함수
void Renderer::clipPolygonAgainstPlane(std::vector<ShadedVertex>& out_vertices, const std::vector<ShadedVertex>& in_vertices, int plane_axis, int plane_sign)
{
	out_vertices.clear();

    for (size_t i = 0; i < in_vertices.size(); ++i)
    {
        const ShadedVertex& current_v = in_vertices[i];
        const ShadedVertex& prev_v = in_vertices[(i + in_vertices.size() - 1) % in_vertices.size()];

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
            out_vertices.push_back(intersection);
        }

        if (is_current_inside)
        {
            // 현재 정점이 평면 안쪽에 있으면 결과에 추가
            out_vertices.push_back(current_v);
        }
    }
}


// 삼각형을 6개의 절두체 평면으로 클리핑하는 메인 함수
std::vector<ShadedVertex> Renderer::clipTriangle(const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2)
{
    std::vector<ShadedVertex> vertices_buffer1;
    std::vector<ShadedVertex> vertices_buffer2;

    vertices_buffer1 = { v0, v1, v2 };

	auto* in_v = &vertices_buffer1;
	auto* out_v = &vertices_buffer2;

    // 1. Left Plane  ( w + x >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, 0, 1);
	std::swap(in_v, out_v);
    // 2. Right Plane ( w - x >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v,  0, -1);
    std::swap(in_v, out_v);
    // 3. Bottom Plane( w + y >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, 1, 1);
    std::swap(in_v, out_v);
    // 4. Top Plane   ( w - y >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, 1, -1);
    std::swap(in_v, out_v);
    // 5. Near Plane  ( w + z >= 0 )  (OpenGL 기준)
    clipPolygonAgainstPlane(*out_v, *in_v, 2, 1);
    std::swap(in_v, out_v);
    // 6. Far Plane   ( w - z >= 0 )  (OpenGL 기준)
    clipPolygonAgainstPlane(*out_v, *in_v, 2, -1);
    std::swap(in_v, out_v);

    return *in_v;
}


void Renderer::RenderScene(const RenderQueue& queue, const Camera& camera, const std::vector<DirectionalLight>& lights)
{
    const SRMath::mat4& viewMatrix = camera.GetViewMatrix();
    const SRMath::mat4& projectionMatrix = camera.GetProjectionMatrix();
	const SRMath::mat4& vp = projectionMatrix * viewMatrix;

    // 타일 데이터 버퍼 초기화
    int num_tiles_x = (m_width + TILE_SIZE - 1) / TILE_SIZE;
    int num_tiles_y = (m_height + TILE_SIZE - 1) / TILE_SIZE;
    std::vector<Tile> tiles(num_tiles_x * num_tiles_y);
    std::vector<std::vector<std::vector<TriangleRef>>> thread_local_storages;

#pragma omp parallel
    {

#pragma omp single
        {
            int num_actual_threads = omp_get_num_threads();
            thread_local_storages.resize(num_actual_threads);
            for (int i = 0; i < num_actual_threads; ++i) {
                thread_local_storages[i].resize(num_tiles_x * num_tiles_y);
            }
        }
        // --- 2. 병렬 Binning: 각 스레드는 자기 ID에 맞는 개인 사물함에만 접근 ---
        int thread_id = omp_get_thread_num();
        auto& my_local_tiles = thread_local_storages[thread_id]; // 참조로 편하게 사용

        // 🆕 per-thread scratch buffers (재사용 가능)
        std::vector<SRMath::vec4> transformed_clip;
        std::vector<int> stamp;
        transformed_clip.reserve(65536); // 충분한 초기 용량
        stamp.reserve(65536);

		int cmd_count = queue.GetRenderCommands().size();
#pragma omp for schedule(guided)
        for (int cmd_idx = 0; cmd_idx < cmd_count; ++cmd_idx)
        {
            //drawMesh(cmd, vp, camera.GetCameraPos(), lights);

            const auto& cmd = queue.GetRenderCommands()[cmd_idx];
            const Mesh* mesh = cmd.sourceMesh;
            const SRMath::mat4& worldTransform = cmd.worldTransform;
            const SRMath::mat4 mvp = vp * worldTransform;

            const auto& vertices = mesh->vertices;
            const auto& indices = *cmd.indicesToDraw; // 실제 그릴 인덱스 목록

            if ((int)transformed_clip.size() < (int)vertices.size()) {
                transformed_clip.resize(vertices.size());
                stamp.resize(vertices.size(), -1);
            }
            int my_stamp = cmd_idx;

			int indices_size = indices.size();
            // 2. 메쉬의 모든 '삼각형'을 순회합니다.
            for (size_t i = 0; i < indices_size; i += 3)
            {
                // 3. 삼각형의 세 정점 인덱스를 가져옵니다.
                uint32_t i0 = indices[i];
                uint32_t i1 = indices[i + 1];
                uint32_t i2 = indices[i + 2];

                // 4. 세 정점의 월드-뷰-프로젝션 변환을 수행하여 클립 공간 좌표를 구합니다.
                // 🆕 transform with caching
                if (stamp[i0] != my_stamp) {
                    transformed_clip[i0] = mvp * SRMath::vec4(vertices[i0].position, 1.0f);
                    stamp[i0] = my_stamp;
                }
                if (stamp[i1] != my_stamp) {
                    transformed_clip[i1] = mvp * SRMath::vec4(vertices[i1].position, 1.0f);
                    stamp[i1] = my_stamp;
                }
                if (stamp[i2] != my_stamp) {
                    transformed_clip[i2] = mvp * SRMath::vec4(vertices[i2].position, 1.0f);
                    stamp[i2] = my_stamp;
                }

                const SRMath::vec4& v0_clip = transformed_clip[i0];
                const SRMath::vec4& v1_clip = transformed_clip[i1];
                const SRMath::vec4& v2_clip = transformed_clip[i2];

                // ❗ (심화) W-Clipping: 정점의 w 값이 0 이하(카메라 뒤)이면 비정상적인 AABB가 계산될 수 있습니다.
                // 지금은 일단 생략하지만, 완성도 높은 렌더러를 위해서는 이 부분에 대한 처리가 필수적입니다.
                if (v0_clip.w <= 0 || v1_clip.w <= 0 || v2_clip.w <= 0) continue;

                // 5. 원근 나누기를 통해 NDC(-1~1) 좌표를 구합니다.
                SRMath::vec3 v0_ndc = SRMath::vec3(v0_clip) / v0_clip.w;
                SRMath::vec3 v1_ndc = SRMath::vec3(v1_clip) / v1_clip.w;
                SRMath::vec3 v2_ndc = SRMath::vec3(v2_clip) / v2_clip.w;

                // 6. 삼각형의 화면 공간 AABB를 계산합니다.
                float min_x = std::max(-1.0f, std::min({ v0_ndc.x, v1_ndc.x, v2_ndc.x }));
                float max_x = std::min(1.0f, std::max({ v0_ndc.x, v1_ndc.x, v2_ndc.x }));
                float min_y = std::max(-1.0f, std::min({ v0_ndc.y, v1_ndc.y, v2_ndc.y }));
                float max_y = std::min(1.0f, std::max({ v0_ndc.y, v1_ndc.y, v2_ndc.y }));

                if (max_x < min_x || max_y < min_y) continue; // 화면 밖

                // X축 뒤집기 적용
                auto ndc_to_tile_x = [&](float ndc_x) {
                    return ((ndc_x * 0.5f + 0.5f) * num_tiles_x);
                    };

                auto ndc_to_tile_y = [&](float ndc_y) {
                    // 화면 좌표계(Y=0 위쪽)로 변환하려면 NDC의 Y를 뒤집어야 함
                    return (((1.0f - ndc_y) * 0.5f) * num_tiles_y);
                    };

                // 7. AABB를 이용해 영향을 받는 타일 범위를 계산합니다.
                int min_tile_x = static_cast<int>(std::floor(ndc_to_tile_x(min_x))) - 1;
                int max_tile_x = static_cast<int>(std::ceil(ndc_to_tile_x(max_x)));
                int min_tile_y = static_cast<int>(std::floor(ndc_to_tile_y(max_y))) - 1;
                int max_tile_y = static_cast<int>(std::ceil(ndc_to_tile_y(min_y)));

                min_tile_x = std::clamp(min_tile_x, 0, num_tiles_x - 1);
                min_tile_y = std::clamp(min_tile_y, 0, num_tiles_y - 1);
                max_tile_x = std::clamp(max_tile_x, 0, num_tiles_x - 1);
                max_tile_y = std::clamp(max_tile_y, 0, num_tiles_y - 1);

                // 해당 '삼각형'을 영향을 받는 모든 타일에 추가합니다.
                TriangleRef tri_ref = { &cmd, static_cast<uint32_t>(i) };
                for (int ty = min_tile_y; ty < max_tile_y; ++ty) {
                    for (int tx = min_tile_x; tx < max_tile_x; ++tx) {
                        my_local_tiles[ty * num_tiles_x + tx].push_back(tri_ref);
                    }
                }
            }
        }

        // 각 타일에 해당하는 삼각형들을 병렬로 모읍니다.

        int tiles_size = tiles.size();
		int num_threads = thread_local_storages.size();

        #pragma omp for schedule(guided)
        for (int i = 0; i < tiles_size; ++i)
        {
            // 3-1. 최종 타일에 들어갈 삼각형의 총개수를 미리 계산
            size_t total_triangle_count = 0;
            for (int t = 0; t < num_threads; ++t) {
                total_triangle_count += thread_local_storages[t][i].size();
            }

            if (total_triangle_count == 0) continue;

            // 3-2. 단 한 번의 메모리 할당을 위해 용량을 완벽하게 예약! (재할당 방지)
            tiles[i].triangles.reserve(total_triangle_count);
            // 3-3. 각 스레드의 결과물을 재할당 없이 차례대로 삽입
            for (int t = 0; t < num_threads; ++t) {
                tiles[i].triangles.insert(
                    tiles[i].triangles.end(),
                    std::make_move_iterator(thread_local_storages[t][i].begin()), // 이동 반복자 시작
                    std::make_move_iterator(thread_local_storages[t][i].end()) // 이동 반복자 끝
                );
            }
        }

        // --- 직렬 병합 단계 이후 ---
        std::vector<int> active_tile_indices;
		active_tile_indices.reserve(tiles.size());

        for (int i = 0; i < tiles_size; ++i) {
            if (!tiles[i].triangles.empty()) {
                active_tile_indices.push_back(i);
            }
        }

        // --- 병렬 렌더링 단계 ---
        int active_tile_size = active_tile_indices.size();
        // 실제 작업이 있는 수백 개의 타일만 순회
#pragma omp for schedule(dynamic)
        for (int i = 0; i < active_tile_size; ++i) {
            int tile_idx = active_tile_indices[i];
            int tx = tile_idx % num_tiles_x;
            int ty = tile_idx / num_tiles_x;
            renderTile(tx, ty, tiles[tile_idx], vp, camera.GetCameraPos(), lights);
        }
    
		int queue_size = queue.GetDebugCommands().size();
#pragma omp for schedule(dynamic)
        for(int i = 0; i < queue_size; ++i)
        {
            const auto& cmd = queue.GetDebugCommands()[i];
            // 디버그 프리미티브를 렌더링합니다.
            drawDebugPrimitive(cmd, vp, camera);
		}
    }
}

void Renderer::renderTile(int tx, int ty, const Tile& tiles, const SRMath::mat4& vp, const SRMath::vec3& camPos, const std::vector<DirectionalLight> lights)
{
    // 타일의 화면 경계 계산
    int tile_minX = tx * TILE_SIZE;
    int tile_minY = ty * TILE_SIZE;
    int tile_maxX = std::min(tile_minX + TILE_SIZE, m_width);
    int tile_maxY = std::min(tile_minY + TILE_SIZE, m_height);

    // 타일 내 픽셀만 처리
    for (const auto& tri_ref : tiles.triangles)
    {
        const MeshRenderCommand* cmd = tri_ref.sourceCommand;
        const Mesh* mesh = cmd->sourceMesh;
        const SRMath::mat4& worldTransform = cmd->worldTransform;

        const SRMath::mat4 mvp = vp * worldTransform;
        const SRMath::mat4 inverseTransposeWorld =
            SRMath::inverse_transpose(worldTransform).value_or(SRMath::mat4(1.f));


        // 1. 삼각형의 정점 데이터를 가져옵니다.
        const auto& vertices = mesh->vertices;
        const auto& indices = *cmd->indicesToDraw;
        const Vertex& v0_in = vertices[indices[tri_ref.triangleIndex]];
        const Vertex& v1_in = vertices[indices[tri_ref.triangleIndex + 1]];
        const Vertex& v2_in = vertices[indices[tri_ref.triangleIndex + 2]];

        // 2. Vertex Shader 역할: 정점 변환, 셰이딩 속성 계산 등
        ShadedVertex sv0;
        ShadedVertex sv1;
        ShadedVertex sv2;

        sv0.pos_clip = mvp * SRMath::vec4(v0_in.position, 1.0f);
        sv1.pos_clip = mvp * SRMath::vec4(v1_in.position, 1.0f);
        sv2.pos_clip = mvp * SRMath::vec4(v2_in.position, 1.0f);

        // 클립 공간 백페이스 컬링 로직
        SRMath::vec3 ndc0 = sv0.pos_clip / sv0.pos_clip.w;
        SRMath::vec3 ndc1 = sv1.pos_clip / sv1.pos_clip.w;
        SRMath::vec3 ndc2 = sv2.pos_clip / sv2.pos_clip.w;

        float area = (ndc1.x - ndc0.x) * (ndc2.y - ndc0.y) - (ndc1.y - ndc0.y) * (ndc2.x - ndc0.x);

        // CCW가 앞면일 때, 오른손->왼손 투영 변환을 거치면 NDC에서는 CW가 되므로 area가 음수가 됩니다.
        // 따라서 area가 0 이상(CW가 아니거나 퇴화)이면 컬링합니다.
        if (area >= 0.f) {
            continue;
        }

        sv0.pos_world = SRMath::vec3(worldTransform * SRMath::vec4(v0_in.position, 1.0f));
        sv0.normal_world = SRMath::normalize(SRMath::vec3(inverseTransposeWorld * SRMath::vec4(v0_in.normal, 0.0f)));
        sv0.texcoord = v0_in.texcoord;

        // --- 두 번째 정점 (sv1) 계산 ---
        sv1.pos_world = SRMath::vec3(worldTransform * SRMath::vec4(v1_in.position, 1.0f));
        sv1.normal_world = SRMath::normalize(SRMath::vec3(inverseTransposeWorld * SRMath::vec4(v1_in.normal, 0.0f)));
        sv1.texcoord = v1_in.texcoord;

        // --- 세 번째 정점 (sv2) 계산 ---
        sv2.pos_world = SRMath::vec3(worldTransform * SRMath::vec4(v2_in.position, 1.0f));
        sv2.normal_world = SRMath::normalize(SRMath::vec3(inverseTransposeWorld * SRMath::vec4(v2_in.normal, 0.0f)));
        sv2.texcoord = v2_in.texcoord;

        // 클리핑 (Frustum Clipping)
        // 하나의 삼각형을 Frustum에 맞게 클리핑합니다. (Sutherland-Hodgman 알고리즘 등)
        // 결과로 3~N개의 정점을 가진 폴리곤이 나옵니다.
        std::vector<ShadedVertex> clipped_vertices = clipTriangle( sv0, sv1, sv2 );

        if(clipped_vertices.size() < 3)
        {
            // 클리핑 후 폴리곤이 3개 미만이면 렌더링하지 않습니다.
            continue;
		}

        // 래스터라이제이션
        resterizationForTile(clipped_vertices, cmd->material, lights, camPos,
            *cmd, tile_minX, tile_minY, tile_maxX, tile_maxY);
    }
}

void Renderer::resterizationForTile(const std::vector<ShadedVertex>& clipped_vertices, const Material* material, 
    const std::vector<DirectionalLight>& lights, const SRMath::vec3 camPos, const MeshRenderCommand& cmd, int tile_minX, int tile_minY, int tile_maxX, int tile_maxY)
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
        final_vertices[j].world_pos_over_w = v_clip.pos_world * one_over_w;
    }

    // --- 래스터화 (Fan Triangulation) ---
    for (size_t j = 1; j < final_vertices.size() - 1; ++j)
    {
        const auto& rv0 = final_vertices[0];
        const auto& rv1 = final_vertices[j];
        const auto& rv2 = final_vertices[j + 1];


        // 래스터라이저는 이제 화면 좌표와 원근 보정된 속성들을 받습니다.
        if (cmd.rasterizeMode == ERasterizeMode::Fill)
            drawFilledTriangleForTile(rv0, rv1, rv2, material, lights, camPos, 
                tile_minX, tile_minY, tile_maxX, tile_maxY);
        else
            drawTriangle(rv0.screen_pos, rv1.screen_pos, rv2.screen_pos, RGB(255, 255, 255));
    }
}

// drawFilledTriangle 함수 수정
void Renderer::drawFilledTriangleForTile(const RasterizerVertex& v0, const RasterizerVertex& v1, const RasterizerVertex& v2, 
    const Material* material, const std::vector<DirectionalLight>& lights, const SRMath::vec3& camPos, 
    int tile_minX, int tile_minY, int tile_maxX, int tile_maxY)
{
    // 정점 좌표를 정수로 변환 (화면 픽셀 기준)
    const SRMath::vec2 p0 = { v0.screen_pos.x, v0.screen_pos.y };
    const SRMath::vec2 p1 = { v1.screen_pos.x, v1.screen_pos.y };
    const SRMath::vec2 p2 = { v2.screen_pos.x, v2.screen_pos.y };

    // 1. 삼각형 자체의 스크린 공간 바운딩 박스를 계산합니다.
//    floor/ceil을 사용하여 부동소수점 좌표를 보수적으로 정수화합니다.
    int tri_minY = (std::floor(std::min({ p0.y, p1.y, p2.y })));
    int tri_maxY = (std::ceil(std::max({ p0.y, p1.y, p2.y })));
	int tri_minX = (std::floor(std::min({ p0.x, p1.x, p2.x })));
	int tri_maxX = (std::ceil(std::max({ p0.x, p1.x, p2.x })));

    // 2. ❗❗❗ 최종 루프 범위 계산 (교집합) - 이 부분이 올바른지 집중적으로 확인하세요!
    //    삼각형의 Y 시작점과 타일의 Y 시작점 중 '더 큰' 값에서 시작하고,
    //    삼각형의 Y 끝점과 타일의 Y 끝점 중 '더 작은' 값에서 끝나야 합니다.
    // 2. 최종 루프 범위 (교집합)
    int final_minX = std::max(tri_minX, tile_minX);
    int final_maxX = std::min(tri_maxX, tile_maxX - 1);
    int final_minY = std::max(tri_minY, tile_minY);
    int final_maxY = std::min(tri_maxY, tile_maxY - 1);

    // 교차 영역이 없으면 바로 종료
    if (final_minX > final_maxX || final_minY > final_maxY) return;

    // --- 사전 계산 단계 ---
    // 각 변(edge)의 x, y 변화량을 미리 계산해 둡니다.
    const float dx01 = p0.x - p1.x;
    const float dy01 = p0.y - p1.y;
    const float dx12 = p1.x - p2.x;
    const float dy12 = p1.y - p2.y;
    const float dx20 = p2.x - p0.x;
    const float dy20 = p2.y - p0.y;

    // 경계 상자의 시작점(minX, minY)에서의 바리센트릭 좌표 값을 계산합니다.
    float w0_row = dy12 * (final_minX - p1.x) - dx12 * (final_minY - p1.y);
    float w1_row = dy20 * (final_minX - p2.x) - dx20 * (final_minY - p2.y);
    float w2_row = dy01 * (final_minX - p0.x) - dx01 * (final_minY - p0.y);

    // --- 래스터화 루프 ---
    for (int y = final_minY; y <= final_maxY; ++y)
    {
        // 현재 행의 시작 값을 복사
        float w0 = w0_row;
        float w1 = w1_row;
        float w2 = w2_row;

        for (int x = final_minX; x <= final_maxX; ++x)
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
                        float diffuse_intensity = std::max(0.0f, dot(normal_interpolated, SRMath::normalize(light.direction)));
                        total_diffuse_color += base_color * diffuse_intensity * light.color;

                        // 정반사 조명 계산
                        SRMath::vec3 reflect_dir = SRMath::reflect(-1 * SRMath::normalize(light.direction), normal_interpolated);
                        float spec_dot = SRMath::dot(view_dir, reflect_dir);
                        float spec_factor = std::pow(std::max(0.0f, spec_dot), material->Ns);
                        total_specular_color += material->ks * spec_factor * light.color;
                    }

                    SRMath::vec3 color = ambient_color + total_diffuse_color + total_specular_color;

                    // 최종 색상의 각 채널(R, G, B)을 0.0과 1.0 사이로 클램핑합니다.
                    color.clamp(0.f, 1.0f);

                    unsigned int final_color = RGB(
                        color.x * 255.f,
                        color.y * 255.f,
                        color.z * 255.f
                    );

                    // 깊이 갱신 및 픽셀 쓰기
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

// 설명: 윈도우 리사이즈 대응 (리소스 재할당)
void Renderer::OnResize(HWND hWnd)
{
    Shutdown();
    Initialize(hWnd);
}
