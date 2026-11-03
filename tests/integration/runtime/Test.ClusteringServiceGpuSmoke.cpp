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
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.StepRuns;
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

    // A 64x64 window without validation or vsync.
    Extrinsic::Core::Config::EngineConfig KMeansSmokeConfig(const char* title)
    {
        auto config = Runtime::CreateReferenceEngineConfig();
        config.Window.Title = title;
        config.Window.Width = 64;
        config.Window.Height = 64;
        config.Window.Resizable = false;
        config.Render.EnableValidation = false;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        return config;
    }

    // What a GPU K-Means run needs: residency, the JobService its editor jobs submit to, and step runs.
    void EmplaceKMeansModules(Intrinsic::Tests::RuntimeTestKernel& engine)
    {
        engine.EmplaceModule<Runtime::SpatialIndexCache>();
        engine.EmplaceModule<Runtime::AsyncWorkModule>();
        engine.EmplaceModule<Runtime::ClusteringModule>();
        engine.EmplaceModule<Runtime::StepRunsModule>();
    }

    // IsOperational() stays false until the first clean frame; gate on bootstrap readiness (shut down otherwise).
    bool VulkanReady(Intrinsic::Tests::RuntimeTestKernel& engine)
    {
        const auto ready = Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(&engine.GetDevice());
        if (ready.LogicalDeviceReady && ready.SwapchainReady && ready.CommandSyncReady) return true;
        engine.Shutdown();
        return false;
    }
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

    const auto config = KMeansSmokeConfig("Intrinsic ClusteringService gpu;vulkan parity smoke");

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
    if(GetParam()==3){
        app->Points={{0,0,0},{1,0,0},{6,0,0},{7,0,0},{10,0,0}};
        app->Parameters.ClusterCount=2;
        app->Parameters.MaxIterations=1;
        Runtime::KMeansPagingForTesting={.PagePairs=256,.SubmissionPairs=1u<<18};
    }
    ClusteringServiceGpuApp* appPtr = app.get();
    auto livePoints=appPtr->Points;if(appPtr->Deleted)livePoints.erase(livePoints.begin()+1);
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    EmplaceKMeansModules(engine);
    engine.Initialize();
    if (!VulkanReady(engine)) GTEST_SKIP() << "Promoted Vulkan did not reach device/swapchain/command readiness.";

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
    else if(GetParam()==3)EXPECT_EQ(appPtr->Completion->GpuSubmissions,3u);
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

    if(GetParam()==3){
        // Hierarchical seeds are {4.8,10}; one update returns {3.5,10}.
        // Row3 crosses their Voronoi boundary and must be assigned again.
        const std::vector<std::uint32_t> labels{0,0,0,1,1};
        EXPECT_EQ(appPtr->TypedLabels,labels);
        ASSERT_EQ(appPtr->Completion->Centroids.size(),2u);
        EXPECT_EQ(appPtr->Completion->Centroids[0],glm::vec3(3.5f,0,0));
        EXPECT_EQ(appPtr->Completion->Centroids[1],glm::vec3(10,0,0));
        EXPECT_FLOAT_EQ(appPtr->Completion->Inertia,33.75f);
        EXPECT_EQ(appPtr->Completion->MaxDistanceIndex,0u);
        EXPECT_EQ(appPtr->Completion->Iterations,1u);
        EXPECT_FALSE(appPtr->Completion->Converged);
    }

    EXPECT_EQ(appPtr->Stats.GpuRequestsAccepted, 2u);
    EXPECT_EQ(appPtr->Stats.GpuFallbacks, 0u);
    EXPECT_EQ(appPtr->Stats.GpuCompletions, 1u);
    EXPECT_EQ(appPtr->Stats.LabelsCommitted, 1u);
    EXPECT_EQ(appPtr->Stats.VisualizationRefreshReactions, 1u);

    engine.Shutdown();
}

INSTANTIATE_TEST_SUITE_P(ResidentPages,ClusteringServiceGpuSmoke,::testing::Values(0,1,2,3));

