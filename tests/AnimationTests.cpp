// 5단계 애니메이션: 쿼터니언, 키프레임 샘플링, 스키닝(알려진 포즈, 스칼라 = AVX), Animator 재생, 장면 연결과 컬링.
#include "Animation/AnimationSampling.h"
#include "Animation/Animator.h"
#include "Animation/Skinning.h"
#include "Core/FrameRunner.h"
#include "Graphics/GltfLoader.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Math/Quaternion.h"
#include "Math/SIMD.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"

#include <cmath>
#include <cstring>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;

namespace
{
constexpr float pi = std::numbers::pi_v<float>;

void vec3Near(const SRMath::vec3 &actual, const SRMath::vec3 &expected, std::string_view what,
              float tolerance = 1e-4f)
{
    for (std::size_t i = 0; i < 3; ++i)
        check(approximatelyEqual(actual[i], expected[i], tolerance),
              std::string(what) + ": got (" + std::to_string(actual.x) + ", " + std::to_string(actual.y) + ", " +
                  std::to_string(actual.z) + ")");
}

void matrixNear(const SRMath::mat4 &a, const SRMath::mat4 &b, std::string_view what)
{
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            check(approximatelyEqual(a[c][r], b[c][r]), what);
}

// 같은 회전이면 q와 -q 어느 쪽이어도 된다.
void sameRotation(const SRMath::quat &a, const SRMath::quat &b, std::string_view what)
{
    check(std::abs(std::abs(SRMath::dot(a, b)) - 1.f) < 1e-5f, what);
}

void quaternionMath()
{
    const SRMath::vec3 axes[] = {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {0.3f, -0.5f, 0.8f}};
    for (const auto &axis : axes)
        for (const float angle : {0.f, 0.4f, pi / 2.f, 2.5f})
        {
            const SRMath::quat q = SRMath::from_axis_angle(axis, angle);
            const SRMath::vec3 rotationVector = SRMath::normalize(axis) * angle;
            matrixNear(SRMath::to_mat4(q), SRMath::rotate(rotationVector), "to_mat4 must match rotate()");
            const SRMath::vec3 v(0.2f, -1.f, 3.f);
            vec3Near(SRMath::rotate(q, v), SRMath::vec3(SRMath::rotate(rotationVector) * SRMath::vec4(v, 0.f)),
                     "rotate(q, v)");
            vec3Near(SRMath::to_rotation_vector(q), rotationVector, "rotation vector round trip");
        }

    // a * b = b를 먼저, a를 나중에(행렬 곱과 같은 순서)
    const SRMath::quat a = SRMath::from_axis_angle({0.f, 0.f, 1.f}, 0.7f);
    const SRMath::quat b = SRMath::from_axis_angle({1.f, 1.f, 0.f}, -1.2f);
    matrixNear(SRMath::to_mat4(a * b), SRMath::mat4(SRMath::to_mat4(a) * SRMath::to_mat4(b)), "product order");
    sameRotation(a * SRMath::conjugate(a), SRMath::quat::identity(), "q * conj(q)");

    const SRMath::quat zero{0.f, 0.f, 0.f, 0.f};
    sameRotation(SRMath::normalize(zero), SRMath::quat::identity(), "zero quaternion normalizes to identity");
    const SRMath::quat nan{std::nanf(""), 0.f, 0.f, 1.f};
    sameRotation(SRMath::normalize(nan), SRMath::quat::identity(), "NaN quaternion normalizes to identity");
}

void quaternionInterpolation()
{
    const SRMath::vec3 z(0.f, 0.f, 1.f);
    const SRMath::quat q0 = SRMath::quat::identity();
    const SRMath::quat q90 = SRMath::from_axis_angle(z, pi / 2.f);
    sameRotation(SRMath::slerp(q0, q90, 0.f), q0, "slerp t=0");
    sameRotation(SRMath::slerp(q0, q90, 1.f), q90, "slerp t=1");
    sameRotation(SRMath::slerp(q0, q90, 0.5f), SRMath::from_axis_angle(z, pi / 4.f), "slerp midpoint");
    sameRotation(SRMath::slerp(q0, q90, 0.25f), SRMath::from_axis_angle(z, pi / 8.f), "slerp constant speed");
    // -q90은 같은 회전이다. 긴 호(270도)로 돌지 않아야 한다.
    const SRMath::quat negated{-q90.x, -q90.y, -q90.z, -q90.w};
    sameRotation(SRMath::slerp(q0, negated, 0.5f), SRMath::from_axis_angle(z, pi / 4.f), "slerp shortest arc");
    sameRotation(SRMath::nlerp(q0, negated, 0.5f), SRMath::from_axis_angle(z, pi / 4.f), "nlerp shortest arc");
    // 거의 같은 두 회전(nlerp로 넘어가는 경로)도 단위 길이다.
    const SRMath::quat tiny = SRMath::from_axis_angle(z, 1e-4f);
    check(approximatelyEqual(SRMath::dot(SRMath::slerp(q0, tiny, 0.5f), SRMath::slerp(q0, tiny, 0.5f)), 1.f),
          "slerp near-identical stays unit length");
}

sr::gltf::AnimationTrack makeTrack(sr::gltf::AnimationPath path, sr::gltf::Interpolation interpolation,
                                   int components, std::vector<float> times, std::vector<float> values)
{
    sr::gltf::AnimationTrack track;
    track.node = 0;
    track.path = path;
    track.interpolation = interpolation;
    track.componentCount = components;
    track.times = std::move(times);
    track.values = std::move(values);
    return track;
}

void keyframeSampling()
{
    using sr::anim::SampleTrack;
    using Path = sr::gltf::AnimationPath;
    using Interp = sr::gltf::Interpolation;

    const auto linear = makeTrack(Path::Translation, Interp::Linear, 3, {1.f, 2.f, 4.f},
                                  {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 20.f, 0.f});
    check(SampleTrack(linear, 0.f)[0] == 0.f, "before first key clamps to the first value");
    check(approximatelyEqual(SampleTrack(linear, 1.5f)[0], 5.f), "linear between keys 0 and 1");
    const auto between = SampleTrack(linear, 3.f);
    check(approximatelyEqual(between[0], 10.f) && approximatelyEqual(between[1], 10.f), "linear between 1 and 2");
    check(SampleTrack(linear, 9.f)[1] == 20.f, "after last key clamps to the last value");
    check(SampleTrack(linear, 2.f)[0] == 10.f, "exactly on a key");

    const auto step = makeTrack(Path::Scale, Interp::Step, 3, {0.f, 1.f}, {1.f, 1.f, 1.f, 3.f, 3.f, 3.f});
    check(SampleTrack(step, 0.99f)[2] == 1.f, "step holds the previous key");
    check(SampleTrack(step, 1.f)[2] == 3.f, "step switches on the key");

    // 키 간격 2초, v0 = 0, v1 = 1, out-tangent b0 = 1, in-tangent a1 = 0 (단위/초)
    // u = 0.5: h00 = 0.5, h10 = 0.125 * 2, h01 = 0.5, h11 = -0.125 * 2 → 0.5 + 0.25 = 0.75
    const auto cubic = makeTrack(Path::Translation, Interp::CubicSpline, 1, {0.f, 2.f},
                                 {9.f, 0.f, 1.f, 0.f, 1.f, 9.f});
    check(approximatelyEqual(SampleTrack(cubic, 1.f)[0], 0.75f), "cubic spline uses tangents scaled by key span");
    check(approximatelyEqual(SampleTrack(cubic, 0.f)[0], 0.f) && approximatelyEqual(SampleTrack(cubic, 2.f)[0], 1.f),
          "cubic spline passes through key values, not tangents");

    const SRMath::quat q0 = SRMath::quat::identity();
    const SRMath::quat q180 = SRMath::from_axis_angle({0.f, 1.f, 0.f}, pi * 0.999f);
    const auto rotation = makeTrack(Path::Rotation, Interp::Linear, 4, {0.f, 1.f},
                                    {q0.x, q0.y, q0.z, q0.w, q180.x, q180.y, q180.z, q180.w});
    const auto half = SampleTrack(rotation, 0.5f);
    sameRotation({half[0], half[1], half[2], half[3]}, SRMath::from_axis_angle({0.f, 1.f, 0.f}, pi * 0.4995f),
                 "rotation tracks use slerp, not component lerp");

    // 비정규 키는 정규화하고, 깨진 트랙은 0을 돌려준다.
    const auto unnormalized = makeTrack(Path::Rotation, Interp::Step, 4, {0.f}, {0.f, 0.f, 0.f, 2.f});
    check(SampleTrack(unnormalized, 0.f)[3] == 1.f, "rotation keys are normalized");
    const auto broken = makeTrack(Path::Translation, Interp::Linear, 3, {0.f, 1.f}, {1.f, 2.f});
    check(SampleTrack(broken, 0.5f) == std::array<float, 4>{}, "too few values returns zero");
}

// 정점 3개: 아래(Base만), 관절 위치(반반), 위(Bend만).
std::unique_ptr<Model> threeVertexModel()
{
    Mesh mesh;
    mesh.vertices = {Vertex{{0.f, 0.5f, 0.f}, {0.f, 0.f}, {1.f, 0.f, 0.f}},
                     Vertex{{0.f, 1.f, 0.f}, {0.5f, 0.f}, {1.f, 0.f, 0.f}},
                     Vertex{{0.f, 2.f, 0.f}, {1.f, 1.f}, {1.f, 0.f, 0.f}}};
    mesh.indices = {0, 1, 2};
    std::vector<Mesh> meshes;
    meshes.push_back(std::move(mesh));
    return ModelLoader::FromMeshes(std::move(meshes));
}

void skinningKnownPose()
{
    const SRMath::vec3 zero(0.f, 0.f, 0.f), one(1.f, 1.f, 1.f);
    auto root = std::make_shared<GameObject>(zero, zero, one, nullptr);
    auto base = std::make_shared<GameObject>(zero, zero, one, nullptr);
    auto bend = std::make_shared<GameObject>(SRMath::vec3(0.f, 1.f, 0.f), zero, one, nullptr);
    auto meshNode = std::make_shared<GameObject>(SRMath::vec3(5.f, 5.f, 5.f), zero, one, threeVertexModel());
    check(root->AddChild(base) && base->AddChild(bend) && root->AddChild(meshNode), "hierarchy");

    sr::gltf::VertexSkinWeights weights;
    weights.joints = {{0, 0, 0, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}};
    weights.weights = {{1.f, 0.f, 0.f, 0.f}, {0.5f, 0.5f, 0.f, 0.f}, {2.f, 0.f, 0.f, 0.f}}; // 합 2는 1로 맞춘다
    auto skinned = sr::anim::SkinnedMesh::Create(
        *meshNode->GetModel(), std::span(&weights, 1), {base, bend},
        {SRMath::mat4::identity(), SRMath::translate(SRMath::vec3(0.f, -1.f, 0.f))});
    check(skinned.has_value(), skinned ? "" : skinned.error());

    // 바인드 포즈: 관절 world × inverse bind = 단위 행렬이라 정점이 그대로다(메시 node의 (5,5,5)는 무시한다).
    (*skinned)->Update(meshNode->GetWorldMatrix());
    const auto &bind = (*skinned)->GetPrimitives()[0].deformed.vertices;
    vec3Near(bind[2].position, {0.f, 2.f, 0.f}, "bind pose keeps vertices");

    // Bend를 z축으로 90도: 위 정점 (0,2,0) → (0,1,0) + R(0,1,0) = (-1,1,0), 법선 (1,0,0) → (0,1,0).
    bend->SetOrientation(SRMath::from_axis_angle({0.f, 0.f, 1.f}, pi / 2.f));
    root->Update(0.f, false);
    (*skinned)->Update(meshNode->GetWorldMatrix());
    const auto &posed = (*skinned)->GetPrimitives()[0].deformed.vertices;
    vec3Near(posed[0].position, {0.f, 0.5f, 0.f}, "base-only vertex does not move");
    vec3Near(posed[1].position, {0.f, 1.f, 0.f}, "vertex on the joint stays on the joint");
    vec3Near(posed[2].position, {-1.f, 1.f, 0.f}, "bend-only vertex follows the joint");
    vec3Near(posed[2].normal, {0.f, 1.f, 0.f}, "normal follows the joint");
    vec3Near(posed[1].normal, {0.5f, 0.5f, 0.f}, "blended normal (renderer normalizes)");
    check(posed[2].texcoord.x == 1.f && posed[2].texcoord.y == 1.f, "texcoords are kept");
    check(posed[2].position.data[3] == 0.f, "vec3 padding lane stays zero");

    const AABB &bounds = (*skinned)->GetWorldAABB();
    vec3Near(bounds.min, {-1.f, 0.5f, 0.f}, "bounds min from deformed vertices");
    vec3Near(bounds.max, {0.f, 1.f, 0.f}, "bounds max from deformed vertices");

    // 범위 밖 관절은 거부한다.
    sr::gltf::VertexSkinWeights bad = weights;
    bad.joints[0] = {7, 0, 0, 0};
    check(!sr::anim::SkinnedMesh::Create(*meshNode->GetModel(), std::span(&bad, 1), {base, bend},
                                         {SRMath::mat4::identity(), SRMath::mat4::identity()}),
          "out-of-range joint must fail");
}

void skinningAvxMatchesScalar()
{
    std::mt19937 random(1234);
    std::uniform_real_distribution<float> value(-2.f, 2.f);
    std::uniform_int_distribution<int> joint(0, 7);
    std::vector<SRMath::mat4> joints(8);
    for (auto &matrix : joints)
        matrix = SRMath::translate(SRMath::vec3(value(random), value(random), value(random))) *
                 SRMath::rotate(SRMath::vec3(value(random), value(random), value(random))) *
                 SRMath::scale(SRMath::vec3(1.f + 0.1f * value(random), 1.f, 1.f));
    constexpr std::size_t count = 5003; // 병렬 조각 경계(2048)와 맞지 않는 수
    std::vector<Vertex> in(count);
    std::vector<sr::anim::SkinInfluence> influences(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        in[i].position = {value(random), value(random), value(random)};
        in[i].normal = SRMath::normalize(SRMath::vec3(value(random), value(random), value(random)));
        float sum = 0.f;
        for (std::size_t k = 0; k < 4; ++k)
        {
            influences[i].joints[k] = static_cast<std::uint16_t>(joint(random));
            influences[i].weights[k] = k == 3 ? 0.f : std::abs(value(random));
            sum += influences[i].weights[k];
        }
        for (float &w : influences[i].weights)
            w /= sum;
    }

    std::vector<Vertex> scalar(in), dispatched(in);
    AABB scalarBounds, dispatchedBounds;
    sr::anim::detail::SkinVerticesScalar(in, influences, joints, scalar, scalarBounds);
    sr::anim::SkinVertices(in, influences, joints, dispatched, dispatchedBounds); // AVX(있으면) + 병렬
    const auto sameVertex = [](const Vertex &a, const Vertex &b)
    {
        return std::memcmp(&a.position, &b.position, sizeof(a.position)) == 0 &&
               std::memcmp(&a.normal, &b.normal, sizeof(a.normal)) == 0;
    };
    for (std::size_t i = 0; i < count; ++i)
        check(sameVertex(scalar[i], dispatched[i]), "dispatched skinning differs from scalar at " + std::to_string(i));
    // 성분별로 비트까지 같아야 한다.
    for (std::size_t i = 0; i < 3; ++i)
        check(scalarBounds.min[i] == dispatchedBounds.min[i] && scalarBounds.max[i] == dispatchedBounds.max[i],
              "bounds differ");

    if (SRMath::SIMD::avx_available())
    {
        std::vector<Vertex> avx(in);
        AABB avxBounds;
        sr::anim::detail::SkinVerticesAVX(in, influences, joints, avx, avxBounds);
        for (std::size_t i = 0; i < count; ++i)
            check(sameVertex(scalar[i], avx[i]), "AVX skinning differs from scalar at " + std::to_string(i));
    }

    // 손으로 계산한 섞인 행렬과도 맞는다.
    const std::size_t probe = 17;
    SRMath::mat4 blended(0.f);
    for (std::size_t k = 0; k < 4; ++k)
        for (std::size_t c = 0; c < 4; ++c)
            blended[c] += joints[influences[probe].joints[k]][c] * influences[probe].weights[k];
    vec3Near(scalar[probe].position, SRMath::vec3(blended * SRMath::vec4(in[probe].position, 1.f)),
             "linear blend skinning formula", 1e-4f);
}

sr::gltf::LoadedScene loadTube(const TestContext &context)
{
    auto scene = sr::gltf::LoadScene(context.root / "tests/fixtures/gltf/skinned-tube.glb");
    check(scene.has_value(), scene ? "" : "skinned-tube.glb: " + scene.error().message);
    return std::move(*scene);
}

void animatorPlayback(const TestContext &context)
{
    const auto scene = loadTube(context);
    auto created = sr::anim::Animator::Create(scene);
    check(created.has_value(), created ? "" : created.error());
    auto &animator = **created;
    check(animator.GetClipCount() == 2 && animator.GetClipName(0) == "Bend" && animator.GetClipName(1) == "Twist",
          "clips");
    check(animator.GetClip() == 0 && animator.IsPlaying() && animator.IsLooping(), "defaults");
    check(animator.GetSkinnedObjects().size() == 1 &&
              animator.GetSkinnedObjects()[0]->GetSkinnedMesh()->GetVertexCount() == 13 * 16,
          "one skinned tube");
    check(approximatelyEqual(animator.GetDuration(), 2.f), "Bend lasts 2 s");
    // 0초 포즈로 바로 스킨까지 갱신되어 있다(곧은 원통 y 0..2).
    const AABB initial = animator.GetBounds();
    check(approximatelyEqual(initial.min.y, 0.f) && approximatelyEqual(initial.max.y, 2.f), "initial bounds");

    GameObject &base = *scene.nodes[1].object;
    GameObject &bend = *scene.nodes[2].object;
    animator.ApplyPose(0.5f);
    check(approximatelyEqual(animator.GetTime(), 0.5f), "time advances");
    sameRotation(bend.GetOrientation(), SRMath::from_axis_angle({0.f, 0.f, 1.f}, pi / 4.f), "Bend at 0.5 s");
    vec3Near(base.GetPosition(), {0.f, 0.f, 0.f}, "STEP translation before its key");

    animator.ApplyPose(2.f); // 2.5초 → 반복해서 0.5초
    check(approximatelyEqual(animator.GetTime(), 0.5f), "looping wraps time");
    animator.SetTime(1.25f);
    animator.ApplyPose(0.f);
    vec3Near(base.GetPosition(), {0.25f, 0.f, 0.f}, "STEP translation after its key");

    animator.SetPlaying(false);
    animator.ApplyPose(0.3f);
    check(approximatelyEqual(animator.GetTime(), 1.25f), "paused animator keeps its time");

    animator.SetPlaying(true);
    animator.SetLooping(false);
    animator.ApplyPose(5.f);
    check(approximatelyEqual(animator.GetTime(), 2.f) && !animator.IsPlaying(), "non-looping stops at the end");
    animator.SetPlaying(true);
    animator.ApplyPose(0.25f);
    check(approximatelyEqual(animator.GetTime(), 0.25f), "replaying a finished clip restarts it");

    // 클립을 바꾸면 이전 클립이 움직인 node(Bend, Base 이동)가 로드 시 포즈로 돌아간다.
    animator.SetTime(1.5f);
    animator.ApplyPose(0.f);
    animator.SetClip(1);
    check(animator.GetClip() == 1 && animator.GetTime() == 0.f, "clip switch resets time");
    animator.ApplyPose(0.f);
    sameRotation(bend.GetOrientation(), SRMath::quat::identity(), "Bend restored to rest pose");
    vec3Near(base.GetPosition(), {0.f, 0.f, 0.f}, "Base translation restored");
    animator.SetSpeed(2.f);
    animator.SetLooping(true);
    animator.ApplyPose(0.375f); // 0.75초 = Twist 중간, 접선 0이면 Hermite 중간값 = 키 평균
    check(approximatelyEqual(animator.GetTime(), 0.75f), "speed scales time");
    sameRotation(base.GetOrientation(), SRMath::from_axis_angle({0.f, 1.f, 0.f}, pi / 4.f),
                 "CUBICSPLINE rotation midpoint");

    animator.SetClip(9); // 범위 밖은 무시
    check(animator.GetClip() == 1, "invalid clip index is ignored");
}

void loaderWiggleClip(const TestContext &context)
{
    // 4단계 표본의 Wiggle: node3 이동 LINEAR, node1 회전 STEP, node0 크기 CUBICSPLINE(접선 0).
    auto scene = sr::gltf::LoadScene(context.root / "tests/fixtures/gltf/hierarchy.gltf");
    check(scene.has_value(), "hierarchy.gltf");
    auto created = sr::anim::Animator::Create(*scene);
    check(created.has_value(), created ? "" : created.error());
    (*created)->ApplyPose(1.f);
    vec3Near(scene->nodes[3].object->GetPosition(), {0.f, 2.f, 0.f}, "LINEAR translation at its last key");
    sameRotation(scene->nodes[1].object->GetOrientation(), SRMath::from_axis_angle({0.f, 1.f, 0.f}, pi),
                 "STEP rotation after 0.5 s");
    vec3Near(scene->nodes[0].object->GetScale(), {1.5f, 1.5f, 1.5f}, "CUBICSPLINE scale midpoint");
    check((*created)->GetSkinnedObjects().size() == 1, "Spinner is skinned");
}

void sceneSkinnedRendering(const TestContext &context)
{
    const auto loaded = loadTube(context);
    auto animator = sr::anim::Animator::Create(loaded);
    check(animator.has_value(), "animator");
    const auto *skinned = loaded.nodes[0].object->GetSkinnedMesh();
    if (skinned == nullptr) // check() 안의 throw는 /analyze(C6011)가 따라가지 못한다.
        throw std::runtime_error("Tube node owns the skinned mesh");

    Scene scene;
    check(scene.AddObject(loaded.root), "add root");
    scene.AddAnimator(*animator);
    scene.AddLight(DirectionalLight());
    scene.SetRotateMode(false);
    scene.SetCamera(Camera(SRMath::vec3(0.f, 1.f, -5.f)));
    FrameRunner runner;
    runner.SetScene(std::move(scene));

    runner.Update(0.5f, 160, 120);
    check(approximatelyEqual((*animator)->GetTime(), 0.5f), "Scene::Update advances animators");
    // 포즈 → transform → 스킨 순서라 이번 프레임의 45도 포즈가 이미 변형 정점에 반영되어 있다.
    check(skinned->GetWorldAABB().min.x < -0.5f, "deformed bounds follow this frame's pose");
    const auto commands = runner.GetRenderQueue().GetRenderCommands();
    check(commands.size() == 1, "one command per skinned primitive (no octree split)");
    const auto &primitive = skinned->GetPrimitives()[0];
    check(commands[0].sourceMesh == &primitive.deformed, "command reads the deformed vertices");
    check(commands[0].indicesToDraw.size() == primitive.source->indices.size(), "whole index buffer");
    check(commands[0].material == &primitive.source->material, "source material");
    matrixNear(commands[0].worldTransform, SRMath::mat4::identity(), "world transform is identity");

    MemorySurface surface;
    runner.Render(surface.target);
    std::size_t lit = 0;
    for (const unsigned int pixel : surface.pixels)
        lit += (pixel & 0x00FFFFFFu) != 0 ? 1 : 0;
    check(lit > 200, "skinned tube is drawn");

    // 카메라를 반대로 돌리면 스킨 AABB로 통째 컬링된다.
    runner.GetScene().GetCamera().SetCameraYaw(pi);
    runner.Update(0.f, 160, 120);
    check(runner.GetRenderQueue().GetRenderCommands().empty(), "skinned mesh outside the frustum is culled");

    // 법선·AABB 디버그는 변형된 정점 기준이다.
    runner.GetScene().GetCamera().SetCameraYaw(0.f);
    runner.GetDebugFlags().bShowNormal = true;
    runner.GetDebugFlags().bShowAABB = true;
    runner.Update(0.f, 160, 120);
    const auto debug = runner.GetRenderQueue().GetDebugCommands();
    check(debug.size() == 2, "AABB gizmo + normal lines");
    check(debug[1].vertices.size() == primitive.deformed.vertices.size() * 2, "a normal line per vertex");
    vec3Near(debug[1].vertices[0].position, primitive.deformed.vertices[0].position, "normal line starts at the deformed vertex");
}
} // namespace

void sr::test::RegisterAnimationTests(TestRegistry &registry)
{
    registry.Add("animation.quaternionMath", quaternionMath);
    registry.Add("animation.quaternionInterpolation", quaternionInterpolation);
    registry.Add("animation.keyframeSampling", keyframeSampling);
    registry.Add("animation.skinningKnownPose", skinningKnownPose);
    registry.Add("animation.skinningAvxMatchesScalar", skinningAvxMatchesScalar);
    registry.Add("animation.animatorPlayback", animatorPlayback);
    registry.Add("animation.loaderWiggleClip", loaderWiggleClip);
    registry.Add("animation.sceneSkinnedRendering", sceneSkinnedRendering);
}
