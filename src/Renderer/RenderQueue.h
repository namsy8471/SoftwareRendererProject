#pragma once

#include <vector>
#include "Renderer/RenderCommand.h"

class RenderQueue {
private:
	std::vector<MeshRenderCommand> m_renderCommands;
	std::vector<DebugPrimitiveCommand> m_debugPrimitiveCmds;

public:

	void Submit(const MeshRenderCommand& cmd) {
		m_renderCommands.push_back(cmd);
	}

	void Submit(const DebugPrimitiveCommand& cmd) {
		m_debugPrimitiveCmds.push_back(cmd);
	}

	void Clear() {
		m_renderCommands.clear();
		m_debugPrimitiveCmds.clear();
	}

	const std::vector<MeshRenderCommand>& GetRenderCommands() const {
		return m_renderCommands;
	}

	const std::vector<DebugPrimitiveCommand>& GetDebugCommands() const {
		return m_debugPrimitiveCmds;
	}

	void Sort() {
		// 정렬 로직을 여기에 추가할 수 있습니다.
		// 예를 들어, m_renderCommands를 특정 기준으로 정렬할 수 있습니다.
		// 현재는 단순히 삽입 순서대로 유지합니다.
	}
};