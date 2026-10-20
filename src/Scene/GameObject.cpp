#include "GameObject.h"
#include "Graphics/Model.h"
#include "Math/Frustum.h"
#include "Graphics/Octree.h"
#include "Utils/DebugUtils.h"

#include <stdexcept>
#include <utility>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

GameObject::GameObject(const SRMath::vec3 &position, const SRMath::vec3 &rotation, const SRMath::vec3 &scale,
                       std::unique_ptr<Model> model)
    : m_position(position), m_rotation(rotation), m_scale(scale), m_model(std::move(model))
{
    if (!m_model)
    {
        throw std::invalid_argument("GameObject requires a model");
    }
    UpdateTransform(0.f, false);
}

GameObject::~GameObject() = default;

void GameObject::Update(float deltaTime, bool isRotate)
{
    UpdateTransform(deltaTime, isRotate);

    for (const auto &child : m_children)
    {
        if (child)
        {
            child->Update(deltaTime, isRotate);
        }
    }
}

void GameObject::UpdateTransform(float deltaTime, bool isRotate)
{
    // 열벡터 규약: local = T * R * S, world = parentWorld * local.
    if (isRotate)
        m_rotation += SRMath::vec3(0.0f, 0.4f * deltaTime, 0.0f); // 초당 0.4 rad 회전

    const SRMath::mat4 scaleMatrix = SRMath::scale(m_scale);
    const SRMath::mat4 rotationMatrix = SRMath::rotate(m_rotation);
    const SRMath::mat4 translationMatrix = SRMath::translate(m_position);
    m_worldMatrix = translationMatrix * rotationMatrix * scaleMatrix;
    if (const auto parent = m_parent.lock())
        m_worldMatrix = parent->m_worldMatrix * m_worldMatrix;

    // scale에 0이 있으면 역행렬이 존재하지 않는다. expected를 확인해 실패
    // 경로를 눈에 보이게 하고, 렌더링을 계속할 때만 단위 행렬을 사용한다.
    if (const auto normalMatrix = SRMath::inverse_transpose(m_worldMatrix))
        m_normalMatrix = *normalMatrix;
    else
        m_normalMatrix = SRMath::mat4::identity();

    const AABB &localAABB = m_model->GetLocalAABB();
    m_worldAABB = localAABB.Transform(m_worldMatrix);
}

bool GameObject::AddChild(std::shared_ptr<GameObject> child)
{
    const auto self = weak_from_this().lock();
    if (!self || !child || !child->m_parent.expired())
        return false;
    for (auto ancestor = self; ancestor; ancestor = ancestor->m_parent.lock())
        if (ancestor == child)
            return false;
    m_children.push_back(child);
    child->m_parent = self;
    child->Update(0.f, false);
    return true;
}

void GameObject::SubmitToRenderQueue(RenderQueue &queue, const Frustum &frustum, const DebugFlags &flags)
{
    if (m_model && frustum.IsAABBInFrustum(m_worldAABB))
        submitOwnMeshes(queue, frustum, flags);

    // 부모 AABB는 자식 전체를 포함하지 않는다. 부모가 보이지 않아도 자식은 검사한다.
    for (const auto &child : m_children)
        child->SubmitToRenderQueue(queue, frustum, flags);
}

void GameObject::submitOwnMeshes(RenderQueue &renderQueue, const Frustum &frustum,
                                 const DebugFlags &debugFlags)
{
    const std::span<const Mesh> meshes = m_model->GetMeshes();

    // resize/clear는 병렬 작업 전에만 수행한다. 서로 다른 메시의 batch를
    // 각 작업이 독점하므로 worker 배치가 달라도 최종 제출 순서는 같다.
    m_meshCommandBatches.resize(meshes.size());
    for (auto &batch : m_meshCommandBatches)
    {
        batch.commands.clear();
        batch.debugCommands.clear();
        if (debugFlags.bShowNormal)
            batch.normalLines = renderQueue.AcquireDebugVertices();
    }

    tbb::parallel_for(tbb::blocked_range<std::size_t>{0, meshes.size()},
                      [&](const tbb::blocked_range<std::size_t> &range)
                      {
                          for (std::size_t i = range.begin(); i != range.end(); ++i)
                          {
                              const Mesh &mesh = meshes[i];
                              auto &batch = m_meshCommandBatches[i];
                              collectMeshCommands(mesh, frustum, debugFlags, batch.commands,
                                                  batch.debugCommands, batch.normalLines);
                          }
                      });
    mergeCommands(renderQueue);
}

void GameObject::collectMeshCommands(const Mesh &mesh, const Frustum &frustum, const DebugFlags &debugFlags,
                                     std::vector<MeshRenderCommand> &localCmd,
                                     std::vector<DebugPrimitiveCommand> &localDebugCmd,
                                     std::vector<DebugVertex> &normalLines)
{
    if (mesh.octree)
    {
        mesh.octree->SubmitNodesToRenderQueue(frustum, m_worldMatrix, localCmd, localDebugCmd, debugFlags);
    }
    else
    {
        localCmd.push_back(MeshRenderCommand{
            .sourceMesh = &mesh,
            .indicesToDraw = mesh.indices,
            .worldTransform = m_worldMatrix,
            .material = &mesh.material,
            .rasterizeMode = debugFlags.bShowWireframe ? ERasterizeMode::Wireframe : ERasterizeMode::Fill});
    }

    if (debugFlags.bShowNormal)
    {
        normalLines.clear();
        normalLines.reserve(mesh.vertices.size() * 2); // 각 정점마다 시작점과 끝점이 있으므로 2배 크기

        constexpr float normalLength = 0.1f;

        for (const auto &vertex : mesh.vertices)
        {
            const SRMath::vec3 startWorld{m_worldMatrix * vertex.position};

            // 비균등 스케일에서도 표면에 수직이 되도록 법선은 역전치 행렬로 변환한다.
            // (w=0으로 설정하여 방향 벡터임을 명시)
            const SRMath::vec3 normalWorld{m_normalMatrix * SRMath::vec4(vertex.normal, 0.f)};
            const SRMath::vec3 endWorld = startWorld + SRMath::normalize(normalWorld) * normalLength;

            normalLines.push_back({startWorld, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f)});
            normalLines.push_back({endWorld, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f)});
        }

        if (!normalLines.empty())
        {

            localDebugCmd.push_back(DebugPrimitiveCommand{.vertices = std::move(normalLines),
                                                          .worldTransform = SRMath::mat4::identity(),
                                                          .type = DebugPrimitiveType::Line,
                                                          .depthPolicy = DebugDepthPolicy::DepthTested});
        }
    }
}

void GameObject::mergeCommands(RenderQueue &renderQueue)
{

    for (const auto &batch : m_meshCommandBatches)
    {
        for (const auto &cmd : batch.commands)
        {
            renderQueue.Submit(cmd);
        }
    }

    for (auto &batch : m_meshCommandBatches)
    {
        for (auto &cmd : batch.debugCommands)
        {
            renderQueue.Submit(std::move(cmd));
        }
    }
}