namespace
{
    // Runs K-Means on one cloud once per drive, each driven through its step run, accepting it once it ended (or, for
    // StepOnceAccept, from its first rest), and keeps every committed outcome.
    class ClusteringStepDrivesApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        enum class Drive { Run, StepsOfOne, StepsOfThreeThenRun, RunWithPauses, StepOnceAccept };
        struct Outcome
        {
            Runtime::KMeansRunCompleted Completion{};
            std::vector<std::uint32_t> Labels{};
            std::vector<std::uint32_t> RestSteps{}; // the step count of each frame the run was seen paused, before it ended
        };
        std::vector<Drive> Drives{};
        std::vector<glm::vec3> Points{};
        Runtime::KMeansParameters Parameters{};
        std::vector<Outcome> Outcomes{};
        bool TimedOut{};

        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<Runtime::ClusteringService>();
            Steps = engine.Services().Find<Runtime::StepRunsService>();
            auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
            Entity = scene.Create();
            SetPositions(scene.Raw().emplace<GS::Vertices>(Entity), Points);
            Subscription = Service->SubscribeRunCompleted([this](const auto& completed) { Completion = completed; });
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(60)) TimedOut = true;
            if (TimedOut || Outcomes.size() == Drives.size() || Service == nullptr || Steps == nullptr)
            {
                engine.RequestExit();
                return;
            }
            if (!engine.GetDevice().IsOperational()) return;
            if (!Submitted)
            {
                Correlation = Service->RunKMeans({.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity),
                    .Properties = Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::PointCloudPoint),
                    .Parameters = Parameters, .Backend = Runtime::ClusteringBackend::VulkanCompute, .AutoAccept = false});
                Submitted = true;
                Current = {};
                Paused = Stepped = Accepted = false;
                return;
            }
            if (Completion)
            {
                const auto& properties = engine.Worlds().Get(engine.ActiveWorld())->Raw().get<GS::Vertices>(Entity).Properties;
                if (const auto labels = properties.Get<std::uint32_t>("p:kmeans_label")) Current.Labels = labels.Vector();
                Current.Completion = *Completion;
                Outcomes.push_back(std::move(Current));
                Completion.reset();
                Submitted = false;
                return;
            }
            const auto gpu = Service->GpuRun(Correlation);
            const auto observed = gpu.StepRun != 0u ? Steps->Observe(gpu.StepRun) : std::nullopt;
            if (!observed || Accepted) return;
            const auto& run = *observed;
            const bool resting = run.Phase == Runtime::StepRunPhase::Paused && !run.SegmentInFlight;
            const auto control = [&](const Runtime::StepRunAction action, const std::uint32_t count = 1u)
            {
                std::string refusal;
                (void)Steps->Control(gpu.StepRun, action, count, refusal);
            };
            const auto accept = [&]
            {
                (void)Service->GpuRun(Correlation, Runtime::KMeansGpuAction::Accept);
                Accepted = true;
            };
            if (run.End != Runtime::StepRunEnd::None)
            {
                if (resting && gpu.ReadyToAccept && !gpu.Running) accept();
                return;
            }
            if (resting) Current.RestSteps.push_back(run.Steps);
            switch (Drives[Outcomes.size()])
            {
            case Drive::Run: break;
            case Drive::StepsOfOne:
                if (!Paused) control(Runtime::StepRunAction::Pause), Paused = true;
                else if (resting) control(Runtime::StepRunAction::Step, 1u);
                break;
            case Drive::StepsOfThreeThenRun:
                if (!Paused) control(Runtime::StepRunAction::Pause), Paused = true;
                else if (resting && !Stepped) control(Runtime::StepRunAction::Step, 3u), Stepped = true;
                else if (resting) control(Runtime::StepRunAction::Run);
                break;
            case Drive::RunWithPauses:
                if (run.Phase == Runtime::StepRunPhase::Running && ++Toggle % 2 == 0) control(Runtime::StepRunAction::Pause);
                else if (resting) control(Runtime::StepRunAction::Run);
                break;
            case Drive::StepOnceAccept: // a pause can come before the first page: then one step
                if (!Paused) control(Runtime::StepRunAction::Pause), Paused = true;
                else if (resting && run.Steps == 0u) control(Runtime::StepRunAction::Step, 1u);
                else if (resting && gpu.CanAccept) accept();
                break;
            }
        }

        void Shutdown() override {}

    private:
        Runtime::ClusteringService* Service{};
        Runtime::StepRunsService* Steps{};
        Runtime::KernelEventSubscription Subscription{};
        Runtime::CommandCorrelationId Correlation{};
        std::optional<Runtime::KMeansRunCompleted> Completion{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Outcome Current{};
        bool Submitted{}, Paused{}, Stepped{}, Accepted{};
        std::uint32_t Toggle{};
        std::chrono::steady_clock::time_point Started{std::chrono::steady_clock::now()};
    };

    struct StepDriveResults
    {
        std::vector<ClusteringStepDrivesApp::Outcome> Outcomes{};
        bool TimedOut{};
    };

    // Runs `app` in a Vulkan engine (which owns it); nullopt (skipped) without a device.
    std::optional<StepDriveResults> RunStepDrives(std::unique_ptr<ClusteringStepDrivesApp> app)
    {
        auto* appPtr = app.get();
        Intrinsic::Tests::RuntimeTestKernel engine(KMeansSmokeConfig("Intrinsic K-Means step drives gpu;vulkan smoke"),
                                                   std::move(app));
        EmplaceKMeansModules(engine);
        engine.Initialize();
        if (!VulkanReady(engine)) return std::nullopt;
        engine.Run();
        StepDriveResults results{.Outcomes = appPtr->Outcomes, .TimedOut = appPtr->TimedOut};
        engine.Shutdown();
        return results;
    }
}

