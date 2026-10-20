#include "TextureLoader.h"

#include <charconv>
#include <concepts>
#include <fstream>
#include <memory>
#include <vector>
#include <string_view>

#include "Material.h"
#include "Texture.h"
#include "Graphics/AssetParsing.h"

// stb_image는 C 헤더 기반 단일 구현 라이브러리이므로 이 매크로는 정확히 한
// 번역 단위에만 둔다. 자체 헤더/API로 전파하지 않는 것이 모듈 경계에도 안전하다.
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb_image/stb_image.h"

namespace
{
using sr::asset::splitCommand;
using sr::asset::trimLeft;

template <class Number> [[nodiscard]] auto takeNumber(std::string_view &input) noexcept
{
    return sr::asset::parseNumber<Number>(sr::asset::takeToken(input));
}

[[nodiscard]] std::expected<SRMath::vec3, std::string_view> parseVec3(std::string_view input) noexcept
{
    auto x = takeNumber<float>(input);
    if (!x)
        return std::unexpected(x.error());
    auto y = takeNumber<float>(input);
    if (!y)
        return std::unexpected(y.error());
    auto z = takeNumber<float>(input);
    if (!z)
        return std::unexpected(z.error());
    return SRMath::vec3{*x, *y, *z};
}

[[nodiscard]] AssetLoadError malformedMtl(const std::filesystem::path &path, std::size_t line,
                                          std::string_view command)
{
    return {"Malformed MTL value for command '" + std::string(command) + "'", path, line};
}
} // namespace

std::expected<std::shared_ptr<Texture>, AssetLoadError> TextureLoader::LoadImageFile(
    const std::filesystem::path &filepath)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    const auto nativePath = filepath.string();
    // stb 버퍼는 이 함수 안에서만 산다. Texture는 디코더를 모르도록 값으로 복사해 받는다.
    const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels{
        stbi_load(nativePath.c_str(), &width, &height, &channels, 4), &stbi_image_free};

    if (!pixels)
    {
        const char *reason = stbi_failure_reason();
        return std::unexpected(AssetLoadError{"Unable to load image: " + nativePath +
                                                  (reason ? " (" + std::string(reason) + ")" : ""),
                                              filepath});
    }

    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    return std::make_shared<Texture>(width, height,
                                     std::vector<unsigned char>(pixels.get(), pixels.get() + byteCount));
}

std::expected<std::unordered_map<std::string, Material>, AssetLoadError> TextureLoader::LoadMTLFile(
    const std::filesystem::path &filepath)
{
    std::ifstream file(filepath);
    if (!file)
    {
        return std::unexpected(AssetLoadError{"Unable to open MTL file: " + filepath.string(), filepath});
    }

    std::unordered_map<std::string, Material> materials;
    materials.reserve(100);

    Material *currentMaterial = nullptr;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        sr::asset::trimLineEnd(line);
        const auto [command, arguments] = splitCommand(line);
        if (command.empty() || command.starts_with('#'))
        {
            continue;
        }

        if (command == "newmtl")
        {
            const auto nameView = trimLeft(arguments);
            if (nameView.empty())
            {
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            }

            auto [iterator, inserted] = materials.try_emplace(std::string(nameView));
            currentMaterial = &iterator->second;
            currentMaterial->name = iterator->first;
            continue;
        }

        if (!currentMaterial)
        {
            continue;
        }

        if (command == "Ka" || command == "Kd" || command == "Ks")
        {
            auto color = parseVec3(arguments);
            if (!color)
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            if (command == "Ka")
                currentMaterial->ambient = *color;
            else if (command == "Kd")
                currentMaterial->diffuse = *color;
            else
                currentMaterial->specular = *color;
        }
        else if (command == "Ns" || command == "d")
        {
            auto remaining = arguments;
            auto value = takeNumber<float>(remaining);
            if (!value)
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            if (command == "Ns")
                currentMaterial->shininess = *value;
            else
                currentMaterial->opacity = *value;
        }
        else if (command == "illum")
        {
            auto remaining = arguments;
            auto value = takeNumber<int>(remaining);
            if (!value)
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            currentMaterial->illuminationModel = *value;
        }
    }

    return materials;
}
