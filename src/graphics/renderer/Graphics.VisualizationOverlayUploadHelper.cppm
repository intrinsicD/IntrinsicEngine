// Uploads vector-field and isoline packets into retained per-lane GPU buffers.
module;

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module Extrinsic.Graphics.VisualizationOverlayUploadHelper;

import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; } }

export namespace Extrinsic::Graphics
{
    // Each glyph contributes two packed vertices.
    struct VisualizationVectorFieldUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // Each iso value contributes a deterministic fixture segment, not a
    // scalar-field-derived polyline.
    struct VisualizationIsolineUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // Uploads fixture positions: source position/vector BDAs are not CPU-readable.
    // Each lane retains one growing buffer per frame slot. Device and manager
    // must outlive the helper; the renderer resets it before manager shutdown.
    class VisualizationOverlayUploadHelper
    {
    public:
        VisualizationOverlayUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~VisualizationOverlayUploadHelper();

        void BeginFrame(std::uint32_t frameIndex,
                        std::uint32_t framesInFlight);

        [[nodiscard]] VisualizationVectorFieldUploadResult UploadVectorFields(
            std::span<const VectorFieldOverlayPacket> vectorFields);

        [[nodiscard]] VisualizationIsolineUploadResult UploadIsolines(
            std::span<const IsolineOverlayPacket> isolines);

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

        std::vector<UploadBufferSlot> m_VectorFieldVertexBufferSlots{};

        // GRAPHICS-078 Slice C — independent per-lane buffer lease for
        // the isoline lane. Grows independently of the vector-field
        // lane and is reset before the `BufferManager` in the
        // renderer's `Shutdown()` (via `m_VisualizationOverlayUploadHelper.reset()`
        // before `m_BufferManager.reset()`).
        std::vector<UploadBufferSlot> m_IsolineVertexBufferSlots{};

        std::uint32_t  m_ActiveSlot{0u};
        std::uint64_t  m_BufferAllocationCount{0u};
    };
}
