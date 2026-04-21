module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Core.IOBackend;

import Extrinsic.Core.Error;

// -----------------------------------------------------------------------
// Extrinsic::Core::IO — Abstract I/O backend for asset streaming.
//
// Phase 0: synchronous loose-file reads/writes via std::ifstream/ofstream.
// Phase 1: camera-driven priority queues, async io_uring backend.
// Phase 2: container/pak format locators.
//
// IIOBackend is the extension point; FileIOBackend is the Phase 0 impl.
// PathKey is a stable 64-bit FNV-1a hash of a logical path — distinct from
// Extrinsic::Assets::AssetId, which is an engine asset identity.
// -----------------------------------------------------------------------

export namespace Extrinsic::Core::IO
{
    // Stable key derived from a logical path (FNV-1a 64-bit).
    // Not the same as Extrinsic::Assets::AssetId — this is purely an IO-layer
    // cache key; the asset pipeline assigns its own identity above this layer.
    struct PathKey
    {
        uint64_t Value = 0;

        [[nodiscard]] static PathKey FromPath(std::string_view path) noexcept;
        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }
        constexpr bool operator==(const PathKey&) const = default;
    };

    struct IORequest
    {
        std::string Path;       // Logical path / container locator.
        std::size_t Offset = 0; // Byte offset; 0 = start of file.
        std::size_t Size   = 0; // Bytes to read; 0 = entire file.
        uint8_t Priority   = 128; // 0 = highest priority (camera-driven streaming).
    };

    struct IOReadResult
    {
        std::vector<std::byte> Data;
    };

    // Abstract backend interface. Implementations must be thread-safe:
    // Read() and Write() may be called concurrently from worker threads.
    class IIOBackend
    {
    public:
        virtual ~IIOBackend() = default;
        IIOBackend(const IIOBackend&) = delete;
        IIOBackend& operator=(const IIOBackend&) = delete;
        IIOBackend(IIOBackend&&) = delete;
        IIOBackend& operator=(IIOBackend&&) = delete;

        // Synchronous read. Safe to call from any thread.
        [[nodiscard]] virtual Core::Expected<IOReadResult> Read(
            const IORequest& request) = 0;

        // Synchronous write. Safe to call from any thread.
        // Offset/Size in the request are ignored (full replacement write).
        [[nodiscard]] virtual Core::Result Write(
            const IORequest& request,
            std::span<const std::byte> data) = 0;

    protected:
        IIOBackend() = default;
    };

    // Phase 0: reads/writes loose files via std::ifstream/ofstream.
    class FileIOBackend final : public IIOBackend
    {
    public:
        FileIOBackend() = default;

        [[nodiscard]] Core::Expected<IOReadResult> Read(
            const IORequest& request) override;

        [[nodiscard]] Core::Result Write(
            const IORequest& request,
            std::span<const std::byte> data) override;
    };
}

