#include "Renderer.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <omp.h>
#include <optional>
#include <unordered_map>

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
#include "Renderer/FXAA.h"
#include "Renderer/ShaderVertices.h"

// 부동소수 무한대 상수 (가독성용)
constexpr float FLOATINF = std::numeric_limits<float>::infinity();



// 설명: GDI 백버퍼/DC 초기 상태 설정
Renderer::Renderer() : m_hBitmap(nullptr), m_hMemDC(nullptr),
m_hOldBitmap(nullptr), m_height(), m_width(), m_pPixelData(nullptr)
{
    
}

// 설명: 리소스 해제는 Shutdown()에서 수행
Renderer::~Renderer()
{

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

// 생성의 역순으로 GDI 리소스 해제
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

// 화면 경계 검사 후 지정 좌표에 픽셀 기록
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
// 정수 기반 오차 누적 방식으로 선분을 그립니다.
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
// 실수 증분 기반으로 선분을 그립니다.
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

// 현재 설정된 알고리즘으로 선분을 그립니다.
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

// 3개의 점으로 삼각형 외곽선 렌더링 (정수 좌표)
void Renderer::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color)
{
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

// 3개의 점으로 삼각형 외곽선 렌더링 (vec2 좌표)
void Renderer::drawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color)
{
    drawTriangle(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, color);
}

// 렌더러 클리어
void Renderer::Clear()
{
	// 화면을 검정(0)으로 초기화
	memset(m_pPixelData, 0, m_width * m_height * sizeof(unsigned int));

    // 깊이 버퍼는 가장 낮은 값으로 초기화 (더 큰 z^-1만 패스)
	std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), std::numeric_limits<float>::lowest());

    int num_tiles_x = (m_width + TILE_SIZE - 1) / TILE_SIZE;
    int num_tiles_y = (m_height + TILE_SIZE - 1) / TILE_SIZE;
	int num_tiles = num_tiles_x * num_tiles_y;

    for(auto& storage : m_threadLocalStorages)
    {
        for(auto& tile_storage : storage)
        {
            tile_storage.clear();
        }
	}

    m_threadShadedVertexBuffers.clear();
	m_threadStamps.clear();
	m_threadClipBuffer1.clear();
	m_threadClipBuffer2.clear();
	m_threadClippedVertices.clear();
	m_threadNormalMatrixCache.clear();
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

// 속성(Attribute) 보간 함수
ShadedVertex Renderer::interpolate(const ShadedVertex& v0, const ShadedVertex& v1, float t)
{
	// Sutherland-Hodgman 알고리즘을 사용하여 두 정점 사이의 속성을 선형 보간합니다.

    ShadedVertex result;
    // 모든 속성을 선형 보간합니다.
    // 클립 공간 좌표는 t를 사용해 그대로 선형 보간합니다.
    // 결과 정점의 w 값은 이 보간을 통해 자연스럽게 계산됩니다.
    result.posClip = v0.posClip + (v1.posClip - v0.posClip) * t;

    // --- 원근 보정 시작 ---

    // 1. 각 정점의 1/w 값을 계산합니다.
    float inv_w0 = 1.0f / v0.posClip.w;
    float inv_w1 = 1.0f / v1.posClip.w;

    // 2. 1/w 값을 t를 이용해 선형 보간합니다.
    float interp_inv_w = inv_w0 + (inv_w1 - inv_w0) * t;

    // 3. 보간된 1/w의 역수를 취해, 새 정점의 w값에 해당하는 보정 계수를 구합니다.
    //    (사실 이 값은 result.pos_clip.w와 거의 동일합니다)
    float interp_w = 1.0f / interp_inv_w;

    // 4. 나머지 모든 속성들은 (속성/w) 값을 보간한 뒤, 보정된 w를 곱해줍니다.
    //    아래 수식은 (v0.attr/v0.w * (1-t) + v1.attr/v1.w * t) / (1/v0.w * (1-t) + 1/v1.w * t)
    //    를 정리한 형태입니다.
    result.posWorld = (v0.posWorld * inv_w0 * (1.0f - t) + v1.posWorld * inv_w1 * t) * interp_w;
    result.normalWorld = (v0.normalWorld * inv_w0 * (1.0f - t) + v1.normalWorld * inv_w1 * t) * interp_w;
    result.texcoord = (v0.texcoord * inv_w0 * (1.0f - t) + v1.texcoord * inv_w1 * t) * interp_w;
	// --- 원근 보정 끝 ---

    return result;
}

