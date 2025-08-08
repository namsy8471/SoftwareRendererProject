#pragma once
#include <vector>
#include "Core/pch.h"
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"

struct ShadedVertex;
struct RasterizerVertex;
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

class Renderer
{
private:
	// 백버퍼링에 필요한 GDI 객체 핸들
	HDC     m_hMemDC;       // 백버퍼의 Device Context
	HBITMAP m_hBitmap;      // 백버퍼용 비트맵
	HBITMAP m_hOldBitmap;   // m_hMemDC에 원래 선택되어 있던 비트맵

	int m_width;
	int m_height;

	unsigned int* m_pPixelData;
	std::vector<float> m_depthBuffer;

	ELineAlgorithm m_currentLineAlgorithm = 
		ELineAlgorithm::Bresenham;	// 선 그리기 알고리즘

	// 선 그리기 알고리즘 셀렉터
	void drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color);
	void drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color);

	// 그리기 함수
	void drawPixel(int x, int y, unsigned int color);
	void drawLine(int x0, int y0, int x1, int y1, unsigned int color);
	void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color);
	void drawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color);

	void drawFilledTriangle(const RasterizerVertex& v0, const RasterizerVertex& v1, const RasterizerVertex& v2,
		const Material* material, const std::vector<DirectionalLight>& lights, const SRMath::vec3& camPos);
	
	void resterization(const std::vector<ShadedVertex>& clipped_vertices,
		const Material* material, const std::vector<DirectionalLight>& lights, const SRMath::vec3 camPos, const MeshRenderCommand& cmd);

	void drawMesh(const MeshRenderCommand& cmd, const SRMath::mat4& viewMatrix, const SRMath::mat4& projectionMatrix, const SRMath::vec3& camPos, const std::vector<DirectionalLight> lights);
	void drawDebugPrimitive(const DebugPrimitiveCommand& cmd, const SRMath::mat4& vp, const Camera& camera);

	ShadedVertex interpolate(const ShadedVertex& v0, const ShadedVertex& v1, float t);
	std::vector<ShadedVertex> clipPolygonAgainstPlane(const std::vector<ShadedVertex>& vertices, int plane_axis, int plane_sign);
	std::vector<ShadedVertex> clipTriangle(const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2);

public:
	Renderer();
	~Renderer();

	bool Initialize(HWND hWnd);
	void Shutdown() const;

	void SetLineAlgorithm(ELineAlgorithm eLineAlgorithm);

	void Clear();
	void Present(HDC hScreenDC) const;
	void RenderScene(const RenderQueue& queue, const Camera& camera, const std::vector<DirectionalLight>& lights);

	void DebugNormalVector(const SRMath::vec3& v0_World, const SRMath::vec3& v1_World, const SRMath::vec3& v2_World, const SRMath::vec3& n0_World, const SRMath::vec3& n1_World, const SRMath::vec3& n2_World, const SRMath::mat4& vp);

	void OnResize(HWND hWnd);

	// Getter
	const int GetWidth() const { return m_width; }
	const int GetHeight() const { return m_height; }
};

