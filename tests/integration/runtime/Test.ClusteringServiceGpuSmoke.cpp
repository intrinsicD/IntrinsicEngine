#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
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
#include <functional>
#include <thread>
#include <utility>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.JobService;
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

#include "MinimizedGpuCompletions.hpp"

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
        .Seed = 13u,
        .Initialization = Runtime::KMeansInitialization::Hierarchical,
        .Execution = {.IterationCap = 6u,
            .PreviewEvery = 5u,
            .PreviewIntervalMs = 0u,
            .Start = Runtime::StepRunStart::Run},
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
        params.MaxIterations = parameters.Execution.IterationCap;
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

            // ARCH-023: the repeat run starts once the discarded run's buffers retired into the scratch cache.
            const auto* scratchCache = engine.Services().Find<Runtime::SpatialIndexCache>();
            const bool retired = !Repeated ||
                (scratchCache && scratchCache->Stats().ScratchActiveBytes == 0u &&
                    scratchCache->Stats().ScratchPendingBytes == 0u);
            if (!Submitted && Service != nullptr && retired && engine.GetDevice().IsOperational())
            {
                Correlation = Service->RunKMeans(Runtime::RunKMeans{
                    .StableEntityId = StableEntityId,
                    .Properties = Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::PointCloudPoint),
                    .Parameters =
                        [&]
                    {
                        auto parameters = Parameters;
                        parameters.Execution.AutoAccept =
                            Repeated ? Runtime::StepRunAutoAccept::End : Runtime::StepRunAutoAccept::Never;
                        return parameters;
                    }(),
                    .Backend = Runtime::ClusteringBackend::VulkanCompute,
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
                    if (typed)
                        TypedPreview = cache->QueueGpuCompute(typed->Bytes,
                            [typed](auto& cmd, const auto&)
                            {
                                cmd.BufferBarrier(typed->Buffer,
                                    Extrinsic::RHI::MemoryAccess::ShaderWrite,
                                    Extrinsic::RHI::MemoryAccess::TransferRead);
                                return typed->Buffer;
                            });
                    if (front)
                        Preview = cache->QueueGpuCompute(front->Bytes,
                            [front](auto& cmd, const auto&)
                            {
                                cmd.BufferBarrier(front->Buffer,
                                    Extrinsic::RHI::MemoryAccess::ShaderWrite,
                                    Extrinsic::RHI::MemoryAccess::TransferRead);
                                return front->Buffer;
                            });
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
        app->Parameters.Execution.IterationCap = 1;
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
    // The discarded first run created the workspace; the repeat run reuses its pipelines and, from the scratch
    // cache, its retired buffers.
    EXPECT_GT(appPtr->FirstBuffersCreated,0u);EXPECT_EQ(appPtr->FirstPipelinesCreated,3u);
    EXPECT_EQ(appPtr->Completion->GpuWorkspaceBuffersCreated,0u);
    EXPECT_EQ(appPtr->Completion->GpuWorkspacePipelinesCreated,0u);
    // Case 2's budgets fit a whole iteration (and a preview copy) per submission: one per iteration, the terminal
    // preview/centroid submission, and (ARCH-023: previews are their own submissions, not fused into an iteration's)
    // at most one per due preview that found a free slot. Case 3: its one iteration, the final assignment and the
    // terminal labels; its only step ends the run, so no live preview is copied.
    if (GetParam() == 2)
    {
        const auto iterations = appPtr->Completion->Iterations;
        EXPECT_GE(appPtr->Completion->GpuSubmissions, iterations + 1u);
        EXPECT_LE(appPtr->Completion->GpuSubmissions,
            iterations + 1u + iterations / appPtr->Parameters.Execution.PreviewEvery);
    }
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
    EXPECT_EQ(appPtr->Stats.CaptureReuses, 1u) << "the run compared above reused the discarded run's capture";
    EXPECT_EQ(appPtr->Stats.LabelsCommitted, 1u);
    EXPECT_EQ(appPtr->Stats.VisualizationRefreshReactions, 1u);
    // ARCH-022 Q83: the start's parts and the pages, as the K-Means trace reads them.
    EXPECT_EQ(appPtr->Stats.RequestsSubmitted, 2u);
    EXPECT_GT(appPtr->Stats.GpuSubmissions, 0u);
    EXPECT_GT(appPtr->Stats.GpuSnapshotMicros, 0u);
    EXPECT_GT(appPtr->Stats.GpuInputResolveMicros, 0u);
    EXPECT_GT(appPtr->Stats.GpuWorkspacePrepareMicros, 0u) << "the first run creates its buffers and pipelines";
    EXPECT_LE(appPtr->Stats.GpuSnapshotMicros + appPtr->Stats.GpuInputResolveMicros + appPtr->Stats.GpuSeedingMicros +
                  appPtr->Stats.GpuWorkspacePrepareMicros,
        appPtr->Stats.GpuCaptureInitMicros) << "the parts are nested in the capture and start";

    engine.Shutdown();
}

INSTANTIATE_TEST_SUITE_P(ResidentPages,ClusteringServiceGpuSmoke,::testing::Values(0,1,2,3));

namespace
{
    // Runs K-Means on one cloud once per drive, each driven through its step run, accepting it once it ended (or, for
    // StepOnceAccept, from its first rest; for RestartAtEnd, once its restarted generation ended), and keeps every
    // committed outcome. A drive runs on Vulkan unless Backends names its backend (ARCH-023 B8: the CPU reference is
    // a step run of the same controls).
    class ClusteringStepDrivesApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        // HeldWithoutFrames (ARCH-023): from a rest at step s >= 1, runs with the method queue held for three frames
        // (frames go on, no step completes), then holds graphics and blocks the main thread (no frame, drain or
        // Present) while the worker steps; then runs to its end.
        enum class Drive
        {
            Run,
            StepsOfOne,
            StepsOfThreeThenRun,
            RunWithPauses,
            StepOnceAccept,
            RestartAtEnd,
            HeldWithoutFrames,
            PreviewSteps,
            DiscardReady,
            StaleReady,
            ShutdownReady
        };
        struct Outcome
        {
            Runtime::KMeansRunCompleted Completion{};
            std::vector<std::uint32_t> Labels{};
            std::vector<std::uint32_t> RestSteps{}; // the step count of each frame the run was seen paused, before it ended
            std::vector<Runtime::StepRunMetric> Metrics{}; // the run's per-step metrics (inertia, max shift)
            std::uint64_t PreviewCopies{}; // separate optional submissions, excluded from the numerical paging oracle
            std::vector<std::uint32_t> ShownSteps{};
            bool TerminatedAtReady{};
            bool Restarted{}; // RestartAtEnd: the restart was requested
            // HeldWithoutFrames: frames that ran while the method queue was held and the steps made meanwhile; the
            // steps the worker completed with graphics held and the main thread blocked; whether both gates held.
            std::uint32_t FramesWhileComputeHeld{}, StepsWhileComputeHeld{}, StepsWithoutFrames{};
            bool MethodHeld{}, Gated{};
        };
        std::vector<Drive> Drives{};
        std::vector<Runtime::ClusteringBackend> Backends{}; // per drive; Vulkan for the ones it does not name
        std::vector<glm::vec3> Points{};
        std::vector<glm::vec3> MovedPoints{}; // when set, written as the positions before the last drive
        std::vector<Runtime::KMeansPagingLimits> Limits{}; // when set, the paging budgets of each drive
        Runtime::KMeansParameters Parameters{};
        std::vector<Outcome> Outcomes{};
        bool TimedOut{}, Minimized{}, ShutdownAfterRun{};
        std::uint64_t ShutdownRun{};
        std::uint64_t CaptureReuses{};

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
            if (Minimized && !engine.GetWindow().IsMinimized())
                return;
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(60)) TimedOut = true;
            if (TimedOut || Outcomes.size() == Drives.size() || Service == nullptr || Steps == nullptr)
            {
                if (MethodRelease) // never leave the method queue held into shutdown's device wait
                    (void)std::exchange(MethodRelease, {})();
                if (Service != nullptr) CaptureReuses = Service->Stats().CaptureReuses;
                engine.RequestExit();
                return;
            }
            if (!engine.GetDevice().IsOperational()) return;
            if (!Submitted)
            {
                if (!MovedPoints.empty() && Outcomes.size() + 1u == Drives.size())
                    SetPositions(engine.Worlds().Get(engine.ActiveWorld())->Raw().get<GS::Vertices>(Entity), MovedPoints);
                if (Outcomes.size() < Limits.size())
                    Runtime::KMeansPagingForTesting = Limits[Outcomes.size()]; // copied when the run is admitted
                Cpu = Outcomes.size() < Backends.size() &&
                    Backends[Outcomes.size()] == Runtime::ClusteringBackend::CpuReference;
                HoldFrom = 0u;
                auto parameters = Parameters;
                if (Drives[Outcomes.size()] != Drive::Run && Drives[Outcomes.size()] != Drive::RestartAtEnd)
                    parameters.Execution.Start = Runtime::StepRunStart::Ready; // control the first boundary explicitly
                Correlation =
                    Service->RunKMeans({.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity),
                        .Properties = Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::PointCloudPoint),
                        .Parameters = parameters,
                        .Backend = Cpu ? Runtime::ClusteringBackend::CpuReference
                                       : Runtime::ClusteringBackend::VulkanCompute}); // waits for Accept
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
            const auto id = Service->StepRun(Correlation); // the CPU run's, or the Vulkan run's (gpu.StepRun)
            const auto observed = id != 0u ? Steps->Observe(id) : std::nullopt;
            if (!observed || Accepted) return;
            const auto& run = *observed;
            if (Minimized)
            {
                EXPECT_FALSE(engine.Worlds()
                        .Get(engine.ActiveWorld())
                        ->Raw()
                        .get<GS::Vertices>(Entity)
                        .Properties.Exists("p:kmeans_label"));
                if (run.Phase == Runtime::StepRunPhase::Ready)
                    EXPECT_EQ(run.Steps, 0u);
            }
            Current.PreviewCopies = run.PreviewCopies;
            if (run.ShownStep && (Current.ShownSteps.empty() || Current.ShownSteps.back() != *run.ShownStep))
                Current.ShownSteps.push_back(*run.ShownStep);
            if (run.Metrics)
                Current.Metrics.assign(run.Metrics->Records().begin(), run.Metrics->Records().end());
            const bool resting = run.Phase == Runtime::StepRunPhase::Paused && !run.SegmentInFlight;
            const auto control = [&](const Runtime::StepRunAction action, const std::uint32_t count = 1u)
            {
                std::string refusal;
                (void)Steps->Control(id, action, count, refusal);
            };
            const auto accept = [&]
            {
                if (Cpu)
                    control(Runtime::StepRunAction::Accept); // the run row's Accept
                else
                    (void)Service->GpuRun(Correlation, Runtime::KMeansGpuAction::Accept);
                Accepted = true;
            };
            if (run.Phase == Runtime::StepRunPhase::Ready && !run.SegmentInFlight)
            {
                const auto drive = Drives[Outcomes.size()];
                if (drive == Drive::DiscardReady || drive == Drive::StaleReady || drive == Drive::ShutdownReady)
                {
                    EXPECT_EQ(run.Steps, 0u);
                    Current.TerminatedAtReady = true;
                    Accepted = true; // wait only for the terminal completion (shutdown has none)
                    if (drive == Drive::DiscardReady)
                        control(Runtime::StepRunAction::Discard);
                    else if (drive == Drive::StaleReady)
                        engine.Worlds()
                            .Get(engine.ActiveWorld())
                            ->Raw()
                            .get<GS::Vertices>(Entity)
                            .Properties.Get<glm::vec3>("v:position")[0]
                            .x += 1.f;
                    else
                    {
                        ShutdownRun = id;
                        engine.RequestExit();
                    }
                    return;
                }
            }
            // Vulkan: the final labels are prepared once the ended run rests (its terminal readback landed).
            const bool labelsReady = Cpu || (gpu.ReadyToAccept && !gpu.Running);
            if (run.End != Runtime::StepRunEnd::None)
            {
                const bool restart = Drives[Outcomes.size()] == Drive::RestartAtEnd && run.Generation == 0u;
                if (resting && labelsReady && restart)
                    control(Runtime::StepRunAction::Restart), Current.Restarted = true;
                else if (resting && labelsReady && run.Generation == (Current.Restarted ? 1u : 0u))
                    accept();
                return;
            }
            if (run.Phase == Runtime::StepRunPhase::Ready && !Paused)
            {
                control(Runtime::StepRunAction::Run);
                control(Runtime::StepRunAction::Pause); // before the first worker segment can start
                Paused = true;
                return;
            }
            if (resting) Current.RestSteps.push_back(run.Steps);
            switch (Drives[Outcomes.size()])
            {
            case Drive::Run:
            case Drive::RestartAtEnd:
            case Drive::DiscardReady:
            case Drive::StaleReady:
            case Drive::ShutdownReady: break;
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
                else if (resting && (Cpu ? run.Steps == 1u : gpu.CanAccept))
                    accept();
                break;
            case Drive::HeldWithoutFrames: HoldWithoutFrames(engine, gpu.StepRun, run, resting, control); break;
            case Drive::PreviewSteps:
                if (resting && (run.Steps == 0u || run.ShownStep == run.Steps))
                    control(Runtime::StepRunAction::Step, 1u);
                break;
            }
        }

        void Shutdown() override
        {
            if (ShutdownRun == 0u)
                return;
            EXPECT_FALSE(Kernel()
                    .Worlds()
                    .Get(Kernel().ActiveWorld())
                    ->Raw()
                    .get<GS::Vertices>(Entity)
                    .Properties.Exists("p:kmeans_label"));
            EXPECT_EQ(Service->Stats().LabelsCommitted, 0u);
            const auto run = Steps->Observe(ShutdownRun);
            ASSERT_TRUE(run.has_value());
            EXPECT_EQ(run->Phase, Runtime::StepRunPhase::Discarded);
            EXPECT_FALSE(Kernel().Jobs().HasGpuQueueWork());
        }

    private:
        void HoldWithoutFrames(Extrinsic::Runtime::Engine& engine,
            const std::uint64_t id,
            const Runtime::StepRunObservation& run,
            const bool resting,
            const auto& control)
        {
            namespace Vk = Extrinsic::Backends::Vulkan;
            if (!Paused)
            {
                control(Runtime::StepRunAction::Pause, 1u), Paused = true;
                return;
            }
            if (HoldFrom == 0u)
            {
                if (resting && run.Steps == 0u)
                    control(Runtime::StepRunAction::Step, 1u);
                else if (resting)
                {
                    // The worker's next page queues behind the held method queue: no step can complete yet.
                    MethodRelease = Vk::HoldVulkanQueue(&engine.GetDevice(), Vk::VulkanGatedQueue::MethodCompute);
                    if (!MethodRelease)
                    {
                        control(Runtime::StepRunAction::Run, 1u);
                        HoldFrom = ~0u; // no separate method queue: ungated, Gated stays false
                        return;
                    }
                    HoldFrom = run.Steps;
                    Current.MethodHeld = true;
                    control(Runtime::StepRunAction::Run, 1u);
                }
                return;
            }
            if (HoldFrom == ~0u || Current.Gated)
                return; // ungated, or already held: run to the end
            if (!run.WorkerInFlight)
                return; // the segment is not submitted yet
            if (Current.FramesWhileComputeHeld < 3u)
            {
                ++Current.FramesWhileComputeHeld; // this frame ran with method compute held mid-step
                Current.StepsWhileComputeHeld = run.Steps - HoldFrom;
                return;
            }
            // No frame from here: graphics held too, the main thread waits, only the worker can advance the run.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            std::uint32_t steps = run.Steps;
            Current.Gated = Vk::WithVulkanQueueGate(&engine.GetDevice(),
                Vk::VulkanGatedQueue::Graphics,
                [&]
                {
                    if (MethodRelease)
                        (void)std::exchange(MethodRelease, {})();
                    while (std::chrono::steady_clock::now() < deadline)
                    {
                        const auto now = Steps->Observe(id);
                        steps = now ? now->Steps : steps;
                        if (!now || steps >= HoldFrom + 3u || now->End != Runtime::StepRunEnd::None)
                            break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                });
            if (MethodRelease)
                (void)std::exchange(MethodRelease, {})();
            Current.StepsWithoutFrames = steps - HoldFrom;
            HoldFrom = ~0u; // held once
        }

        Runtime::ClusteringService* Service{};
        Runtime::StepRunsService* Steps{};
        Runtime::KernelEventSubscription Subscription{};
        Runtime::CommandCorrelationId Correlation{};
        std::optional<Runtime::KMeansRunCompleted> Completion{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Outcome Current{};
        bool Submitted{}, Paused{}, Stepped{}, Accepted{}, Cpu{};
        std::uint32_t Toggle{};
        std::uint32_t HoldFrom{}; // HeldWithoutFrames: the step the hold began at (~0: no gate)
        std::function<bool()> MethodRelease{}; // the held method queue's release
        std::chrono::steady_clock::time_point Started{std::chrono::steady_clock::now()};
    };

    struct StepDriveResults
    {
        std::vector<ClusteringStepDrivesApp::Outcome> Outcomes{};
        bool TimedOut{};
        std::uint64_t CaptureReuses{};
        bool ShutdownReadyObserved{};
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
        if (appPtr->Minimized)
            Intrinsic::Tests::RunWithMinimizedGpuCompletions(
                engine,
                [&]
                {
                    appPtr->Frame(0.0, 0.0);
                },
                appPtr->ShutdownAfterRun);
        else
            engine.Run();
        StepDriveResults results{.Outcomes = appPtr->Outcomes,
            .TimedOut = appPtr->TimedOut,
            .CaptureReuses = appPtr->CaptureReuses,
            .ShutdownReadyObserved = appPtr->ShutdownRun != 0u};
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
    app->Parameters = {.ClusterCount = 7u,
        .Seed = 5u,
        .Initialization = Runtime::KMeansInitialization::Random,
        .Execution = {.IterationCap = 40u,
            .PreviewEvery = 3u,
            .PreviewIntervalMs = 0u,
            .AutoAccept = Runtime::StepRunAutoAccept::Never,
            .Start = Runtime::StepRunStart::Run}};
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

// ARCH-023 B8: the CPU reference and the Vulkan run are step runs of the same logical iteration (one assign and update):
// on one cloud they count the same steps and iterations and agree with Geometry.KMeans; each restarted after its end
// (Vulkan: its final labels already prepared) computes again and accepts the uninterrupted run's labels; steps of one on
// the CPU equal its run; preview off (preview_every 0) equals preview on.
TEST(ClusteringStepRunGpuSmoke, CpuAndVulkanStepRunsCountTheSameIterationsAndRestartAfterTheirEnd)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW could not initialize.";
    using Drive = ClusteringStepDrivesApp::Drive;
    using B = Runtime::ClusteringBackend;
    auto app = std::make_unique<ClusteringStepDrivesApp>();
    app->Drives = {Drive::Run, Drive::Run, Drive::RestartAtEnd, Drive::RestartAtEnd, Drive::StepsOfOne};
    app->Backends = {B::VulkanCompute, B::CpuReference, B::VulkanCompute, B::CpuReference, B::CpuReference};
    app->Points.assign(kPoints.begin(), kPoints.end()); // the parity cloud: CPU and Vulkan agree exactly on it
    app->Parameters = {.ClusterCount = kParameters.ClusterCount,
        .Seed = kParameters.Seed,
        .Initialization = kParameters.Initialization,
        .Execution = {.IterationCap = 30u,
            .PreviewEvery = 0u, // preview off: the Vulkan run below equals the preview-on case of the drives above
            .PreviewIntervalMs = 0u,
            .AutoAccept = Runtime::StepRunAutoAccept::Never,
            .Start = Runtime::StepRunStart::Run}};
    const auto points = app->Points;
    const auto parameters = app->Parameters;
    const auto results = RunStepDrives(std::move(app));
    if (!results)
        GTEST_SKIP() << "Promoted Vulkan did not reach readiness.";
    ASSERT_FALSE(results->TimedOut);
    ASSERT_EQ(results->Outcomes.size(), 5u);
    const auto reference = GK::Cluster(std::span<const glm::vec3>{points}, MakeCpuParameters(parameters));
    ASSERT_TRUE(reference.has_value());
    const auto& vulkan = results->Outcomes[0];
    const auto& cpu = results->Outcomes[1];
    ASSERT_TRUE(vulkan.Completion.Succeeded()) << vulkan.Completion.Message;
    ASSERT_TRUE(cpu.Completion.Succeeded()) << cpu.Completion.Message;
    EXPECT_EQ(vulkan.Completion.ActualBackend, B::VulkanCompute);
    EXPECT_EQ(cpu.Completion.ActualBackend, B::CpuReference);
    EXPECT_NE(cpu.Completion.StepRun, 0u) << "the CPU result names its run";
    EXPECT_EQ(cpu.Completion.Iterations, reference->Iterations);
    EXPECT_EQ(vulkan.Completion.Iterations, cpu.Completion.Iterations) << "the same logical iterations";
    EXPECT_EQ(vulkan.Metrics.size(), cpu.Metrics.size()) << "one step per Lloyd iteration on both";
    EXPECT_EQ(cpu.Labels, reference->Labels);
    EXPECT_EQ(vulkan.Labels, cpu.Labels);
    EXPECT_GE(cpu.Completion.Iterations, 2u) << "a step drive pauses between iterations";
    for (std::size_t i = 2; i < results->Outcomes.size(); ++i)
    {
        const auto& driven = results->Outcomes[i];
        const auto& base = i == 2u ? vulkan : cpu;
        SCOPED_TRACE(i);
        ASSERT_TRUE(driven.Completion.Succeeded()) << driven.Completion.Message;
        EXPECT_EQ(driven.Restarted, i < 4u) << "drives 2 and 3 restarted after their end";
        EXPECT_EQ(driven.Completion.Iterations, base.Completion.Iterations);
        EXPECT_EQ(driven.Labels, base.Labels);
        EXPECT_EQ(driven.Completion.Centroids, base.Completion.Centroids) << "centroids bitwise";
        EXPECT_EQ(driven.Completion.Inertia, base.Completion.Inertia);
    }
    EXPECT_FALSE(results->Outcomes[4].RestSteps.empty()) << "the CPU drive paused between steps";
}

TEST(ClusteringStepRunGpuSmoke, AcceptFromARestPublishesTheLabelsOfTheMovedCentroids)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW could not initialize.";
    auto app = std::make_unique<ClusteringStepDrivesApp>();
    app->Drives = {ClusteringStepDrivesApp::Drive::StepOnceAccept};
    app->Points = {{0, 0, 0}, {1, 0, 0}, {6, 0, 0}, {7, 0, 0}, {10, 0, 0}};
    app->Parameters = {.ClusterCount = 2u,
        .Initialization = Runtime::KMeansInitialization::Hierarchical,
        .Execution = {.IterationCap = 10u,
            .PreviewEvery = 5u,
            .PreviewIntervalMs = 0u,
            .AutoAccept = Runtime::StepRunAutoAccept::Never,
            .Start = Runtime::StepRunStart::Run}};
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

// ARCH-022 Q79 step 6: the second run reuses the first run's capture and equals it exactly; the third, after a
// position write, captures afresh and equals the CPU reference of the moved points.
TEST(ClusteringStepRunGpuSmoke, AReusedCaptureEqualsTheFreshRunAndAPositionWriteIsCapturedAgain)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW could not initialize.";
    auto app = std::make_unique<ClusteringStepDrivesApp>();
    app->Drives = {ClusteringStepDrivesApp::Drive::Run, ClusteringStepDrivesApp::Drive::Run,
        ClusteringStepDrivesApp::Drive::Run};
    std::uint32_t random = 41u;
    for (unsigned i = 0; i < 512u; ++i)
    {
        glm::vec3 point{float(i % 8) * 10.0f, 0.0f, 0.0f};
        for (int axis = 0; axis < 3; ++axis)
        {
            random = random * 1664525u + 1013904223u;
            point[axis] += float(random >> 8) / float(1u << 24);
        }
        app->Points.push_back(point);
    }
    app->MovedPoints = app->Points;
    for (unsigned i = 0; i < 96u; ++i) app->MovedPoints[i].x += 10.0f; // a sixth of the rows to the next cluster
    app->Parameters = {.ClusterCount = 8u,
        .Seed = 3u,
        .Initialization = Runtime::KMeansInitialization::Hierarchical,
        .Execution = {.IterationCap = 20u,
            .PreviewEvery = 5u,
            .PreviewIntervalMs = 0u,
            .AutoAccept = Runtime::StepRunAutoAccept::Never,
            .Start = Runtime::StepRunStart::Run}};
    const auto points = app->Points;
    const auto moved = app->MovedPoints;
    const auto parameters = app->Parameters;
    const auto results = RunStepDrives(std::move(app));
    if (!results) GTEST_SKIP() << "Promoted Vulkan did not reach readiness.";
    ASSERT_FALSE(results->TimedOut);
    ASSERT_EQ(results->Outcomes.size(), 3u);
    for (const auto& outcome : results->Outcomes)
    {
        ASSERT_TRUE(outcome.Completion.Succeeded()) << outcome.Completion.Message;
        EXPECT_EQ(outcome.Completion.ActualBackend, Runtime::ClusteringBackend::VulkanCompute);
    }
    EXPECT_EQ(results->CaptureReuses, 1u) << "the second run reused; the third captured the moved points";
    const auto& fresh = results->Outcomes[0];
    const auto& reused = results->Outcomes[1];
    EXPECT_EQ(reused.Labels, fresh.Labels) << "labels exact";
    EXPECT_EQ(reused.Completion.Centroids, fresh.Completion.Centroids) << "centroids bitwise";
    EXPECT_EQ(reused.Completion.Inertia, fresh.Completion.Inertia);
    const auto before = GK::Cluster(std::span<const glm::vec3>{points}, MakeCpuParameters(parameters));
    const auto after = GK::Cluster(std::span<const glm::vec3>{moved}, MakeCpuParameters(parameters));
    ASSERT_TRUE(before && after);
    EXPECT_EQ(fresh.Labels, before->Labels);
    ASSERT_NE(after->Labels, before->Labels) << "the write changes the result, so a stale capture would show";
    const auto& recaptured = results->Outcomes[2];
    EXPECT_EQ(CountLabelMismatches(recaptured.Labels, after->Labels), 0u);
    ASSERT_EQ(recaptured.Completion.Centroids.size(), after->Centroids.size());
    float delta = 0.0f;
    for (std::size_t i = 0; i < after->Centroids.size(); ++i)
        for (int axis = 0; axis < 3; ++axis)
            delta = std::max(delta, std::abs(recaptured.Completion.Centroids[i][axis] - after->Centroids[i][axis]));
    EXPECT_LE(delta, 1e-5f);
    EXPECT_EQ(recaptured.Completion.Iterations, after->Iterations);
}

