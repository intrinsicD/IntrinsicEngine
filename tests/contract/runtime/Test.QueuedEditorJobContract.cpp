// RUNTIME-313: the one setup/completion contract of queued editor jobs
// (`MeshSupport::ActiveOutputJobRefusal`, `ValidateQueuedJob`, `QueuedJobDelivery`),
// checked per operation through a scripted job lane: duplicate refusal before any
// submission, a rejected submission answered once without the callback, and an
// abandoned run that revalidates as Cancelled and delivers exactly once.
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "EditorFeatureTestContext.hpp"
#include "MockRHI.hpp"
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointSetOperations;
import Geometry.Properties;

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;

namespace
{
    class QueuedEditorJobContract : public ::testing::Test
    {
    protected:
        R::WorldRegistry Worlds;
        R::WorldHandle World{Worlds.CreateWorld("queued jobs")};
        Extrinsic::ECS::Scene::Registry& Scene{*Worlds.Get(World)};
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::SpatialIndexCache Cache{Worlds};
        R::CommandBus Bus;
        R::KernelEventBus Events;
        R::ServiceRegistry Services;
        entt::entity Entity{};
        std::uint32_t Id{};
        R::EditorProcessingContext Context{};
        std::vector<R::JobDesc> Queued;
        std::vector<R::EditorJobIdentity> Identities; // of every accepted submission, in order
        // Submissions accepted before the lane rejects (a later stage of a multi-stage run).
        std::size_t AcceptSubmissions{~std::size_t{0}};
        std::optional<R::EditorJobRecord> Active;

        void SetUp() override
        {
            Entity = Scene.Create();
            auto& props = Scene.Raw().emplace<GS::Vertices>(Entity).Properties;
            props.Resize(8);
            auto samples = props.GetOrAdd<glm::vec3>("samples");
            auto directions = props.GetOrAdd<glm::vec3>("directions");
            for (std::size_t i = 0; i < 8; ++i)
            {
                samples[i] = {float(i % 2), float((i / 2) % 2), float(i / 4)};
                directions[i] = {0, 0, 1};
            }
            Id = R::SelectionController::ToStableEntityId(Entity);
            Device.ShaderFloat64 = true;
            Device.TransferQueue.AcceptBufferUploads = true;
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Bus, Events, Jobs.Jobs(), Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup).has_value());
            Context.Scene = &Scene;
            Context.World = World;
            Context.Device = &Device;
            Context.SpatialIndices = &Cache;
            Context.JobCommands.Submit = [this](R::JobDesc desc, R::EditorJobIdentity identity) {
                if (Queued.size() >= AcceptSubmissions) return R::JobToken{};
                Queued.push_back(std::move(desc));
                Identities.push_back(std::move(identity));
                return R::JobToken{static_cast<std::uint32_t>(Queued.size()), 1u};
            };
            Context.JobCommands.FindActive = [this](const R::EditorJobIdentity& identity) {
                auto active = Active;
                if (active) active->Identity = identity;
                return active;
            };
        }
        void TearDown() override
        {
            Queued.clear();
            Jobs.Jobs().CancelAndDrain();
            R::RuntimeModuleShutdownContext shutdown{Bus, Events, Jobs.Jobs(), Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        [[nodiscard]] R::EditorProcessingCommands Commands() { return R::BindEditorProcessingCommands(Context); }
        [[nodiscard]] R::GeometryPropertyRef Ref(const char* name, Geometry::PropertyValueKind kind) const
        {
            return {D::PointCloudPoint, name, kind};
        }
        [[nodiscard]] R::GeometryPropertyRef Positions() const { return Ref("samples", Geometry::PropertyValueKind::Vec3); }
        [[nodiscard]] R::GeometryPropertyRef Normals() const { return Ref("directions", Geometry::PropertyValueKind::Vec3); }

        // Every queued point operation, by its shared label.
        template <class Check>
        void ForEachOperation(Check check)
        {
            const auto commands = Commands();
            check("Outlier estimation", [&](auto done) {
                return R::ApplyEditorOutlierAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Mask = Ref("outliers", Geometry::PropertyValueKind::UInt32),
                    .Score = Ref("scores", Geometry::PropertyValueKind::Float), .KNeighbors = 2}, done); });
            check("Normal estimation", [&](auto done) {
                return R::ApplyEditorNormalEstimationCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Output = Ref("normals", Geometry::PropertyValueKind::Vec3)}, done); });
            check("Density estimation", [&](auto done) {
                return R::ApplyEditorKernelDensityCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Density = Ref("density", Geometry::PropertyValueKind::Float), .KNeighbors = 2}, done); });
            check("Radii estimation", [&](auto done) {
                return R::ApplyEditorPointSpacingCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Radii = Ref("radii", Geometry::PropertyValueKind::Float)}, done); });
            check("Density weights", [&](auto done) {
                return R::ApplyEditorDensityWeightCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Weights = Ref("weights", Geometry::PropertyValueKind::Float)}, done); });
            check("Keypoint analysis", [&](auto done) {
                return R::ApplyEditorKeypointAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Mask = Ref("keypoints", Geometry::PropertyValueKind::UInt32),
                    .Score = Ref("saliency", Geometry::PropertyValueKind::Float), .MinimumNeighbors = 1}, done); });
            check("Descriptor analysis", [&](auto done) {
                return R::ApplyEditorDescriptorAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Normals = Normals(), .Outputs = R::MakeDescriptorOutputProperties(D::PointCloudPoint, "descriptor")}, done); });
            check("Bilateral filter", [&](auto done) {
                return R::ApplyEditorBilateralFilterCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Normals = Normals(), .Output = Ref("filtered", Geometry::PropertyValueKind::Vec3), .KNeighbors = 2}, done); });
            check("Point construction", [&](auto done) {
                return R::ApplyEditorPointConstructionCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Method = R::PointConstructionMethod::KnnGraph, .KNeighbors = 2}, done); });
        }
    };
}