TEST(ClusteringStepRunGpuSmoke, EveryStepSequenceEqualsTheUninterruptedRun)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW could not initialize.";
    using Drive = ClusteringStepDrivesApp::Drive;
    auto app = std::make_unique<ClusteringStepDrivesApp>();
    app->Drives = {Drive::Run, Drive::StepsOfOne, Drive::StepsOfThreeThenRun, Drive::RunWithPauses};
    std::uint32_t random = 29u;
    for (unsigned i = 0; i < 600u; ++i)
    {
        glm::vec3 point{float(i % 6) * 3.0f, float(i % 4) * 2.0f, 0.0f};
        for (int axis = 0; axis < 3; ++axis)
        {
            random = random * 1664525u + 1013904223u;
            point[axis] += 2.0f * float(random >> 8) / float(1u << 24);
        }
        app->Points.push_back(point);
    }
    app->Parameters = {.ClusterCount = 7u, .MaxIterations = 40u, .Seed = 5u, .GpuPreviewInterval = 3u,
        .Initialization = Runtime::KMeansInitialization::Random};
    const auto results = RunStepDrives(std::move(app));
    if (!results) GTEST_SKIP() << "Promoted Vulkan did not reach readiness.";
    const auto* appPtr = &*results;
    ASSERT_FALSE(appPtr->TimedOut);
    ASSERT_EQ(appPtr->Outcomes.size(), 4u);
    const auto& uninterrupted = appPtr->Outcomes[0];
    ASSERT_TRUE(uninterrupted.Completion.Succeeded()) << uninterrupted.Completion.Message;
    EXPECT_EQ(uninterrupted.Completion.ActualBackend, Runtime::ClusteringBackend::VulkanCompute);
    EXPECT_GT(uninterrupted.Completion.Iterations, 4u) << "enough iterations for the drives to differ";
    for (std::size_t i = 1; i < appPtr->Outcomes.size(); ++i)
    {
        const auto& driven = appPtr->Outcomes[i];
        SCOPED_TRACE(i);
        ASSERT_TRUE(driven.Completion.Succeeded()) << driven.Completion.Message;
        EXPECT_FALSE(driven.RestSteps.empty()) << "the drive paused the run";
        EXPECT_EQ(driven.Completion.Iterations, uninterrupted.Completion.Iterations);
        EXPECT_EQ(driven.Labels, uninterrupted.Labels) << "labels exact";
        EXPECT_EQ(driven.Completion.Centroids, uninterrupted.Completion.Centroids) << "centroids bitwise";
        EXPECT_EQ(driven.Completion.Inertia, uninterrupted.Completion.Inertia);
    }
    const auto& ones = appPtr->Outcomes[1].RestSteps;
    for (std::size_t i = 1; i < ones.size(); ++i) EXPECT_LE(ones[i] - ones[i - 1], 1u) << "steps of one";
}

TEST(ClusteringStepRunGpuSmoke, AcceptFromARestPublishesTheLabelsOfTheMovedCentroids)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW could not initialize.";
    auto app = std::make_unique<ClusteringStepDrivesApp>();
    app->Drives = {ClusteringStepDrivesApp::Drive::StepOnceAccept};
    app->Points = {{0, 0, 0}, {1, 0, 0}, {6, 0, 0}, {7, 0, 0}, {10, 0, 0}};
    app->Parameters = {.ClusterCount = 2u, .MaxIterations = 10u, .Initialization = Runtime::KMeansInitialization::Hierarchical};
    const auto results = RunStepDrives(std::move(app));
    if (!results) GTEST_SKIP() << "Promoted Vulkan did not reach readiness.";
    const auto* appPtr = &*results;
    ASSERT_FALSE(appPtr->TimedOut);
    ASSERT_EQ(appPtr->Outcomes.size(), 1u);
    const auto& outcome = appPtr->Outcomes[0];
    ASSERT_TRUE(outcome.Completion.Succeeded()) << outcome.Completion.Message;
    // Hierarchical seeds {4.8, 10}; the first update gives {3.5, 10}, across which row 3 (x = 7) changes cluster.
    EXPECT_EQ(outcome.Completion.Iterations, 1u) << "Accept counts no iteration";
    EXPECT_EQ(outcome.Labels, (std::vector<std::uint32_t>{0, 0, 0, 1, 1}));
    ASSERT_EQ(outcome.Completion.Centroids.size(), 2u);
    EXPECT_EQ(outcome.Completion.Centroids[0], glm::vec3(3.5f, 0, 0));
    EXPECT_EQ(outcome.Completion.Centroids[1], glm::vec3(10, 0, 0));
}
