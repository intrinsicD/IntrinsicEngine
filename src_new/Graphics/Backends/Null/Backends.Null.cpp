module;

#include <memory>
#include <string>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Config.Render;
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
import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Null
{
    [[nodiscard]] std::unique_ptr<RHI::IBindlessHeap> CreateNullBindlessHeap();
    [[nodiscard]] std::unique_ptr<RHI::IProfiler> CreateNullProfiler(std::uint32_t framesInFlight);
    [[nodiscard]] std::unique_ptr<RHI::ITransferQueue> CreateNullTransferQueue();

    struct BufferEntry
    {
        std::uint64_t SizeBytes = 0;
        RHI::BufferUsage Usage = RHI::BufferUsage::None;
        bool HostVisible = false;
    };

    struct TextureEntry
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t MipLevels = 1;
        RHI::Format Fmt = RHI::Format::Undefined;
        RHI::TextureLayout Layout = RHI::TextureLayout::Undefined;
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

        void Initialize(Platform::IWindow& window,
                        const Core::Config::RenderConfig& config) override
        {
            [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"NullDevice::Initialize", Extrinsic::Core::Telemetry::HashString("NullDevice::Initialize")};
            (void)config;
            m_BackbufferExtent = window.GetFramebufferExtent();
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

        Platform::Extent2D GetBackbufferExtent() const override { return m_BackbufferExtent; }

        void SetPresentMode(RHI::PresentMode mode) override { m_PresentMode = mode; }
        [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return m_PresentMode; }

        [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle&) const override
        {
            return RHI::TextureHandle{0, 0};
        }

        RHI::ICommandContext& GetGraphicsContext(std::uint32_t) override { return m_CommandContext; }

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

        std::uint32_t m_FrameIndex{0};
        RHI::PresentMode m_PresentMode{RHI::PresentMode::VSync};
        Platform::Extent2D m_BackbufferExtent{};
        NullCommandContext m_CommandContext{};

        std::unique_ptr<RHI::ITransferQueue> m_TransferQueue;
        std::unique_ptr<RHI::IBindlessHeap> m_BindlessHeap;
        std::unique_ptr<RHI::IProfiler> m_Profiler;

        Core::ResourcePool<BufferEntry, RHI::BufferHandle, 0> m_Buffers;
        Core::ResourcePool<TextureEntry, RHI::TextureHandle, 0> m_Textures;
        Core::ResourcePool<SamplerEntry, RHI::SamplerHandle, 0> m_Samplers;
        Core::ResourcePool<PipelineEntry, RHI::PipelineHandle, 0> m_Pipelines;
    };

    std::unique_ptr<RHI::IDevice> CreateNullDevice()
    {
        return std::make_unique<NullDevice>();
    }
}