namespace
{
    // FNV-1a over the bit patterns of a run's outputs: equal hashes mean equal bits.
    [[nodiscard]] std::uint64_t BitHash(std::span<const std::uint32_t> words)
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const std::uint32_t word : words)
            for (int byte = 0; byte < 4; ++byte)
                hash = (hash ^ ((word >> (8 * byte)) & 0xffu)) * 1099511628211ull;
        return hash;
    }

    struct OracleBits
    {
        std::uint64_t Centroids{}, Labels{}, StepMetrics{};
        std::uint32_t Inertia{}, MaxDistanceIndex{}, Iterations{};
        bool Converged{};
        std::uint64_t Submissions{};
        bool operator==(const OracleBits&) const = default;
    };

    [[nodiscard]] OracleBits BitsOf(const ClusteringStepDrivesApp::Outcome& outcome)
    {
        std::vector<std::uint32_t> centroids, metrics;
        for (const glm::vec3& c : outcome.Completion.Centroids)
            for (int axis = 0; axis < 3; ++axis) centroids.push_back(std::bit_cast<std::uint32_t>(c[axis]));
        for (const auto& metric : outcome.Metrics) // each step's inertia and max shift, as the floats they were
            for (int value = 0; value < 2; ++value)
                metrics.push_back(std::bit_cast<std::uint32_t>(float(metric.Values[value])));
        return {.Centroids = BitHash(centroids),
            .Labels = BitHash(outcome.Labels),
            .StepMetrics = BitHash(metrics),
            .Inertia = std::bit_cast<std::uint32_t>(outcome.Completion.Inertia),
            .MaxDistanceIndex = outcome.Completion.MaxDistanceIndex,
            .Iterations = outcome.Completion.Iterations,
            .Converged = outcome.Completion.Converged,
            .Submissions = outcome.Completion.GpuSubmissions - outcome.PreviewCopies};
    }

    [[nodiscard]] std::string Initializer(const OracleBits& b)
    {
        return std::format("{{.Centroids = {:#x}ull, .Labels = {:#x}ull, .StepMetrics = {:#x}ull, .Inertia = {:#x}u, "
            ".MaxDistanceIndex = {}u, .Iterations = {}u, .Converged = {}, .Submissions = {}u}}",
            b.Centroids, b.Labels, b.StepMetrics, b.Inertia, b.MaxDistanceIndex, b.Iterations, b.Converged,
            b.Submissions);
    }

    [[nodiscard]] std::vector<glm::vec3> Jittered(const unsigned count, std::uint32_t random,
                                                  const auto& center, const float jitter)
    {
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < count; ++i)
        {
            glm::vec3 point = center(i);
            for (int axis = 0; axis < 3; ++axis)
            {
                random = random * 1664525u + 1013904223u;
                point[axis] += jitter * float(random >> 8) / float(1u << 24);
            }
            points.push_back(point);
        }
        return points;
    }

    struct OracleCase
    {
        const char* Name{};
        std::vector<glm::vec3> Points{};
        Runtime::KMeansParameters Parameters{};
        std::vector<Runtime::KMeansPagingLimits> Limits{};
        std::vector<OracleBits> Expected{}; // one per entry of Limits, from the path before ARCH-022 Q79 step 7b
    };

    [[nodiscard]] std::vector<OracleCase> OracleCases()
    {
        constexpr Runtime::KMeansPagingLimits tiny{.PagePairs = 2u, .SubmissionPairs = 8u};
        constexpr Runtime::KMeansPagingLimits small{.PagePairs = 256u, .SubmissionPairs = 1u << 18u};
        constexpr Runtime::KMeansPagingLimits standard{};
        const auto hierarchical = [](std::uint32_t k, std::uint32_t iterations)
        {
            return Runtime::KMeansParameters{.ClusterCount = k,
                .Seed = 7u,
                .Initialization = Runtime::KMeansInitialization::Hierarchical,
                .Execution = {.IterationCap = iterations,
                    .PreviewEvery = 5u,
                    .PreviewIntervalMs = 0u,
                    .AutoAccept = Runtime::StepRunAutoAccept::End,
                    .Start = Runtime::StepRunStart::Run}};
        };
        std::vector<OracleCase> cases;
        // Equal farthest distances on every Reduce page: the earliest page's (and lowest index's) point wins.
        std::vector<glm::vec3> ties;
        for (unsigned i = 0; i < 24u; ++i)
            ties.push_back({float(i % 2 == 0 ? -1 : 1) + (i % 4 < 2 ? 0.0f : 50.0f), 0, 0});
        cases.push_back({"farthest_ties_across_pages", ties, hierarchical(2u, 6u), {tiny, small, standard}, {
            {.Centroids = 0x41d0053979edefa1ull, .Labels = 0xeac2ddc24676aea3ull, .StepMetrics = 0x16bf3c1e43c9b6bcull,
             .Inertia = 0x41c00000u, .MaxDistanceIndex = 0u, .Iterations = 2u, .Converged = true, .Submissions = 39u},
            {.Centroids = 0x41d0053979edefa1ull, .Labels = 0xeac2ddc24676aea3ull, .StepMetrics = 0x16bf3c1e43c9b6bcull,
             .Inertia = 0x41c00000u, .MaxDistanceIndex = 0u, .Iterations = 2u, .Converged = true, .Submissions = 3u},
            {.Centroids = 0x41d0053979edefa1ull, .Labels = 0xeac2ddc24676aea3ull, .StepMetrics = 0x16bf3c1e43c9b6bcull,
             .Inertia = 0x41c00000u, .MaxDistanceIndex = 0u, .Iterations = 2u, .Converged = true, .Submissions = 3u}
        }});
        // Two locations for four clusters: empty clusters take the farthest point.
        std::vector<glm::vec3> empty;
        for (unsigned i = 0; i < 12u; ++i) empty.push_back({i % 2 == 0 ? 0.0f : 5.0f, 0, 0});
        cases.push_back({"empty_clusters", empty, hierarchical(4u, 6u), {tiny, small, standard}, {
            {.Centroids = 0x440bca625ef310a3ull, .Labels = 0xfaa8e4c44796f503ull, .StepMetrics = 0x3463390b9e47557bull,
             .Inertia = 0x0u, .MaxDistanceIndex = 0u, .Iterations = 2u, .Converged = true, .Submissions = 32u},
            {.Centroids = 0x440bca625ef310a3ull, .Labels = 0xfaa8e4c44796f503ull, .StepMetrics = 0x3463390b9e47557bull,
             .Inertia = 0x0u, .MaxDistanceIndex = 0u, .Iterations = 2u, .Converged = true, .Submissions = 3u},
            {.Centroids = 0x440bca625ef310a3ull, .Labels = 0xfaa8e4c44796f503ull, .StepMetrics = 0x3463390b9e47557bull,
             .Inertia = 0x0u, .MaxDistanceIndex = 0u, .Iterations = 2u, .Converged = true, .Submissions = 3u}
        }});
        // Large and small coordinates over several Reduce pages and Update chunks, the last one short.
        cases.push_back({"cancellation_sensitive_sums",
            Jittered(10000u, 11u,
                [](unsigned i) { return i % 2 == 0 ? glm::vec3{1.0e7f, 0, 0} : glm::vec3{1.0e-3f, 0, 0}; }, 0.5f),
            hierarchical(3u, 8u), {small, standard}, {
            {.Centroids = 0x579a122440ea7bb5ull, .Labels = 0x42982e62570029e1ull, .StepMetrics = 0x1597093b69d324aeull,
             .Inertia = 0x43e2dccdu, .MaxDistanceIndex = 389u, .Iterations = 8u, .Converged = false,
             .Submissions = 10u},
            {.Centroids = 0x579a122440ea7bb5ull, .Labels = 0x42982e62570029e1ull, .StepMetrics = 0x1597093b69d324aeull,
             .Inertia = 0x43e2dccdu, .MaxDistanceIndex = 389u, .Iterations = 8u, .Converged = false,
             .Submissions = 10u}
            }});
        // 65 clusters: the centroid block boundary.
        cases.push_back({"sixty_five_clusters",
            Jittered(300u, 13u, [](unsigned i) { return glm::vec3{float(i % 65) * 10.0f, 0, 0}; }, 1.0f),
            hierarchical(65u, 6u), {small, standard}, {
            {.Centroids = 0xac0ff55f65707622ull, .Labels = 0x311ca15b9b879b14ull, .StepMetrics = 0x4fee56042e517da2ull,
             .Inertia = 0x42799591u, .MaxDistanceIndex = 222u, .Iterations = 2u, .Converged = true, .Submissions = 3u},
            {.Centroids = 0xac0ff55f65707622ull, .Labels = 0x311ca15b9b879b14ull, .StepMetrics = 0x4fee56042e517da2ull,
             .Inertia = 0x42799591u, .MaxDistanceIndex = 222u, .Iterations = 2u, .Converged = true, .Submissions = 3u}
            }});
        // As many clusters as points (128): more centroids than one tile of 64.
        cases.push_back({"one_point_per_cluster",
            Jittered(128u, 17u, [](unsigned i) { return glm::vec3{float(i) * 3.0f, 0, 0}; }, 1.0f),
            hierarchical(128u, 4u), {standard}, {
            {.Centroids = 0x5958a5d81b652d44ull, .Labels = 0x75a715cc7ef3b753ull, .StepMetrics = 0x367aac658a69b173ull,
             .Inertia = 0x0u, .MaxDistanceIndex = 0u, .Iterations = 2u, .Converged = true, .Submissions = 3u}
            }});
        // One cluster of two points one ulp apart near 1e-20: the mean rounds to the even one, so a seed at the odd
        // one moves the centroid by an ulp whose square underflows to 0 (Moved without shift); the run then records
        // a final assignment, which a lost movement flag would skip.
        const float a = 1.0e-20f;
        const std::vector<glm::vec3> ulp{{a, 0, 0}, {std::nextafter(a, 1.0f), 0, 0}};
        // Seed 3 starts at the odd point (moved: a final assignment, three submissions), seed 1 at the even one.
        for (const auto& [seed, expected] : {std::pair{3u, OracleBits
            {.Centroids = 0xf30faae3c178b53aull, .Labels = 0x47fe0d7eaf8e51e3ull, .StepMetrics = 0x47fe0d7eaf8e51e3ull,
             .Inertia = 0x0u, .MaxDistanceIndex = 0u, .Iterations = 1u, .Converged = true, .Submissions = 3u}
        }, std::pair{1u, OracleBits
            {.Centroids = 0xf30faae3c178b53aull, .Labels = 0x47fe0d7eaf8e51e3ull, .StepMetrics = 0x47fe0d7eaf8e51e3ull,
             .Inertia = 0x0u, .MaxDistanceIndex = 0u, .Iterations = 1u, .Converged = true, .Submissions = 2u}
        }})
            cases.push_back({"moved_without_shift",
                ulp,
                {.ClusterCount = 1u,
                    .Seed = seed,
                    .Initialization = Runtime::KMeansInitialization::Random,
                    .Execution = {.IterationCap = 1u,
                        .PreviewEvery = 5u,
                        .PreviewIntervalMs = 0u,
                        .AutoAccept = Runtime::StepRunAutoAccept::End,
                        .Start = Runtime::StepRunStart::Run}},
                {standard},
                {expected}});
        return cases;
    }
}

