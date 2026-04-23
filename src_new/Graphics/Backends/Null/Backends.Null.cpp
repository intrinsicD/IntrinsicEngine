module;

// Null backend — stub IDevice implementation with no GPU calls. Kept
// here as a compile-time scaffold and test fixture. When a real Vulkan
// backend arrives it should live alongside this as Extrinsic.Backends.Vulkan,
// at which point the global module fragment below will grow the actual
// vulkan.h / vk_mem_alloc.h includes.
#include <cstdint>
#include <memory>
#include <vector>
#include <expected>
#include <string>
#include <string_view>
#include <cassert>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Config.Render;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;  // all typed resource handles
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Null
{
    template<typename T>
    class ResourcePool
    {
    public:
        std::pair<std::uint32_t, std::uint32_t> Allocate()
        {
            if (!m_FreeList.empty())
            {
                const std::uint32_t idx = m_FreeList.back();
                m_FreeList.pop_back();
                m_Alive[idx] = true;
                return {idx, m_Generations[idx]};
            }
            const std::uint32_t idx = static_cast<std::uint32_t>(m_Data.size());
            m_Data.emplace_back();
            m_Generations.push_back(0);
            m_Alive.push_back(true);
            return {idx, 0};
        }

        void Free(std::uint32_t index, std::uint32_t generation)
        {
            if (index >= m_Data.size() || !m_Alive[index] || m_Generations[index] != generation)
                return;
            m_Alive[index] = false;
            ++m_Generations[index];
            m_Data[index] = T{};
            m_FreeList.push_back(index);
        }

        [[nodiscard]] T* Get(std::uint32_t index, std::uint32_t generation)
        {
            if (index >= m_Data.size() || !m_Alive[index] || m_Generations[index] != generation)
                return nullptr;
            return &m_Data[index];
        }

    private:
        std::vector<T>             m_Data;
        std::vector<std::uint32_t> m_Generations;
        std::vector<bool>          m_Alive;
        std::vector<std::uint32_t> m_FreeList;
    };

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
        void BeginRenderPass(RHI::TextureHandle, RHI::TextureHandle) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}
        void BindVertexBuffer(std::uint32_t, RHI::BufferHandle, std::uint64_t) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, bool) override {}
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
        void BufferBarrier(RHI::BufferHandle) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle,
                        std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
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

        void EndFrame(const RHI::FrameHandle&) override {}
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

        RHI::ICommandContext& GetGraphicsContext(std::uint32_t) override
        {
            return m_CommandContext;
        }

        [[nodiscard]] RHI::BufferHandle CreateBuffer(const RHI::BufferDesc& desc) override
        {
            auto [idx, gen] = m_Buffers.Allocate();
            BufferEntry* e = m_Buffers.Get(idx, gen);
            e->SizeBytes = desc.SizeBytes;
            e->Usage = desc.Usage;
            e->HostVisible = desc.HostVisible;
            return RHI::BufferHandle{idx, gen};
        }

        void DestroyBuffer(RHI::BufferHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Buffers.Free(handle.Index, handle.Generation);
        }

        void WriteBuffer(RHI::BufferHandle, const void*, std::uint64_t, std::uint64_t) override {}

        [[nodiscard]] std::uint64_t GetBufferDeviceAddress(RHI::BufferHandle) const override
        {
            return 0;
        }

        [[nodiscard]] RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override
        {
            auto [idx, gen] = m_Textures.Allocate();
            TextureEntry* e = m_Textures.Get(idx, gen);
            e->Width = desc.Width;
            e->Height = desc.Height;
            e->MipLevels = desc.MipLevels;
            e->Fmt = desc.Fmt;
            e->Layout = desc.InitialLayout;
            return RHI::TextureHandle{idx, gen};
        }

        void DestroyTexture(RHI::TextureHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Textures.Free(handle.Index, handle.Generation);
        }

        void WriteTexture(RHI::TextureHandle, const void*, std::uint64_t, std::uint32_t, std::uint32_t) override {}

        [[nodiscard]] RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc& desc) override
        {
            auto [idx, gen] = m_Samplers.Allocate();
            SamplerEntry* e = m_Samplers.Get(idx, gen);
            e->Desc = desc;
            return RHI::SamplerHandle{idx, gen};
        }

        void DestroySampler(RHI::SamplerHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Samplers.Free(handle.Index, handle.Generation);
        }

        [[nodiscard]] RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc& desc) override
        {
            auto [idx, gen] = m_Pipelines.Allocate();
            PipelineEntry* e = m_Pipelines.Get(idx, gen);
            e->IsCompute = !desc.ComputeShaderPath.empty();
            e->DebugName = desc.DebugName ? desc.DebugName : "";
            return RHI::PipelineHandle{idx, gen};
        }

        void DestroyPipeline(RHI::PipelineHandle handle) override
        {
            if (!handle.IsValid()) return;
            m_Pipelines.Free(handle.Index, handle.Generation);
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      const void*,
                                                      std::uint64_t,
                                                      std::uint64_t) override
        {
            m_LastCompletedTransferValue = ++m_NextTransferValue;
            return RHI::TransferToken{m_NextTransferValue};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                       const void*,
                                                       std::uint64_t,
                                                       std::uint32_t,
                                                       std::uint32_t) override
        {
            m_LastCompletedTransferValue = ++m_NextTransferValue;
            return RHI::TransferToken{m_NextTransferValue};
        }

        [[nodiscard]] bool IsTransferComplete(RHI::TransferToken token) const override
        {
            return token.Value <= m_LastCompletedTransferValue;
        }

        void CollectCompletedTransfers() override
        {
            m_LastCompletedTransferValue = m_NextTransferValue;
        }

        [[nodiscard]] RHI::IBindlessHeap& GetBindlessHeap() override { return m_BindlessHeap; }
        [[nodiscard]] RHI::IProfiler* GetProfiler() override { return &m_Profiler; }
        [[nodiscard]] std::uint32_t GetFramesInFlight() const override { return kFramesInFlight; }
        [[nodiscard]] std::uint64_t GetGlobalFrameNumber() const override { return m_FrameIndex; }

    private:
        static constexpr std::uint32_t kFramesInFlight = 2;

        std::uint32_t m_FrameIndex{0};
        std::uint64_t m_NextTransferValue{0};
        std::uint64_t m_LastCompletedTransferValue{0};
        Platform::Extent2D m_BackbufferExtent{};
        NullCommandContext m_CommandContext{};
        NullBindlessHeap m_BindlessHeap{};
        NullProfiler m_Profiler{};

        ResourcePool<BufferEntry> m_Buffers;
        ResourcePool<TextureEntry> m_Textures;
        ResourcePool<SamplerEntry> m_Samplers;
        ResourcePool<PipelineEntry> m_Pipelines;
    };

    std::unique_ptr<RHI::IDevice> CreateNullDevice()
    {
        return std::make_unique<NullDevice>();
    }
}
