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
#include <string_view>
#include <utility>
#include <vector>

namespace Extrinsic::Tests
{
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

        [[nodiscard]] RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle,
                                                             RHI::SamplerHandle) override
        {
            ++AllocateCalls;
            return ++m_NextSlot;
        }
        void UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle,
                               RHI::SamplerHandle) override { ++UpdateCalls; }
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
        void Begin() override {}
        void End()   override {}
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
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle,
                                      std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle,
                               std::uint64_t, std::uint32_t) override {}
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

        // ---- Knobs ---------------------------------------------------------
        bool Operational            = true;
        bool FailNextBufferCreate   = false;
        bool FailNextTextureCreate  = false;
        bool FailNextSamplerCreate  = false;
        bool FailNextPipelineCreate = false;

        // ---- Counters ------------------------------------------------------
        int CreateBufferCount    = 0;
        int DestroyBufferCount   = 0;
        int CreateTextureCount   = 0;
        int DestroyTextureCount  = 0;
        int CreateSamplerCount   = 0;
        int DestroySamplerCount  = 0;
        int CreatePipelineCount  = 0;
        int DestroyPipelineCount = 0;
        std::vector<BufferWriteRecord> BufferWrites;

        MockBindlessHeap   Bindless;
        MockCommandContext CommandContext;

        // ---- IDevice -------------------------------------------------------
        [[nodiscard]] bool IsOperational() const noexcept override { return Operational; }

        void Initialize(Platform::IWindow&, const Core::Config::RenderConfig&) override {}
        void Shutdown() override {}
        void WaitIdle() override {}

        bool BeginFrame(RHI::FrameHandle& out) override { out = {}; return true; }
        void EndFrame(const RHI::FrameHandle&) override {}
        void Present(const RHI::FrameHandle&) override {}
        void Resize(std::uint32_t, std::uint32_t) override {}
        Platform::Extent2D GetBackbufferExtent() const override { return {}; }

        RHI::ICommandContext& GetGraphicsContext(std::uint32_t) override { return CommandContext; }

        RHI::BufferHandle CreateBuffer(const RHI::BufferDesc&) override
        {
            ++CreateBufferCount;
            if (FailNextBufferCreate) { FailNextBufferCreate = false; return {}; }
            return RHI::BufferHandle{m_NextBuffer++, 1u};
        }
        void DestroyBuffer(RHI::BufferHandle) override { ++DestroyBufferCount; }
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
        [[nodiscard]] std::uint64_t GetBufferDeviceAddress(RHI::BufferHandle) const override { return 0; }

        RHI::TextureHandle CreateTexture(const RHI::TextureDesc&) override
        {
            ++CreateTextureCount;
            if (FailNextTextureCreate) { FailNextTextureCreate = false; return {}; }
            return RHI::TextureHandle{m_NextTexture++, 1u};
        }
        void DestroyTexture(RHI::TextureHandle) override { ++DestroyTextureCount; }
        void WriteTexture(RHI::TextureHandle, const void*, std::uint64_t,
                          std::uint32_t, std::uint32_t) override {}

        RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc&) override
        {
            ++CreateSamplerCount;
            if (FailNextSamplerCreate) { FailNextSamplerCreate = false; return {}; }
            return RHI::SamplerHandle{m_NextSampler++, 1u};
        }
        void DestroySampler(RHI::SamplerHandle) override { ++DestroySamplerCount; }

        RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc&) override
        {
            ++CreatePipelineCount;
            if (FailNextPipelineCreate) { FailNextPipelineCreate = false; return {}; }
            return RHI::PipelineHandle{m_NextPipeline++, 1u};
        }
        void DestroyPipeline(RHI::PipelineHandle) override { ++DestroyPipelineCount; }

        RHI::TransferToken UploadBuffer(RHI::BufferHandle, const void*,
                                        std::uint64_t, std::uint64_t) override { return RHI::TransferToken{0}; }
        RHI::TransferToken UploadTexture(RHI::TextureHandle, const void*, std::uint64_t,
                                         std::uint32_t, std::uint32_t) override { return RHI::TransferToken{0}; }
        [[nodiscard]] bool IsTransferComplete(RHI::TransferToken) const override { return true; }
        void CollectCompletedTransfers() override {}

        RHI::IBindlessHeap& GetBindlessHeap() override { return Bindless; }
        RHI::IProfiler* GetProfiler() override { return nullptr; }

        [[nodiscard]] std::uint32_t GetFramesInFlight()    const override { return 2; }
        [[nodiscard]] std::uint64_t GetGlobalFrameNumber() const override { return 0; }

    private:
        std::uint32_t m_NextBuffer   = 1; // 0 is reserved / invalid
        std::uint32_t m_NextTexture  = 1;
        std::uint32_t m_NextSampler  = 1;
        std::uint32_t m_NextPipeline = 1;
    };
}
