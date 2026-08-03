#pragma once

#include <filesystem>
#include <windows.h>

static std::filesystem::path GetExeDir()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) throw std::runtime_error("GetModuleFileNameW failed");
    return std::filesystem::path(buf).parent_path();
}

static std::string MakeAssetPath(const std::string& nameWithExt)
{
    static const std::filesystem::path base = GetExeDir() / L"assets";
    return (base / std::filesystem::path(std::wstring(nameWithExt.begin(), nameWithExt.end()))).string();
}