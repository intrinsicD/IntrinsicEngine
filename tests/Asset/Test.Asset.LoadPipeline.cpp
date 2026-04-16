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
    LoadRequest req{.id = AssetId{0u, 1u}, .typeId = 0u, .path = "", .needsGpuUpload = false};
    auto r = p.EnqueueIO(std::move(req));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidState);
}

TEST(AssetLoadPipeline, EnqueueTransitionsUnloadedToReadyInlineWhenNoGpu)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    LoadRequest req{.id = id, .typeId = 0u, .path = "/tmp/a", .needsGpuUpload = false};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());

    auto st = f.registry.GetState(id).value();
    EXPECT_EQ(st, AssetState::Ready);
    EXPECT_EQ(f.pipeline.InFlightCount(), 0u);
    EXPECT_EQ(f.bus.PendingCount(), 1u); // Ready event queued
}

TEST(AssetLoadPipeline, EnqueueWithGpuEndsInQueuedGPU)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    LoadRequest req{.id = id, .typeId = 0u, .path = "/tmp/a", .needsGpuUpload = true};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());

    EXPECT_EQ(f.registry.GetState(id).value(), AssetState::QueuedGPU);
    EXPECT_EQ(f.pipeline.InFlightCount(), 1u);
}

TEST(AssetLoadPipeline, EnqueueFailsWhenAssetNotUnloaded)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    ASSERT_TRUE(f.registry.SetState(id, AssetState::Unloaded, AssetState::Ready).has_value());
    LoadRequest req{.id = id, .typeId = 0u, .path = "", .needsGpuUpload = false};
    auto r = f.pipeline.EnqueueIO(std::move(req));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidState);
}

TEST(AssetLoadPipeline, OnGpuUploadedMovesFromQueuedGpuToReady)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    LoadRequest req{.id = id, .typeId = 0u, .path = "", .needsGpuUpload = true};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());
    ASSERT_EQ(f.registry.GetState(id).value(), AssetState::QueuedGPU);

    ASSERT_TRUE(f.pipeline.OnGpuUploaded(id).has_value());
    EXPECT_EQ(f.registry.GetState(id).value(), AssetState::Ready);
    EXPECT_EQ(f.pipeline.InFlightCount(), 0u);
}

TEST(AssetLoadPipeline, OnGpuUploadedFromWrongStateFails)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    // State is Unloaded, not QueuedGPU.
    auto r = f.pipeline.OnGpuUploaded(id);
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
    LoadRequest req{.id = id, .typeId = 0u, .path = "", .needsGpuUpload = false};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());
    f.bus.Flush();
    EXPECT_EQ(ready.load(), 1);
}

TEST(AssetLoadPipeline, MarkFailedMovesToFailedAndPublishesEvent)
{
    PipelineFixture f;
    std::atomic<int> failed{0};
    (void)f.bus.SubscribeAll([&](AssetId, AssetEvent e)
    {
        if (e == AssetEvent::Failed) ++failed;
    });

    auto id = f.NewAsset();
    ASSERT_TRUE(f.pipeline.MarkFailed(id).has_value());
    EXPECT_EQ(f.registry.GetState(id).value(), AssetState::Failed);

    f.bus.Flush();
    EXPECT_EQ(failed.load(), 1);
}

TEST(AssetLoadPipeline, MarkFailedIsIdempotent)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    ASSERT_TRUE(f.pipeline.MarkFailed(id).has_value());
    EXPECT_TRUE(f.pipeline.MarkFailed(id).has_value());
    EXPECT_EQ(f.registry.GetState(id).value(), AssetState::Failed);
}

TEST(AssetLoadPipeline, PathIsCopiedInRequestAndSurvivesCallerString)
{
    // Regression: LoadRequest::path used to be const char* - pointing at
    // a caller-owned buffer that could be freed before the async decode ran.
    // We now own a std::string. Verify the field is populated.
    PipelineFixture f;
    auto id = f.NewAsset();

    std::string transient = "/tmp/transient.bin";
    LoadRequest req{.id = id, .typeId = 0u, .path = transient, .needsGpuUpload = true};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());
    transient = "clobbered"; // would have been a dangling pointer with const char*

    EXPECT_EQ(f.registry.GetState(id).value(), AssetState::QueuedGPU);
    EXPECT_EQ(f.pipeline.InFlightCount(), 1u);
}

TEST(AssetLoadPipeline, CancelDropsInFlightEntry)
{
    PipelineFixture f;
    auto id = f.NewAsset();
    LoadRequest req{.id = id, .typeId = 0u, .path = "", .needsGpuUpload = true};
    ASSERT_TRUE(f.pipeline.EnqueueIO(std::move(req)).has_value());
    EXPECT_TRUE(f.pipeline.IsInFlight(id));

    f.pipeline.Cancel(id);
    EXPECT_FALSE(f.pipeline.IsInFlight(id));
    EXPECT_EQ(f.pipeline.InFlightCount(), 0u);
}

TEST(AssetLoadPipeline, CancelOnUnknownIdIsHarmless)
{
    PipelineFixture f;
    f.pipeline.Cancel(AssetId{});
    f.pipeline.Cancel(AssetId{42u, 1u});
    SUCCEED();
}