TEST_F(QueuedEditorJobContract, DuplicateOutputIsRefusedBeforeSubmissionWithTheSharedMessage)
{
    Active = R::EditorJobRecord{.Token = R::JobToken{3u, 1u}, .State = R::JobState::Running};
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        unsigned calls{0u};
        const auto result = apply([&](auto) { ++calls; });
        EXPECT_EQ(result.Status, R::EditorCommandStatus::Pending);
        EXPECT_EQ(result.Message, label + " already has an active running job (job 3:1).");
        EXPECT_TRUE(Queued.empty());
        EXPECT_EQ(calls, 0u);
    });
}

TEST_F(QueuedEditorJobContract, RejectedSubmissionAnswersOnceWithoutTheCallback)
{
    AcceptSubmissions = 0u;
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        unsigned calls{0u};
        const auto result = apply([&](auto) { ++calls; });
        EXPECT_EQ(result.Status, R::EditorCommandStatus::GeometryProcessingFailed) << result.Message;
        EXPECT_EQ(result.Message.rfind(label + " job submission was rejected", 0), 0u) << result.Message;
        // The immediate answer is the report (panels publish it); a callback would report twice.
        EXPECT_EQ(calls, 0u);
    });
}

TEST_F(QueuedEditorJobContract, AbandonedRunRevalidatesAsCancelledAndDeliversOnce)
{
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        unsigned calls{0u};
        R::EditorCommandStatus status{};
        std::string message;
        const auto result = apply([&](auto delivered) {
            ++calls;
            status = delivered.Status;
            message = delivered.Message;
        });
        ASSERT_EQ(result.Status, R::EditorCommandStatus::Pending) << result.Message;
        ASSERT_FALSE(Queued.empty());
        auto& last = Queued.back();
        ASSERT_EQ(last.ValidateBeforeApply(), R::JobApplyValidation::Current);
        last.FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(calls, 1u);
        EXPECT_EQ(status, R::EditorCommandStatus::StaleEntity);
        EXPECT_EQ(message, label + " was cancelled or its source became stale; nothing was applied.");
        // An abandoned run never publishes: any stage revalidated afterwards is Cancelled.
        EXPECT_EQ(last.ValidateBeforeApply(), R::JobApplyValidation::Cancelled);
        last.FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(calls, 1u);
    });
}

// A multi-stage (Vulkan) run whose later stage the lane rejects: the rejection is the
// immediate answer only, and the stage already queued is abandoned (revalidates as
// Cancelled, its finalizer delivers nothing).
TEST_F(QueuedEditorJobContract, RejectedLaterStageAbandonsTheQueuedStagesWithoutACallback)
{
    const auto commands = Commands();
    const auto check = [&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        AcceptSubmissions = 1u;
        unsigned calls{0u};
        const auto result = apply([&](auto) { ++calls; });
        EXPECT_EQ(result.Status, R::EditorCommandStatus::GeometryProcessingFailed) << result.Message;
        EXPECT_EQ(result.Message.rfind(label + " job submission was rejected (", 0), 0u) << result.Message;
        ASSERT_EQ(Queued.size(), 1u) << result.Message;
        EXPECT_EQ(Queued.front().ValidateBeforeApply(), R::JobApplyValidation::Cancelled);
        Queued.front().FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(calls, 0u);
    };
    check("Keypoint analysis", [&](auto done) {
        return R::ApplyEditorKeypointAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Mask = Ref("keypoints", Geometry::PropertyValueKind::UInt32),
            .Score = Ref("saliency", Geometry::PropertyValueKind::Float), .MinimumNeighbors = 1,
            .Backend = R::KeypointAnalysisBackend::VulkanLBVH}, done); });
    check("Descriptor analysis", [&](auto done) {
        return R::ApplyEditorDescriptorAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Normals = Normals(), .Outputs = R::MakeDescriptorOutputProperties(D::PointCloudPoint, "descriptor"),
            .Backend = R::DescriptorAnalysisBackend::VulkanLBVH}, done); });
    check("Point construction", [&](auto done) {
        return R::ApplyEditorPointConstructionCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Method = R::PointConstructionMethod::KnnGraph, .KNeighbors = 2,
            .Backend = R::PointConstructionBackend::VulkanLBVH}, done); });
    check("Bilateral filter", [&](auto done) {
        return R::ApplyEditorBilateralFilterCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Normals = Normals(), .Output = Ref("filtered", Geometry::PropertyValueKind::Vec3), .KNeighbors = 2,
            .Iterations = 1, .Backend = R::BilateralFilterBackend::VulkanLBVH}, done); });
}

