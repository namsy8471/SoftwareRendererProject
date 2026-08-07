#pragma once

#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <windows.h>

// GetModuleFileNameW의 MAX_PATH 고정 버퍼는 긴 경로에서 잘릴 수 있다. C++11
// vector를 성장 버퍼로 사용해 필요한 길이까지 반복하고 path로 소유권을 넘긴다.
[[nodiscard]] inline std::filesystem::path GetExecutableDirectory()
{
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
}

[[nodiscard]] inline std::filesystem::path MakeAssetPath(std::string_view nameWithExt)
{
    static const std::filesystem::path base = GetExecutableDirectory() / L"assets";
    return base / std::filesystem::path(nameWithExt);
}
