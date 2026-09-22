// Shared RHI test doubles expose recorded commands and failure controls for
// graphics and runtime tests without creating a GPU device.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <expected>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <span>
#include <vector>

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Geometry2D;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.TextureUpload;

namespace Extrinsic::Tests
{
    class MockTransferQueue final : public RHI::ITransferQueue
    {
    public:
        struct TextureUploadRecord
        {
            RHI::TextureHandle Texture{};
            std::uint64_t SizeBytes = 0;
            std::uint32_t MipLevel = 0;
            std::uint32_t ArrayLayer = 0;
            std::vector<std::byte> Data{};
        };

        struct BufferUploadRecord
        {
            RHI::BufferHandle Buffer{};
            std::uint64_t Offset{0u};
            std::vector<std::byte> Data{};
        };

        bool AlwaysComplete = true;
        bool AcceptBufferUploads = false;
        bool FailTextureUploads = false;
        std::vector<RHI::TransferToken> Issued{};
        std::vector<BufferUploadRecord> BufferUploads{};
        std::vector<TextureUploadRecord> TextureUploads{};

        [[nodiscard]] RHI::TransferToken UploadBuffer(
            RHI::BufferHandle buffer,
            const void* data,
            std::uint64_t size,
            std::uint64_t offset = 0) override;

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle buffer,
                                                     std::span<const std::byte> src,
                                                     std::uint64_t offset = 0) override;

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle texture,
                                                       const void* data,
                                                       std::uint64_t size,
                                                       std::uint32_t mipLevel = 0,
                                                       std::uint32_t arrayLayer = 0) override;

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                std::span<const std::byte>) override
        {
            return {};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override
        {
            (void)token;
            return AlwaysComplete;
        }

        void CollectCompleted() override {}

    private:
        std::uint64_t m_Counter = 0;
    };

    class MockBindlessHeap final : public RHI::IBindlessHeap
    {
    public:
        int AllocateCalls = 0;
        int FreeCalls     = 0;
        int UpdateCalls   = 0;
        int FlushCalls    = 0;
        std::vector<RHI::TextureHandle> AllocatedTextures;
        std::vector<RHI::SamplerHandle> AllocatedSamplers;
        std::vector<RHI::TextureHandle> UpdatedTextures;
        std::vector<RHI::SamplerHandle> UpdatedSamplers;

        [[nodiscard]] RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle texture,
                                                             RHI::SamplerHandle sampler) override;
        void UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle texture,
                               RHI::SamplerHandle sampler) override;
        void FreeSlot(RHI::BindlessIndex) override { ++FreeCalls; }
        void FlushPending() override { ++FlushCalls; }
        [[nodiscard]] std::uint32_t GetCapacity()           const override { return 65536; }
        [[nodiscard]] std::uint32_t GetAllocatedSlotCount() const override { return m_NextSlot; }

    private:
        std::uint32_t m_NextSlot = 0; // slot 0 reserved; first Allocate returns 1
    };

    class MockCommandContext final : public RHI::ICommandContext
    {
    public:
        enum class EventKind
        {
            Begin,
            End,
            FillBuffer,
            BindPipeline,
            SetScissor,
            PushConstants,
            Dispatch,
            DispatchIndirect,
            BindIndexBuffer,
            Draw,
            DrawIndexed,
            DrawIndexedIndirectCount,
            DrawIndirectCount,
            TextureBarrier,
            MemoryBarrier,
            CopyBuffer,
        };

        struct DrawRecord
        {
            std::uint32_t VertexCount = 0;
            std::uint32_t InstanceCount = 0;
            std::uint32_t FirstVertex = 0;
            std::uint32_t FirstInstance = 0;
        };

        struct IndirectCountRecord
        {
            RHI::BufferHandle ArgumentBuffer{};
            std::uint64_t ArgumentOffset = 0;
            RHI::BufferHandle CountBuffer{};
            std::uint64_t CountOffset = 0;
            std::uint32_t MaxDrawCount = 0;
        };

        struct DrawIndexedRecord
        {
            std::uint32_t IndexCount = 0;
            std::uint32_t InstanceCount = 0;
            std::uint32_t FirstIndex = 0;
            std::int32_t VertexOffset = 0;
            std::uint32_t FirstInstance = 0;
        };

        struct ScissorRecord
        {
            std::int32_t X = 0;
            std::int32_t Y = 0;
            std::uint32_t Width = 0u;
            std::uint32_t Height = 0u;
        };

        struct TextureBarrierRecord
        {
            RHI::TextureHandle Texture{};
            RHI::TextureLayout Before = RHI::TextureLayout::Undefined;
            RHI::TextureLayout After = RHI::TextureLayout::Undefined;
            RHI::MemoryAccess BeforeAccess = RHI::MemoryAccess::None;
            RHI::MemoryAccess AfterAccess = RHI::MemoryAccess::None;
        };

        struct SampledTextureBindingRecord
        {
            RHI::TextureHandle Texture{};
            std::uint32_t DescriptorIndex = 0u;
        };

        struct BufferBarrierRecord
        {
            RHI::BufferHandle Buffer{};
            RHI::MemoryAccess Before = RHI::MemoryAccess::None;
            RHI::MemoryAccess After = RHI::MemoryAccess::None;
        };

        struct MemoryBarrierRecord
        {
            RHI::MemoryAccess Before = RHI::MemoryAccess::None;
            RHI::MemoryAccess After = RHI::MemoryAccess::None;
        };

        struct DispatchRecord
        {
            std::uint32_t X = 0;
            std::uint32_t Y = 0;
            std::uint32_t Z = 0;
        };

        struct DispatchIndirectRecord
        {
            RHI::BufferHandle Args{};
            std::uint64_t Offset = 0;
        };

        struct CopyBufferRecord
        {
            RHI::BufferHandle Src{};
            RHI::BufferHandle Dst{};
            std::uint64_t SrcOffset = 0;
            std::uint64_t DstOffset = 0;
            std::uint64_t Size = 0;
        };

        void Begin() override;
        void End()   override;
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(const std::int32_t x,
                        const std::int32_t y,
                        const std::uint32_t width,
                        const std::uint32_t height) override;
        void BindPipeline(RHI::PipelineHandle handle) override;
        void BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t offset, RHI::IndexType type) override;
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t offset) override;
        void Draw(std::uint32_t vertexCount,
                  std::uint32_t instanceCount,
                  std::uint32_t firstVertex,
                  std::uint32_t firstInstance) override;
        void DrawIndexed(std::uint32_t indexCount,
                         std::uint32_t instanceCount,
                         std::uint32_t firstIndex,
                         std::int32_t vertexOffset,
                         std::uint32_t firstInstance) override;
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle,
                                      std::uint64_t, std::uint32_t maxDrawCount) override;
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle,
                               std::uint64_t, std::uint32_t maxDrawCount) override;
        void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;
        void DispatchIndirect(RHI::BufferHandle argBuffer, std::uint64_t offset) override;
        void TextureBarrier(RHI::TextureHandle texture, RHI::TextureLayout before, RHI::TextureLayout after) override;

        void BindFrameSampledTextureAt(RHI::TextureHandle texture, std::uint32_t descriptorIndex) override;

        void BufferBarrier(RHI::BufferHandle buffer, RHI::MemoryAccess before, RHI::MemoryAccess after) override;

        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override;

        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override;
        void CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                        std::uint64_t srcOffset, std::uint64_t dstOffset,
                        std::uint64_t size) override;
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}

        std::vector<TextureBarrierRecord> TextureBarrierCalls{};
        std::vector<SampledTextureBindingRecord> SampledTextureBindings{};
        std::vector<BufferBarrierRecord>  BufferBarrierCalls{};
        std::vector<MemoryBarrierRecord>  MemoryBarrierCalls{};
        std::vector<DispatchRecord> DispatchRecords{};
        std::vector<DispatchIndirectRecord> DispatchIndirectRecords{};
        std::vector<ScissorRecord> ScissorRecords{};
        std::vector<CopyBufferRecord> CopyBufferRecords{};
        std::vector<EventKind> Events{};
        std::vector<std::uint32_t> PushConstantSizes{};
        std::vector<RHI::PipelineHandle> BoundPipelines{};
        // Payloads and sizes share the same submission-order index.
        std::vector<std::vector<std::byte>> PushConstantPayloads{};
        int BeginCalls = 0;
        int EndCalls = 0;
        int FillBufferCalls = 0;
        int BindPipelineCalls = 0;
        int BindIndexBufferCalls = 0;
        int SetScissorCalls = 0;
        int PushConstantsCalls = 0;
        int DispatchCalls = 0;
        int DispatchIndirectCalls = 0;
        int CopyBufferCalls = 0;
        int DrawCalls = 0;
        int DrawIndexedCalls = 0;
        int DrawIndexedIndirectCountCalls = 0;
        int DrawIndirectCountCalls = 0;
        IndirectCountRecord LastDrawIndexedIndirectCount{};
        IndirectCountRecord LastDrawIndirectCount{};
        DrawRecord LastDraw{};
        DrawIndexedRecord LastDrawIndexed{};
        ScissorRecord LastScissor{};
        std::uint32_t LastPushConstantSize = 0;
        std::uint32_t LastPushConstantOffset = 0;
        DispatchRecord LastDispatch{};
        DispatchIndirectRecord LastDispatchIndirect{};
        CopyBufferRecord LastCopyBuffer{};
        RHI::PipelineHandle LastBoundPipeline{};
        RHI::BufferHandle LastIndexBuffer{};
        std::uint64_t LastIndexBufferOffset = 0;
        RHI::IndexType LastIndexType = RHI::IndexType::Uint32;
        std::uint32_t LastMaxDrawCount = 0;
    };

    class MockDevice final : public RHI::IDevice
    {
    public:
        MockDevice() noexcept;
        ~MockDevice() noexcept override;

        [[nodiscard]] bool HasBackbufferBarrier(
            const RHI::TextureLayout before,
            const RHI::TextureLayout after) const noexcept;

        struct BufferWriteRecord
        {
            RHI::BufferHandle Handle{};
            std::uint64_t Offset = 0;
            std::vector<std::byte> Data{};
        };

        struct TextureWriteRecord
        {
            RHI::TextureHandle Handle{};
            std::uint64_t SizeBytes = 0;
            std::uint32_t MipLevel = 0;
            std::uint32_t ArrayLayer = 0;
            std::vector<std::byte> Data{};
        };

        struct QueueSubmitContextRequest
        {
            RHI::QueueAffinity Affinity = RHI::QueueAffinity::Graphics;
            std::uint32_t FrameIndex = 0;
            std::uint32_t BatchIndex = 0;
        };

        struct RecordedQueueSubmitBatch
        {
            RHI::QueueAffinity Queue = RHI::QueueAffinity::Graphics;
            std::vector<RHI::QueueTimelineWaitDesc> Waits{};
            std::vector<RHI::QueueTimelineSignalDesc> Signals{};
        };

        // ---- Knobs ---------------------------------------------------------
        bool Operational            = true;
        // FailNext flags clear when the corresponding allocation fails.
        bool FailNextBufferCreate   = false;
        bool FailNextTextureCreate  = false;
        bool FailNextSamplerCreate  = false;
        bool FailNextPipelineCreate = false;
        bool PlacedMemorySupported  = false;
        bool FailNextMemoryBlockCreate = false;
        bool FailNextPlacedBufferCreate = false;
        bool FailNextPlacedTextureCreate = false;
        int  FailPipelineCreateCall = 0;
        bool BeginFrameResult       = true;
        bool AsyncComputeQueueAvailable = false;
        bool TransferQueueAvailable = false;
        bool AcceptQueueSubmitPlans = false;
        bool ParallelCommandContextsAvailable = false;
        bool AcceptParallelCommandContextPlans = false;
        RHI::FrameHandle NextFrame{.FrameIndex = 0u, .SwapchainImageIndex = 0u};
        RHI::TextureHandle BackbufferHandle{100u, 1u};
        RHI::Format BackbufferFormat = RHI::Format::RGBA8_UNORM;
        std::uint64_t GlobalFrameNumber = 0;
        bool AdvanceGlobalFrameOnEndFrame = true;
        std::uint32_t FramesInFlight = 2u;

        // ---- Counters ------------------------------------------------------
        int CreateBufferCount    = 0;
        int DestroyBufferCount   = 0;
        int CreateTextureCount   = 0;
        int DestroyTextureCount  = 0;
        int CreateSamplerCount   = 0;
        int DestroySamplerCount  = 0;
        int CreatePipelineCount  = 0;
        int DestroyPipelineCount = 0;
        mutable int GetBufferMemoryRequirementsCount = 0;
        mutable int GetTextureMemoryRequirementsCount = 0;
        int CreateMemoryBlockCount = 0;
        int DestroyMemoryBlockCount = 0;
        int CreatePlacedBufferCount = 0;
        int CreatePlacedTextureCount = 0;
        int BeginFrameCount      = 0;
        int EndFrameCount        = 0;
        int PresentCount         = 0;
        int ResizeCount          = 0;
        mutable int GetBackbufferHandleCount = 0;
        mutable RHI::FrameHandle LastBackbufferFrame{};
        std::vector<BufferWriteRecord> BufferWrites;
        std::vector<TextureWriteRecord> TextureWrites;
        std::vector<RHI::TextureDesc> CreatedTextureDescs;
        std::vector<RHI::TextureHandle> CreatedTextureHandles;
        std::vector<RHI::BufferDesc> CreatedPlacedBufferDescs;
        std::vector<RHI::BufferHandle> CreatedPlacedBufferHandles;
        std::vector<RHI::TextureDesc> CreatedPlacedTextureDescs;
        std::vector<RHI::TextureHandle> CreatedPlacedTextureHandles;
        std::vector<RHI::PipelineDesc> CreatedPipelineDescs;
        std::vector<RHI::PipelineHandle> CreatedPipelineHandles;
        std::vector<RHI::SamplerHandle> CreatedSamplerHandles;
        std::vector<QueueSubmitContextRequest> QueueSubmitContextRequests;
        std::vector<RecordedQueueSubmitBatch> RecordedQueueSubmitPlan;
        std::vector<RHI::ParallelCommandContextRequest> RecordedParallelCommandContextPlan;
        std::vector<RHI::ParallelCommandContextRequest> ParallelCommandContextRequests;
        std::vector<RHI::ParallelCommandContextRequest> SubmittedParallelCommandContexts;
        std::mutex ParallelCommandContextRequestsMutex;

        std::vector<bool> RecipeGraphValidationCalls;

        MockBindlessHeap   Bindless;
        MockCommandContext CommandContext;
        MockCommandContext AsyncComputeContext;
        MockCommandContext TransferContext;
        std::vector<MockCommandContext> ParallelCommandContexts;
        MockTransferQueue  TransferQueue;
        RHI::IProfiler* Profiler = nullptr;

        // ---- IDevice -------------------------------------------------------
        [[nodiscard]] bool IsOperational() const noexcept override { return Operational; }

        void NoteRecipeGraphValidation(bool clean) noexcept override;

        void Initialize(const RHI::DeviceCreateDesc&) override {}
        void Shutdown() override {}
        void WaitIdle() override {}

        bool BeginFrame(RHI::FrameHandle& out) override;
        void EndFrame(const RHI::FrameHandle&) override;
        void Present(const RHI::FrameHandle&) override { ++PresentCount; }
        void Resize(std::uint32_t, std::uint32_t) override { ++ResizeCount; }
        void SetPresentMode(RHI::PresentMode) override {}
        [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return RHI::PresentMode::VSync; }
        [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle& frame) const override;
        Core::Extent2D GetBackbufferExtent() const override { return {}; }
        [[nodiscard]] RHI::Format GetBackbufferFormat() const override { return BackbufferFormat; }

        RHI::ICommandContext& GetGraphicsContext(std::uint32_t) override { return CommandContext; }
        RHI::ITransferQueue& GetTransferQueue() override { return TransferQueue; }

        [[nodiscard]] RHI::QueueCapabilityProfile GetQueueCapabilityProfile() const noexcept override
        {
            return RHI::QueueCapabilityProfile{
                .SupportsAsyncCompute = AsyncComputeQueueAvailable,
                .SupportsTransfer = TransferQueueAvailable,
            };
        }

        [[nodiscard]] RHI::ICommandContext& GetQueueContext(const RHI::QueueAffinity affinity,
                                                            std::uint32_t) override
        {
            return GetMockQueueContext(affinity);
        }

        [[nodiscard]] bool BeginFrameQueueSubmitPlan(const RHI::FrameHandle&,
                                                     const RHI::FrameQueueSubmitPlanDesc& plan) override;

        [[nodiscard]] RHI::ICommandContext& GetQueueSubmitContext(const RHI::QueueAffinity affinity,
                                                                  std::uint32_t frameIndex,
                                                                  std::uint32_t batchIndex) override;

        [[nodiscard]] bool SupportsParallelCommandContexts() const noexcept override
        {
            return ParallelCommandContextsAvailable;
        }

        [[nodiscard]] bool BeginFrameParallelCommandContexts(
            const RHI::FrameHandle&,
            const RHI::ParallelCommandContextPlanDesc& plan) override;

        [[nodiscard]] RHI::ICommandContext& GetParallelCommandContext(
            const RHI::ParallelCommandContextRequest& request) override;

        void SubmitParallelCommandContext(const RHI::ParallelCommandContextRequest& request,
                                          RHI::ICommandContext& submitContext) override;

        void EndFrameParallelCommandContexts(const RHI::FrameHandle&) override;

        void SetQueueCapabilityProfile(const RHI::QueueCapabilityProfile profile) noexcept
        {
            AsyncComputeQueueAvailable = profile.SupportsAsyncCompute;
            TransferQueueAvailable = profile.SupportsTransfer;
        }

        [[nodiscard]] bool SupportsMockQueue(const RHI::QueueAffinity affinity) const noexcept;

        [[nodiscard]] RHI::ICommandContext& GetMockQueueContext(const RHI::QueueAffinity affinity);

        [[nodiscard]] RHI::ITransferQueue& GetMockTransferQueueForAffinity()
        {
            return TransferQueue;
        }

        RHI::BufferHandle CreateBuffer(const RHI::BufferDesc&) override;
        void DestroyBuffer(RHI::BufferHandle handle) override;
        void WriteBuffer(RHI::BufferHandle handle, const void* src, std::uint64_t size, std::uint64_t offset) override;

        // ReadBuffer copies only seeded bytes; missing handles leave the destination untouched.
        std::unordered_map<std::uint32_t, std::vector<std::byte>> BufferContents;

        void ReadBuffer(RHI::BufferHandle handle, void* data, std::uint64_t size, std::uint64_t offset) override;
        [[nodiscard]] std::uint64_t GetBufferDeviceAddress(RHI::BufferHandle handle) const override;

        RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override;
        void DestroyTexture(RHI::TextureHandle handle) override;
        void WriteTexture(RHI::TextureHandle handle, const void* data, std::uint64_t size,
                          std::uint32_t mipLevel, std::uint32_t arrayLayer) override;

        RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc&) override;
        void DestroySampler(RHI::SamplerHandle) override { ++DestroySamplerCount; }

        RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc& desc) override;
        void DestroyPipeline(RHI::PipelineHandle) override { ++DestroyPipelineCount; }

        RHI::IBindlessHeap& GetBindlessHeap() override { return Bindless; }
        RHI::IProfiler* GetProfiler() override { return Profiler; }

        [[nodiscard]] std::uint32_t GetFramesInFlight()    const override { return FramesInFlight; }
        [[nodiscard]] std::uint64_t GetGlobalFrameNumber() const override { return GlobalFrameNumber; }

        [[nodiscard]] RHI::ResourceMemoryRequirements GetBufferMemoryRequirements(
            const RHI::BufferDesc& desc) const noexcept override;

        [[nodiscard]] RHI::ResourceMemoryRequirements GetTextureMemoryRequirements(
            const RHI::TextureDesc& desc) const noexcept override;

        [[nodiscard]] RHI::MemoryBlockHandle CreateMemoryBlock(const RHI::MemoryBlockDesc& desc) override;

        void DestroyMemoryBlock(RHI::MemoryBlockHandle handle) override;

        [[nodiscard]] RHI::MemoryBlockInfo GetMemoryBlockInfo(
            RHI::MemoryBlockHandle handle) const noexcept override;

        [[nodiscard]] RHI::BufferHandle CreatePlacedBuffer(const RHI::PlacedBufferDesc& desc) override;

        [[nodiscard]] RHI::TextureHandle CreatePlacedTexture(const RHI::PlacedTextureDesc& desc) override;

        [[nodiscard]] RHI::PlacedResourceInfo GetBufferMemoryPlacement(
            RHI::BufferHandle handle) const noexcept override;

        [[nodiscard]] RHI::PlacedResourceInfo GetTextureMemoryPlacement(
            RHI::TextureHandle handle) const noexcept override;

    private:
        static constexpr std::uint64_t kPlacedMemoryAlignment = 256u;
        static constexpr std::uint32_t kPlacedMemoryTypeBit = 1u;

        [[nodiscard]] static constexpr std::uint64_t AlignUp(
            const std::uint64_t value,
            const std::uint64_t alignment) noexcept
        {
            if (alignment <= 1u)
            {
                return value;
            }
            const std::uint64_t remainder = value % alignment;
            return remainder == 0u ? value : value + (alignment - remainder);
        }

        [[nodiscard]] static constexpr std::uint32_t SelectMemoryTypeBit(
            const std::uint32_t memoryTypeBits) noexcept
        {
            return memoryTypeBits & (~memoryTypeBits + 1u);
        }

        [[nodiscard]] std::optional<RHI::PlacedResourceInfo> ValidatePlacedResource(
            const RHI::ResourceMemoryRequirements requirements,
            const RHI::PlacedResourceBinding& binding) const noexcept;

        std::unordered_map<std::uint32_t, RHI::MemoryBlockInfo> MemoryBlocks;
        std::unordered_map<std::uint32_t, RHI::PlacedResourceInfo> BufferPlacements;
        std::unordered_map<std::uint32_t, RHI::PlacedResourceInfo> TexturePlacements;

        std::uint32_t m_NextBuffer   = 1; // 0 is reserved / invalid
        std::uint32_t m_NextTexture  = 1;
        std::uint32_t m_NextSampler  = 1;
        std::uint32_t m_NextPipeline = 1;
        std::uint32_t m_NextMemoryBlock = 1;
    };
}