// 하나의 평면으로 폴리곤을 클리핑하는 함수
void Renderer::clipPolygonAgainstPlane(std::vector<ShadedVertex>& outVertices, 
    const std::vector<ShadedVertex>& inVertices, const __m128& plane)
{
	outVertices.clear();
    if(inVertices.empty()) return;

    // SIMD 최적화를 위해 0 벡터를 미리 생성
    const __m128 zero = _mm_setzero_ps();

    for (size_t i = 0; i < inVertices.size(); ++i)
    {
        const ShadedVertex& current_v = inVertices[i];
        const ShadedVertex& prev_v = inVertices[i == 0 ? inVertices.size() - 1 : i - 1];

        // 각 정점의 w 값에 plane_sign을 곱한 값과, 특정 축(axis) 값을 비교합니다.
        // 예: Left Plane (w + x >= 0) -> axis=0(x), sign=1. dist = w + x

        // --- SIMD 거리 계산 ---
        __m128 currentPosPs = _mm_load_ps(&current_v.posClip.x);
        __m128 prevPosPs = _mm_load_ps(&prev_v.posClip.x);

        __m128 distsPs = _mm_dp_ps(currentPosPs, plane, 0xF1); // dists_ps = [current_dist, ?, ?, ?]
        distsPs = _mm_add_ps(distsPs, _mm_dp_ps(prevPosPs, plane, 0xF2)); // dists_ps = [current_dist, prev_dist, ?, ?]

        // 비교 연산까지 모두 SIMD로 처리하여 파이프라인 스톨 제거
        __m128 inside_mask_ps = _mm_cmpge_ps(distsPs, zero); // [c_inside_mask, p_inside_mask, ?, ?]

        // SIMD 마스크 결과를 정수 하나로 매우 빠르게 추출
        int inside_mask = _mm_movemask_ps(inside_mask_ps);

        bool isCurrentInside = inside_mask & 1;
        bool isPrevInside =    inside_mask & 2;

        if (isCurrentInside != isPrevInside)
        {
            // 교차점 계산은 스칼라로 수행 (SIMD->스칼라 변환이 필요하므로)
            float currentDist = _mm_cvtss_f32(distsPs);
            float prevDist = _mm_cvtss_f32(_mm_shuffle_ps(distsPs, distsPs, _MM_SHUFFLE(1, 1, 1, 1)));

            // 두 정점이 평면을 기준으로 서로 다른 쪽에 있으면 교차점을 계산
            float t = prevDist / (prevDist - currentDist);
            ShadedVertex intersection = interpolate(prev_v, current_v, t);
            outVertices.emplace_back(intersection);
        }

        if (isCurrentInside)
        {
            // 현재 정점이 평면 안쪽에 있으면 결과에 추가
            outVertices.emplace_back(current_v);
        }
    }
}

// 각 비트는 특정 평면의 '바깥쪽'임을 의미합니다.
constexpr int INSIDEPLANE = 0;      // 000000
constexpr int LEFTPLANE = 1;      // 000001 (x < -w)
constexpr int RIGHTPLANE = 2;      // 000010 (x > w)
constexpr int BOTTOMPLANE = 4;      // 000100 (y < -w)
constexpr int TOPPLANE = 8;      // 001000 (y > w)
constexpr int NEARPLANE = 16;     // 010000 (z < -w)
constexpr int FARPLANE = 32;     // 100000 (z > w)

// 절두체 평면에 대한 아웃코드 계산 함수
int ComputeOutcode(const SRMath::vec4& posClip)
{
    int outcode = INSIDEPLANE; // 일단 안쪽이라고 가정

    if (posClip.x < -posClip.w) outcode |= LEFTPLANE;
    if (posClip.x > posClip.w) outcode |= RIGHTPLANE;
    if (posClip.y < -posClip.w) outcode |= BOTTOMPLANE;
    if (posClip.y > posClip.w) outcode |= TOPPLANE;
    if (posClip.z < -posClip.w) outcode |= NEARPLANE;   // OpenGL: -w, DirectX: 0
    if (posClip.z > posClip.w) outcode |= FARPLANE;

    return outcode;
}

// 삼각형을 6개의 절두체 평면으로 클리핑하는 메인 함수
void Renderer::clipTriangle(std::vector<ShadedVertex>& outVertices, const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2,
    std::vector<ShadedVertex>& buffer1, std::vector<ShadedVertex>& buffer2)
{
    buffer1 = { v0, v1, v2 };

	auto* in_v = &buffer1;
	auto* out_v = &buffer2;

	// 6개의 절두체 평면에 대해 클리핑을 수행합니다.
    // // _mm_set_ps는 인자 순서가 (w, z, y, x)로 역순임에 주의!
    const __m128 plane_left = _mm_set_ps(1.0f, 0.0f, 0.0f, 1.0f);  //  x + w >= 0
    const __m128 plane_right = _mm_set_ps(1.0f, 0.0f, 0.0f, -1.0f); // -x + w >= 0
    const __m128 plane_bottom = _mm_set_ps(1.0f, 0.0f, 1.0f, 0.0f);  //  y + w >= 0
    const __m128 plane_top = _mm_set_ps(1.0f, 0.0f, -1.0f, 0.0f); // -y + w >= 0
    const __m128 plane_near = _mm_set_ps(1.0f, 1.0f, 0.0f, 0.0f);  //  z + w >= 0
    const __m128 plane_far = _mm_set_ps(1.0f, -1.0f, 0.0f, 0.0f); // -z + w >= 0

    // Near Plane  ( w + z >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, plane_near);
    if (out_v->empty()) return; // 클리핑 결과가 비어있으면 더 이상 진행하지 않습니다.
    std::swap(in_v, out_v);

    // Far Plane   ( w - z >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, plane_far);
    if (out_v->empty()) return;
    std::swap(in_v, out_v);

    // Left Plane  ( w + x >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, plane_left);
    if( out_v->empty()) return; 
	std::swap(in_v, out_v);

    // Right Plane ( w - x >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, plane_right);
    if (out_v->empty()) return;
    std::swap(in_v, out_v);
    
    // Bottom Plane( w + y >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, plane_bottom);
    if (out_v->empty()) return;
    std::swap(in_v, out_v);
    
    // Top Plane   ( w - y >= 0 )
    clipPolygonAgainstPlane(*out_v, *in_v, plane_top);
    if (out_v->empty()) return;
    std::swap(in_v, out_v);
    
	outVertices = *in_v; // 최종 결과를 out_vertices에 저장
}


