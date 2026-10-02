// RUNTIME-292: the Vulkan smoothing run as a GPU property transaction (ADR 0030 decisions 5-7)
// on a null/mock device: Accept publishes through the undoable transaction and binds the ring
// front as the canonical slot of the new revision; Discard, cancel, stale-while-pending and undo
// behave as the ADR says; "Applied" is reported only after the CPU publication.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.Properties;
namespace R = Extrinsic::Runtime;
namespace G = Extrinsic::Graphics;
using D = R::GeometryElementDomain;
using K = Geometry::PropertyValueKind;
namespace
{
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        entt::entity Entity;
        R::EditorProcessingContext Context;
        R::PropertySmoothingConfig Config;
        Extrinsic::Tests::MockDevice Device;
        G::GpuPropertyResidency Residency{Device};
        Extrinsic::Tests::EditorJobHarness Jobs;
        explicit Harness(K kind = K::Double)
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, D::PointCloudPoint);
            Config.Input = {D::PointCloudPoint, "temperature", kind};
            Config.Output = {D::PointCloudPoint, "smooth", kind};
            Config.Positions = {D::PointCloudPoint, "samples", K::Vec3};
            Config.Backend = R::PropertySmoothingBackend::Vulkan;
            auto& props = Props();
            auto samples = props.GetOrAdd<glm::vec3>("samples", {});
            for (std::size_t i = 0; i < props.Size(); ++i) samples[i] = {float(i), 0, 0};
            if (kind == K::Double)
            {
                auto values = props.GetOrAdd<double>("temperature", 0.);
                for (std::size_t i = 0; i < props.Size(); ++i) values[i] = double(i);
            }
            else
            {
                auto values = props.GetOrAdd<float>("temperature", 0.f);
                for (std::size_t i = 0; i < props.Size(); ++i) values[i] = float(i);
            }
            Device.TransferQueue.AcceptBufferUploads = true;
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
            Jobs.Attach(Context);
        }
        Geometry::PropertySet& Props() { return Intrinsic::Tests::PointDomainProperties(Scene, Entity, D::PointCloudPoint); }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }
        std::size_t Live() const { return LiveRows; }
        std::size_t LiveRows{};
        // A transaction waiting for Accept whose device result is `value` on every live row.
        R::EditorPropertySmoothingTransactionHandle Ready(const double value, const bool withResidency = true)
        {
            std::vector<double> front;
            const auto& props = Props();
            const auto deleted = std::as_const(props).Get<bool>("v:deleted");
            for (std::size_t i = 0; i < props.Size(); ++i)
                if (!deleted || !deleted[i]) front.push_back(value);
            LiveRows = front.size();
            return R::MakeEditorPropertySmoothingTransactionForTest(Commands(), Id(), Config, front, withResidency ? &Residency : nullptr);
        }
        G::GpuPropertyKey Key() const { return R::MakeGpuPropertyKey(Context.World, Entity, Config.Output); }
        std::optional<Geometry::PropertyRevision> OutputRevision()
        {
            const auto& props = Props();
            if (Config.Output.ValueKind == K::Double)
            {
                const auto p = std::as_const(props).Get<double>("smooth");
                return p ? std::optional{p.Revision()} : std::nullopt;
            }
            const auto p = std::as_const(props).Get<float>("smooth");
            return p ? std::optional{p.Revision()} : std::nullopt;
        }
    };
}

