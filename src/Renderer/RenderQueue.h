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
		// ���� ������ ���⿡ �߰��� �� �ֽ��ϴ�.
		// ���� ���, m_renderCommands�� Ư�� �������� ������ �� �ֽ��ϴ�.
		// ����� �ܼ��� ���� ������� �����մϴ�.
	}
};