void Renderer::RenderScene(const RenderQueue& queue, const Camera& camera, const std::vector<DirectionalLight>& lights)
{
    const SRMath::mat4& viewMatrix = camera.GetViewMatrix();
    const SRMath::mat4& projectionMatrix = camera.GetProjectionMatrix();
	const SRMath::mat4& vp = projectionMatrix * viewMatrix;

    // 타일 데이터 버퍼 초기화
    int numTilesX = (m_width + TILE_SIZE - 1) / TILE_SIZE;
    int numTilesY = (m_height + TILE_SIZE - 1) / TILE_SIZE;

#pragma omp parallel
    {

#pragma omp single
        {
            int numThreads = omp_get_num_threads();

            // 스레드별 스크래치 버퍼들도 스레드 개수만큼 준비
            m_threadLocalStorages.resize(numThreads);
            m_threadTrianglePools.resize(numThreads);
            m_threadPoolCounters = std::make_unique<AlignedAtomicInt[]>(numThreads);
            m_threadShadedVertexBuffers.resize(numThreads);
            m_threadStamps.resize(numThreads);
            m_threadClipBuffer1.resize(numThreads); 
            m_threadClipBuffer2.resize(numThreads); 
            m_threadClippedVertices.resize(numThreads);
			m_threadNormalMatrixCache.resize(numThreads);

			const int MAX_TRIANGLES_PER_THREAD_POOL = 500000; // 메모리 할당
			
            // 각 스레드의 타일 스토리지 초기화
            for (int i = 0; i < numThreads; ++i) {
                m_threadLocalStorages[i].resize(numTilesX * numTilesY);
                m_threadTrianglePools[i].resize(MAX_TRIANGLES_PER_THREAD_POOL);

				m_threadShadedVertexBuffers[i].reserve(65536); // 65536은 최대 정점 수 (16K 타일 * 4096 정점)
				m_threadStamps[i].reserve(65536); // 스탬프 초기화

                m_threadClipBuffer1[i].reserve(6); // 6개 정점까지 capacity 넣음
                m_threadClipBuffer2[i].reserve(6); // 6개 정점까지 capacity 넣음
                m_threadClippedVertices[i].reserve(6); // 6개 정점까지 capacity 넣음
				m_threadNormalMatrixCache[i].rehash(65536); // 최대 65536개의 정점에 대한 역행렬 캐시
            }
        }

        // 병렬 Binning: 각 스레드는 자기 ID에 맞는 개인 사물함에만 접근
		int threadId = omp_get_thread_num();                    // 현재 스레드 ID
        auto& myLocalTiles = m_threadLocalStorages[threadId];   // 참조로 편하게 사용
		auto& myTrianglePool = m_threadTrianglePools[threadId]; // 각 스레드의 삼각형 풀
		auto& myPoolCounter = m_threadPoolCounters[threadId];   // 각 스레드의 풀 카운터

		auto& myThreadShadedVertex = m_threadShadedVertexBuffers[threadId];     // 각 스레드마다 타일에 클립 공간 좌표를 저장
		auto& myThreadStamp = m_threadStamps[threadId];                         // 각 스레드마다 타일에 스탬프를 저장
		auto& myThreadClipBuffer1 = m_threadClipBuffer1[threadId];              // 클리핑 스왑 버퍼 1
		auto& myThreadClipBuffer2 = m_threadClipBuffer2[threadId];              // 클리핑 스왑 버퍼 2
		auto& myThreadClippedVertices = m_threadClippedVertices[threadId];      // 클리핑된 정점 저장
		auto& myThreadNormalMatrixCache = m_threadNormalMatrixCache[threadId];  // 역행렬 캐시

        myPoolCounter.value = 0;

		int cmd_count = queue.GetRenderCommands().size();
#pragma omp for schedule(static)
        for (int cmd_idx = 0; cmd_idx < cmd_count; ++cmd_idx)
        {
            const auto& cmd = queue.GetRenderCommands()[cmd_idx];
            const Mesh* mesh = cmd.sourceMesh;
            const SRMath::mat4& worldTransform = cmd.worldTransform;
            const SRMath::mat4 mvp = vp * worldTransform;

            if (myThreadNormalMatrixCache.find(&cmd) == myThreadNormalMatrixCache.end())
                myThreadNormalMatrixCache.try_emplace(&cmd, SRMath::inverse_transpose(worldTransform).value_or(SRMath::mat4(1.f)));

            const SRMath::mat4 inverseTransposeWorld = myThreadNormalMatrixCache.at(&cmd);

            const auto& vertices = mesh->vertices;
            const auto& indices = *cmd.indicesToDraw; // 실제 그릴 인덱스 목록

            if ((int)myThreadShadedVertex.size() < (int)vertices.size()) {
                myThreadShadedVertex.resize(vertices.size());
                myThreadStamp.resize(vertices.size(), -1);
            }

            int myStamp = cmd_idx;
			int indices_size = indices.size();

            // 메쉬의 모든 '삼각형'을 순회합니다.
            for (size_t i = 0; i < indices_size; i += 3)
            {
                // 삼각형의 세 정점 인덱스를 가져옵니다.
                uint32_t i0 = indices[i];
                uint32_t i1 = indices[i + 1];
                uint32_t i2 = indices[i + 2];

                ShadedVertex sv0, sv1, sv2;

                // --- 정점 0 셰이딩 및 캐싱 ---
                if (myThreadStamp[i0] != myStamp) {
                    // 캐시 미스(Cache Miss): 처음 보는 정점이므로 모든 셰이딩 수행
                    const Vertex& vIn = vertices[i0];
                    ShadedVertex temp_sv;
                    temp_sv.posClip = mvp * SRMath::vec4(vIn.position, 1.0f);
                    temp_sv.posWorld = SRMath::vec3(worldTransform * SRMath::vec4(vIn.position, 1.0f));
                    temp_sv.normalWorld = SRMath::normalize(SRMath::vec3(inverseTransposeWorld * SRMath::vec4(vIn.normal, 0.0f)));
                    temp_sv.texcoord = vIn.texcoord;

                    // 계산이 끝난 ShadedVertex를 캐시에 저장
                    myThreadShadedVertex[i0] = temp_sv;
                    myThreadStamp[i0] = myStamp;
                }

                sv0 = myThreadShadedVertex[i0];

                // --- 정점 1 셰이딩 및 캐싱 ---
                if (myThreadStamp[i1] != myStamp) {
                    // 캐시 미스(Cache Miss): 처음 보는 정점이므로 모든 셰이딩 수행
                    const Vertex& vIn = vertices[i1];
                    ShadedVertex temp_sv;
                    temp_sv.posClip = mvp * SRMath::vec4(vIn.position, 1.0f);
                    temp_sv.posWorld = SRMath::vec3(worldTransform * SRMath::vec4(vIn.position, 1.0f));
                    temp_sv.normalWorld = SRMath::normalize(SRMath::vec3(inverseTransposeWorld * SRMath::vec4(vIn.normal, 0.0f)));
                    temp_sv.texcoord = vIn.texcoord;

                    // 계산이 끝난 ShadedVertex를 캐시에 저장
                    myThreadShadedVertex[i1] = temp_sv;
                    myThreadStamp[i1] = myStamp;
                }

                sv1 = myThreadShadedVertex[i1];

                // --- 정점 2 셰이딩 및 캐싱 ---
                if (myThreadStamp[i2] != myStamp) {
                    // 캐시 미스(Cache Miss): 처음 보는 정점이므로 모든 셰이딩 수행
                    const Vertex& vIn = vertices[i2];
                    ShadedVertex temp_sv;
                    temp_sv.posClip = mvp * SRMath::vec4(vIn.position, 1.0f);
                    temp_sv.posWorld = SRMath::vec3(worldTransform * SRMath::vec4(vIn.position, 1.0f));
                    temp_sv.normalWorld = SRMath::normalize(SRMath::vec3(inverseTransposeWorld * SRMath::vec4(vIn.normal, 0.0f)));
                    temp_sv.texcoord = vIn.texcoord;

                    // 계산이 끝난 ShadedVertex를 캐시에 저장
                    myThreadShadedVertex[i2] = temp_sv;
                    myThreadStamp[i2] = myStamp;
                }

                sv2 = myThreadShadedVertex[i2];


                const SRMath::vec4& v0Clip = sv0.posClip;
                const SRMath::vec4& v1Clip = sv1.posClip;
                const SRMath::vec4& v2Clip = sv2.posClip;

                // 원근 나누기를 통해 NDC(-1~1) 좌표를 구합니다.
                SRMath::vec3 v0Ndc = SRMath::vec3(v0Clip) / v0Clip.w;
                SRMath::vec3 v1Ndc = SRMath::vec3(v1Clip) / v1Clip.w;
                SRMath::vec3 v2Ndc = SRMath::vec3(v2Clip) / v2Clip.w;

                float area = (v1Ndc.x - v0Ndc.x) * (v2Ndc.y - v0Ndc.y) - (v1Ndc.y - v0Ndc.y) * (v2Ndc.x - v0Ndc.x);

                // CCW가 앞면일 때, 오른손->왼손 투영 변환을 거치면 NDC에서는 CW가 되므로 area가 음수가 됩니다.
                // 따라서 area가 0 이상(CW가 아니거나 퇴화)이면 컬링합니다.
                if (area >= 0.f) {
                    continue;
                }

                // 세 정점의 아웃코드를 각각 계산합니다.
                int outcode0 = ComputeOutcode(v0Clip);
                int outcode1 = ComputeOutcode(v1Clip);
                int outcode2 = ComputeOutcode(v2Clip);

                // Trival Rejection Test(순회 기각 테스트) (Outcode가 모두 INSIDEPLANE이면 클리핑 필요 없음)
                if ((outcode0 & outcode1 & outcode2) != 0)
                {
                    // 세 아웃코드의 AND 연산 결과가 0이 아니라는 것은,
                    // 세 정점 모두에게 켜져 있는 공통 비트가 있다는 의미입니다.
                    // 즉, 세 정점 모두가 '같은 평면'의 바깥쪽에 있다는 뜻이므로,
                    // 이 삼각형은 절대로 보일 수 없습니다.
                    continue; // 즉시 다음 삼각형으로 넘어감
                }

                myThreadClippedVertices.clear(); // 이전에 저장된 정점들을 비웁니다

                if ((outcode0 | outcode1 | outcode2) == 0) {
                    // Trivial Acceptance: 원본 셰이딩된 정점 3개를 그대로 사용
                    myThreadClippedVertices.emplace_back(sv0);
                    myThreadClippedVertices.emplace_back(sv1);
                    myThreadClippedVertices.emplace_back(sv2);
                }
                else {
                    // Clipping Path: 클리핑 수행. 결과는 verticesToBin에 저장됨
                    clipTriangle(myThreadClippedVertices, sv0, sv1, sv2, myThreadClipBuffer1, myThreadClipBuffer2);
                }

                // 클리핑된 결과(폴리곤)를 삼각형 팬(Fan)으로 분할 후 Binning
                if (myThreadClippedVertices.size() < 3) continue;

                for (size_t j = 1; j < myThreadClippedVertices.size() - 1; ++j)
                {
                    const auto& final_v0 = myThreadClippedVertices[0];
                    const auto& final_v1 = myThreadClippedVertices[j];
                    const auto& final_v2 = myThreadClippedVertices[j + 1];

                    // --- '클리핑된 최종 삼각형'으로 NDC와 AABB를 계산 ---
                    // 이제 이 정점들은 w>0 임이 보장되므로, NDC 계산이 안전합니다.
                    SRMath::vec3 v0_ndc = SRMath::vec3(final_v0.posClip) / final_v0.posClip.w;
                    SRMath::vec3 v1_ndc = SRMath::vec3(final_v1.posClip) / final_v1.posClip.w;
                    SRMath::vec3 v2_ndc = SRMath::vec3(final_v2.posClip) / final_v2.posClip.w;

                    float min_x = std::max(-1.0f, std::min({ v0_ndc.x, v1_ndc.x, v2_ndc.x }));
                    float max_x = std::min(1.0f, std::max({ v0_ndc.x, v1_ndc.x, v2_ndc.x }));
                    float min_y = std::max(-1.0f, std::min({ v0_ndc.y, v1_ndc.y, v2_ndc.y }));
                    float max_y = std::min(1.0f, std::max({ v0_ndc.y, v1_ndc.y, v2_ndc.y }));

                    if (max_x < min_x || max_y < min_y) continue;

                    // X축 뒤집기 적용
                    auto ndcToTileX = [&](float ndc_x) {
                        return ((ndc_x * 0.5f + 0.5f) * numTilesX);
                        };

                    auto ndcToTileY = [&](float ndc_y) {
                        // 화면 좌표계(Y=0 위쪽)로 변환하려면 NDC의 Y를 뒤집어야 함
                        return (((1.0f - ndc_y) * 0.5f) * numTilesY);
                        };

                    // AABB를 이용해 영향을 받는 타일 범위를 계산합니다.
                    int minTileX = static_cast<int>(std::floor(ndcToTileX(min_x))) - 1;
                    int maxTileX = static_cast<int>(std::floor(ndcToTileX(max_x)));
                    int minTileY = static_cast<int>(std::floor(ndcToTileY(max_y))) - 1;
                    int maxTileY = static_cast<int>(std::floor(ndcToTileY(min_y)));

                    minTileX = std::clamp(minTileX, 0, numTilesX - 1);
                    minTileY = std::clamp(minTileY, 0, numTilesY - 1);
                    maxTileX = std::clamp(maxTileX, 0, numTilesX - 1);
					maxTileY = std::clamp(maxTileY, 0, numTilesY - 1);

                    // TriangleRef 구조체도 최종 정점 데이터를 담도록 수정해야 합니다.
                    TriangleRef finalTri = { &cmd, final_v0, final_v1, final_v2 };

                    // 풀에서 다음 객체의 인덱스를 가져옵니다. (Thread-safe)
                    int poolIndex = myPoolCounter.value.fetch_add(1);

                    // (선택사항) 풀이 가득 찼는지 확인하는 방어 코드
                    if (poolIndex >= myTrianglePool.size()) {
                        // 풀이 넘쳤음! 에러 처리 또는 풀 크기 늘리기
                        continue;
                    }

                    // 풀에서 해당 인덱스의 객체에 대한 포인터를 가져옵니다.
                    TriangleRef* triPtr = &myTrianglePool[poolIndex];

                    // 가져온 객체에 최종 삼각형 데이터를 복사합니다.
                    *triPtr = finalTri; // finalTri는 이전에 계산된 최종 삼각형 데이터

                    for (int ty = minTileY; ty <= maxTileY; ++ty) {
                        for (int tx = minTileX; tx <= maxTileX; ++tx) {
                            myLocalTiles[ty * numTilesX + tx].emplace_back(triPtr);
                        }
                    }
                }
            }
        }

        // --- 병렬 렌더링 단계 ---
        int totalTiles = numTilesX * numTilesY;
#pragma omp for schedule(dynamic)
        for (int tileIdx = 0; tileIdx < totalTiles; ++tileIdx)
        {
            int tx = tileIdx % numTilesX;
            int ty = tileIdx / numTilesX;

            #pragma omp task
            renderTile(tx, ty, numTilesX, m_threadLocalStorages, camera.GetCameraPos(), lights);
        }

    
		int queueSize = queue.GetDebugCommands().size();
#pragma omp for schedule(dynamic)
        for(int i = 0; i < queueSize; ++i)
        {
            const auto& cmd = queue.GetDebugCommands()[i];
            // 디버그 프리미티브를 렌더링합니다.
            drawDebugPrimitive(cmd, vp, camera);
		}
    }

    switch (m_currentAAAlgorithm)
    {
    case EAAAlgorithm::None:
        break;
    case EAAAlgorithm::FXAA:
        {
		    // MSAA를 적용합니다.
            std::vector<unsigned int> copyPixelData(m_width * m_height);
            std::copy(m_pPixelData, m_pPixelData + m_width * m_height, copyPixelData.data());        
		    ApplyFXAA(copyPixelData.data(), m_pPixelData, m_width, m_height);
            break;
        }
    default:
        break;
    }
}

