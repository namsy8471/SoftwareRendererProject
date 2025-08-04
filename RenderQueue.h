#pragma once

#include <vector>
#include "RenderCommand.h"

class RenderQueue {
private:
	std::vector<MeshRenderCommand> m_renderCommands;
	std::vector<MeshPartRenderCommand> m_meshPartCommands;
	std::vector<DebugAABBRenderCommand> m_debugAABBCmds;

public:

	void Submit(const MeshRenderCommand& cmd) {
		m_renderCommands.push_back(cmd);
	}

	void Submit(const DebugAABBRenderCommand& cmd) {
		m_debugAABBCmds.push_back(cmd);
	}

	void Submit(const MeshPartRenderCommand& command) {
		m_meshPartCommands.push_back(command);
	}

	void Clear() {
		m_renderCommands.clear();
		m_debugAABBCmds.clear();
		m_meshPartCommands.clear();
	}

	const std::vector<MeshRenderCommand>& GetRenderCommands() const {
		return m_renderCommands;
	}

	const std::vector<DebugAABBRenderCommand>& GetDebugAABBCmds() const {
		return m_debugAABBCmds;
	}

	const std::vector<MeshPartRenderCommand>& GetMeshPartCommands() const {
		return m_meshPartCommands;
	}

	void Sort() {
		// 정렬 로직을 여기에 추가할 수 있습니다.
		// 예를 들어, m_renderCommands를 특정 기준으로 정렬할 수 있습니다.
		// 현재는 단순히 삽입 순서대로 유지합니다.
	}
};