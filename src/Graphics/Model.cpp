#include "Model.h"
#include "Renderer/RenderQueue.h"
#include "Graphics/Octree.h"

void Model::SubmitToRenderQueue(RenderQueue& renderQueue, const SRMath::mat4& worldTransform)
{
    for (const auto& mesh : m_meshes) {
        MeshRenderCommand cmd;
        cmd.sourceMesh = &mesh;
		cmd.indicesToDraw = &mesh.indices;
        cmd.worldTransform = worldTransform;

        renderQueue.Submit(cmd);

        // 만약 Octree 디버그 뷰를 그리고 싶다면
        if (mesh.octree) {
            mesh.octree->SubmitDebugToRenderQueue(renderQueue, worldTransform);
        }
    }
}
