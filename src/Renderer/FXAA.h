#pragma once

#include <span>
#include "Renderer/RenderTarget.h"

namespace sr::fxaa
{
using Pixel = unsigned int;
static_assert(sizeof(Pixel) == 4, "The Win32 DIB back buffer requires 32-bit pixels.");

void Apply(std::span<const Pixel> input, std::span<Pixel> output, int width, int height);
// 입력과 출력은 겹치지 않아야 한다. 서로 다른 stride를 허용하고 행 padding은 보존한다.
void Apply(ConstPixelSurfaceView input, PixelSurfaceView output);
} // namespace sr::fxaa
