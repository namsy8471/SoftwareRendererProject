#pragma once

#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#include "Platform/Win32Headers.h"
#endif

[[nodiscard]] inline std::filesystem::path GetExecutableDirectory()
{
#if !defined(_WIN32)
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#else
    std::vector<wchar_t> buffer(260);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            throw std::runtime_error("GetModuleFileNameW failed");
        if (length < buffer.size() - 1)
            return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
        buffer.resize(buffer.size() * 2);
    }
#endif
}

[[nodiscard]] inline std::filesystem::path MakeAssetPath(std::string_view nameWithExt)
{
    static const std::filesystem::path base = GetExecutableDirectory() / L"assets";
    return base / std::filesystem::path(nameWithExt);
}