void Renderer::renderTile(int tx, int ty, int numTilesX, const std::vector<std::vector<std::vector<TriangleRef*>>>& threadLocalStorages,
    const SRMath::vec3& camPos, const std::vector<DirectionalLight>& lights)
{
    // 타일의 화면 경계 계산
    int tileMinX = tx * TILE_SIZE;
    int tileMinY = ty * TILE_SIZE;
    int tileMaxX = std::min(tileMinX + TILE_SIZE, m_width);
    int tileMaxY = std::min(tileMinY + TILE_SIZE, m_height);

    int tileIdx = ty * numTilesX + tx;

    for (size_t t = 0; t < threadLocalStorages.size(); ++t)
    {
        // t번 스레드가 이 타일(tileIdx)을 위해 분류해 둔 삼각형 목록(bin)을 가져옵니다.
        const auto& triangleBin = threadLocalStorages[t][tileIdx];

        for (const auto& triRef : triangleBin)
        {
            const MeshRenderCommand* cmd = triRef->sourceCommand;
            const Mesh* mesh = cmd->sourceMesh;

            // 래스터라이제이션
            resterizationForTile(triRef->sv0, triRef->sv1, triRef->sv2, cmd->material, lights, camPos,
                *cmd, tileMinX, tileMinY, tileMaxX, tileMaxY);
        }
    }

}

