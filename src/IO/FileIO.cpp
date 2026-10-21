#include "IO/FileIO.h"

#include <fstream>
#include <system_error>

namespace sr::io
{
std::expected<std::string, AssetLoadError> ReadWholeFile(const std::filesystem::path &path,
                                                         std::uintmax_t maxBytes)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return std::unexpected(AssetLoadError{"Cannot open file: " + error.message(), path});
    if (size > maxBytes)
        return std::unexpected(AssetLoadError{"File is too large", path});

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::unexpected(AssetLoadError{"Cannot open file", path});
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected(AssetLoadError{"Cannot read file", path});
    return bytes;
}

std::expected<void, AssetLoadError> WriteFileAtomically(const std::filesystem::path &path,
                                                        std::span<const char> bytes)
{
    std::error_code error;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return std::unexpected(AssetLoadError{"Cannot create directory: " + error.message(), path});
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
            return std::unexpected(AssetLoadError{"Cannot create temporary file", temporary});
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file)
        {
            file.close();
            std::filesystem::remove(temporary, error);
            return std::unexpected(AssetLoadError{"Cannot write file", temporary});
        }
    }
    // std::filesystem::rename은 대상이 있으면 바꿔 넣는다(Windows: MoveFileExW REPLACE_EXISTING).
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return std::unexpected(AssetLoadError{"Cannot replace file: " + error.message(), path});
    }
    return {};
}
} // namespace sr::io
