#include <gtest/gtest.h>
#include <atomic>
 
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.EventBus;
import Extrinsic.Core.Error;
 
using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;
 
namespace
{
    // Fresh bound pipeline for each test - scheduler is intentionally NOT
    // initialized so decode runs inline on the caller thread.
    struct PipelineFixture
    {
        AssetRegistry registry;
        AssetEventBus bus;
        AssetLoadPipeline pipeline;
 
        PipelineFixture()
        {
            pipeline.BindRegistry(&registry);
            pipeline.BindEventBus(&bus);
        }
 
        AssetId NewAsset()
        {
            return registry.Create(0u, 0u).value();
        }
    };
}
 
TEST(AssetLoadPipeline, EnqueueWithoutRegistryBoundFails)
{
    AssetLoadPipeline p;
    LoadRequest req{.id = AssetId{0u, 1u}};
    auto r = p.EnqueueIO(std::move(req));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidState);
}
 
TEST(AssetLoadPipeline, EnqueueTransitionsUnloadedToReadyInline)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    LoadRequest req{.id = id};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());
 
    auto st = f.registry.GetState(id).value();
    EXPECT_EQ(st, AssetState::Ready);
    EXPECT_EQ(f.pipeline.InFlightCount(), 0u);
    EXPECT_EQ(f.bus.PendingCount(), 1u); // Ready event queued
}

TEST(AssetLoadPipeline, EnqueueFailsWhenAssetNotUnloaded)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    ASSERT_TRUE(f.registry.SetState(id, AssetState::Unloaded, AssetState::Ready).has_value());
    LoadRequest req{.id = id};
    auto r = f.pipeline.EnqueueIO(std::move(req));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidState);
}

TEST(AssetLoadPipeline, OnCpuDecodedWithoutBindFails)
{
    AssetLoadPipeline p;
    auto r = p.OnCpuDecoded(AssetId{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidState);
}
 
TEST(AssetLoadPipeline, OnCpuDecodedForUnknownIdIsNotFound)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    // id is alive in registry, but not in-flight in the pipeline.
    auto r = f.pipeline.OnCpuDecoded(id);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetLoadPipeline, EventBusSeesReadyOnSuccess)
{
    PipelineFixture f;
    std::atomic<int> ready{0};
    (void)f.bus.SubscribeAll([&](AssetId, AssetEvent e)
    {
        if (e == AssetEvent::Ready) ++ready;
    });
 
    auto id = f.NewAsset();
    LoadRequest req{.id = id};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());
    f.bus.Flush();
    EXPECT_EQ(ready.load(), 1);
}

TEST(AssetLoadPipeline, CancelOnUnknownIdIsHarmless)
{
    PipelineFixture f;
    f.pipeline.Cancel(AssetId{});
    f.pipeline.Cancel(AssetId{42u, 1u});
    SUCCEED();
}
