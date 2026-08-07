#pragma once
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <span>
#include <tbb/enumerable_thread_specific.h>

#include "Platform/Win32Headers.h"
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Renderer/Tile.h"
#include "Renderer/ShaderVertices.h"
#include "Utils/FixedCapacityVector.h"

class Frustum;
class RenderQueue;
class Camera;
struct DirectionalLight;
struct DebugPrimitiveCommand;
struct MeshRenderCommand;
struct Material;

enum class ELineAlgorithm
{
	Bresenham,
	DDA
};

enum class EAAAlgorithm
{
	None,
	FXAA	// Fast Approxiate Anti-Aliasing
};

class Renderer
{
private:
	// 삼각형을 6개 평면으로 자를 때 최대 정점 수는 작고 고정적이다.
	// C++26 inplace_vector(또는 동일 API fallback)로 프레임별 힙 할당을 없앤다.
	using ClipBuffer = sr::FixedCapacityVector<ShadedVertex, 12>;

	// GDI는 unique_ptr 하나로는 처리할 수 없다. 비트맵을 삭제하기 전에 DC에
	// 선택돼 있던 원래 객체를 복원해야 하므로 세 핸들을 하나의 RAII 타입이
	// 생성/복원/해제 순서까지 소유한다.
	class GdiBackBuffer
	{
	private:
		HDC m_memoryDc = nullptr;
		HBITMAP m_bitmap = nullptr;
		HGDIOBJ m_previousBitmap = nullptr;

	public:
		GdiBackBuffer() = default;
		~GdiBackBuffer() { reset(); }
		GdiBackBuffer(const GdiBackBuffer&) = delete;
		GdiBackBuffer& operator=(const GdiBackBuffer&) = delete;
		GdiBackBuffer(GdiBackBuffer&& other) noexcept;
		GdiBackBuffer& operator=(GdiBackBuffer&& other) noexcept;

		[[nodiscard]] bool create(HWND window, int width, int height, unsigned int*& pixels) noexcept;
		void reset() noexcept;
		[[nodiscard]] HDC get() const noexcept { return m_memoryDc; }
	};

	GdiBackBuffer m_backBuffer;

	int m_width = 0;
	int m_height = 0;

	unsigned int* m_pPixelData = nullptr;
	std::vector<float> m_depthBuffer;

	ELineAlgorithm m_currentLineAlgorithm = ELineAlgorithm::Bresenham;

	EAAAlgorithm m_currentAAAlgorithm = EAAAlgorithm::None;

	// Renderer Optimization
	std::vector<tbb::concurrent_vector<TriangleRef*>> m_finalTriangleBins;
	tbb::enumerable_thread_specific<tbb::concurrent_vector<TriangleRef>> m_threadTrianglePools; // 실제 TriangleRef 객체들이 저장될 스레드별 메모리 풀
	tbb::enumerable_thread_specific<ClipBuffer> m_threadClipBuffer1, m_threadClipBuffer2, m_threadClippedVertices;
	tbb::enumerable_thread_specific<std::unordered_map<const MeshRenderCommand*, SRMath::mat4>>m_threadNormalMatrixCache;

	tbb::enumerable_thread_specific<std::vector<ShadedVertex>> m_threadShadedVertexBuffers; // 클립 공간 좌표를 저장할 버퍼
	tbb::enumerable_thread_specific<std::vector<std::uint64_t>> m_threadStamps;
	// 프레임 카운터 추가 (스레드 셰이더버퍼와 스탬프 데이터 오염 방지)
	std::uint64_t m_frameCounter = 0;

	// Resize용 재초기화 함수
	bool reInit(HWND hWnd);
	void shutdownForResize() noexcept;

	// 선 그리기 알고리즘 셀렉터
	void drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color);
	void drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color);

	// 그리기 함수
	void drawPixel(int x, int y, unsigned int color);
	void drawLine(int x0, int y0, int x1, int y1, unsigned int color);
	void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color);
	void drawTriangle(const SRMath::vec2& v0, const SRMath::vec2& v1, const SRMath::vec2& v2, unsigned int color);

	void renderTile(int tx, int ty, const tbb::concurrent_vector<TriangleRef*>& triangleBin,
		const SRMath::vec3& camPos, std::span<const DirectionalLight> lights);

	void resterizationForTile(const ShadedVertex& sv0, const ShadedVertex& sv1, const ShadedVertex& sv2, const Material* material,
		std::span<const DirectionalLight> lights, const SRMath::vec3& camPos, const MeshRenderCommand& cmd, int tile_minX, int tile_minY, int tile_maxX, int tile_maxY);
	void drawFilledTriangleForTile(const RasterizerVertex& v0, const RasterizerVertex& v1, const RasterizerVertex& v2, const Material* material, std::span<const DirectionalLight> lights, const SRMath::vec3& camPos, int tile_minX, int tile_minY, int tile_maxX, int tile_maxY);

	void drawDebugPrimitive(const DebugPrimitiveCommand& cmd, const SRMath::mat4& vp);

	ShadedVertex interpolate(const ShadedVertex& v0, const ShadedVertex& v1, float t);
	void clipPolygonAgainstPlane(ClipBuffer& out_vertices, const ClipBuffer& vertices, const __m128& plane);
	void clipTriangle(ClipBuffer& out_vertices, const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2,
		ClipBuffer& buffer1, ClipBuffer& buffer2);


public:
	Renderer(HWND hWnd);
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;
	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	void SetLineAlgorithm(ELineAlgorithm eLineAlgorithm) { m_currentLineAlgorithm = eLineAlgorithm; }
	void SetAAAlgorithm(EAAAlgorithm eAAAlgorithm) { m_currentAAAlgorithm = eAAAlgorithm; }

	void Clear();
	void Present(HDC hScreenDC) const;
	void RenderScene(const RenderQueue& queue, const Camera& camera, std::span<const DirectionalLight> lights);

	void OnResize(HWND hWnd);

	// Getter
	[[nodiscard]] int GetWidth() const noexcept { return m_width; }
	[[nodiscard]] int GetHeight() const noexcept { return m_height; }
};
