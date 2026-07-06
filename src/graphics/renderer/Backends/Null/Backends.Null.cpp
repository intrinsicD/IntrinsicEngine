module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.ResourcePool;
import Extrinsic.Core.Telemetry;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.RHI.Types;

namespace Extrinsic::Backends::Null
{
    namespace
    {
        constexpr std::uint32_t kDeviceLocalMemoryTypeBit = 1u << 0u;
        constexpr std::uint32_t kHostVisibleMemoryTypeBit = 1u << 1u;
        constexpr std::uint64_t kPlacedResourceAlignmentBytes = 256u;

        [[nodiscard]] constexpr std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) noexcept
        {
            if (alignment == 0u)
                return 0u;

            const std::uint64_t remainder = value % alignment;
            if (remainder == 0u)
                return value;

            const std::uint64_t padding = alignment - remainder;
            const std::uint64_t aligned = value + padding;
            return aligned < value ? 0u : aligned;
        }

        [[nodiscard]] constexpr bool IsAligned(std::uint64_t value, std::uint64_t alignment) noexcept
        {
            return alignment != 0u && (value % alignment) == 0u;
        }

        [[nodiscard]] constexpr std::uint32_t SelectLowestMemoryTypeBit(std::uint32_t bits) noexcept
        {
            for (std::uint32_t bit = 1u; bit != 0u; bit <<= 1u)
            {
                if ((bits & bit) != 0u)
                    return bit;
            }
            return 0u;
        }

