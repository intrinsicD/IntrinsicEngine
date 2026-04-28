module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Core.IOBackend;

import Core.Error;

export namespace Core::IO
{
    // Stable asset identifier (hash of logical path or GUID).
    // Phase 0: hash of file path. Later: content hash, GUID, etc.
    struct AssetId
    {
        uint64_t Value = 0;

        static AssetId FromPath(std::string_view path);

        constexpr bool operator==(const AssetId&) const = default;
        [[nodiscard]] constexpr bool IsValid() const { return Value != 0; }
    };

    // What to read
    struct IORequest
    {
        std::string Path;         // Phase 0: file path. Phase 2: container locator.
        size_t Offset = 0;        // 0 = start of file
        size_t Size = 0;          // 0 = whole file
        uint8_t Priority = 128;   // 0 = highest. Phase 1: camera-driven streaming.
    };

    // What was read
    struct IOReadResult
    {
        std::vector<std::byte> Data;
    };

    // Abstract I/O backend. Phase 0: files. Phase 2: containers. Phase 3: io_uring.
    class IIOBackend
    {
    public:
        virtual ~IIOBackend() = default;
        IIOBackend(const IIOBackend&) = delete;
        IIOBackend& operator=(const IIOBackend&) = delete;
        IIOBackend(IIOBackend&&) = delete;
        IIOBackend& operator=(IIOBackend&&) = delete;

        // Synchronous read. Safe to call from any thread.
        // Phase 0 callers are already on worker threads (via Tasks::Scheduler::Dispatch).
        [[nodiscard]] virtual std::expected<IOReadResult, ErrorCode> Read(
            const IORequest& request) = 0;

        // Synchronous write. Safe to call from any thread.
        // IORequest::Path specifies the destination. Offset/Size are ignored (full replacement).
        [[nodiscard]] virtual std::expected<void, ErrorCode> Write(
            const IORequest& request,
            std::span<const std::byte> data) = 0;

    protected:
        IIOBackend() = default;
    };

    // Phase 0 implementation: reads/writes loose files via std::ifstream/std::ofstream.
    class FileIOBackend final : public IIOBackend
    {
    public:
        FileIOBackend() = default;
        [[nodiscard]] std::expected<IOReadResult, ErrorCode> Read(
            const IORequest& request) override;
        [[nodiscard]] std::expected<void, ErrorCode> Write(
            const IORequest& request,
            std::span<const std::byte> data) override;
    };
}