TEST(PropertySmoothingTransaction, AcceptPublishesUndoablyThenBindsTheFrontAsTheCanonicalRevision)
{
    Harness h;
    const auto run = h.Ready(7.0);
    ASSERT_TRUE(run);
    auto snapshot = R::SnapshotEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
    EXPECT_TRUE(snapshot.CanAccept) << snapshot.AcceptDisabledReason;
    EXPECT_EQ(snapshot.Result.Status, R::EditorCommandStatus::Pending);
    EXPECT_TRUE(h.Residency.HasRing(h.Key())) << "the seam publishes a front on the output ring";
    EXPECT_FALSE(h.Props().Exists("smooth")) << "nothing is published while the result waits";

    std::optional<R::EditorPropertySmoothingResult> delivered;
    const auto accepted = R::AcceptEditorPropertySmoothing(h.Commands(), run, [&](R::EditorPropertySmoothingResult r) { delivered = r; });
    ASSERT_EQ(accepted.Status, R::EditorCommandStatus::Pending) << accepted.Message;
    snapshot = R::SnapshotEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::Accepting);
    EXPECT_FALSE(snapshot.CanAccept);
    EXPECT_EQ(snapshot.AcceptDisabledReason, "Accept is already under way.") << "the lifecycle's own refusal, in every phase";
    EXPECT_EQ(snapshot.Result.Status, R::EditorCommandStatus::Pending) << "\"Applied\" only after the CPU publication";
    EXPECT_FALSE(delivered);

    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    snapshot = R::SnapshotEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::Applied);
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::Applied) << delivered->Message;
    EXPECT_EQ(delivered->BackendId, "vulkan_compute");
    const auto smooth = std::as_const(h.Props()).Get<double>("smooth");
    ASSERT_TRUE(smooth);
    const auto deleted = std::as_const(h.Props()).Get<bool>("v:deleted");
    for (std::size_t i = 0; i < h.Props().Size(); ++i)
        if (!deleted || !deleted[i]) EXPECT_DOUBLE_EQ(smooth[i], 7.0);

    // The front is now the canonical slot of the published revision: the ring is gone and the
    // next GPU use of this revision hits without uploading.
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    const auto front = h.Residency.Front(h.Key());
    ASSERT_TRUE(front);
    ASSERT_TRUE(h.OutputRevision());
    EXPECT_EQ(front->Revision, *h.OutputRevision());
    const auto before = h.Residency.Stats();
    const auto input = R::ResolveGpuPropertyInput(h.Residency, h.Scene, h.Context.World, h.Entity, h.Config.Output);
    ASSERT_TRUE(input);
    EXPECT_EQ(input->Buffer, front->Buffer);
    const auto after = h.Residency.Stats();
    EXPECT_EQ(after.Uploads, before.Uploads) << "a resident revision uploads nothing";
    EXPECT_EQ(after.UploadBytes, before.UploadBytes);
    EXPECT_EQ(after.Hits, before.Hits + 1u);

    // Undo changes the CPU revision (the property is removed again); the next use uploads once.
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(h.Props().Exists("smooth"));
    EXPECT_FALSE(R::ResolveGpuPropertyInput(h.Residency, h.Scene, h.Context.World, h.Entity, h.Config.Output))
        << "a removed property has no input";
    ASSERT_TRUE(h.History.Redo().Succeeded());
    ASSERT_TRUE(h.OutputRevision());
    EXPECT_NE(*h.OutputRevision(), front->Revision) << "redo republishes under a new revision";
    const auto uploadsBefore = h.Residency.Stats().Uploads;
    const auto reuploaded = R::ResolveGpuPropertyInput(h.Residency, h.Scene, h.Context.World, h.Entity, h.Config.Output);
    ASSERT_TRUE(reuploaded);
    EXPECT_EQ(h.Residency.Stats().Uploads, uploadsBefore + 1u) << "a new revision uploads exactly once";
    EXPECT_EQ(reuploaded->Revision, *h.OutputRevision());
}

TEST(PropertySmoothingTransaction, DiscardPublishesNothingAndReleasesTheRing)
{
    Harness h;
    const auto run = h.Ready(3.0);
    ASSERT_TRUE(run);
    ASSERT_TRUE(h.Residency.HasRing(h.Key()));
    R::DiscardEditorPropertySmoothing(h.Commands(), run);
    const auto snapshot = R::SnapshotEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(snapshot.CanAccept);
    EXPECT_FALSE(h.Residency.HasRing(h.Key())) << "observation returns to the canonical slot";
    EXPECT_FALSE(h.Residency.Front(h.Key())) << "no canonical slot was ever bound";
    EXPECT_FALSE(h.Props().Exists("smooth"));
    EXPECT_FALSE(h.History.Undo().Succeeded()) << "nothing entered the history";
    const auto refused = R::AcceptEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(refused.Status, R::EditorCommandStatus::InvalidProcessingParameters) << "a discarded result cannot be accepted";
}

