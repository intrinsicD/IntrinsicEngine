#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    // A controllable transfer queue: tests can flip a flag to keep tokens
    // pending and observe the GpuUploading state without it auto-completing.
    class ControllableTransferQueue final : public RHI::ITransferQueue
    {
    public:
        bool                          AlwaysComplete = true;
        std::vector<RHI::TransferToken> Issued{};

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                     const void*,
                                                     std::uint64_t,
                                                     std::uint64_t = 0) override
        {
            RHI::TransferToken t{++m_Counter};
            Issued.push_back(t);
            return t;
        }
        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                     std::span<const std::byte>,
                                                     std::uint64_t = 0) override
        {
            RHI::TransferToken t{++m_Counter};
            Issued.push_back(t);
            return t;
        }
        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                      const void*,
                                                      std::uint64_t,
                                                      std::uint32_t = 0,
                                                      std::uint32_t = 0) override
        {
            RHI::TransferToken t{++m_Counter};
            Issued.push_back(t);
            return t;
        }
        [[nodiscard]] bool IsComplete(RHI::TransferToken) const override
        {
            return AlwaysComplete;
        }
        void CollectCompleted() override {}

    private:
        std::uint64_t m_Counter = 0;
    };

    // Convenient fixture-style helpers — keep tests readable.
    struct CacheFixture
    {
        MockDevice                  Device;
        RHI::BufferManager          BufferMgr;
        RHI::TextureManager         TextureMgr;
        ControllableTransferQueue   Transfer;
        Graphics::GpuAssetCache     Cache;

        CacheFixture()
            : Device()
            , BufferMgr(Device)
            , TextureMgr(Device, Device.Bindless)
            , Transfer()
            , Cache(BufferMgr, TextureMgr, Transfer)
        {
        }
    };

    Assets::AssetId MakeAssetId(std::uint32_t index, std::uint32_t generation = 1u)
    {
        // Asset IDs are Core::StrongHandle<AssetTag>; the public type
        // exposes index+generation construction via the StrongHandle ctor.
        return Assets::AssetId{index, generation};
    }

    RHI::BufferDesc AnyBufferDesc()
    {
        return RHI::BufferDesc{
            .SizeBytes = 64,
            .Usage     = RHI::BufferUsage::Vertex | RHI::BufferUsage::TransferDst,
            .DebugName = "test-buffer",
        };
    }

    RHI::TextureDesc AnyTextureDesc()
    {
        return RHI::TextureDesc{
            .Width  = 8,
            .Height = 8,
            .MipLevels = 1,
            .Fmt    = RHI::Format::RGBA8_UNORM,
            .Usage  = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
            .DebugName = "test-texture",
        };
    }

    RHI::SamplerHandle AnySampler() { return RHI::SamplerHandle{42u, 1u}; }

    std::array<std::byte, 64> ZeroBytes64{};
}

// -----------------------------------------------------------------------------
// 1. Request -> Ready (buffer)
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, BufferRequestTransitionsToReadyAfterTick)
{
    CacheFixture fx;
    const auto id = MakeAssetId(1);

    auto r = fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()});
    ASSERT_TRUE(r.has_value()) << static_cast<int>(r.error());

    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::GpuUploading);
    EXPECT_EQ(fx.Transfer.Issued.size(), 1u);
    EXPECT_EQ(fx.Device.CreateBufferCount, 1);

    fx.Cache.Tick(/*frame=*/0, /*framesInFlight=*/2);

    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::Ready);
    auto view = fx.Cache.GetView(id);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->Kind, Graphics::GpuAssetKind::Buffer);
    EXPECT_TRUE(view->Buffer.IsValid());
    EXPECT_GT(view->Generation, 0u);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 0u);
}

// -----------------------------------------------------------------------------
// 2. Request -> Ready (texture, with bindless slot)
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, TextureRequestPopulatesBindlessIndex)
{
    CacheFixture fx;
    const auto id = MakeAssetId(2);

    auto r = fx.Cache.RequestUpload(Graphics::GpuTextureRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyTextureDesc(),
        .Sampler = AnySampler()});
    ASSERT_TRUE(r.has_value());

    fx.Cache.Tick(0, 2);

    auto view = fx.Cache.GetView(id);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->Kind, Graphics::GpuAssetKind::Texture);
    EXPECT_TRUE(view->Texture.IsValid());
    EXPECT_NE(view->BindlessIdx, RHI::kInvalidBindlessIndex)
        << "TextureManager allocates a bindless slot when a sampler is given.";
}

// -----------------------------------------------------------------------------
// 3. Failed (allocation OOM)
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, BufferAllocationFailureMarksEntryFailed)
{
    CacheFixture fx;
    const auto id = MakeAssetId(3);

    fx.Device.FailNextBufferCreate = true;
    auto r = fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()});
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), Core::ErrorCode::OutOfDeviceMemory);
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::Failed);
}

// -----------------------------------------------------------------------------
// 4. Failed (CPU pipeline notify)
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, NotifyFailedCreatesEntryInFailedState)
{
    CacheFixture fx;
    const auto id = MakeAssetId(4);

    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::NotRequested);
    fx.Cache.NotifyFailed(id);
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::Failed);
    EXPECT_FALSE(fx.Cache.GetView(id).has_value());
}

