// RUNTIME-274: standalone point sampling through the editor command.
#include <algorithm>
#include <cstring>
#include <optional>
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "SandboxEditorJobHarness.hpp"
#include "MockRHI.hpp"
import Extrinsic.Runtime.PointSamplingGpu;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Hierarchy;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace T = Extrinsic::ECS::Components::Transform;
using D = R::GeometryElementDomain;
namespace
{
    struct Scene
    {
        R::WorldRegistry Worlds;
        R::WorldHandle World{Worlds.CreateWorld("sampling")};
        Extrinsic::ECS::Scene::Registry& Registry{*Worlds.Get(World)};
        R::EditorCommandHistory History;
        R::EditorProcessingContext Context{.Scene = &Registry, .World = World, .CommandHistory = &History};
        [[nodiscard]] R::EditorProcessingCommands Commands() { return R::BindEditorProcessingCommands(Context); }
    };

    entt::entity MakeCloud(Scene& s, std::size_t count)
    {
        const auto entity = s.Registry.Create();
        s.Registry.Raw().emplace<T::Component>(entity);
        auto& vertices = s.Registry.Raw().emplace<GS::Vertices>(entity).Properties;
        vertices.Resize(count);
        std::mt19937 random(3u);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        auto positions = vertices.GetOrAdd<glm::vec3>("v:position");
        for (auto& p : positions.Vector()) p = {uniform(random), uniform(random), 0.5f * uniform(random)};
        return entity;
    }

    std::uint32_t Id(entt::entity entity) { return R::SelectionController::ToStableEntityId(entity); }
}

TEST(PointSamplingOperations, PublishesRankAndSelectionPropertiesUndoably)
{
    Scene s;
    const auto cloud = MakeCloud(s, 500);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 64};
    ASSERT_TRUE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled);
    const auto result = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.SampleCount, 64u);
    EXPECT_EQ(result.InputCount, 500u);
    EXPECT_GT(result.DistancePairs, 0u) << "farthest point by default";
    const auto& vertices = std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties);
    const auto rank = vertices.Get<float>("v:sample_rank");
    const auto selected = vertices.Get<bool>("v:sample_selected");
    ASSERT_TRUE(rank && selected);
    EXPECT_EQ(std::count(selected.Vector().begin(), selected.Vector().end(), true), 64);
    EXPECT_EQ(std::count_if(rank.Vector().begin(), rank.Vector().end(), [](float r) { return r >= 0.0f; }), 64);
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_FALSE(vertices.Exists("v:sample_rank"));
    EXPECT_FALSE(vertices.Exists("v:sample_selected"));
}

TEST(PointSamplingOperations, CreatesAPointCloudWithEveryMethod)
{
    Scene s;
    const auto cloud = MakeCloud(s, 400);
    for (const auto method : {R::PointSamplingMethod::Random, R::PointSamplingMethod::FarthestPoint,
                              R::PointSamplingMethod::ProgressivePoisson, R::PointSamplingMethod::CoupledSieve,
                              R::PointSamplingMethod::FlatGreedy, R::PointSamplingMethod::SampleElimination,
                              R::PointSamplingMethod::LazyGreedy, R::PointSamplingMethod::Tournament})
    {
        R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 50,
                                               .Output = R::PointSamplingOutput::PointCloud};
        config.Sampling.Method = method;
        const auto result = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
        ASSERT_TRUE(result.Succeeded()) << result.Message << " (" << result.Method << ")";
        EXPECT_GT(result.OutputEntityId, 0u);
        EXPECT_LE(result.SampleCount, 50u);
        EXPECT_GT(result.SampleCount, 0u);
        ASSERT_TRUE(s.History.Undo().Succeeded()) << result.Method;
    }
}

TEST(PointSamplingOperations, WeightsAndPriorityScoresComeFromAFloatProperty)
{
    Scene s;
    const auto cloud = MakeCloud(s, 300);
    auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
    auto weights = vertices.GetOrAdd<float>("v:importance");
    for (std::size_t i = 0; i < weights.Vector().size(); ++i) weights.Vector()[i] = 1.0f + float(i % 7);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 40, .WeightsName = "v:importance"};
    EXPECT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    config.Sampling.Method = R::PointSamplingMethod::ProgressivePoisson;
    config.Sampling.PoissonSelection = R::PointSamplingPoissonSelection::FeaturePriority;
    EXPECT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    config.WeightsName = "v:missing";
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled);
    config.WeightsName.clear();
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled) << "priority needs scores";
}

