module;

// Null backend — stub IDevice implementation with no GPU calls. Kept
// here as a compile-time scaffold and test fixture. When a real Vulkan
// backend arrives it should live alongside this as Extrinsic.Backends.Vulkan,
// at which point the global module fragment below will grow the actual
// vulkan.h / vk_mem_alloc.h includes.
#include <atomic>
#include <memory>
#include <span>
#include <vector>
#include <expected>
#include <string>
#include <string_view>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.ResourcePool;
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

    struct BufferEntry
    {
        std::uint64_t        SizeBytes   = 0;
        RHI::BufferUsage     Usage       = RHI::BufferUsage::None;
        bool                 HostVisible = false;
    };

    struct TextureEntry
    {
        std::uint32_t        Width      = 0;
        std::uint32_t        Height     = 0;
        std::uint32_t        MipLevels  = 1;
        RHI::Format          Fmt        = RHI::Format::Undefined;
        RHI::TextureLayout   Layout     = RHI::TextureLayout::Undefined;
    };

    struct SamplerEntry
    {
        RHI::SamplerDesc Desc{};
    };

    struct PipelineEntry
    {
        std::string DebugName;
        bool        IsCompute = false;
    };

    class NullBindlessHeap final : public RHI::IBindlessHeap
    {
    public:
        [[nodiscard]] RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle) override
        {
            std::scoped_lock lock{m_Mutex};

            RHI::BindlessIndex slot = RHI::kInvalidBindlessIndex;
            if (!m_FreeSlots.empty())
            {
                slot = m_FreeSlots.back();
                m_FreeSlots.pop_back();
            }
            else
            {
                if (m_NextSlot >= kCapacity)
                    return RHI::kInvalidBindlessIndex;
                slot = m_NextSlot++;
            }

            m_PendingOps.push_back(PendingOp{PendingOpType::Allocate, slot, {}, {}});
            return slot;
        }

        void UpdateTextureSlot(RHI::BindlessIndex slot, RHI::TextureHandle texture, RHI::SamplerHandle sampler) override
        {
            std::scoped_lock lock{m_Mutex};
            if (slot == RHI::kInvalidBindlessIndex || slot >= m_NextSlot)
                return;
            m_PendingOps.push_back(PendingOp{PendingOpType::Update, slot, texture, sampler});
        }

        void FreeSlot(RHI::BindlessIndex slot) override
        {
            std::scoped_lock lock{m_Mutex};
            if (slot == RHI::kInvalidBindlessIndex || slot >= m_NextSlot)
                return;
            m_PendingOps.push_back(PendingOp{PendingOpType::Free, slot, {}, {}});
        }

        void FlushPending() override
        {
            std::scoped_lock lock{m_Mutex};
            for (const PendingOp& op : m_PendingOps)
            {
                switch (op.Type)
                {
                case PendingOpType::Allocate:
                    if (const auto [_, inserted] = m_AllocatedSlots.emplace(op.Slot); inserted)
                        ++m_AllocatedSlotCount;
                    break;
                case PendingOpType::Update:
                    if (m_AllocatedSlots.contains(op.Slot))
                        m_Bindings[op.Slot] = SlotBinding{op.Texture, op.Sampler};
                    break;
                case PendingOpType::Free:
                    if (m_AllocatedSlots.erase(op.Slot) > 0)
                    {
                        m_Bindings.erase(op.Slot);
                        m_FreeSlots.push_back(op.Slot);
                        if (m_AllocatedSlotCount > 0)
                            --m_AllocatedSlotCount;
                    }
                    break;
                }
            }
            m_PendingOps.clear();
        }

        [[nodiscard]] std::uint32_t GetCapacity() const override { return kCapacity; }
        [[nodiscard]] std::uint32_t GetAllocatedSlotCount() const override { return m_AllocatedSlotCount; }

    private:
        static constexpr std::uint32_t kCapacity = 65536;

        enum class PendingOpType : std::uint8_t { Allocate, Update, Free };

        struct SlotBinding
        {
            RHI::TextureHandle Texture{};
            RHI::SamplerHandle Sampler{};
        };

        struct PendingOp
        {
            PendingOpType      Type{};
            RHI::BindlessIndex Slot{};
            RHI::TextureHandle Texture{};
            RHI::SamplerHandle Sampler{};
        };

        mutable std::mutex m_Mutex;
        std::uint32_t m_NextSlot = 1;
        std::uint32_t m_AllocatedSlotCount = 0;
        std::vector<RHI::BindlessIndex> m_FreeSlots;
        std::vector<PendingOp> m_PendingOps;
        std::unordered_set<RHI::BindlessIndex> m_AllocatedSlots;
        std::unordered_map<RHI::BindlessIndex, SlotBinding> m_Bindings;
    };

    class NullProfiler final : public RHI::IProfiler
    {
    public:
        void BeginFrame(std::uint32_t frameIndex, std::uint32_t maxScopesHint) override
        {
            std::scoped_lock lock{m_Mutex};
            m_ActiveFrame.FrameIndex = frameIndex;
            m_ActiveFrame.FrameStart = Clock::now();
            m_ActiveFrame.Scopes.clear();
            m_ActiveFrame.Scopes.reserve(maxScopesHint);
            m_FrameOpen = true;
        }

        void EndFrame() override
        {
            std::scoped_lock lock{m_Mutex};
            if (!m_FrameOpen)
                return;

            const auto now = Clock::now();
            RHI::GpuTimestampFrame resolved{};
            resolved.FrameNumber = m_ActiveFrame.FrameIndex;
            resolved.CpuSubmitTimeNs = ToNs(now.time_since_epoch());
            resolved.GpuFrameTimeNs = ToNs(now - m_ActiveFrame.FrameStart);

            resolved.Scopes.reserve(m_ActiveFrame.Scopes.size());
            for (const ScopeState& scope : m_ActiveFrame.Scopes)
            {
                RHI::GpuTimestampScope out{};
                out.Name = scope.Name;
                out.DurationNs = scope.Ended ? ToNs(scope.EndTime - scope.StartTime) : 0;
                resolved.Scopes.push_back(std::move(out));
            }

            m_ResolvedFrames[resolved.FrameNumber] = std::move(resolved);
            m_FrameOpen = false;
        }

        [[nodiscard]] std::uint32_t BeginScope(std::string_view name) override
        {
            std::scoped_lock lock{m_Mutex};
            if (!m_FrameOpen)
                return 0;

            ScopeState scope{};
            scope.Name = std::string(name);
            scope.StartTime = Clock::now();
            m_ActiveFrame.Scopes.push_back(std::move(scope));
            return static_cast<std::uint32_t>(m_ActiveFrame.Scopes.size() - 1);
        }

        void EndScope(std::uint32_t scopeHandle) override
        {
            std::scoped_lock lock{m_Mutex};
            if (!m_FrameOpen || scopeHandle >= m_ActiveFrame.Scopes.size())
                return;
            ScopeState& scope = m_ActiveFrame.Scopes[scopeHandle];
            scope.EndTime = Clock::now();
            scope.Ended = true;
        }

        [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
        Resolve(std::uint32_t frameIndex) const override
        {
            std::scoped_lock lock{m_Mutex};
            const auto it = m_ResolvedFrames.find(frameIndex);
            if (it == m_ResolvedFrames.end())
                return std::unexpected(RHI::ProfilerError::NotReady);
            return it->second;
        }

        [[nodiscard]] std::uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }

    private:
        using Clock = std::chrono::steady_clock;

        static std::uint64_t ToNs(Clock::duration duration)
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
        }

        struct ScopeState
        {
            std::string Name{};
            Clock::time_point StartTime{};
            Clock::time_point EndTime{};
            bool Ended = false;
        };

        struct ActiveFrameState
        {
            std::uint32_t FrameIndex = 0;
            Clock::time_point FrameStart{};
            std::vector<ScopeState> Scopes{};
        };

        mutable std::mutex m_Mutex;
        ActiveFrameState m_ActiveFrame{};
        std::unordered_map<std::uint32_t, RHI::GpuTimestampFrame> m_ResolvedFrames;
        bool m_FrameOpen = false;
        std::uint32_t m_FramesInFlight = 2;
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
        // BDA-only: BindVertexBuffer / BindIndexBuffer removed.
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t,
                                      RHI::BufferHandle, std::uint64_t,
                                      std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle,
                        std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    // ---- Null transfer queue ----------------------------------------
    // Immediately marks every upload as complete — no GPU work occurs.
    class NullTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      const void*,
                                                      std::uint64_t,
                                                      std::uint64_t) override
        {
            return RHI::TransferToken{++m_Next};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      std::span<const std::byte>,
                                                      std::uint64_t) override
        {
            return RHI::TransferToken{++m_Next};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                       const void*,
                                                       std::uint64_t,
                                                       std::uint32_t,
                                                       std::uint32_t) override
        {
            return RHI::TransferToken{++m_Next};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override
        {
            return token.Value <= m_Next.load(std::memory_order_relaxed);
        }

        void CollectCompleted() override {}

    private:
        std::atomic<std::uint64_t> m_Next{0};
    };

    class NullDevice final : public RHI::IDevice
    {
    public:
        [[nodiscard]] bool IsOperational() const noexcept override { return false; }

        void Initialize(Platform::IWindow& window,
                        const Core::Config::RenderConfig& config) override
        {
            (void)config;
            m_BackbufferExtent = window.GetFramebufferExtent();
        }

        void Shutdown() override {}
        void WaitIdle() override {}

        bool BeginFrame(RHI::FrameHandle& outFrame) override
        {
            outFrame.FrameIndex = m_FrameIndex++;
            outFrame.SwapchainImageIndex = 0;
            return true;
        }

        void EndFrame(const RHI::FrameHandle&) override
        {
            m_Buffers.ProcessDeletions(m_FrameIndex);
            m_Textures.ProcessDeletions(m_FrameIndex);
            m_Samplers.ProcessDeletions(m_FrameIndex);
            m_Pipelines.ProcessDeletions(m_FrameIndex);
        }
        void Present(const RHI::FrameHandle&) override {}

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            m_BackbufferExtent = {.Width = static_cast<int>(width),
                                  .Height = static_cast<int>(height)};
        }

        Platform::Extent2D GetBackbufferExtent() const override
        {
            return m_BackbufferExtent;
        }

        void SetPresentMode(RHI::PresentMode mode) override { m_PresentMode = mode; }
        [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return m_PresentMode; }

        [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle&) const override
        {
            // Null backend has no real swapchain — return a fixed sentinel handle
            // (index=0, generation=0) that the Null command context silently ignores.
            return RHI::TextureHandle{0, 0};
        }

        RHI::ICommandContext& GetGraphicsContext(std::uint32_t) override
        {
            return m_CommandContext;
        }

        [[nodiscard]] RHI::BufferHandle CreateBuffer(const RHI::BufferDesc& desc) override
        {
            return m_Buffers.Add(BufferEntry{
                .SizeBytes   = desc.SizeBytes,
                .Usage       = desc.Usage,
                .HostVisible = desc.HostVisible,
            });
        }

        void DestroyBuffer(RHI::BufferHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Buffers.Remove(handle, m_FrameIndex);
        }

        void WriteBuffer(RHI::BufferHandle, const void*, std::uint64_t, std::uint64_t) override {}

        [[nodiscard]] std::uint64_t GetBufferDeviceAddress(RHI::BufferHandle) const override
        {
            return 0;
        }

        [[nodiscard]] RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override
        {
            return m_Textures.Add(TextureEntry{
                .Width     = desc.Width,
                .Height    = desc.Height,
                .MipLevels = desc.MipLevels,
                .Fmt       = desc.Fmt,
                .Layout    = desc.InitialLayout,
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

        [[nodiscard]] RHI::ITransferQueue& GetTransferQueue() override { return m_TransferQueue; }

        [[nodiscard]] RHI::IBindlessHeap& GetBindlessHeap() override { return m_BindlessHeap; }
        [[nodiscard]] RHI::IProfiler* GetProfiler() override { return &m_Profiler; }
        [[nodiscard]] std::uint32_t GetFramesInFlight() const override { return kFramesInFlight; }
        [[nodiscard]] std::uint64_t GetGlobalFrameNumber() const override { return m_FrameIndex; }

    private:
        static constexpr std::uint32_t kFramesInFlight = 2;

        std::uint32_t      m_FrameIndex{0};
        RHI::PresentMode   m_PresentMode{RHI::PresentMode::VSync};
        Platform::Extent2D m_BackbufferExtent{};
        NullCommandContext m_CommandContext{};
        NullTransferQueue  m_TransferQueue{};
        NullBindlessHeap   m_BindlessHeap{};
        NullProfiler       m_Profiler{};

        Core::ResourcePool<BufferEntry,   RHI::BufferHandle,   0> m_Buffers;
        Core::ResourcePool<TextureEntry,  RHI::TextureHandle,  0> m_Textures;
        Core::ResourcePool<SamplerEntry,  RHI::SamplerHandle,  0> m_Samplers;
        Core::ResourcePool<PipelineEntry, RHI::PipelineHandle, 0> m_Pipelines;
    };

    std::unique_ptr<RHI::IDevice> CreateNullDevice()
    {
        return std::make_unique<NullDevice>();
    }
}
