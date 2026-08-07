#pragma once
#include <expected>
#include <span>
#include <vector>
#include "Graphics/ModelLoader.h"
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Math/AABB.h"

class RenderQueue; // 전방 선언

class Model
{
	// Loader만 완성된 메시 묶음과 그에 대응하는 모델 AABB를 한 번에 채운다.
	// 공개 쓰기 API를 만들지 않아 로드 이후 Model의 불변식을 보존한다.
	friend std::expected<std::unique_ptr<Model>, AssetLoadError> ModelLoader::LoadOBJ(const std::filesystem::path& filepath);

private:
	std::vector<Mesh> m_meshes;
	AABB m_localAABB;

public:
	[[nodiscard]] std::span<const Mesh> GetMeshes() const noexcept { return std::span<const Mesh>{ m_meshes }; }
	[[nodiscard]] const AABB& GetLocalAABB() const noexcept { return m_localAABB; }
};