// Every later stage of a chain carries the run's first job token as EditorJobIdentity::Run, so a
// run's cancel reaches it and never another run on the same output.
TEST_F(QueuedEditorJobContract, LaterStagesOfAChainJoinTheFirstJobsRun)
{
    const auto commands = Commands();
    const auto check = [&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        Identities.clear();
        const auto result = apply([](auto) {});
        ASSERT_EQ(result.Status, R::EditorCommandStatus::Pending) << result.Message;
        ASSERT_GE(Identities.size(), 2u) << "a chain";
        EXPECT_FALSE(Identities.front().Run.IsValid()) << "the first job names its run by its own token";
        for (std::size_t i = 1; i < Identities.size(); ++i)
            EXPECT_EQ(Identities[i].Run, (R::JobToken{1u, 1u})) << "stage " << i;
        Queued.front().FinalizeUnpublishedOnMainThread();
    };
    check("Keypoint analysis", [&](auto done) {
        return R::ApplyEditorKeypointAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Mask = Ref("keypoints", Geometry::PropertyValueKind::UInt32),
            .Score = Ref("saliency", Geometry::PropertyValueKind::Float), .MinimumNeighbors = 1,
            .Backend = R::KeypointAnalysisBackend::VulkanLBVH}, done); });
    check("Descriptor analysis", [&](auto done) {
        return R::ApplyEditorDescriptorAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Normals = Normals(), .Outputs = R::MakeDescriptorOutputProperties(D::PointCloudPoint, "descriptor"),
            .Backend = R::DescriptorAnalysisBackend::VulkanLBVH}, done); });
    check("Point construction", [&](auto done) {
        return R::ApplyEditorPointConstructionCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Method = R::PointConstructionMethod::KnnGraph, .KNeighbors = 2,
            .Backend = R::PointConstructionBackend::VulkanLBVH}, done); });
    check("Bilateral filter", [&](auto done) {
        return R::ApplyEditorBilateralFilterCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
            .Normals = Normals(), .Output = Ref("filtered", Geometry::PropertyValueKind::Vec3), .KNeighbors = 2,
            .Iterations = 1, .Backend = R::BilateralFilterBackend::VulkanLBVH}, done); });
}

