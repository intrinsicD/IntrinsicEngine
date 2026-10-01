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
#include "Modules/Clustering/Runtime.KMeansPaging.TestSupport.hpp"
#include <cstring>
#include <format>
#include <chrono>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
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

    [[nodiscard]] GK::KMeansParams MakeCpuParameters(const Runtime::KMeansParameters& parameters)
    {
        GK::KMeansParams params{};
        params.ClusterCount = parameters.ClusterCount;
        params.MaxIterations = parameters.MaxIterations;
        params.Seed = parameters.Seed;
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
        std::vector<glm::vec3> Points{kPoints.begin(),kPoints.end()};
        Runtime::KMeansParameters Parameters{kParameters};
        bool Deleted{};
        std::chrono::steady_clock::time_point Started{std::chrono::steady_clock::now()};
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
            SetPositions(vertices, Points);
            if(Deleted)vertices.Properties.GetOrAdd<bool>("v:deleted",false)[1]=true;
            (void)vertices.Properties.GetOrAdd<std::uint32_t>("p:kmeans_label",9u);
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
                    .Parameters = Parameters,
                    .Backend = Runtime::ClusteringBackend::VulkanCompute,
                    .AutoAccept = Repeated,
                });
                Submitted = true;
            }

            if (Submitted && !Repeated && Service->GpuRun(Correlation).ReadyToAccept && !Preview)
            {
                auto* cache=engine.Services().Find<Runtime::SpatialIndexCache>();
                auto& residency=*cache->PropertyResidency();
                const auto refs=Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::PointCloudPoint);
                auto& scene=*engine.Worlds().Get(engine.ActiveWorld());
                const auto observed=Runtime::ObserveGpuPropertyFront(residency,scene,engine.ActiveWorld(),Entity,refs.OutputLabels);
                if(observed){
                    PreviewObserved=true;
                    const auto front=residency.Front(Runtime::MakeGpuPropertyKey(engine.ActiveWorld(),Entity,
                        Runtime::GpuPropertyPresentationRef(refs.OutputLabels)));
                    const auto typed=residency.Front(Runtime::MakeGpuPropertyKey(engine.ActiveWorld(),Entity,refs.OutputLabels));
                    if(typed)TypedPreview=cache->QueueGpuCompute(typed->Bytes,[typed](auto& cmd,const auto&){
                        cmd.BufferBarrier(typed->Buffer,Extrinsic::RHI::MemoryAccess::ShaderWrite,Extrinsic::RHI::MemoryAccess::TransferRead);
                        return typed->Buffer;
                    },Runtime::SpatialGpuLatency::Immediate);
                    if(front)Preview=cache->QueueGpuCompute(front->Bytes,[front](auto& cmd,const auto&){
                        cmd.BufferBarrier(front->Buffer,Extrinsic::RHI::MemoryAccess::ShaderWrite,
                            Extrinsic::RHI::MemoryAccess::TransferRead);return front->Buffer;
                    },Runtime::SpatialGpuLatency::Immediate);
                }
            }
            if(Preview && Preview->State==Runtime::SpatialQueryState::Ready && TypedPreview && TypedPreview->State==Runtime::SpatialQueryState::Ready && !DiscardSent && Service->GpuRun(Correlation).ReadyToAccept){
                PreviewLabels.resize(Preview->Data.size()/4);std::memcpy(PreviewLabels.data(),Preview->Data.data(),Preview->Data.size());
                TypedLabels.resize(TypedPreview->Data.size()/4);std::memcpy(TypedLabels.data(),TypedPreview->Data.data(),TypedPreview->Data.size());
                (void)Service->GpuRun(Correlation,Runtime::KMeansGpuAction::Discard);DiscardSent=true;
            }
            if(Completion && !Repeated){
                auto& properties=engine.Worlds().Get(engine.ActiveWorld())->Raw().get<GS::Vertices>(Entity).Properties;
                const auto labels=std::as_const(properties).Get<std::uint32_t>("p:kmeans_label");
                DiscardRestored=Completion->Status==Runtime::KMeansRunStatus::Cancelled && labels &&
                    std::ranges::all_of(labels.Vector(),[](auto value){return value==9u;}) && !properties.Exists("p:kmeans_color");
                FirstUploadBytes=Completion->GpuInputUploadBytes;FirstCentroids=Completion->Centroids;
                FirstBuffersCreated=Completion->GpuWorkspaceBuffersCreated;FirstPipelinesCreated=Completion->GpuWorkspacePipelinesCreated;
                Completion.reset();Repeated=true;Submitted=false;return;
            }
            if (Completion.has_value())
            {
                CaptureCommittedProperties();
                Stats = Service->Stats();
                engine.RequestExit();
                return;
            }

            if (std::chrono::steady_clock::now()-Started > std::chrono::seconds(45))
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
        bool Repeated{}, PreviewObserved{}, DiscardSent{}, DiscardRestored{};
        std::uint64_t FirstUploadBytes{};
        std::uint32_t FirstBuffersCreated{}, FirstPipelinesCreated{};
        std::shared_ptr<Runtime::SpatialGpuResult> Preview{},TypedPreview{};
        std::vector<std::uint32_t> TypedLabels{};
        std::vector<glm::vec3> FirstCentroids{};
        std::vector<float> PreviewLabels{};
    };
}