// ARCH-022 Q79 step 7b: the batched Reduce and Update keep every page partial and its combine order, so a run's
// centroids, labels, inertia, farthest point, per-step inertia and max shift, iterations and convergence are the bits
// of the path before the change (stored below), for every paging budget.
TEST(ClusteringStepRunGpuSmoke, ReductionsKeepTheirBitsForEveryPagingBudget)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW could not initialize.";
    struct RestorePaging { Runtime::KMeansPagingLimits Before{Runtime::KMeansPagingForTesting};
        ~RestorePaging(){Runtime::KMeansPagingForTesting=Before;} } restore;
    for (const OracleCase& oracle : OracleCases())
    {
        SCOPED_TRACE(oracle.Name);
        auto app = std::make_unique<ClusteringStepDrivesApp>();
        app->Drives.assign(oracle.Limits.size(), ClusteringStepDrivesApp::Drive::Run);
        app->Limits = oracle.Limits;
        app->Points = oracle.Points;
        app->Parameters = oracle.Parameters;
        const auto results = RunStepDrives(std::move(app));
        if (!results) GTEST_SKIP() << "Promoted Vulkan did not reach readiness.";
        ASSERT_FALSE(results->TimedOut);
        ASSERT_EQ(results->Outcomes.size(), oracle.Limits.size());
        for (std::size_t i = 0; i < results->Outcomes.size(); ++i)
        {
            const auto& outcome = results->Outcomes[i];
            ASSERT_TRUE(outcome.Completion.Succeeded()) << outcome.Completion.Message;
            EXPECT_EQ(outcome.Completion.ActualBackend, Runtime::ClusteringBackend::VulkanCompute);
            const OracleBits actual = BitsOf(outcome);
            if (i >= oracle.Expected.size() || actual != oracle.Expected[i])
                ADD_FAILURE() << oracle.Name << " budget " << i << ": actual " << Initializer(actual);
        }
    }
}

