#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "OneShotLifecycleScript.hpp"
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
namespace
{
    namespace R = Extrinsic::Runtime;
    namespace RHI = Extrinsic::RHI;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using D = R::GeometryElementDomain;
    struct GeometryOutput
    {
        std::vector<glm::vec3> Positions;
        std::vector<std::uint32_t> Edge0, Edge1;
        std::size_t Faces{};
    };
    // ARCH-023: every request is a one-shot step run of the session's service (Vulkan pages its kNN rows on the
    // worker through a prepared query session). Failure modes, each creating no entity: 1 a stale source (edited
    // after the start), 2 the Jobs window's cancel of the run (its holder job) once its step is in flight or done,
    // 3 the lane refusing the run's step job, 4 a Discard (run_control's) once its step is in flight or done; modes 2
    // and 4 wait for Accept, so the cancel always precedes any Commit. Scripted: the common one-shot script on the
    // device, accepted into the CPU reference.
    class ConstructionApp final : public Intrinsic::Tests::RuntimeTestModule
    {
      public:
        unsigned Phase{}, FailureMode{};
        bool Scripted{}, Done{}, TimedOut{}, Cancelled{};
        Intrinsic::Tests::OneShotLifecycleScript Script{};
        double MaxError{};
        std::vector<double> PhaseMs, CpuMs;
        std::vector<std::size_t> ForegroundPixels;
        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Context.Scene = Kernel().Worlds().Get(Kernel().ActiveWorld());
            Context.World = Kernel().ActiveWorld();
            Context.SpatialIndices = Kernel().Services().Find<R::SpatialIndexCache>();
            ASSERT_NE(Context.SpatialIndices, nullptr);
            Context.StepRuns = Kernel().Services().Find<R::StepRunsService>(); // ARCH-023: every run is a step run
            ASSERT_NE(Context.StepRuns, nullptr);
            // Retain the reference camera while replacing its triangle with the
            // actual generated result for the pixel test.
            std::vector<entt::entity> old;
            for (auto e : Context.Scene->Raw().view<GS::Vertices>())
                old.push_back(e);
            for (auto e : old)
                Context.Scene->Destroy(e);
            std::vector<glm::vec3> points;
            for (unsigned i = 0; i < 34; ++i)
            {
                const float z = 1 - 2 * (float(i) + .5f) / 34, t = float(i) * 2.39996323f,
                            rad = std::sqrt(1 - z * z);
                points.push_back(.65f * glm::vec3(rad * std::cos(t), rad * std::sin(t), z));
            }
            points[1] = points[0];
            for (unsigned d = 1; d <= 8; ++d)
            {
                const auto e = Context.Scene->Create();
                Entities[d - 1] = e;
                if (d <= unsigned(D::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}),
                               c = mesh.AddVertex({0, 1, 0});
                    (void)mesh.AddTriangle(a, b, c);
                    GS::PopulateFromMesh(Context.Scene->Raw(), e, mesh);
                }
                else if (d < unsigned(D::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;
                    const auto a = graph.AddVertex({0, 0, 0}), b = graph.AddVertex({1, 0, 0});
                    (void)graph.AddEdge(a, b);
                    GS::PopulateFromGraph(Context.Scene->Raw(), e, graph);
                }
                else
                    Context.Scene->Raw().emplace<GS::Vertices>(e);
                auto& props = Props(d);
                props.Resize(points.size());
                props.GetOrAdd<glm::vec3>("samples").Vector() = points;
                auto normals = props.GetOrAdd<glm::vec3>("directions");
                for (unsigned i = 0; i < points.size(); ++i)
                    normals[i] = glm::normalize(points[i]);
                props.GetOrAdd<float>("keep").Vector().assign(points.size(), 42);
                const bool half = D(d) == D::MeshHalfedge || D(d) == D::GraphHalfedge;
                if (half)
                {
                    auto& edges = Context.Scene->Raw().get<GS::Edges>(e).Properties;
                    edges.Resize(points.size() / 2);
                    edges.GetOrAdd<bool>("e:deleted")[2] = true;
                }
                else
                    props.GetOrAdd<bool>(D(d) == D::MeshFace ? "f:deleted"
                                         : (D(d) == D::MeshEdge || D(d) == D::GraphEdge)
                                             ? "e:deleted"
                                             : "v:deleted")[4] = true;
                props.Get<glm::vec3>("samples")[4] = {NAN, 0, 0};
                Revisions[d - 1] = props.Revision();
            }
        }
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(R::ResolveGeometryPropertySet(
                R::BuildGeometryAvailability(Context.Scene->Raw(), Entities[d - 1]), D(d)));
        }
        R::PointConstructionConfig Config(unsigned d) const
        {
            R::PointConstructionConfig c;
            c.StableEntityId = R::SelectionController::ToStableEntityId(Entities[d - 1]);
            c.Positions = {D(d), "samples", Geometry::PropertyValueKind::Vec3};
            c.Normals = {D(d), "directions", Geometry::PropertyValueKind::Vec3};
            c.EstimateNormals = false;
            c.Method = Phase < 3 ? R::PointConstructionMethod::Hoppe
                                 : R::PointConstructionMethod::KnnGraph;
            c.KNeighbors = Phase == 0 ? 1 : 3;
            c.Mutual = Phase == 4;
            c.Resolution = 6;
            c.GpuQueryBatchSize = 73;
            c.Backend = R::PointConstructionBackend::VulkanLBVH;
            return c;
        }
        GeometryOutput Read(const R::EditorPointConstructionResult& result)
        {
            const auto entity = R::SelectionController::ToEntityHandle(result.OutputEntityId);
            const auto view = GS::BuildConstView(Context.Scene->Raw(), entity);
            GeometryOutput output;
            EXPECT_NE(view.VertexSource, nullptr);
            if (!view.VertexSource)
                return output;
            output.Positions = view.VertexSource->Properties.Get<glm::vec3>("v:position").Vector();
            output.Edge0 = view.EdgeSource->Properties.Get<std::uint32_t>("e:v0").Vector();
            output.Edge1 = view.EdgeSource->Properties.Get<std::uint32_t>("e:v1").Vector();
            output.Faces = result.OutputFaceCount;
            return output;
        }
        void Remove(const R::EditorPointConstructionResult& result)
        {
            if (result.OutputEntityId)
                Context.Scene->Destroy(
                    R::SelectionController::ToEntityHandle(result.OutputEntityId));
        }
        void Start()
        {
            Results.clear();
            Tokens.clear();
            Submissions = 0;
            PhaseStarted = std::chrono::steady_clock::now();
            Submitted = true;
            const unsigned count = FailureMode || Scripted ? 1 : 8;
            double cpu = 0;
            if (!FailureMode)
                for (unsigned d = 1; d <= count; ++d)
                {
                    auto c = Config(d);
                    c.Backend = R::PointConstructionBackend::CpuReference;
                    auto context = Context;
                    context.JobCommands = {};
                    const auto start = std::chrono::steady_clock::now();
                    const auto result = R::ApplyEditorPointConstructionCommand(
                        R::BindEditorProcessingCommands(context), c);
                    ASSERT_TRUE(result.Succeeded()) << result.Message;
                    cpu += std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - start)
                               .count();
                    Expected[d - 1] = Read(result);
                    Remove(result);
                }
            CpuMs.push_back(cpu);
            PhaseStarted = std::chrono::steady_clock::now();
            Context.JobCommands.Submit = [this](R::JobDesc desc, R::EditorJobIdentity)
            {
                ++Submissions;
                if (FailureMode == 3 && Submissions == 2) // the run's step job (its holder is the first)
                    return R::JobToken{};
                const auto token = Kernel().Jobs().Submit(std::move(desc));
                EXPECT_TRUE(token.IsValid());
                Tokens.push_back(token);
                return token;
            };
            for (unsigned d = 1; d <= count; ++d)
            {
                const auto deliver = [this, d](R::EditorPointConstructionResult r)
                {
                    Results.emplace_back(d, std::move(r));
                };
                auto c = Config(d);
                // Interactive starts: ready for the script, computing at once but waiting for Accept for 2 and 4.
                if (FailureMode == 2 || FailureMode == 4)
                    c.Execution.Start = R::StepRunStart::Run;
                const auto commands = R::BindEditorProcessingCommands(Context);
                const auto result = Scripted || FailureMode == 2 || FailureMode == 4
                    ? R::StartEditorPointConstructionRun(commands, c, deliver)
                    : R::ApplyEditorPointConstructionCommand(commands, c, deliver);
                EXPECT_NE(result.StepRun, 0u) << result.Message;
                RunId = result.StepRun;
                if (result.Status != R::EditorCommandStatus::Pending)
                    Results.emplace_back(d, result);
            }
            if (Scripted)
            {
                const auto nothing = [this]
                {
                    EXPECT_TRUE(Results.empty()) << "nothing delivered before Accept";
                    EXPECT_EQ(Context.Scene->Raw().view<GS::Vertices>().size(), 8u) << "no entity before Accept";
                };
                Script = {.OnReadyHeld = nothing,
                    .OnFirstResult = nothing,
                    .OnRestartedReady = nothing,
                    .OnRestartedResult = nothing};
            }
            if (FailureMode == 1)
                Props(1).Get<glm::vec3>("samples")[0].x += .1f;
        }
        void Frame(double, double) override
        {
            if (::testing::Test::HasFatalFailure())
            {
                Kernel().RequestExit();
                return;
            }
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(150))
            {
                ADD_FAILURE() << "Point construction timed out: phase=" << Phase
                              << " failure=" << FailureMode << " results=" << Results.size();
                for (auto token : Tokens)
                    ADD_FAILURE() << R::ToString(Kernel().Jobs().GetState(token));
                TimedOut = true;
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational())
                return;
            if (!Readback.IsValid() && !FailureMode)
            {
                auto& device = Kernel().GetDevice();
                const auto extent = device.GetBackbufferExtent();
                ASSERT_EQ(RHI::BytesPerBlock(device.GetBackbufferFormat()), 4u);
                Width = extent.Width;
                Height = extent.Height;
                Readback = device.CreateBuffer({.SizeBytes = std::uint64_t(Width) * Height * 4,
                                                .Usage = RHI::BufferUsage::TransferDst,
                                                .HostVisible = true,
                                                .HostReadback = true,
                                                .DebugName = "PointConstruction.Readback"});
                ASSERT_TRUE(Readback.IsValid());
                Kernel().GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer(Readback);
            }
            if (!Submitted)
            {
                Start();
                return;
            }
            if (Scripted && !Script.Advance(*Context.StepRuns, RunId))
                return;
            if ((FailureMode == 2 || FailureMode == 4) && !Cancelled && !Tokens.empty())
            {
                const auto run = Context.StepRuns->Observe(RunId);
                if (run && (run->WorkerInFlight || run->Steps > 0u))
                {
                    std::string refusal;
                    Cancelled = FailureMode == 2
                        ? Kernel().Jobs().Cancel(Tokens.front())
                        : Context.StepRuns->Control(RunId, R::StepRunAction::Discard, 1u, refusal);
                }
            }
            if (Results.size() != (FailureMode || Scripted ? 1u : 8u))
                return;
            if (FailureMode)
            {
                bool terminal = true;
                for (auto token : Tokens)
                {
                    const auto state = Kernel().Jobs().GetState(token);
                    // An issued token can be reaped after downstream finalization
                    // has delivered the result checked above.
                    terminal &= state == R::JobState::Invalid || state == R::JobState::Rejected || state == R::JobState::Cancelled ||
                                state == R::JobState::Published ||
                                state == R::JobState::StaleDiscarded ||
                                state == R::JobState::Dropped;
                }
                if (!terminal)
                    return;
                EXPECT_FALSE(Results.front().second.Succeeded());
                EXPECT_EQ(Results.front().second.OutputEntityId, 0u);
                EXPECT_EQ(Context.Scene->Raw().view<GS::Vertices>().size(), 8u);
                Done = true;
                Kernel().RequestExit();
                return;
            }
            if (!Checked)
            {
                PhaseMs.push_back(std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - PhaseStarted)
                                      .count());
                for (const auto& [d, result] : Results)
                {
                    ASSERT_TRUE(result.Succeeded()) << "domain=" << d << " " << result.Message;
                    EXPECT_EQ(result.ActualBackend, "vulkan_lbvh");
                    EXPECT_GT(result.GpuQueryBatches, 0u);
                    EXPECT_NE(result.StepRun, 0u);
                    // The hybrid traffic of the step: every page uploads the session's full fixed page (73 grid
                    // rows; a graph's page holds at most its samples), and its rows come back.
                    EXPECT_EQ(result.CpuStageUploadBytes,
                        result.GpuQueryBatches * (Phase < 3 ? 73u : std::min<std::size_t>(73u, result.InputCount)) *
                            12u);
                    EXPECT_GT(result.CpuStageReadbackBytes, 0u);
                    if (Phase > 0)
                        EXPECT_TRUE(result.IndexReused);
                    const auto output = Read(result);
                    ASSERT_EQ(output.Positions.size(), Expected[d - 1].Positions.size());
                    EXPECT_EQ(output.Edge0, Expected[d - 1].Edge0);
                    EXPECT_EQ(output.Edge1, Expected[d - 1].Edge1);
                    EXPECT_EQ(output.Faces, Expected[d - 1].Faces);
                    for (unsigned i = 0; i < output.Positions.size(); ++i)
                        MaxError =
                            std::max(MaxError, double(glm::length(output.Positions[i] -
                                                                  Expected[d - 1].Positions[i])));
                    EXPECT_EQ(Props(d).Revision(), Revisions[d - 1]);
                    if (d != 8)
                        Remove(result);
                }
                Checked = true;
                VisibleFrames = 0;
                if (Scripted)
                {
                    EXPECT_EQ(Script.Current, Intrinsic::Tests::OneShotLifecycleScript::Stage::Done);
                    Done = true;
                    Kernel().RequestExit();
                }
                return;
            }
            if (++VisibleFrames < 6)
                return;
            auto& device = Kernel().GetDevice();
            device.WaitIdle();
            std::vector<std::uint8_t> pixels(std::size_t(Width) * Height * 4);
            device.ReadBuffer(Readback, pixels.data(), pixels.size());
            std::size_t foreground = 0;
            // The background is uniform and all other renderable entities have
            // been removed. Ignore alpha and compare the center area to a corner.
            for (unsigned y = Height / 4; y < Height * 3 / 4; ++y)
                for (unsigned x = Width / 4; x < Width * 3 / 4; ++x)
                {
                    const auto base = (std::size_t(y) * Width + x) * 4;
                    unsigned difference = 0;
                    for (unsigned c = 0; c < 3; ++c)
                        difference += std::abs(int(pixels[base + c]) - int(pixels[c]));
                    foreground += difference > 25;
                }
            ForegroundPixels.push_back(foreground);
            EXPECT_GT(foreground, 10u) << "phase=" << Phase;
            for (const auto& [d, result] : Results)
                if (d == 8)
                    Remove(result);
            if (++Phase == 5)
            {
                Done = true;
                Kernel().RequestExit();
                return;
            }
            Submitted = false;
            Checked = false;
        }
        void Shutdown() override
        {
            if (Readback.IsValid())
            {
                Kernel().GetRenderer().SetDefaultRecipeBackbufferReadbackBuffer({});
                Kernel().GetDevice().WaitIdle();
                Kernel().GetDevice().DestroyBuffer(Readback);
                Readback = {};
            }
        }

      private:
        R::EditorProcessingContext Context{};
        std::array<entt::entity, 8> Entities{};
        std::array<Geometry::PropertyRevision, 8> Revisions{};
        std::array<GeometryOutput, 8> Expected{};
        std::vector<std::pair<unsigned, R::EditorPointConstructionResult>> Results;
        std::vector<R::JobToken> Tokens;
        std::chrono::steady_clock::time_point Started{}, PhaseStarted{};
        RHI::BufferHandle Readback{};
        std::uint64_t RunId{};
        unsigned Width{}, Height{}, Submissions{}, VisibleFrames{};
        bool Submitted{}, Checked{};
    };
} // namespace
TEST(PointConstructionGpuSmoke, QueriesMatchReferenceAcrossDomainsAndGeneratedGeometryRenders)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    auto config = R::CreateReferenceEngineConfig();
    config.Window.Width = 96;
    config.Window.Height = 96;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    auto app = std::make_unique<ConstructionApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<R::SpatialIndexCache>();
    engine.EmplaceModule<R::StepRunsModule>();
    engine.Initialize();
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError, 1e-4);
    ASSERT_EQ(run->PhaseMs.size(), 5u);
    ASSERT_EQ(run->ForegroundPixels.size(), 5u);
    if (const auto* output = std::getenv("INTRINSIC_POINT_CONSTRUCTION_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{
            {"benchmark_id", "geometry.point_lbvh.point_construction_runtime_smoke"},
            {"method", "geometry.point_construction"},
            {"backend", "gpu_vulkan_compute"},
            {"dataset", "builtin.point_construction.eight_domains.sphere34"},
            {"commit", "local-dev"},
            {"metrics", {{"runtime_ms", run->PhaseMs[2]}, {"quality_error_linf", run->MaxError}}},
            {"diagnostics",
             {{"runner", "IntrinsicPointLBVHGpuTests"},
              {"mode", "smoke"},
              {"cpu_reference_total_ms", run->CpuMs},
              {"vulkan_total_ms", run->PhaseMs},
              {"foreground_pixels", run->ForegroundPixels},
              {"cpu_geometry_extraction", true}}},
            {"status", ::testing::Test::HasFailure() ? "failed" : "passed"}};
        std::ofstream stream(output);
        ASSERT_TRUE(stream.good());
        stream << json.dump(2) << '\n';
        ASSERT_TRUE(stream.good());
    }
}
TEST(PointConstructionGpuSmoke, StaleCancelledAndPartialSubmissionCreateNoEntity)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    for (unsigned failure = 1; failure <= 4; ++failure)
    {
        SCOPED_TRACE(failure);
        auto config = R::CreateReferenceEngineConfig();
        config.Window.Width = 64;
        config.Window.Height = 64;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = false;
        auto app = std::make_unique<ConstructionApp>();
        auto* run = app.get();
        run->FailureMode = failure;
        Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
        engine.EmplaceModule<R::SpatialIndexCache>();
        engine.EmplaceModule<R::StepRunsModule>();
        engine.Initialize();
        engine.Run();
        ASSERT_TRUE(engine.GetDevice().IsOperational());
        ASSERT_FALSE(run->TimedOut);
        ASSERT_TRUE(run->Done);
        if (failure == 2 || failure == 4)
            EXPECT_TRUE(run->Cancelled);
    }
}
// ARCH-023: a Vulkan Hoppe construction started ready follows the common one-shot script on the device (nothing
// recorded while ready, Step 1, Restart from the same capture and session, Run, one Accept). Predictions: no entity
// and no delivered result before Accept; the accepted surface equals the CPU reference (positions within 1e-4, the
// same edges and faces) with GPU batches and hybrid traffic reported. Mutations: a restart that keeps the first
// generation's field (no reset in Compute) fails Run's extraction; a start that ignores `start: ready` fails the
// script's hold-ready stage.
TEST(PointConstructionGpuSmoke, TheCommonOneShotScriptRunsOnTheDevice)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    auto config = R::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    auto app = std::make_unique<ConstructionApp>();
    auto* run = app.get();
    run->Scripted = true;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<R::SpatialIndexCache>();
    engine.EmplaceModule<R::StepRunsModule>();
    engine.Initialize();
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError, 1e-4);
}
