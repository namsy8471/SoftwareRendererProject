#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "Math/SRMath.h"

class Texture;

enum class AlphaMode : std::uint8_t
{
    Opaque = 0,
    Mask,
    Blend
};

struct Material
{
    std::string name; // 머티리얼 이름
    SRMath::vec3 ambient = {0.5f, 0.5f, 0.5f};
    SRMath::vec3 diffuse = {0.5f, 0.5f, 0.5f};
    SRMath::vec3 specular = {0.5f, 0.5f, 0.5f};
    float shininess = 32.0f;
    float opacity = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;

    // 여러 mesh material이 같은 이미지 수명을 공유하므로 shared_ptr가 맞다.
    // 빈 상태는 nullptr 리터럴보다 값 초기화로 표현한다.
    std::shared_ptr<Texture> diffuseTexture{};
    std::shared_ptr<Texture> normalTexture{};
    float normalScale = 1.0f;
    int illuminationModel = 2; // MTL illum 2: Phong 반사 모델
};
