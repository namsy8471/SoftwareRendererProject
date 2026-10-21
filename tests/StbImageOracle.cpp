// 임시 테스트 오라클: 자체 디코더 결과를 stb_image와 픽셀 단위로 비교한다.
// 자체 디코더가 모든 fixture에서 같아지면 이 파일과 src/ThirdParty/stb_image를 함께 지운다.
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "ThirdParty/stb_image/stb_image.h"

namespace sr::test
{
struct OracleImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

std::optional<OracleImage> DecodeWithStb(std::span<const std::uint8_t> bytes)
{
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels =
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels)
        return std::nullopt;
    OracleImage image{
        width, height,
        std::vector<std::uint8_t>(pixels, pixels + static_cast<std::size_t>(width) * height * 4)};
    stbi_image_free(pixels);
    return image;
}
} // namespace sr::test
