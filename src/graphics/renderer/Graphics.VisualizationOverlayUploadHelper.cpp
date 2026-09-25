module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.VisualizationOverlayUploadHelper;

import Extrinsic.RHI.Device;

import Extrinsic.Graphics.TransientDebugUploadHelper;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint64_t kInitialVectorFieldRecordCount = 16u;
        constexpr std::uint64_t kMaxVectorFieldRecordCount = 1u << 14; // 16 384 fields (2 MiB)
        // GRAPHICS-078 Slice C — isoline lane shares the per-vertex
        // format and the geometric-growth shape. Caps are independent
        // per lane so a heavy isoline submission does not squeeze the
        // vector-field lane's allocation policy and vice versa,
        // mirroring the GRAPHICS-077 Slice C triangle/line/point per-
        // lane independence.
        constexpr std::uint64_t kInitialIsolineVertexCount = 256u;
        constexpr std::uint64_t kMaxIsolineVertexCount = 1u << 18;


        struct OverlayFixtureSegment
        {
            glm::vec3 Start{0.0f};
            glm::vec3 End{0.0f};
        };

        [[nodiscard]] constexpr float FixturePixelCenterToNdcX(const std::uint32_t pixelX) noexcept
        {
            return ((static_cast<float>(pixelX) + 0.5f) / 256.0f) * 2.0f - 1.0f;
        }

        [[nodiscard]] constexpr float FixturePixelCenterToNdcY(const std::uint32_t pixelY) noexcept
        {
            return 1.0f - ((static_cast<float>(pixelY) + 0.5f) / 256.0f) * 2.0f;
        }

        [[nodiscard]] OverlayFixtureSegment MakeIsolineFixtureSegment(
            const std::uint64_t isoIndex) noexcept
        {
            const std::uint32_t lane = static_cast<std::uint32_t>(isoIndex % 4u);
            const float y = FixturePixelCenterToNdcY(176u + lane * 12u);
            return OverlayFixtureSegment{
                .Start = glm::vec3{FixturePixelCenterToNdcX(160u), y, 0.0f},
                .End   = glm::vec3{FixturePixelCenterToNdcX(208u), y, 0.0f},
            };
        }
    }

    VisualizationOverlayUploadHelper::VisualizationOverlayUploadHelper(RHI::IDevice& device,
                                                                       RHI::BufferManager& bufferManager)
        : m_Device(&device)
        , m_BufferManager(&bufferManager)
    {
    }

    VisualizationOverlayUploadHelper::~VisualizationOverlayUploadHelper() = default;

    void VisualizationOverlayUploadHelper::EnsureFrameSlots(const std::uint32_t framesInFlight)
    {
        const std::uint32_t slotCount = framesInFlight == 0u ? 1u : framesInFlight;
        if (m_VectorFieldRecordBufferSlots.size() != slotCount)
        {
            m_VectorFieldRecordBufferSlots.resize(slotCount);
        }
        if (m_IsolineVertexBufferSlots.size() != slotCount)
        {
            m_IsolineVertexBufferSlots.resize(slotCount);
        }
        if (m_ActiveSlot >= slotCount)
        {
            m_ActiveSlot = 0u;
        }
    }

    void VisualizationOverlayUploadHelper::BeginFrame(const std::uint32_t frameIndex,
                                                      const std::uint32_t framesInFlight)
    {
        EnsureFrameSlots(framesInFlight);
        const std::uint32_t slotCount =
            static_cast<std::uint32_t>(m_VectorFieldRecordBufferSlots.empty()
                ? 1u
                : m_VectorFieldRecordBufferSlots.size());
        m_ActiveSlot = slotCount == 0u ? 0u : (frameIndex % slotCount);
    }


    VisualizationVectorFieldUploadResult VisualizationOverlayUploadHelper::UploadVectorFields(
        const std::span<const VectorFieldOverlayPacket> vectorFields)
    {
        VisualizationVectorFieldUploadResult result{};
        result.PacketCount = static_cast<std::uint32_t>(vectorFields.size());
        result.RecordIndexForPacket.assign(
            vectorFields.size(), VisualizationVectorFieldUploadResult::kInvalidRecord);

        if (vectorFields.empty() || !m_Device->IsOperational())
        {
            return result;
        }
        if (m_VectorFieldRecordBufferSlots.empty())
        {
            EnsureFrameSlots(1u);
        }

        std::vector<VisualizationVectorFieldDrawRecord> records;
        records.reserve(vectorFields.size());
        for (std::size_t packetIndex = 0; packetIndex < vectorFields.size(); ++packetIndex)
        {
            const VectorFieldOverlayPacket& packet = vectorFields[packetIndex];
            if (!IsRenderableVectorFieldPacket(packet))
            {
                continue;
            }
            result.RecordIndexForPacket[packetIndex] =
                static_cast<std::uint32_t>(records.size());
            records.push_back(VisualizationVectorFieldDrawRecord{
                .ObjectToWorld = packet.ObjectToWorld,
                .PositionBufferBDA = packet.PositionBufferBDA,
                .VectorBufferBDA = packet.VectorBufferBDA,
                .RowBufferBDA = packet.RowBufferBDA,
                .ElementCount = packet.ElementCount,
                .RowCount = packet.RowCount,
                .RowStride = packet.RowStride,
                .PackedColor = PackVertexColorUnorm4x8(packet.Color),
                .Scale = packet.Scale,
                .LineWidthPx = packet.LineWidthPx,
                .Flags = packet.NormalizeLength ? kVisualizationVectorFieldNormalizeFlag : 0u,
            });
        }
        if (records.empty())
        {
            return result;
        }

        UploadBufferSlot& slot = m_VectorFieldRecordBufferSlots[m_ActiveSlot];
        const PackedVertexUploadResult laneOutput = UploadRetainedHostBytes(
            *m_Device,
            *m_BufferManager,
            slot.Buffer,
            slot.CapacityBytes,
            m_BufferAllocationCount,
            std::as_bytes(std::span<const VisualizationVectorFieldDrawRecord>{records}),
            sizeof(VisualizationVectorFieldDrawRecord),
            kInitialVectorFieldRecordCount,
            kMaxVectorFieldRecordCount,
            "VisualizationOverlay.VectorFieldRecords");
        if (laneOutput.Overflow || !laneOutput.Uploaded)
        {
            result.Overflow = laneOutput.Overflow;
            std::fill(result.RecordIndexForPacket.begin(),
                      result.RecordIndexForPacket.end(),
                      VisualizationVectorFieldUploadResult::kInvalidRecord);
            return result;
        }

        result.RecordBuffer = laneOutput.Handle;
        result.RecordBufferBDA = laneOutput.BDA;
        result.RecordCount = static_cast<std::uint32_t>(records.size());
        result.Uploaded = true;
        return result;
    }

    VisualizationIsolineUploadResult VisualizationOverlayUploadHelper::UploadIsolines(
        const std::span<const IsolineOverlayPacket> isolines)
    {
        VisualizationIsolineUploadResult result{};
        result.PacketCount = static_cast<std::uint32_t>(isolines.size());

        if (isolines.empty() || !m_Device->IsOperational())
        {
            return result;
        }
        if (m_IsolineVertexBufferSlots.empty())
        {
            EnsureFrameSlots(1u);
        }

        // CPU/null contract: each iso value contributes one deterministic
        // placeholder `LineList` segment (two packed vertices) per packet so
        // the pass can issue `Draw(2 * IsoValueCount, 1, 0, 0)` per packet.
        // Actual scalar-field-derived polyline expansion remains future
        // source-BDA work.
        std::uint64_t totalEndpointCount = 0u;
        for (const IsolineOverlayPacket& packet : isolines)
        {
            totalEndpointCount += static_cast<std::uint64_t>(packet.IsoValueCount) * 2u;
        }

        if (totalEndpointCount == 0u)
        {
            return result;
        }

        // Fail-close BEFORE allocating staging when a packet (or the
        // accumulated lane payload) would push the helper past
        // `kMaxIsolineVertexCount`. Mirrors the vector-field overflow
        // gate so an adversarial `IsoValueCount = UINT32_MAX` (or any
        // accumulated payload above the cap) cannot trigger a multi-
        // GiB host allocation or throw `bad_alloc` before the overflow
        // gate fires.
        if (totalEndpointCount > kMaxIsolineVertexCount)
        {
            result.Overflow = true;
            return result;
        }

        std::vector<PackedColorVertex> staging(static_cast<std::size_t>(totalEndpointCount));
        std::size_t writeIndex = 0;
        for (const IsolineOverlayPacket& packet : isolines)
        {
            const std::uint32_t packedColor = PackVertexColorUnorm4x8(packet.Color);
            const std::uint64_t endpointCount =
                static_cast<std::uint64_t>(packet.IsoValueCount) * 2u;
            for (std::uint64_t endpoint = 0; endpoint < endpointCount; ++endpoint)
            {
                // CPU/null path: the helper does not have CPU access
                // to the source scalar field (its values + topology
                // are GPU-side), so GRAPHICS-078E emits deterministic
                // placeholder iso-line segments instead of claiming
                // source-buffer parity. A future source-BDA expansion
                // can replace only this fixture-position calculation
                // while preserving per-packet color packing and draw
                // shape.
                const OverlayFixtureSegment segment =
                    MakeIsolineFixtureSegment(endpoint / 2u);
                const glm::vec3 position = (endpoint % 2u == 0u) ? segment.Start : segment.End;
                PackedColorVertex& vertex = staging[writeIndex++];
                vertex.Position[0] = position.x;
                vertex.Position[1] = position.y;
                vertex.Position[2] = position.z;
                vertex.PackedColor = packedColor;
            }
        }

        UploadBufferSlot& slot = m_IsolineVertexBufferSlots[m_ActiveSlot];
        const PackedVertexUploadResult laneOutput = UploadPackedColorVertices(
            *m_Device,
            *m_BufferManager,
            slot.Buffer,
            slot.CapacityBytes,
            m_BufferAllocationCount,
            std::span<const PackedColorVertex>{staging.data(), staging.size()},
            kInitialIsolineVertexCount,
            kMaxIsolineVertexCount,
            "VisualizationOverlay.IsolineVertices");
        if (laneOutput.Overflow)
        {
            result.Overflow = true;
            return result;
        }
        if (!laneOutput.Uploaded)
        {
            return result;
        }

        result.VertexBuffer = laneOutput.Handle;
        result.VertexBufferBDA = laneOutput.BDA;
        result.VertexCount = static_cast<std::uint32_t>(totalEndpointCount);
        result.Uploaded = true;
        return result;
    }
}
