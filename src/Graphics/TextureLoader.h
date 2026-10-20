#pragma once
#include <expected>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include "Utils/AssetLoadError.h"

struct StbiImageDeleter
{
    void operator()(unsigned char *p) const noexcept;
};

using StbiImagePtr = std::unique_ptr<unsigned char, StbiImageDeleter>;

class Texture;
struct Material;

class TextureLoader
{
  public:
    // RGBA 픽셀 수명은 Texture가 소유한다. 열기/디코딩 실패는 expected 오류다.
    [[nodiscard]] static std::expected<std::shared_ptr<Texture>, AssetLoadError> LoadImageFile(
        const std::filesystem::path &filepath);
    // MTL의 색상·광택 값을 읽는다. 현재 map_Kd 자동 연결은 지원하지 않는다.
    [[nodiscard]] static std::expected<std::unordered_map<std::string, Material>, AssetLoadError> LoadMTLFile(
        const std::filesystem::path &filepath);
};
