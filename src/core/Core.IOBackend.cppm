// Backend-neutral byte I/O: IIOBackend plus the synchronous FileIOBackend used
// for config, asset loading and scene persistence.
module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Core.IOBackend;

import Extrinsic.Core.Error;

export namespace Extrinsic::Core::IO
{
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

