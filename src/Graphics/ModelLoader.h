#pragma once
#include <expected>
#include <filesystem>
#include <memory>
#include "Utils/AssetLoadError.h"

class Model;

class ModelLoader
{
public:
	[[nodiscard]] static std::expected<std::unique_ptr<Model>, AssetLoadError> LoadOBJ(const std::filesystem::path& filepath);
};
