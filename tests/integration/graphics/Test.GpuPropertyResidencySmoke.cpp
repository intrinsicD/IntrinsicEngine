// GRAPHICS-154 / ADR 0030: a property-space spatial index reads its positions from the
// property's canonical residency slot. Its Vulkan queries equal, bit for bit, the same queries
// over a private workspace that uploads its own copy (the previous path); later queries of the
// same revision upload nothing, and a new revision uploads once.
#include "RuntimeTestModule.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    std::vector<glm::vec3> Cloud(std::size_t count, unsigned seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        std::vector<glm::vec3> points(count);
        for (auto& p : points) p = {uniform(random), uniform(random), uniform(random)};
        return points;
    }

    const Runtime::GeometryPropertyRef kRef{.Domain = Runtime::GeometryElementDomain::PointCloudPoint,
                                            .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3};

    class ResidencyApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Cache = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            auto& scene = *Kernel().Worlds().Get(Kernel().ActiveWorld());
            Entity = scene.Create();
            auto& props = scene.Raw().emplace<GS::Vertices>(Entity).Properties;
            Points = Cloud(3000, 5u);
            props.Resize(Points.size());
            props.GetOrAdd<glm::vec3>("v:position").Vector() = Points;
            Queries = Cloud(500, 6u);
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(60))
            {
                ADD_FAILURE() << "GRAPHICS-154 smoke timed out in phase " << Phase;
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational() || Cache == nullptr) return;
            if (Batch && (Batch->State == Runtime::SpatialQueryState::Queued ||
                          Batch->State == Runtime::SpatialQueryState::Submitted))
                return;
            if (Batch && Batch->State == Runtime::SpatialQueryState::Failed)
            {
                ADD_FAILURE() << "phase " << Phase << ": " << Batch->Diagnostic;
                Kernel().RequestExit();
                return;
            }
            if (Batch) Results.push_back(*Batch);
            Batch.reset();
            const auto stats = [&] { return Cache->PropertyResidency() ? Cache->PropertyResidency()->Stats()
                                                                        : Extrinsic::Graphics::GpuPropertyResidencyStats{}; };
            switch (Phase++)
            {
            case 0: // property-space index: canonical slot
                Handle = Cache->Acquire(Kernel().ActiveWorld(), Entity, kRef).Handle;
                Batch = Cache->QueueGpuKNearest(Handle, Queries, 8u);
                return;
            case 1: // the same queries over a private workspace (own upload, the previous path)
                UploadsAfterFirst = stats().Uploads;
                Private = Cache->CreateWorkspace(Points);
                Batch = Cache->QueueGpuKNearest(Private.Handle, Queries, 8u);
                return;
            case 2: // the same revision again: no upload
                Batch = Cache->QueueGpuKNearest(Handle, Queries, 8u);
                return;
            case 3: // a new revision: one more upload
            {
                UploadsAfterRepeat = stats().Uploads;
                auto& scene = *Kernel().Worlds().Get(Kernel().ActiveWorld());
                auto positions = scene.Raw().get<GS::Vertices>(Entity).Properties.GetOrAdd<glm::vec3>("v:position");
                for (std::size_t i = 0; i < Points.size(); ++i) positions[i] = Points[i] + glm::vec3(0.25f, 0.0f, 0.0f);
                Handle = Cache->Acquire(Kernel().ActiveWorld(), Entity, kRef).Handle;
                Batch = Cache->QueueGpuKNearest(Handle, Queries, 8u);
                return;
            }
            default:
                UploadsAfterRevision = stats().Uploads;
                Final = stats();
                Kernel().RequestExit();
                return;
            }
        }

        void Shutdown() override { Private = {}; }

        Runtime::SpatialIndexCache* Cache{};
        entt::entity Entity{entt::null};
        Runtime::SpatialIndexHandle Handle{};
        Runtime::SpatialIndexWorkspace Private{};
        std::vector<glm::vec3> Points{}, Queries{};
        std::shared_ptr<Runtime::SpatialNearestBatch> Batch{};
        std::vector<Runtime::SpatialNearestBatch> Results{};
        std::uint64_t UploadsAfterFirst{}, UploadsAfterRepeat{}, UploadsAfterRevision{};
        Extrinsic::Graphics::GpuPropertyResidencyStats Final{};
        std::chrono::steady_clock::time_point Started{};
        int Phase{0};
    };
}

TEST(GRAPHICS154PropertyResidency, IndexReadsTheCanonicalSlotAndMatchesTheOwnUploadBitwise)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    auto app = std::make_unique<ResidencyApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    engine.Run();
    ASSERT_EQ(run->Results.size(), 4u);

    const auto& canonical = run->Results[0];
    const auto& ownUpload = run->Results[1];
    ASSERT_EQ(canonical.Counts, ownUpload.Counts);
    ASSERT_EQ(canonical.Neighbors.size(), ownUpload.Neighbors.size());
    for (std::size_t i = 0; i < canonical.Neighbors.size(); ++i)
    {
        EXPECT_EQ(canonical.Neighbors[i].Index, ownUpload.Neighbors[i].Index) << i;
        EXPECT_EQ(canonical.Neighbors[i].SquaredDistance, ownUpload.Neighbors[i].SquaredDistance) << i;
    }
    EXPECT_EQ(run->Results[2].Neighbors.size(), canonical.Neighbors.size());
    for (std::size_t i = 0; i < canonical.Neighbors.size(); ++i)
        EXPECT_EQ(run->Results[2].Neighbors[i].Index, canonical.Neighbors[i].Index) << i;

    EXPECT_EQ(run->UploadsAfterFirst, 1u) << "the property-space index uploaded through the residency";
    EXPECT_EQ(run->UploadsAfterRepeat, 1u) << "the same revision uploads nothing";
    EXPECT_EQ(run->UploadsAfterRevision, 2u) << "a new revision uploads once";
    EXPECT_EQ(run->Final.Slots, 1u) << "the new revision replaced the slot";
}