// ARCH-023 Q5/Q7: GPU K-Means progresses without frames. With the method queue held mid-step, three render frames run
// and no step completes; then, with graphics held and the main thread blocked (no RunFrame, drain, CollectCompleted
// or Present), the worker completes at least three more iterations. Accepted at its end, the run equals the
// uninterrupted one bitwise. Mutation: the former owner-driven form advances one page per drained frame: zero steps
// without frames.
TEST(ClusteringStepRunGpuSmoke, IterationsCompleteWithoutFramesAndTheAcceptedRunEqualsTheUninterruptedOne)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW could not initialize.";
    using Drive = ClusteringStepDrivesApp::Drive;
    auto app = std::make_unique<ClusteringStepDrivesApp>();
    app->Drives = {Drive::Run, Drive::HeldWithoutFrames};
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
    app->Parameters = {.ClusterCount = 7u,
        .Seed = 5u,
        .Initialization = Runtime::KMeansInitialization::Random,
        .Execution = {.IterationCap = 40u,
            .PreviewEvery = 3u,
            .PreviewIntervalMs = 0u,
            .AutoAccept = Runtime::StepRunAutoAccept::Never,
            .Start = Runtime::StepRunStart::Run}};
    const auto results = RunStepDrives(std::move(app));
    if (!results)
        GTEST_SKIP() << "Promoted Vulkan did not reach readiness.";
    ASSERT_FALSE(results->TimedOut);
    ASSERT_EQ(results->Outcomes.size(), 2u);
    const auto& uninterrupted = results->Outcomes[0];
    const auto& held = results->Outcomes[1];
    ASSERT_TRUE(uninterrupted.Completion.Succeeded()) << uninterrupted.Completion.Message;
    ASSERT_GT(uninterrupted.Completion.Iterations, 4u) << "room for three steps after the first";
    ASSERT_TRUE(held.Completion.Succeeded()) << held.Completion.Message;
    if (!held.MethodHeld)
        GTEST_SKIP() << "No separate method queue: render-independent progress needs one.";
    EXPECT_TRUE(held.Gated) << "graphics was held while the main thread waited";
    EXPECT_EQ(held.FramesWhileComputeHeld, 3u) << "frames ran while method compute was held";
    EXPECT_EQ(held.StepsWhileComputeHeld, 0u) << "and no step completed then";
    EXPECT_GE(held.StepsWithoutFrames, 3u) << "iterations completed with no frame, drain or present";
    EXPECT_EQ(held.Completion.Iterations, uninterrupted.Completion.Iterations);
    EXPECT_EQ(held.Labels, uninterrupted.Labels) << "labels exact";
    EXPECT_EQ(held.Completion.Centroids, uninterrupted.Completion.Centroids) << "centroids bitwise";
    EXPECT_EQ(held.Completion.Inertia, uninterrupted.Completion.Inertia);
}

