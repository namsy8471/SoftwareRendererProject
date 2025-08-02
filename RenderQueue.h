#pragma once

#include <vector>
#include "RenderCommand.h"

class RenderQueue {
private:
	std::vector<MeshRenderCommand> m_renderCommands;
	std::vector<DebugAABBRenderCommand> m_debugAABBCmds;

public:

	void Submit(const MeshRenderCommand& cmd) {
		m_renderCommands.push_back(cmd);
	}

	void Submit(const DebugAABBRenderCommand& cmd) {
		m_debugAABBCmds.push_back(cmd);
	}

	void Clear() {
		m_renderCommands.clear();
		m_debugAABBCmds.clear();
	}

	const std::vector<MeshRenderCommand>& GetRenderCommands() const {
		return m_renderCommands;
	}

	const std::vector<DebugAABBRenderCommand>& GetDebugAABBCmds() const {
		return m_debugAABBCmds;
	}

	void Sort() {
		// ���� ������ ���⿡ �߰��� �� �ֽ��ϴ�.
		// ���� ���, m_renderCommands�� Ư�� �������� ������ �� �ֽ��ϴ�.
		// ����� �ܼ��� ���� ������� �����մϴ�.
	}
};