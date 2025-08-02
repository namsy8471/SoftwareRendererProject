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

        // ���� Octree ����� �並 �׸��� �ʹٸ�
        if (mesh.octree) {
            mesh.octree->SubmitDebugToRenderQueue(renderQueue, worldTransform);
        }
    }
}
