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
        // Per-vertex format consumed by `assets/shaders/transient_debug_triangle.vert`:
        // a packed `position(vec3) + packed RGBA8 color(uint32)` (16 bytes
        // including padding) layout that the BDA-fetch vertex shader
        // dereferences via `gl_VertexIndex`. Kept small so the host-
        // visible vertex buffer's per-frame WriteBuffer cost is minimal
        // even with hundreds of debug triangles.
        struct PackedTriangleVertex
        {
            float        Position[3];
            std::uint32_t PackedColor;
        };

        constexpr std::uint64_t kInitialTriangleVertexCount = 256u;
        constexpr std::uint64_t kMaxTriangleVertexCount = 1u << 18; // 262 144 verts (~4 MiB)

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

    void TransientDebugUploadHelper::BeginFrame()
    {
        // Host-visible vertex buffer is reused across frames; per-frame
        // state is the upload result returned from `UploadTriangles(...)`
        // (no allocator counters to clear here in Slice B). Slice C
        // extends this to the line + point lanes.
    }

    TransientDebugTriangleUploadResult TransientDebugUploadHelper::UploadTriangles(
        const std::span<const DebugTrianglePacket> triangles)
    {
        TransientDebugTriangleUploadResult result{};
        result.PacketCount = static_cast<std::uint32_t>(triangles.size());

        if (triangles.empty() || m_Device == nullptr || m_BufferManager == nullptr ||
            !m_Device->IsOperational())
        {
            return result;
        }

        const std::uint64_t requestedVertexCount = static_cast<std::uint64_t>(triangles.size()) * 3u;
        if (requestedVertexCount > kMaxTriangleVertexCount)
        {
            result.Overflow = true;
            return result;
        }

        const std::uint64_t requestedBytes = requestedVertexCount * sizeof(PackedTriangleVertex);

        if (!m_TriangleVertexBuffer.has_value() ||
            m_TriangleVertexBufferCapacityBytes < requestedBytes)
        {
            // Geometric growth: double until the capacity covers the
            // requested vertex count, capped to `kMaxTriangleVertexCount`.
            std::uint64_t newVertexCapacity =
                std::max<std::uint64_t>(kInitialTriangleVertexCount, requestedVertexCount);
            if (m_TriangleVertexBufferCapacityBytes > 0u)
            {
                const std::uint64_t previousVertexCapacity =
                    m_TriangleVertexBufferCapacityBytes / sizeof(PackedTriangleVertex);
                newVertexCapacity = std::max(newVertexCapacity, previousVertexCapacity * 2u);
            }
            newVertexCapacity = std::min(newVertexCapacity, kMaxTriangleVertexCount);
            const std::uint64_t newCapacityBytes = newVertexCapacity * sizeof(PackedTriangleVertex);

            // Reset the existing lease so the previous buffer is
            // returned to the device before the new allocation happens
            // (avoids momentarily holding two allocations).
            m_TriangleVertexBuffer.reset();
            m_TriangleVertexBufferCapacityBytes = 0u;

            RHI::BufferDesc desc{};
            desc.SizeBytes = newCapacityBytes;
            desc.Usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::TransferDst;
            desc.HostVisible = true;
            desc.DebugName = "TransientDebug.TriangleVertices";

            auto lease = m_BufferManager->Create(desc);
            if (!lease.has_value())
            {
                result.Overflow = true;
                return result;
            }

            m_TriangleVertexBuffer.emplace(std::move(*lease));
            m_TriangleVertexBufferCapacityBytes = newCapacityBytes;
            ++m_BufferAllocationCount;
        }

        // Pack `requestedVertexCount` vertices into a CPU staging vector
        // and submit them via `IDevice::WriteBuffer(...)`. Each triangle
        // packet contributes three vertices in source order so the
        // per-packet `Draw(3, 1, firstVertex=packetIndex*3, 0)` shape
        // the executor records matches what the shader fetches via BDA.
        std::vector<PackedTriangleVertex> staging(static_cast<std::size_t>(requestedVertexCount));
        for (std::size_t packetIndex = 0; packetIndex < triangles.size(); ++packetIndex)
        {
            const DebugTrianglePacket& triangle = triangles[packetIndex];
            const std::uint32_t packedColor = PackUnorm4x8(triangle.Color);
            const glm::vec3 corners[3] = {triangle.A, triangle.B, triangle.C};
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                PackedTriangleVertex& vertex = staging[packetIndex * 3 + corner];
                vertex.Position[0] = corners[corner].x;
                vertex.Position[1] = corners[corner].y;
                vertex.Position[2] = corners[corner].z;
                vertex.PackedColor = packedColor;
            }
        }

        const RHI::BufferHandle handle = m_TriangleVertexBuffer->GetHandle();
        m_Device->WriteBuffer(handle, staging.data(),
                              static_cast<std::uint64_t>(staging.size() * sizeof(PackedTriangleVertex)),
                              0u);

        result.VertexBuffer = handle;
        result.VertexBufferBDA = m_Device->GetBufferDeviceAddress(handle);
        result.VertexCount = static_cast<std::uint32_t>(requestedVertexCount);
        result.Uploaded = true;
        return result;
    }
}
