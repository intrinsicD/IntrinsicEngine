// TransformSyncSystem previous-model tracking contract: the stale sweep must
// evict exactly the instances that stopped appearing in the synced records
// (generation-stamp sweep), and re-appearing instances are re-tracked as new.

#include <array>
#include <memory>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace Graphics = Extrinsic::Graphics;
namespace RHI = Extrinsic::RHI;

namespace
{
    [[nodiscard]] Graphics::TransformSyncRecord MakeRecord(
        const Graphics::GpuInstanceHandle instance,
        const float x)
    {
        glm::mat4 model{1.f};
        model[3] = glm::vec4{x, 0.f, 0.f, 1.f};
        return Graphics::TransformSyncRecord{
            .StableId = instance.Index,
            .Instance = instance,
            .Model = model,
            .RenderFlags = RHI::GpuRender_Visible | RHI::GpuRender_Surface |
                           RHI::GpuRender_Opaque,
        };
    }
}

TEST(TransformSyncSystem, StaleSweepEvictsInstancesMissingFromRecords)
{
    Tests::MockDevice device;
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    auto& gpuWorld = renderer->GetGpuWorld();

    Graphics::TransformSyncSystem sync;
    sync.Initialize();

    const auto a = gpuWorld.AllocateInstance(1u);
    const auto b = gpuWorld.AllocateInstance(2u);
    ASSERT_TRUE(a.IsValid());
    ASSERT_TRUE(b.IsValid());

    const std::array both{MakeRecord(a, 1.f), MakeRecord(b, 2.f)};
    sync.SyncGpuBuffer(both, gpuWorld);
    EXPECT_EQ(sync.GetTrackedInstanceCountForTest(), 2u);

    // Instance `b` stops appearing: exactly its entry is evicted.
    const std::array onlyA{MakeRecord(a, 3.f)};
    sync.SyncGpuBuffer(onlyA, gpuWorld);
    EXPECT_EQ(sync.GetTrackedInstanceCountForTest(), 1u);

    // A re-appearing instance is re-tracked.
    sync.SyncGpuBuffer(both, gpuWorld);
    EXPECT_EQ(sync.GetTrackedInstanceCountForTest(), 2u);

    // Invalid-instance records are ignored by tracking, so a sync that
    // carries none of the live instances clears all tracked state.
    const std::array invalidOnly{Graphics::TransformSyncRecord{.StableId = 3u}};
    sync.SyncGpuBuffer(invalidOnly, gpuWorld);
    EXPECT_EQ(sync.GetTrackedInstanceCountForTest(), 0u);

    sync.Shutdown();
    renderer->Shutdown();
}

TEST(TransformSyncSystem, ShutdownClearsTrackedState)
{
    Tests::MockDevice device;
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    auto& gpuWorld = renderer->GetGpuWorld();

    Graphics::TransformSyncSystem sync;
    sync.Initialize();

    const auto a = gpuWorld.AllocateInstance(1u);
    ASSERT_TRUE(a.IsValid());

    const std::array records{MakeRecord(a, 1.f)};
    sync.SyncGpuBuffer(records, gpuWorld);
    EXPECT_EQ(sync.GetTrackedInstanceCountForTest(), 1u);

    sync.Shutdown();
    EXPECT_EQ(sync.GetTrackedInstanceCountForTest(), 0u);

    // Uninitialized syncs are ignored entirely.
    sync.SyncGpuBuffer(records, gpuWorld);
    EXPECT_EQ(sync.GetTrackedInstanceCountForTest(), 0u);

    renderer->Shutdown();
}
