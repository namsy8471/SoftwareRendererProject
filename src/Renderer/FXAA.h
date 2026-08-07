#pragma once

#include <span>

namespace sr::fxaa
{
    using Pixel = unsigned int;
    static_assert(sizeof(Pixel) == 4, "The Win32 DIB back buffer requires 32-bit pixels.");

    // 입력과 출력은 소유하지 않는 동일 크기 32-bit pixel buffer다. C++20
    // span으로 포인터와 길이를 함께 전달해 예전 raw-pointer API의 범위 누락을
    // 막는다. 두 span은 서로 겹치지 않아야 한다.
    void Apply(std::span<const Pixel> input, std::span<Pixel> output, int width, int height);
}