void Renderer::resterizationForTile(const ShadedVertex& sv0, const ShadedVertex& sv1, const ShadedVertex& sv2, const Material* material,
    const std::vector<DirectionalLight>& lights, const SRMath::vec3& camPos, const MeshRenderCommand& cmd, int tileMinX, int tileMinY, int tileMaxX, int tileMaxY)
{
    // 모든 클리핑된 정점에 대해 원근 분할 및 뷰포트 변환을 먼저 수행합니다.
    RasterizerVertex rv0, rv1, rv2;
   
    // --- 정점 0 계산 ---
    const float oneOverW0 = 1.0f / sv0.posClip.w;
    const SRMath::vec3 posNdc0 = SRMath::vec3(sv0.posClip) * oneOverW0;
    rv0.screenPos.x = (posNdc0.x + 1.0f) * 0.5f * m_width;
    rv0.screenPos.y = (1.0f - posNdc0.y) * 0.5f * m_height;
    rv0.oneOverW = oneOverW0;
    rv0.normalWorldOverW = sv0.normalWorld * oneOverW0;
    rv0.texcoordOverW = sv0.texcoord * oneOverW0;       
    rv0.worldPosOverW = sv0.posWorld * oneOverW0;         

    // --- 정점 1 계산 ---
    const float oneOverW1 = 1.0f / sv1.posClip.w;
    const SRMath::vec3 posNdc1 = SRMath::vec3(sv1.posClip) * oneOverW1;
    rv1.screenPos.x = (posNdc1.x + 1.0f) * 0.5f * m_width;
    rv1.screenPos.y = (1.0f - posNdc1.y) * 0.5f * m_height;
    rv1.oneOverW = oneOverW1;
    rv1.normalWorldOverW = sv1.normalWorld * oneOverW1;
    rv1.texcoordOverW = sv1.texcoord * oneOverW1;
    rv1.worldPosOverW = sv1.posWorld * oneOverW1;

    // --- 정점 2 계산 ---
    const float oneOverW2 = 1.0f / sv2.posClip.w;
    const SRMath::vec3 posNdc2 = SRMath::vec3(sv2.posClip) * oneOverW2;
    rv2.screenPos.x = (posNdc2.x + 1.0f) * 0.5f * m_width;
    rv2.screenPos.y = (1.0f - posNdc2.y) * 0.5f * m_height;
    rv2.oneOverW = oneOverW2;
    rv2.normalWorldOverW = sv2.normalWorld * oneOverW2;
    rv2.texcoordOverW = sv2.texcoord * oneOverW2;
    rv2.worldPosOverW = sv2.posWorld * oneOverW2;
    
    // 래스터라이저는 이제 화면 좌표와 원근 보정된 속성들을 받습니다.
    if (cmd.rasterizeMode == ERasterizeMode::Fill)
        drawFilledTriangleForTile(rv0, rv1, rv2, material, lights, camPos, 
            tileMinX, tileMinY, tileMaxX, tileMaxY);
    else
        drawTriangle(rv0.screenPos, rv1.screenPos, rv2.screenPos, RGB(255, 255, 255));
}

