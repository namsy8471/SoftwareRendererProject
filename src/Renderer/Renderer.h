#pragma once
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <tbb/enumerable_thread_specific.h>

#include "Core/pch.h"
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Renderer/Tile.h"
#include "Renderer/ShaderVertices.h"

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

struct alignas(64) AlignedAtomicInt {
	std::atomic<int> value;
};

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
	std::vector<float> m_depthBuffer;

	ELineAlgorithm m_currentLineAlgorithm = 
		ELineAlgorithm::Bresenham;	// �� �׸��� �˰����

	EAAAlgorithm m_currentAAAlgorithm = 
		EAAAlgorithm::None;	// ��Ƽ�ٸ���� �˰����

	// Renderer Optimization		
	std::vector<tbb::concurrent_vector<TriangleRef*>> m_finalTriangleBins;
	tbb::enumerable_thread_specific<tbb::concurrent_vector<TriangleRef>> m_threadTrianglePools; // ���� TriangleRef ��ü���� ����� �����庰 �޸� Ǯ
	tbb::enumerable_thread_specific<std::vector<ShadedVertex>> m_threadClipBuffer1, m_threadClipBuffer2, m_threadClippedVertices;
	tbb::enumerable_thread_specific<std::unordered_map<const MeshRenderCommand*, SRMath::mat4>>m_threadNormalMatrixCache;

	tbb::enumerable_thread_specific<std::vector<ShadedVertex>> m_threadShadedVertexBuffers; // Ŭ�� ���� ��ǥ�� ������ ����
	tbb::enumerable_thread_specific<std::vector<uint64_t>> m_threadStamps;         // ������ ������ (��ȯ ĳ�� �뵵)
	// ������ ī���� �߰� (������ ���̴����ۿ� ������ ������ ���� ����)
	uint64_t m_frameCounter = 0;

	// Resize�� ���ʱ�ȭ �Լ�
	bool reInit(HWND hWnd);
	void shutdownForResize() const;

	// �� �׸��� �˰���� ������
	void drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color);
	void drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color);

	// �׸��� �Լ�
	void drawPixel(int x, int y, unsigned int color);
	void drawLine(int x0, int y0, int x1, int y1, unsigned int color);
	void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color);
	void drawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color);

	void renderTile(int tx, int ty, int numTileX, const tbb::concurrent_vector<TriangleRef*>& triangleBin, const SRMath::vec3& camPos, const std::vector<DirectionalLight>& lights);
	
	void resterizationForTile(const ShadedVertex& sv0, const ShadedVertex& sv1, const ShadedVertex& sv2, const Material* material,
		const std::vector<DirectionalLight>& lights, const SRMath::vec3& camPos, const MeshRenderCommand& cmd, int tile_minX, int tile_minY, int tile_maxX, int tile_maxY);
	void drawFilledTriangleForTile(const RasterizerVertex& v0, const RasterizerVertex& v1, const RasterizerVertex& v2, const Material* material, const std::vector<DirectionalLight>& lights, const SRMath::vec3& camPos, int tile_minX, int tile_minY, int tile_maxX, int tile_maxY);

	void drawDebugPrimitive(const DebugPrimitiveCommand& cmd, const SRMath::mat4& vp, const Camera& camera);

	ShadedVertex interpolate(const ShadedVertex& v0, const ShadedVertex& v1, float t);
	void clipPolygonAgainstPlane(std::vector<ShadedVertex>& out_vertices, const std::vector<ShadedVertex>& vertices, const __m128& plane);
	void clipTriangle(std::vector<ShadedVertex>& out_vertices, const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2,
		std::vector<ShadedVertex>& buffer1, std::vector<ShadedVertex>& buffer2);


public:
	Renderer(HWND hWnd);
	~Renderer();

	void SetLineAlgorithm(ELineAlgorithm eLineAlgorithm) { m_currentLineAlgorithm = eLineAlgorithm; }
	void SetAAAlgorithm(EAAAlgorithm eAAAlgorithm) { m_currentAAAlgorithm = eAAAlgorithm; }

	void Clear();
	void Present(HDC hScreenDC) const;
	void RenderScene(const RenderQueue& queue, const Camera& camera, const std::vector<DirectionalLight>& lights);

	void OnResize(HWND hWnd);

	// Getter
	const int GetWidth() const { return m_width; }
	const int GetHeight() const { return m_height; }
};
