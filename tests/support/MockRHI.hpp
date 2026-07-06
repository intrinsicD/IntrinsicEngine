#pragma once

// Shared test doubles for RHI::IDevice and RHI::IBindlessHeap. Used by the
// tests/Graphics/Test.RHI.*Manager.cpp files to exercise manager behavior
// without touching a real GPU.
//
// The including .cpp MUST have already `import`ed the RHI modules this
// header depends on, since imports inside an included header are fragile
// under C++23 module rules. The expected order at the top of a test .cpp:
//
//   #include <gtest/gtest.h>
//   import Extrinsic.RHI.Device;
//   import Extrinsic.RHI.Bindless;
//   import Extrinsic.RHI.CommandContext;
//   ... (any other imports the test needs)
//   #include "MockRHI.hpp"

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
    // -----------------------------------------------------------------------
    // Minimal no-op ITransferQueue used by MockDevice.
    // -----------------------------------------------------------------------
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

        bool AlwaysComplete = true;
        bool FailTextureUploads = false;
        std::vector<RHI::TransferToken> Issued{};
        std::vector<TextureUploadRecord> TextureUploads{};

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                     const void*,
                                                     std::uint64_t,
                                                     std::uint64_t = 0) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                     std::span<const std::byte> src,
                                                     std::uint64_t = 0) override
        {
            return {std::uint64_t(src.size())};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle texture,
                                                       const void* data,
                                                       std::uint64_t size,
                                                       std::uint32_t mipLevel = 0,
                                                       std::uint32_t arrayLayer = 0) override
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

    // -----------------------------------------------------------------------
    // Minimal no-op IBindlessHeap. TextureManager asks for one in its
    // constructor; Retain/Release paths call AllocateTextureSlot / FreeSlot.
    // -----------------------------------------------------------------------
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
                                                             RHI::SamplerHandle sampler) override
        {
            ++AllocateCalls;
            AllocatedTextures.push_back(texture);
            AllocatedSamplers.push_back(sampler);
            return ++m_NextSlot;
        }
        void UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle texture,
                               RHI::SamplerHandle sampler) override
        {
            ++UpdateCalls;
            UpdatedTextures.push_back(texture);
            UpdatedSamplers.push_back(sampler);
        }
        void FreeSlot(RHI::BindlessIndex) override { ++FreeCalls; }
        void FlushPending() override { ++FlushCalls; }
        [[nodiscard]] std::uint32_t GetCapacity()           const override { return 65536; }
        [[nodiscard]] std::uint32_t GetAllocatedSlotCount() const override { return m_NextSlot; }

    private:
        std::uint32_t m_NextSlot = 0; // slot 0 reserved; first Allocate returns 1
    };

    // -----------------------------------------------------------------------
    // Minimal no-op ICommandContext — IDevice::GetGraphicsContext must return
    // something. None of the manager tests drive commands directly.
    // -----------------------------------------------------------------------
    class MockCommandContext final : public RHI::ICommandContext
    {
    public:
        enum class EventKind
        {
            Begin,
            End,
            FillBuffer,
            BindPipeline,
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

        struct DrawIndexedRecord
        {
            std::uint32_t IndexCount = 0;
            std::uint32_t InstanceCount = 0;
            std::uint32_t FirstIndex = 0;
            std::int32_t VertexOffset = 0;
            std::uint32_t FirstInstance = 0;
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

        void Begin() override { ++BeginCalls; Events.push_back(EventKind::Begin); }
        void End()   override { ++EndCalls; Events.push_back(EventKind::End); }
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle handle) override
        {
            ++BindPipelineCalls;
            LastBoundPipeline = handle;
            BoundPipelines.push_back(handle);
            Events.push_back(EventKind::BindPipeline);
        }
        void BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t offset, RHI::IndexType type) override
        {
            ++BindIndexBufferCalls;
            LastIndexBuffer = buffer;
            LastIndexBufferOffset = offset;
            LastIndexType = type;
            Events.push_back(EventKind::BindIndexBuffer);
        }
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t offset) override
        {
            ++PushConstantsCalls;
            LastPushConstantSize = size;
            LastPushConstantOffset = offset;
            PushConstantSizes.push_back(size);
            // GRAPHICS-072 Slice C — record the pushed payload so contract
            // tests can verify push-constant content (e.g. the deferred
            // lighting pass's `ShadowAtlasBindlessIndex` field) without
            // adding new renderer test seams. The lifetime of `data` is
            // strictly the duration of this call, so we deep-copy here.
            std::vector<std::byte> payload(size);
            if (data != nullptr && size > 0u)
            {
                std::memcpy(payload.data(), data, size);
            }
            PushConstantPayloads.push_back(std::move(payload));
            Events.push_back(EventKind::PushConstants);
        }
        void Draw(std::uint32_t vertexCount,
                  std::uint32_t instanceCount,
                  std::uint32_t firstVertex,
                  std::uint32_t firstInstance) override
        {
            ++DrawCalls;
            LastDraw = DrawRecord{
                .VertexCount = vertexCount,
                .InstanceCount = instanceCount,
                .FirstVertex = firstVertex,
                .FirstInstance = firstInstance,
            };
            Events.push_back(EventKind::Draw);
        }
        void DrawIndexed(std::uint32_t indexCount,
                         std::uint32_t instanceCount,
                         std::uint32_t firstIndex,
                         std::int32_t vertexOffset,
                         std::uint32_t firstInstance) override
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
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle,
                                      std::uint64_t, std::uint32_t maxDrawCount) override
        {
            ++DrawIndexedIndirectCountCalls;
            LastMaxDrawCount = maxDrawCount;
            Events.push_back(EventKind::DrawIndexedIndirectCount);
        }
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle,
                               std::uint64_t, std::uint32_t maxDrawCount) override
        {
            ++DrawIndirectCountCalls;
            LastMaxDrawCount = maxDrawCount;
            Events.push_back(EventKind::DrawIndirectCount);
        }
        void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override
        {
            ++DispatchCalls;
            LastDispatch = DispatchRecord{.X = x, .Y = y, .Z = z};
            DispatchRecords.push_back(LastDispatch);
            Events.push_back(EventKind::Dispatch);
        }
        void DispatchIndirect(RHI::BufferHandle argBuffer, std::uint64_t offset) override
        {
            ++DispatchIndirectCalls;
            LastDispatchIndirect = DispatchIndirectRecord{
                .Args = argBuffer,
                .Offset = offset,
            };
            DispatchIndirectRecords.push_back(LastDispatchIndirect);
            Events.push_back(EventKind::DispatchIndirect);
        }
        void TextureBarrier(RHI::TextureHandle texture, RHI::TextureLayout before, RHI::TextureLayout after) override
        {
            TextureBarrierCalls.push_back({texture, before, after});
            Events.push_back(EventKind::TextureBarrier);
        }

        void BindFrameSampledTextureAt(RHI::TextureHandle texture, std::uint32_t descriptorIndex) override
        {
            SampledTextureBindings.push_back(SampledTextureBindingRecord{
                .Texture = texture,
                .DescriptorIndex = descriptorIndex,
            });
        }

        void BufferBarrier(RHI::BufferHandle buffer, RHI::MemoryAccess before, RHI::MemoryAccess after) override
        {
            BufferBarrierCalls.push_back({buffer, before, after});
        }

        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
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

        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override
        {
            ++FillBufferCalls;
            Events.push_back(EventKind::FillBuffer);
        }
        void CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                        std::uint64_t srcOffset, std::uint64_t dstOffset,
                        std::uint64_t size) override
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
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}

        std::vector<TextureBarrierRecord> TextureBarrierCalls{};
        std::vector<SampledTextureBindingRecord> SampledTextureBindings{};
        std::vector<BufferBarrierRecord>  BufferBarrierCalls{};
        std::vector<MemoryBarrierRecord>  MemoryBarrierCalls{};
        std::vector<DispatchRecord> DispatchRecords{};
        std::vector<DispatchIndirectRecord> DispatchIndirectRecords{};
        std::vector<CopyBufferRecord> CopyBufferRecords{};
        std::vector<EventKind> Events{};
        std::vector<std::uint32_t> PushConstantSizes{};
        std::vector<RHI::PipelineHandle> BoundPipelines{};
        // GRAPHICS-072 Slice C — full payload capture parallel to
        // `PushConstantSizes`. `PushConstantPayloads[i]` holds the bytes of
        // the i-th `PushConstants(...)` call in submission order.
        std::vector<std::vector<std::byte>> PushConstantPayloads{};
        int BeginCalls = 0;
        int EndCalls = 0;
        int FillBufferCalls = 0;
        int BindPipelineCalls = 0;
        int BindIndexBufferCalls = 0;
        int PushConstantsCalls = 0;
        int DispatchCalls = 0;
        int DispatchIndirectCalls = 0;
        int CopyBufferCalls = 0;
        int DrawCalls = 0;
        int DrawIndexedCalls = 0;
        int DrawIndexedIndirectCountCalls = 0;
        int DrawIndirectCountCalls = 0;
        DrawRecord LastDraw{};
        DrawIndexedRecord LastDrawIndexed{};
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

    // -----------------------------------------------------------------------
    // MockDevice — lightweight IDevice with knobs for manager tests.
    //
    // Knobs (tweak before manager calls):
    //   Operational            — IsOperational() return value (F14 gate).
    //   FailNext*Create        — next Create* returns an invalid handle,
    //                            simulating a failed device allocation
    //                            (F3 OOM / shader-compile path). Self-
    //                            clearing: set true, the NEXT call fails
    //                            then the flag resets.
    //
    // Counters (read after manager calls):
    //   Create*Count / Destroy*Count — how many times the manager reached
    //   through to IDevice. Useful for verifying short-circuits and
    //   refcount-driven destroy flows.
    // -----------------------------------------------------------------------
    class MockDevice final : public RHI::IDevice
    {
    public:
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
        // Default mirrors a typical double-buffered swapchain (matches the
        // RHI's historical fixed value); tests that need to exercise
        // frame-count-dependent allocations (e.g. picking readback buffer
        // sizing in GRAPHICS-074 Slice D.1) flip this between
        // `Initialize()` and `RebuildOperationalResources()` calls.
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

        // GRAPHICS-033E: records every `NoteRecipeGraphValidation(bool)` call
        // so contract tests can verify the renderer publishes the recipe-aware
        // validation outcome exactly once per recipe compile attempt.
        std::vector<bool> RecipeGraphValidationCalls;

        MockBindlessHeap   Bindless;
        MockCommandContext CommandContext;
        MockCommandContext AsyncComputeContext;
        MockCommandContext TransferContext;
        std::vector<MockCommandContext> ParallelCommandContexts;
        MockTransferQueue  TransferQueue;

        // ---- IDevice -------------------------------------------------------
        [[nodiscard]] bool IsOperational() const noexcept override { return Operational; }

        void NoteRecipeGraphValidation(bool clean) noexcept override
        {
            RecipeGraphValidationCalls.push_back(clean);
        }

        void Initialize(const RHI::DeviceCreateDesc&) override {}
        void Shutdown() override {}
        void WaitIdle() override {}

        bool BeginFrame(RHI::FrameHandle& out) override
        {
            ++BeginFrameCount;
            out = NextFrame;
            return BeginFrameResult;
        }
        void EndFrame(const RHI::FrameHandle&) override
        {
            ++EndFrameCount;
            ++GlobalFrameNumber;
        }
        void Present(const RHI::FrameHandle&) override { ++PresentCount; }
        void Resize(std::uint32_t, std::uint32_t) override { ++ResizeCount; }
        void SetPresentMode(RHI::PresentMode) override {}
        [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return RHI::PresentMode::VSync; }
        [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle& frame) const override
        {
            ++GetBackbufferHandleCount;
            LastBackbufferFrame = frame;
            return BackbufferHandle;
        }
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
                                                     const RHI::FrameQueueSubmitPlanDesc& plan) override
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

        [[nodiscard]] RHI::ICommandContext& GetQueueSubmitContext(const RHI::QueueAffinity affinity,
                                                                  std::uint32_t frameIndex,
                                                                  std::uint32_t batchIndex) override
        {
            QueueSubmitContextRequests.push_back(QueueSubmitContextRequest{
                .Affinity = affinity,
                .FrameIndex = frameIndex,
                .BatchIndex = batchIndex,
            });
            return GetMockQueueContext(affinity);
        }

        [[nodiscard]] bool SupportsParallelCommandContexts() const noexcept override
        {
            return ParallelCommandContextsAvailable;
        }

        [[nodiscard]] bool BeginFrameParallelCommandContexts(
            const RHI::FrameHandle&,
            const RHI::ParallelCommandContextPlanDesc& plan) override
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

        [[nodiscard]] RHI::ICommandContext& GetParallelCommandContext(
            const RHI::ParallelCommandContextRequest& request) override
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

        void SubmitParallelCommandContext(const RHI::ParallelCommandContextRequest& request,
                                          RHI::ICommandContext& submitContext) override
        {
            (void)submitContext;
            SubmittedParallelCommandContexts.push_back(request);
        }

        void EndFrameParallelCommandContexts(const RHI::FrameHandle&) override
        {
            ParallelCommandContexts.clear();
        }

        void SetQueueCapabilityProfile(const RHI::QueueCapabilityProfile profile) noexcept
        {
            AsyncComputeQueueAvailable = profile.SupportsAsyncCompute;
            TransferQueueAvailable = profile.SupportsTransfer;
        }

        [[nodiscard]] bool SupportsMockQueue(const RHI::QueueAffinity affinity) const noexcept
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

        [[nodiscard]] RHI::ICommandContext& GetMockQueueContext(const RHI::QueueAffinity affinity)
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

        [[nodiscard]] RHI::ITransferQueue& GetMockTransferQueueForAffinity()
        {
            return TransferQueue;
        }

        RHI::BufferHandle CreateBuffer(const RHI::BufferDesc&) override
        {
            ++CreateBufferCount;
            if (FailNextBufferCreate) { FailNextBufferCreate = false; return {}; }
            return RHI::BufferHandle{m_NextBuffer++, 1u};
        }
        void DestroyBuffer(RHI::BufferHandle handle) override
        {
            ++DestroyBufferCount;
            BufferPlacements.erase(handle.Index);
        }
        void WriteBuffer(RHI::BufferHandle handle, const void* src, std::uint64_t size, std::uint64_t offset) override
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

        // GRAPHICS-074 Slice D.3 — host-visible buffer content seeding for
        // `ReadBuffer(...)`. Tests populate `BufferContents[handle.Index]`
        // with the bytes the renderer's `BeginFrame()`-side picking-readback
        // drain should observe, since `MockCommandContext::CopyTextureToBuffer`
        // is a no-op and the renderer needs to see *some* deterministic
        // contents at `slot * 8` (one 4-byte `EntityId` + one 4-byte
        // `EncodedSelectionId`). `ReadBuffer(...)` below copies from this
        // table when the requested handle has a matching entry; unknown
        // handles preserve the default no-op contract documented on
        // `IDevice::ReadBuffer`.
        std::unordered_map<std::uint32_t, std::vector<std::byte>> BufferContents;

        void ReadBuffer(RHI::BufferHandle handle, void* data, std::uint64_t size, std::uint64_t offset) override
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
        [[nodiscard]] std::uint64_t GetBufferDeviceAddress(RHI::BufferHandle handle) const override
        {
            if (!handle.IsValid())
            {
                return 0u;
            }
            return 0x1'0000'0000ull + (static_cast<std::uint64_t>(handle.Index) * 0x1000ull);
        }

        RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override
        {
            ++CreateTextureCount;
            if (FailNextTextureCreate) { FailNextTextureCreate = false; return {}; }
            const RHI::TextureHandle handle{m_NextTexture++, 1u};
            CreatedTextureDescs.push_back(desc);
            CreatedTextureHandles.push_back(handle);
            return handle;
        }
        void DestroyTexture(RHI::TextureHandle handle) override
        {
            ++DestroyTextureCount;
            TexturePlacements.erase(handle.Index);
        }
        void WriteTexture(RHI::TextureHandle handle, const void* data, std::uint64_t size,
                          std::uint32_t mipLevel, std::uint32_t arrayLayer) override
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

        RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc&) override
        {
            ++CreateSamplerCount;
            if (FailNextSamplerCreate) { FailNextSamplerCreate = false; return {}; }
            const RHI::SamplerHandle handle{m_NextSampler++, 1u};
            CreatedSamplerHandles.push_back(handle);
            return handle;
        }
        void DestroySampler(RHI::SamplerHandle) override { ++DestroySamplerCount; }

        RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc& desc) override
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
        void DestroyPipeline(RHI::PipelineHandle) override { ++DestroyPipelineCount; }

        RHI::IBindlessHeap& GetBindlessHeap() override { return Bindless; }
        RHI::IProfiler* GetProfiler() override { return nullptr; }

        [[nodiscard]] std::uint32_t GetFramesInFlight()    const override { return FramesInFlight; }
        [[nodiscard]] std::uint64_t GetGlobalFrameNumber() const override { return GlobalFrameNumber; }

        [[nodiscard]] RHI::ResourceMemoryRequirements GetBufferMemoryRequirements(
            const RHI::BufferDesc& desc) const noexcept override
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

        [[nodiscard]] RHI::ResourceMemoryRequirements GetTextureMemoryRequirements(
            const RHI::TextureDesc& desc) const noexcept override
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

        [[nodiscard]] RHI::MemoryBlockHandle CreateMemoryBlock(const RHI::MemoryBlockDesc& desc) override
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

        void DestroyMemoryBlock(RHI::MemoryBlockHandle handle) override
        {
            ++DestroyMemoryBlockCount;
            MemoryBlocks.erase(handle.Index);
        }

        [[nodiscard]] RHI::MemoryBlockInfo GetMemoryBlockInfo(
            RHI::MemoryBlockHandle handle) const noexcept override
        {
            const auto it = MemoryBlocks.find(handle.Index);
            return it == MemoryBlocks.end() ? RHI::MemoryBlockInfo{} : it->second;
        }

        [[nodiscard]] RHI::BufferHandle CreatePlacedBuffer(const RHI::PlacedBufferDesc& desc) override
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

        [[nodiscard]] RHI::TextureHandle CreatePlacedTexture(const RHI::PlacedTextureDesc& desc) override
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

        [[nodiscard]] RHI::PlacedResourceInfo GetBufferMemoryPlacement(
            RHI::BufferHandle handle) const noexcept override
        {
            const auto it = BufferPlacements.find(handle.Index);
            return it == BufferPlacements.end() ? RHI::PlacedResourceInfo{} : it->second;
        }

        [[nodiscard]] RHI::PlacedResourceInfo GetTextureMemoryPlacement(
            RHI::TextureHandle handle) const noexcept override
        {
            const auto it = TexturePlacements.find(handle.Index);
            return it == TexturePlacements.end() ? RHI::PlacedResourceInfo{} : it->second;
        }

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
