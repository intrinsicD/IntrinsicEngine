#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;
import Geometry.PointCloud.Consolidation;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.Properties;

namespace
{
    namespace ECS = Extrinsic::ECS;
    namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace Graphics = Extrinsic::Graphics;
    namespace Runtime = Extrinsic::Runtime;
    namespace Consolidation = Geometry::PointCloud::Consolidation;

    constexpr double kRmsTolerance = 5.0e-4;
    constexpr double kLinfTolerance = 2.0e-3;

    struct ParityFixture
    {
        std::string Name{};
        std::vector<glm::vec3> Input{};
        Runtime::PointCloudConsolidationConfig Config{};
    };

    [[nodiscard]] std::vector<glm::vec3> MakePlane(
        const std::uint32_t width,
        const std::uint32_t height,
        const double xWarpExponent)
    {
        std::vector<glm::vec3> points{};
        points.reserve(static_cast<std::size_t>(width) * height);
        for (std::uint32_t row = 0u; row < height; ++row)
        {
            const double v = static_cast<double>(row) /
                static_cast<double>(height - 1u);
            const double y = -1.0 + 2.0 * v;
            for (std::uint32_t column = 0u; column < width; ++column)
            {
                const double u = static_cast<double>(column) /
                    static_cast<double>(width - 1u);
                const double x = -1.0 +
                    2.0 * std::pow(u, xWarpExponent);
                const double z = 0.018 * std::sin(13.0 * x + 7.0 * y) +
                    0.006 * std::cos(5.0 * x - 11.0 * y);
                points.emplace_back(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z));
            }
        }
        return points;
    }

    [[nodiscard]] Runtime::PointCloudConsolidationConfig MakeConfig(
        const Runtime::PointCloudConsolidationStrategy strategy,
        const double supportRadius,
        const std::uint32_t targetPointCount,
        const std::uint32_t seed)
    {
        return Runtime::PointCloudConsolidationConfig{
            .Backend =
                Runtime::PointCloudConsolidationBackend::VulkanCompute,
            .Strategy = strategy,
            .SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual,
            .SupportRadius = supportRadius,
            .RepulsionWeight = 0.35,
            .MaxIterations = 8u,
            .ConvergenceTolerance = 0.0,
            .TargetPointCount = targetPointCount,
            .Seed = seed,
            .NormalRefinementRounds = 3u,
        };
    }

    [[nodiscard]] std::array<ParityFixture, 2u> MakeFixtures()
    {
        return {{
            ParityFixture{
                .Name = "lop",
                .Input = MakePlane(32u, 32u, 1.0),
                .Config = MakeConfig(
                    Runtime::PointCloudConsolidationStrategy::Lop,
                    0.18,
                    256u,
                    1901u),
            },
            ParityFixture{
                .Name = "wlop_isotropic",
                .Input = MakePlane(40u, 24u, 1.7),
                .Config = MakeConfig(
                    Runtime::PointCloudConsolidationStrategy::Wlop,
                    0.32,
                    240u,
                    1902u),
            },
        }};
    }

    [[nodiscard]] Consolidation::Params MakeCpuParams(
        const Runtime::PointCloudConsolidationConfig& config)
    {
        Consolidation::Params params{
            .SupportRadius = config.SupportRadius,
            .RepulsionWeight = config.RepulsionWeight,
            .MaxIterations = config.MaxIterations,
            .ConvergenceTolerance = config.ConvergenceTolerance,
            .TargetPointCount = config.TargetPointCount,
            .Seed = config.Seed,
        };
        params.Method = config.Strategy ==
                Runtime::PointCloudConsolidationStrategy::Lop
            ? Consolidation::Strategy{Consolidation::LopStrategy{}}
            : Consolidation::Strategy{Consolidation::WlopStrategy{
                  .Weighting = Consolidation::WeightingMode::Isotropic,
              }};
        return params;
    }

    void SetPositions(
        GS::Vertices& vertices,
        const std::span<const glm::vec3> positions)
    {
        vertices.Properties.Resize(positions.size());
        auto property = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{GS::PropertyNames::kPosition}, glm::vec3{0.0f});
        property.Vector().assign(positions.begin(), positions.end());
    }

    struct PositionError
    {
        double Rms{0.0};
        double Linf{0.0};
    };

    [[nodiscard]] PositionError MeasurePositionError(
        const std::span<const glm::vec3> lhs,
        const std::span<const glm::vec3> rhs)
    {
        if (lhs.size() != rhs.size() || lhs.empty())
        {
            const double infinity =
                std::numeric_limits<double>::infinity();
            return PositionError{.Rms = infinity, .Linf = infinity};
        }

        double sumSquared = 0.0;
        double maximum = 0.0;
        for (std::size_t index = 0u; index < lhs.size(); ++index)
        {
            const glm::dvec3 delta =
                glm::dvec3{lhs[index]} - glm::dvec3{rhs[index]};
            const double distance = glm::length(delta);
            sumSquared += distance * distance;
            maximum = std::max(maximum, distance);
        }
        return PositionError{
            .Rms = std::sqrt(sumSquared / static_cast<double>(lhs.size())),
            .Linf = maximum,
        };
    }

    class PointCloudConsolidationGpuParityApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit PointCloudConsolidationGpuParityApp(
            std::array<ParityFixture, 2u> fixtures)
            : Fixtures(std::move(fixtures))
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            for (std::size_t run = 0u; run < Entities.size(); ++run)
            {
                const ParityFixture& fixture = Fixtures[run / 2u];
                Entities[run] = Scene->Create();
                auto& vertices =
                    Scene->Raw().emplace<GS::Vertices>(Entities[run]);
                SetPositions(vertices, fixture.Input);
            }

            CompletionSubscription = Service->SubscribeCompleted(
                [this](
                    const Runtime::PointCloudConsolidationResult& result)
                {
                    Results[PendingRun] = result;
                    if (result.Succeeded())
                    {
                        const auto positions = Scene->Raw()
                            .get<GS::Vertices>(Entities[PendingRun])
                            .Properties.Get<glm::vec3>(
                                GS::PropertyNames::kPosition);
                        if (positions)
                            Outputs[PendingRun] = positions.Vector();
                    }
                    InFlight = false;
                    ++CompletedRuns;
                });
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Frames;
            if (!InFlight && NextRun < Entities.size() &&
                engine.GetDevice().IsOperational())
            {
                PendingRun = NextRun;
                InFlight = true;
                const ParityFixture& fixture = Fixtures[NextRun / 2u];
                Correlations[NextRun] = Service->Run(
                    Runtime::PointCloudConsolidationRequest{
                        .StableEntityId = Runtime::SelectionController::
                            ToStableEntityId(Entities[NextRun]),
                        .Config = fixture.Config,
                    });
                ++NextRun;
            }

            if (CompletedRuns == Entities.size())
            {
                Stats = Service->Stats();
                engine.RequestExit();
            }
            else if (Frames > 2'000u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        std::array<ParityFixture, 2u> Fixtures{};
        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::PointCloudConsolidationModuleStats Stats{};
        std::array<ECS::EntityHandle, 4u> Entities{};
        std::array<Runtime::CommandCorrelationId, 4u> Correlations{};
        std::array<std::optional<Runtime::PointCloudConsolidationResult>, 4u>
            Results{};
        std::array<std::vector<glm::vec3>, 4u> Outputs{};
        std::size_t NextRun{0u};
        std::size_t PendingRun{0u};
        std::size_t CompletedRuns{0u};
        std::uint32_t Frames{0u};
        bool MissingService{false};
        bool InFlight{false};
        bool TimedOut{false};
    };

    class PointCloudConsolidationGpuAutoMeshApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit PointCloudConsolidationGpuAutoMeshApp(
            Geometry::HalfedgeMesh::Mesh mesh,
            std::vector<glm::vec3> positions)
            : Mesh(std::move(mesh)), Input(std::move(positions))
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Extraction = engine.Services().Find<
                Runtime::RenderExtractionCache>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() ||
                Extraction == nullptr || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            Entity = Scene->Create();
            GS::PopulateFromMesh(Scene->Raw(), Entity, Mesh);
            Scene->Raw().emplace<
                Extrinsic::ECS::Components::Transform::WorldMatrix>(Entity);
            Scene->Raw().emplace<Graphics::Components::RenderSurface>(Entity);
            CompletionSubscription = Service->SubscribeCompleted(
                [this](
                    const Runtime::PointCloudConsolidationResult& result)
                {
                    Completion = result;
                    if (result.Succeeded())
                    {
                        const auto positions = Scene->Raw()
                            .get<GS::Vertices>(Entity)
                            .Properties.Get<glm::vec3>(
                                GS::PropertyNames::kPosition);
                        if (positions)
                            Output = positions.Vector();
                        TaggedGpuDirty = Scene->Raw().all_of<Dirty::GpuDirty>(
                            Entity);
                        TaggedPositionDirty = Scene->Raw().all_of<
                            Dirty::DirtyVertexPositions>(Entity);
                    }
                });
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Frames;
            if (!InitialResidencyCaptured &&
                engine.GetDevice().IsOperational())
            {
                const auto sidecar = Extraction->FindRenderableSidecarForTest(
                    Runtime::SelectionController::ToStableEntityId(Entity));
                Graphics::GpuGeometryResidencyView residency{};
                if (sidecar.has_value() && sidecar->HasMeshResidency &&
                    engine.GetRenderer().GetGpuWorld().
                        TryGetGeometryResidencyView(
                            sidecar->MeshGeometry, residency))
                {
                    InitialResidencyCaptured = true;
                    InitialPositionFingerprint =
                        residency.PositionFingerprint;
                    InitialContentRevision = residency.ContentRevision;
                }
            }

            if (InitialResidencyCaptured && !Submitted)
            {
                Submitted = true;
                Runtime::PointCloudConsolidationConfig config{};
                config.Backend =
                    Runtime::PointCloudConsolidationBackend::VulkanCompute;
                Correlation = Service->Run(
                    Runtime::PointCloudConsolidationRequest{
                        .StableEntityId = Runtime::SelectionController::
                            ToStableEntityId(Entity),
                        .Properties = Runtime::
                            MakePointCloudConsolidationPropertyRefs(
                                Runtime::GeometryElementDomain::MeshVertex,
                                std::string{GS::PropertyNames::kPosition},
                                std::nullopt),
                        .Config = config,
                    });
            }

            if (Completion.has_value() && Completion->Succeeded())
            {
                const auto sidecar = Extraction->FindRenderableSidecarForTest(
                    Runtime::SelectionController::ToStableEntityId(Entity));
                Graphics::GpuGeometryResidencyView residency{};
                if (sidecar.has_value() && sidecar->HasMeshResidency &&
                    engine.GetRenderer().GetGpuWorld().
                        TryGetGeometryResidencyView(
                            sidecar->MeshGeometry, residency) &&
                    residency.ContentRevision > InitialContentRevision &&
                    residency.PositionFingerprint !=
                        InitialPositionFingerprint)
                {
                    UpdatedResidencyObserved = true;
                    UpdatedPositionFingerprint =
                        residency.PositionFingerprint;
                    UpdatedContentRevision = residency.ContentRevision;
                    DirtyTagsConsumed = !Scene->Raw().any_of<
                        Dirty::GpuDirty,
                        Dirty::DirtyVertexPositions>(Entity);
                    Stats = Service->Stats();
                    engine.RequestExit();
                }
            }
            else if (Completion.has_value())
            {
                Stats = Service->Stats();
                engine.RequestExit();
            }
            else if (Frames > 4'000u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<glm::vec3> Input{};
        std::vector<glm::vec3> Output{};
        Runtime::PointCloudConsolidationService* Service{};
        Runtime::RenderExtractionCache* Extraction{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId Correlation{};
        std::optional<Runtime::PointCloudConsolidationResult> Completion{};
        Runtime::PointCloudConsolidationModuleStats Stats{};
        std::uint32_t Frames{0u};
        bool Submitted{false};
        bool MissingService{false};
        bool TimedOut{false};
        bool TaggedGpuDirty{false};
        bool TaggedPositionDirty{false};
        bool InitialResidencyCaptured{false};
        bool UpdatedResidencyObserved{false};
        bool DirtyTagsConsumed{false};
        std::uint64_t InitialPositionFingerprint{0u};
        std::uint64_t UpdatedPositionFingerprint{0u};
        std::uint64_t InitialContentRevision{0u};
        std::uint64_t UpdatedContentRevision{0u};
    };
}

