#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "pch.h"
#include "SRMath.h"

class Model;
class Texture;

enum class ELineAlgorithm
{
	Bresenham,
	DDA
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
	
	// Model Variables
	std::vector<std::shared_ptr<Model>> m_models;
	std::shared_ptr<Model> m_model;

	// Texture Variables
	std::vector<std::string> texNames;
	std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
	std::shared_ptr<Texture> m_ptexture;

	ELineAlgorithm m_currentLineAlgorithm = 
		ELineAlgorithm::Bresenham;	// �� �׸��� �˰���

	bool m_isNrmDebug = false;

	void drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color);
	void drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color);

public:
	Renderer();
	~Renderer();

	bool Initialize(HWND hWnd);
	void Shutdown() const;

	void DrawPixel(int x, int y, unsigned int color);
	void DrawLine(int x0, int y0, int x1, int y1, unsigned int color);
	void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color);
	void DrawTriangle(const SRMath::vec2 v0, const SRMath::vec2 v1, const SRMath::vec2 v2, unsigned int color);
	void DrawFilledTriangle(const SRMath::vec2& v0, const SRMath::vec2& v1, const SRMath::vec2& v2,
		const SRMath::vec3& n0_World, const SRMath::vec3& n1_World, const SRMath::vec3& n2_World,
		float z0, float z1, float z2, const SRMath::vec2& uv0_clipped, const SRMath::vec2& uv1_clipped,
		const SRMath::vec2& uv2_clipped, const SRMath::vec3& light_dir, const std::shared_ptr<Texture> texture);

	void SetLineAlgorithm(ELineAlgorithm eLineAlgorithm);
	void SetDebugNormal();

	void Clear();
	void Present(HDC hScreenDC) const;
	void Render(SRMath::mat4& projectionMatrix, SRMath::mat4& viewMatrix, SRMath::vec3& light_dir);

	void DebugNormalVector(const SRMath::vec3& v0_World, const SRMath::vec3& v1_World, const SRMath::vec3& v2_World, const SRMath::vec3& n0_World, const SRMath::vec3& n1_World, const SRMath::vec3& n2_World, SRMath::mat4& vp);

	void OnResize(HWND hWnd);

	// Getter
	const int GetWidth() const { return m_width; }
	const int GetHeight() const { return m_height; }
};

