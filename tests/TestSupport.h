#pragma once

// 여러 테스트 파일이 공유하는 검사 도구다. Release에서도 생략되지 않도록 assert 대신 예외를 쓴다.
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Math/SRMath.h"
#include "Renderer/RenderTarget.h"
#include "Scene/GameObject.h"
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sr::test
{
// assert와 달리 NDEBUG에서도 검사를 수행한다.
inline void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}
inline bool approximatelyEqual(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) <= tolerance;
}
inline void vectorNear(const SRMath::vec4 &a, const SRMath::vec4 &b)
{
    for (std::size_t i = 0; i < 4; ++i)
        check(approximatelyEqual(a[i], b[i]), "vector mismatch");
}
class MemorySurface
{
  public:
    MemorySurface(int width = 160, int height = 120) { Resize(width, height); }
    void Resize(int width, int height)
    {
        target.Detach();
        pixels.resize(static_cast<std::size_t>(width) * height);
        check(target.Attach({pixels, width, height, static_cast<std::size_t>(width)}).has_value(),
              "memory target attachment");
        target.Clear();
    }
    std::vector<unsigned int> pixels;
    RenderTarget target;
};
inline std::shared_ptr<GameObject> loadObject(const std::filesystem::path &path, SRMath::vec3 position,
                                       float scale = 1.f)
{
    auto model = ModelLoader::LoadOBJ(path);
    check(model.has_value(), "model load failed");
    return std::make_shared<GameObject>(position, SRMath::vec3{0.f, 0.f, 0.f},
                                        SRMath::vec3{scale, scale, scale}, std::move(*model));
}
} // namespace sr::test
