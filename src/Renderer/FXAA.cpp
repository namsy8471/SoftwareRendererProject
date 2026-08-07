#include "Renderer/FXAA.h"

#include "Math/SRMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

// oneTBB is an implementation dependency. Keeping it in this .cpp prevents
// vendor declarations from becoming part of the public FXAA interface.
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace
{
    constexpr float edge_threshold = 0.08f;
    constexpr float minimum_edge_threshold = 0.03125f;
    constexpr int maximum_span = 16;
    constexpr float epsilon = 1e-6f;
    constexpr float blend_strength = 0.2f;

    [[nodiscard]] SRMath::Color unpack_color(sr::fxaa::Pixel pixel) noexcept
    {
        constexpr float byte_to_unit = 1.0f / 255.0f;
        return {
            static_cast<float>(pixel & 0xffu) * byte_to_unit,
            static_cast<float>((pixel >> 8u) & 0xffu) * byte_to_unit,
            static_cast<float>((pixel >> 16u) & 0xffu) * byte_to_unit
        };
    }

    [[nodiscard]] sr::fxaa::Pixel pack_color(const SRMath::Color& color) noexcept
    {
        const auto to_byte = [](float channel) noexcept {
            return static_cast<sr::fxaa::Pixel>(std::clamp(channel, 0.0f, 1.0f) * 255.0f);
        };

        const auto red = to_byte(color.r);
        const auto green = to_byte(color.g);
        const auto blue = to_byte(color.b);
        return (blue << 16u) | (green << 8u) | red;
    }

    [[nodiscard]] constexpr SRMath::Color lerp_color(
        const SRMath::Color& from, const SRMath::Color& to, float amount) noexcept
    {
        return {
            from.r + (to.r - from.r) * amount,
            from.g + (to.g - from.g) * amount,
            from.b + (to.b - from.b) * amount
        };
    }

    [[nodiscard]] constexpr float luma(const SRMath::Color& color) noexcept
    {
        // NTSC luma weights retained to preserve the renderer's edge response.
        return color.r * 0.299f + color.g * 0.587f + color.b * 0.114f;
    }
}

void sr::fxaa::Apply(
    std::span<const Pixel> input, std::span<Pixel> output, int width, int height)
{
    if (width <= 0 || height <= 0) return;

    const auto row_size = static_cast<std::size_t>(width);
    const auto row_count = static_cast<std::size_t>(height);
    if (row_size > std::numeric_limits<std::size_t>::max() / row_count) return;

    const std::size_t pixel_count = row_size * row_count;
    if (input.size() < pixel_count || output.size() < pixel_count) return;

    // FXAA needs a one-pixel neighborhood. Tiny images have no interior, so a
    // typed range copy is both safer and clearer than pointer arithmetic.
    if (width < 3 || height < 3)
    {
        std::ranges::copy(input.first(pixel_count), output.begin());
        return;
    }

    std::vector<float> luma_buffer(pixel_count);

    // Pass 1 owns disjoint rows. The older implementation let adjacent TBB
    // tasks write overlapping halo rows; identical values still constitute a
    // C++ data race. Finishing this pass before filtering provides the required
    // synchronization and makes every read in pass 2 immutable.
    tbb::parallel_for(tbb::blocked_range<int>{ 0, height },
        [&](const tbb::blocked_range<int>& rows)
        {
            for (int y = rows.begin(); y != rows.end(); ++y)
            {
                const std::size_t row = static_cast<std::size_t>(y) * row_size;
                for (int x = 0; x < width; ++x)
                {
                    const std::size_t index = row + static_cast<std::size_t>(x);
                    luma_buffer[index] = luma(unpack_color(input[index]));
                }
            }
        });

    // Pass 2 also owns disjoint output rows and only reads the completed luma map.
    tbb::parallel_for(tbb::blocked_range<int>{ 1, height - 1 },
        [&](const tbb::blocked_range<int>& rows)
        {
            for (int y = rows.begin(); y != rows.end(); ++y)
            {
                for (int x = 1; x < width - 1; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * row_size + x;
                    const float center = luma_buffer[index];
                    const float north = luma_buffer[index - row_size];
                    const float south = luma_buffer[index + row_size];
                    const float west = luma_buffer[index - 1];
                    const float east = luma_buffer[index + 1];

                    const float minimum = std::min({ center, north, south, west, east });
                    const float maximum = std::max({ center, north, south, west, east });
                    const float contrast = maximum - minimum;
                    const float threshold = std::max(minimum_edge_threshold, maximum * edge_threshold);
                    if (contrast < threshold)
                    {
                        output[index] = input[index];
                        continue;
                    }

                    const float horizontal_difference = std::abs(west - east);
                    const float vertical_difference = std::abs(north - south);
                    const bool vertical_edge = vertical_difference >= horizontal_difference;
                    const float gradient = vertical_edge ? vertical_difference : horizontal_difference;
                    if (gradient <= epsilon)
                    {
                        output[index] = input[index];
                        continue;
                    }

                    float negative_distance = 0.0f;
                    float positive_distance = 0.0f;
                    for (int offset = 0; offset < maximum_span; ++offset)
                    {
                        const int sample_x = vertical_edge ? x : x - offset - 1;
                        const int sample_y = vertical_edge ? y - offset - 1 : y;
                        if (sample_x < 0 || sample_y < 0) break;

                        const std::size_t sample_index =
                            static_cast<std::size_t>(sample_y) * row_size + sample_x;
                        if (std::abs(luma_buffer[sample_index] - center) / gradient >= 0.4f) break;
                        negative_distance = static_cast<float>(offset + 1);
                    }

                    for (int offset = 0; offset < maximum_span; ++offset)
                    {
                        const int sample_x = vertical_edge ? x : x + offset + 1;
                        const int sample_y = vertical_edge ? y + offset + 1 : y;
                        if (sample_x >= width || sample_y >= height) break;

                        const std::size_t sample_index =
                            static_cast<std::size_t>(sample_y) * row_size + sample_x;
                        if (std::abs(luma_buffer[sample_index] - center) / gradient >= 0.4f) break;
                        positive_distance = static_cast<float>(offset + 1);
                    }

                    const float edge_length = positive_distance + negative_distance;
                    if (edge_length < 1.0f)
                    {
                        output[index] = input[index];
                        continue;
                    }

                    const float pixel_offset = (positive_distance - negative_distance) / edge_length;
                    const float blend_factor = blend_strength * std::abs(pixel_offset);
                    const std::size_t neighbor_index = vertical_edge
                        ? (pixel_offset > 0.0f ? index - row_size : index + row_size)
                        : (pixel_offset > 0.0f ? index - 1 : index + 1);

                    output[index] = pack_color(lerp_color(
                        unpack_color(input[index]), unpack_color(input[neighbor_index]), blend_factor));
                }
            }
        });

    // The filter does not process the outer one-pixel border.
    for (int y = 0; y < height; ++y)
    {
        const std::size_t row = static_cast<std::size_t>(y) * row_size;
        output[row] = input[row];
        output[row + row_size - 1] = input[row + row_size - 1];
    }
    std::ranges::copy(input.first(row_size), output.begin());
    std::ranges::copy(input.subspan(pixel_count - row_size, row_size),
        output.begin() + static_cast<std::ptrdiff_t>(pixel_count - row_size));
}