TEST(PointCloudConsolidationGpuParity,
     VulkanLopAndIsotropicWlopMatchFrozenCpuReferencesAndRepeat)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP()
            << "GLFW could not initialize; gpu;vulkan LOP parity is opt-in.";
    }

    const std::array<ParityFixture, 2u> fixtures = MakeFixtures();
    std::array<Consolidation::Result, 2u> references{};
    for (std::size_t index = 0u; index < fixtures.size(); ++index)
    {
        references[index] = Consolidation::Consolidate(
            fixtures[index].Input,
            MakeCpuParams(fixtures[index].Config));
        ASSERT_TRUE(references[index].Succeeded() ||
                    references[index].State ==
                        Consolidation::Status::NotConverged)
            << fixtures[index].Name << ": "
            << Consolidation::DebugName(references[index].State)
            << ", iterations="
            << references[index].Diagnostics.Iterations
            << ", empty_neighborhoods="
            << references[index].Diagnostics.EmptyNeighborhoodCount;
    }

    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic LOP gpu;vulkan parity smoke";
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Window.Resizable = false;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;

    auto app = std::make_unique<PointCloudConsolidationGpuParityApp>(
        fixtures);
    PointCloudConsolidationGpuParityApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();

    const auto operationalInputs =
        Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(
            &engine.GetDevice());
    if (!operationalInputs.LogicalDeviceReady ||
        !operationalInputs.SwapchainReady ||
        !operationalInputs.CommandSyncReady)
    {
        engine.Shutdown();
        GTEST_SKIP()
            << "Promoted Vulkan did not reach device/swapchain/command readiness.";
    }

    const auto validationBefore = Extrinsic::Backends::Vulkan::
        GetVulkanOperationalDiagnosticsSnapshot();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_EQ(appPtr->CompletedRuns, 4u);
    for (std::size_t run = 0u; run < 4u; ++run)
    {
        const std::size_t fixtureIndex = run / 2u;
        SCOPED_TRACE(fixtures[fixtureIndex].Name +
                     " run " + std::to_string(run % 2u));
        ASSERT_TRUE(appPtr->Correlations[run].IsValid());
        ASSERT_TRUE(appPtr->Results[run].has_value());
        const Runtime::PointCloudConsolidationResult& result =
            *appPtr->Results[run];
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.RequestedBackend,
                  Runtime::PointCloudConsolidationBackend::VulkanCompute);
        EXPECT_EQ(result.ActualBackend,
                  Runtime::PointCloudConsolidationBackend::VulkanCompute);
        EXPECT_FALSE(result.FellBackToCpu) << result.BackendDiagnostic;
        EXPECT_EQ(result.ImplementationId, "gpu_vulkan_compute");
        EXPECT_EQ(result.GeometryStatus, references[fixtureIndex].State);
        EXPECT_EQ(result.OutputPointCount,
                  references[fixtureIndex].Positions.size());
        EXPECT_EQ(result.Iterations,
                  references[fixtureIndex].Diagnostics.Iterations);
        EXPECT_EQ(result.Converged,
                  references[fixtureIndex].Diagnostics.Converged);
        ASSERT_EQ(appPtr->Outputs[run].size(),
                  references[fixtureIndex].Positions.size());
        EXPECT_TRUE(std::all_of(
            appPtr->Outputs[run].begin(),
            appPtr->Outputs[run].end(),
            [](const glm::vec3& position)
            {
                return std::isfinite(position.x) &&
                    std::isfinite(position.y) &&
                    std::isfinite(position.z);
            }));
        const PositionError parity = MeasurePositionError(
            appPtr->Outputs[run], references[fixtureIndex].Positions);
        EXPECT_LE(parity.Rms, kRmsTolerance);
        EXPECT_LE(parity.Linf, kLinfTolerance);
    }

    for (std::size_t fixtureIndex = 0u;
         fixtureIndex < fixtures.size();
         ++fixtureIndex)
    {
        const PositionError repeat = MeasurePositionError(
            appPtr->Outputs[fixtureIndex * 2u],
            appPtr->Outputs[fixtureIndex * 2u + 1u]);
        EXPECT_LE(repeat.Rms, kRmsTolerance)
            << fixtures[fixtureIndex].Name;
        EXPECT_LE(repeat.Linf, kLinfTolerance)
            << fixtures[fixtureIndex].Name;
    }

    EXPECT_EQ(appPtr->Stats.GpuRequestsAccepted, 4u);
    EXPECT_EQ(appPtr->Stats.GpuFallbacks, 0u);
    EXPECT_EQ(appPtr->Stats.GpuCompletions, 4u);
    EXPECT_EQ(appPtr->Stats.ResultsCommitted, 4u);

    engine.Shutdown();
    const auto validationAfter = Extrinsic::Backends::Vulkan::
        GetVulkanOperationalDiagnosticsSnapshot();
    EXPECT_EQ(validationAfter.VulkanValidationErrorCount,
              validationBefore.VulkanValidationErrorCount);
}

