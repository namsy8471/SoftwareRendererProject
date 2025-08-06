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

        // ���� Octree ����� �並 �׸��� �ʹٸ�
        if (mesh.octree) {
            mesh.octree->SubmitDebugToRenderQueue(renderQueue, worldTransform);
        }
    }
}
