module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

module Extrinsic.Graphics.ImGuiUploadHelper;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint64_t kInitialVertexCount = 1024u;
        constexpr std::uint64_t kInitialIndexCount = 2048u;
        constexpr std::uint64_t kMaxVertexCount = 1u << 20;
        constexpr std::uint64_t kMaxIndexCount = 1u << 21;

        struct LaneUploadOutput
        {
            RHI::BufferHandle Handle{};
            std::uint64_t BDA{0u};
            bool Uploaded{false};
            bool Overflow{false};
        };

        [[nodiscard]] bool DrawCommandRangeValid(const ImGuiOverlayDrawList& drawList,
                                                 const ImGuiOverlayDrawCommand& command) noexcept
        {
            return command.IndexCount > 0u &&
                   command.IndexOffset <= drawList.IndexCount &&
                   command.IndexCount <= drawList.IndexCount - command.IndexOffset &&
                   command.VertexOffset < drawList.VertexCount;
        }

        [[nodiscard]] std::vector<ImGuiDrawCommandUploadResult> BuildCommandUploads(
            const ImGuiOverlayDrawList& drawList)
        {
            std::vector<ImGuiDrawCommandUploadResult> commands;
            if (drawList.Commands.empty())
            {
                commands.push_back(ImGuiDrawCommandUploadResult{
                    .IndexOffset = 0u,
                    .VertexOffset = 0u,
                    .IndexCount = drawList.IndexCount,
                    .TextureBindlessIndex = RHI::kInvalidBindlessIndex,
                    .UsesUserTexture = drawList.UsesUserTexture,
                });
                return commands;
            }

            commands.reserve(drawList.Commands.size());
            for (const ImGuiOverlayDrawCommand& command : drawList.Commands)
            {
                if (!DrawCommandRangeValid(drawList, command))
                {
                    return {};
                }
                commands.push_back(ImGuiDrawCommandUploadResult{
                    .IndexOffset = command.IndexOffset,
                    .VertexOffset = command.VertexOffset,
                    .IndexCount = command.IndexCount,
                    .TextureBindlessIndex = command.TextureBindlessIndex,
                    .UsesUserTexture = command.UsesUserTexture,
                });
            }
            return commands;
        }

        [[nodiscard]] LaneUploadOutput UploadBytes(
            RHI::IDevice& device,
            RHI::BufferManager& bufferManager,
            std::optional<RHI::BufferManager::BufferLease>& lease,
            std::uint64_t& capacityBytes,
            std::uint64_t& allocationCount,
            const void* data,
            const std::uint64_t requestedBytes,
            const std::uint64_t initialBytes,
            const std::uint64_t maxBytes,
            const RHI::BufferUsage usage,
            const char* debugName)
        {
            LaneUploadOutput out{};
            if (requestedBytes == 0u)
            {
                return out;
            }
            if (requestedBytes > maxBytes)
            {
                out.Overflow = true;
                return out;
            }

            if (!lease.has_value() || capacityBytes < requestedBytes)
            {
                std::uint64_t newCapacityBytes =
                    std::max<std::uint64_t>(initialBytes, requestedBytes);
                if (capacityBytes > 0u)
                {
                    newCapacityBytes = std::max(newCapacityBytes, capacityBytes * 2u);
                }
                newCapacityBytes = std::min(newCapacityBytes, maxBytes);

                lease.reset();
                capacityBytes = 0u;

                RHI::BufferDesc desc{};
                desc.SizeBytes = newCapacityBytes;
                desc.Usage = usage;
                desc.HostVisible = true;
                desc.DebugName = debugName;

                auto buffer = bufferManager.Create(desc);
                if (!buffer.has_value())
                {
                    out.Overflow = true;
                    return out;
                }

                lease.emplace(std::move(*buffer));
                capacityBytes = newCapacityBytes;
                ++allocationCount;
            }

            const RHI::BufferHandle handle = lease->GetHandle();
            device.WriteBuffer(handle, data, requestedBytes, 0u);

            out.Handle = handle;
            out.BDA = device.GetBufferDeviceAddress(handle);
            out.Uploaded = true;
            return out;
        }
    }

    ImGuiUploadHelper::ImGuiUploadHelper(RHI::IDevice& device,
                                         RHI::BufferManager& bufferManager)
        : m_Device(&device)
        , m_BufferManager(&bufferManager)
    {}

    ImGuiUploadHelper::~ImGuiUploadHelper() = default;

    void ImGuiUploadHelper::BeginFrame()
    {
        // Buffers are reused across frames; per-frame state is returned from
        // `UploadFrame(...)`.
    }

    ImGuiUploadResult ImGuiUploadHelper::UploadFrame(const ImGuiOverlayFrame& frame)
    {
        ImGuiUploadResult result{};
        result.DrawListCount = static_cast<std::uint32_t>(frame.DrawLists.size());

        if (!frame.Enabled || frame.DrawLists.empty() || m_Device == nullptr ||
            m_BufferManager == nullptr || !m_Device->IsOperational())
        {
            return result;
        }

        std::uint64_t totalVertexCount = 0u;
        std::uint64_t totalIndexCount = 0u;
        for (const ImGuiOverlayDrawList& drawList : frame.DrawLists)
        {
            if (drawList.Vertices.size() != drawList.VertexCount ||
                drawList.Indices.size() != drawList.IndexCount ||
                BuildCommandUploads(drawList).empty())
            {
                return result;
            }
            totalVertexCount += static_cast<std::uint64_t>(drawList.VertexCount);
            totalIndexCount += static_cast<std::uint64_t>(drawList.IndexCount);
        }

        if (totalVertexCount == 0u || totalIndexCount == 0u)
        {
            return result;
        }
        if (totalVertexCount > kMaxVertexCount || totalIndexCount > kMaxIndexCount)
        {
            result.Overflow = true;
            return result;
        }

        std::vector<ImGuiOverlayVertex> vertices;
        vertices.reserve(static_cast<std::size_t>(totalVertexCount));
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(totalIndexCount));
        result.DrawLists.reserve(frame.DrawLists.size());

        std::uint32_t firstVertex = 0u;
        std::uint64_t indexOffsetBytes = 0u;
        for (const ImGuiOverlayDrawList& drawList : frame.DrawLists)
        {
            ImGuiDrawListUploadResult listResult{};
            listResult.FirstVertex = firstVertex;
            listResult.IndexOffsetBytes = indexOffsetBytes;
            listResult.VertexCount = drawList.VertexCount;
            listResult.IndexCount = drawList.IndexCount;
            listResult.Commands = BuildCommandUploads(drawList);
            if (listResult.Commands.empty())
            {
                return ImGuiUploadResult{};
            }

            vertices.insert(vertices.end(), drawList.Vertices.begin(), drawList.Vertices.end());
            indices.insert(indices.end(), drawList.Indices.begin(), drawList.Indices.end());
            result.DrawLists.push_back(listResult);

            firstVertex += drawList.VertexCount;
            indexOffsetBytes += static_cast<std::uint64_t>(drawList.IndexCount) *
                                sizeof(std::uint32_t);
        }

        const LaneUploadOutput vertexUpload = UploadBytes(
            *m_Device,
            *m_BufferManager,
            m_VertexBuffer,
            m_VertexBufferCapacityBytes,
            m_BufferAllocationCount,
            vertices.data(),
            static_cast<std::uint64_t>(vertices.size() * sizeof(ImGuiOverlayVertex)),
            kInitialVertexCount * sizeof(ImGuiOverlayVertex),
            kMaxVertexCount * sizeof(ImGuiOverlayVertex),
            RHI::BufferUsage::Vertex | RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
            "ImGui.TransientVertices");
        if (vertexUpload.Overflow || !vertexUpload.Uploaded)
        {
            result.Overflow = vertexUpload.Overflow;
            return result;
        }

        const LaneUploadOutput indexUpload = UploadBytes(
            *m_Device,
            *m_BufferManager,
            m_IndexBuffer,
            m_IndexBufferCapacityBytes,
            m_BufferAllocationCount,
            indices.data(),
            static_cast<std::uint64_t>(indices.size() * sizeof(std::uint32_t)),
            kInitialIndexCount * sizeof(std::uint32_t),
            kMaxIndexCount * sizeof(std::uint32_t),
            RHI::BufferUsage::Index | RHI::BufferUsage::TransferDst,
            "ImGui.TransientIndices");
        if (indexUpload.Overflow || !indexUpload.Uploaded)
        {
            result.Overflow = indexUpload.Overflow;
            return result;
        }

        for (ImGuiDrawListUploadResult& drawList : result.DrawLists)
        {
            drawList.VertexBuffer = vertexUpload.Handle;
            drawList.VertexBufferBDA = vertexUpload.BDA;
            drawList.IndexBuffer = indexUpload.Handle;
            drawList.Uploaded = true;
        }

        result.Uploaded = !result.DrawLists.empty();
        return result;
    }
}
