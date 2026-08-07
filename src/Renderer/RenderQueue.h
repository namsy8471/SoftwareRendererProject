#pragma once

#include <vector>
#include <span>
#include <utility>
#include "Renderer/RenderCommand.h"

class RenderQueue {
private:
	std::vector<MeshRenderCommand> m_renderCommands;
	std::vector<DebugPrimitiveCommand> m_debugPrimitiveCmds;

public:

	// 값으로 받은 명령은 lvalue 호출에는 복사, 임시 객체에는 이동을 적용한다.
	// 별도 const&/&& 오버로드 없이 동일한 소유권 규칙을 제공한다.
	void Submit(MeshRenderCommand cmd) {
		m_renderCommands.push_back(std::move(cmd));
	}

	void Submit(DebugPrimitiveCommand cmd) {
		m_debugPrimitiveCmds.push_back(std::move(cmd));
	}

	void Clear() {
		m_renderCommands.clear();
		m_debugPrimitiveCmds.clear();
	}

	[[nodiscard]] std::span<const MeshRenderCommand> GetRenderCommands() const noexcept {
		return m_renderCommands;
	}

	[[nodiscard]] std::span<const DebugPrimitiveCommand> GetDebugCommands() const noexcept {
		return m_debugPrimitiveCmds;
	}

};