TEST(PropertySmoothingTransaction, StaleWhilePendingDisablesAcceptUntilDiscard)
{
    Harness h;
    const auto run = h.Ready(5.0);
    ASSERT_TRUE(run);
    EXPECT_TRUE(R::SnapshotEditorPropertySmoothing(h.Commands(), run).CanAccept);
    // The input changes while the result waits.
    h.Props().Get<double>("temperature")[0] += 1.0;
    const auto stale = R::SnapshotEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(stale.Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
    EXPECT_TRUE(stale.Stale);
    EXPECT_FALSE(stale.CanAccept);
    EXPECT_NE(stale.AcceptDisabledReason.find("changed"), std::string::npos) << stale.AcceptDisabledReason;
    const auto refused = R::AcceptEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(refused.Status, R::EditorCommandStatus::StaleEntity) << refused.Message;
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Commands(), run).Phase, R::EditorGpuTransactionPhase::ReadyToAccept)
        << "a refused Accept leaves the result waiting";
    EXPECT_FALSE(h.Props().Exists("smooth"));
    R::DiscardEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Commands(), run).Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
}

TEST(PropertySmoothingTransaction, CancelWhileAcceptingPublishesNothing)
{
    Harness h;
    const auto run = h.Ready(9.0);
    ASSERT_TRUE(run);
    std::optional<R::EditorPropertySmoothingResult> delivered;
    ASSERT_EQ(R::AcceptEditorPropertySmoothing(h.Commands(), run, [&](R::EditorPropertySmoothingResult r) { delivered = r; }).Status,
              R::EditorCommandStatus::Pending);
    // Discard before the publication drain: the job finalizes unpublished.
    R::DiscardEditorPropertySmoothing(h.Commands(), run);
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Commands(), run).Phase, R::EditorGpuTransactionPhase::Discarded);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Commands(), run).Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(h.Props().Exists("smooth"));
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity) << delivered->Message;
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    EXPECT_FALSE(h.History.Undo().Succeeded());
}

TEST(PropertySmoothingTransaction, StaleAtPublicationIsRejectedAndReleasesTheRing)
{
    Harness h;
    const auto run = h.Ready(2.0);
    ASSERT_TRUE(run);
    std::optional<R::EditorPropertySmoothingResult> delivered;
    ASSERT_EQ(R::AcceptEditorPropertySmoothing(h.Commands(), run, [&](R::EditorPropertySmoothingResult r) { delivered = r; }).Status,
              R::EditorCommandStatus::Pending);
    // The input changes between the readback and the publication drain.
    h.Props().Get<double>("temperature")[1] += 1.0;
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity) << delivered->Message;
    EXPECT_NE(R::SnapshotEditorPropertySmoothing(h.Commands(), run).Phase, R::EditorGpuTransactionPhase::Applied);
    EXPECT_FALSE(h.Props().Exists("smooth"));
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    EXPECT_FALSE(h.Residency.Front(h.Key()));
}

