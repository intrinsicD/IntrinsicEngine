module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.TransientDebugUploadHelper;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    namespace
    {
        // Per-vertex format consumed by all three transient-debug shader
        // pairs (`assets/shaders/transient_debug_{triangle,line,point}.vert`):
        // a packed `position(vec3) + packed RGBA8 color(uint32)` (16 bytes
        // including padding) layout that the BDA-fetch vertex shaders
        // dereference via `gl_VertexIndex`. Kept small so the host-
        // visible vertex buffers' per-frame WriteBuffer cost is minimal
        // even with hundreds of debug primitives per lane.
        struct PackedDebugVertex
        {
            float        Position[3];
            std::uint32_t PackedColor;
        };

        constexpr std::uint64_t kInitialTriangleVertexCount = 256u;
        constexpr std::uint64_t kMaxTriangleVertexCount = 1u << 18; // 262 144 verts (~4 MiB)
        // GRAPHICS-077 Slice C — line + point lanes share the per-
        // vertex format and the geometric-growth shape. Caps are
        // independent per lane so a heavy line submission does not
        // squeeze the point lane's allocation policy and vice versa.
        constexpr std::uint64_t kInitialLineVertexCount = 256u;
        constexpr std::uint64_t kMaxLineVertexCount = 1u << 18;
        constexpr std::uint64_t kInitialPointVertexCount = 256u;
        constexpr std::uint64_t kMaxPointVertexCount = 1u << 18;

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

    TransientDebugUploadHelper::TransientDebugUploadHelper(RHI::IDevice& device,
                                                           RHI::BufferManager& bufferManager)
        : m_Device(&device)
        , m_BufferManager(&bufferManager)
    {
    }

    TransientDebugUploadHelper::~TransientDebugUploadHelper() = default;

    void TransientDebugUploadHelper::EnsureFrameSlots(const std::uint32_t framesInFlight)
    {
        const std::uint32_t slotCount = framesInFlight == 0u ? 1u : framesInFlight;
        if (m_TriangleVertexBufferSlots.size() != slotCount)
        {
            m_TriangleVertexBufferSlots.resize(slotCount);
        }
        if (m_LineVertexBufferSlots.size() != slotCount)
        {
            m_LineVertexBufferSlots.resize(slotCount);
        }
        if (m_PointVertexBufferSlots.size() != slotCount)
        {
            m_PointVertexBufferSlots.resize(slotCount);
        }
        if (m_ActiveSlot >= slotCount)
        {
            m_ActiveSlot = 0u;
        }
    }

    void TransientDebugUploadHelper::BeginFrame(const std::uint32_t frameIndex,
                                                const std::uint32_t framesInFlight)
    {
        EnsureFrameSlots(framesInFlight);
        const std::uint32_t slotCount =
            static_cast<std::uint32_t>(m_TriangleVertexBufferSlots.empty()
                ? 1u
                : m_TriangleVertexBufferSlots.size());
        m_ActiveSlot = slotCount == 0u ? 0u : (frameIndex % slotCount);
    }

    namespace
    {
        // Shared per-lane upload prologue: grows a host-visible vertex
        // buffer to fit `requestedVertexCount` packed vertices, copies
        // `staging` through `device.WriteBuffer(...)`, and returns the
        // device handle + BDA the executor records in push constants.
        //
        // Returns `Overflow = true` when (a) the requested vertex count
        // exceeds the lane's cap, or (b) the underlying
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
            std::span<const PackedDebugVertex> staging,
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

            const std::uint64_t requestedBytes = requestedVertexCount * sizeof(PackedDebugVertex);

            if (!bufferLease.has_value() || capacityBytes < requestedBytes)
            {
                std::uint64_t newVertexCapacity =
                    std::max<std::uint64_t>(initialVertexCount, requestedVertexCount);
                if (capacityBytes > 0u)
                {
                    const std::uint64_t previousVertexCapacity =
                        capacityBytes / sizeof(PackedDebugVertex);
                    newVertexCapacity = std::max(newVertexCapacity, previousVertexCapacity * 2u);
                }
                newVertexCapacity = std::min(newVertexCapacity, maxVertexCount);
                const std::uint64_t newCapacityBytes = newVertexCapacity * sizeof(PackedDebugVertex);

                bufferLease.reset();
                capacityBytes = 0u;

                RHI::BufferDesc desc{};
                desc.SizeBytes = newCapacityBytes;
                // GRAPHICS-077 — `BufferUsage::Storage` is required
                // alongside `Vertex` + `TransferDst` because the
                // `transient_debug_*.vert` shaders fetch per-vertex
                // data via BDA (push-constant carries the buffer-
                // device address); per `RHI.Device.cppm`
                // `GetBufferDeviceAddress` contract and the Vulkan
                // backend's `Backends.Vulkan.Device.cpp` `HasBDA`
                // gate, only buffers created with `Storage`
                // participate in BDA.
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
                               static_cast<std::uint64_t>(staging.size() * sizeof(PackedDebugVertex)),
                               0u);

            out.Handle = handle;
            out.BDA = device.GetBufferDeviceAddress(handle);
            out.Uploaded = true;
            return out;
        }
    }

    TransientDebugTriangleUploadResult TransientDebugUploadHelper::UploadTriangles(
        const std::span<const DebugTrianglePacket> triangles)
    {
        TransientDebugTriangleUploadResult result{};
        result.PacketCount = static_cast<std::uint32_t>(triangles.size());

        if (triangles.empty() || !m_Device->IsOperational())
        {
            return result;
        }
        if (m_TriangleVertexBufferSlots.empty())
        {
            EnsureFrameSlots(1u);
        }

        const std::uint64_t requestedVertexCount = static_cast<std::uint64_t>(triangles.size()) * 3u;
        std::vector<PackedDebugVertex> staging(static_cast<std::size_t>(requestedVertexCount));
        for (std::size_t packetIndex = 0; packetIndex < triangles.size(); ++packetIndex)
        {
            const DebugTrianglePacket& triangle = triangles[packetIndex];
            const std::uint32_t packedColor = PackUnorm4x8(triangle.Color);
            const glm::vec3 corners[3] = {triangle.A, triangle.B, triangle.C};
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                PackedDebugVertex& vertex = staging[packetIndex * 3 + corner];
                vertex.Position[0] = corners[corner].x;
                vertex.Position[1] = corners[corner].y;
                vertex.Position[2] = corners[corner].z;
                vertex.PackedColor = packedColor;
            }
        }

        UploadBufferSlot& slot = m_TriangleVertexBufferSlots[m_ActiveSlot];
        const LaneUploadOutput laneOutput = UploadPackedVertices(
            *m_Device,
            *m_BufferManager,
            slot.Buffer,
            slot.CapacityBytes,
            m_BufferAllocationCount,
            std::span<const PackedDebugVertex>{staging.data(), staging.size()},
            kInitialTriangleVertexCount,
            kMaxTriangleVertexCount,
            "TransientDebug.TriangleVertices");
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
        result.VertexCount = static_cast<std::uint32_t>(requestedVertexCount);
        result.Uploaded = true;
        return result;
    }

    TransientDebugLineUploadResult TransientDebugUploadHelper::UploadLines(
        const std::span<const DebugLinePacket> lines)
    {
        TransientDebugLineUploadResult result{};
        result.PacketCount = static_cast<std::uint32_t>(lines.size());

        if (lines.empty() || !m_Device->IsOperational())
        {
            return result;
        }
        if (m_LineVertexBufferSlots.empty())
        {
            EnsureFrameSlots(1u);
        }

        // Two vertices per packet (Start, End). The pass records
        // `Draw(2, 1, 0, 0)` per packet with `FirstVertex = packetIndex
        // * 2` carried in the push block; the BDA-fetch vertex shader
        // resolves `vbuf.v[FirstVertex + gl_VertexIndex]` per draw.
        const std::uint64_t requestedVertexCount = static_cast<std::uint64_t>(lines.size()) * 2u;
        std::vector<PackedDebugVertex> staging(static_cast<std::size_t>(requestedVertexCount));
        for (std::size_t packetIndex = 0; packetIndex < lines.size(); ++packetIndex)
        {
            const DebugLinePacket& line = lines[packetIndex];
            const std::uint32_t packedColor = PackUnorm4x8(line.Color);
            const glm::vec3 endpoints[2] = {line.Start, line.End};
            for (std::size_t endpoint = 0; endpoint < 2; ++endpoint)
            {
                PackedDebugVertex& vertex = staging[packetIndex * 2 + endpoint];
                vertex.Position[0] = endpoints[endpoint].x;
                vertex.Position[1] = endpoints[endpoint].y;
                vertex.Position[2] = endpoints[endpoint].z;
                vertex.PackedColor = packedColor;
            }
        }

        UploadBufferSlot& slot = m_LineVertexBufferSlots[m_ActiveSlot];
        const LaneUploadOutput laneOutput = UploadPackedVertices(
            *m_Device,
            *m_BufferManager,
            slot.Buffer,
            slot.CapacityBytes,
            m_BufferAllocationCount,
            std::span<const PackedDebugVertex>{staging.data(), staging.size()},
            kInitialLineVertexCount,
            kMaxLineVertexCount,
            "TransientDebug.LineVertices");
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
        result.VertexCount = static_cast<std::uint32_t>(requestedVertexCount);
        result.Uploaded = true;
        return result;
    }

    TransientDebugPointUploadResult TransientDebugUploadHelper::UploadPoints(
        const std::span<const DebugPointPacket> points)
    {
        TransientDebugPointUploadResult result{};
        result.PacketCount = static_cast<std::uint32_t>(points.size());

        if (points.empty() || !m_Device->IsOperational())
        {
            return result;
        }
        if (m_PointVertexBufferSlots.empty())
        {
            EnsureFrameSlots(1u);
        }

        // One vertex per packet. The pass records `Draw(1, 1, 0, 0)`
        // per packet with `FirstVertex = packetIndex` carried in the
        // push block.
        const std::uint64_t requestedVertexCount = static_cast<std::uint64_t>(points.size());
        std::vector<PackedDebugVertex> staging(static_cast<std::size_t>(requestedVertexCount));
        for (std::size_t packetIndex = 0; packetIndex < points.size(); ++packetIndex)
        {
            const DebugPointPacket& point = points[packetIndex];
            const std::uint32_t packedColor = PackUnorm4x8(point.Color);
            PackedDebugVertex& vertex = staging[packetIndex];
            vertex.Position[0] = point.Position.x;
            vertex.Position[1] = point.Position.y;
            vertex.Position[2] = point.Position.z;
            vertex.PackedColor = packedColor;
        }

        UploadBufferSlot& slot = m_PointVertexBufferSlots[m_ActiveSlot];
        const LaneUploadOutput laneOutput = UploadPackedVertices(
            *m_Device,
            *m_BufferManager,
            slot.Buffer,
            slot.CapacityBytes,
            m_BufferAllocationCount,
            std::span<const PackedDebugVertex>{staging.data(), staging.size()},
            kInitialPointVertexCount,
            kMaxPointVertexCount,
            "TransientDebug.PointVertices");
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
        result.VertexCount = static_cast<std::uint32_t>(requestedVertexCount);
        result.Uploaded = true;
        return result;
    }
}
