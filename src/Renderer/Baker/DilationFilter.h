#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sr::baker
{

// Dilates valid texels in a 0x00RRGGBB image by `radius` pixels into invalid/background texels.
// mask: 1 for valid texels, 0 for empty background texels.
// Mutates both pixels and mask in place so that newly dilated texels are marked valid.
void DilateRGB(int width, int height, std::span<std::uint32_t> pixels,
               std::span<std::uint8_t> mask, int radius = 8);

// Dilates normal map vectors specifically, decoding tangent-space normals,
// averaging them in 3D vector space, renormalizing, and re-encoding to RGB [0, 255].
void DilateNormalMap(int width, int height, std::span<std::uint32_t> pixels,
                     std::span<std::uint8_t> mask, int radius = 8);

// Dilates single-channel 8-bit grayscale values (e.g. AO map).
void DilateGrayscale(int width, int height, std::span<std::uint8_t> values,
                     std::span<std::uint8_t> mask, int radius = 8);

} // namespace sr::baker