TEST(PointSamplingOperations, ConfigRoundTripsAndRejectsUnusableSettings)
{
    R::PointSamplingOperationConfig config{.Count = 0, .Output = R::PointSamplingOutput::PointCloud};
    config.Sampling.Method = R::PointSamplingMethod::FlatGreedy;
    config.Sampling.Beta = 1.4;
    const auto decoded = R::DecodePointSamplingOperationConfig(R::SerializePointSamplingOperationConfig(config));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Sampling.Method, R::PointSamplingMethod::FlatGreedy);
    EXPECT_EQ(decoded->Sampling.Beta, 1.4);
    EXPECT_EQ(decoded->Output, R::PointSamplingOutput::PointCloud);
    EXPECT_EQ(decoded->Count, 0u);
    const auto invalid = [](const R::PointSamplingOperationConfig& c) {
        return !R::ValidatePointSamplingOperationConfigSection(R::SerializePointSamplingOperationConfig(c), {}, "test").Usable();
    };
    EXPECT_TRUE(invalid({.RankName = "v:sample_selected"}));
    R::PointSamplingOperationConfig exact;
    exact.Sampling = {.Method = R::PointSamplingMethod::CoupledSieve, .Eta = 1.0, .CandidateCap = 3};
    EXPECT_TRUE(invalid(exact));
    Scene s;
    const auto cloud = MakeCloud(s, 20);
    const auto parent = s.Registry.Create();
    s.Registry.Raw().emplace<Extrinsic::ECS::Components::Hierarchy::Component>(cloud).Parent = parent;
    // Topology names are refused once the domain is resolved (here: point-cloud points).
    const auto plain = MakeCloud(s, 20);
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(plain),
                                                                     .RankName = "v:deleted"}).Enabled);
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(cloud)}).Enabled);
}

TEST(PointSamplingOperations, VulkanBackendWithoutADeviceRunsOnTheCpuAndSaysWhy)
{
    // RUNTIME-290: the requested backend, the one that ran and why, for every fallback path.
    Scene s;
    const auto cloud = MakeCloud(s, 300);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 40,
                                           .Backend = R::PointSamplingBackend::Vulkan};
    const auto decoded = R::DecodePointSamplingOperationConfig(R::SerializePointSamplingOperationConfig(config));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Backend, R::PointSamplingBackend::Vulkan);

    const auto cpu = R::ApplyEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(cloud), .Count = 40});
    ASSERT_TRUE(cpu.Succeeded()) << cpu.Message;
    EXPECT_EQ(cpu.RequestedBackend, "cpu_reference");
    EXPECT_TRUE(cpu.BackendDiagnostic.empty());
    const auto rank = [&] { return std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties).Get<float>("v:sample_rank").Vector(); };
    const auto cpuRank = rank();

    int delivered = 0;
    const auto noLane = R::ApplyEditorPointSamplingCommand(s.Commands(), config, [&](R::EditorPointSamplingResult) { ++delivered; });
    ASSERT_TRUE(noLane.Succeeded()) << noLane.Message;
    EXPECT_EQ(delivered, 1) << "immediate results reach the sink too";
    EXPECT_EQ(noLane.RequestedBackend, "gpu_vulkan_compute");
    EXPECT_EQ(noLane.Backend, "cpu_reference");
    EXPECT_NE(noLane.BackendDiagnostic.find("job lane"), std::string::npos) << noLane.BackendDiagnostic;
    EXPECT_EQ(rank(), cpuRank) << "the fallback is the CPU order";

    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
    const auto noDevice = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
    EXPECT_EQ(noDevice.Backend, "cpu_reference");
    EXPECT_NE(noDevice.BackendDiagnostic.find("spatial compute service"), std::string::npos) << noDevice.BackendDiagnostic;

    namespace PS = Geometry::PointSampling;
    EXPECT_NE(R::PointSamplingGpuUnsupportedReason({.Method = PS::Method::Random}, 300u, 40u, nullptr).find("no Vulkan kernel"),
              std::string::npos);
    EXPECT_NE(R::PointSamplingGpuUnsupportedReason({}, 300u, 40u, nullptr).find("No operational Vulkan device"),
              std::string::npos);
}