namespace
{
    std::unique_ptr<ClusteringStepDrivesApp> MinimizedKMeansApp(const bool minimized, const std::uint32_t buffers)
    {
        using Drive = ClusteringStepDrivesApp::Drive;
        auto app = std::make_unique<ClusteringStepDrivesApp>();
        app->Minimized = minimized;
        app->Drives = {minimized ? Drive::PreviewSteps : Drive::Run};
        app->Points = Jittered(
            600u,
            29u,
            [](const unsigned i)
            {
                return glm::vec3{float(i % 6) * 3.f, float(i % 4) * 2.f, 0.f};
            },
            2.f);
        app->Parameters = {.ClusterCount = 7u,
            .Seed = 5u,
            .Initialization = Runtime::KMeansInitialization::Random,
            .Execution = {.IterationCap = 5u,
                .PreviewEvery = minimized ? 1u : 0u,
                .PreviewIntervalMs = 0u,
                .AutoAccept = Runtime::StepRunAutoAccept::Never,
                .Start = Runtime::StepRunStart::Run,
                .PreviewBuffers = buffers}};
        return app;
    }
}

// ARCH-024: simulated minimization; real device work and owner completion run through Engine::Run.
TEST(ClusteringStepRunGpuSmoke, MinimizedInitPreviewAndAcceptNeedNoIdleHook)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    const auto reference = RunStepDrives(MinimizedKMeansApp(false, 3u));
    if (!reference)
        GTEST_SKIP() << "Vulkan bootstrap unavailable";
    ASSERT_FALSE(reference->TimedOut);
    ASSERT_EQ(reference->Outcomes.size(), 1u);
    const auto& expected = reference->Outcomes.front();
    ASSERT_TRUE(expected.Completion.Succeeded()) << expected.Completion.Message;
    ASSERT_EQ(expected.Completion.Iterations, 5u);
    for (const std::uint32_t buffers : {2u, 3u})
    {
        SCOPED_TRACE(buffers);
        const auto result = RunStepDrives(MinimizedKMeansApp(true, buffers));
        ASSERT_TRUE(result.has_value());
        ASSERT_FALSE(result->TimedOut);
        ASSERT_EQ(result->Outcomes.size(), 1u);
        const auto& actual = result->Outcomes.front();
        ASSERT_TRUE(actual.Completion.Succeeded()) << actual.Completion.Message;
        EXPECT_GE(actual.PreviewCopies, 4u);
        for (const std::uint32_t step : {1u, 2u, 3u, 4u})
            EXPECT_NE(std::ranges::find(actual.ShownSteps, step), actual.ShownSteps.end());
        EXPECT_EQ(actual.Labels, expected.Labels);
        EXPECT_EQ(actual.Completion.Iterations, expected.Completion.Iterations);
        EXPECT_EQ(actual.Completion.Centroids, expected.Completion.Centroids);
        EXPECT_EQ(actual.Completion.Inertia, expected.Completion.Inertia);
    }
}