class ClusteringServiceGpuSmoke : public ::testing::TestWithParam<int> {};

TEST_P(ClusteringServiceGpuSmoke,
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

    struct RestorePaging { Runtime::KMeansPagingLimits Before{Runtime::KMeansPagingForTesting};
        ~RestorePaging(){Runtime::KMeansPagingForTesting=Before;} } restore;
    Runtime::KMeansPagingForTesting={.PagePairs=2,.SubmissionPairs=8};
    auto app = std::make_unique<ClusteringServiceGpuApp>();
    if(GetParam()==1){
        app->Parameters.ClusterCount=3;
        // Repeated seeds tie across centroid pages; index 1 also beats index 0.
        app->Points={{0,0,0},{0,0,0},{0,0,0},{4,0,0},{4,0,0},{4,0,0}};
    }
    if(GetParam()==2){
        app->Parameters.ClusterCount=8;app->Deleted=true;app->Points.clear();
        std::uint32_t random=17;
        for(unsigned i=0;i<2049;++i){
            glm::vec3 point{float(i%8)*10,0,0};
            for(int axis=0;axis<3;++axis){random=random*1664525u+1013904223u;point[axis]+=float(random>>8)/float(1u<<24);}
            app->Points.push_back(point);
        }
        Runtime::KMeansPagingForTesting={.PagePairs=256,.SubmissionPairs=1u<<18};
    }
    ClusteringServiceGpuApp* appPtr = app.get();
    auto livePoints=appPtr->Points;if(appPtr->Deleted)livePoints.erase(livePoints.begin()+1);
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.EmplaceModule<Runtime::ClusteringModule>();
    engine.Initialize();

    // IsOperational() stays false until the first clean frame; gate on bootstrap readiness.
    const auto ready = Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(&engine.GetDevice());
    if (!ready.LogicalDeviceReady || !ready.SwapchainReady || !ready.CommandSyncReady)
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
    EXPECT_EQ(appPtr->Completion->ImplementationId,"vulkan_resident_paged_lloyd");
    EXPECT_TRUE(appPtr->PreviewObserved);EXPECT_TRUE(appPtr->DiscardRestored);
    EXPECT_EQ(appPtr->FirstCentroids,appPtr->Completion->Centroids);
    EXPECT_EQ(appPtr->FirstUploadBytes,appPtr->Points.size()*(appPtr->Deleted?16u:12u));
    EXPECT_EQ(appPtr->Completion->GpuInputUploadBytes,0u);
    EXPECT_GT(appPtr->Completion->GpuInputCacheHits,0u);
    // The discarded first run created the workspace; the repeat run reuses it.
    EXPECT_GT(appPtr->FirstBuffersCreated,0u);EXPECT_EQ(appPtr->FirstPipelinesCreated,3u);
    EXPECT_EQ(appPtr->Completion->GpuWorkspaceBuffersCreated,0u);
    EXPECT_EQ(appPtr->Completion->GpuWorkspacePipelinesCreated,0u);
    // Case 2's budgets fit a whole iteration (and a due preview) per submission:
    // one per iteration plus the terminal preview/centroid submission.
    if(GetParam()==2)EXPECT_EQ(appPtr->Completion->GpuSubmissions,appPtr->Completion->Iterations+1u);
    else EXPECT_GT(appPtr->Completion->GpuSubmissions,appPtr->Completion->Iterations+1u);
    EXPECT_EQ(appPtr->Completion->LabelCount, livePoints.size());
    EXPECT_EQ(appPtr->Completion->ClusterCount,
              appPtr->Parameters.ClusterCount);
    EXPECT_TRUE(appPtr->ColorsCommitted);
    ASSERT_TRUE(appPtr->LabelsChanged.has_value());
    EXPECT_EQ(appPtr->LabelsChanged->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->LabelsChanged->Labels.Name, "p:kmeans_label");
    EXPECT_EQ(appPtr->LabelsChanged->Colors.Name, "p:kmeans_color");

    const std::optional<GK::KMeansResult> cpu = GK::Cluster(
        std::span<const glm::vec3>{livePoints},
        MakeCpuParameters(appPtr->Parameters));
    ASSERT_TRUE(cpu.has_value());
    ASSERT_EQ(appPtr->Labels.size(),appPtr->Points.size());
    ASSERT_EQ(appPtr->TypedLabels.size(),appPtr->Points.size());
    ASSERT_EQ(appPtr->PreviewLabels.size(),appPtr->Points.size());
    if(appPtr->Deleted){
        EXPECT_EQ(appPtr->Labels[1],9u);EXPECT_EQ(appPtr->TypedLabels[1],9u);EXPECT_EQ(appPtr->PreviewLabels[1],9.f);
        appPtr->Labels.erase(appPtr->Labels.begin()+1);appPtr->TypedLabels.erase(appPtr->TypedLabels.begin()+1);
        appPtr->PreviewLabels.erase(appPtr->PreviewLabels.begin()+1);
    }
    ASSERT_EQ(appPtr->Labels.size(), cpu->Labels.size());
    EXPECT_EQ(appPtr->TypedLabels,cpu->Labels);
    const auto mismatches=CountLabelMismatches(appPtr->Labels,cpu->Labels);
    RecordProperty("label_mismatch_count",std::format("{:.17g}",double(mismatches)));EXPECT_EQ(mismatches,0u);
    ASSERT_EQ(appPtr->Completion->Centroids.size(),cpu->Centroids.size());
    float delta=0;
    for(std::size_t i=0;i<cpu->Centroids.size();++i)for(int axis=0;axis<3;++axis)
        delta=std::max(delta,std::abs(appPtr->Completion->Centroids[i][axis]-cpu->Centroids[i][axis]));
    RecordProperty("centroid_linf_delta",std::format("{:.17g}",delta));EXPECT_LE(delta,1e-5f);
    EXPECT_EQ(appPtr->Completion->Iterations,cpu->Iterations);
    EXPECT_EQ(appPtr->Completion->Converged,cpu->Converged);
    ASSERT_EQ(appPtr->PreviewLabels.size(),cpu->Labels.size());
    for(std::size_t i=0;i<cpu->Labels.size();++i)EXPECT_EQ(appPtr->PreviewLabels[i],float(cpu->Labels[i]));
    EXPECT_LE(std::abs(appPtr->Completion->Inertia - cpu->Inertia), 1.0e-4f*std::max(1.f,cpu->Inertia));
    EXPECT_EQ(appPtr->Completion->MaxDistanceIndex,
              cpu->MaxDistanceIndex);

    EXPECT_EQ(appPtr->Stats.GpuRequestsAccepted, 2u);
    EXPECT_EQ(appPtr->Stats.GpuFallbacks, 0u);
    EXPECT_EQ(appPtr->Stats.GpuCompletions, 1u);
    EXPECT_EQ(appPtr->Stats.LabelsCommitted, 1u);
    EXPECT_EQ(appPtr->Stats.VisualizationRefreshReactions, 1u);

    engine.Shutdown();
}

INSTANTIATE_TEST_SUITE_P(ResidentPages,ClusteringServiceGpuSmoke,::testing::Values(0,1,2));