namespace
{
    class ResidentPointSampling : public ::testing::Test
    {
    protected:
        Scene S;
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::SpatialIndexCache Cache;
        R::CommandBus Commands;
        R::KernelEventBus Events;
        R::ServiceRegistry Services;
        std::optional<R::JobDesc> Queued;
        entt::entity Entity{};
        R::PointSamplingOperationConfig Config;
        unsigned Calls{};
        R::EditorPointSamplingResult Last;
        void SetUp() override
        {
            Entity = MakeCloud(S, 12);
            Config = {.SourceStableEntityId = Id(Entity), .Count = 4, .Backend = R::PointSamplingBackend::Vulkan};
            Device.ShaderFloat64 = true;
            Device.TransferQueue.AcceptBufferUploads = true;
            S.Context.Device = &Device;
            S.Context.SpatialIndices = &Cache;
            S.Context.JobCommands.Submit = [this](R::JobDesc desc, R::EditorJobIdentity) {
                Queued = std::move(desc); return R::JobToken{1, 1};
            };
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Commands, Events, Jobs.Jobs(), S.Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup).has_value());
        }
        void TearDown() override
        {
            Queued.reset();
            Jobs.Jobs().CancelAndDrain();
            R::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), S.Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        auto Start()
        {
            return R::ApplyEditorPointSamplingCommand(S.Commands(), Config,
                [this](auto result) { ++Calls; Last = std::move(result); });
        }
        auto& Rows() { return S.Registry.Raw().get<GS::Vertices>(Entity).Properties; }
    };
}

TEST_F(ResidentPointSampling, SecondAdmissionReusesCanonicalInputAndDiscardKeepsCpuUnchanged)
{
    auto first = Start();
    ASSERT_EQ(first.Status, R::EditorCommandStatus::Pending) << first.Message;
    EXPECT_EQ(first.GpuInputUploadBytes, 12u * 12u);
    ASSERT_TRUE(Queued);
    Queued->FinalizeUnpublishedOnMainThread();
    Queued->FinalizeUnpublishedOnMainThread();
    EXPECT_EQ(Calls, 1u);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    EXPECT_FALSE(Rows().Exists(Config.SelectedName));
    auto second = Start();
    ASSERT_EQ(second.Status, R::EditorCommandStatus::Pending) << second.Message;
    EXPECT_EQ(second.GpuInputUploadBytes, 0u);
    EXPECT_EQ(second.GpuInputCacheHits, 1u);
    EXPECT_EQ(second.CpuStageReadbackBytes, 0u);
    Queued->FinalizeUnpublishedOnMainThread();
    EXPECT_EQ(Calls, 2u);
}

TEST_F(ResidentPointSampling, InputOutputAndTransformEditsInvalidateQueuedPublication)
{
    for (unsigned edit = 0; edit < 4; ++edit)
    {
        ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
        ASSERT_EQ(Queued->ValidateBeforeApply(), R::JobApplyValidation::Current);
        if (edit == 0) Rows().Get<glm::vec3>("v:position")[0].x += 1;
        if (edit == 1) Rows().GetOrAdd<float>(Config.RankName)[0] = 2;
        if (edit == 2) Rows().GetOrAdd<bool>(Config.SelectedName)[0] = true;
        if (edit == 3) S.Registry.Raw().get<T::Component>(Entity).Position.x += 1;
        EXPECT_EQ(Queued->ValidateBeforeApply(), R::JobApplyValidation::StaleGeneration);
        Queued->FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity);
    }
    EXPECT_EQ(Calls, 4u);
}

