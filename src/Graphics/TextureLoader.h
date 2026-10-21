#pragma once
#include <expected>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include "Utils/AssetLoadError.h"

class Texture;
struct Material;

class TextureLoader
{
  public:
    // 디코딩한 RGBA 픽셀을 Texture에 복사해 넘긴다. 열기/디코딩 실패는 expected 오류다.
    [[nodiscard]] static std::expected<std::shared_ptr<Texture>, AssetLoadError> LoadImageFile(
        const std::filesystem::path &filepath);
    // MTL의 색상·광택 값과 map_Kd 텍스처를 읽는다. 텍스처 경로는 MTL 폴더 기준이고, 같은 파일은 한 번만 디코딩해
    // 머티리얼끼리 공유한다. 텍스처를 읽지 못하면 그 머티리얼만 텍스처 없이(Kd 색) 남기고 계속한다.
    [[nodiscard]] static std::expected<std::unordered_map<std::string, Material>, AssetLoadError> LoadMTLFile(
        const std::filesystem::path &filepath);
};
