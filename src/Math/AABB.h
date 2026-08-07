#pragma once
#include "Math/SRMath.h"
#include <array>
#include <limits>

struct Mesh;

struct AABB
{
	SRMath::vec3 min{
		std::numeric_limits<float>::max(),
		std::numeric_limits<float>::max(),
		std::numeric_limits<float>::max() };
	SRMath::vec3 max{
		std::numeric_limits<float>::lowest(),
		std::numeric_limits<float>::lowest(),
		std::numeric_limits<float>::lowest() };

	// 현재 경계를 다른 경계/점까지 확장한다. 상태를 변경하므로 반환값은 없다.
	void Encapsulate(const AABB& other) noexcept;
	void Encapsulate(const SRMath::vec3& point) noexcept;

	[[nodiscard]] bool Intersects(const AABB& other) const noexcept;
	[[nodiscard]] bool Contains(const AABB& other) const noexcept;

	// 값 반환형의 top-level const는 이동을 막을 뿐 호출자 보호에 도움이 없어
	// 제거했다. 이는 오래된 C++ 관용구를 현대적인 값 의미론으로 고친 것이다.
	[[nodiscard]] SRMath::vec3 Center() const noexcept;
	[[nodiscard]] std::array<SRMath::vec3, 8> Corners() const noexcept;
	[[nodiscard]] bool IsValid() const noexcept;

	[[nodiscard]] static AABB CreateFromMesh(const Mesh& mesh);
	[[nodiscard]] AABB Transform(const SRMath::mat4& transform) const noexcept;
};