TEST(PropertySmoothingTransaction, EditingTheOutputWhileWaitingIsStale)
{
    // The output is not a publication watch (an aliased output is guarded by its expected
    // values), but a waiting result must not be accepted over an edited output.
    Harness h;
    const auto run = h.Ready(4.0);
    ASSERT_TRUE(run);
    EXPECT_TRUE(R::SnapshotEditorPropertySmoothing(h.Commands(), run).CanAccept);
    h.Props().GetOrAdd<double>("smooth", 0.0)[0] = 1.0; // the output appears (or changes) under the result
    const auto stale = R::SnapshotEditorPropertySmoothing(h.Commands(), run);
    EXPECT_TRUE(stale.Stale);
    EXPECT_FALSE(stale.CanAccept);
    EXPECT_EQ(R::AcceptEditorPropertySmoothing(h.Commands(), run).Status, R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPropertySmoothing(h.Commands(), run);
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
}

namespace
{
    // A context that passes the Vulkan admission on the mock device: an operational device
    // with shader doubles, a registered SpatialIndexCache (its own residency and GPU
    // participant) and editor jobs, so Start reaches the transaction guards.
    struct AdmittedHarness
    {
        R::CommandBus Commands;
        R::KernelEventBus Events;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::WorldRegistry Worlds;
        R::ServiceRegistry Services;
        Extrinsic::Tests::MockDevice Device;
        R::SpatialIndexCache Cache;
        R::WorldHandle World;
        entt::entity Entity{};
        R::EditorCommandHistory History;
        R::EditorProcessingContext Context;
        R::PropertySmoothingConfig Config;
        AdmittedHarness()
        {
            World = Worlds.CreateWorld("smoothing");
            auto& scene = *Worlds.Get(World);
            Entity = Intrinsic::Tests::MakePointDomainSource(scene, D::PointCloudPoint);
            auto& props = Intrinsic::Tests::PointDomainProperties(scene, Entity, D::PointCloudPoint);
            auto samples = props.GetOrAdd<glm::vec3>("samples", {});
            auto values = props.GetOrAdd<double>("temperature", 0.);
            for (std::size_t i = 0; i < props.Size(); ++i) { samples[i] = {float(i), 0, 0}; values[i] = double(i); }
            Device.ShaderFloat64 = true;
            Device.TransferQueue.AcceptBufferUploads = true;
            Services.BeginRegistration();
            EXPECT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Commands, Events, Jobs.Jobs(), Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            EXPECT_TRUE(Cache.OnRegister(setup).has_value());
            Config.Input = {D::PointCloudPoint, "temperature", K::Double};
            Config.Output = {D::PointCloudPoint, "smooth", K::Double};
            Config.Positions = {D::PointCloudPoint, "samples", K::Vec3};
            Config.Backend = R::PropertySmoothingBackend::Vulkan;
            Context.Scene = &scene;
            Context.World = World;
            Context.CommandHistory = &History;
            Context.SpatialIndices = &Cache;
            Context.Device = &Device;
            Jobs.Attach(Context);
        }
        ~AdmittedHarness()
        {
            R::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        auto Cmd() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }
    };
}

TEST(PropertySmoothingTransaction, ASecondRunOnTheSameOutputWaitsForTheDecision)
{
    AdmittedHarness h;
    ASSERT_TRUE(h.Cache.GpuQueriesAvailable());
    ASSERT_TRUE(R::PreviewEditorPropertySmoothingCommand(h.Cmd(), h.Id(), h.Config).Enabled) << "the harness passes the Vulkan admission";
    // A result waits on the output's ring (the seam), as after a finished run.
    auto& props = Intrinsic::Tests::PointDomainProperties(*h.Worlds.Get(h.World), h.Entity, D::PointCloudPoint);
    std::vector<double> front;
    const auto deleted = std::as_const(props).Get<bool>("v:deleted");
    for (std::size_t i = 0; i < props.Size(); ++i) if (!deleted || !deleted[i]) front.push_back(1.0);
    const auto run = R::MakeEditorPropertySmoothingTransactionForTest(h.Cmd(), h.Id(), h.Config, front, h.Cache.PropertyResidency());
    ASSERT_TRUE(run);
    const auto key = R::MakeGpuPropertyKey(h.World, h.Entity, h.Config.Output);
    ASSERT_TRUE(h.Cache.PropertyResidency()->HasRing(key));
    R::EditorPropertySmoothingResult failure;
    EXPECT_FALSE(R::StartEditorPropertySmoothing(h.Cmd(), h.Id(), h.Config, failure));
    EXPECT_EQ(failure.Status, R::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_NE(failure.Message.find("Accept or Discard"), std::string::npos) << failure.Message;
    // Once decided, a new run starts (it queues a job on the mock device).
    R::DiscardEditorPropertySmoothing(h.Cmd(), run);
    EXPECT_FALSE(h.Cache.PropertyResidency()->HasRing(key));
    const auto next = R::StartEditorPropertySmoothing(h.Cmd(), h.Id(), h.Config, failure);
    ASSERT_TRUE(next) << failure.Message;
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Cmd(), next).Phase, R::EditorGpuTransactionPhase::Running);
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Cmd(), next).AcceptDisabledReason, "No GPU result waits for Accept.");
    R::DiscardEditorPropertySmoothing(h.Cmd(), next);
    EXPECT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Cmd(), next).Phase, R::EditorGpuTransactionPhase::Discarded);
}