// -----------------------------------------------------------------------------
// 5. Hot reload — generation advances; state flow correct
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, HotReloadAdvancesGenerationAndRetiresOldLease)
{
    CacheFixture fx;
    const auto id = MakeAssetId(5);

    // Initial upload.
    ASSERT_TRUE(fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()})
        .has_value());
    fx.Cache.Tick(0, 2);
    auto first = fx.Cache.GetView(id);
    ASSERT_TRUE(first.has_value());
    const auto firstHandle = first->Buffer;
    const auto firstGen    = first->Generation;
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 0u);

    // Hot reload: same id, second RequestUpload while Ready.
    ASSERT_TRUE(fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()})
        .has_value());

    // During GpuUploading, GetView still returns the old (first) view —
    // renderer snapshots taken before the reload remain valid.
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::GpuUploading);
    auto duringReload = fx.Cache.GetView(id);
    ASSERT_TRUE(duringReload.has_value());
    EXPECT_EQ(duringReload->Buffer, firstHandle);
    EXPECT_EQ(duringReload->Generation, firstGen);

    // Tick swaps in the new resource and queues the old lease for retirement.
    fx.Cache.Tick(1, 2);
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::Ready);
    auto second = fx.Cache.GetView(id);
    ASSERT_TRUE(second.has_value());
    EXPECT_GT(second->Generation, firstGen);
    EXPECT_NE(second->Buffer, firstHandle);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 1u);
}

// -----------------------------------------------------------------------------
// 6. Old-view lifetime preservation across framesInFlight
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, OldLeaseSurvivesFramesInFlightBeforeDestruction)
{
    CacheFixture fx;
    const auto id = MakeAssetId(6);
    constexpr std::uint32_t kFif = 2;

    // Initial upload + complete.
    ASSERT_TRUE(fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()})
        .has_value());
    fx.Cache.Tick(0, kFif);
    const int destroysAfterInitial = fx.Device.DestroyBufferCount;

    // Hot reload — old lease retires on this Tick (deadline = 1 + kFif = 3).
    ASSERT_TRUE(fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()})
        .has_value());
    fx.Cache.Tick(1, kFif);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 1u);
    EXPECT_EQ(fx.Device.DestroyBufferCount, destroysAfterInitial)
        << "Old lease must remain alive immediately after retirement.";

    // Tick at frame 2 — still under the deadline (3 > 2).
    fx.Cache.Tick(2, kFif);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 1u);
    EXPECT_EQ(fx.Device.DestroyBufferCount, destroysAfterInitial)
        << "Old lease must outlive framesInFlight - 1 frames after retirement.";

    // Tick at frame 3 — deadline reached (3 <= 3). Lease released; mock
    // device sees the underlying DestroyBuffer call.
    fx.Cache.Tick(3, kFif);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 0u);
    EXPECT_EQ(fx.Device.DestroyBufferCount, destroysAfterInitial + 1)
        << "Old lease must be released exactly once after framesInFlight frames.";
}

// -----------------------------------------------------------------------------
// 7. NotifyReloaded keeps the existing lease alive (no retire on its own).
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, NotifyReloadedTransitionsReadyToCpuPendingWithoutRetiring)
{
    CacheFixture fx;
    const auto id = MakeAssetId(7);

    ASSERT_TRUE(fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()})
        .has_value());
    fx.Cache.Tick(0, 2);
    ASSERT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::Ready);
    auto before = fx.Cache.GetView(id);
    ASSERT_TRUE(before.has_value());

    fx.Cache.NotifyReloaded(id);
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::CpuPending);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 0u)
        << "NotifyReloaded only flips state; the next RequestUpload retires.";

    // The current view is still queryable — readers retain stable handles.
    auto stillVisible = fx.Cache.GetView(id);
    ASSERT_TRUE(stillVisible.has_value());
    EXPECT_EQ(stillVisible->Buffer, before->Buffer);
    EXPECT_EQ(stillVisible->Generation, before->Generation);
}

// -----------------------------------------------------------------------------
// 8. NotifyDestroyed retires the lease and removes the entry.
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, NotifyDestroyedRemovesEntryAndQueuesLeaseForRetirement)
{
    CacheFixture fx;
    const auto id = MakeAssetId(8);

    ASSERT_TRUE(fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()})
        .has_value());
    fx.Cache.Tick(0, 2);
    EXPECT_EQ(fx.Cache.TrackedCount(), 1u);

    fx.Cache.NotifyDestroyed(id);
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::NotRequested);
    EXPECT_EQ(fx.Cache.TrackedCount(), 0u);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 1u);

    // The deadline is anchored on the first Tick after retirement
    // (deadline = anchorFrame + framesInFlight).  So a retirement requested
    // before Tick(1, 2) becomes deadline=3 and is dropped at Tick(3, 2).
    fx.Cache.Tick(1, 2);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 1u);
    fx.Cache.Tick(2, 2);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 1u);
    fx.Cache.Tick(3, 2);
    EXPECT_EQ(fx.Cache.PendingRetireCount(), 0u);
}

// -----------------------------------------------------------------------------
// 9. RequestUpload while GpuUploading reports Conflict (no overlapping uploads).
// -----------------------------------------------------------------------------

TEST(GpuAssetCache, RequestUploadWhileGpuUploadingReturnsConflict)
{
    CacheFixture fx;
    fx.Transfer.AlwaysComplete = false;  // keep first upload in flight
    const auto id = MakeAssetId(9);

    ASSERT_TRUE(fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()})
        .has_value());
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::GpuUploading);

    fx.Cache.Tick(0, 2);  // does not advance — IsComplete returns false
    EXPECT_EQ(fx.Cache.GetState(id), Graphics::GpuAssetState::GpuUploading);

    auto r = fx.Cache.RequestUpload(Graphics::GpuBufferRequest{
        .Id = id, .Bytes = std::span{ZeroBytes64}, .Desc = AnyBufferDesc()});
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), Core::ErrorCode::ResourceBusy);
}
