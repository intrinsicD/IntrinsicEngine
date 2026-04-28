module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module Core.IOBackend;

namespace Core::IO
{
    AssetId AssetId::FromPath(std::string_view path)
    {
        // FNV-1a 64-bit hash
        uint64_t hash = 14695981039346656037ULL;
        for (char c : path)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return AssetId{hash};
    }

    std::expected<IOReadResult, ErrorCode> FileIOBackend::Read(const IORequest& request)
    {
        namespace fs = std::filesystem;

        if (request.Path.empty())
            return std::unexpected(ErrorCode::InvalidPath);

        std::error_code ec;
        if (!fs::exists(request.Path, ec) || ec)
            return std::unexpected(ErrorCode::FileNotFound);

        std::ifstream file(request.Path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return std::unexpected(ErrorCode::FileReadError);

        const auto fileSize = static_cast<size_t>(file.tellg());

        const size_t offset = request.Offset;
        if (offset > fileSize)
            return std::unexpected(ErrorCode::OutOfRange);

        const size_t readSize = (request.Size == 0) ? (fileSize - offset) : request.Size;
        if (offset + readSize > fileSize)
            return std::unexpected(ErrorCode::OutOfRange);

        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file)
            return std::unexpected(ErrorCode::FileReadError);

        IOReadResult result;
        result.Data.resize(readSize);
        file.read(reinterpret_cast<char*>(result.Data.data()), static_cast<std::streamsize>(readSize));

        if (!file)
            return std::unexpected(ErrorCode::FileReadError);

        return result;
    }

    std::expected<void, ErrorCode> FileIOBackend::Write(
        const IORequest& request,
        std::span<const std::byte> data)
    {
        if (request.Path.empty())
            return std::unexpected(ErrorCode::InvalidPath);

        // Create parent directories if they don't exist
        namespace fs = std::filesystem;
        std::error_code ec;
        auto parent = fs::path(request.Path).parent_path();
        if (!parent.empty())
            fs::create_directories(parent, ec);

        std::ofstream file(request.Path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return std::unexpected(ErrorCode::FileWriteError);

        if (!data.empty())
        {
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
            if (!file)
                return std::unexpected(ErrorCode::FileWriteError);
        }

        return {};
    }
}
