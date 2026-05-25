module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.VisualizationOverlayUploadHelper;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    namespace
    {
        // Per-vertex format consumed by all visualization-overlay shader
        // pairs (`assets/shaders/visualization_*.{vert,frag}`): packed
        // `position(vec3) + packed RGBA8 color(uint32)` (16 bytes
        // including padding) layout that the BDA-fetch vertex shaders
        // dereference via `gl_VertexIndex`. Identical to
        // `PackedDebugVertex` in `Graphics.TransientDebugUploadHelper.cpp`
        // by design so both upload helpers share a single shader vertex
        // contract.
        struct PackedOverlayVertex
        {
            float        Position[3];
            std::uint32_t PackedColor;
        };

        constexpr std::uint64_t kInitialVectorFieldVertexCount = 256u;
        constexpr std::uint64_t kMaxVectorFieldVertexCount = 1u << 18; // 262 144 verts (~4 MiB)

        [[nodiscard]] std::uint32_t PackUnorm4x8(const glm::vec4& color) noexcept
        {
            const glm::vec4 clamped = glm::clamp(color, glm::vec4{0.0f}, glm::vec4{1.0f});
            const std::uint32_t r = static_cast<std::uint32_t>(clamped.r * 255.0f + 0.5f);
            const std::uint32_t g = static_cast<std::uint32_t>(clamped.g * 255.0f + 0.5f);
            const std::uint32_t b = static_cast<std::uint32_t>(clamped.b * 255.0f + 0.5f);
            const std::uint32_t a = static_cast<std::uint32_t>(clamped.a * 255.0f + 0.5f);
            return r | (g << 8) | (b << 16) | (a << 24);
        }
    }

    VisualizationOverlayUploadHelper::VisualizationOverlayUploadHelper(RHI::IDevice& device,
                                                                       RHI::BufferManager& bufferManager)
        : m_Device(&device)
        , m_BufferManager(&bufferManager)
    {
    }

    VisualizationOverlayUploadHelper::~VisualizationOverlayUploadHelper() = default;

    void VisualizationOverlayUploadHelper::BeginFrame()
    {
        // Host-visible vertex buffers are reused across frames; per-
        // frame state is the upload result returned from each
        // `Upload*(...)` call (no allocator counters to clear here).
    }

    namespace
    {
        // Shared per-lane upload prologue: grows a host-visible vertex
        // buffer to fit `requestedVertexCount` packed vertices, copies
        // `staging` through `device.WriteBuffer(...)`, and returns the
        // device handle + BDA the executor records in push constants.
        //
        // Mirrors the `UploadPackedVertices(...)` shape from
        // `Graphics.TransientDebugUploadHelper.cpp` exactly so a future
        // refactor can hoist the shared prologue into a small support
        // module if both helpers end up colocated. Returns
        // `Overflow = true` when (a) the requested vertex count exceeds
        // the lane's cap, or (b) the underlying
        // `BufferManager::Create(...)` allocation fails.
        struct LaneUploadOutput
        {
            RHI::BufferHandle Handle{};
            std::uint64_t     BDA{0u};
            bool              Uploaded{false};
            bool              Overflow{false};
        };

        [[nodiscard]] LaneUploadOutput UploadPackedVertices(
            RHI::IDevice& device,
            RHI::BufferManager& bufferManager,
            std::optional<RHI::BufferManager::BufferLease>& bufferLease,
            std::uint64_t& capacityBytes,
            std::uint64_t& bufferAllocationCount,
            std::span<const PackedOverlayVertex> staging,
            std::uint64_t initialVertexCount,
            std::uint64_t maxVertexCount,
            const char* debugName)
        {
            LaneUploadOutput out{};
            const std::uint64_t requestedVertexCount = static_cast<std::uint64_t>(staging.size());
            if (requestedVertexCount > maxVertexCount)
            {
                out.Overflow = true;
                return out;
            }

            const std::uint64_t requestedBytes = requestedVertexCount * sizeof(PackedOverlayVertex);

            if (!bufferLease.has_value() || capacityBytes < requestedBytes)
            {
                std::uint64_t newVertexCapacity =
                    std::max<std::uint64_t>(initialVertexCount, requestedVertexCount);
                if (capacityBytes > 0u)
                {
                    const std::uint64_t previousVertexCapacity =
                        capacityBytes / sizeof(PackedOverlayVertex);
                    newVertexCapacity = std::max(newVertexCapacity, previousVertexCapacity * 2u);
                }
                newVertexCapacity = std::min(newVertexCapacity, maxVertexCount);
                const std::uint64_t newCapacityBytes = newVertexCapacity * sizeof(PackedOverlayVertex);

                bufferLease.reset();
                capacityBytes = 0u;

                RHI::BufferDesc desc{};
                desc.SizeBytes = newCapacityBytes;
                // GRAPHICS-078 — `BufferUsage::Storage` is required
                // alongside `Vertex` + `TransferDst` because the
                // `visualization_*.vert` shaders fetch per-vertex
                // data via BDA (push-constant carries the buffer-
                // device address); only buffers created with
                // `Storage` participate in BDA per the
                // `RHI.Device.cppm` `GetBufferDeviceAddress` contract.
                desc.Usage = RHI::BufferUsage::Vertex |
                             RHI::BufferUsage::Storage |
                             RHI::BufferUsage::TransferDst;
                desc.HostVisible = true;
                desc.DebugName = debugName;

                auto lease = bufferManager.Create(desc);
                if (!lease.has_value())
                {
                    out.Overflow = true;
                    return out;
                }

                bufferLease.emplace(std::move(*lease));
                capacityBytes = newCapacityBytes;
                ++bufferAllocationCount;
            }

            const RHI::BufferHandle handle = bufferLease->GetHandle();
            device.WriteBuffer(handle, staging.data(),
                               static_cast<std::uint64_t>(staging.size() * sizeof(PackedOverlayVertex)),
                               0u);

            out.Handle = handle;
            out.BDA = device.GetBufferDeviceAddress(handle);
            out.Uploaded = true;
            return out;
        }
    }

    VisualizationVectorFieldUploadResult VisualizationOverlayUploadHelper::UploadVectorFields(
        const std::span<const VectorFieldOverlayPacket> vectorFields)
    {
        VisualizationVectorFieldUploadResult result{};
        result.PacketCount = static_cast<std::uint32_t>(vectorFields.size());

        if (vectorFields.empty() || m_Device == nullptr || m_BufferManager == nullptr ||
            !m_Device->IsOperational())
        {
            return result;
        }

        // Two vertices per glyph (anchor + tip). Per-packet vertex
        // count is `2 * ElementCount`; the helper packs all packets'
        // glyph endpoints contiguously so the pass can issue
        // `Draw(2 * ElementCount, 1, 0, 0)` per packet with
        // `FirstVertex = cumulative endpoint offset` carried in the
        // push block.
        std::uint64_t totalEndpointCount = 0u;
        for (const VectorFieldOverlayPacket& packet : vectorFields)
        {
            totalEndpointCount += static_cast<std::uint64_t>(packet.ElementCount) * 2u;
        }

        if (totalEndpointCount == 0u)
        {
            return result;
        }

        // Fail-close BEFORE allocating staging when a packet (or the
        // accumulated lane payload) would push the helper past
        // `kMaxVectorFieldVertexCount`. The cap check inside
        // `UploadPackedVertices(...)` would otherwise run only after
        // the `std::vector<PackedOverlayVertex>` allocation, so a
        // single `ElementCount = UINT32_MAX` packet (or any
        // accumulated payload above the cap) could throw `bad_alloc`
        // — or attempt a multi-GiB host allocation — before the
        // overflow gate fires. Mirror the
        // `UploadPackedVertices(...)` overflow signaling so the
        // executor records `Overflow = true` and the pass reports
        // `SkippedUnavailable` deterministically.
        if (totalEndpointCount > kMaxVectorFieldVertexCount)
        {
            result.Overflow = true;
            return result;
        }

        std::vector<PackedOverlayVertex> staging(static_cast<std::size_t>(totalEndpointCount));
        std::size_t writeIndex = 0;
        for (const VectorFieldOverlayPacket& packet : vectorFields)
        {
            const std::uint32_t packedColor = PackUnorm4x8(packet.Color);
            const std::uint64_t endpointCount =
                static_cast<std::uint64_t>(packet.ElementCount) * 2u;
            for (std::uint64_t endpoint = 0; endpoint < endpointCount; ++endpoint)
            {
                // CPU/null path: the helper does not have CPU access
                // to `packet.PositionBufferBDA` / `VectorBufferBDA`
                // (those are GPU pointers), so the packed-vertex
                // position stays zero. The Vulkan-tuned variant
                // (Slice D) substitutes a backend-locally-resolved
                // anchor + scaled tip per glyph. Per-packet color
                // packing is identical across both paths.
                PackedOverlayVertex& vertex = staging[writeIndex++];
                vertex.Position[0] = 0.0f;
                vertex.Position[1] = 0.0f;
                vertex.Position[2] = 0.0f;
                vertex.PackedColor = packedColor;
            }
        }

        const LaneUploadOutput laneOutput = UploadPackedVertices(
            *m_Device,
            *m_BufferManager,
            m_VectorFieldVertexBuffer,
            m_VectorFieldVertexBufferCapacityBytes,
            m_BufferAllocationCount,
            std::span<const PackedOverlayVertex>{staging.data(), staging.size()},
            kInitialVectorFieldVertexCount,
            kMaxVectorFieldVertexCount,
            "VisualizationOverlay.VectorFieldVertices");
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