// Drift guard: a queued editor operation reuses the shared helper instead of hand-writing the
// prologue/epilogue this task consolidated. Scans the operation sources (like the layering
// tests): no hand-written "already active" refusal or duplicate wording, no direct active-job
// lookup or message builder outside its owner, no ad hoc deliver-once flag, and the guarded-sink +
// unpublished-finalize pattern only in the files listed below, each with the reason it does not
// use `QueuedJobDelivery`. A listed file that no longer matches fails too, so the list shrinks.
#ifndef INTRINSIC_SOURCE_DIR
#error "INTRINSIC_SOURCE_DIR must be defined for the queued-job drift guard"
#endif
TEST(QueuedEditorJobDriftGuard, OperationsUseTheSharedQueuedJobHelper)
{
    namespace fs = std::filesystem;
    const auto root = fs::path{INTRINSIC_SOURCE_DIR} / "src" / "runtime" / "Editor" / "Operations";
    ASSERT_TRUE(fs::exists(root)) << root;
    constexpr std::string_view owner = "Runtime.GeometryProcessingOperations.MeshSupport.cpp";
    // Files that own a job lifecycle the helper does not cover (keep this list short; a new
    // queued operation belongs on `QueuedJobDelivery`).
    constexpr std::array<std::string_view, 10> handWritten{
        // GPU Run/Accept transactions not yet on Runtime.GpuTransactionLifecycle (RUNTIME-311).
        // Keypoints stays listed for its resident run's guarded publication sink.
        "Runtime.GeometryProcessingOperations.GpuPositions.cpp",
        "Runtime.GeometryProcessingOperations.Keypoints.cpp",
        "Runtime.GeometryProcessingOperations.Normals.cpp",
        "Runtime.MeshFieldOperations.Smoothing.cpp",
        // Mesh-family jobs that keep their result in a typed state struct with the shared
        // `BuildUnpublishedEditorJobFailure` wording and `ActiveOutputJobRefusal`.
        "Runtime.GeometryProcessingOperations.cpp",
        "Runtime.GeometryProcessingOperations.Registration.cpp",
        "Runtime.GeometryProcessingOperations.Uv.cpp",
        "Runtime.MeshFieldOperations.Curvature.cpp",
        "Runtime.MeshTopologyOperations.Topology.cpp",
        // A run object with interactive steps (Busy flag, optional completion).
        "Runtime.RegistrationOperations.CoherentPointDrift.cpp",
    };
    constexpr std::string_view deliverOnceAllowed = "Runtime.RegistrationOperations.CoherentPointDrift.cpp";
    // RUNTIME-311: Accept front readbacks belong to the shared GPU transaction lifecycle; these
    // files still read fronts themselves (GpuPositions also defines the readback primitive).
    constexpr std::string_view lifecycle = "Runtime.GpuTransactionLifecycle.cpp";
    constexpr std::array<std::string_view, 3> ownFrontReadback{
        "Runtime.GeometryProcessingOperations.GpuPositions.cpp",
        "Runtime.GeometryProcessingOperations.Normals.cpp",
        "Runtime.MeshFieldOperations.Smoothing.cpp",
    };
    std::size_t frontMatched = 0;
    std::size_t scanned = 0, matched = 0;
    bool deliverOnceMatched = false;
    for (const auto& entry : fs::directory_iterator(root))
    {
        const auto name = entry.path().filename().string();
        if (entry.path().extension() != ".cpp" || name == owner) continue;
        std::ifstream file(entry.path());
        const std::string text{std::istreambuf_iterator<char>(file), {}};
        ++scanned;
        SCOPED_TRACE(name);
        EXPECT_EQ(text.find("already active"), std::string::npos)
            << "a duplicate refusal comes from MeshSupport::ActiveOutputJobRefusal";
        EXPECT_EQ(text.find("FindActiveEditorJob("), std::string::npos)
            << "look up an active output job through MeshSupport::ActiveOutputJobRefusal";
        EXPECT_EQ(text.find("BuildActiveDerivedJobMessage("), std::string::npos)
            << "the duplicate wording comes from MeshSupport::ActiveOutputJobRefusal";
        EXPECT_EQ(text.find("already has an active"), std::string::npos)
            << "the duplicate wording comes from MeshSupport::ActiveOutputJobRefusal";
        const bool deliverOnce = text.find("make_shared<bool>") != std::string::npos;
        if (name == deliverOnceAllowed)
            deliverOnceMatched = deliverOnce;
        else
            EXPECT_FALSE(deliverOnce) << "deliver-once state belongs to MeshSupport::QueuedJobDelivery";
        // A transaction on the shared lifecycle guards its typed sink; the lifecycle finalizes.
        const bool onLifecycle = text.find("GpuTransactionCore") != std::string::npos;
        const bool handWrites = !onLifecycle && text.find("GuardEditorProcessingResult(") != std::string::npos &&
                                text.find("FinalizeUnpublishedOnMainThread") != std::string::npos;
        if (name != lifecycle)
        {
            const bool readsFronts = text.find("BeginGpuFrontReadback(") != std::string::npos;
            const bool frontListed = std::find(ownFrontReadback.begin(), ownFrontReadback.end(), name) != ownFrontReadback.end();
            if (readsFronts)
                EXPECT_TRUE(frontListed) << "a GPU transaction's Accept readback belongs to Runtime.GpuTransactionLifecycle";
            else
                EXPECT_FALSE(frontListed) << "stale front-readback allowlist entry; remove it";
            if (frontListed) ++frontMatched;
        }
        const bool listed = std::find(handWritten.begin(), handWritten.end(), name) != handWritten.end();
        if (handWrites)
            EXPECT_TRUE(listed)
                << "a queued operation uses MeshSupport::QueuedJobDelivery instead of a hand-written guarded sink and finalizer";
        else
            EXPECT_FALSE(listed) << "stale allowlist entry: the file no longer hand-writes its job lifecycle; remove it";
        if (listed) ++matched;
    }
    EXPECT_GT(scanned, 20u);
    EXPECT_EQ(matched, handWritten.size()) << "an allowlisted file is missing";
    EXPECT_EQ(frontMatched, ownFrontReadback.size()) << "a front-readback allowlisted file is missing";
    EXPECT_TRUE(deliverOnceMatched) << "stale allowance: " << deliverOnceAllowed << " no longer keeps its own deliver-once flag";
}
