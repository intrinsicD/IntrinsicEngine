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
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SelectionController;
import Geometry.PointCloud.Consolidation;
import Geometry.Properties;

namespace
{
    namespace ECS = Extrinsic::ECS;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
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
    config.Render.EnableValidation = false;
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
}