TEST_F(ResidentPointSampling, RefusedUploadAndRejectedJobNotifyExactlyOnceWithoutCpuPublication)
{
    Device.TransferQueue.AcceptBufferUploads = false;
    const auto refused = Start();
    EXPECT_EQ(refused.Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(Calls, 1u);
    EXPECT_FALSE(Queued);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    Device.TransferQueue.AcceptBufferUploads = true;
    S.Context.JobCommands.Submit = [](R::JobDesc, R::EditorJobIdentity) { return R::JobToken{}; };
    EXPECT_EQ(Start().Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(Calls, 2u);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
}

// RUNTIME-313: the Vulkan run is a queued editor job like the others: a second run on the
// same output is refused before any upload or submission, and a cancelled run reports once
// with the shared wording.
TEST_F(ResidentPointSampling, DuplicateOutputIsRefusedAndCancelUsesTheSharedWording)
{
    S.Context.JobCommands.FindActive = [](const R::EditorJobIdentity& identity) {
        return std::optional{R::EditorJobRecord{.Token = R::JobToken{7, 2}, .Identity = identity,
                                                .State = R::JobState::AwaitingApply}};
    };
    const auto duplicate = Start();
    EXPECT_EQ(duplicate.Status, R::EditorCommandStatus::Pending);
    EXPECT_EQ(duplicate.Message, "Vulkan point sampling already has an active awaiting-apply job (job 7:2).");
    EXPECT_FALSE(Queued);
    EXPECT_EQ(duplicate.GpuInputUploadBytes, 0u) << "refused before acquiring device input";
    EXPECT_EQ(Calls, 0u);
    S.Context.JobCommands.FindActive = {};
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Queued);
    Queued->FinalizeUnpublishedOnMainThread();
    EXPECT_EQ(Calls, 1u);
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_EQ(Last.Message, "Vulkan point sampling was cancelled or its source became stale; nothing was applied.");
}

TEST_F(ResidentPointSampling, CompletionOnlyFrameWaitsPastFenceReuseWithoutReadback)
{
    auto result = Cache.QueueGpuCompute(0u, [](auto&, const auto&) { return Extrinsic::RHI::BufferHandle{1, 1}; });
    ASSERT_TRUE(result);
    Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
    EXPECT_EQ(result->State, R::SpatialQueryState::Submitted);
    Device.GlobalFrameNumber = Device.FramesInFlight;
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    EXPECT_EQ(result->State, R::SpatialQueryState::Submitted);
    ++Device.GlobalFrameNumber;
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    EXPECT_EQ(result->State, R::SpatialQueryState::Ready);
    EXPECT_TRUE(result->Data.empty());
}

TEST_F(ResidentPointSampling, WorkspaceReadsResidentPropertiesAndUploadsOnlyMetadata)
{
    namespace G = Extrinsic::Graphics;
    auto& residency = *Cache.PropertyResidency();
    const auto view = R::ResolveGpuPropertyInput(residency, S.Registry, S.World, Entity,
        {.Domain = D::PointCloudPoint, .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3});
    ASSERT_TRUE(view);
    const std::vector<std::uint32_t> rows{2, 4, 8};
    G::FarthestPointSamplingWorkspace workspace(Device);
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .Rows = rows, .Count = 2}));
    Device.BufferWrites.clear();
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    // The only host writes are the live-row map and the world matrix; values are device inputs.
    ASSERT_EQ(Device.BufferWrites.size(), 2u);
    EXPECT_EQ(workspace.Produced(), 2u);
    EXPECT_TRUE(workspace.Finished());
    auto wrong = *view;
    wrong.Layout.Channels = 4;
    EXPECT_FALSE(workspace.Begin({.Positions = wrong, .Count = 2}));
    const std::vector<std::uint32_t> invalidRows{12};
    EXPECT_FALSE(workspace.Begin({.Positions = *view, .Rows = invalidRows, .Count = 1}));
}

TEST_F(ResidentPointSampling, TerminalGpuOrderPublishesAtomicallyAndUndoRestoresBothFields)
{
    std::optional<Extrinsic::RHI::ReadbackSink> sink;
    Device.ComputeReadback = [&](auto record, std::uint64_t bytes, auto completion) {
        EXPECT_EQ(bytes, Config.Count * 12u);
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        sink = std::move(completion);
        return Extrinsic::RHI::ReadbackToken{1};
    };
    const auto& rows = std::as_const(Rows());
    const auto points = rows.Get<glm::vec3>("v:position");
    const auto reference = Geometry::PointSampling::Order(std::span<const glm::vec3>(points.Vector()), {}, Config.Count);
    ASSERT_TRUE(reference.Succeeded());
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    EXPECT_FALSE(Queued->IsReadyToApply());
    ASSERT_TRUE(sink);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    std::vector<std::byte> bytes(Config.Count * 12u);
    std::memcpy(bytes.data(), reference.Clearance.data(), Config.Count * sizeof(double));
    std::memcpy(bytes.data() + Config.Count * sizeof(double), reference.Order.data(), Config.Count * sizeof(std::uint32_t));
    sink->Deliver(bytes);
    ASSERT_TRUE(Queued->IsReadyToApply());
    ASSERT_EQ(Queued->ValidateBeforeApply(), R::JobApplyValidation::Current);
    ASSERT_TRUE(Queued->PublishCompletion(Events, R::JobResultEnvelope::Make(true)));
    EXPECT_EQ(Calls, 1u);
    EXPECT_EQ(Last.Backend, "gpu_vulkan_compute");
    for (std::size_t i = 0; i < reference.Order.size(); ++i)
    {
        EXPECT_EQ(rows.Get<float>(Config.RankName)[reference.Order[i]], float(i));
        EXPECT_TRUE(rows.Get<bool>(Config.SelectedName)[reference.Order[i]]);
    }
    ASSERT_TRUE(S.History.Undo().Succeeded());
    EXPECT_FALSE(rows.Exists(Config.RankName));
    EXPECT_FALSE(rows.Exists(Config.SelectedName));
    ASSERT_TRUE(S.History.Redo().Succeeded());
    EXPECT_TRUE(rows.Exists(Config.RankName));
    Queued->FinalizeUnpublishedOnMainThread();
    EXPECT_EQ(Calls, 1u);
}

