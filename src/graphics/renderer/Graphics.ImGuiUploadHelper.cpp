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
                                                 const ImGuiOverlayDrawCommand& command,
                                                 const std::uint32_t displayWidth,
                                                 const std::uint32_t displayHeight) noexcept
        {
            return command.IndexCount > 0u &&
                   command.IndexOffset <= drawList.IndexCount &&
                   command.IndexCount <= drawList.IndexCount - command.IndexOffset &&
                   command.VertexOffset < drawList.VertexCount &&
                   !command.Scissor.IsEmpty() &&
                   command.Scissor.X >= 0 && command.Scissor.Y >= 0 &&
                   static_cast<std::uint64_t>(command.Scissor.X) +
                           command.Scissor.Width <=
                       displayWidth &&
                   static_cast<std::uint64_t>(command.Scissor.Y) +
                           command.Scissor.Height <=
                       displayHeight;
        }

        [[nodiscard]] std::vector<ImGuiDrawCommandUploadResult> BuildCommandUploads(
            const ImGuiOverlayDrawList& drawList,
            const std::uint32_t displayWidth,
            const std::uint32_t displayHeight)
        {
            std::vector<ImGuiDrawCommandUploadResult> commands;
            if (drawList.Commands.empty())
            {
                if (drawList.CommandCount == 0u ||
                    displayWidth == 0u || displayHeight == 0u)
                {
                    return commands;
                }
                commands.push_back(ImGuiDrawCommandUploadResult{
                    .IndexOffset = 0u,
                    .VertexOffset = 0u,
                    .IndexCount = drawList.IndexCount,
                    .TextureBindlessIndex = RHI::kInvalidBindlessIndex,
                    .UsesUserTexture = drawList.UsesUserTexture,
                    .Scissor = ImGuiOverlayScissor{
                        .X = 0,
                        .Y = 0,
                        .Width = displayWidth,
                        .Height = displayHeight,
                    },
                });
                return commands;
            }

            commands.reserve(drawList.Commands.size());
            for (const ImGuiOverlayDrawCommand& command : drawList.Commands)
            {
                if (!DrawCommandRangeValid(
                        drawList,
                        command,
                        displayWidth,
                        displayHeight))
                {
                    return {};
                }
                commands.push_back(ImGuiDrawCommandUploadResult{
                    .IndexOffset = command.IndexOffset,
                    .VertexOffset = command.VertexOffset,
                    .IndexCount = command.IndexCount,
                    .TextureBindlessIndex = command.TextureBindlessIndex,
                    .UsesUserTexture = command.UsesUserTexture,
                    .Scissor = command.Scissor,
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

    void ImGuiUploadHelper::EnsureFrameSlots(const std::uint32_t framesInFlight)
    {
        const std::uint32_t slotCount = framesInFlight == 0u ? 1u : framesInFlight;
        if (m_VertexBufferSlots.size() != slotCount)
        {
            m_VertexBufferSlots.resize(slotCount);
        }
        if (m_IndexBufferSlots.size() != slotCount)
        {
            m_IndexBufferSlots.resize(slotCount);
        }
        if (m_ActiveSlot >= slotCount)
        {
            m_ActiveSlot = 0u;
        }
    }

    void ImGuiUploadHelper::BeginFrame(const std::uint32_t frameIndex,
                                       const std::uint32_t framesInFlight)
    {
        EnsureFrameSlots(framesInFlight);
        const std::uint32_t slotCount =
            static_cast<std::uint32_t>(m_VertexBufferSlots.empty()
                ? 1u
                : m_VertexBufferSlots.size());
        m_ActiveSlot = slotCount == 0u ? 0u : (frameIndex % slotCount);
    }

    ImGuiUploadResult ImGuiUploadHelper::UploadFrame(const ImGuiOverlayFrame& frame)
    {
        ImGuiUploadResult result{};
        result.DisplayWidth = frame.DisplayWidth;
        result.DisplayHeight = frame.DisplayHeight;
        result.DrawListCount = static_cast<std::uint32_t>(frame.DrawLists.size());

        if (!frame.Enabled || frame.DrawLists.empty() || m_Device == nullptr ||
            m_BufferManager == nullptr || !m_Device->IsOperational())
        {
            return result;
        }

        std::uint64_t totalVertexCount = 0u;
        std::uint64_t totalIndexCount = 0u;
        std::vector<std::vector<ImGuiDrawCommandUploadResult>> commandUploads;
        commandUploads.reserve(frame.DrawLists.size());
        for (const ImGuiOverlayDrawList& drawList : frame.DrawLists)
        {
            std::vector<ImGuiDrawCommandUploadResult> commands =
                BuildCommandUploads(
                    drawList,
                    frame.DisplayWidth,
                    frame.DisplayHeight);
            if (drawList.Vertices.size() != drawList.VertexCount ||
                drawList.Indices.size() != drawList.IndexCount ||
                commands.empty())
            {
                return result;
            }
            totalVertexCount += static_cast<std::uint64_t>(drawList.VertexCount);
            totalIndexCount += static_cast<std::uint64_t>(drawList.IndexCount);
            result.DrawCommandCount += static_cast<std::uint32_t>(commands.size());
            ++result.CommandUploadListBuilds;
            commandUploads.push_back(std::move(commands));
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

        if (m_VertexBufferSlots.empty() || m_IndexBufferSlots.empty())
        {
            EnsureFrameSlots(1u);
        }
        UploadBufferSlot& vertexSlot = m_VertexBufferSlots[m_ActiveSlot];
        UploadBufferSlot& indexSlot = m_IndexBufferSlots[m_ActiveSlot];

        m_VertexScratch.clear();
        m_VertexScratch.reserve(static_cast<std::size_t>(totalVertexCount));
        m_IndexScratch.clear();
        m_IndexScratch.reserve(static_cast<std::size_t>(totalIndexCount));
        result.DrawLists.reserve(frame.DrawLists.size());

        std::uint32_t firstVertex = 0u;
        std::uint64_t indexOffsetBytes = 0u;
        std::size_t drawListIndex = 0u;
        for (const ImGuiOverlayDrawList& drawList : frame.DrawLists)
        {
            ImGuiDrawListUploadResult listResult{};
            listResult.FirstVertex = firstVertex;
            listResult.IndexOffsetBytes = indexOffsetBytes;
            listResult.VertexCount = drawList.VertexCount;
            listResult.IndexCount = drawList.IndexCount;
            listResult.Commands = std::move(commandUploads[drawListIndex]);
            if (listResult.Commands.empty())
            {
                return ImGuiUploadResult{};
            }

            m_VertexScratch.insert(m_VertexScratch.end(), drawList.Vertices.begin(), drawList.Vertices.end());
            m_IndexScratch.insert(m_IndexScratch.end(), drawList.Indices.begin(), drawList.Indices.end());
            result.DrawLists.push_back(listResult);

            firstVertex += drawList.VertexCount;
            indexOffsetBytes += static_cast<std::uint64_t>(drawList.IndexCount) *
                                sizeof(std::uint32_t);
            ++drawListIndex;
        }

        const std::uint64_t vertexUploadBytes =
            static_cast<std::uint64_t>(m_VertexScratch.size() *
                                       sizeof(ImGuiOverlayVertex));
        const std::uint64_t indexUploadBytes =
            static_cast<std::uint64_t>(m_IndexScratch.size() *
                                       sizeof(std::uint32_t));

        const LaneUploadOutput vertexUpload = UploadBytes(
            *m_Device,
            *m_BufferManager,
            vertexSlot.Buffer,
            vertexSlot.CapacityBytes,
            m_BufferAllocationCount,
            m_VertexScratch.data(),
            vertexUploadBytes,
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
            indexSlot.Buffer,
            indexSlot.CapacityBytes,
            m_BufferAllocationCount,
            m_IndexScratch.data(),
            indexUploadBytes,
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
        result.VertexUploadBytes = vertexUploadBytes;
        result.IndexUploadBytes = indexUploadBytes;
        result.TotalUploadBytes = vertexUploadBytes + indexUploadBytes;
        return result;
    }
}
