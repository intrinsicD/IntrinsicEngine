// RUNTIME-296: mesh vertex normals on the GPU property residency, on a real device (ADR 0030
// decisions 6-9). The Vulkan run reads the canonical positions slot and a topology bundle the
// residency keeps per topology revision, writes the output's float3 ring (no viewport preview:
// vec3 rings are not observed), and Accept publishes it through the undoable normals entry so
// the CPU rows equal the CPU reference within the parity bound (double kernels in the
// reference's summation order; float rounding of the normalized result). A second run on the
// same revisions uploads zero bytes (positions, the accepted output and the bundle are
// resident); Discard keeps the CPU normals; undo restores them.
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <entt/entity/entity.hpp>
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
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.HalfedgeMesh;
import Geometry.NormalEstimation.Types;
import Geometry.Properties;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace ECSC = Extrinsic::ECS::Components;
    using Domain = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    using Phase = Runtime::EditorGpuTransactionPhase;
    using Weighting = Geometry::HalfedgeMesh::VertexNormals::AveragingMode;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    // Parity bound: the kernels run in double precision in the reference's summation order
    // (uncontracted), so the only differences are the float rounding of the normalized result
    // and a possible last-bit difference of sqrt / division between the device and the CPU
    // libm: a few float ulps of a unit vector.
    constexpr double kParityTolerance = 2e-6;

    class NormalsApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Context.Scene = Kernel().Worlds().Get(Kernel().ActiveWorld());
            Context.World = Kernel().ActiveWorld();
            Context.SpatialIndices = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            Context.Device = &Kernel().GetDevice();
            Context.CommandHistory = &History;
            Context.JobCommands.Submit = [this](Runtime::JobDesc desc, Runtime::EditorJobIdentity) {
                return Kernel().Jobs().Submit(std::move(desc));
            };
            // The CPU reference publishes synchronously through a context without jobs.
            CpuContext = Context;
            CpuContext.JobCommands = {};
            // A 9x9 grid with quads on even cells and two triangles on odd ones (the fan sum
            // and the ring order matter), z-noise so the normals vary, one deleted face and
            // one deleted vertex.
            auto& raw = Context.Scene->Raw();
            Entity = Context.Scene->Create();
            raw.emplace<ECSC::MetaData>(Entity, std::string{"NormalsMesh"});
            raw.emplace<ECSC::Transform::Component>(Entity);
            Geometry::HalfedgeMesh::Mesh grid;
            std::vector<Geometry::VertexHandle> handles;
            std::mt19937 random(296);
            std::uniform_real_distribution<float> noise(-0.15f, 0.15f);
            constexpr int kSide = 9;
            for (int y = 0; y < kSide; ++y)
                for (int x = 0; x < kSide; ++x)
                    handles.push_back(grid.AddVertex({float(x) * 0.25f, float(y) * 0.25f, noise(random)}));
            const auto v = [&](int i, int j) { return handles[std::size_t(j * kSide + i)]; };
            for (int y = 0; y + 1 < kSide; ++y)
                for (int x = 0; x + 1 < kSide; ++x)
                {
                    if ((x + y) % 2 == 0)
                        (void)grid.AddQuad(v(x, y), v(x + 1, y), v(x + 1, y + 1), v(x, y + 1));
                    else
                    {
                        (void)grid.AddTriangle(v(x, y), v(x + 1, y), v(x + 1, y + 1));
                        (void)grid.AddTriangle(v(x, y), v(x + 1, y + 1), v(x, y + 1));
                    }
                }
            GS::PopulateFromMesh(raw, Entity, grid);
            Vertices().GetOrAdd<bool>("v:deleted", false)[kDeletedVertex] = true;
            raw.get<GS::Faces>(Entity).Properties.GetOrAdd<bool>("f:deleted", false)[kDeletedFace] = true;
            Id = Runtime::SelectionController::ToStableEntityId(Entity);
            Config.StableEntityId = Id;
            Config.Method = Runtime::NormalEstimationMethod::MeshFaceWeighted;
            Config.Backend = Runtime::NormalEstimationBackend::Vulkan;
            Config.Positions = {Domain::MeshVertex, "v:position", K::Vec3};
            Config.Output = {Domain::MeshVertex, "v:normal", K::Vec3};
            Config.Weighting = Weighting::AreaWeighted;
        }

        static constexpr std::size_t kDeletedVertex = 40, kDeletedFace = 7;
        Geometry::PropertySet& Vertices() { return Context.Scene->Raw().get<GS::Vertices>(Entity).Properties; }
        Geometry::PropertySet& Faces() { return Context.Scene->Raw().get<GS::Faces>(Entity).Properties; }
        std::vector<glm::vec3> Normals(const char* name = "v:normal")
        {
            const auto p = std::as_const(Vertices()).Get<glm::vec3>(name);
            return p ? p.Vector() : std::vector<glm::vec3>{};
        }
        std::vector<glm::vec3> FaceNormals(const char* name = "f:normal")
        {
            const auto p = std::as_const(Faces()).Get<glm::vec3>(name);
            return p ? p.Vector() : std::vector<glm::vec3>{};
        }
        // mesh_face_normals (slice 2): Newell normals over the face rows.
        Runtime::NormalEstimationConfig FaceConfig(const char* output = "f:normal") const
        {
            auto config = Config;
            config.Method = Runtime::NormalEstimationMethod::MeshFaceNormals;
            config.Output = {Domain::MeshFace, output, K::Vec3};
            return config;
        }
        bool StartFaces(const char* what)
        {
            Runtime::EditorNormalEstimationResult failure;
            Run = Runtime::StartEditorNormalEstimationTransaction(Commands(), FaceConfig(), failure);
            if (!Run) Fail(std::string{what} + " start rejected: " + failure.Message);
            return Run != nullptr;
        }
        Extrinsic::Graphics::GpuPropertyResidency* Residency() { return Context.SpatialIndices->PropertyResidency(); }
        Extrinsic::Graphics::GpuPropertyKey Key() { return Runtime::MakeGpuPropertyKey(Context.World, Entity, Config.Output); }
        Runtime::EditorProcessingCommands Commands() { return Runtime::BindEditorProcessingCommands(Context); }
        Runtime::EditorNormalTransactionSnapshot Snapshot() { return Runtime::SnapshotEditorNormalEstimation(Commands(), Run); }

        // The CPU reference of `weighting`, published into `output` synchronously.
        bool Reference(const Weighting weighting, const char* output, Runtime::EditorNormalEstimationResult& result)
        {
            auto reference = Config;
            reference.Backend = Runtime::NormalEstimationBackend::CpuKDTree;
            reference.Weighting = weighting;
            reference.Output.Name = output;
            result = Runtime::ApplyEditorNormalEstimationCommand(Runtime::BindEditorProcessingCommands(CpuContext), reference);
            if (!result.Succeeded()) Fail(std::string{"CPU reference "} + output + " failed: " + result.Message);
            return result.Succeeded();
        }
        bool Start(const Weighting weighting, const char* what)
        {
            auto config = Config;
            config.Weighting = weighting;
            Runtime::EditorNormalEstimationResult failure;
            Run = Runtime::StartEditorNormalEstimationTransaction(Commands(), config, failure);
            if (!Run) Fail(std::string{what} + " start rejected: " + failure.Message);
            return Run != nullptr;
        }
        bool Ready(const char* what)
        {
            const auto snapshot = Snapshot();
            if (snapshot.Phase == Phase::Running) return false;
            if (snapshot.Phase != Phase::ReadyToAccept) { Fail(std::string{what} + " ended without a result: " + snapshot.Result.Message); return false; }
            return true;
        }
        void Accept()
        {
            Accepted.reset();
            const auto accepted = Runtime::AcceptEditorNormalEstimation(Commands(), Run,
                [this](Runtime::EditorNormalEstimationResult result) { Accepted = std::move(result); });
            if (accepted.Status != Runtime::EditorCommandStatus::Pending) Fail("accept refused: " + accepted.Message);
        }
        // Max component delta between the accepted normals and the reference over every row
        // (deleted rows included: both keep 0 / their published bytes).
        void ExpectParity(const std::vector<glm::vec3>& gpu, const std::vector<glm::vec3>& cpu, const char* what,
                          const std::size_t deletedRow = kDeletedVertex)
        {
            ASSERT_EQ(gpu.size(), cpu.size()) << what;
            double delta = 0;
            std::size_t worst = 0;
            for (std::size_t i = 0; i < gpu.size(); ++i)
                for (int c = 0; c < 3; ++c)
                {
                    // Both operands must be finite: a NaN would otherwise compare as "no delta".
                    EXPECT_TRUE(std::isfinite(gpu[i][c])) << what << ": GPU row " << i << " channel " << c;
                    EXPECT_TRUE(std::isfinite(cpu[i][c])) << what << ": CPU row " << i << " channel " << c;
                    const double d = std::abs(double(gpu[i][c]) - double(cpu[i][c]));
                    if (!(d <= delta)) { delta = std::isfinite(d) ? d : std::numeric_limits<double>::infinity(); worst = i; }
                }
            ::testing::Test::RecordProperty(std::string{what} + "_max_abs_delta", std::to_string(delta));
            EXPECT_LE(delta, kParityTolerance) << what << ": row " << worst << " GPU (" << gpu[worst].x << ", " << gpu[worst].y << ", "
                                               << gpu[worst].z << ") vs CPU (" << cpu[worst].x << ", " << cpu[worst].y << ", " << cpu[worst].z << ")";
            EXPECT_EQ(gpu[deletedRow], cpu[deletedRow]) << what << ": a deleted row keeps its published bytes";
        }
        void ExpectCounts(const Runtime::EditorNormalEstimationResult& gpu, const Runtime::EditorNormalEstimationResult& cpu, const char* what)
        {
            EXPECT_EQ(gpu.ValidCount, cpu.ValidCount) << what;
            EXPECT_EQ(gpu.FallbackCount, cpu.FallbackCount) << what;
            EXPECT_EQ(gpu.ProcessedFaces, cpu.ProcessedFaces) << what;
            EXPECT_EQ(gpu.WrittenCount, cpu.WrittenCount) << what;
            EXPECT_EQ(gpu.LiveCount, cpu.LiveCount) << what;
        }

        void Fail(const std::string& message)
        {
            ADD_FAILURE() << message;
            Kernel().RequestExit();
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(150)) { TimedOut = true; return Fail("RUNTIME-296 smoke timeout at step " + std::to_string(Step)); }
            if (!Kernel().GetDevice().IsOperational() || !Context.SpatialIndices) return;
            ++OperationalFrames;
            switch (Step)
            {
            case 0: // The CPU references, then run 1 (area weighting) on the device.
            {
                if (OperationalFrames < 6) return;
                if (!Reference(Weighting::AreaWeighted, "cpu_area", CpuArea) || !Reference(Weighting::MaxWeighted, "cpu_max", CpuMax) ||
                    !Reference(Weighting::UniformFace, "cpu_uniform", CpuUniform)) return;
                EXPECT_GT(CpuArea.ProcessedFaces, 0u);
                EXPECT_GT(CpuArea.ValidCount, 0u);
                StatsBefore = Residency()->Stats();
                if (!Start(Weighting::AreaWeighted, "run 1")) return;
                ++Step;
                return;
            }
            case 1: // Ready: the positions and the bundle were uploaded once each; nothing is published.
            {
                if (!Ready("run 1")) return;
                const auto snapshot = Snapshot();
                EXPECT_TRUE(snapshot.CanAccept) << snapshot.AcceptDisabledReason;
                EXPECT_EQ(snapshot.Previews, 1u);
                const auto stats = Residency()->Stats();
                EXPECT_EQ(stats.Uploads, StatsBefore.Uploads + 2u) << "the first run uploads the positions and the topology bundle once";
                EXPECT_EQ(snapshot.Result.GpuInputUploadBytes, Vertices().Size() * sizeof(glm::vec3));
                EXPECT_FALSE(snapshot.Result.GpuTopologyReused);
                EXPECT_GT(snapshot.Result.GpuTopologyBytes, 0u);
                EXPECT_EQ(stats.UploadBytes, StatsBefore.UploadBytes + snapshot.Result.GpuInputUploadBytes + snapshot.Result.GpuTopologyBytes);
                EXPECT_TRUE(Residency()->HasRing(Key()));
                EXPECT_FALSE(std::as_const(Vertices()).Exists("v:normal")) << "nothing is published before Accept";
                Accept();
                ++Step;
                return;
            }
            case 2: // Applied only after the CPU publication; parity with the CPU reference; the front is canonical.
            {
                if (!Accepted) { EXPECT_NE(Snapshot().Phase, Phase::Applied) << "Applied before the publication landed"; return; }
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                EXPECT_EQ(Accepted->ActualBackend, "vulkan_mesh_face_weighted");
                EXPECT_EQ(Snapshot().Phase, Phase::Applied);
                Run1 = Normals();
                ExpectParity(Run1, Normals("cpu_area"), "run1_area");
                ExpectCounts(*Accepted, CpuArea, "run 1");
                EXPECT_FALSE(Residency()->HasRing(Key()));
                const auto canonical = Residency()->Front(Key());
                ASSERT_TRUE(canonical);
                EXPECT_EQ(canonical->Revision, std::as_const(Vertices()).Get<glm::vec3>("v:normal").Revision());
                // Run 2 (max weighting) right away: positions, the accepted output and the bundle are resident.
                StatsBefore = Residency()->Stats();
                if (!Start(Weighting::MaxWeighted, "run 2")) return;
                ++Step;
                return;
            }
            case 3: // Zero upload bytes on the same revisions; Accept and compare with the max reference.
            {
                if (!Ready("run 2")) return;
                const auto snapshot = Snapshot();
                const auto stats = Residency()->Stats();
                EXPECT_EQ(stats.UploadBytes, StatsBefore.UploadBytes) << "run 2 on resident inputs uploads zero bytes";
                EXPECT_EQ(stats.Uploads, StatsBefore.Uploads);
                EXPECT_EQ(snapshot.Result.GpuInputUploadBytes, 0u);
                EXPECT_TRUE(snapshot.Result.GpuTopologyReused);
                EXPECT_GT(stats.Hits, StatsBefore.Hits);
                Accept();
                ++Step;
                return;
            }
            case 4:
            {
                if (!Accepted) return;
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                Run2 = Normals();
                ExpectParity(Run2, Normals("cpu_max"), "run2_max");
                ExpectCounts(*Accepted, CpuMax, "run 2");
                EXPECT_NE(Run2, Run1) << "max weighting differs from area weighting on a noisy grid";
                // Run 3 (uniform): accepted and compared like the others.
                StatsBefore = Residency()->Stats();
                if (!Start(Weighting::UniformFace, "run 3")) return;
                ++Step;
                return;
            }
            case 5:
            {
                if (!Ready("run 3")) return;
                EXPECT_EQ(Residency()->Stats().UploadBytes, StatsBefore.UploadBytes) << "run 3 uploads nothing";
                Accept();
                ++Step;
                return;
            }
            case 6:
            {
                if (!Accepted) return;
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                Run3 = Normals();
                ExpectParity(Run3, Normals("cpu_uniform"), "run3_uniform");
                ExpectCounts(*Accepted, CpuUniform, "run 3");
                EXPECT_NE(Run3, Run1) << "uniform weighting differs from area weighting on a noisy grid";
                // Run 4 (area again): ready, then discarded.
                StatsBefore = Residency()->Stats();
                if (!Start(Weighting::AreaWeighted, "run 4")) return;
                ++Step;
                return;
            }
            case 7: // Discard keeps the accepted normals; undo restores run 2's.
            {
                if (!Ready("run 4")) return;
                EXPECT_EQ(Residency()->Stats().UploadBytes, StatsBefore.UploadBytes);
                Runtime::DiscardEditorNormalEstimation(Commands(), Run);
                EXPECT_EQ(Snapshot().Phase, Phase::Discarded);
                EXPECT_FALSE(Residency()->HasRing(Key()));
                EXPECT_EQ(Normals(), Run3) << "Discard publishes nothing";
                // One undo restores run 2's rows (the normals publication guards each entry by
                // the output's revision, so only the newest entry on an output is undoable).
                ASSERT_TRUE(History.Undo().Succeeded());
                EXPECT_EQ(Normals(), Run2) << "undo restores the previous publication";
                // Slice 2: face normals. The CPU reference, then a device run whose positions
                // are resident; only the face bundle uploads.
                auto reference = FaceConfig("cpu_face");
                reference.Backend = Runtime::NormalEstimationBackend::CpuKDTree;
                CpuFace = Runtime::ApplyEditorNormalEstimationCommand(Runtime::BindEditorProcessingCommands(CpuContext), reference);
                if (!CpuFace.Succeeded()) return Fail("CPU face reference failed: " + CpuFace.Message);
                EXPECT_GT(CpuFace.ValidCount, 0u);
                EXPECT_GT(CpuFace.FallbackCount, 0u) << "the faces around the deleted vertex take the fallback";
                StatsBefore = Residency()->Stats();
                if (!StartFaces("face run 1")) return;
                ++Step;
                return;
            }
            case 8:
            {
                if (!Ready("face run 1")) return;
                const auto snapshot = Snapshot();
                EXPECT_EQ(snapshot.Result.GpuInputUploadBytes, 0u) << "the positions are resident";
                EXPECT_FALSE(snapshot.Result.GpuTopologyReused);
                EXPECT_EQ(Residency()->Stats().Uploads, StatsBefore.Uploads + 1u) << "only the face bundle uploads";
                EXPECT_FALSE(std::as_const(Faces()).Exists("f:normal"));
                Accept();
                ++Step;
                return;
            }
            case 9:
            {
                if (!Accepted) return;
                ASSERT_EQ(Accepted->Status, Runtime::EditorCommandStatus::Applied) << Accepted->Message;
                EXPECT_EQ(Accepted->ActualBackend, "vulkan_mesh_face_normals");
                FaceRun = FaceNormals();
                ExpectParity(FaceRun, FaceNormals("cpu_face"), "face_run1", kDeletedFace);
                ExpectCounts(*Accepted, CpuFace, "face run 1");
                StatsBefore = Residency()->Stats();
                if (!StartFaces("face run 2")) return;
                ++Step;
                return;
            }
            case 10: // Resident positions, bundle and accepted output: zero bytes; Discard keeps the rows.
            {
                if (!Ready("face run 2")) return;
                const auto snapshot = Snapshot();
                EXPECT_EQ(Residency()->Stats().UploadBytes, StatsBefore.UploadBytes) << "face run 2 uploads nothing";
                EXPECT_TRUE(snapshot.Result.GpuTopologyReused);
                Runtime::DiscardEditorNormalEstimation(Commands(), Run);
                EXPECT_EQ(Snapshot().Phase, Phase::Discarded);
                EXPECT_EQ(FaceNormals(), FaceRun);
                Done = true;
                Kernel().RequestExit();
                return;
            }
            default: return;
            }
        }

        void Shutdown() override
        {
            Run.reset();
            Context = {};
            CpuContext = {};
        }

        Runtime::EditorProcessingContext Context{}, CpuContext{};
        Runtime::EditorCommandHistory History{};
        entt::entity Entity{};
        std::uint32_t Id{};
        Runtime::NormalEstimationConfig Config{};
        Runtime::EditorNormalTransactionHandle Run{};
        std::optional<Runtime::EditorNormalEstimationResult> Accepted{};
        Runtime::EditorNormalEstimationResult CpuArea{}, CpuMax{}, CpuUniform{}, CpuFace{};
        std::vector<glm::vec3> Run1{}, Run2{}, Run3{}, FaceRun{};
        Extrinsic::Graphics::GpuPropertyResidencyStats StatsBefore{};
        std::chrono::steady_clock::time_point Started{};
        std::size_t OperationalFrames{};
        int Step{};
        bool Done{}, TimedOut{};
    };

    bool Prepare(Extrinsic::Core::Config::EngineConfig& config)
    {
        config = Runtime::CreateReferenceEngineConfig();
        config.Window.Width = 128;
        config.Window.Height = 128;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        return Extrinsic::Platform::Backends::Glfw::CanInitialize();
    }
}

TEST(RUNTIME296VertexNormalsResidency, AcceptMatchesTheCpuReferenceAndASecondRunUploadsNothing)
{
    Extrinsic::Core::Config::EngineConfig config;
    if (!Prepare(config)) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<NormalsApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done) << "the scenario stopped at step " << run->Step;
}
