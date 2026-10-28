#include "TextureLoader.h"

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>
#include <string_view>

#include "Material.h"
#include "Texture.h"
#include "Graphics/AssetParsing.h"

#include "Graphics/Codecs/ImageCodecs.h"

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

// map_Kd [옵션...] 파일 이름. 옵션(-o 1 1 1, -bm 0.5, -clamp on 등)을 건너뛰고 나머지 전체를 경로로 본다.
// 경로에 공백이 있을 수 있으므로 마지막 토큰만 쓰지 않는다.
[[nodiscard]] std::string_view textureMapPath(std::string_view arguments,
                                              float *outBumpMultiplier = nullptr) noexcept
{
    arguments = trimLeft(arguments);
    while (arguments.starts_with('-'))
    {
        const auto option = sr::asset::takeToken(arguments);
        // 옵션마다 받을 수 있는 값의 최대 개수. 숫자나 on/off만 값으로 소비한다.
        const int maxValues = option == "-o" || option == "-s" || option == "-t" ? 3 : option == "-mm" ? 2 : 1;
        for (int i = 0; i < maxValues; ++i)
        {
            auto peek = arguments;
            const auto value = sr::asset::takeToken(peek);
            const auto parsedFloat = sr::asset::parseNumber<float>(value);
            if (value.empty() || !(value == "on" || value == "off" || parsedFloat))
                break;
            if (option == "-bm" && i == 0 && outBumpMultiplier && parsedFloat)
                *outBumpMultiplier = *parsedFloat;
            arguments = peek;
        }
        arguments = trimLeft(arguments);
    }
    while (!arguments.empty() && (arguments.back() == ' ' || arguments.back() == '\t'))
        arguments.remove_suffix(1);
    return arguments;
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
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        return std::unexpected(AssetLoadError{"Unable to open image: " + filepath.string(), filepath});
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    // 형식은 시그니처로 고르고, 시그니처가 없는 TGA만 확장자를 본다.
    auto decoded = sr::codec::DecodeImage(bytes, filepath.extension().string());
    if (!decoded)
        return std::unexpected(
            AssetLoadError{"Unable to load image: " + filepath.string() + " (" + decoded.error() + ")", filepath});
    auto texture = std::make_shared<Texture>(decoded->width, decoded->height, std::move(decoded->rgba));
    texture->GenerateMipmaps();
    return texture;
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

    // 같은 이미지를 참조하는 머티리얼은 Texture 하나를 공유한다. 실패한 경로도 기억해 다시 읽지 않는다.
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;
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
        else if (command == "Ke")
        {
            auto color = parseVec3(arguments);
            if (!color)
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            currentMaterial->emissive = *color;
        }
        else if (command == "Pr")
        {
            auto remaining = arguments;
            auto value = takeNumber<float>(remaining);
            if (!value)
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            currentMaterial->roughness = std::clamp(*value, 0.04f, 1.0f);
            currentMaterial->isPbr = true;
        }
        else if (command == "Pm")
        {
            auto remaining = arguments;
            auto value = takeNumber<float>(remaining);
            if (!value)
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            currentMaterial->metallic = std::clamp(*value, 0.0f, 1.0f);
            currentMaterial->isPbr = true;
        }
        else if (command == "Ns" || command == "d" || command == "Tr")
        {
            auto remaining = arguments;
            auto value = takeNumber<float>(remaining);
            if (!value)
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            if (command == "Ns")
                currentMaterial->shininess = *value;
            else if (command == "d")
            {
                currentMaterial->opacity = std::clamp(*value, 0.0f, 1.0f);
                if (currentMaterial->opacity < 1.0f)
                    currentMaterial->alphaMode = AlphaMode::Blend;
            }
            else
            {
                currentMaterial->opacity = std::clamp(1.0f - *value, 0.0f, 1.0f);
                if (currentMaterial->opacity < 1.0f)
                    currentMaterial->alphaMode = AlphaMode::Blend;
            }
        }
        else if (command == "map_Kd")
        {
            const auto name = textureMapPath(arguments);
            if (name.empty())
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            // Windows에서 만든 MTL은 역슬래시 구분자를 쓴다. 모든 플랫폼에서 같은 파일을 찾도록 /로 바꾼다.
            std::string relative(name);
            std::ranges::replace(relative, '\\', '/');
            const auto texturePath = (filepath.parent_path() / std::filesystem::path(relative)).lexically_normal();
            auto [cached, inserted] = textureCache.try_emplace(texturePath.generic_string());
            if (inserted)
            {
                if (auto texture = LoadImageFile(texturePath))
                    cached->second = std::move(*texture);
            }
            currentMaterial->diffuseTexture = cached->second;
        }
        else if (command == "map_Bump" || command == "map_bump" || command == "bump")
        {
            float bumpScale = currentMaterial->normalScale;
            const auto name = textureMapPath(arguments, &bumpScale);
            if (name.empty())
                return std::unexpected(malformedMtl(filepath, lineNumber, command));
            std::string relative(name);
            std::ranges::replace(relative, '\\', '/');
            const auto texturePath = (filepath.parent_path() / std::filesystem::path(relative)).lexically_normal();
            auto [cached, inserted] = textureCache.try_emplace(texturePath.generic_string());
            if (inserted)
            {
                if (auto texture = LoadImageFile(texturePath))
                    cached->second = std::move(*texture);
            }
            currentMaterial->normalTexture = cached->second;
            currentMaterial->normalScale = bumpScale;
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