TEST(PropertySmoothingTransaction, FloatOutputsAcceptInTheirOwnPrecision)
{
    Harness h(K::Float);
    const auto run = h.Ready(0.5);
    ASSERT_TRUE(run);
    std::optional<R::EditorPropertySmoothingResult> delivered;
    ASSERT_EQ(R::AcceptEditorPropertySmoothing(h.Commands(), run, [&](R::EditorPropertySmoothingResult r) { delivered = r; }).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::Applied) << delivered->Message;
    const auto smooth = std::as_const(h.Props()).Get<float>("smooth");
    ASSERT_TRUE(smooth);
    EXPECT_FLOAT_EQ(smooth[0], 0.5f);
    const auto front = h.Residency.Front(h.Key());
    ASSERT_TRUE(front);
    EXPECT_EQ(front->Layout.Scalar, G::GpuScalarType::Float32);
    EXPECT_EQ(front->Revision, *h.OutputRevision());
}

TEST(PropertySmoothingTransaction, ShowAcceptsAPendingResidentOutputAndFallsBackAfterDiscard)
{
    // "Show <output>" for a newly named output that exists only as a ring front (a result
    // awaiting Accept) is accepted; after Discard the same request is refused as usual and
    // the appearance that still names it encodes as a missing source, not an error.
    Harness h;
    h.Config.Output = {D::PointCloudPoint, "temperature_smoothed", K::Double};
    const auto run = h.Ready(6.0);
    ASSERT_TRUE(run);
    ASSERT_FALSE(h.Props().Exists("temperature_smoothed"));
    R::EditorVisualizationEditingContext view;
    view.Scene = &h.Scene;
    view.World = h.Context.World;
    view.CommandHistory = &h.History;
    view.VisualizationCommandsAvailable = true;
    view.PendingResidentScalar = [&](const Extrinsic::ECS::EntityHandle entity, const R::GeometryPropertyRef& ref) {
        return h.Residency.HasRing(R::MakeGpuPropertyKey(h.Context.World, entity, R::GpuPropertyPresentationRef(ref)));
    };
    const R::EditorVisualizationRecipeCommand show{.StableEntityId = h.Id(), .EnableRecipe = true,
                                                   .Recipe = R::MakeEditorPropertyVisualizationRecipe(h.Config.Output)};
    EXPECT_EQ(R::ApplyEditorVisualizationRecipeCommand(view, show), R::EditorCommandStatus::Applied);
    namespace GC = Extrinsic::Graphics::Components;
    const auto shown = [&]() -> std::optional<GC::VisualizationConfig> {
        if (const auto* overrides = h.Scene.Raw().try_get<GC::VisualizationLaneOverrides>(h.Entity); overrides && overrides->Points)
            return overrides->Points;
        if (const auto* config = h.Scene.Raw().try_get<GC::VisualizationConfig>(h.Entity)) return *config;
        return std::nullopt;
    };
    auto appearance = shown();
    ASSERT_TRUE(appearance);
    EXPECT_EQ(appearance->Source, GC::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(appearance->ScalarFieldName, "temperature_smoothed");
    // Extraction encodes the appearance from the front (external element count) while the
    // ring exists, and reports a missing source (no error) once it is gone.
    const auto availability = R::BuildGeometryAvailability(h.Scene.Raw(), h.Entity);
    R::ScalarVisualizationRecipe recipe{.Source = {D::PointCloudPoint, "temperature_smoothed", K::Unknown},
                                        .OutputName = "temperature_smoothed", .BufferBDA = 0x1000u, .AutoRange = false,
                                        .RangeMin = 0.f, .RangeMax = 1.f};
    recipe.ExternalElementCount = std::uint32_t(h.Props().Size());
    EXPECT_EQ(R::EncodeVisualizationRecipe(availability, {.Data = recipe}).Status, R::VisualizationRecipeStatus::Encoded);
    R::DiscardEditorPropertySmoothing(h.Commands(), run);
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    EXPECT_EQ(R::ApplyEditorVisualizationRecipeCommand(view, show), R::EditorCommandStatus::InvalidVisualizationProperty)
        << "without a front the ordinary refusal returns";
    appearance = shown();
    ASSERT_TRUE(appearance);
    EXPECT_EQ(appearance->ScalarFieldName, "temperature_smoothed") << "the appearance keeps naming the scalar";
    R::ScalarVisualizationRecipe fallback{.Source = {D::PointCloudPoint, "temperature_smoothed", K::Unknown},
                                          .OutputName = "temperature_smoothed"};
    EXPECT_EQ(R::EncodeVisualizationRecipe(availability, {.Data = fallback}).Status, R::VisualizationRecipeStatus::MissingSource)
        << "the CPU upload path reports a missing source and the lane falls back";
}

// RUNTIME-313: a duplicate Vulkan start answers Pending with the shared wording, like every queued job.
TEST(PropertySmoothingTransaction, DuplicateStartIsPendingWithTheSharedMessage)
{
    AdmittedHarness h;
    h.Context.JobCommands.FindActive = [](const R::EditorJobIdentity& identity) { return std::optional{R::EditorJobRecord{.Token=R::JobToken{5,1},.Identity=identity,.State=R::JobState::Running}}; };
    R::EditorPropertySmoothingResult failure;
    EXPECT_FALSE(R::StartEditorPropertySmoothing(h.Cmd(), h.Id(), h.Config, failure));
    EXPECT_EQ(failure.Status, R::EditorCommandStatus::Pending) << failure.Message;
    EXPECT_EQ(failure.Message, "Property smoothing already has an active running job (job 5:1).");
}

// RUNTIME-311: on the shared lifecycle a Discard issued by a history observer while Accept
// publishes is ignored (before: the smoothing run ended Discarded/StaleEntity mid-publication,
// while the property was published), and the callback fires exactly once; a second Accept
// while the first is under way answers Pending (before: InvalidProcessingParameters).
TEST(PropertySmoothingTransaction, ReentrantDiscardDuringAcceptStillDeliversAppliedOnce)
{
    Harness h;
    R::EditorPropertySmoothingTransactionHandle run;
    unsigned discards = 0;
    h.Context.InvalidateWorkspaceSnapshotCache = [&] { ++discards; R::DiscardEditorPropertySmoothing(h.Commands(), run); };
    run = h.Ready(7.0);
    ASSERT_TRUE(run);
    std::vector<R::EditorPropertySmoothingResult> results;
    ASSERT_EQ(R::AcceptEditorPropertySmoothing(h.Commands(), run, [&](auto r) { results.push_back(r); }).Status,
              R::EditorCommandStatus::Pending);
    EXPECT_EQ(R::AcceptEditorPropertySmoothing(h.Commands(), run, {}).Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_GT(discards, 0u) << "the publication ran its observer";
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().Status, R::EditorCommandStatus::Applied) << results.front().Message;
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(h.Commands(), run).Phase, R::EditorGpuTransactionPhase::Applied);
    EXPECT_TRUE(h.Props().Exists("smooth"));
    EXPECT_FALSE(h.Residency.HasRing(h.Key())) << "the accepted front became the canonical slot";
}
