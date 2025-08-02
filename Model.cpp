#include "Model.h"
#include "RenderQueue.h"
#include "Octree.h"

void Model::SubmitToRenderQueue(RenderQueue& renderQueue, const SRMath::mat4& worldTransform)
{
    for (const auto& mesh : m_meshes) {
        MeshRenderCommand cmd;
        cmd.mesh = &mesh;
        cmd.modelMatrix = worldTransform;
        renderQueue.Submit(cmd);

        // 만약 Octree 디버그 뷰를 그리고 싶다면
        if (mesh.octree) {
            mesh.octree->SubmitDebugToRenderQueue(renderQueue, worldTransform);
        }
    }
}