TEST_F(ResidentPointSampling, RefusedRecordedSubmissionIsNotReplayedOrPublished)
{
    unsigned records = 0;
    Device.ComputeReadback = [&](auto record, std::uint64_t, auto) {
        ++records;
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        return Extrinsic::RHI::ReadbackToken{};
    };
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Queued->IsReadyToApply());
    Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
    EXPECT_EQ(records, 1u);
    EXPECT_FALSE(Queued->PublishCompletion(Events, R::JobResultEnvelope::Make(true)));
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    Queued->FinalizeUnpublishedOnMainThread();
    EXPECT_EQ(Calls, 1u);
}

TEST_F(ResidentPointSampling, WeightAndDeletionChangesRefusePublication)
{
    Config.WeightsName = "v:importance";
    (void)Rows().GetOrAdd<float>(Config.WeightsName, 1.0f);
    for (unsigned edit = 0; edit < 2; ++edit)
    {
        ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
        if (edit == 0) Rows().Get<float>(Config.WeightsName)[0] = 2.0f;
        else Rows().GetOrAdd<bool>("v:deleted", false)[0] = true;
        EXPECT_EQ(Queued->ValidateBeforeApply(), R::JobApplyValidation::StaleGeneration);
        Queued->FinalizeUnpublishedOnMainThread();
        EXPECT_FALSE(Rows().Exists(Config.RankName));
    }
    EXPECT_EQ(Calls, 2u);
}

TEST_F(ResidentPointSampling, WorkspacePagesRoundsWithinTheSubmissionBudget)
{
    namespace G = Extrinsic::Graphics;
    const auto count = G::FarthestPointSamplingWorkspace::MaxPoints;
    const auto buffer = Device.CreateBuffer({.SizeBytes = std::uint64_t(count) * 12u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage});
    const G::GpuPropertyView input{.Buffer = buffer, .Address = Device.GetBufferDeviceAddress(buffer),
        .Bytes = std::uint64_t(count) * 12u, .Layout = {.Channels = 3, .Count = count}};
    G::FarthestPointSamplingWorkspace workspace(Device);
    ASSERT_TRUE(workspace.Begin({.Positions = input, .Count = count}));
    EXPECT_FALSE(workspace.NextChunkFinishes());
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    const auto first = workspace.Produced();
    EXPECT_LE(std::uint64_t(first - 1u) * count, G::FarthestPointSamplingWorkspace::MaxPairsPerSubmission);
    EXPECT_FALSE(workspace.Finished());
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    EXPECT_LE(std::uint64_t(workspace.Produced() - first) * count, G::FarthestPointSamplingWorkspace::MaxPairsPerSubmission);
    EXPECT_GT(workspace.Produced(), first);
    for (const auto& dispatch : Device.CommandContext.DispatchRecords) EXPECT_LE(dispatch.X, 65535u);
}

TEST_F(ResidentPointSampling, WarmWorkspaceKeepsCapacityAndUnbindsPriorRowMap)
{
    namespace G = Extrinsic::Graphics;
    auto view = R::ResolveGpuPropertyInput(*Cache.PropertyResidency(), S.Registry, S.World, Entity,
        {.Domain = D::PointCloudPoint, .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3});
    ASSERT_TRUE(view);
    std::vector<std::uint32_t> rows(view->Layout.Count);
    for (std::uint32_t i = 0; i < rows.size(); ++i) rows[i] = std::uint32_t(rows.size()) - i - 1;
    G::FarthestPointSamplingWorkspace workspace(Device);
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .Rows = rows, .Count = 3}));
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    const auto buffers = Device.CreateBufferCount;
    const auto pipelines = Device.CreatePipelineCount;
    Device.BufferWrites.clear();
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .FirstIndex = 1, .Count = 2}));
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    EXPECT_EQ(Device.CreateBufferCount, buffers);
    EXPECT_EQ(Device.CreatePipelineCount, pipelines);
    EXPECT_EQ(workspace.Produced(), 2u);
    ASSERT_EQ(Device.BufferWrites.size(), 1u); // New transform, no stale row map upload.
    const auto& push = Device.CommandContext.PushConstantPayloads.back();
    ASSERT_EQ(push.size(), 112u);
    std::uint64_t rowAddress{};
    std::memcpy(&rowAddress, push.data() + 88, sizeof(rowAddress));
    EXPECT_EQ(rowAddress, 0u);
    // Growing only the result count preserves the other scratch buffers and pipeline.
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .Count = 4}));
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    EXPECT_EQ(Device.CreateBufferCount, buffers + 1);
    EXPECT_EQ(Device.CreatePipelineCount, pipelines);
}