        [[nodiscard]] constexpr bool RangeFits(std::uint64_t offset,
                                               std::uint64_t size,
                                               std::uint64_t blockSize) noexcept
        {
            return offset <= blockSize && size <= (blockSize - offset);
        }
    }

    [[nodiscard]] std::unique_ptr<RHI::IBindlessHeap> CreateNullBindlessHeap();
    [[nodiscard]] std::unique_ptr<RHI::IProfiler> CreateNullProfiler(std::uint32_t framesInFlight);
    [[nodiscard]] std::unique_ptr<RHI::ITransferQueue> CreateNullTransferQueue();

    struct BufferEntry
    {
        std::uint64_t SizeBytes = 0;
        RHI::BufferUsage Usage = RHI::BufferUsage::None;
        bool HostVisible = false;
        RHI::PlacedResourceInfo Placement{};
    };

    struct TextureEntry
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t MipLevels = 1;
        RHI::Format Fmt = RHI::Format::Undefined;
        RHI::TextureLayout Layout = RHI::TextureLayout::Undefined;
        RHI::PlacedResourceInfo Placement{};
    };

    struct MemoryBlockEntry
    {
        std::uint64_t SizeBytes = 0;
        std::uint64_t AlignmentBytes = 1;
        std::uint32_t MemoryTypeBits = 0;
        std::uint32_t SelectedMemoryTypeBit = 0;
        std::string DebugName;
    };

    struct SamplerEntry
    {
        RHI::SamplerDesc Desc{};
    };

    struct PipelineEntry
    {
        std::string DebugName;
        bool IsCompute = false;
    };

    class NullCommandContext final : public RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t,
                                      RHI::BufferHandle, std::uint64_t,
                                      std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t,
                               RHI::BufferHandle, std::uint64_t,
                               std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle,
                        std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    class NullDevice final : public RHI::IDevice
    {
    public:
        NullDevice()
            : m_TransferQueue(CreateNullTransferQueue())
            , m_BindlessHeap(CreateNullBindlessHeap())
            , m_Profiler(CreateNullProfiler(kFramesInFlight))
        {
        }

        [[nodiscard]] bool IsOperational() const noexcept override { return false; }

        void Initialize(const RHI::DeviceCreateDesc& desc) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullDevice::Initialize", Extrinsic::Core::Telemetry::HashString("NullDevice::Initialize")};
            m_BackbufferExtent = desc.InitialFramebufferExtent;
        }

        void Shutdown() override {}
        void WaitIdle() override {}

        bool BeginFrame(RHI::FrameHandle& outFrame) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullDevice::BeginFrame", Extrinsic::Core::Telemetry::HashString("NullDevice::BeginFrame")};
            outFrame.FrameIndex = m_FrameIndex++;
            outFrame.SwapchainImageIndex = 0;
            return true;
        }

        void EndFrame(const RHI::FrameHandle&) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullDevice::EndFrame", Extrinsic::Core::Telemetry::HashString("NullDevice::EndFrame")};
            m_Buffers.ProcessDeletions(m_FrameIndex);
            m_Textures.ProcessDeletions(m_FrameIndex);
            m_MemoryBlocks.ProcessDeletions(m_FrameIndex);
            m_Samplers.ProcessDeletions(m_FrameIndex);
            m_Pipelines.ProcessDeletions(m_FrameIndex);
        }

        void Present(const RHI::FrameHandle&) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullDevice::Present", Extrinsic::Core::Telemetry::HashString("NullDevice::Present")};
        }

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullDevice::Resize", Extrinsic::Core::Telemetry::HashString("NullDevice::Resize")};
            m_BackbufferExtent = {.Width = static_cast<int>(width),
                                  .Height = static_cast<int>(height)};
        }

        Core::Extent2D GetBackbufferExtent() const override { return m_BackbufferExtent; }

        void SetPresentMode(RHI::PresentMode mode) override { m_PresentMode = mode; }
        [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return m_PresentMode; }

        [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle&) const override
        {
            return RHI::TextureHandle{0, 0};
        }

        RHI::ICommandContext& GetGraphicsContext(std::uint32_t) override { return m_CommandContext; }

        [[nodiscard]] bool SupportsParallelCommandContexts() const noexcept override { return true; }

        [[nodiscard]] bool BeginFrameParallelCommandContexts(
            const RHI::FrameHandle& frame,
            const RHI::ParallelCommandContextPlanDesc& plan) override
        {
            m_ParallelCommandContextFrame = frame;
            m_ParallelCommandContextRequests.assign(plan.Requests.begin(), plan.Requests.end());
            m_ParallelCommandContexts.clear();
            m_ParallelCommandContexts.reserve(m_ParallelCommandContextRequests.size());
            for (std::size_t i = 0; i < m_ParallelCommandContextRequests.size(); ++i)
            {
                m_ParallelCommandContexts.push_back(std::make_unique<NullCommandContext>());
            }
            return !m_ParallelCommandContexts.empty();
        }

        RHI::ICommandContext& GetParallelCommandContext(
            const RHI::ParallelCommandContextRequest& request) override
        {
            if (request.ContextIndex < m_ParallelCommandContexts.size() &&
                m_ParallelCommandContexts[request.ContextIndex] != nullptr)
            {
                return *m_ParallelCommandContexts[request.ContextIndex];
            }
            return GetGraphicsContext(request.FrameIndex);
        }

        void SubmitParallelCommandContext(const RHI::ParallelCommandContextRequest& request,
                                          RHI::ICommandContext& submitContext) override
        {
            (void)submitContext;
            m_SubmittedParallelCommandContexts.push_back(request);
        }

        void EndFrameParallelCommandContexts(const RHI::FrameHandle& frame) override
        {
            (void)frame;
            m_ParallelCommandContextRequests.clear();
            m_SubmittedParallelCommandContexts.clear();
            m_ParallelCommandContexts.clear();
        }

        [[nodiscard]] RHI::BufferHandle CreateBuffer(const RHI::BufferDesc& desc) override
        {
            return m_Buffers.Add(BufferEntry{
                .SizeBytes = desc.SizeBytes,
                .Usage = desc.Usage,
                .HostVisible = desc.HostVisible,
            });
        }

        void DestroyBuffer(RHI::BufferHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Buffers.Remove(handle, m_FrameIndex);
        }

        void WriteBuffer(RHI::BufferHandle, const void*, std::uint64_t, std::uint64_t) override {}
        [[nodiscard]] std::uint64_t GetBufferDeviceAddress(RHI::BufferHandle) const override { return 0; }

        [[nodiscard]] RHI::ResourceMemoryRequirements GetBufferMemoryRequirements(
            const RHI::BufferDesc& desc) const noexcept override
        {
            if (desc.SizeBytes == 0u)
                return {};

            return RHI::ResourceMemoryRequirements{
                .SizeBytes = AlignUp(desc.SizeBytes, kPlacedResourceAlignmentBytes),
                .AlignmentBytes = kPlacedResourceAlignmentBytes,
                .MemoryTypeBits = desc.HostVisible ? kHostVisibleMemoryTypeBit : kDeviceLocalMemoryTypeBit,
                .DedicatedAllocationRequired = false,
            };
        }

        [[nodiscard]] RHI::MemoryBlockHandle CreateMemoryBlock(const RHI::MemoryBlockDesc& desc) override
        {
            const std::uint32_t selectedBit = SelectLowestMemoryTypeBit(desc.MemoryTypeBits);
            if (desc.SizeBytes == 0u || desc.AlignmentBytes == 0u || selectedBit == 0u)
                return {};

            return m_MemoryBlocks.Add(MemoryBlockEntry{
                .SizeBytes = desc.SizeBytes,
                .AlignmentBytes = desc.AlignmentBytes,
                .MemoryTypeBits = desc.MemoryTypeBits,
                .SelectedMemoryTypeBit = selectedBit,
                .DebugName = desc.DebugName ? desc.DebugName : "",
            });
        }

        void DestroyMemoryBlock(RHI::MemoryBlockHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_MemoryBlocks.Remove(handle, m_FrameIndex);
        }

        [[nodiscard]] RHI::MemoryBlockInfo GetMemoryBlockInfo(RHI::MemoryBlockHandle handle) const noexcept override
        {
            const MemoryBlockEntry* block = m_MemoryBlocks.GetIfValid(handle);
            if (block == nullptr)
                return {};

            return RHI::MemoryBlockInfo{
                .SizeBytes = block->SizeBytes,
                .AlignmentBytes = block->AlignmentBytes,
                .MemoryTypeBits = block->MemoryTypeBits,
                .SelectedMemoryTypeBit = block->SelectedMemoryTypeBit,
                .IsValid = true,
            };
        }

        [[nodiscard]] RHI::BufferHandle CreatePlacedBuffer(const RHI::PlacedBufferDesc& placedDesc) override
        {
            const RHI::ResourceMemoryRequirements requirements =
                GetBufferMemoryRequirements(placedDesc.Desc);
            const RHI::PlacedResourceInfo placement =
                ValidatePlacedResource(placedDesc.Placement, requirements);
            if (!placement.IsPlaced)
                return {};

            return m_Buffers.Add(BufferEntry{
                .SizeBytes = placedDesc.Desc.SizeBytes,
                .Usage = placedDesc.Desc.Usage,
                .HostVisible = placedDesc.Desc.HostVisible,
                .Placement = placement,
            });
        }

        [[nodiscard]] RHI::PlacedResourceInfo GetBufferMemoryPlacement(
            RHI::BufferHandle handle) const noexcept override
        {
            const BufferEntry* buffer = m_Buffers.GetIfValid(handle);
            return buffer != nullptr ? buffer->Placement : RHI::PlacedResourceInfo{};
        }

        [[nodiscard]] RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override
        {
            return m_Textures.Add(TextureEntry{
                .Width = desc.Width,
                .Height = desc.Height,
                .MipLevels = desc.MipLevels,
                .Fmt = desc.Fmt,
                .Layout = desc.InitialLayout,
            });
        }

        void DestroyTexture(RHI::TextureHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Textures.Remove(handle, m_FrameIndex);
        }

        void WriteTexture(RHI::TextureHandle, const void*, std::uint64_t, std::uint32_t, std::uint32_t) override {}

        [[nodiscard]] RHI::ResourceMemoryRequirements GetTextureMemoryRequirements(
            const RHI::TextureDesc& desc) const noexcept override
        {
            const std::uint64_t storageBytes = RHI::EstimateTextureStorageBytes(desc);
            const std::uint64_t alignedBytes = AlignUp(storageBytes, kPlacedResourceAlignmentBytes);
            if (alignedBytes == 0u)
                return {};

            return RHI::ResourceMemoryRequirements{
                .SizeBytes = alignedBytes,
                .AlignmentBytes = kPlacedResourceAlignmentBytes,
                .MemoryTypeBits = kDeviceLocalMemoryTypeBit,
                .DedicatedAllocationRequired = false,
            };
        }

        [[nodiscard]] RHI::TextureHandle CreatePlacedTexture(const RHI::PlacedTextureDesc& placedDesc) override
        {
            const RHI::ResourceMemoryRequirements requirements =
                GetTextureMemoryRequirements(placedDesc.Desc);
            const RHI::PlacedResourceInfo placement =
                ValidatePlacedResource(placedDesc.Placement, requirements);
            if (!placement.IsPlaced)
                return {};

            return m_Textures.Add(TextureEntry{
                .Width = placedDesc.Desc.Width,
                .Height = placedDesc.Desc.Height,
                .MipLevels = placedDesc.Desc.MipLevels,
                .Fmt = placedDesc.Desc.Fmt,
                .Layout = placedDesc.Desc.InitialLayout,
                .Placement = placement,
            });
        }

        [[nodiscard]] RHI::PlacedResourceInfo GetTextureMemoryPlacement(
            RHI::TextureHandle handle) const noexcept override
        {
            const TextureEntry* texture = m_Textures.GetIfValid(handle);
            return texture != nullptr ? texture->Placement : RHI::PlacedResourceInfo{};
        }

        [[nodiscard]] RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc& desc) override
        {
            return m_Samplers.Add(SamplerEntry{.Desc = desc});
        }

        void DestroySampler(RHI::SamplerHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Samplers.Remove(handle, m_FrameIndex);
        }

        [[nodiscard]] RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc& desc) override
        {
            return m_Pipelines.Add(PipelineEntry{
                .DebugName = desc.DebugName ? desc.DebugName : "",
                .IsCompute = !desc.ComputeShaderPath.empty(),
            });
        }

        void DestroyPipeline(RHI::PipelineHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Pipelines.Remove(handle, m_FrameIndex);
        }

        [[nodiscard]] RHI::ITransferQueue& GetTransferQueue() override { return *m_TransferQueue; }
        [[nodiscard]] RHI::IBindlessHeap& GetBindlessHeap() override { return *m_BindlessHeap; }
        [[nodiscard]] RHI::IProfiler* GetProfiler() override { return m_Profiler.get(); }
        [[nodiscard]] std::uint32_t GetFramesInFlight() const override { return kFramesInFlight; }
        [[nodiscard]] std::uint64_t GetGlobalFrameNumber() const override { return m_FrameIndex; }

    private:
        static constexpr std::uint32_t kFramesInFlight = 2;

        [[nodiscard]] RHI::PlacedResourceInfo ValidatePlacedResource(
            const RHI::PlacedResourceBinding& binding,
            const RHI::ResourceMemoryRequirements& requirements) const noexcept
        {
            if (!binding.Block.IsValid() || !requirements.IsValid())
                return {};

            const MemoryBlockEntry* block = m_MemoryBlocks.GetIfValid(binding.Block);
            if (block == nullptr)
                return {};
            if ((block->SelectedMemoryTypeBit & requirements.MemoryTypeBits) == 0u)
                return {};
            if (!IsAligned(binding.OffsetBytes, requirements.AlignmentBytes))
                return {};
            if (!RangeFits(binding.OffsetBytes, requirements.SizeBytes, block->SizeBytes))
                return {};

            return RHI::PlacedResourceInfo{
                .Block = binding.Block,
                .OffsetBytes = binding.OffsetBytes,
                .SizeBytes = requirements.SizeBytes,
                .AlignmentBytes = requirements.AlignmentBytes,
                .MemoryTypeBit = block->SelectedMemoryTypeBit,
                .IsPlaced = true,
            };
        }

        std::uint32_t m_FrameIndex{0};
        RHI::PresentMode m_PresentMode{RHI::PresentMode::VSync};
        Core::Extent2D m_BackbufferExtent{};
        NullCommandContext m_CommandContext{};

        std::unique_ptr<RHI::ITransferQueue> m_TransferQueue;
        std::unique_ptr<RHI::IBindlessHeap> m_BindlessHeap;
        std::unique_ptr<RHI::IProfiler> m_Profiler;
        RHI::FrameHandle m_ParallelCommandContextFrame{};
        std::vector<RHI::ParallelCommandContextRequest> m_ParallelCommandContextRequests;
        std::vector<RHI::ParallelCommandContextRequest> m_SubmittedParallelCommandContexts;
        std::vector<std::unique_ptr<NullCommandContext>> m_ParallelCommandContexts;

        Core::ResourcePool<BufferEntry, RHI::BufferHandle, 0> m_Buffers;
        Core::ResourcePool<TextureEntry, RHI::TextureHandle, 0> m_Textures;
        Core::ResourcePool<MemoryBlockEntry, RHI::MemoryBlockHandle, 0> m_MemoryBlocks;
        Core::ResourcePool<SamplerEntry, RHI::SamplerHandle, 0> m_Samplers;
        Core::ResourcePool<PipelineEntry, RHI::PipelineHandle, 0> m_Pipelines;
    };

    std::unique_ptr<RHI::IDevice> CreateNullDevice()
    {
        return std::make_unique<NullDevice>();
    }
}
