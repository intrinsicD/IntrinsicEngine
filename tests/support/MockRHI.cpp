// Compiles shared RHI test-double behavior once for graphics and runtime tests.
#include "MockRHI.hpp"

namespace Extrinsic::Tests
{
    // Keep the device's container construction and cleanup in the compiled owner.
    MockDevice::MockDevice() noexcept = default;
    MockDevice::~MockDevice() noexcept = default;

    RHI::TransferToken MockTransferQueue::UploadBuffer(
        RHI::BufferHandle buffer,
        const void* data,
        std::uint64_t size,
        std::uint64_t offset)
    {
        if (!AcceptBufferUploads || data == nullptr || size == 0u)
            return {};

        BufferUploadRecord record{
            .Buffer = buffer,
            .Offset = offset,
        };
        const auto bytes = std::span{
            static_cast<const std::byte*>(data),
            static_cast<std::size_t>(size)};
        record.Data.assign(bytes.begin(), bytes.end());
        BufferUploads.push_back(std::move(record));
        RHI::TransferToken token{++m_Counter};
        Issued.push_back(token);
        return token;
    }

    RHI::TransferToken MockTransferQueue::UploadBuffer(RHI::BufferHandle buffer,
        std::span<const std::byte> src,
        std::uint64_t offset)
    {
        return UploadBuffer(buffer, src.data(), src.size_bytes(), offset);
    }

    RHI::TransferToken MockTransferQueue::UploadTexture(RHI::TextureHandle texture,
        const void* data,
        std::uint64_t size,
        std::uint32_t mipLevel,
        std::uint32_t arrayLayer)
    {
        TextureUploadRecord rec;
        rec.Texture = texture;
        rec.SizeBytes = size;
        rec.MipLevel = mipLevel;
        rec.ArrayLayer = arrayLayer;
        rec.Data.resize(static_cast<std::size_t>(size));
        if (data != nullptr && size > 0)
            std::memcpy(rec.Data.data(), data, static_cast<std::size_t>(size));
        TextureUploads.push_back(std::move(rec));

        if (FailTextureUploads)
            return {};

        RHI::TransferToken t{++m_Counter};
        Issued.push_back(t);
        return t;
    }

    RHI::BindlessIndex MockBindlessHeap::AllocateTextureSlot(RHI::TextureHandle texture,
        RHI::SamplerHandle sampler)
    {
        ++AllocateCalls;
        AllocatedTextures.push_back(texture);
        AllocatedSamplers.push_back(sampler);
        return ++m_NextSlot;
    }

