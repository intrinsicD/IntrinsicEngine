// Per-frame packed vertex uploads for transient debug and visualization overlays.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.TransientDebugUploadHelper;

import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; } }

// The renderer owns per-lane leases and resets them before BufferManager shutdown.
// Frame slots retain buffers until reuse; packed uploads stay separate from retained GpuWorld geometry.

export namespace Extrinsic::Graphics
{
    // Shared 16-byte BDA vertex layout consumed by debug and visualization shaders.
    struct PackedColorVertex
    {
        float Position[3];
        std::uint32_t PackedColor;
    };
    static_assert(sizeof(PackedColorVertex) == 16u);

    struct PackedVertexUploadResult
    {
        RHI::BufferHandle Handle{};
        std::uint64_t BDA{0u};
        bool Uploaded{false};
        bool Overflow{false};
    };

    [[nodiscard]] std::uint32_t PackVertexColorUnorm4x8(const glm::vec4& color) noexcept;

    // Writes `bytes` into a retained host-visible storage buffer that grows
    // geometrically in whole `elementBytes` units up to `maxElementCount`.
    // The caller owns frame-slot selection and caps; allocation failure reports
    // Overflow and leaves the slot empty. Call only after the slot is reusable.
    [[nodiscard]] PackedVertexUploadResult UploadRetainedHostBytes(
        RHI::IDevice& device,
        RHI::BufferManager& bufferManager,
        std::optional<RHI::BufferManager::BufferLease>& bufferLease,
        std::uint64_t& capacityBytes,
        std::uint64_t& bufferAllocationCount,
        std::span<const std::byte> bytes,
        std::uint64_t elementBytes,
        std::uint64_t initialElementCount,
        std::uint64_t maxElementCount,
        const char* debugName);

    // Packed-vertex form of `UploadRetainedHostBytes`.
    [[nodiscard]] PackedVertexUploadResult UploadPackedColorVertices(
        RHI::IDevice& device,
        RHI::BufferManager& bufferManager,
        std::optional<RHI::BufferManager::BufferLease>& bufferLease,
        std::uint64_t& capacityBytes,
        std::uint64_t& bufferAllocationCount,
        std::span<const PackedColorVertex> staging,
        std::uint64_t initialVertexCount,
        std::uint64_t maxVertexCount,
        const char* debugName);


    struct TransientDebugTriangleUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // Each line packet contributes two packed vertices.
    struct TransientDebugLineUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // Each point packet contributes one packed vertex.
    struct TransientDebugPointUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // Device and manager must outlive the helper. Each lane owns one growing
    // buffer per frame slot, retained until that slot is reusable.
    class TransientDebugUploadHelper
    {
    public:
        TransientDebugUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~TransientDebugUploadHelper();

        void BeginFrame(std::uint32_t frameIndex,
                        std::uint32_t framesInFlight);

        [[nodiscard]] TransientDebugTriangleUploadResult UploadTriangles(
            std::span<const DebugTrianglePacket> triangles);

        [[nodiscard]] TransientDebugLineUploadResult UploadLines(
            std::span<const DebugLinePacket> lines);

        [[nodiscard]] TransientDebugPointUploadResult UploadPoints(
            std::span<const DebugPointPacket> points);

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept
        {
            return m_BufferAllocationCount;
        }

    private:
        RHI::IDevice*       m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};

        struct UploadBufferSlot
        {
            std::optional<RHI::BufferManager::BufferLease> Buffer{};
            std::uint64_t CapacityBytes{0u};
        };

        void EnsureFrameSlots(std::uint32_t framesInFlight);

        std::vector<UploadBufferSlot> m_TriangleVertexBufferSlots{};

        std::vector<UploadBufferSlot> m_LineVertexBufferSlots{};

        std::vector<UploadBufferSlot> m_PointVertexBufferSlots{};

        std::uint32_t  m_ActiveSlot{0u};
        std::uint64_t  m_BufferAllocationCount{0u};
    };
}
