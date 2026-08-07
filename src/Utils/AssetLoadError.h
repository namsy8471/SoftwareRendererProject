#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct AssetLoadError
{
    std::string message;
    std::filesystem::path path{};
    std::size_t line = 0;
};
