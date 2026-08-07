#include "TextureLoader.h"

#include <charconv>
#include <concepts>
#include <fstream>
#include <string_view>

#include "Material.h"
#include "Texture.h"

// stb_image는 C 헤더 기반 단일 구현 라이브러리이므로 이 매크로는 정확히 한
// 번역 단위에만 둔다. 자체 헤더/API로 전파하지 않는 것이 모듈 경계에도 안전하다.
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb_image/stb_image.h"

namespace
{
    [[nodiscard]] constexpr std::string_view trim_left(std::string_view text) noexcept
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        {
            text.remove_prefix(1);
        }
        return text;
    }

    // 한 줄을 "명령 토큰 + 나머지 인수"로 나눈다. string_view를 반환하므로
    // C++11/14식 stringstream과 임시 string 복사가 발생하지 않는다.
    [[nodiscard]] constexpr std::pair<std::string_view, std::string_view>
        split_command(std::string_view line) noexcept
    {
        line = trim_left(line);
        const auto separator = line.find_first_of(" \t\r");
        if (separator == std::string_view::npos)
        {
            return { line, {} };
        }
        return { line.substr(0, separator), trim_left(line.substr(separator)) };
    }

    template <typename Number>
        requires std::floating_point<Number> || std::integral<Number>
    [[nodiscard]] std::expected<Number, std::string_view> parse_number(std::string_view& input) noexcept
    {
        input = trim_left(input);
        const auto separator = input.find_first_of(" \t\r");
        const auto token = input.substr(0, separator);
        if (token.empty()) return std::unexpected(token);

        Number value{};
        const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (error != std::errc{} || end != token.data() + token.size())
        {
            return std::unexpected(token);
        }

        input = separator == std::string_view::npos ? std::string_view{} : input.substr(separator);
        return value;
    }

    [[nodiscard]] std::expected<SRMath::vec3, std::string_view>
        parse_vec3(std::string_view input) noexcept
    {
        auto x = parse_number<float>(input);
        if (!x) return std::unexpected(x.error());
        auto y = parse_number<float>(input);
        if (!y) return std::unexpected(y.error());
        auto z = parse_number<float>(input);
        if (!z) return std::unexpected(z.error());
        return SRMath::vec3{ *x, *y, *z };
    }

    [[nodiscard]] AssetLoadError malformed_mtl(const std::filesystem::path& path,
                                                std::size_t line,
                                                std::string_view command)
    {
        return {
            "Malformed MTL value for command '" + std::string(command) + "'",
            path,
            line
        };
    }
}

void StbiImageDeleter::operator()(unsigned char* pixels) const noexcept
{
    stbi_image_free(pixels);
}

std::expected<std::shared_ptr<Texture>, AssetLoadError>
TextureLoader::LoadImageFile(const std::filesystem::path& filepath)
{
    auto texture = std::make_shared<Texture>();

    int channels = 0;
    const auto nativePath = filepath.string();
    unsigned char* pixels = stbi_load(nativePath.c_str(), &texture->m_width, &texture->m_height, &channels, 4);

    if (!pixels)
    {
        const char* reason = stbi_failure_reason();
        return std::unexpected(AssetLoadError{
            "Unable to load image: " + nativePath + (reason ? " (" + std::string(reason) + ")" : ""),
            filepath
        });
    }

    texture->SetPixels(StbiImagePtr{ pixels });
    return texture;
}

std::expected<std::unordered_map<std::string, Material>, AssetLoadError>
TextureLoader::LoadMTLFile(const std::filesystem::path& filepath)
{
    std::ifstream file(filepath);
    if (!file)
    {
        return std::unexpected(AssetLoadError{ "Unable to open MTL file: " + filepath.string(), filepath });
    }

    std::unordered_map<std::string, Material> materials;
    materials.reserve(100);

    Material* currentMaterial = nullptr;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        const auto [command, arguments] = split_command(line);
        if (command.empty() || command.starts_with('#'))
        {
            continue;
        }

        if (command == "newmtl")
        {
            const auto nameView = trim_left(arguments);
            if (nameView.empty())
            {
                return std::unexpected(malformed_mtl(filepath, lineNumber, command));
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
            auto color = parse_vec3(arguments);
            if (!color) return std::unexpected(malformed_mtl(filepath, lineNumber, command));
            if (command == "Ka") currentMaterial->ambient = *color;
            else if (command == "Kd") currentMaterial->diffuse = *color;
            else currentMaterial->specular = *color;
        }
        else if (command == "Ns" || command == "d")
        {
            auto remaining = arguments;
            auto value = parse_number<float>(remaining);
            if (!value) return std::unexpected(malformed_mtl(filepath, lineNumber, command));
            if (command == "Ns") currentMaterial->shininess = *value;
            else currentMaterial->opacity = *value;
        }
        else if (command == "illum")
        {
            auto remaining = arguments;
            auto value = parse_number<int>(remaining);
            if (!value) return std::unexpected(malformed_mtl(filepath, lineNumber, command));
            currentMaterial->illuminationModel = *value;
        }
    }

    return materials;
}