    void MockBindlessHeap::UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle texture,
        RHI::SamplerHandle sampler)
    {
        ++UpdateCalls;
        UpdatedTextures.push_back(texture);
        UpdatedSamplers.push_back(sampler);
    }

    void MockCommandContext::Begin()
    {
        ++BeginCalls;
        Events.push_back(EventKind::Begin);
    }

    void MockCommandContext::End()
    {
        ++EndCalls;
        Events.push_back(EventKind::End);
    }

    void MockCommandContext::SetScissor(const std::int32_t x,
        const std::int32_t y,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        ++SetScissorCalls;
        LastScissor = ScissorRecord{
            .X = x,
            .Y = y,
            .Width = width,
            .Height = height,
        };
        ScissorRecords.push_back(LastScissor);
        Events.push_back(EventKind::SetScissor);
    }

    void MockCommandContext::BindPipeline(RHI::PipelineHandle handle)
    {
        ++BindPipelineCalls;
        LastBoundPipeline = handle;
        BoundPipelines.push_back(handle);
        Events.push_back(EventKind::BindPipeline);
    }

    void MockCommandContext::BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t offset, RHI::IndexType type)
    {
        ++BindIndexBufferCalls;
        LastIndexBuffer = buffer;
        LastIndexBufferOffset = offset;
        LastIndexType = type;
        Events.push_back(EventKind::BindIndexBuffer);
    }

    void MockCommandContext::PushConstants(const void* data, std::uint32_t size, std::uint32_t offset)
    {
        ++PushConstantsCalls;
        LastPushConstantSize = size;
        LastPushConstantOffset = offset;
        PushConstantSizes.push_back(size);
        // The caller owns data only for this call; retain a copy for later assertions.
        std::vector<std::byte> payload(size);
        if (data != nullptr && size > 0u)
        {
            std::memcpy(payload.data(), data, size);
        }
        PushConstantPayloads.push_back(std::move(payload));
        Events.push_back(EventKind::PushConstants);
    }

    void MockCommandContext::Draw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance)
    {
        ++DrawCalls;
        LastDraw = DrawRecord{
            .VertexCount = vertexCount,
            .InstanceCount = instanceCount,
            .FirstVertex = firstVertex,
            .FirstInstance = firstInstance,
        };
        DrawRecords.push_back(LastDraw);
        Events.push_back(EventKind::Draw);
    }

    void MockCommandContext::DrawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance)
    {
        ++DrawIndexedCalls;
        LastDrawIndexed = DrawIndexedRecord{
            .IndexCount = indexCount,
            .InstanceCount = instanceCount,
            .FirstIndex = firstIndex,
            .VertexOffset = vertexOffset,
            .FirstInstance = firstInstance,
        };
        Events.push_back(EventKind::DrawIndexed);
    }

    void MockCommandContext::DrawIndexedIndirectCount(RHI::BufferHandle argumentBuffer,
        std::uint64_t argumentOffset, RHI::BufferHandle countBuffer,
        std::uint64_t countOffset, std::uint32_t maxDrawCount)
    {
        LastDrawIndexedIndirectCount = {argumentBuffer, argumentOffset, countBuffer, countOffset, maxDrawCount};
        ++DrawIndexedIndirectCountCalls;
        LastMaxDrawCount = maxDrawCount;
        Events.push_back(EventKind::DrawIndexedIndirectCount);
    }

    void MockCommandContext::DrawIndirectCount(RHI::BufferHandle argumentBuffer,
        std::uint64_t argumentOffset, RHI::BufferHandle countBuffer,
        std::uint64_t countOffset, std::uint32_t maxDrawCount)
    {
        LastDrawIndirectCount = {argumentBuffer, argumentOffset, countBuffer, countOffset, maxDrawCount};
        ++DrawIndirectCountCalls;
        LastMaxDrawCount = maxDrawCount;
        Events.push_back(EventKind::DrawIndirectCount);
    }

    void MockCommandContext::Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
    {
        ++DispatchCalls;
        LastDispatch = DispatchRecord{.X = x, .Y = y, .Z = z};
        DispatchRecords.push_back(LastDispatch);
        Events.push_back(EventKind::Dispatch);
    }

    void MockCommandContext::DispatchIndirect(RHI::BufferHandle argBuffer, std::uint64_t offset)
    {
        ++DispatchIndirectCalls;
        LastDispatchIndirect = DispatchIndirectRecord{
            .Args = argBuffer,
            .Offset = offset,
        };
        DispatchIndirectRecords.push_back(LastDispatchIndirect);
        Events.push_back(EventKind::DispatchIndirect);
    }

    void MockCommandContext::TextureBarrier(RHI::TextureHandle texture, RHI::TextureLayout before, RHI::TextureLayout after)
    {
        TextureBarrierCalls.push_back({texture, before, after});
        Events.push_back(EventKind::TextureBarrier);
    }

    void MockCommandContext::BindFrameSampledTextureAt(RHI::TextureHandle texture, std::uint32_t descriptorIndex)
    {
        SampledTextureBindings.push_back(SampledTextureBindingRecord{
            .Texture = texture,
            .DescriptorIndex = descriptorIndex,
        });
    }

    void MockCommandContext::BufferBarrier(RHI::BufferHandle buffer, RHI::MemoryAccess before, RHI::MemoryAccess after)
    {
        BufferBarrierCalls.push_back({buffer, before, after});
    }

    void MockCommandContext::SubmitBarriers(const RHI::BarrierBatchDesc& batch)
    {
        for (const RHI::TextureBarrierDesc& barrier : batch.TextureBarriers)
        {
            TextureBarrierCalls.push_back(TextureBarrierRecord{
                .Texture = barrier.Texture,
                .Before = barrier.BeforeLayout,
                .After = barrier.AfterLayout,
                .BeforeAccess = barrier.BeforeAccess,
                .AfterAccess = barrier.AfterAccess,
            });
            Events.push_back(EventKind::TextureBarrier);
        }
        for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
        {
            BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
        }
        for (const RHI::MemoryBarrierDesc& barrier : batch.MemoryBarriers)
        {
            MemoryBarrierCalls.push_back(MemoryBarrierRecord{
                .Before = barrier.BeforeAccess,
                .After = barrier.AfterAccess,
            });
            Events.push_back(EventKind::MemoryBarrier);
        }
    }

    void MockCommandContext::FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t)
    {
        ++FillBufferCalls;
        Events.push_back(EventKind::FillBuffer);
    }

    void MockCommandContext::CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
        std::uint64_t srcOffset, std::uint64_t dstOffset,
        std::uint64_t size)
    {
        ++CopyBufferCalls;
        LastCopyBuffer = CopyBufferRecord{
            .Src = src,
            .Dst = dst,
            .SrcOffset = srcOffset,
            .DstOffset = dstOffset,
            .Size = size,
        };
        CopyBufferRecords.push_back(LastCopyBuffer);
        Events.push_back(EventKind::CopyBuffer);
    }

    bool MockDevice::HasBackbufferBarrier(
        const RHI::TextureLayout before,
        const RHI::TextureLayout after) const noexcept
    {
        for (const auto& barrier : CommandContext.TextureBarrierCalls)
        {
            if (barrier.Texture == BackbufferHandle &&
                barrier.Before == before &&
                barrier.After == after)
            {
                return true;
            }
        }
        return false;
    }

    void MockDevice::NoteRecipeGraphValidation(bool clean) noexcept
    {
        RecipeGraphValidationCalls.push_back(clean);
    }

    bool MockDevice::BeginFrame(RHI::FrameHandle& out)
    {
        ++BeginFrameCount;
        out = NextFrame;
        return BeginFrameResult;
    }

    void MockDevice::EndFrame(const RHI::FrameHandle&)
    {
        ++EndFrameCount;
        if (AdvanceGlobalFrameOnEndFrame)
        {
            ++GlobalFrameNumber;
        }
    }

    RHI::TextureHandle MockDevice::GetBackbufferHandle(const RHI::FrameHandle& frame) const
    {
        ++GetBackbufferHandleCount;
        LastBackbufferFrame = frame;
        return BackbufferHandle;
    }

    bool MockDevice::BeginFrameQueueSubmitPlan(const RHI::FrameHandle&,
        const RHI::FrameQueueSubmitPlanDesc& plan)
    {
        RecordedQueueSubmitPlan.clear();
        if (!AcceptQueueSubmitPlans)
        {
            return false;
        }

        RecordedQueueSubmitPlan.reserve(plan.Batches.size());
        for (const RHI::QueueSubmitBatchDesc& batch : plan.Batches)
        {
            RecordedQueueSubmitBatch recorded{
                .Queue = batch.Queue,
            };
            recorded.Waits.assign(batch.Waits.begin(), batch.Waits.end());
            recorded.Signals.assign(batch.Signals.begin(), batch.Signals.end());
            RecordedQueueSubmitPlan.push_back(std::move(recorded));
        }
        return true;
    }

    RHI::ICommandContext& MockDevice::GetQueueSubmitContext(const RHI::QueueAffinity affinity,
        std::uint32_t frameIndex,
        std::uint32_t batchIndex)
    {
        QueueSubmitContextRequests.push_back(QueueSubmitContextRequest{
            .Affinity = affinity,
            .FrameIndex = frameIndex,
            .BatchIndex = batchIndex,
        });
        return GetMockQueueContext(affinity);
    }

    bool MockDevice::BeginFrameParallelCommandContexts(
        const RHI::FrameHandle&,
        const RHI::ParallelCommandContextPlanDesc& plan)
    {
        RecordedParallelCommandContextPlan.clear();
        ParallelCommandContextRequests.clear();
        SubmittedParallelCommandContexts.clear();
        ParallelCommandContexts.clear();

        if (!ParallelCommandContextsAvailable || !AcceptParallelCommandContextPlans)
        {
            return false;
        }

        RecordedParallelCommandContextPlan.assign(plan.Requests.begin(), plan.Requests.end());
        ParallelCommandContexts.resize(RecordedParallelCommandContextPlan.size());
        return !ParallelCommandContexts.empty();
    }

    RHI::ICommandContext& MockDevice::GetParallelCommandContext(
        const RHI::ParallelCommandContextRequest& request)
    {
        {
            std::scoped_lock lock(ParallelCommandContextRequestsMutex);
            ParallelCommandContextRequests.push_back(request);
        }
        if (request.ContextIndex < ParallelCommandContexts.size())
        {
            return ParallelCommandContexts[request.ContextIndex];
        }
        return GetMockQueueContext(request.Queue);
    }

    void MockDevice::SubmitParallelCommandContext(const RHI::ParallelCommandContextRequest& request,
        RHI::ICommandContext& submitContext)
    {
        (void)submitContext;
        SubmittedParallelCommandContexts.push_back(request);
    }

    void MockDevice::EndFrameParallelCommandContexts(const RHI::FrameHandle&)
    {
        ParallelCommandContexts.clear();
    }

    bool MockDevice::SupportsMockQueue(const RHI::QueueAffinity affinity) const noexcept
    {
        switch (affinity)
        {
        case RHI::QueueAffinity::Graphics:
            return true;
        case RHI::QueueAffinity::AsyncCompute:
            return AsyncComputeQueueAvailable;
        case RHI::QueueAffinity::Transfer:
            return TransferQueueAvailable;
        }
        return false;
    }

    RHI::ICommandContext& MockDevice::GetMockQueueContext(const RHI::QueueAffinity affinity)
    {
        if (affinity == RHI::QueueAffinity::AsyncCompute && AsyncComputeQueueAvailable)
        {
            return AsyncComputeContext;
        }
        if (affinity == RHI::QueueAffinity::Transfer && TransferQueueAvailable)
        {
            return TransferContext;
        }
        return CommandContext;
    }

    RHI::BufferHandle MockDevice::CreateBuffer(const RHI::BufferDesc&)
    {
        ++CreateBufferCount;
        if (FailNextBufferCreate) { FailNextBufferCreate = false; return {}; }
        return RHI::BufferHandle{m_NextBuffer++, 1u};
    }

    void MockDevice::DestroyBuffer(RHI::BufferHandle handle)
    {
        ++DestroyBufferCount;
        BufferPlacements.erase(handle.Index);
    }

    void MockDevice::WriteBuffer(RHI::BufferHandle handle, const void* src, std::uint64_t size, std::uint64_t offset)
    {
        BufferWriteRecord rec;
        rec.Handle = handle;
        rec.Offset = offset;
        rec.Data.resize(static_cast<std::size_t>(size));
        if (size > 0 && src != nullptr)
        {
            std::memcpy(rec.Data.data(), src, static_cast<std::size_t>(size));
        }
        BufferWrites.push_back(std::move(rec));
    }

    void MockDevice::ReadBuffer(RHI::BufferHandle handle, void* data, std::uint64_t size, std::uint64_t offset)
    {
        if (data == nullptr || size == 0u)
        {
            return;
        }
        auto it = BufferContents.find(handle.Index);
        if (it == BufferContents.end())
        {
            return;
        }
        const std::vector<std::byte>& contents = it->second;
        if (offset >= contents.size())
        {
            return;
        }
        const std::uint64_t available = static_cast<std::uint64_t>(contents.size()) - offset;
        const std::uint64_t toCopy    = (size < available) ? size : available;
        std::memcpy(data,
                    contents.data() + static_cast<std::size_t>(offset),
                    static_cast<std::size_t>(toCopy));
    }

    std::uint64_t MockDevice::GetBufferDeviceAddress(RHI::BufferHandle handle) const
    {
        if (!handle.IsValid())
        {
            return 0u;
        }
        return 0x1'0000'0000ull + (static_cast<std::uint64_t>(handle.Index) * 0x1000ull);
    }

    RHI::TextureHandle MockDevice::CreateTexture(const RHI::TextureDesc& desc)
    {
        ++CreateTextureCount;
        if (FailNextTextureCreate) { FailNextTextureCreate = false; return {}; }
        const RHI::TextureHandle handle{m_NextTexture++, 1u};
        CreatedTextureDescs.push_back(desc);
        CreatedTextureHandles.push_back(handle);
        return handle;
    }

    void MockDevice::DestroyTexture(RHI::TextureHandle handle)
    {
        ++DestroyTextureCount;
        TexturePlacements.erase(handle.Index);
    }

    void MockDevice::WriteTexture(RHI::TextureHandle handle, const void* data, std::uint64_t size,
        std::uint32_t mipLevel, std::uint32_t arrayLayer)
    {
        TextureWriteRecord record{.Handle = handle,
                                  .SizeBytes = size,
                                  .MipLevel = mipLevel,
                                  .ArrayLayer = arrayLayer};
        record.Data.resize(static_cast<std::size_t>(size));
        if (data != nullptr && size > 0u)
        {
            std::memcpy(record.Data.data(), data, static_cast<std::size_t>(size));
        }
        TextureWrites.push_back(std::move(record));
    }

    RHI::SamplerHandle MockDevice::CreateSampler(const RHI::SamplerDesc&)
    {
        ++CreateSamplerCount;
        if (FailNextSamplerCreate) { FailNextSamplerCreate = false; return {}; }
        const RHI::SamplerHandle handle{m_NextSampler++, 1u};
        CreatedSamplerHandles.push_back(handle);
        return handle;
    }

    RHI::PipelineHandle MockDevice::CreatePipeline(const RHI::PipelineDesc& desc)
    {
        ++CreatePipelineCount;
        CreatedPipelineDescs.push_back(desc);
        if (FailPipelineCreateCall > 0 && CreatePipelineCount == FailPipelineCreateCall)
        {
            CreatedPipelineHandles.push_back(RHI::PipelineHandle{});
            return {};
        }
        if (FailNextPipelineCreate)
        {
            FailNextPipelineCreate = false;
            CreatedPipelineHandles.push_back(RHI::PipelineHandle{});
            return {};
        }
        const RHI::PipelineHandle handle{m_NextPipeline++, 1u};
        CreatedPipelineHandles.push_back(handle);
        return handle;
    }

    RHI::ResourceMemoryRequirements MockDevice::GetBufferMemoryRequirements(
        const RHI::BufferDesc& desc) const noexcept
    {
        ++GetBufferMemoryRequirementsCount;
        if (!PlacedMemorySupported || desc.SizeBytes == 0u)
        {
            return {};
        }
        return RHI::ResourceMemoryRequirements{
            .SizeBytes = AlignUp(desc.SizeBytes, kPlacedMemoryAlignment),
            .AlignmentBytes = kPlacedMemoryAlignment,
            .MemoryTypeBits = kPlacedMemoryTypeBit,
            .DedicatedAllocationRequired = false,
        };
    }

    RHI::ResourceMemoryRequirements MockDevice::GetTextureMemoryRequirements(
        const RHI::TextureDesc& desc) const noexcept
    {
        ++GetTextureMemoryRequirementsCount;
        if (!PlacedMemorySupported)
        {
            return {};
        }
        const std::uint64_t storageBytes = RHI::EstimateTextureStorageBytes(desc);
        if (storageBytes == 0u)
        {
            return {};
        }
        return RHI::ResourceMemoryRequirements{
            .SizeBytes = AlignUp(storageBytes, kPlacedMemoryAlignment),
            .AlignmentBytes = kPlacedMemoryAlignment,
            .MemoryTypeBits = kPlacedMemoryTypeBit,
            .DedicatedAllocationRequired = false,
        };
    }

    RHI::MemoryBlockHandle MockDevice::CreateMemoryBlock(const RHI::MemoryBlockDesc& desc)
    {
        ++CreateMemoryBlockCount;
        if (!PlacedMemorySupported || FailNextMemoryBlockCreate ||
            desc.SizeBytes == 0u || desc.AlignmentBytes == 0u ||
            desc.MemoryTypeBits == 0u ||
            (desc.SizeBytes % desc.AlignmentBytes) != 0u)
        {
            FailNextMemoryBlockCreate = false;
            return {};
        }

        const std::uint32_t selectedBit = SelectMemoryTypeBit(desc.MemoryTypeBits);
        if (selectedBit == 0u)
        {
            return {};
        }

        const RHI::MemoryBlockHandle handle{m_NextMemoryBlock++, 1u};
        MemoryBlocks.emplace(handle.Index, RHI::MemoryBlockInfo{
            .SizeBytes = desc.SizeBytes,
            .AlignmentBytes = desc.AlignmentBytes,
            .MemoryTypeBits = desc.MemoryTypeBits,
            .SelectedMemoryTypeBit = selectedBit,
            .IsValid = true,
        });
        return handle;
    }

    void MockDevice::DestroyMemoryBlock(RHI::MemoryBlockHandle handle)
    {
        ++DestroyMemoryBlockCount;
        MemoryBlocks.erase(handle.Index);
    }

    RHI::MemoryBlockInfo MockDevice::GetMemoryBlockInfo(
        RHI::MemoryBlockHandle handle) const noexcept
    {
        const auto it = MemoryBlocks.find(handle.Index);
        return it == MemoryBlocks.end() ? RHI::MemoryBlockInfo{} : it->second;
    }

    RHI::BufferHandle MockDevice::CreatePlacedBuffer(const RHI::PlacedBufferDesc& desc)
    {
        ++CreatePlacedBufferCount;
        if (!PlacedMemorySupported || FailNextPlacedBufferCreate)
        {
            FailNextPlacedBufferCreate = false;
            return {};
        }

        const std::optional<RHI::PlacedResourceInfo> placement =
            ValidatePlacedResource(GetBufferMemoryRequirements(desc.Desc), desc.Placement);
        if (!placement.has_value())
        {
            return {};
        }

        const RHI::BufferHandle handle{m_NextBuffer++, 1u};
        BufferPlacements.emplace(handle.Index, *placement);
        CreatedPlacedBufferDescs.push_back(desc.Desc);
        CreatedPlacedBufferHandles.push_back(handle);
        return handle;
    }

    RHI::TextureHandle MockDevice::CreatePlacedTexture(const RHI::PlacedTextureDesc& desc)
    {
        ++CreatePlacedTextureCount;
        if (!PlacedMemorySupported || FailNextPlacedTextureCreate)
        {
            FailNextPlacedTextureCreate = false;
            return {};
        }

        const std::optional<RHI::PlacedResourceInfo> placement =
            ValidatePlacedResource(GetTextureMemoryRequirements(desc.Desc), desc.Placement);
        if (!placement.has_value())
        {
            return {};
        }

        const RHI::TextureHandle handle{m_NextTexture++, 1u};
        TexturePlacements.emplace(handle.Index, *placement);
        CreatedPlacedTextureDescs.push_back(desc.Desc);
        CreatedPlacedTextureHandles.push_back(handle);
        return handle;
    }

    RHI::PlacedResourceInfo MockDevice::GetBufferMemoryPlacement(
        RHI::BufferHandle handle) const noexcept
    {
        const auto it = BufferPlacements.find(handle.Index);
        return it == BufferPlacements.end() ? RHI::PlacedResourceInfo{} : it->second;
    }

    RHI::PlacedResourceInfo MockDevice::GetTextureMemoryPlacement(
        RHI::TextureHandle handle) const noexcept
    {
        const auto it = TexturePlacements.find(handle.Index);
        return it == TexturePlacements.end() ? RHI::PlacedResourceInfo{} : it->second;
    }

    std::optional<RHI::PlacedResourceInfo> MockDevice::ValidatePlacedResource(
        const RHI::ResourceMemoryRequirements requirements,
        const RHI::PlacedResourceBinding& binding) const noexcept
    {
        if (!requirements.IsValid() || !binding.Block.IsValid())
        {
            return std::nullopt;
        }
        const auto blockIt = MemoryBlocks.find(binding.Block.Index);
        if (blockIt == MemoryBlocks.end() || !blockIt->second.IsValid)
        {
            return std::nullopt;
        }
        const RHI::MemoryBlockInfo& block = blockIt->second;
        if ((block.SelectedMemoryTypeBit & requirements.MemoryTypeBits) == 0u ||
            (binding.OffsetBytes % requirements.AlignmentBytes) != 0u ||
            binding.OffsetBytes > block.SizeBytes ||
            requirements.SizeBytes > block.SizeBytes - binding.OffsetBytes)
        {
            return std::nullopt;
        }
        return RHI::PlacedResourceInfo{
            .Block = binding.Block,
            .OffsetBytes = binding.OffsetBytes,
            .SizeBytes = requirements.SizeBytes,
            .AlignmentBytes = requirements.AlignmentBytes,
            .MemoryTypeBit = block.SelectedMemoryTypeBit,
            .IsPlaced = true,
        };
    }
}
