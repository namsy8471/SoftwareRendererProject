#pragma once
#include <expected>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include "Utils/AssetLoadError.h"

struct StbiImageDeleter
{
	void operator()(unsigned char* p) const noexcept;
};

using StbiImagePtr = std::unique_ptr<unsigned char, StbiImageDeleter>;

class Texture;
struct Material;

class TextureLoader
{
public:
	[[nodiscard]] static std::expected<std::shared_ptr<Texture>, AssetLoadError> LoadImageFile(const std::filesystem::path& filepath);
	[[nodiscard]] static std::expected<std::unordered_map<std::string, Material>, AssetLoadError> LoadMTLFile(const std::filesystem::path& filepath);
};
