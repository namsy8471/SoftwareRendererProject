#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include "Utils/AssetLoadError.h"

// 작업 스레드에서 쓰는 동기 파일 함수. 프레임 루프에서는 BackgroundLoader를 통해서만 부른다.
namespace sr::io
{
// 기본 상한 1GiB. 드롭된 임의 파일이 메모리를 다 쓰지 않도록 크기를 먼저 확인한다.
inline constexpr std::uintmax_t defaultMaxFileBytes = 1ull << 30;

[[nodiscard]] std::expected<std::string, AssetLoadError> ReadWholeFile(
    const std::filesystem::path &path, std::uintmax_t maxBytes = defaultMaxFileBytes);

// 같은 폴더의 임시 파일에 다 쓴 뒤 rename으로 바꿔 넣는다. 쓰는 중 실패하거나 프로세스가 죽어도
// 기존 파일은 이전 내용 그대로다(임시 파일만 남을 수 있다). 상위 폴더가 없으면 만든다.
[[nodiscard]] std::expected<void, AssetLoadError> WriteFileAtomically(const std::filesystem::path &path,
                                                                      std::span<const char> bytes);
} // namespace sr::io
