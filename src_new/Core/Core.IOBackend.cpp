module;

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module Extrinsic.Core.IOBackend;

namespace Extrinsic::Core::IO
{
    PathKey PathKey::FromPath(std::string_view path) noexcept
    {
        // FNV-1a 64-bit
        uint64_t hash = 14695981039346656037ULL;
        for (const char c : path)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return PathKey{hash};
    }

    Core::Expected<IOReadResult> FileIOBackend::Read(const IORequest& request)
    {
        namespace fs = std::filesystem;

        if (request.Path.empty())
            return std::unexpected(Core::ErrorCode::InvalidPath);

        std::error_code ec;
        if (!fs::exists(request.Path, ec) || ec)
            return std::unexpected(Core::ErrorCode::FileNotFound);

        std::ifstream file(request.Path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return std::unexpected(Core::ErrorCode::FileReadError);

        const auto fileSize = static_cast<std::size_t>(file.tellg());
        const std::size_t offset = request.Offset;
        if (offset > fileSize)
            return std::unexpected(Core::ErrorCode::OutOfRange);

        const std::size_t readSize =
            (request.Size == 0) ? (fileSize - offset) : request.Size;
        if (offset + readSize > fileSize)
            return std::unexpected(Core::ErrorCode::OutOfRange);

        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file)
            return std::unexpected(Core::ErrorCode::FileReadError);

        IOReadResult result;
        result.Data.resize(readSize);
        file.read(reinterpret_cast<char*>(result.Data.data()),
                  static_cast<std::streamsize>(readSize));
        if (!file)
            return std::unexpected(Core::ErrorCode::FileReadError);

        return result;
    }

    Core::Result FileIOBackend::Write(const IORequest& request,
                                      std::span<const std::byte> data)
    {
        if (request.Path.empty())
            return std::unexpected(Core::ErrorCode::InvalidPath);

        namespace fs = std::filesystem;
        std::error_code ec;
        const auto parent = fs::path(request.Path).parent_path();
        if (!parent.empty())
            fs::create_directories(parent, ec);

        std::ofstream file(request.Path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return std::unexpected(Core::ErrorCode::FileWriteError);

        if (!data.empty())
        {
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
            if (!file)
                return std::unexpected(Core::ErrorCode::FileWriteError);
        }
        return Core::Ok();
    }
}

