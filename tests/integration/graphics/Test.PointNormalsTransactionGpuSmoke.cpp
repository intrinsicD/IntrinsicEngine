// Resident PCA normal parity, publication, IO reuse and discard on the real Vulkan path.
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.Properties;
namespace
{
    namespace R = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using Phase = R::EditorGpuTransactionPhase;
    class PointNormalsApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        R::EditorProcessingContext Context;
        R::EditorCommandHistory History;
        R::NormalEstimationConfig Config;
        R::EditorNormalTransactionHandle Run;
        std::optional<R::EditorNormalEstimationResult> Accepted;
        R::EditorNormalEstimationResult CpuResult;
        entt::entity Entity{};
        std::vector<glm::vec3> Reference, Before;
        std::chrono::steady_clock::time_point Started;
        unsigned Step{}, Mode{};
        bool Done{};
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        Geometry::PropertySet& Props() { return Context.Scene->Raw().get<GS::Vertices>(Entity).Properties; }
        auto Rows() { return std::as_const(Props()).Get<glm::vec3>("normals").Vector(); }
        void Fail(const std::string& why) { ADD_FAILURE() << why; Kernel().RequestExit(); }
        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Context.Scene = Kernel().Worlds().Get(Kernel().ActiveWorld());
            Context.World = Kernel().ActiveWorld();
            Context.Device = &Kernel().GetDevice();
            Context.SpatialIndices = Kernel().Services().Find<R::SpatialIndexCache>();
            Context.CommandHistory = &History;
            Context.JobCommands.Submit = [this](R::JobDesc d, R::EditorJobIdentity) { return Kernel().Jobs().Submit(std::move(d)); };
            Entity = Context.Scene->Create();
            Context.Scene->Raw().emplace<GS::Vertices>(Entity);
            std::mt19937 random(299);
            std::uniform_real_distribution<float> noise(-.04f, .04f);
            std::vector<glm::vec3> points;
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
                points.push_back({x * .2f, y * .2f, .03f*x*x + noise(random)});
            for (int x = 0; x < 8; ++x) points.push_back({10.f + x*.1f, 0, 0});
            for (int x = 0; x < 8; ++x) points.push_back({20, 0, 0});
            Props().Resize(points.size());
            Props().GetOrAdd<glm::vec3>("samples").Vector() = points;
            Props().GetOrAdd<bool>("v:deleted")[4] = true;
            Props().GetOrAdd<glm::vec3>("normals").Vector().assign(points.size(), glm::vec3(7,8,9));
            Config.StableEntityId = R::SelectionController::ToStableEntityId(Entity);
            Config.Positions = {R::GeometryElementDomain::PointCloudPoint, "samples", Geometry::PropertyValueKind::Vec3};
            Config.Output = {R::GeometryElementDomain::PointCloudPoint, "normals", Geometry::PropertyValueKind::Vec3};
            Config.Backend = R::NormalEstimationBackend::VulkanLBVH;
            Config.Orientation = Geometry::PointCloud::Normals::OrientationMode::None;
            Config.KNeighbors = 5; Config.Radius = .65f;
            Config.FallbackNormal = {1,2,3};
        }
        bool Start()
        {
            R::EditorNormalEstimationResult failure;
            Run = R::StartEditorNormalEstimationTransaction(Commands(), Config, failure);
            if (!Run) Fail(failure.Message);
            return bool(Run);
        }
        bool Ready()
        {
            const auto s = R::SnapshotEditorNormalEstimation(Commands(), Run);
            if (s.Phase == Phase::Failed || s.Phase == Phase::Discarded) { Fail(s.Result.Message); return false; }
            return s.Phase == Phase::ReadyToAccept;
        }
        void Parity()
        {
            const auto rows = Rows();
            ASSERT_EQ(rows.size(), Reference.size());
            glm::dvec3 delta(0); double angle = 0;
            for (std::size_t i = 0; i < rows.size(); ++i)
            {
                for (int c = 0; c < 3; ++c)
                {
                    ASSERT_TRUE(std::isfinite(rows[i][c]));
                    delta[c] = std::max(delta[c], std::abs(double(rows[i][c])-double(Reference[i][c])));
                }
                if (i != 4)
                {
                    const auto a = glm::normalize(glm::dvec3(rows[i])), b = glm::normalize(glm::dvec3(Reference[i]));
                    // atan2 is stable at zero and retains the signed-normal contract (no abs(dot)).
                    angle = std::max(angle, std::atan2(glm::length(glm::cross(a,b)), glm::dot(a,b)));
                }
            }
            const auto record = [&](const std::string& name, double value) {
                std::ostringstream out; out << std::setprecision(17) << value;
                ::testing::Test::RecordProperty((Mode ? "radius_" : "knn_") + name, out.str()); };
            for (int c = 0; c < 3; ++c) record("max_component_delta_"+std::to_string(c), delta[c]);
            record("max_angle_radians", angle);
            // Eight float epsilons on unit normals allow final normalization and device
            // sqrt/division rounding. Ordered double covariance/eigensolve should be much closer.
            EXPECT_LE(std::max({delta.x,delta.y,delta.z}), 8*double(std::numeric_limits<float>::epsilon()));
            EXPECT_LE(angle, 2e-6);
            EXPECT_EQ(rows[4], glm::vec3(7,8,9));
        }
        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now()-Started > std::chrono::seconds(120)) return Fail("PCA smoke timed out");
            if (!Kernel().GetDevice().IsOperational()) return;
            if (Step == 0)
            {
                auto mst = Config; mst.Orientation = Geometry::PointCloud::Normals::OrientationMode::MinimumSpanningTree;
                const auto refused = R::PreviewEditorNormalEstimationCommand(Commands(), mst);
                EXPECT_FALSE(refused.Enabled); EXPECT_NE(refused.DisabledReason.find("MST"), std::string::npos);
                Config.UseRadiusSearch = Mode != 0;
                auto cpu = Context; cpu.JobCommands = {}; cpu.CommandHistory = nullptr;
                auto config = Config; config.Backend = R::NormalEstimationBackend::CpuKDTree; config.Output.Name = "reference";
                Props().GetOrAdd<glm::vec3>("reference").Vector() = Rows();
                const auto result = R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(cpu), config);
                if (!result.Succeeded()) return Fail(result.Message);
                EXPECT_GT(result.FallbackCount, 0u); EXPECT_GT(result.ValidCount, 0u);
                CpuResult = result;
                Reference = std::as_const(Props()).Get<glm::vec3>("reference").Vector();
                Before = Rows();
                if (Start()) ++Step;
            }
            else if (Step == 1)
            {
                if (!Ready()) return;
                EXPECT_EQ(Rows(), Before);
                Accepted.reset();
                const auto result = R::AcceptEditorNormalEstimation(Commands(), Run, [this](auto r) { Accepted = r; });
                if (result.Status != R::EditorCommandStatus::Pending) return Fail(result.Message);
                ++Step;
            }
            else if (Step == 2)
            {
                if (!Accepted) return;
                if (!Accepted->Succeeded()) return Fail(Accepted->Message);
                EXPECT_EQ(Accepted->ActualBackend, "vulkan_lbvh");
                EXPECT_GE(Accepted->CpuStageReadbackBytes, Rows().size()*sizeof(glm::vec3));
                EXPECT_EQ(Accepted->ValidCount, CpuResult.ValidCount);
                EXPECT_EQ(Accepted->FallbackCount, CpuResult.FallbackCount);
                EXPECT_EQ(Accepted->PointDiagnostics.DuplicatePositionCount, CpuResult.PointDiagnostics.DuplicatePositionCount);
                EXPECT_EQ(Accepted->PointDiagnostics.FallbackNormalWasRepaired, CpuResult.PointDiagnostics.FallbackNormalWasRepaired);
                Parity();
                const auto key = R::MakeGpuPropertyKey(Context.World, Entity, Config.Output);
                auto* residency = Context.SpatialIndices->PropertyResidency();
                ASSERT_TRUE(residency->Front(key)); EXPECT_FALSE(residency->HasRing(key));
                EXPECT_EQ(residency->Front(key)->Revision, std::as_const(Props()).Get<glm::vec3>("normals").Revision());
                Before = Rows();
                if (Start()) ++Step;
            }
            else
            {
                if (!Ready()) return;
                const auto result = R::SnapshotEditorNormalEstimation(Commands(), Run).Result;
                EXPECT_EQ(result.GpuInputUploadBytes, 0u); EXPECT_GT(result.GpuInputCacheHits, 0u);
                R::DiscardEditorNormalEstimation(Commands(), Run); EXPECT_EQ(Rows(), Before);
                if (++Mode == 2) { Done = true; Kernel().RequestExit(); }
                else Step = 0;
            }
        }
        void Shutdown() override { Run.reset(); Context = {}; }
    };
}
TEST(RUNTIME299PointNormalsResidency, PcaParityAcceptZeroUploadAndDiscard)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto config = R::CreateReferenceEngineConfig();
    config.Window.Width = 64; config.Window.Height = 64;
    config.Render.EnableValidation = true; config.Render.EnableVSync = false; config.ReferenceScene.Enabled = false;
    auto app = std::make_unique<PointNormalsApp>(); auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<R::SpatialIndexCache>(); engine.Initialize();
    struct Shutdown { R::Engine& Engine; ~Shutdown() { Engine.Shutdown(); } } shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run(); EXPECT_TRUE(engine.GetDevice().IsOperational()); EXPECT_TRUE(run->Done);
}
