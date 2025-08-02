#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "pch.h"
#include "SRMath.h"
#include "Mesh.h"

class Texture;
struct ShadedVertex;
struct RasterizerVertex;
struct Frustum;
class OctreeNode;

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

	// Texture Variables
	std::vector<std::string> texNames;
	std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
	std::shared_ptr<Texture> m_ptexture;

	ELineAlgorithm m_currentLineAlgorithm = 
		ELineAlgorithm::Bresenham;	// 선 그리기 알고리즘

	bool m_isNrmDebug = false;

	void drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color);
	void drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color);

	ShadedVertex interpolate(const ShadedVertex& v0, const ShadedVertex& v1, float t);
	std::vector<ShadedVertex> clipPolygonAgainstPlane(const std::vector<ShadedVertex>& vertices, int plane_axis, int plane_sign);
	std::vector<ShadedVertex> clipTriangle(const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2);
	void resterization(const std::vector<ShadedVertex>& clipped_vertices, std::vector<RasterizerVertex>& final_vertices, const SRMath::vec3& light_dir);

	void extractFrustumPlanes(const SRMath::mat4& vp, Frustum& out_frustum);
	bool isSphereInFrustum(const Frustum& frustum, const SRMath::vec3& sphere_center, float sphere_radius);
	//void renderOctreeNode(const OctreeNode* node, const Frustum& frustum, const Mesh& mesh, const SRMath::mat4& 
	// Matrix,const SRMath::mat4& mv, const SRMath::mat4& vp, const SRMath::mat4& mvp, const SRMath::mat4& normal_matrix, const SRMath::vec3& light_dir);

public:
	Renderer();
	~Renderer();

	bool Initialize(HWND hWnd);
	void Shutdown() const;

	void DrawPixel(int x, int y, unsigned int color);
	void DrawLine(int x0, int y0, int x1, int y1, unsigned int color);
	void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color);
	void DrawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color);

	void DrawMesh(const SRMath::vec2& v0, const SRMath::vec2& v1, const SRMath::vec2& v2,
		float one_over_w0, float one_over_w1, float one_over_w2, const SRMath::vec3& n0_World, const SRMath::vec3& n1_World, const SRMath::vec3& n2_World,
		const SRMath::vec2& uv0_clipped, const SRMath::vec2& uv1_clipped, const SRMath::vec2& uv2_clipped,
		const SRMath::vec3& light_dir, const std::shared_ptr<Texture> texture);

	void SetLineAlgorithm(ELineAlgorithm eLineAlgorithm);
	void SetDebugNormal();

	void Clear();
	void Present(HDC hScreenDC) const;
	void Render(SRMath::mat4& projectionMatrix, SRMath::mat4& viewMatrix, SRMath::vec3& light_dir, const float deltaTime);

	void DebugNormalVector(const SRMath::vec3& v0_World, const SRMath::vec3& v1_World, const SRMath::vec3& v2_World, const SRMath::vec3& n0_World, const SRMath::vec3& n1_World, const SRMath::vec3& n2_World, const SRMath::mat4& vp);

	void OnResize(HWND hWnd);

	// Getter
	const int GetWidth() const { return m_width; }
	const int GetHeight() const { return m_height; }
};

