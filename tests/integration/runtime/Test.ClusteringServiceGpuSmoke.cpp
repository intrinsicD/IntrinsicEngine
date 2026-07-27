#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Geometry.KMeans;
import Geometry.Properties;

namespace
{
    namespace ECS = Extrinsic::ECS;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
    namespace Runtime = Extrinsic::Runtime;
    namespace GK = Geometry::KMeans;

    constexpr std::array<glm::vec3, 6> kPoints{{
        glm::vec3{-2.0f, 0.0f, 0.0f},
        glm::vec3{-1.0f, 1.0f, 0.0f},
        glm::vec3{-1.0f, -1.0f, 0.0f},
        glm::vec3{8.0f, 0.0f, 0.0f},
        glm::vec3{9.0f, 1.0f, 0.0f},
        glm::vec3{9.0f, -1.0f, 0.0f},
    }};

    constexpr Runtime::KMeansParameters kParameters{
        .ClusterCount = 2u,
        .MaxIterations = 6u,
        .Seed = 13u,
        .Initialization = Runtime::KMeansInitialization::Hierarchical,
    };

    void SetPositions(GS::Vertices& vertices,
                      std::span<const glm::vec3> positions)
    {
        vertices.Properties.Resize(positions.size());
        auto property = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition}, glm::vec3{0.0f});
        property.Vector().assign(positions.begin(), positions.end());
    }

    [[nodiscard]] GK::KMeansParams MakeCpuParameters()
    {
        GK::KMeansParams params{};
        params.ClusterCount = kParameters.ClusterCount;
        params.MaxIterations = kParameters.MaxIterations;
        params.Seed = kParameters.Seed;
        params.Init = GK::Initialization::Hierarchical;
        params.Compute = GK::Backend::CPU;
        return params;
    }

    [[nodiscard]] std::uint32_t CountLabelMismatches(
        std::span<const std::uint32_t> lhs,
        std::span<const std::uint32_t> rhs)
    {
        std::uint32_t mismatches = 0u;
        const std::size_t count = std::min(lhs.size(), rhs.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            if (lhs[index] != rhs[index])
                ++mismatches;
        }
        return mismatches + static_cast<std::uint32_t>(
                                std::max(lhs.size(), rhs.size()) - count);
    }

    class ClusteringServiceGpuApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<Runtime::ClusteringService>();
            if (Service == nullptr || !Service->Available())
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
            Entity = scene.Create();
            auto& vertices = scene.Raw().emplace<GS::Vertices>(Entity);
            SetPositions(vertices, kPoints);
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);

            CompletionSubscription = Service->SubscribeRunCompleted(
                [this](const Runtime::KMeansRunCompleted& completed)
                { Completion = completed; });
            ChangedSubscription = Service->SubscribeClusterLabelsChanged(
                [this](const Runtime::ClusterLabelsChanged& changed)
                { LabelsChanged = changed; });
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Frames;

            if (!Submitted && Service != nullptr &&
                engine.GetDevice().IsOperational())
            {
                Correlation = Service->RunKMeans(Runtime::RunKMeans{
                    .StableEntityId = StableEntityId,
                    .Properties = Runtime::MakeKMeansPropertyRefs(
                        Runtime::GeometryElementDomain::PointCloudPoint),
                    .Parameters = kParameters,
                    .Backend = Runtime::ClusteringBackend::VulkanCompute,
                });
                Submitted = true;
            }

            if (Completion.has_value())
            {
                CaptureCommittedProperties();
                Stats = Service->Stats();
                engine.RequestExit();
                return;
            }

            if (Frames > 600u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override {}

        void CaptureCommittedProperties()
        {
            auto& scene = *Kernel().Worlds().Get(Kernel().ActiveWorld());
            if (!scene.Raw().valid(Entity) ||
                !scene.Raw().all_of<GS::Vertices>(Entity))
            {
                return;
            }

            auto& properties =
                scene.Raw().get<GS::Vertices>(Entity).Properties;
            const auto labels = properties.Get<std::uint32_t>(
                "p:kmeans_label");
            if (labels)
                Labels = labels.Vector();
            ColorsCommitted = static_cast<bool>(
                properties.Get<glm::vec4>("p:kmeans_color"));
        }

        Runtime::ClusteringService* Service{};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::KernelEventSubscription ChangedSubscription{};
        Runtime::CommandCorrelationId Correlation{};
        Runtime::ClusteringModuleStats Stats{};
        std::optional<Runtime::KMeansRunCompleted> Completion{};
        std::optional<Runtime::ClusterLabelsChanged> LabelsChanged{};
        std::vector<std::uint32_t> Labels{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::uint32_t Frames{0u};
        bool MissingService{false};
        bool Submitted{false};
        bool TimedOut{false};
        bool ColorsCommitted{false};
    };
}

TEST(ClusteringServiceGpuSmoke,
     VulkanExecutionMatchesCpuReferenceAndCommitsCanonicalProperties)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP()
            << "GLFW could not initialize; gpu;vulkan clustering smoke is opt-in.";
    }

    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic ClusteringService gpu;vulkan parity smoke";
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;

    auto app = std::make_unique<ClusteringServiceGpuApp>();
    ClusteringServiceGpuApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::ClusteringModule>();
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
    EXPECT_TRUE(appPtr->Submitted);
    ASSERT_TRUE(appPtr->Correlation.IsValid());
    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->Completion->RequestedBackend,
              Runtime::ClusteringBackend::VulkanCompute);
    EXPECT_EQ(appPtr->Completion->ActualBackend,
              Runtime::ClusteringBackend::VulkanCompute);
    EXPECT_FALSE(appPtr->Completion->FellBackToCpu)
        << appPtr->Completion->BackendDiagnostic;
    EXPECT_EQ(appPtr->Completion->LabelCount, kPoints.size());
    EXPECT_EQ(appPtr->Completion->ClusterCount,
              kParameters.ClusterCount);
    EXPECT_TRUE(appPtr->ColorsCommitted);
    ASSERT_TRUE(appPtr->LabelsChanged.has_value());
    EXPECT_EQ(appPtr->LabelsChanged->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->LabelsChanged->Labels.Name, "p:kmeans_label");
    EXPECT_EQ(appPtr->LabelsChanged->Colors.Name, "p:kmeans_color");

    const std::optional<GK::KMeansResult> cpu = GK::Cluster(
        std::span<const glm::vec3>{kPoints.data(), kPoints.size()},
        MakeCpuParameters());
    ASSERT_TRUE(cpu.has_value());
    ASSERT_EQ(appPtr->Labels.size(), cpu->Labels.size());
    EXPECT_EQ(CountLabelMismatches(appPtr->Labels, cpu->Labels), 0u);
    EXPECT_LT(std::abs(appPtr->Completion->Inertia - cpu->Inertia), 1.0e-4f);
    EXPECT_EQ(appPtr->Completion->MaxDistanceIndex,
              cpu->MaxDistanceIndex);

    EXPECT_EQ(appPtr->Stats.GpuRequestsAccepted, 1u);
    EXPECT_EQ(appPtr->Stats.GpuFallbacks, 0u);
    EXPECT_EQ(appPtr->Stats.GpuCompletions, 1u);
    EXPECT_EQ(appPtr->Stats.LabelsCommitted, 1u);
    EXPECT_EQ(appPtr->Stats.VisualizationRefreshReactions, 1u);

    engine.Shutdown();
}