TEST(ClusteringStepRunGpuSmoke, MinimizedDiscardAndStaleSourcePublishNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    using Drive = ClusteringStepDrivesApp::Drive;
    bool ran = false;
    for (const auto drive : {Drive::DiscardReady, Drive::StaleReady})
    {
        SCOPED_TRACE(static_cast<int>(drive));
        auto app = MinimizedKMeansApp(true, 2u);
        app->Drives = {drive};
        const auto result = RunStepDrives(std::move(app));
        if (!ran && !result)
            GTEST_SKIP() << "Vulkan bootstrap unavailable";
        ASSERT_TRUE(result.has_value());
        ran = true;
        ASSERT_FALSE(result->TimedOut);
        ASSERT_EQ(result->Outcomes.size(), 1u);
        const auto& outcome = result->Outcomes.front();
        EXPECT_TRUE(outcome.TerminatedAtReady);
        EXPECT_EQ(outcome.Completion.Status,
            drive == Drive::DiscardReady ? Runtime::KMeansRunStatus::Cancelled : Runtime::KMeansRunStatus::StaleSource);
        EXPECT_EQ(outcome.Completion.Iterations, 0u);
        EXPECT_TRUE(outcome.Labels.empty());
    }
}

TEST(ClusteringStepRunGpuSmoke, MinimizedShutdownDiscardsAnInitializedGpuRun)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    auto app = MinimizedKMeansApp(true, 2u);
    app->Drives = {ClusteringStepDrivesApp::Drive::ShutdownReady};
    app->ShutdownAfterRun = true;
    const auto result = RunStepDrives(std::move(app));
    if (!result)
        GTEST_SKIP() << "Vulkan bootstrap unavailable";
    ASSERT_FALSE(result->TimedOut);
    EXPECT_TRUE(result->ShutdownReadyObserved);
    EXPECT_TRUE(result->Outcomes.empty());
}