// drawFilledTriangle 함수 수정
void Renderer::drawFilledTriangleForTile(const RasterizerVertex& v0, const RasterizerVertex& v1, const RasterizerVertex& v2, 
    const Material* material, const std::vector<DirectionalLight>& lights, const SRMath::vec3& camPos, 
    int tileMinX, int tileMinY, int tileMaxX, int tileMaxY)
{
    // 정점 좌표를 정수로 변환 (화면 픽셀 기준)
    const SRMath::vec2 p0 = { v0.screenPos.x, v0.screenPos.y };
    const SRMath::vec2 p1 = { v1.screenPos.x, v1.screenPos.y };
    const SRMath::vec2 p2 = { v2.screenPos.x, v2.screenPos.y };

    // 삼각형 자체의 스크린 공간 바운딩 박스를 계산합니다.
    const __m128 min_xy = _mm_min_ps(v0.screenPos.m128, _mm_min_ps(v1.screenPos.m128, v2.screenPos.m128));
    const __m128 max_xy = _mm_max_ps(v0.screenPos.m128, _mm_max_ps(v1.screenPos.m128, v2.screenPos.m128));

	const __m128i min_xy_i = _mm_cvttps_epi32(min_xy);
	const __m128i max_xy_i = _mm_cvttps_epi32(max_xy);

	alignas(16) int min_xy_arr[4];
	alignas(16) int max_xy_arr[4];

	_mm_store_si128(reinterpret_cast<__m128i*>(min_xy_arr), min_xy_i);
	_mm_store_si128(reinterpret_cast<__m128i*>(max_xy_arr), max_xy_i);

    int triMinX = min_xy_arr[0];
    int triMinY = min_xy_arr[1];
	int triMaxX = max_xy_arr[0];
    int triMaxY = max_xy_arr[1];

    // 최종 루프 범위 계산 (교집합) - 이 부분이 올바른지 집중적으로 확인하세요!
    // 삼각형의 Y 시작점과 타일의 Y 시작점 중 '더 큰' 값에서 시작하고,
    // 삼각형의 Y 끝점과 타일의 Y 끝점 중 '더 작은' 값에서 끝나야 합니다.
    // 최종 루프 범위 (교집합)
    int finalMinX = std::max(triMinX, tileMinX);
    int finalMaxX = std::min(triMaxX, tileMaxX - 1);
    int finalMinY = std::max(triMinY, tileMinY);
    int finalMaxY = std::min(triMaxY, tileMaxY - 1);

    // 교차 영역이 없으면 바로 종료
    if (finalMinX > finalMaxX || finalMinY > finalMaxY) return;

    // --- 사전 계산 단계 ---
    // 각 변(edge)의 x, y 변화량을 미리 계산해 둡니다.
    const float dx01 = p0.x - p1.x;
    const float dy01 = p0.y - p1.y;
    const float dx12 = p1.x - p2.x;
    const float dy12 = p1.y - p2.y;
    const float dx20 = p2.x - p0.x;
    const float dy20 = p2.y - p0.y;

    // 경계 상자의 시작점(minX, minY)에서의 바리센트릭 좌표 값을 계산합니다.
    float w0Row = dy12 * (finalMinX + 0.5f - p1.x) - dx12 * (finalMinY + 0.5f - p1.y);
    float w1Row = dy20 * (finalMinX + 0.5f - p2.x) - dx20 * (finalMinY + 0.5f - p2.y);
    float w2Row = dy01 * (finalMinX + 0.5f - p0.x) - dx01 * (finalMinY + 0.5f - p0.y);

	// 고정 소수점으로 변환
    const SRMath::FixedPoint<16> dx01_fixed = SRMath::FixedPoint<16>(dx01);
    const SRMath::FixedPoint<16> dy01_fixed = SRMath::FixedPoint<16>(dy01);
    const SRMath::FixedPoint<16> dx12_fixed = SRMath::FixedPoint<16>(dx12);
    const SRMath::FixedPoint<16> dy12_fixed = SRMath::FixedPoint<16>(dy12);
    const SRMath::FixedPoint<16> dx20_fixed = SRMath::FixedPoint<16>(dx20);
    const SRMath::FixedPoint<16> dy20_fixed = SRMath::FixedPoint<16>(dy20);

	SRMath::FixedPoint<16> w0Row_fixed = SRMath::FixedPoint<16>(w0Row);
	SRMath::FixedPoint<16> w1Row_fixed = SRMath::FixedPoint<16>(w1Row);
	SRMath::FixedPoint<16> w2Row_fixed = SRMath::FixedPoint<16>(w2Row);

    // --- 래스터화 루프 ---
    for (int y = finalMinY; y <= finalMaxY; ++y)
    {
        // 현재 행의 시작 값을 복사
        SRMath::FixedPoint<16> w0_fixed = w0Row_fixed;
        SRMath::FixedPoint<16> w1_fixed = w1Row_fixed;
        SRMath::FixedPoint<16> w2_fixed = w2Row_fixed;

        for (int x = finalMinX; x <= finalMaxX; ++x)
        {
            // 바리센트릭 좌표가 모두 양수이면 삼각형 내부에 있는 것입니다.
            if ((w0_fixed.value | w1_fixed.value | w2_fixed.value) >= 0)
            {
				float w0 = w0_fixed.toFloat();
				float w1 = w1_fixed.toFloat();
				float w2 = w2_fixed.toFloat();

                float total_w = static_cast<float>(w0 + w1 + w2);

                if (std::abs(total_w) < 1e-5f) continue;

				float one_over_total_w = 1.0f / total_w;
                float wBary = w0 * one_over_total_w;
                float uBary = w1 * one_over_total_w;
                float vBary = w2 * one_over_total_w;

                float interpolatedOneOverW = v0.oneOverW * wBary;
                      interpolatedOneOverW += v1.oneOverW * uBary;
                      interpolatedOneOverW += v2.oneOverW * vBary;

                int idx = y * m_width + x;
                if (interpolatedOneOverW > m_depthBuffer[idx])
                {
					float oneOverInterpolatedOneOverW = 1.0f / interpolatedOneOverW;

                    SRMath::vec3 normalInterpolated = v0.normalWorldOverW * wBary;
                                 normalInterpolated += v1.normalWorldOverW * uBary;
                                 normalInterpolated += v2.normalWorldOverW * vBary;
                                 normalInterpolated *= oneOverInterpolatedOneOverW;

                    SRMath::vec2 uvOverWInterpolated = v0.texcoordOverW * wBary;
                                 uvOverWInterpolated += v1.texcoordOverW * uBary;
                                 uvOverWInterpolated += v2.texcoordOverW * vBary;
                    
                    SRMath::vec2 uv_interpolated = uvOverWInterpolated * oneOverInterpolatedOneOverW;

                    SRMath::vec3 base_color;

                    if (material->diffuseTexture)
                    {
                        base_color = material->diffuseTexture->GetPixels(uv_interpolated.x, uv_interpolated.y);
                    }
                    else {
                        base_color = material->kd; // 재질의 기본 난반사 색상
                    }

                    // 주변광 조명 계산
                    SRMath::vec3 ambient_color = material->ka; // 재질의 기본 주변광 색상

                    // 여러 빛의 난반사/정반사 효과를 누적할 변수를 0으로 초기화합니다.
                    SRMath::vec3 totalDiffuseColor = { 0.0f, 0.0f, 0.0f };
                    SRMath::vec3 totalSpecularColor = { 0.0f, 0.0f, 0.0f };

                    SRMath::vec3 interpolatedWorldPos = v0.worldPosOverW * wBary;
                                 interpolatedWorldPos += v1.worldPosOverW * uBary;
                                 interpolatedWorldPos += v2.worldPosOverW * vBary;
                                 interpolatedWorldPos *= oneOverInterpolatedOneOverW;
                                 interpolatedWorldPos = SRMath::normalize(interpolatedWorldPos);

                    SRMath::vec3 viewDir = SRMath::normalize(camPos - interpolatedWorldPos);

                    for (const auto& light : lights)
                    {
                        SRMath::vec3 lightDir = SRMath::normalize(light.direction);

                        // 난반사 조명 계산
                        float diffuse_intensity = std::max(0.0f, dot(normalInterpolated, lightDir));
                        SRMath::vec3 diffuseTerm = base_color;
                        diffuseTerm *= diffuse_intensity;
						diffuseTerm *= light.color;

                        totalDiffuseColor += diffuseTerm;

                        // 정반사 조명 계산
                        SRMath::vec3 reflect_dir = SRMath::reflect(-1 * lightDir, normalInterpolated);
                        float spec_dot = SRMath::dot(viewDir, reflect_dir);
                        float spec_factor = std::max(0.0f, spec_dot);
                        for (int i = 2; i < material->Ns; i *= 2)
                        	spec_factor *= spec_factor;
                        
						SRMath::vec3 specularTerm = material->ks;
						specularTerm *= spec_factor;
						specularTerm *= light.color;

                        totalSpecularColor += specularTerm;
                    }

                    SRMath::vec3 color = ambient_color;
                    color += totalDiffuseColor;
                    color += totalSpecularColor;

                    // 최종 색상의 각 채널(R, G, B)을 0.0과 1.0 사이로 클램핑합니다.
                    color.clamp(0.f, 1.0f);

                    unsigned int final_color = RGB(
                        color.x * 255.f,
                        color.y * 255.f,
                        color.z * 255.f
                    );

                    // 깊이 갱신 및 픽셀 쓰기
                    m_depthBuffer[idx] = interpolatedOneOverW;
                    drawPixel(x, y, final_color);
                }
            }

            // x가 1 증가했으므로, y의 변화량만큼 더해줍니다. (점진적 계산)
            w0_fixed += dy12;
            w1_fixed += dy20;
            w2_fixed += dy01;
        }

        // y가 1 증가했으므로, 다음 행의 시작 값을 x의 변화량만큼 더해서 갱신합니다.
        w0Row_fixed -= dx12_fixed;
        w1Row_fixed -= dx20_fixed;
        w2Row_fixed -= dx01_fixed;
    }
}

// 윈도우 리사이즈 대응 (리소스 재할당)
void Renderer::OnResize(HWND hWnd)
{
    Shutdown();
    Initialize(hWnd);
}