TEST(PointCloudConsolidationGpuParity,
     VulkanAutoProcessesChildMeshPositionsAndPublishesDisplacement)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP()
            << "GLFW could not initialize; gpu;vulkan LOP smoke is opt-in.";
    }

    const std::string path =
        std::string{ENGINE_ROOT_DIR} + "/assets/models/child.obj";
    const auto mesh = Geometry::MeshIO::LoadOBJ(path);
    ASSERT_TRUE(mesh.has_value());
    const auto meshPositions = mesh->Vertices.Get<glm::vec3>("v:point");
    ASSERT_TRUE(meshPositions.IsValid());
    ASSERT_EQ(meshPositions.Vector().size(), 50'002u);
    const auto meshFaces = mesh->Faces.Get<std::vector<std::uint32_t>>(
        "f:vertices");
    ASSERT_TRUE(meshFaces.IsValid());
    Geometry::MeshSoup::IndexedMesh indexedMesh{};
    for (const glm::vec3 position : meshPositions.Vector())
        static_cast<void>(indexedMesh.AddVertex(position));
    for (const std::vector<std::uint32_t>& face : meshFaces.Vector())
        static_cast<void>(indexedMesh.AddFace(face));
    auto runtimeMesh = Geometry::Mesh::Conversion::ToHalfedgeMesh(indexedMesh);
    ASSERT_TRUE(runtimeMesh.Succeeded());
    ASSERT_EQ(runtimeMesh.Mesh.VerticesSize(), 50'002u);

    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic child.obj LOP Vulkan Auto smoke";
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Window.Resizable = false;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;

    std::vector<glm::vec3> input = meshPositions.Vector();
    auto app = std::make_unique<PointCloudConsolidationGpuAutoMeshApp>(
        std::move(runtimeMesh.Mesh), input);
    PointCloudConsolidationGpuAutoMeshApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();

    const auto operationalInputs =
        Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(
            &engine.GetDevice());
    if (!operationalInputs.LogicalDeviceReady ||
        !operationalInputs.SwapchainReady ||
        !operationalInputs.CommandSyncReady)
    {
        engine.Shutdown();
        GTEST_SKIP()
            << "Promoted Vulkan did not reach device/swapchain/command readiness.";
    }

    const auto validationBefore = Extrinsic::Backends::Vulkan::
        GetVulkanOperationalDiagnosticsSnapshot();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Correlation.IsValid());
    ASSERT_TRUE(appPtr->Completion.has_value());
    const Runtime::PointCloudConsolidationResult& result =
        *appPtr->Completion;
    ASSERT_TRUE(result.Succeeded())
        << result.Message
        << "; actual_backend=" << Runtime::StableToken(result.ActualBackend)
        << "; radius=" << result.ResolvedSupportRadius
        << "; requested_rank="
        << result.SupportRadiusRequestedNeighborRank
        << "; selected_rank=" << result.SupportRadiusNeighborRank
        << "; support_p95=" << result.SupportNeighborsP95
        << "; support_max=" << result.SupportNeighborsMax
        << "; backend_diagnostic=" << result.BackendDiagnostic;
    EXPECT_EQ(result.RequestedBackend,
              Runtime::PointCloudConsolidationBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::PointCloudConsolidationBackend::VulkanCompute);
    EXPECT_FALSE(result.FellBackToCpu) << result.BackendDiagnostic;
    EXPECT_EQ(result.ImplementationId, "gpu_vulkan_compute");
    EXPECT_EQ(result.SupportRadiusAnalysisStatus, "success");
    EXPECT_EQ(result.SupportRadiusRequestedNeighborRank, 16u);
    EXPECT_TRUE(result.SupportRadiusWorkloadAdjusted);
    EXPECT_EQ(result.SupportRadiusNeighborRank, 5u);
    EXPECT_DOUBLE_EQ(result.SupportNeighborsP95, 30.0);
    EXPECT_EQ(result.SupportNeighborsMax, 45u);
    EXPECT_EQ(result.PredictedSupportQueryCount, 3'100'124u);
    EXPECT_EQ(result.PredictedContributionCount, 93'003'720u);
    EXPECT_EQ(result.OutputPointCount, 50'002u);
    EXPECT_GT(result.Iterations, 0u);
    EXPECT_GT(result.AverageDisplacement, 0.0);
    EXPECT_GT(result.MaxDisplacement, 0.0);
    ASSERT_EQ(appPtr->Output.size(), appPtr->Input.size());
    const PositionError displacement = MeasurePositionError(
        appPtr->Output, appPtr->Input);
    EXPECT_GT(displacement.Rms, 0.0);
    EXPECT_GT(displacement.Linf, 0.0);
    EXPECT_TRUE(appPtr->TaggedGpuDirty);
    EXPECT_TRUE(appPtr->TaggedPositionDirty);
    EXPECT_TRUE(appPtr->InitialResidencyCaptured);
    EXPECT_TRUE(appPtr->UpdatedResidencyObserved);
    EXPECT_TRUE(appPtr->DirtyTagsConsumed);
    EXPECT_NE(appPtr->InitialPositionFingerprint, 0u);
    EXPECT_NE(appPtr->UpdatedPositionFingerprint,
              appPtr->InitialPositionFingerprint);
    EXPECT_GT(appPtr->UpdatedContentRevision,
              appPtr->InitialContentRevision);
    EXPECT_EQ(appPtr->Stats.GpuRequestsAccepted, 1u);
    EXPECT_EQ(appPtr->Stats.GpuFallbacks, 0u);
    EXPECT_EQ(appPtr->Stats.GpuCompletions, 1u);
    EXPECT_EQ(appPtr->Stats.ResultsCommitted, 1u);

    RecordProperty(
        "ResolvedSupportRadius",
        std::to_string(result.ResolvedSupportRadius));
    RecordProperty(
        "RequestedNeighborRank",
        std::to_string(result.SupportRadiusRequestedNeighborRank));
    RecordProperty(
        "SelectedNeighborRank",
        std::to_string(result.SupportRadiusNeighborRank));
    RecordProperty(
        "SupportNeighborsP95",
        std::to_string(result.SupportNeighborsP95));
    RecordProperty(
        "SupportNeighborsMax",
        std::to_string(result.SupportNeighborsMax));
    RecordProperty(
        "PredictedSupportQueries",
        std::to_string(result.PredictedSupportQueryCount));
    RecordProperty(
        "PredictedContributions",
        std::to_string(result.PredictedContributionCount));
    RecordProperty(
        "AverageDisplacement",
        std::to_string(result.AverageDisplacement));
    RecordProperty(
        "MaximumDisplacement",
        std::to_string(result.MaxDisplacement));
    RecordProperty(
        "InitialPositionFingerprint",
        std::to_string(appPtr->InitialPositionFingerprint));
    RecordProperty(
        "UpdatedPositionFingerprint",
        std::to_string(appPtr->UpdatedPositionFingerprint));
    RecordProperty(
        "InitialContentRevision",
        std::to_string(appPtr->InitialContentRevision));
    RecordProperty(
        "UpdatedContentRevision",
        std::to_string(appPtr->UpdatedContentRevision));

    engine.Shutdown();
    const auto validationAfter = Extrinsic::Backends::Vulkan::
        GetVulkanOperationalDiagnosticsSnapshot();
    EXPECT_EQ(validationAfter.VulkanValidationErrorCount,
              validationBefore.VulkanValidationErrorCount);
}
