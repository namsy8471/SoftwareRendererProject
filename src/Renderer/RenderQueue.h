#pragma once

#include <vector>
#include <span>
#include <utility>
#include "Renderer/RenderCommand.h"

class RenderQueue
{
  private:
    std::vector<MeshRenderCommand> m_renderCommands;
    std::vector<DebugPrimitiveCommand> m_debugPrimitiveCmds;
    // 지난 프레임 debug 명령의 정점 vector를 제출 순서대로 보관한다. 법선 선은 프레임마다
    // 약 8MB를 새로 할당/해제해 Update의 malloc과 Clear의 해제가 VTune 상위에 올랐다.
    std::vector<std::vector<DebugVertex>> m_vertexPool;
    std::size_t m_nextPooledVertices = 0;

  public:
    // 값으로 받은 명령은 lvalue 호출에는 복사, 임시 객체에는 이동을 적용한다.
    // 별도 const&/&& 오버로드 없이 동일한 소유권 규칙을 제공한다.
    void Submit(MeshRenderCommand cmd) { m_renderCommands.push_back(std::move(cmd)); }

    void Submit(DebugPrimitiveCommand cmd) { m_debugPrimitiveCmds.push_back(std::move(cmd)); }

    void Clear()
    {
        m_renderCommands.clear();
        // 사용하지 않은 이전 보관분은 해제하고 이번 프레임 정점 저장소만 용량째 보관한다.
        m_vertexPool.clear();
        m_nextPooledVertices = 0;
        for (auto &command : m_debugPrimitiveCmds)
        {
            command.vertices.clear();
            m_vertexPool.push_back(std::move(command.vertices));
        }
        m_debugPrimitiveCmds.clear();
    }

    // 빈 정점 vector를 돌려준다. 같은 순서로 다시 제출하면 이전 프레임과 같은 용량을 받는다.
    // 병렬 작업 안에서 호출하지 않는다.
    [[nodiscard]] std::vector<DebugVertex> AcquireDebugVertices()
    {
        if (m_nextPooledVertices < m_vertexPool.size())
            return std::move(m_vertexPool[m_nextPooledVertices++]);
        return {};
    }

    [[nodiscard]] std::span<const MeshRenderCommand> GetRenderCommands() const noexcept
    {
        return m_renderCommands;
    }

    [[nodiscard]] std::span<const DebugPrimitiveCommand> GetDebugCommands() const noexcept
    {
        return m_debugPrimitiveCmds;
    }
};
