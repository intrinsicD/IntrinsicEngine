// ARCH-006 runtime Sandbox editor SessionLifecycle contract partition.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.Properties;

#include "RuntimeTestModule.hpp"

namespace Runtime = Extrinsic::Runtime;
namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace GS = Extrinsic::ECS::Components::GeometrySources;

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

[[nodiscard]] Runtime::EditorWorkspaceSnapshotRequest
MakeNoEditorModelBuildRequest() {
  Runtime::EditorWorkspaceSnapshotRequest request{};
  request.Hierarchy = false;
  request.Inspector = false;
  request.Selection = false;
  request.Document = false;
  request.SceneFile = false;
  request.FileImport = false;
  request.AssetImportQueue = false;
  request.RenderGraph = false;
  request.RenderRecipe = false;
  request.CameraRender = false;
  request.Visualization = false;
  return request;
    }

[[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }
} // namespace
TEST(SandboxEditorSession, UnattachedPrepareFrameFailsClosed)
{
    Runtime::EditorWorkspaceAttachment attachment;

    EXPECT_FALSE(attachment.IsAttached());
    EXPECT_FALSE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment).has_value());
    EXPECT_FALSE(Runtime::PrepareEditorSceneEditingFrame(attachment).Commands.IsBound());
    EXPECT_FALSE(Runtime::PrepareEditorProcessingCommands(attachment).IsBound());
    EXPECT_FALSE(Runtime::PrepareEditorPointSetFrame(attachment).Commands.IsBound());
    EXPECT_FALSE(Runtime::PrepareEditorPointConstructionFrame(attachment).Commands.IsBound());
    EXPECT_FALSE(Runtime::PrepareEditorPointCloudServiceFrame(attachment).Commands.IsBound());
    EXPECT_FALSE(Runtime::PrepareEditorVisualizationEditingFrame(attachment).Commands.IsBound());
    EXPECT_FALSE(Runtime::PrepareEditorRenderRecipeEditingFrame(attachment).Commands.IsBound());
}
TEST(SandboxEditorSession, AttachPrepareDetachBoundsPreparedFrameLifetime)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    engine.Initialize();

    Runtime::EditorWorkspaceAttachment attachment;
    attachment.Attach(engine.Worlds(), engine.Services());
    EXPECT_TRUE(attachment.IsAttached());
    EXPECT_FALSE(Runtime::PrepareEditorSceneEditingFrame(attachment).Commands.IsBound());

    const auto workspace = Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment);
    ASSERT_TRUE(workspace.has_value());
    Runtime::EditorSceneEditingPreparedFrame scene =
        Runtime::PrepareEditorSceneEditingFrame(attachment);
    const Runtime::EditorProcessingCommands processing =
        Runtime::PrepareEditorProcessingCommands(attachment);
    const Runtime::EditorRenderRecipeEditingPreparedFrame renderRecipe =
        Runtime::PrepareEditorRenderRecipeEditingFrame(attachment);
    EXPECT_TRUE(scene.SceneAvailable);
    EXPECT_FALSE(renderRecipe.CommandsAvailable);
    EXPECT_FALSE(Runtime::AreEditorProcessingConfigCommandsAvailable(processing));
    // A service family with no attached service reports both runs unavailable.
    const Runtime::EditorPointCloudServicePreparedFrame services =
        Runtime::PrepareEditorPointCloudServiceFrame(attachment);
    EXPECT_FALSE(services.ClusteringAvailable);
    EXPECT_FALSE(services.PointCloudConsolidationAvailable);
    scene.LastAssetImportResult = Runtime::EditorFileImportResult{
        .Status = Runtime::EditorCommandStatus::Applied,
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    };
    EXPECT_EQ(renderRecipe.Draft.DraftRevision, 0u);
    const Runtime::EditorRenderRecipeCommandResult update =
        Runtime::ApplyEditorRenderRecipeCommand(
            renderRecipe.Commands,
            Runtime::EditorRenderRecipeCommand{
                .Kind = Runtime::EditorRenderRecipeCommandKind::UpdateDraft,
                .Document = "{}",
                .SourceId = "session-lifecycle-test",
            });
    EXPECT_TRUE(update.Succeeded());
    EXPECT_EQ(update.Revision, 1u);
    EXPECT_TRUE(workspace->Frame.FileImport.Enabled);

    attachment.Detach();
    EXPECT_FALSE(attachment.IsAttached());
    EXPECT_FALSE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment).has_value());

    attachment.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment).has_value());
    const Runtime::EditorSceneEditingPreparedFrame reattachedScene =
        Runtime::PrepareEditorSceneEditingFrame(attachment);
    const Runtime::EditorRenderRecipeEditingPreparedFrame reattachedRecipe =
        Runtime::PrepareEditorRenderRecipeEditingFrame(attachment);
    EXPECT_FALSE(reattachedScene.LastAssetImportResult.has_value());
    EXPECT_TRUE(reattachedRecipe.Draft.DraftDocument.empty());
    EXPECT_EQ(reattachedRecipe.Draft.DraftRevision, 0u);

    attachment.Detach();
    engine.Shutdown();
}
TEST(SandboxEditorSession, StaleCopiedCommandSurfacesFailAfterDetachAndReattach)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    engine.Initialize();

    Runtime::EditorWorkspaceAttachment attachment;
    attachment.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                    attachment, MakeNoEditorModelBuildRequest())
                    .has_value());

    Runtime::EditorSceneEditingCommands staleSceneCommands{};
    Runtime::EditorProcessingCommands staleGeometryCommands{};
    Runtime::EditorPointCloudServicePreparedFrame staleServiceFrame{};
    Runtime::EditorVisualizationEditingCommands staleVisualizationCommands{};
    Runtime::EditorRenderRecipeEditingCommands staleRenderRecipeCommands{};
    Runtime::EditorWorkspaceSnapshotQueries staleSnapshotQueries{};
    Runtime::EditorDocumentCommandSurface staleDocumentCommands{};
    Runtime::EditorParameterizationPreparedFrame staleParameterization{};
    staleSceneCommands =
        Runtime::PrepareEditorSceneEditingFrame(attachment).Commands;
    staleGeometryCommands = Runtime::PrepareEditorProcessingCommands(attachment);
    staleServiceFrame = Runtime::PrepareEditorPointCloudServiceFrame(attachment);
    staleParameterization = Runtime::PrepareEditorParameterizationFrame(attachment);
    staleVisualizationCommands =
        Runtime::PrepareEditorVisualizationEditingFrame(attachment).Commands;
    staleRenderRecipeCommands =
        Runtime::PrepareEditorRenderRecipeEditingFrame(attachment).Commands;
    staleSnapshotQueries =
        Runtime::PrepareEditorWorkspaceSnapshotFrame(
            attachment, MakeNoEditorModelBuildRequest())
            ->SnapshotQueries;
    staleDocumentCommands =
        Runtime::PrepareEditorSceneEditingFrame(attachment).DocumentCommands;
    ASSERT_TRUE(staleSceneCommands.IsBound());
    ASSERT_TRUE(staleGeometryCommands.IsBound());
    ASSERT_TRUE(staleParameterization.UvViewCommands.Available());
    ASSERT_TRUE(staleVisualizationCommands.IsBound());
    ASSERT_TRUE(staleRenderRecipeCommands.IsBound());
    ASSERT_TRUE(staleSnapshotQueries.IsBound());
    ASSERT_TRUE(staleDocumentCommands.Available());
    const Runtime::EditorFileImportResult activeImport = Runtime::ApplyEditorFileImportCommand(
        staleSceneCommands, Runtime::EditorFileImportCommand{
                                .Path = "/tmp/intrinsic-session-active-command.obj",
                                .PayloadKind = Assets::AssetPayloadKind::Mesh,
                            });
    EXPECT_EQ(activeImport.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_TRUE(activeImport.Operation.IsValid());
    EXPECT_EQ(activeImport.Error, Core::ErrorCode::Success);

    attachment.Detach();
    attachment.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                    attachment, MakeNoEditorModelBuildRequest())
                    .has_value());

    EXPECT_FALSE(staleSceneCommands.IsBound());
    EXPECT_FALSE(staleGeometryCommands.IsBound());
    // The service frame keeps its borrowed pointers; every entry point gates on
    // the handle first, so a stale copy reports unavailable instead of touching
    // a service that the previous attachment owned.
    EXPECT_FALSE(staleServiceFrame.Commands.IsBound());
    EXPECT_FALSE(Runtime::IsEditorClusteringAvailable(
        staleServiceFrame.Commands, staleServiceFrame.Clustering));
    EXPECT_FALSE(Runtime::IsEditorPointCloudConsolidationAvailable(
        staleServiceFrame.Commands, staleServiceFrame.PointCloudConsolidation));
    EXPECT_FALSE(Runtime::PreviewEditorKMeansRun(staleServiceFrame.Commands,
        staleServiceFrame.Clustering, {}).Enabled);
    EXPECT_EQ(Runtime::SubmitKMeansRun(staleServiceFrame.Commands,
                                       staleServiceFrame.Clustering, {})
                  .Status,
              Runtime::KMeansRunStatus::ModuleUnavailable);
    EXPECT_EQ(Runtime::SubmitEditorPointCloudConsolidation(
                  staleServiceFrame.Commands,
                  staleServiceFrame.PointCloudConsolidation, {})
                  .Status,
              Runtime::PointCloudConsolidationRunStatus::ModuleUnavailable);
    EXPECT_FALSE(staleVisualizationCommands.IsBound());
    EXPECT_FALSE(staleRenderRecipeCommands.IsBound());
    EXPECT_FALSE(staleSnapshotQueries.IsBound());
    EXPECT_FALSE(Runtime::SelectEditorEntity(staleSceneCommands, 1u));
    EXPECT_EQ(Runtime::ApplyEditorTransformEdit(
                  staleSceneCommands,
                  Runtime::EditorTransformEditCommand{
                      .StableEntityId = 1u,
                      .SetPosition = true,
                  }),
              Runtime::EditorCommandStatus::MissingScene);
    EXPECT_EQ(staleDocumentCommands.Undo().Status, Runtime::EditorCommandHistoryStatus::NoChange);
    EXPECT_FALSE(Runtime::BuildEditorWorkspaceSnapshot(staleSnapshotQueries).FileImport.Enabled);
    const Runtime::EditorFileImportResult staleImport = Runtime::ApplyEditorFileImportCommand(
        staleSceneCommands, Runtime::EditorFileImportCommand{
                                .Path = "/tmp/intrinsic-session-stale-command.obj",
                                .PayloadKind = Assets::AssetPayloadKind::Mesh,
                            });
    EXPECT_EQ(staleImport.Status, Runtime::EditorCommandStatus::AssetImportFailed);
    EXPECT_EQ(staleImport.Error, Core::ErrorCode::InvalidState);
    const Runtime::EditorParameterizationUvViewState staleUvState =
        Runtime::SubmitEditorParameterizationUvView(
            staleParameterization.UvViewCommands,
            Runtime::EditorParameterizationViewModel{
                .HasSelectedEntity = true,
                .SelectedEntityIsMesh = true,
                .HasUvCoordinates = true,
                .HasFiniteUvBounds = true,
                .SelectedStableEntityId = 7u,
                .View =
                    Runtime::ParameterizationViewConfig{
                        .RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded,
                        .BackgroundMode = Runtime::ParameterizationUvBackgroundMode::Texture,
                    },
            },
            320u, 180u);
    EXPECT_EQ(staleUvState.Status,
              Runtime::EditorParameterizationUvViewStatus::CpuFallbackNonOperational);
    EXPECT_EQ(staleUvState.ActiveMode, Runtime::ParameterizationUvRenderMode::CpuLayout);
    EXPECT_EQ(staleUvState.ActiveBackground, Runtime::ParameterizationUvBackgroundMode::Checker);
    EXPECT_NE(staleUvState.RequestToken, 0u);
    EXPECT_FALSE(staleUvState.GpuReady);
    EXPECT_NE(staleUvState.Message.find("attachment expired"), std::string::npos);
    ASSERT_TRUE(static_cast<bool>(staleParameterization.UvViewCommands.TextureTabs));
    EXPECT_TRUE(staleParameterization.UvViewCommands.TextureTabs(1u).empty());

    attachment.Detach();
    engine.Shutdown();
}
TEST(SandboxEditorSession, ReattachObservesEqualSequenceFromDifferentEngine)
{
    Runtime::EditorWorkspaceAttachment attachment;
    std::uint64_t firstSequence = 0u;

    {
        Extrinsic::Runtime::Engine firstEngine(HeadlessConfig());
        firstEngine.EmplaceModule<Runtime::SceneDocumentModule>();
        firstEngine.EmplaceModule<Runtime::AssetWorkflowModule>();
        firstEngine.Initialize();
        EXPECT_FALSE(RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(firstEngine)
                         .ImportAssetFromPath(
                             Runtime::RuntimeAssetImportRequest{
                                 .Path =
                                     "/tmp/intrinsic-session-first-missing.obj",
                                 .PayloadKind =
                                     Assets::AssetPayloadKind::Mesh,
                             })
                         .has_value());
        const auto& event = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(firstEngine)
                                .GetLastAssetImportEvent();
        ASSERT_TRUE(event.has_value());
        firstSequence = event->Sequence;

        attachment.Attach(firstEngine.Worlds(), firstEngine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                        attachment, MakeNoEditorModelBuildRequest())
                        .has_value());
        const Runtime::EditorSceneEditingPreparedFrame scene =
            Runtime::PrepareEditorSceneEditingFrame(attachment);
        ASSERT_TRUE(scene.LastAssetImportResult.has_value());
        EXPECT_EQ(scene.LastAssetImportResult->PayloadKind,
                  Assets::AssetPayloadKind::Mesh);
        attachment.Detach();
        firstEngine.Shutdown();
    }

    {
        Extrinsic::Runtime::Engine secondEngine(HeadlessConfig());
        secondEngine.EmplaceModule<Runtime::SceneDocumentModule>();
        secondEngine.EmplaceModule<Runtime::AssetWorkflowModule>();
        secondEngine.Initialize();
        EXPECT_FALSE(RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(secondEngine)
                         .ImportAssetFromPath(
                             Runtime::RuntimeAssetImportRequest{
                                 .Path =
                                     "/tmp/intrinsic-session-second-missing.xyz",
                                 .PayloadKind =
                                     Assets::AssetPayloadKind::PointCloud,
                             })
                         .has_value());
        const auto& event = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(secondEngine)
                                .GetLastAssetImportEvent();
        ASSERT_TRUE(event.has_value());
        ASSERT_EQ(event->Sequence, firstSequence);

        attachment.Attach(secondEngine.Worlds(), secondEngine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                        attachment, MakeNoEditorModelBuildRequest())
                        .has_value());
        const Runtime::EditorSceneEditingPreparedFrame scene =
            Runtime::PrepareEditorSceneEditingFrame(attachment);
        ASSERT_TRUE(scene.LastAssetImportResult.has_value());
        EXPECT_EQ(scene.LastAssetImportResult->PayloadKind,
                  Assets::AssetPayloadKind::PointCloud);
        attachment.Detach();
        secondEngine.Shutdown();
    }
}

// BUG-141: a geometry-processing outcome had no lifetime — it was superseded
// only by the next run of the same operation, and a user who was not going to
// run that operation again had no way to clear it. The session now owns an
// explicit per-slot dismissal, and dismissing one operation must not touch
// another operation's outcome.
TEST(SandboxEditorSession, DismissClearsOneGeometryProcessingResultSlot)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    engine.Initialize();

    Runtime::EditorWorkspaceAttachment attachment;
    attachment.Attach(engine.Worlds(), engine.Services());

    // Each observation requests an independent copy of the current session results.
    const auto observe = [&attachment]() -> Runtime::EditorPointSetResultsSnapshot
    {
        EXPECT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment)
                        .has_value());
        return Runtime::PrepareEditorPointSetFrame(attachment).Results;
    };

    ASSERT_TRUE(
        Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment).has_value());
    const Runtime::EditorPointSetPreparedFrame prepared =
        Runtime::PrepareEditorPointSetFrame(attachment);
    ASSERT_TRUE(static_cast<bool>(prepared.ResultSinks.DismissResult));
    // Mesh topology owns its own slots; observing them must not require the
    // broad processing frame.
    const auto observeTopology =
        [&attachment]() -> Runtime::EditorMeshTopologyResultsSnapshot
    {
        EXPECT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment)
                        .has_value());
        return Runtime::PrepareEditorMeshTopologyFrame(attachment).Results;
    };
    const Runtime::EditorMeshTopologyPreparedFrame topology =
        Runtime::PrepareEditorMeshTopologyFrame(attachment);
    ASSERT_TRUE(static_cast<bool>(topology.ResultSinks.MeshSimplify));
    ASSERT_TRUE(static_cast<bool>(topology.ResultSinks.MeshDenoise));
    ASSERT_TRUE(static_cast<bool>(topology.ResultSinks.DismissResult));
    const auto normals = Runtime::PrepareEditorNormalFrame(attachment);
    ASSERT_TRUE(static_cast<bool>(normals.ResultSinks.NormalEstimation));
    normals.ResultSinks.NormalEstimation({.Status = Runtime::EditorCommandStatus::Applied,
                                           .ActualBackend = "cpu_lbvh", .Message = "named normals published"});
    ASSERT_TRUE(Runtime::PrepareEditorNormalFrame(attachment).Results.LastNormalEstimationResult);
    EXPECT_EQ(Runtime::PrepareEditorNormalFrame(attachment).Results.LastNormalEstimationResult->ActualBackend, "cpu_lbvh");
    ASSERT_TRUE(static_cast<bool>(prepared.ResultSinks.BilateralFilter));
    prepared.ResultSinks.BilateralFilter({.Status = Runtime::EditorCommandStatus::Applied,
                                           .ActualBackend = "cpu_lbvh", .Message = "positions filtered"});
    ASSERT_TRUE(observe().LastBilateralFilterResult);
    EXPECT_EQ(observe().LastBilateralFilterResult->ActualBackend, "cpu_lbvh");

    Runtime::EditorMeshSimplifyResult simplify{};
    simplify.Status = Runtime::EditorCommandStatus::GeometryProcessingFailed;
    simplify.Message = "simplify rejected the selected mesh";
    topology.ResultSinks.MeshSimplify(simplify);

    Runtime::EditorMeshDenoiseResult denoise{};
    denoise.Status = Runtime::EditorCommandStatus::NoChange;
    denoise.Message = "denoise moved nothing";
    topology.ResultSinks.MeshDenoise(denoise);

    {
        const auto results = observeTopology();
        ASSERT_TRUE(results.LastMeshSimplifyResult.has_value());
        ASSERT_TRUE(results.LastMeshDenoiseResult.has_value());
    }

    // The next run of the same operation supersedes rather than accumulates.
    simplify.Message = "simplify rejected it again";
    topology.ResultSinks.MeshSimplify(simplify);
    {
        const auto results = observeTopology();
        ASSERT_TRUE(results.LastMeshSimplifyResult.has_value());
        EXPECT_EQ(results.LastMeshSimplifyResult->Message,
                  "simplify rejected it again");
    }

    topology.ResultSinks.DismissResult(
        Runtime::EditorMeshTopologyResultSlot::MeshSimplify);
    {
        const auto results = observeTopology();
        EXPECT_FALSE(results.LastMeshSimplifyResult.has_value());
        ASSERT_TRUE(results.LastMeshDenoiseResult.has_value())
            << "dismissing one operation must not clear another's outcome";
        EXPECT_EQ(results.LastMeshDenoiseResult->Message,
                  "denoise moved nothing");
    }

    topology.ResultSinks.DismissResult(
        Runtime::EditorMeshTopologyResultSlot::MeshDenoise);
    EXPECT_FALSE(observeTopology().LastMeshDenoiseResult.has_value());

    // Curvature is the mesh-field family's only retained outcome, and it
    // dismisses without naming a slot.
    const auto fields = Runtime::PrepareEditorMeshFieldFrame(attachment);
    ASSERT_TRUE(static_cast<bool>(fields.ResultSinks.MeshCurvature));
    fields.ResultSinks.MeshCurvature({.Status = Runtime::EditorCommandStatus::Applied,
                                      .Message = "curvature published"});
    ASSERT_TRUE(Runtime::PrepareEditorMeshFieldFrame(attachment)
                    .Results.LastMeshCurvatureResult);
    // Clearing a mesh-field outcome must leave the topology family alone.
    topology.ResultSinks.MeshSimplify(simplify);
    fields.ResultSinks.DismissResult();
    EXPECT_FALSE(Runtime::PrepareEditorMeshFieldFrame(attachment)
                     .Results.LastMeshCurvatureResult);
    ASSERT_TRUE(observeTopology().LastMeshSimplifyResult.has_value());
    topology.ResultSinks.DismissResult(
        Runtime::EditorMeshTopologyResultSlot::MeshSimplify);

    EXPECT_TRUE(Runtime::PrepareEditorNormalFrame(attachment).Results.LastNormalEstimationResult);
    normals.ResultSinks.DismissResult();
    EXPECT_FALSE(Runtime::PrepareEditorNormalFrame(attachment).Results.LastNormalEstimationResult);

    const auto parameterization = Runtime::PrepareEditorParameterizationFrame(attachment);
    ASSERT_TRUE(static_cast<bool>(parameterization.ResultSinks.Parameterization));
    ASSERT_TRUE(static_cast<bool>(parameterization.ResultSinks.UvRegeneration));
    parameterization.ResultSinks.Parameterization({.Status = Runtime::EditorCommandStatus::Applied,
                                                   .Message = "uv solved"});
    parameterization.ResultSinks.UvRegeneration({.Status = Runtime::EditorCommandStatus::Applied,
                                                 .Diagnostic = "atlas rebuilt"});
    {
        const auto observed = Runtime::PrepareEditorParameterizationFrame(attachment);
        ASSERT_TRUE(observed.Results.LastParameterizationResult);
        ASSERT_TRUE(observed.Results.LastUvRegenerationResult);
    }
    // The two parameterization slots dismiss independently of each other.
    parameterization.ResultSinks.DismissResult();
    {
        const auto observed = Runtime::PrepareEditorParameterizationFrame(attachment);
        EXPECT_FALSE(observed.Results.LastParameterizationResult);
        ASSERT_TRUE(observed.Results.LastUvRegenerationResult);
    }
    parameterization.ResultSinks.DismissUvRegenerationResult();
    EXPECT_FALSE(Runtime::PrepareEditorParameterizationFrame(attachment)
                     .Results.LastUvRegenerationResult);

    const auto registration = Runtime::PrepareEditorRegistrationFrame(attachment);
    ASSERT_TRUE(static_cast<bool>(registration.ResultSinks.Registration));
    registration.ResultSinks.Registration({.Status = Runtime::EditorCommandStatus::Applied,
                                           .Message = "source aligned onto target"});
    ASSERT_TRUE(Runtime::PrepareEditorRegistrationFrame(attachment).Results.LastRegistrationResult);
    // Registration owns its own slot: dismissing it leaves sibling families alone.
    registration.ResultSinks.DismissResult();
    EXPECT_FALSE(Runtime::PrepareEditorRegistrationFrame(attachment).Results.LastRegistrationResult);
    EXPECT_TRUE(observe().LastBilateralFilterResult);
    prepared.ResultSinks.DismissResult(Runtime::EditorPointSetResultSlot::BilateralFilter);
    EXPECT_FALSE(observe().LastBilateralFilterResult);

    // A dismissal sink copied out of a prepared frame must not reach into a
    // detached session, which is the same epoch rule every result sink obeys.
    attachment.Detach();
    topology.ResultSinks.DismissResult(
        Runtime::EditorMeshTopologyResultSlot::MeshSimplify);
    fields.ResultSinks.DismissResult();

    engine.Shutdown();
}

TEST(SandboxEditorSession, TypedResultSinksCopyDismissAndRejectExpiredAttachments)
{
    Runtime::Engine engine(HeadlessConfig());
    engine.Initialize();
    Runtime::EditorPointSetPreparedFrame expired;
    Runtime::EditorPointCloudServicePreparedFrame expiredService{};
    Runtime::EditorParameterizationPreparedFrame expiredParameterization{};
    {
        Runtime::EditorWorkspaceAttachment attachment;
        const auto unattachedService =
            Runtime::PrepareEditorPointCloudServiceFrame(attachment);
        EXPECT_FALSE(unattachedService.Results.LastKMeansResult);
        EXPECT_FALSE(unattachedService.Results.LastPointCloudConsolidationResult);
        const auto unattachedPointSet = Runtime::PrepareEditorPointSetFrame(attachment);
        const auto unattachedConstruction =
            Runtime::PrepareEditorPointConstructionFrame(attachment);
        const auto unattachedAnalysis = Runtime::PrepareEditorPointAnalysisFrame(attachment);
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        // One copied frame per family: the sink publishes into the session, a
        // freshly prepared frame observes it, older copies stay independent, and
        // the family's own dismissal clears exactly its slot.
        const auto check = [&]<typename Frame, typename Sinks, typename Snapshot,
                               typename Result, typename Slot>(
            Frame (*prepare)(const Runtime::EditorWorkspaceAttachment&),
            const Frame& unattached,
            std::function<void(Result)> Sinks::* sink,
            std::optional<Result> Snapshot::* member,
            Slot slot)
        {
            SCOPED_TRACE(int(slot));
            const Frame empty = prepare(attachment);
            ASSERT_TRUE(empty.ResultSinks.*sink);
            EXPECT_FALSE(unattached.Results.*member);
            Result result{};
            result.Status = Runtime::EditorCommandStatus::Applied;
            (empty.ResultSinks.*sink)(result);
            const Frame first = prepare(attachment);
            ASSERT_TRUE(first.Results.*member);
            EXPECT_EQ((first.Results.*member)->Status, Runtime::EditorCommandStatus::Applied);
            EXPECT_FALSE(empty.Results.*member);
            result.Status = Runtime::EditorCommandStatus::NoChange;
            (empty.ResultSinks.*sink)(result);
            const Frame replacement = prepare(attachment);
            ASSERT_TRUE(replacement.Results.*member);
            EXPECT_EQ((replacement.Results.*member)->Status,
                      Runtime::EditorCommandStatus::NoChange);
            empty.ResultSinks.DismissResult(slot);
            EXPECT_FALSE(prepare(attachment).Results.*member);
            EXPECT_EQ((first.Results.*member)->Status, Runtime::EditorCommandStatus::Applied);
        };
        using PointSetSinks = Runtime::EditorPointSetResultSinks;
        using PointSetResults = Runtime::EditorPointSetResultsSnapshot;
        using PointSetSlot = Runtime::EditorPointSetResultSlot;
        check(&Runtime::PrepareEditorPointSetFrame, unattachedPointSet,
              &PointSetSinks::ProgressivePoisson,
              &PointSetResults::LastProgressivePoissonResult,
              PointSetSlot::ProgressivePoisson);
        check(&Runtime::PrepareEditorPointSetFrame, unattachedPointSet,
              &PointSetSinks::BilateralFilter,
              &PointSetResults::LastBilateralFilterResult, PointSetSlot::BilateralFilter);
        check(&Runtime::PrepareEditorPointAnalysisFrame, unattachedAnalysis,
              &Runtime::EditorPointAnalysisResultSinks::DescriptorAnalysis,
              &Runtime::EditorPointAnalysisResultsSnapshot::LastDescriptorAnalysisResult,
              Runtime::EditorPointAnalysisResultSlot::DescriptorAnalysis);
        check(&Runtime::PrepareEditorPointConstructionFrame, unattachedConstruction,
              &Runtime::EditorPointConstructionResultSinks::PointConstruction,
              &Runtime::EditorPointConstructionResultsSnapshot::LastPointConstructionResult,
              Runtime::EditorPointConstructionResultSlot::PointConstruction);

        expired = Runtime::PrepareEditorPointSetFrame(attachment);
        expiredService = Runtime::PrepareEditorPointCloudServiceFrame(attachment);
        expiredParameterization = Runtime::PrepareEditorParameterizationFrame(attachment);
        expired.ResultSinks.BilateralFilter({.Message = "before detach"});
        ASSERT_TRUE(Runtime::PrepareEditorPointSetFrame(attachment)
                        .Results.LastBilateralFilterResult);
        attachment.Detach();
        const auto detached = Runtime::PrepareEditorPointSetFrame(attachment);
        EXPECT_FALSE(detached.Commands.IsBound());
        EXPECT_FALSE(detached.Results.LastBilateralFilterResult);
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        auto current = Runtime::PrepareEditorPointSetFrame(attachment);
        current.ResultSinks.BilateralFilter({.Message = "new attachment"});
        expired.ResultSinks.BilateralFilter({.Message = "old attachment"});
        expired.ResultSinks.DismissResult(PointSetSlot::BilateralFilter);
        const auto fresh = Runtime::PrepareEditorPointSetFrame(attachment);
        ASSERT_TRUE(fresh.Results.LastBilateralFilterResult);
        EXPECT_EQ(fresh.Results.LastBilateralFilterResult->Message, "new attachment");
        EXPECT_FALSE(expired.Commands.IsBound());
    }
    // The callbacks can outlive the session object itself.
    expired.ResultSinks.BilateralFilter({.Message = "after destruction"});
    expired.ResultSinks.DismissResult(Runtime::EditorPointSetResultSlot::BilateralFilter);
    EXPECT_FALSE(expired.Commands.IsBound());
    // A service frame copied out of a destroyed session still holds its borrowed
    // pointers; the handle check keeps every entry point from dereferencing one.
    expiredService.ResultSinks.DismissResult(
        Runtime::EditorPointCloudServiceResultSlot::KMeans);
    EXPECT_FALSE(expiredService.Commands.IsBound());
    EXPECT_FALSE(Runtime::IsEditorClusteringAvailable(
        expiredService.Commands, expiredService.Clustering));
    EXPECT_FALSE(Runtime::IsEditorPointCloudConsolidationAvailable(
        expiredService.Commands, expiredService.PointCloudConsolidation));
    EXPECT_EQ(Runtime::PrepareEditorPointCloudConsolidationAvailability(
                  expiredService.Commands,
                  expiredService.PointCloudConsolidation, {})
                  .Available,
              false);
    EXPECT_FALSE(Runtime::BuildEditorParameterizationViewModel(
                     expiredParameterization.Commands, expiredParameterization.Results)
                     .HasLastResult);
    engine.Shutdown();
}

TEST(SandboxEditorSessionLifecycle, MeshTopologyFramesRetainAllSlotsAndRejectExpiredDelivery)
{
    Runtime::Engine engine{HeadlessConfig()};
    engine.Initialize();
    Runtime::EditorMeshTopologyPreparedFrame expired{};
    {
        Runtime::EditorWorkspaceAttachment attachment;
        const auto unattached = Runtime::PrepareEditorMeshTopologyFrame(attachment);
        EXPECT_FALSE(unattached.Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto empty = Runtime::PrepareEditorMeshTopologyFrame(attachment);
        ASSERT_TRUE(empty.Commands.IsBound());
        const auto check = [&]<typename Result>(
            std::function<void(Result)> Runtime::EditorMeshTopologyResultSinks::* sink,
            std::optional<Result> Runtime::EditorMeshTopologyResultsSnapshot::* member,
            Runtime::EditorMeshTopologyResultSlot slot)
        {
            SCOPED_TRACE(int(slot));
            ASSERT_TRUE(empty.ResultSinks.*sink);
            EXPECT_FALSE(unattached.Results.*member);
            Result result{};
            result.Status = Runtime::EditorCommandStatus::Applied;
            (empty.ResultSinks.*sink)(result);
            // Reading a new family frame must see publication even without
            // rebuilding the workspace frame, and old copies stay independent.
            const auto first = Runtime::PrepareEditorMeshTopologyFrame(attachment);
            ASSERT_TRUE(first.Results.*member);
            EXPECT_EQ((first.Results.*member)->Status, Runtime::EditorCommandStatus::Applied);
            EXPECT_FALSE(empty.Results.*member);
            result.Status = Runtime::EditorCommandStatus::NoChange;
            (empty.ResultSinks.*sink)(result);
            const auto replacement = Runtime::PrepareEditorMeshTopologyFrame(attachment);
            ASSERT_TRUE(replacement.Results.*member);
            EXPECT_EQ((replacement.Results.*member)->Status, Runtime::EditorCommandStatus::NoChange);
            empty.ResultSinks.DismissResult(slot);
            EXPECT_FALSE(Runtime::PrepareEditorMeshTopologyFrame(attachment).Results.*member);
            EXPECT_EQ((first.Results.*member)->Status, Runtime::EditorCommandStatus::Applied);
        };
        using S = Runtime::EditorMeshTopologyResultSinks;
        using R = Runtime::EditorMeshTopologyResultsSnapshot;
        using Slot = Runtime::EditorMeshTopologyResultSlot;
        check(&S::MeshDenoise, &R::LastMeshDenoiseResult, Slot::MeshDenoise);
        check(&S::MeshRemesh, &R::LastMeshRemeshResult, Slot::MeshRemesh);
        check(&S::MeshSubdivide, &R::LastMeshSubdivideResult, Slot::MeshSubdivide);
        check(&S::MeshSimplify, &R::LastMeshSimplifyResult, Slot::MeshSimplify);
        empty.ResultSinks.MeshRemesh({.Message = "remeshed"});
        empty.ResultSinks.MeshSubdivide({.Message = "subdivided"});
        empty.ResultSinks.DismissResult(Slot::MeshRemesh);
        ASSERT_TRUE(Runtime::PrepareEditorMeshTopologyFrame(attachment)
                        .Results.LastMeshSubdivideResult);
        expired = empty;
        attachment.Detach();
        EXPECT_FALSE(expired.Commands.IsBound());
        // A detached handle refuses the command outright rather than running it
        // against a session that is gone.
        EXPECT_EQ(Runtime::ApplyEditorMeshSubdivideCommand(expired.Commands, {}).Status,
                  Runtime::EditorCommandStatus::MissingScene);
        EXPECT_FALSE(Runtime::PrepareEditorMeshTopologyFrame(attachment)
                         .Results.LastMeshSubdivideResult);
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto current = Runtime::PrepareEditorMeshTopologyFrame(attachment);
        current.ResultSinks.MeshSimplify({.Message = "new attachment"});
        expired.ResultSinks.MeshSimplify({.Message = "old attachment"});
        expired.ResultSinks.DismissResult(Slot::MeshSimplify);
        const auto fresh = Runtime::PrepareEditorMeshTopologyFrame(attachment);
        ASSERT_TRUE(fresh.Results.LastMeshSimplifyResult);
        EXPECT_EQ(fresh.Results.LastMeshSimplifyResult->Message, "new attachment");
        expired = current;
    }
    // The callbacks can outlive the session object itself.
    expired.ResultSinks.MeshDenoise({.Message = "after destruction"});
    expired.ResultSinks.DismissResult(Runtime::EditorMeshTopologyResultSlot::MeshDenoise);
    EXPECT_FALSE(expired.Commands.IsBound());
    engine.Shutdown();
}

TEST(SandboxEditorSessionLifecycle, MeshFieldFramesRetainCurvatureAndRejectExpiredDelivery)
{
    Runtime::Engine engine{HeadlessConfig()};
    engine.Initialize();
    Runtime::EditorMeshFieldPreparedFrame expired{};
    {
        Runtime::EditorWorkspaceAttachment attachment;
        EXPECT_FALSE(Runtime::PrepareEditorMeshFieldFrame(attachment).Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto empty = Runtime::PrepareEditorMeshFieldFrame(attachment);
        ASSERT_TRUE(empty.Commands.IsBound());
        ASSERT_TRUE(static_cast<bool>(empty.ResultSinks.MeshCurvature));
        ASSERT_TRUE(static_cast<bool>(empty.ResultSinks.DismissResult));
        EXPECT_FALSE(empty.Results.LastMeshCurvatureResult);
        empty.ResultSinks.MeshCurvature({.Status = Runtime::EditorCommandStatus::Applied,
                                         .Message = "curvature"});
        const auto first = Runtime::PrepareEditorMeshFieldFrame(attachment);
        ASSERT_TRUE(first.Results.LastMeshCurvatureResult);
        EXPECT_EQ(first.Results.LastMeshCurvatureResult->Message, "curvature");
        // The frame copied before publication keeps its own empty snapshot.
        EXPECT_FALSE(empty.Results.LastMeshCurvatureResult);
        empty.ResultSinks.MeshCurvature({.Message = "replacement"});
        EXPECT_EQ(Runtime::PrepareEditorMeshFieldFrame(attachment)
                      .Results.LastMeshCurvatureResult->Message,
                  "replacement");
        EXPECT_EQ(first.Results.LastMeshCurvatureResult->Message, "curvature");
        empty.ResultSinks.DismissResult();
        EXPECT_FALSE(Runtime::PrepareEditorMeshFieldFrame(attachment)
                         .Results.LastMeshCurvatureResult);
        expired = empty;
        attachment.Detach();
        EXPECT_FALSE(expired.Commands.IsBound());
        EXPECT_FALSE(Runtime::GetEditorMeshCurvatureConfig(expired.Commands));
        EXPECT_FALSE(Runtime::GetEditorGeodesicsConfig(expired.Commands));
        EXPECT_EQ(Runtime::ApplyEditorGeodesicsCommand(expired.Commands, {}).Status,
                  Runtime::EditorCommandStatus::MissingScene);
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto current = Runtime::PrepareEditorMeshFieldFrame(attachment);
        current.ResultSinks.MeshCurvature({.Message = "new attachment"});
        expired.ResultSinks.MeshCurvature({.Message = "old attachment"});
        expired.ResultSinks.DismissResult();
        const auto fresh = Runtime::PrepareEditorMeshFieldFrame(attachment);
        ASSERT_TRUE(fresh.Results.LastMeshCurvatureResult);
        EXPECT_EQ(fresh.Results.LastMeshCurvatureResult->Message, "new attachment");
        expired = current;
    }
    expired.ResultSinks.MeshCurvature({.Message = "after destruction"});
    expired.ResultSinks.DismissResult();
    EXPECT_FALSE(expired.Commands.IsBound());
    engine.Shutdown();
}

TEST(SandboxEditorSessionLifecycle, PointFieldFramesRetainIndependentResultsAndRejectExpiredDelivery)
{
    Runtime::Engine engine{HeadlessConfig()};
    engine.Initialize();
    Runtime::EditorPointFieldPreparedFrame expired{};
    {
        Runtime::EditorWorkspaceAttachment attachment;
        EXPECT_FALSE(Runtime::PrepareEditorPointFieldFrame(attachment).Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto empty = Runtime::PrepareEditorPointFieldFrame(attachment);
        ASSERT_TRUE(empty.Commands.IsBound());
        ASSERT_TRUE(empty.ResultSinks.KernelDensity);
        ASSERT_TRUE(empty.ResultSinks.PointSpacing);
        EXPECT_FALSE(empty.Results.LastKernelDensityResult);
        EXPECT_FALSE(empty.Results.LastPointSpacingResult);
        empty.ResultSinks.KernelDensity({.Message="density"});
        empty.ResultSinks.PointSpacing({.Message="spacing"});
        const auto first = Runtime::PrepareEditorPointFieldFrame(attachment);
        ASSERT_TRUE(first.Results.LastKernelDensityResult);
        ASSERT_TRUE(first.Results.LastPointSpacingResult);
        EXPECT_EQ(first.Results.LastKernelDensityResult->Message,"density");
        EXPECT_EQ(first.Results.LastPointSpacingResult->Message,"spacing");
        EXPECT_FALSE(empty.Results.LastKernelDensityResult);
        empty.ResultSinks.KernelDensity({.Message="replacement"});
        EXPECT_EQ(Runtime::PrepareEditorPointFieldFrame(attachment).Results.LastKernelDensityResult->Message,"replacement");
        EXPECT_EQ(first.Results.LastKernelDensityResult->Message,"density");
        empty.ResultSinks.DismissResult(Runtime::EditorPointFieldResultSlot::KernelDensity);
        const auto dismissed = Runtime::PrepareEditorPointFieldFrame(attachment);
        EXPECT_FALSE(dismissed.Results.LastKernelDensityResult);
        ASSERT_TRUE(dismissed.Results.LastPointSpacingResult);
        expired = empty;
        attachment.Detach();
        EXPECT_FALSE(expired.Commands.IsBound());
        EXPECT_FALSE(Runtime::GetEditorPointSpacingConfig(expired.Commands));
        EXPECT_FALSE(Runtime::PreviewEditorKernelDensityCommand(expired.Commands, {}).Enabled);
        EXPECT_FALSE(Runtime::PrepareEditorPointFieldFrame(attachment).Results.LastPointSpacingResult);
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto current = Runtime::PrepareEditorPointFieldFrame(attachment);
        current.ResultSinks.PointSpacing({.Message="new attachment"});
        expired.ResultSinks.PointSpacing({.Message="old attachment"});
        expired.ResultSinks.DismissResult(Runtime::EditorPointFieldResultSlot::PointSpacing);
        const auto fresh = Runtime::PrepareEditorPointFieldFrame(attachment);
        ASSERT_TRUE(fresh.Results.LastPointSpacingResult);
        EXPECT_EQ(fresh.Results.LastPointSpacingResult->Message,"new attachment");
        current.ResultSinks.DismissResult(Runtime::EditorPointFieldResultSlot::PointSpacing);
        EXPECT_FALSE(Runtime::PrepareEditorPointFieldFrame(attachment).Results.LastPointSpacingResult);
        expired = current;
    }
    expired.ResultSinks.KernelDensity({.Message="after destruction"});
    expired.ResultSinks.DismissResult(Runtime::EditorPointFieldResultSlot::KernelDensity);
    EXPECT_FALSE(Runtime::PreviewEditorPointSpacingCommand(expired.Commands, {}).Enabled);
    engine.Shutdown();
}

TEST(SandboxEditorSessionLifecycle, PointAnalysisFramesRetainAllSlotsAndRejectExpiredDelivery)
{
    Runtime::Engine engine{HeadlessConfig()};
    engine.Initialize();
    Runtime::EditorPointAnalysisPreparedFrame expired{};
    {
        Runtime::EditorWorkspaceAttachment attachment;
        const auto unattached = Runtime::PrepareEditorPointAnalysisFrame(attachment);
        EXPECT_FALSE(unattached.Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto empty = Runtime::PrepareEditorPointAnalysisFrame(attachment);
        ASSERT_TRUE(empty.Commands.IsBound());
        const auto check = [&]<typename Result>(
            std::function<void(Result)> Runtime::EditorPointAnalysisResultSinks::* sink,
            std::optional<Result> Runtime::EditorPointAnalysisResultsSnapshot::* member,
            Runtime::EditorPointAnalysisResultSlot slot)
        {
            SCOPED_TRACE(int(slot));
            ASSERT_TRUE(empty.ResultSinks.*sink);
            EXPECT_FALSE(unattached.Results.*member);
            Result result{};
            result.Status = Runtime::EditorCommandStatus::Applied;
            (empty.ResultSinks.*sink)(result);
            // Reading a new processing frame must see publication even without
            // rebuilding the workspace frame, and old copies stay independent.
            const auto first = Runtime::PrepareEditorPointAnalysisFrame(attachment);
            ASSERT_TRUE(first.Results.*member);
            EXPECT_EQ((first.Results.*member)->Status, Runtime::EditorCommandStatus::Applied);
            EXPECT_FALSE(empty.Results.*member);
            result.Status = Runtime::EditorCommandStatus::NoChange;
            (empty.ResultSinks.*sink)(result);
            const auto replacement = Runtime::PrepareEditorPointAnalysisFrame(attachment);
            ASSERT_TRUE(replacement.Results.*member);
            EXPECT_EQ((replacement.Results.*member)->Status, Runtime::EditorCommandStatus::NoChange);
            empty.ResultSinks.DismissResult(slot);
            EXPECT_FALSE(Runtime::PrepareEditorPointAnalysisFrame(attachment).Results.*member);
            EXPECT_EQ((first.Results.*member)->Status, Runtime::EditorCommandStatus::Applied);
        };
        using S = Runtime::EditorPointAnalysisResultSinks;
        using R = Runtime::EditorPointAnalysisResultsSnapshot;
        using Slot = Runtime::EditorPointAnalysisResultSlot;
        check(&S::OutlierAnalysis, &R::LastOutlierAnalysisResult, Slot::OutlierAnalysis);
        check(&S::KeypointAnalysis, &R::LastKeypointAnalysisResult, Slot::KeypointAnalysis);
        check(&S::DensityWeight, &R::LastDensityWeightResult, Slot::DensityWeight);
        empty.ResultSinks.KeypointAnalysis({.Message="keypoints"});
        empty.ResultSinks.DensityWeight({.Message="weights"});
        empty.ResultSinks.DismissResult(Slot::KeypointAnalysis);
        ASSERT_TRUE(Runtime::PrepareEditorPointAnalysisFrame(attachment).Results.LastDensityWeightResult);
        expired = empty;
        attachment.Detach();
        EXPECT_FALSE(expired.Commands.IsBound());
        EXPECT_FALSE(Runtime::GetEditorDensityWeightConfig(expired.Commands));
        EXPECT_FALSE(Runtime::PreviewEditorKeypointAnalysisCommand(expired.Commands, {}).Enabled);
        EXPECT_FALSE(Runtime::PrepareEditorPointAnalysisFrame(attachment).Results.LastDensityWeightResult);
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto current = Runtime::PrepareEditorPointAnalysisFrame(attachment);
        current.ResultSinks.OutlierAnalysis({.Message="new attachment"});
        expired.ResultSinks.OutlierAnalysis({.Message="old attachment"});
        expired.ResultSinks.DismissResult(Slot::OutlierAnalysis);
        ASSERT_TRUE(Runtime::PrepareEditorPointAnalysisFrame(attachment).Results.LastOutlierAnalysisResult);
        EXPECT_EQ(Runtime::PrepareEditorPointAnalysisFrame(attachment).Results.LastOutlierAnalysisResult->Message,"new attachment");
        expired = current;
    }
    expired.ResultSinks.OutlierAnalysis({.Message="after destruction"});
    expired.ResultSinks.KeypointAnalysis({.Message="after destruction"});
    expired.ResultSinks.DensityWeight({.Message="after destruction"});
    expired.ResultSinks.DismissResult(Runtime::EditorPointAnalysisResultSlot::OutlierAnalysis);
    EXPECT_FALSE(Runtime::PreviewEditorOutlierAnalysisCommand(expired.Commands, {}).Enabled);
    engine.Shutdown();
}

TEST(SandboxEditorSessionLifecycle, PointAnalysisRemovalClearsSelectionThroughPreparedCommands)
{
    Runtime::Engine engine{HeadlessConfig()};
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.Initialize();
    auto* scene = engine.Worlds().Get(engine.Worlds().ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const auto entity = scene->Create();
    auto& props = scene->Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(4);
    props.GetOrAdd<glm::vec3>("samples").Vector() = {{0,0,0}, {0.01f,0,0}, {0.02f,0,0}, {10,0,0}};
    const auto id = Runtime::SelectionController::ToStableEntityId(entity);
    auto* selection = engine.Services().Find<Runtime::SelectionController>();
    ASSERT_NE(selection, nullptr);
    ASSERT_FALSE(selection->EditPrimitives(*scene, id, Runtime::GeometryElementDomain::PointCloudPoint,
        Runtime::PrimitiveSelectionEdit::All).Indices.empty());
    Runtime::EditorWorkspaceAttachment attachment;
    attachment.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
    const auto frame = Runtime::PrepareEditorPointAnalysisFrame(attachment);
    Runtime::OutlierAnalysisConfig config{
        .StableEntityId=id,
        .Positions={Runtime::GeometryElementDomain::PointCloudPoint,"samples",Geometry::PropertyValueKind::Vec3},
        .Mask={Runtime::GeometryElementDomain::PointCloudPoint,"outliers",Geometry::PropertyValueKind::UInt32},
        .Score={Runtime::GeometryElementDomain::PointCloudPoint,"scores",Geometry::PropertyValueKind::Float},
        .KNeighbors=2};
    const auto detected = Runtime::ApplyEditorOutlierAnalysisCommand(frame.Commands, config);
    ASSERT_TRUE(detected.Succeeded()) << detected.Message;
    config.Operation = Runtime::OutlierAnalysisOperation::RemoveMarked;
    const auto removed = Runtime::ApplyEditorOutlierAnalysisCommand(frame.Commands, config);
    ASSERT_EQ(removed.Status, Runtime::EditorCommandStatus::Applied) << removed.Message;
    EXPECT_EQ(props.Size(), 3u);
    EXPECT_EQ(selection->ReadPrimitives(*scene, id, Runtime::GeometryElementDomain::PointCloudPoint).Status,
              Runtime::PrimitiveSelectionStatus::Empty);
    attachment.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorSessionLifecycle, RegistrationFramesCopyResultsAndGuardExpiredCallbacks)
{
    Runtime::Engine engine{HeadlessConfig()};
    engine.Initialize();
    Runtime::EditorRegistrationPreparedFrame expired{};
    {
        Runtime::EditorWorkspaceAttachment attachment;
        EXPECT_FALSE(Runtime::PrepareEditorRegistrationFrame(attachment).Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        auto frame = Runtime::PrepareEditorRegistrationFrame(attachment);
        ASSERT_TRUE(frame.Commands.IsBound());
        ASSERT_TRUE(frame.ResultSinks.Registration);
        ASSERT_TRUE(frame.ResultSinks.DismissResult);
        frame.ResultSinks.Registration({.Status = Runtime::EditorCommandStatus::Applied, .Message = "first"});
        auto copied = Runtime::PrepareEditorRegistrationFrame(attachment);
        ASSERT_TRUE(copied.Results.LastRegistrationResult);
        frame.ResultSinks.Registration({.Status = Runtime::EditorCommandStatus::NoChange, .Message = "second"});
        EXPECT_EQ(copied.Results.LastRegistrationResult->Message, "first");
        EXPECT_EQ(Runtime::PrepareEditorRegistrationFrame(attachment).Results.LastRegistrationResult->Message, "second");
        frame.ResultSinks.DismissResult();
        EXPECT_FALSE(Runtime::PrepareEditorRegistrationFrame(attachment).Results.LastRegistrationResult);
        expired = frame;
        attachment.Detach();
        EXPECT_FALSE(Runtime::PrepareEditorRegistrationFrame(attachment).Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        auto current = Runtime::PrepareEditorRegistrationFrame(attachment);
        current.ResultSinks.Registration({.Message = "new attachment"});
        expired.ResultSinks.Registration({.Message = "old attachment"});
        expired.ResultSinks.DismissResult();
        EXPECT_EQ(Runtime::PrepareEditorRegistrationFrame(attachment).Results.LastRegistrationResult->Message,
                  "new attachment");
    }
    expired.ResultSinks.Registration({.Message = "destroyed attachment"});
    expired.ResultSinks.DismissResult();
    engine.Shutdown();
}

TEST(SandboxEditorSessionLifecycle, NormalFramesCopyResultsAndGuardExpiredCallbacks)
{
    Runtime::Engine engine{HeadlessConfig()};
    engine.Initialize();
    Runtime::EditorNormalPreparedFrame expired{};
    {
        Runtime::EditorWorkspaceAttachment attachment;
        EXPECT_FALSE(Runtime::PrepareEditorNormalFrame(attachment).Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        auto frame = Runtime::PrepareEditorNormalFrame(attachment);
        ASSERT_TRUE(frame.Commands.IsBound());
        ASSERT_TRUE(frame.ResultSinks.NormalEstimation);
        ASSERT_TRUE(frame.ResultSinks.DismissResult);
        frame.ResultSinks.NormalEstimation({.Status = Runtime::EditorCommandStatus::Applied, .Message = "first"});
        auto copied = Runtime::PrepareEditorNormalFrame(attachment);
        ASSERT_TRUE(copied.Results.LastNormalEstimationResult);
        frame.ResultSinks.NormalEstimation({.Status = Runtime::EditorCommandStatus::NoChange, .Message = "second"});
        EXPECT_EQ(copied.Results.LastNormalEstimationResult->Message, "first");
        EXPECT_EQ(Runtime::PrepareEditorNormalFrame(attachment).Results.LastNormalEstimationResult->Message, "second");
        frame.ResultSinks.DismissResult();
        EXPECT_FALSE(Runtime::PrepareEditorNormalFrame(attachment).Results.LastNormalEstimationResult);
        expired = frame;
        attachment.Detach();
        EXPECT_FALSE(Runtime::PrepareEditorNormalFrame(attachment).Commands.IsBound());
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        auto current = Runtime::PrepareEditorNormalFrame(attachment);
        current.ResultSinks.NormalEstimation({.Message = "new attachment"});
        expired.ResultSinks.NormalEstimation({.Message = "old attachment"});
        expired.ResultSinks.DismissResult();
        EXPECT_EQ(Runtime::PrepareEditorNormalFrame(attachment).Results.LastNormalEstimationResult->Message, "new attachment");
    }
    expired.ResultSinks.NormalEstimation({.Message = "destroyed attachment"});
    expired.ResultSinks.DismissResult();
    engine.Shutdown();
}

namespace
{
    class PointReadinessFrameProbe final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        std::function<void()> OnFrame{};
    private:
        void Frame(double, double) override
        {
            if (OnFrame) OnFrame();
            else Kernel().RequestExit();
        }
    };
    class EditorPointReadiness : public testing::Test
    {
    protected:
        Runtime::Engine Engine{HeadlessConfig()};
        Runtime::EditorWorkspaceAttachment Attachment{};
        ECS::Scene::Registry* Scene{};
        entt::entity Entity{};
        Runtime::EditorProcessingCommands Commands{};
        Runtime::KeypointAnalysisConfig Keypoints{};
        Runtime::OutlierAnalysisConfig Outliers{};
        Runtime::NormalEstimationConfig Normals{};
        Runtime::KernelDensityConfig Density{};
        Runtime::PointSpacingConfig Spacing{};
        Runtime::DensityWeightConfig Weights{};
        Runtime::BilateralFilterConfig Bilateral{};
        Runtime::DescriptorAnalysisConfig Descriptors{};
        Runtime::PointConstructionConfig Construction{};
        Runtime::EditorRegistrationCommand Registration{};
        entt::entity RegistrationTarget{};
        PointReadinessFrameProbe* FrameProbe{};

        void SetUp() override
        {
            FrameProbe = &Intrinsic::Tests::AddRuntimeTestModule(
                Engine, std::make_unique<PointReadinessFrameProbe>());
            Engine.EmplaceModule<Runtime::AsyncWorkModule>();
            Engine.EmplaceModule<Runtime::SceneInteractionModule>();
            Engine.EmplaceModule<Runtime::SpatialIndexCache>();
            Engine.Initialize();
            Scene = Engine.Worlds().Get(Engine.Worlds().ActiveWorld());
            Entity = Scene->Create();
            auto& props = Scene->Raw().emplace<GS::Vertices>(Entity).Properties;
            props.Resize(5);
            props.GetOrAdd<glm::vec3>("samples").Vector() =
                {{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0}, {std::numeric_limits<float>::quiet_NaN(),0,0}};
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            Keypoints.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity);
            Keypoints.Positions = {Runtime::GeometryElementDomain::PointCloudPoint, "samples", Geometry::PropertyValueKind::Vec3};
            Keypoints.Mask = {Keypoints.Positions.Domain, "keypoints", Geometry::PropertyValueKind::UInt32};
            Keypoints.Score = {Keypoints.Positions.Domain, "saliency", Geometry::PropertyValueKind::Float};
            Keypoints.MinimumNeighbors = 1;
            Outliers.StableEntityId = Keypoints.StableEntityId;
            Outliers.Positions = Keypoints.Positions;
            Outliers.Mask = {Keypoints.Positions.Domain, "outliers", Geometry::PropertyValueKind::UInt32};
            Outliers.Score = {Keypoints.Positions.Domain, "scores", Geometry::PropertyValueKind::Float};
            Outliers.KNeighbors = 2;
            Normals.StableEntityId = Keypoints.StableEntityId;
            Normals.Positions = Keypoints.Positions;
            Normals.Output = {Keypoints.Positions.Domain, "normals", Geometry::PropertyValueKind::Vec3};
            Density.StableEntityId = Spacing.StableEntityId = Weights.StableEntityId = Keypoints.StableEntityId;
            Density.Positions = Spacing.Positions = Weights.Positions = Keypoints.Positions;
            Density.Density = {Keypoints.Positions.Domain, "density", Geometry::PropertyValueKind::Float};
            Spacing.Radii = {Keypoints.Positions.Domain, "radii", Geometry::PropertyValueKind::Float};
            Weights.Weights = {Keypoints.Positions.Domain, "weights", Geometry::PropertyValueKind::Float};
            Construction.StableEntityId = Keypoints.StableEntityId;
            Construction.Positions = Keypoints.Positions;
            Construction.Method = Runtime::PointConstructionMethod::KnnGraph;
            Construction.KNeighbors = 2;
            Construction.Resolution = 6;
            Attachment.Attach(Engine.Worlds(), Engine.Services());
            PrepareFrame();
        }
        void TearDown() override
        {
            Attachment.Detach();
            Engine.Shutdown();
        }
        void PrepareFrame()
        {
            ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(Attachment, MakeNoEditorModelBuildRequest()));
            Commands = Runtime::PrepareEditorPointAnalysisFrame(Attachment).Commands;
        }
        Geometry::PropertySet& Properties() { return Scene->Raw().get<GS::Vertices>(Entity).Properties; }
        Runtime::ActionReadiness Preview() { return Runtime::PreviewEditorKeypointAnalysisCommand(Commands, Keypoints); }
        Runtime::ActionReadiness PreviewNormals()
        {
            return Runtime::PreviewEditorNormalEstimationCommand(
                Runtime::PrepareEditorNormalFrame(Attachment).Commands, Normals);
        }
        std::array<Runtime::ActionReadiness, 3> PreviewScalars()
        {
            const auto fields = Runtime::PrepareEditorPointFieldFrame(Attachment).Commands;
            return {Runtime::PreviewEditorKernelDensityCommand(fields, Density),
                    Runtime::PreviewEditorPointSpacingCommand(fields, Spacing),
                    Runtime::PreviewEditorDensityWeightCommand(Commands, Weights)};
        }
        Runtime::EditorPointInputReadinessStats Stats() { return Runtime::GetEditorPointInputReadinessStats(Commands); }
        void SetNormalInputs()
        {
            Properties().GetOrAdd<glm::vec3>("directions").Vector() =
                {{0,0,1}, {0,1,0}, {1,0,0}, {0,0,1}, {NAN,0,0}};
            Bilateral.StableEntityId = Descriptors.StableEntityId = Keypoints.StableEntityId;
            Bilateral.Positions = Descriptors.Positions = Keypoints.Positions;
            Bilateral.Normals = Descriptors.Normals =
                {Keypoints.Positions.Domain, "directions", Geometry::PropertyValueKind::Vec3};
            Bilateral.Output = {Keypoints.Positions.Domain, "filtered", Geometry::PropertyValueKind::Vec3};
            Bilateral.KNeighbors = 2;
            Descriptors.Outputs = Runtime::MakeDescriptorOutputProperties(Keypoints.Positions.Domain, "descriptor");
        }
        std::array<Runtime::ActionReadiness, 2> PreviewOriented()
        {
            return {Runtime::PreviewEditorBilateralFilterCommand(
                        Runtime::PrepareEditorPointSetFrame(Attachment).Commands, Bilateral),
                    Runtime::PreviewEditorDescriptorAnalysisCommand(Commands, Descriptors)};
        }
        Runtime::EditorPointConstructionReadiness PreviewConstruction()
        {
            return Runtime::PreviewEditorPointConstructionCommand(
                Runtime::PrepareEditorPointConstructionFrame(Attachment).Commands, Construction);
        }
        void SupplyConstructionNormals()
        {
            SetNormalInputs();
            Construction.Method = Runtime::PointConstructionMethod::Hoppe;
            Construction.EstimateNormals = false;
            Construction.Normals = Bilateral.Normals;
        }
        void SetRegistrationInputs()
        {
            Scene->Raw().emplace<ECSC::Transform::Component>(Entity);
            RegistrationTarget = Scene->Create();
            Scene->Raw().emplace<ECSC::Transform::Component>(RegistrationTarget);
            auto& props = Scene->Raw().emplace<GS::Vertices>(RegistrationTarget).Properties;
            props.Resize(5);
            props.GetOrAdd<glm::vec3>("samples").Vector() =
                {{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0}, {NAN,0,0}};
            props.GetOrAdd<glm::vec3>("directions").Vector() =
                {{0,0,1}, {0,0,1}, {0,0,1}, {0,0,1}, {NAN,0,0}};
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            Registration.SourceStableEntityId = Keypoints.StableEntityId;
            Registration.TargetStableEntityId = Runtime::SelectionController::ToStableEntityId(RegistrationTarget);
            Registration.SourcePositions = Registration.TargetPositions = Keypoints.Positions;
            Registration.TargetNormals = {Keypoints.Positions.Domain, "directions", Geometry::PropertyValueKind::Vec3};
            Registration.Variant = Runtime::EditorICPVariant::PointToPlane;
        }
        Geometry::PropertySet& RegistrationProperties()
        {
            return Scene->Raw().get<GS::Vertices>(RegistrationTarget).Properties;
        }
        Runtime::ActionReadiness PreviewRegistration()
        {
            return Runtime::PreviewEditorRegistrationCommand(Commands, Registration);
        }
        void Drain() { Engine.Commands().Drain(*Scene); }
    };
}

TEST_F(EditorPointReadiness, CatalogAndMethodsShareDeferredVerdictAcrossFrames)
{
    const auto pending = Runtime::GetEditorPointInputCatalog(Commands, Keypoints.StableEntityId);
    EXPECT_TRUE(pending.Empty());
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FALSE(Preview().Enabled);
        EXPECT_FALSE(Runtime::PreviewEditorOutlierAnalysisCommand(Commands, Outliers).Enabled);
        EXPECT_FALSE(PreviewNormals().Enabled);
        EXPECT_EQ(Stats().ChecksQueued, 1u);
        EXPECT_EQ(Stats().PropertyScans, 0u);
    }
    Drain();
    const auto ready = Runtime::GetEditorPointInputCatalog(Commands, Keypoints.StableEntityId);
    ASSERT_EQ(ready.Size(), 1u);
    EXPECT_EQ(ready.Entries.front().Ref, Keypoints.Positions);
    EXPECT_NE(ready.SourceGeneration, pending.SourceGeneration);
    for (int i = 0; i < 3; ++i)
    {
        PrepareFrame();
        EXPECT_TRUE(Preview().Enabled);
        EXPECT_TRUE(Runtime::PreviewEditorOutlierAnalysisCommand(Commands, Outliers).Enabled);
        EXPECT_TRUE(PreviewNormals().Enabled);
        EXPECT_EQ(Stats().PropertyScans, 1u);
        EXPECT_EQ(Stats().ChecksQueued, 1u);
    }
    Properties().GetOrAdd<float>("unrelated")[0] = 7;
    EXPECT_TRUE(Preview().Enabled);
    EXPECT_TRUE(PreviewNormals().Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
}

TEST_F(EditorPointReadiness, ScalarCatalogsAndPreviewsShareDeferredVerdictAcrossFrames)
{
    const auto fields = Runtime::PrepareEditorPointFieldFrame(Attachment).Commands;
    const auto densityPending = Runtime::GetEditorKernelDensityInputCatalog(fields, Density.StableEntityId);
    const auto spacingPending = Runtime::GetEditorPointSpacingInputCatalog(fields, Spacing.StableEntityId);
    EXPECT_TRUE(densityPending.Empty());
    EXPECT_TRUE(spacingPending.Empty());
    for (const auto& preview : PreviewScalars())
    {
        EXPECT_FALSE(preview.Enabled);
        EXPECT_EQ(preview.DisabledReason, "Checking live point samples. Wait for input validation.");
    }
    EXPECT_EQ(Stats().ChecksQueued, 1u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    const auto densityReady = Runtime::GetEditorKernelDensityInputCatalog(fields, Density.StableEntityId);
    const auto spacingReady = Runtime::GetEditorPointSpacingInputCatalog(fields, Spacing.StableEntityId);
    ASSERT_EQ(densityReady.Size(), 1u);
    ASSERT_EQ(spacingReady.Size(), 1u);
    EXPECT_EQ(densityReady.Entries.front().Ref, Density.Positions);
    EXPECT_EQ(spacingReady.Entries.front().Ref, Spacing.Positions);
    EXPECT_NE(densityReady.SourceGeneration, densityPending.SourceGeneration);
    EXPECT_NE(spacingReady.SourceGeneration, spacingPending.SourceGeneration);
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        for (const auto& preview : PreviewScalars()) EXPECT_TRUE(preview.Enabled) << preview.DisabledReason;
        EXPECT_TRUE(Preview().Enabled);
        EXPECT_TRUE(PreviewNormals().Enabled);
        EXPECT_EQ(Stats().ChecksQueued, 1u);
        EXPECT_EQ(Stats().PropertyScans, 1u);
    }
}

TEST_F(EditorPointReadiness, ScalarReadinessCachesNegativeInputsButRevalidatesOutputs)
{
    auto positions = Properties().Get<glm::vec3>("samples");
    auto& values = positions.Vector();
    values[0].x = std::numeric_limits<float>::infinity();
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        for (const auto& preview : PreviewScalars())
            EXPECT_EQ(preview.DisabledReason, "Live position samples must be finite.");
        EXPECT_EQ(Stats().PropertyScans, 1u);
        EXPECT_EQ(Stats().ChecksQueued, 1u);
    }
    values[0] = {0,0,0};
    positions.MarkModified();
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewScalars()) EXPECT_TRUE(preview.Enabled);
    (void)Properties().GetOrAdd<glm::vec3>("density");
    (void)Properties().GetOrAdd<glm::vec3>("radii");
    (void)Properties().GetOrAdd<glm::vec3>("weights");
    const auto invalidOutputs = PreviewScalars();
    const std::array labels{"Density", "Radii", "Weight"};
    for (unsigned i = 0; i < invalidOutputs.size(); ++i)
    {
        EXPECT_FALSE(invalidOutputs[i].Enabled);
        EXPECT_EQ(invalidOutputs[i].DisabledReason,
            std::string(labels[i]) + " outputs must be absent or count-matched properties of the configured type.");
    }
    EXPECT_EQ(Stats().PropertyScans, 2u);
    EXPECT_EQ(Stats().ChecksQueued, 2u);
}

TEST_F(EditorPointReadiness, ScalarMinimumCountsAndCatalogMembershipRemainMethodSpecific)
{
    auto deleted = Properties().Get<bool>("v:deleted");
    deleted[1] = deleted[2] = deleted[3] = true;
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    const auto one = PreviewScalars();
    EXPECT_EQ(one[0].DisabledReason, "Kernel density requires at least two live samples.");
    EXPECT_EQ(one[1].DisabledReason, "Point spacing requires at least two live samples.");
    EXPECT_TRUE(one[2].Enabled);
    auto fields = Runtime::PrepareEditorPointFieldFrame(Attachment).Commands;
    EXPECT_EQ(Runtime::GetEditorPointInputCatalog(Commands, Density.StableEntityId).Size(), 1u);
    EXPECT_TRUE(Runtime::GetEditorKernelDensityInputCatalog(fields, Density.StableEntityId).Empty());
    EXPECT_TRUE(Runtime::GetEditorPointSpacingInputCatalog(fields, Spacing.StableEntityId).Empty());
    deleted[1] = false;
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewScalars()) EXPECT_TRUE(preview.Enabled);
    EXPECT_EQ(Runtime::GetEditorKernelDensityInputCatalog(fields, Density.StableEntityId).Size(), 1u);
    EXPECT_EQ(Runtime::GetEditorPointSpacingInputCatalog(fields, Spacing.StableEntityId).Size(), 1u);
    deleted[0] = deleted[1] = true;
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    const auto zero = PreviewScalars();
    for (const auto& preview : zero) EXPECT_FALSE(preview.Enabled);
    EXPECT_EQ(zero[2].DisabledReason, "Density weights require at least one live sample.");
    EXPECT_EQ(Stats().PropertyScans, 3u);
    EXPECT_EQ(Stats().ChecksQueued, 3u);
}

TEST_F(EditorPointReadiness, ScalarBackendPredicatesFollowTheSharedInputVerdict)
{
    Density.Backend = Runtime::KernelDensityBackend::CpuLBVH;
    Spacing.Backend = Runtime::PointSpacingBackend::CpuLBVH;
    Weights.Backend = Runtime::DensityWeightBackend::CpuLBVH;
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewScalars()) ASSERT_TRUE(preview.Enabled) << preview.DisabledReason;
    Properties().Get<glm::vec3>("samples")[0].x = 2e18f;
    for (const auto& preview : PreviewScalars())
        EXPECT_EQ(preview.DisabledReason, "Checking live point samples. Wait for input validation.");
    Drain();
    for (const auto& preview : PreviewScalars())
    {
        EXPECT_FALSE(preview.Enabled);
        EXPECT_EQ(preview.DisabledReason.find("LBVH requires"), 0u);
    }
    // Catalog eligibility is independent of the chosen execution backend.
    const auto fields = Runtime::PrepareEditorPointFieldFrame(Attachment).Commands;
    EXPECT_EQ(Runtime::GetEditorKernelDensityInputCatalog(fields, Density.StableEntityId).Size(), 1u);
    EXPECT_EQ(Runtime::GetEditorPointSpacingInputCatalog(fields, Spacing.StableEntityId).Size(), 1u);
    Properties().Get<glm::vec3>("samples")[0].x = std::numeric_limits<float>::denorm_min();
    Weights.Backend = Runtime::DensityWeightBackend::VulkanLBVH;
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    const auto subnormal = PreviewScalars();
    EXPECT_TRUE(subnormal[0].Enabled);
    EXPECT_TRUE(subnormal[1].Enabled);
    EXPECT_EQ(subnormal[2].DisabledReason,
        "Vulkan density weights require normal or zero coordinate components; subnormal coordinates are unsupported.");
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, ScalarOutputMetadataFollowsPendingInputValidation)
{
    (void)Properties().GetOrAdd<glm::vec3>("density");
    (void)Properties().GetOrAdd<glm::vec3>("radii");
    (void)Properties().GetOrAdd<glm::vec3>("weights");
    for (const auto& preview : PreviewScalars())
        EXPECT_EQ(preview.DisabledReason, "Checking live point samples. Wait for input validation.");
    EXPECT_EQ(Stats().ChecksQueued, 1u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    for (const auto& preview : PreviewScalars())
    {
        EXPECT_FALSE(preview.Enabled);
        EXPECT_NE(preview.DisabledReason.find("configured type"), std::string::npos);
    }
    EXPECT_EQ(Stats().PropertyScans, 1u);
}

TEST_F(EditorPointReadiness, ScalarCommandsSubmitWhileReadinessIsPending)
{
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    const auto fields = Runtime::PrepareEditorPointFieldFrame(Attachment).Commands;
    EXPECT_EQ(Runtime::ApplyEditorKernelDensityCommand(fields, Density).Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Runtime::ApplyEditorPointSpacingCommand(fields, Spacing).Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Runtime::ApplyEditorDensityWeightCommand(Commands, Weights).Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
}

TEST_F(EditorPointReadiness, ScalarCommandsRecaptureDespiteWarmReadiness)
{
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewScalars()) ASSERT_TRUE(preview.Enabled);
    Properties().Get<glm::vec3>("samples")[0].x = std::numeric_limits<float>::quiet_NaN();
    const auto fields = Runtime::PrepareEditorPointFieldFrame(Attachment).Commands;
    const auto density = Runtime::ApplyEditorKernelDensityCommand(fields, Density);
    const auto spacing = Runtime::ApplyEditorPointSpacingCommand(fields, Spacing);
    const auto weights = Runtime::ApplyEditorDensityWeightCommand(Commands, Weights);
    EXPECT_EQ(density.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(spacing.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(weights.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(density.Message, "Live position samples must be finite.");
    EXPECT_EQ(spacing.Message, density.Message);
    EXPECT_EQ(weights.Message, density.Message);
    EXPECT_FALSE(Properties().Exists("density"));
    EXPECT_FALSE(Properties().Exists("radii"));
    EXPECT_FALSE(Properties().Exists("weights"));
    EXPECT_EQ(Stats().PropertyScans, 1u);
}

TEST_F(EditorPointReadiness, OrientedInputsShareCatalogAndScalarVerdictsAcrossFrames)
{
    SetNormalInputs();
    const auto sets = Runtime::PrepareEditorPointSetFrame(Attachment).Commands;
    for (const auto& preview : PreviewOriented())
        EXPECT_EQ(preview.DisabledReason, "Checking live point samples. Wait for input validation.");
    const auto pending = Runtime::GetEditorBilateralFilterInputCatalog(sets, Bilateral.StableEntityId);
    EXPECT_TRUE(pending.Empty());
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    const auto ready = Runtime::GetEditorBilateralFilterInputCatalog(sets, Bilateral.StableEntityId);
    EXPECT_EQ(ready.Size(), 2u);
    EXPECT_NE(ready.SourceGeneration, pending.SourceGeneration);
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        for (const auto& preview : PreviewOriented()) EXPECT_TRUE(preview.Enabled) << preview.DisabledReason;
        for (const auto& preview : PreviewScalars()) EXPECT_TRUE(preview.Enabled);
        EXPECT_TRUE(Preview().Enabled);
        EXPECT_TRUE(PreviewNormals().Enabled);
        EXPECT_EQ(Stats().ChecksQueued, 2u);
        EXPECT_EQ(Stats().PropertyScans, 2u);
    }
}

TEST_F(EditorPointReadiness, OrientedNegativeAndZeroNormalVerdictsRemainRoleSpecific)
{
    SetNormalInputs();
    auto normals = Properties().Get<glm::vec3>("directions");
    auto& values = normals.Vector();
    values[0].x = NAN;
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        for (const auto& preview : PreviewOriented())
            EXPECT_EQ(preview.DisabledReason, "Live normal samples must be finite.");
        EXPECT_EQ(Stats().ChecksQueued, 2u);
        EXPECT_EQ(Stats().PropertyScans, 2u);
    }
    values[0] = {-0.0f, 0, -0.0f};
    normals.MarkModified();
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        const auto ready = PreviewOriented();
        EXPECT_TRUE(ready[0].Enabled);
        EXPECT_EQ(ready[1].DisabledReason, "Live positions must be finite and normals finite and nonzero.");
        for (const auto& preview : PreviewScalars()) EXPECT_TRUE(preview.Enabled);
        EXPECT_EQ(Stats().PropertyScans, 3u);
    }
    values[0] = {std::numeric_limits<float>::denorm_min(), 0, 0};
    values[4] = {0, 0, 0}; // Deleted zero normals do not affect eligibility.
    normals.MarkModified();
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewOriented()) EXPECT_TRUE(preview.Enabled);
    EXPECT_EQ(Stats().PropertyScans, 4u);
}

TEST_F(EditorPointReadiness, OrientedEqualPropertiesShareOneVerdict)
{
    SetNormalInputs();
    Bilateral.Normals = Bilateral.Positions;
    Descriptors.Normals = Descriptors.Positions;
    EXPECT_FALSE(Preview().Enabled);
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
    Drain();
    EXPECT_TRUE(Preview().Enabled);
    EXPECT_TRUE(PreviewOriented()[0].Enabled);
    EXPECT_FALSE(PreviewOriented()[1].Enabled); // The position at the origin is a zero normal.
    EXPECT_EQ(Stats().PropertyScans, 1u);
}

TEST_F(EditorPointReadiness, OrientedMetadataPrecedesPendingAndMinimumCountsRemainDistinct)
{
    SetNormalInputs();
    const auto output = Bilateral.Output;
    const auto histogram = Descriptors.Outputs[0];
    Bilateral.Output = Bilateral.Normals;
    Descriptors.Outputs[0].Name = Descriptors.Normals.Name;
    auto bad = PreviewOriented();
    EXPECT_NE(bad[0].DisabledReason.find("cannot overwrite"), std::string::npos);
    EXPECT_NE(bad[1].DisabledReason.find("distinct from inputs"), std::string::npos);
    EXPECT_EQ(Stats().ChecksQueued, 0u);
    Bilateral.Output = output;
    Descriptors.Outputs[0] = histogram;
    Bilateral.Normals.Name = Descriptors.Normals.Name = "missing";
    for (const auto& preview : PreviewOriented())
        EXPECT_EQ(preview.DisabledReason, "Choose count-matched vec3 normals on the position domain.");
    EXPECT_EQ(Stats().ChecksQueued, 0u);
    Bilateral.Normals.Name = Descriptors.Normals.Name = "directions";
    auto deleted = Properties().Get<bool>("v:deleted");
    deleted[1] = deleted[2] = deleted[3] = true;
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    auto one = PreviewOriented();
    EXPECT_EQ(one[0].DisabledReason, "Bilateral filtering requires at least two live samples.");
    EXPECT_EQ(one[1].DisabledReason, "Descriptor analysis requires at least two live samples and positive spacing.");
    const auto sets = Runtime::PrepareEditorPointSetFrame(Attachment).Commands;
    EXPECT_TRUE(Runtime::GetEditorBilateralFilterInputCatalog(sets, Bilateral.StableEntityId).Empty());
    deleted[0] = true;
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    auto zero = PreviewOriented();
    EXPECT_EQ(zero[0].DisabledReason, one[0].DisabledReason);
    EXPECT_EQ(zero[1].DisabledReason, "Descriptor analysis requires live input samples.");
    EXPECT_EQ(Stats().PropertyScans, 4u);
}

TEST_F(EditorPointReadiness, OrientedBackendLimitsApplyToPositionsAndRetainFamilyGates)
{
    SetNormalInputs();
    Properties().Get<glm::vec3>("directions")[0] = {2e18f,0,1};
    Bilateral.Backend = Runtime::BilateralFilterBackend::CpuLBVH;
    Descriptors.Backend = Runtime::DescriptorAnalysisBackend::CpuLBVH;
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewOriented()) ASSERT_TRUE(preview.Enabled) << preview.DisabledReason;
    Bilateral.Backend = Runtime::BilateralFilterBackend::VulkanLBVH;
    Descriptors.Backend = Runtime::DescriptorAnalysisBackend::VulkanLBVH;
    for (const auto& preview : PreviewOriented())
    {
        EXPECT_FALSE(preview.Enabled);
        EXPECT_EQ(preview.DisabledReason.find("Vulkan"), 0u);
    }
    Bilateral.Backend = Runtime::BilateralFilterBackend::CpuLBVH;
    Descriptors.Backend = Runtime::DescriptorAnalysisBackend::CpuLBVH;
    Properties().Get<glm::vec3>("samples")[0].x = 2e18f;
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewOriented()) EXPECT_EQ(preview.DisabledReason.find("LBVH"), 0u);
    const auto sets = Runtime::PrepareEditorPointSetFrame(Attachment).Commands;
    EXPECT_EQ(Runtime::GetEditorBilateralFilterInputCatalog(sets, Bilateral.StableEntityId).Size(), 2u);
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, OrientedNormalSupersessionAndDeletionRevalidateOnlyAffectedEntries)
{
    SetNormalInputs();
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Properties().Get<glm::vec3>("directions")[0] = {1,1,0};
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 1u);
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewOriented()) EXPECT_TRUE(preview.Enabled);
    EXPECT_EQ(Stats().PropertyScans, 2u);
    EXPECT_EQ(Stats().ChecksQueued, 3u);
    Properties().Get<bool>("v:deleted")[4] = false;
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    Drain();
    for (const auto& preview : PreviewOriented())
        EXPECT_EQ(preview.DisabledReason, "Live position samples must be finite.");
    EXPECT_EQ(Stats().PropertyScans, 4u);
}

TEST_F(EditorPointReadiness, OrientedCommandsSubmitWhileReadinessIsPending)
{
    SetNormalInputs();
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    const auto sets = Runtime::PrepareEditorPointSetFrame(Attachment).Commands;
    EXPECT_EQ(Runtime::ApplyEditorBilateralFilterCommand(sets, Bilateral).Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Runtime::ApplyEditorDescriptorAnalysisCommand(Commands, Descriptors).Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Stats().PropertyScans, 0u);
}

TEST_F(EditorPointReadiness, OrientedCommandsRecaptureWarmNormals)
{
    SetNormalInputs();
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    const auto sets = Runtime::PrepareEditorPointSetFrame(Attachment).Commands;
    Drain();
    for (const auto& preview : PreviewOriented()) ASSERT_TRUE(preview.Enabled);
    Properties().Get<glm::vec3>("directions")[0].x = NAN;
    const auto bilateral = Runtime::ApplyEditorBilateralFilterCommand(sets, Bilateral);
    const auto descriptor = Runtime::ApplyEditorDescriptorAnalysisCommand(Commands, Descriptors);
    EXPECT_EQ(bilateral.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(descriptor.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(bilateral.Message, "Live normal samples must be finite.");
    EXPECT_EQ(descriptor.Message, bilateral.Message);
    EXPECT_FALSE(Properties().Exists(Bilateral.Output.Name));
    for (const auto& output : Descriptors.Outputs) EXPECT_FALSE(Properties().Exists(output.Name));
    EXPECT_EQ(Stats().PropertyScans, 2u);
}

TEST_F(EditorPointReadiness, ConstructionSharesDeferredInputAndNormalVerdictsAcrossFrames)
{
    SupplyConstructionNormals();
    EXPECT_FALSE(Preview().Enabled);
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Checking live point samples. Wait for input validation.");
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        ASSERT_TRUE(PreviewConstruction().Ready) << PreviewConstruction().Diagnostic;
        for (const auto& preview : PreviewOriented()) EXPECT_TRUE(preview.Enabled);
        EXPECT_TRUE(Preview().Enabled);
        EXPECT_EQ(Stats().ChecksQueued, 2u);
        EXPECT_EQ(Stats().PropertyScans, 2u);
    }
    Properties().GetOrAdd<float>("unrelated")[0] = 1;
    EXPECT_TRUE(PreviewConstruction().Ready);
    Construction.Normals = Construction.Positions;
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Live supplied normals must be finite and nonzero.");
    EXPECT_EQ(Stats().ChecksQueued, 2u);
}

TEST_F(EditorPointReadiness, ConstructionWithoutSuppliedNormalsOnlyRequestsPositions)
{
    Construction.Normals.Name = "missing";
    EXPECT_FALSE(PreviewConstruction().Ready);
    Construction.Method = Runtime::PointConstructionMethod::Hoppe;
    Construction.EstimateNormals = true;
    EXPECT_FALSE(PreviewConstruction().Ready);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
    Drain();
    ASSERT_TRUE(PreviewConstruction().Ready);
    Construction.Method = Runtime::PointConstructionMethod::KnnGraph;
    Construction.EstimateNormals = false;
    EXPECT_TRUE(PreviewConstruction().Ready);
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
}

TEST_F(EditorPointReadiness, ConstructionCachedNormalLengthsPreserveFamilyThresholds)
{
    SupplyConstructionNormals();
    for (const float length : {0.0f, 0x1p-27f, 0x1p-26f, 2e18f, 2e19f})
    {
        SCOPED_TRACE(length);
        Properties().Get<glm::vec3>("directions")[0] = {length,0,0};
        EXPECT_FALSE(PreviewConstruction().Ready);
        Drain();
        const bool accepted = length == 0x1p-26f || length == 2e18f;
        const auto scans = Stats().PropertyScans;
        for (unsigned frame = 0; frame < 2; ++frame)
        {
            PrepareFrame();
            const auto preview = PreviewConstruction();
            EXPECT_EQ(preview.Ready, accepted) << preview.Diagnostic;
            if (!accepted) EXPECT_EQ(preview.Diagnostic, "Live supplied normals must be finite and nonzero.");
            const auto oriented = PreviewOriented();
            EXPECT_TRUE(oriented[0].Enabled);
            EXPECT_EQ(oriented[1].Enabled, length != 0);
            EXPECT_EQ(Stats().PropertyScans, scans);
        }
    }
    EXPECT_EQ(Stats().PropertyScans, 6u);
    Properties().Get<glm::vec3>("directions")[0].x = NAN;
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Live normal samples must be finite.");
    Properties().Get<glm::vec3>("samples")[1].x = NAN;
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Live position samples must be finite.");
}

TEST_F(EditorPointReadiness, ConstructionPreservesTransformMetadataAndLiveCountGates)
{
    SupplyConstructionNormals();
    Construction.Normals.Name = "missing";
    EXPECT_EQ(PreviewConstruction().Diagnostic,
              "Hoppe reconstruction requires count-matched normals on the position domain, or CPU normal estimation.");
    EXPECT_EQ(Stats().ChecksQueued, 0u);
    Construction.Normals = Bilateral.Normals;
    auto& transform = Scene->Raw().emplace<ECS::Components::Transform::Component>(Entity);
    transform.Scale = {0,1,1};
    EXPECT_EQ(PreviewConstruction().Diagnostic, "The source hierarchy must have a finite, nonsingular world transform.");
    EXPECT_EQ(Stats().ChecksQueued, 0u);
    transform.Scale = {1,1,1};
    auto deleted = Properties().Get<bool>("v:deleted");
    deleted[1] = deleted[2] = deleted[3] = true;
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    const auto insufficient = PreviewConstruction();
    EXPECT_FALSE(insufficient.Ready);
    EXPECT_EQ(insufficient.Diagnostic,
              "Construction requires 3..1048576 live samples for Hoppe or 1..1048576 for graphs.");
    Construction.Method = Runtime::PointConstructionMethod::KnnGraph;
    EXPECT_TRUE(PreviewConstruction().Ready);
    deleted[0] = true;
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    EXPECT_EQ(PreviewConstruction().Diagnostic, insufficient.Diagnostic);
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, ConstructionRetainsBackendCoordinateGatesWithoutRescanning)
{
    Construction.Backend = Runtime::PointConstructionBackend::CpuLBVH;
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    ASSERT_TRUE(PreviewConstruction().Ready);
    Construction.Backend = Runtime::PointConstructionBackend::VulkanLBVH;
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Vulkan construction requires framed GPU queries and editor jobs.");
    Properties().Get<glm::vec3>("samples")[0].x = std::numeric_limits<float>::denorm_min();
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Checking live point samples. Wait for input validation.");
    Drain();
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Vulkan construction does not support subnormal coordinate components.");
    Construction.Backend = Runtime::PointConstructionBackend::CpuLBVH;
    EXPECT_TRUE(PreviewConstruction().Ready);
    Properties().Get<glm::vec3>("samples")[0].x = 2e18f;
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    for (auto backend : {Runtime::PointConstructionBackend::CpuReference,
                         Runtime::PointConstructionBackend::CpuLBVH,
                         Runtime::PointConstructionBackend::VulkanLBVH})
    {
        Construction.Backend = backend;
        EXPECT_EQ(PreviewConstruction().Diagnostic,
                  "Live positions must be finite and within the shared 1e18 coordinate limit.");
    }
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, ConstructionNormalSupersessionAndDeletionRefreshOnlyAffectedEntries)
{
    SupplyConstructionNormals();
    EXPECT_FALSE(PreviewConstruction().Ready);
    Properties().Get<glm::vec3>("directions")[0] = {0,0,0};
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Live supplied normals must be finite and nonzero.");
    EXPECT_EQ(Stats().ChecksQueued, 3u);
    Properties().Get<bool>("v:deleted")[0] = true;
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    EXPECT_TRUE(PreviewConstruction().Ready);
    EXPECT_EQ(Stats().PropertyScans, 4u);
}

TEST_F(EditorPointReadiness, ConstructionCommandsRecaptureDespiteWarmReadiness)
{
    SupplyConstructionNormals();
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    ASSERT_TRUE(PreviewConstruction().Ready);
    const auto commands = Runtime::PrepareEditorPointConstructionFrame(Attachment).Commands;
    for (const float length : {0.0f, 0x1p-27f, 2e19f})
    {
        SCOPED_TRACE(length);
        Properties().Get<glm::vec3>("directions")[0] = {length,0,0};
        const auto result = Runtime::ApplyEditorPointConstructionCommand(commands, Construction);
        EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
        EXPECT_EQ(result.Message, "Live supplied normals must be finite and nonzero.");
        EXPECT_TRUE(Scene->Raw().view<ECS::Components::StableId>().empty());
        EXPECT_EQ(Stats().PropertyScans, 2u);
    }
    EXPECT_FALSE(PreviewConstruction().Ready);
    Drain();
    ASSERT_FALSE(PreviewConstruction().Ready);
    Properties().Get<glm::vec3>("directions")[0] = {0,0,1};
    EXPECT_EQ(Runtime::ApplyEditorPointConstructionCommand(commands, Construction).Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, ConstructionCommandsSubmitWhileReadinessIsPending)
{
    EXPECT_FALSE(PreviewConstruction().Ready);
    const auto commands = Runtime::PrepareEditorPointConstructionFrame(Attachment).Commands;
    EXPECT_EQ(Runtime::ApplyEditorPointConstructionCommand(commands, Construction).Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Stats().PropertyScans, 0u);
}

TEST_F(EditorPointReadiness, MalformedPositionStorageIsRejectedBeforeScanning)
{
    for (const std::size_t count : {4u, 6u})
    {
        Properties().Get<glm::vec3>("samples").Vector().resize(count);
        const auto invalid = Preview();
        EXPECT_FALSE(invalid.Enabled);
        EXPECT_NE(invalid.DisabledReason.find("count-matched"), std::string::npos);
        EXPECT_TRUE(Runtime::GetEditorPointInputCatalog(Commands, Keypoints.StableEntityId).Empty());
        EXPECT_EQ(Stats().ChecksQueued, 0u);
        EXPECT_EQ(Runtime::ApplyEditorKeypointAnalysisCommand(Commands, Keypoints).Message, invalid.DisabledReason);
        Drain();
        EXPECT_EQ(Stats().PropertyScans, 0u);
    }
}

TEST_F(EditorPointReadiness, RegistrationSharesPairedVerdictsAndCatalogsAcrossFrames)
{
    SetRegistrationInputs();
    Registration.SourcePositions.Domain = Registration.TargetPositions.Domain =
        Registration.TargetNormals.Domain = Runtime::GeometryElementDomain::Unknown;
    EXPECT_FALSE(Preview().Enabled);
    const auto pending = PreviewRegistration();
    EXPECT_FALSE(pending.Enabled);
    EXPECT_NE(pending.DisabledReason.find("Checking"), std::string::npos);
    EXPECT_TRUE(Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.SourceStableEntityId).Empty());
    EXPECT_TRUE(Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.TargetStableEntityId).Empty());
    EXPECT_EQ(Stats().ChecksQueued, 3u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    for (int frame = 0; frame < 4; ++frame)
    {
        PrepareFrame();
        const auto ready = PreviewRegistration();
        EXPECT_TRUE(ready.Enabled) << ready.DisabledReason;
        EXPECT_TRUE(Preview().Enabled);
        EXPECT_EQ(Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.SourceStableEntityId).Size(), 1u);
        EXPECT_EQ(Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.TargetStableEntityId).Size(), 2u);
        EXPECT_EQ(Stats().ChecksQueued, 3u);
        EXPECT_EQ(Stats().PropertyScans, 3u);
    }
    Registration.Variant = Runtime::EditorICPVariant::PointToPoint;
    EXPECT_TRUE(PreviewRegistration().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, RegistrationNormalMetadataRejectsWithoutScanning)
{
    SetRegistrationInputs();
    EXPECT_FALSE(PreviewRegistration().Enabled);
    Drain();
    ASSERT_TRUE(PreviewRegistration().Enabled);
    const auto scans = Stats().PropertyScans;
    const auto rejects = [&]
    {
        const auto invalid = PreviewRegistration();
        EXPECT_FALSE(invalid.Enabled);
        EXPECT_NE(invalid.DisabledReason.find("Point-to-plane"), std::string::npos);
        EXPECT_EQ(Runtime::ApplyEditorRegistrationCommand(Commands, Registration).Message, invalid.DisabledReason);
        EXPECT_EQ(Stats().PropertyScans, scans);
    };
    for (const std::size_t count : {4u, 6u})
    {
        RegistrationProperties().Get<glm::vec3>("directions").Vector().resize(count);
        rejects();
        EXPECT_NE(PreviewRegistration().DisabledReason.find("one vector per target point"), std::string::npos);
        const auto catalog = Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.TargetStableEntityId);
        EXPECT_TRUE(std::ranges::none_of(catalog.Entries, [](const auto& entry) { return entry.Ref.Name == "directions"; }));
    }
    RegistrationProperties().Get<glm::vec3>("directions").Vector().resize(5, {0,0,1});
    Registration.TargetNormals.ValueKind = Geometry::PropertyValueKind::Float;
    rejects();
    Registration.TargetNormals.ValueKind = Geometry::PropertyValueKind::Vec3;
    Registration.TargetNormals.Domain = Runtime::GeometryElementDomain::MeshFace;
    rejects();
    Registration.TargetNormals.Domain = Registration.TargetPositions.Domain;
    Registration.TargetNormals.Name = "missing";
    rejects();
}

TEST_F(EditorPointReadiness, RegistrationRejectsNormalValuesAndReusesNegativeVerdicts)
{
    SetRegistrationInputs();
    EXPECT_FALSE(PreviewRegistration().Enabled);
    Drain();
    ASSERT_TRUE(PreviewRegistration().Enabled);
    for (const auto normal : {glm::vec3{NAN,0,0}, glm::vec3{0}, glm::vec3{1e-30f,0,0}})
    {
        RegistrationProperties().Get<glm::vec3>("directions")[0] = normal;
        const auto scans = Stats().PropertyScans;
        EXPECT_FALSE(PreviewRegistration().Enabled);
        Drain();
        const auto invalid = PreviewRegistration();
        EXPECT_FALSE(invalid.Enabled);
        EXPECT_NE(invalid.DisabledReason.find("Point-to-plane"), std::string::npos);
        const auto applied = Runtime::ApplyEditorRegistrationCommand(Commands, Registration);
        EXPECT_EQ(applied.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
        EXPECT_EQ(applied.Message, invalid.DisabledReason);
        EXPECT_EQ(PreviewRegistration().DisabledReason, invalid.DisabledReason);
        EXPECT_EQ(Stats().PropertyScans, scans + 1);
    }
}

TEST_F(EditorPointReadiness, RegistrationRechecksTransformedNormalsWithoutCachingTransformVerdicts)
{
    SetRegistrationInputs();
    EXPECT_FALSE(PreviewRegistration().Enabled);
    Drain();
    ASSERT_TRUE(PreviewRegistration().Enabled);
    auto& transform = Scene->Raw().get<ECSC::Transform::Component>(RegistrationTarget);
    for (const auto scale : {glm::vec3{1,1,0}, glm::vec3{1,1,1e-30f}, glm::vec3{1,1,1e30f}})
    {
        transform.Scale = scale;
        const auto invalid = PreviewRegistration();
        EXPECT_FALSE(invalid.Enabled) << scale.z;
        EXPECT_EQ(Runtime::ApplyEditorRegistrationCommand(Commands, Registration).Message, invalid.DisabledReason);
        EXPECT_EQ(Stats().PropertyScans, 3u);
    }
    transform.Scale = {-2,3,4};
    EXPECT_TRUE(PreviewRegistration().Enabled);
    Scene->Raw().remove<ECSC::Transform::Component>(RegistrationTarget);
    EXPECT_TRUE(PreviewRegistration().Enabled); // A missing target transform is identity.
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, RegistrationSupersessionAndDeletionOnlyRescanAffectedInputs)
{
    SetRegistrationInputs();
    EXPECT_FALSE(PreviewRegistration().Enabled);
    RegistrationProperties().Get<glm::vec3>("directions")[0] = {1,0,0};
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 2u);
    EXPECT_FALSE(PreviewRegistration().Enabled);
    Drain();
    ASSERT_TRUE(PreviewRegistration().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 3u);
    RegistrationProperties().Get<bool>("v:deleted")[4] = false;
    EXPECT_FALSE(PreviewRegistration().Enabled);
    Drain();
    const auto invalid = PreviewRegistration();
    EXPECT_FALSE(invalid.Enabled);
    EXPECT_EQ(invalid.DisabledReason, "Live position samples must be finite.");
    EXPECT_EQ(Stats().PropertyScans, 5u);
}

TEST_F(EditorPointReadiness, RegistrationKeepsThreeSampleCatalogGateAndTransformPrecedence)
{
    SetRegistrationInputs();
    Registration.Variant = Runtime::EditorICPVariant::PointToPoint;
    auto deleted = Properties().Get<bool>("v:deleted");
    deleted[2] = deleted[3] = true;
    EXPECT_FALSE(PreviewRegistration().Enabled);
    Drain();
    EXPECT_TRUE(Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.SourceStableEntityId).Empty());
    EXPECT_EQ(PreviewRegistration().DisabledReason, "ICP requires at least three live samples per operand.");
    EXPECT_EQ(Stats().PropertyScans, 2u);
    Scene->Raw().remove<ECSC::Transform::Component>(Entity);
    EXPECT_EQ(PreviewRegistration().DisabledReason, "ICP registration source entity has no Transform to drive.");
    EXPECT_EQ(Runtime::ApplyEditorRegistrationCommand(Commands, Registration).Status,
              Runtime::EditorCommandStatus::MissingTransform);
    Registration.TargetStableEntityId = Registration.SourceStableEntityId;
    EXPECT_NE(PreviewRegistration().DisabledReason.find("distinct"), std::string::npos);
    EXPECT_EQ(Stats().PropertyScans, 2u);
}

TEST_F(EditorPointReadiness, RegistrationCommandsRecaptureAndCanSubmitWhileReadinessIsPending)
{
    SetRegistrationInputs();
    EXPECT_FALSE(PreviewRegistration().Enabled);
    EXPECT_EQ(Runtime::ApplyEditorRegistrationCommand(Commands, Registration).Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    ASSERT_TRUE(PreviewRegistration().Enabled);
    RegistrationProperties().Get<glm::vec3>("directions")[0].x = NAN;
    const auto invalid = Runtime::ApplyEditorRegistrationCommand(Commands, Registration);
    EXPECT_EQ(invalid.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_NE(invalid.Message.find("Point-to-plane"), std::string::npos);
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, NormalReadinessReusesNegativeVerdictsAndRevalidatesOutputMetadata)
{
    Normals.Output = Normals.Positions;
    EXPECT_EQ(PreviewNormals().DisabledReason, "Normals must use a distinct output property on the input domain.");
    EXPECT_EQ(Stats().ChecksQueued, 0u);
    Normals.Output.Name = "normals";
    EXPECT_EQ(PreviewNormals().DisabledReason, "Checking live point samples. Wait for input validation.");
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    ASSERT_TRUE(PreviewNormals().Enabled);
    auto positions = Properties().Get<glm::vec3>("samples");
    auto& values = positions.Vector();
    values[0].x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(PreviewNormals().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 1u);
    Drain();
    EXPECT_EQ(PreviewNormals().DisabledReason, "Live position samples must be finite.");
    EXPECT_EQ(Preview().DisabledReason, PreviewNormals().DisabledReason);
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    values[0] = {0,0,0};
    positions.MarkModified();
    EXPECT_FALSE(PreviewNormals().Enabled);
    Drain();
    ASSERT_TRUE(PreviewNormals().Enabled);
    Properties().GetOrAdd<float>("normals")[0] = 1;
    EXPECT_EQ(PreviewNormals().DisabledReason, "Normal output must be absent or a count-matched vec3 property.");
    EXPECT_EQ(Stats().ChecksQueued, 3u);
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, NormalReadinessRetainsPcaMinimumAndBackendLimits)
{
    auto deleted = Properties().Get<bool>("v:deleted");
    deleted[2] = deleted[3] = true;
    EXPECT_FALSE(PreviewNormals().Enabled);
    Drain();
    EXPECT_EQ(PreviewNormals().DisabledReason, "Point-set PCA requires at least three live finite samples.");
    deleted[0] = deleted[1] = true;
    EXPECT_FALSE(PreviewNormals().Enabled);
    Drain();
    EXPECT_EQ(PreviewNormals().DisabledReason, "Normal estimation requires live input samples.");
    deleted[0] = deleted[1] = false;
    deleted[2] = deleted[3] = false;
    Normals.Backend = Runtime::NormalEstimationBackend::CpuLBVH;
    EXPECT_FALSE(PreviewNormals().Enabled);
    Drain();
    ASSERT_TRUE(PreviewNormals().Enabled);
    Properties().Get<glm::vec3>("samples")[0].x = 2e18f;
    EXPECT_FALSE(PreviewNormals().Enabled);
    Drain();
    EXPECT_EQ(PreviewNormals().DisabledReason,
        "CPU LBVH requires the spatial cache, at most 2^24 samples and coordinates/radius within 1e18.");
    Normals.Backend = Runtime::NormalEstimationBackend::CpuKDTree;
    EXPECT_TRUE(PreviewNormals().Enabled);
    Normals.Backend = Runtime::NormalEstimationBackend::VulkanLBVH;
    EXPECT_FALSE(PreviewNormals().Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 4u);
    EXPECT_EQ(Stats().PropertyScans, 4u);
}

TEST_F(EditorPointReadiness, UvFaceRingsCacheVerdictsAndPreserveErrorPriority)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(Scene->Raw(), Entity, mesh);
    const Runtime::EditorUvRegenerationCommand command{
        .StableEntityId = Keypoints.StableEntityId, .Atlas = {.Resolution = 64u, .Padding = 2u}};
    const auto preview = [&] { return Runtime::PreviewEditorUvRegenerationCommand(Commands, command); };
    const std::string pending = "UV regeneration cannot use the selected entity: Checking mesh face rings. Wait for input validation.";
    const std::string invalidRing = "UV regeneration cannot use the selected entity: selected mesh has a face ring that is not a valid polygon";
    EXPECT_EQ(preview().DisabledReason, pending);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    for (int frame = 0; frame < 3; ++frame) { PrepareFrame(); EXPECT_EQ(preview().DisabledReason, pending); }
    EXPECT_EQ(Stats().ChecksQueued, 1u);
    Drain();
    for (int frame = 0; frame < 3; ++frame) { PrepareFrame(); EXPECT_TRUE(preview().Enabled); }
    EXPECT_EQ(Stats().PropertyScans, 1u);

    // Point-row validation of the same position property must use a separate entry.
    Normals.Positions = {Runtime::GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
    Normals.Output.Domain = Normals.Positions.Domain;
    Normals.Method = Runtime::NormalEstimationMethod::MeshFaceWeighted;
    EXPECT_FALSE(PreviewNormals().Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    Drain();
    EXPECT_TRUE(PreviewNormals().Enabled);
    EXPECT_TRUE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 2u);

    Properties().Get<glm::vec3>("v:position")[0].x = 0.1f;
    EXPECT_TRUE(preview().Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    EXPECT_EQ(Stats().PropertyScans, 2u);

    auto& halves = Scene->Raw().get<GS::Halfedges>(Entity).Properties;
    auto next = halves.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext);
    const auto savedNext = next.Vector();
    next[0] = std::numeric_limits<std::uint32_t>::max();
    // A retained enabled preview cannot bypass current command validation.
    EXPECT_EQ(Runtime::ApplyEditorUvRegenerationCommand(Commands, command).Diagnostic, invalidRing);
    auto mask = Properties().GetOrAdd<bool>("v:deleted");
    mask.Vector().clear();
    EXPECT_EQ(preview().DisabledReason, pending);
    Drain();
    EXPECT_EQ(preview().DisabledReason, invalidRing);
    EXPECT_EQ(Runtime::ApplyEditorUvRegenerationCommand(Commands, command).Diagnostic, invalidRing);
    EXPECT_EQ(Stats().PropertyScans, 3u);
    EXPECT_EQ(Stats().ChecksQueued, 3u);

    next.Vector() = savedNext;
    EXPECT_EQ(preview().DisabledReason, pending);
    Drain();
    EXPECT_EQ(preview().DisabledReason, "UV regeneration: v:deleted must match the bound position property: v:position");
    mask.Vector().resize(Properties().Size(), false);
    EXPECT_TRUE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 4u);
    EXPECT_EQ(Stats().ChecksQueued, 4u);

    // No usable face is distinct from an invalid polygon; all consumers share the walk.
    auto face = Scene->Raw().get<GS::Faces>(Entity).Properties.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge);
    const auto savedFace = face[0];
    face[0] = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(preview().DisabledReason, pending);
    Drain();
    const auto empty = preview();
    EXPECT_FALSE(empty.Enabled);
    EXPECT_EQ(empty.DisabledReason, "UV regeneration cannot use the selected entity: selected mesh has no valid surface faces");
    EXPECT_EQ(Runtime::ApplyEditorUvRegenerationCommand(Commands, command).Diagnostic, empty.DisabledReason);
    face[0] = savedFace;
    EXPECT_EQ(preview().DisabledReason, pending);
    Drain();
    EXPECT_TRUE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 6u);

    // Closing the panel expires its entry; it cannot scan later or retain a verdict.
    PrepareFrame();
    PrepareFrame();
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 6u);
    EXPECT_EQ(preview().DisabledReason, pending);
    Drain();
    EXPECT_TRUE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 7u);

    Attachment.Detach();
    ASSERT_TRUE(Engine.Services().Withdraw<Runtime::CommandBus>(Engine.Commands()));
    Attachment.Attach(Engine.Worlds(), Engine.Services());
    PrepareFrame();
    EXPECT_EQ(preview().DisabledReason,
              "UV regeneration cannot use the selected entity: Mesh-ring readiness is unavailable. Attach an editor with a command queue.");
    EXPECT_EQ(Stats().ChecksQueued, 0u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
}

TEST_F(EditorPointReadiness, MeshTopologyReadinessSharesRingVerdictsAndKeepsMaskPriority)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(Scene->Raw(), Entity, mesh);
    const auto id = Keypoints.StableEntityId;
    const Runtime::EditorMeshDenoiseCommand denoise{.StableEntityId = id};
    const Runtime::EditorMeshRemeshCommand remesh{.StableEntityId = id};
    const Runtime::EditorMeshSubdivideCommand subdivide{.StableEntityId = id};
    const Runtime::EditorMeshSimplifyCommand simplify{.StableEntityId = id, .TargetFaces = 1u};
    const auto previews = [&] {
        return std::array{
            Runtime::PreviewEditorMeshDenoiseCommand(Commands, denoise),
            Runtime::PreviewEditorMeshRemeshCommand(Commands, remesh),
            Runtime::PreviewEditorMeshSubdivideCommand(Commands, subdivide),
            Runtime::PreviewEditorMeshSimplifyCommand(Commands, simplify)};
    };
    const std::array<std::string, 4> names{"Mesh denoise", "Mesh remesh", "Mesh subdivide", "Mesh simplify"};
    const auto expectBlocked = [&](const std::string& reason) {
        const auto states = previews();
        for (std::size_t i = 0; i < states.size(); ++i)
        {
            SCOPED_TRACE(names[i]);
            EXPECT_FALSE(states[i].Enabled);
            EXPECT_EQ(states[i].DisabledReason, names[i] + ": " + reason);
        }
    };
    const auto expectApplyBlocked = [&] {
        const auto states = previews();
        const auto check = [&](const auto& result, std::size_t i) {
            SCOPED_TRACE(names[i]);
            EXPECT_FALSE(result.Succeeded());
            EXPECT_EQ(result.Message, states[i].DisabledReason);
        };
        check(Runtime::ApplyEditorMeshDenoiseCommand(Commands, denoise), 0);
        check(Runtime::ApplyEditorMeshRemeshCommand(Commands, remesh), 1);
        check(Runtime::ApplyEditorMeshSubdivideCommand(Commands, subdivide), 2);
        check(Runtime::ApplyEditorMeshSimplifyCommand(Commands, simplify), 3);
    };
    const std::string pending = "Checking mesh face rings. Wait for input validation.";
    expectBlocked(pending);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    for (int frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        for (const auto& state : previews()) EXPECT_TRUE(state.Enabled) << state.DisabledReason;
    }
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_TRUE(Runtime::PreviewEditorUvRegenerationCommand(Commands, {.StableEntityId = id}).Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 1u);

    auto next = Scene->Raw().get<GS::Halfedges>(Entity).Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext);
    const auto savedNext = next.Vector();
    next[0] = std::numeric_limits<std::uint32_t>::max();
    // Previously enabled preview does not authorize a stale command source.
    const auto stale = Runtime::ApplyEditorMeshDenoiseCommand(Commands, denoise);
    EXPECT_FALSE(stale.Succeeded());
    EXPECT_EQ(stale.Message, "Mesh denoise: selected mesh has a face ring that is not a valid polygon");
    auto mask = Properties().GetOrAdd<bool>("v:deleted");
    mask.Vector().clear();
    expectBlocked("v:deleted must match the bound position property: v:position");
    expectApplyBlocked();
    EXPECT_EQ(Stats().ChecksQueued, 1u);
    mask.Vector().resize(Properties().Size(), false);
    expectBlocked(pending);
    Drain();
    expectBlocked("selected mesh has a face ring that is not a valid polygon");
    expectApplyBlocked();
    for (int frame = 0; frame < 3; ++frame)
    {
        PrepareFrame();
        expectBlocked("selected mesh has a face ring that is not a valid polygon");
    }
    EXPECT_EQ(Stats().PropertyScans, 2u);
    next.Vector() = savedNext;
    expectBlocked(pending);
    Drain();
    for (const auto& state : previews()) EXPECT_TRUE(state.Enabled) << state.DisabledReason;
    EXPECT_EQ(Stats().PropertyScans, 3u);

    auto face = Scene->Raw().get<GS::Faces>(Entity).Properties.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge);
    face[0] = std::numeric_limits<std::uint32_t>::max();
    expectBlocked(pending);
    Drain();
    expectBlocked("selected mesh has no valid surface faces");
    expectApplyBlocked();
    EXPECT_EQ(Stats().PropertyScans, 4u);
}

TEST_F(EditorPointReadiness, UvFaceRingsDiscardSupersededAndDetachedChecks)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(Scene->Raw(), Entity, mesh);
    const Runtime::EditorUvRegenerationCommand command{.StableEntityId = Keypoints.StableEntityId};
    const auto preview = [&] { return Runtime::PreviewEditorUvRegenerationCommand(Commands, command); };
    EXPECT_FALSE(preview().Enabled);
    auto next = Scene->Raw().get<GS::Halfedges>(Entity).Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext);
    const auto saved = next.Vector();
    next[0] = std::numeric_limits<std::uint32_t>::max();
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    EXPECT_NE(preview().DisabledReason.find("not a valid polygon"), std::string::npos);
    next.Vector() = saved;
    EXPECT_FALSE(preview().Enabled);
    Attachment.Detach();
    Attachment.Attach(Engine.Worlds(), Engine.Services());
    PrepareFrame();
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 0u);
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_TRUE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 1u);

    next[0] = std::numeric_limits<std::uint32_t>::max();
    EXPECT_FALSE(preview().Enabled);
    RequiredEngineService<Runtime::JobService>(Engine).AdvanceWorldGeneration(Engine.Worlds().ActiveWorld());
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 2u);
    EXPECT_NE(preview().DisabledReason.find("not a valid polygon"), std::string::npos);
}

TEST_F(EditorPointReadiness, NormalTopologyReadinessSharesPointVerdictButKeepsLiveTopologyChecks)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    const auto d = mesh.AddVertex({1,1,0});
    (void)mesh.AddTriangle(a,b,c);
    (void)mesh.AddTriangle(c,b,d);
    GS::PopulateFromMesh(Scene->Raw(), Entity, mesh);
    Normals.Positions = {Runtime::GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
    Normals.Output.Domain = Normals.Positions.Domain;
    Normals.Method = Runtime::NormalEstimationMethod::MeshFaceWeighted;
    EXPECT_EQ(PreviewNormals().DisabledReason, "Checking live point samples. Wait for input validation.");
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    ASSERT_TRUE(PreviewNormals().Enabled);
    auto& faces = Scene->Raw().get<GS::Faces>(Entity).Properties;
    auto& edges = Scene->Raw().get<GS::Edges>(Entity).Properties;
    faces.GetOrAdd<bool>("f:deleted")[0] = true;
    edges.GetOrAdd<bool>("e:deleted")[0] = true;
    EXPECT_TRUE(PreviewNormals().Enabled);
    Normals.Method = Runtime::NormalEstimationMethod::MeshFaceNormals;
    Normals.Output.Domain = Runtime::GeometryElementDomain::MeshFace;
    EXPECT_TRUE(PreviewNormals().Enabled);
    Normals.Method = Runtime::NormalEstimationMethod::GraphNeighborhood;
    Normals.Output.Domain = Normals.Positions.Domain;
    EXPECT_TRUE(PreviewNormals().Enabled);
    auto edgeMask = edges.Get<bool>("e:deleted");
    edges.Remove(edgeMask);
    (void)edges.GetOrAdd<float>("e:deleted");
    EXPECT_EQ(PreviewNormals().DisabledReason, "Invalid edge deletion mask.");
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
}

TEST_F(EditorPointReadiness, NormalTopologyMetadataRefreshesWithoutPointRescans)
{
    using D = Runtime::GeometryElementDomain;
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(Scene->Raw(), Entity, mesh);
    Normals.Positions = {D::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
    Normals.Output.Domain = D::MeshFace;
    Normals.Method = Runtime::NormalEstimationMethod::MeshFaceNormals;
    EXPECT_FALSE(PreviewNormals().Enabled);
    Drain();
    ASSERT_TRUE(PreviewNormals().Enabled);
    auto& faces = Scene->Raw().get<GS::Faces>(Entity).Properties;
    auto& edges = Scene->Raw().get<GS::Edges>(Entity).Properties;
    auto& halves = Scene->Raw().get<GS::Halfedges>(Entity).Properties;
    for (const auto method : {Runtime::NormalEstimationMethod::MeshFaceNormals,
                             Runtime::NormalEstimationMethod::MeshFaceWeighted,
                             Runtime::NormalEstimationMethod::GraphNeighborhood})
    {
        Normals.Method = method;
        Normals.Output.Domain = method == Runtime::NormalEstimationMethod::MeshFaceNormals ? D::MeshFace : D::MeshVertex;
        for (const bool faceMask : {false, true})
        {
            if (faceMask && method == Runtime::NormalEstimationMethod::GraphNeighborhood) continue;
            auto& props = faceMask ? faces : edges;
            const char* name = faceMask ? "f:deleted" : "e:deleted";
            if (auto mask = props.Get<bool>(name)) props.Remove(mask);
            ASSERT_TRUE(PreviewNormals().Enabled);
            auto mask = props.GetOrAdd<bool>(name);
            for (int frame = 0; frame < 3; ++frame)
            {
                mask[0] = frame % 2 == 0;
                PrepareFrame();
                EXPECT_TRUE(PreviewNormals().Enabled);
            }
            const auto reason = faceMask ? "Invalid face deletion mask." :
                (method == Runtime::NormalEstimationMethod::GraphNeighborhood ? "Invalid edge deletion mask." :
                 "Invalid mesh edge deletion mask or halfedge cardinality.");
            mask.Vector().pop_back();
            EXPECT_EQ(PreviewNormals().DisabledReason, reason);
            props.Remove(mask);
            auto wrongType = props.GetOrAdd<float>(name);
            EXPECT_EQ(PreviewNormals().DisabledReason, reason);
            props.Remove(wrongType);
            EXPECT_TRUE(PreviewNormals().Enabled);
            (void)props.GetOrAdd<bool>(name);
        }
    }
    Normals.Method = Runtime::NormalEstimationMethod::MeshFaceWeighted;
    halves.Resize(halves.Size() - 1);
    EXPECT_EQ(PreviewNormals().DisabledReason, "Invalid mesh edge deletion mask or halfedge cardinality.");
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_EQ(Stats().ChecksQueued, 1u);
}

TEST_F(EditorPointReadiness, NormalBackendReasonFollowsPendingPointValidation)
{
    Normals.Backend = Runtime::NormalEstimationBackend::VulkanLBVH;
    EXPECT_EQ(PreviewNormals().DisabledReason, "Checking live point samples. Wait for input validation.");
    Drain();
    const auto unavailable = PreviewNormals();
    EXPECT_FALSE(unavailable.Enabled);
    EXPECT_EQ(unavailable.DisabledReason,
        "Vulkan normal neighborhoods require the framed spatial cache and job service.");
}

TEST_F(EditorPointReadiness, RevisionsAndRetainedBorrowInvalidateNegativeVerdicts)
{
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    ASSERT_TRUE(Preview().Enabled);
    auto positions = Properties().Get<glm::vec3>("samples");
    auto& values = positions.Vector();
    values[0].x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 1u);
    Drain();
    const auto invalid = Preview();
    EXPECT_FALSE(invalid.Enabled);
    EXPECT_EQ(invalid.DisabledReason, "Live position samples must be finite.");
    EXPECT_EQ(Stats().PropertyScans, 2u);
    EXPECT_EQ(Preview().DisabledReason, invalid.DisabledReason);
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    values[0] = {0,0,0};
    positions.MarkModified();
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    EXPECT_TRUE(Preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 3u);
}

TEST_F(EditorPointReadiness, SupersededAndChangedBeforeDrainRequestsNeverScanOldKeys)
{
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Properties().Get<glm::vec3>("samples")[0].x = 2;
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 0u);
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Properties().Get<glm::vec3>("samples")[0].x = 3;
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    Drain();
    EXPECT_TRUE(Preview().Enabled);
    for (const auto& preview : PreviewScalars()) EXPECT_TRUE(preview.Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 3u);
    EXPECT_EQ(Stats().PropertyScans, 1u);
}

TEST_F(EditorPointReadiness, DeletionMaskAndMetadataInvalidateWithoutScanning)
{
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    ASSERT_TRUE(Preview().Enabled);
    Properties().Get<bool>("v:deleted")[4] = false;
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    EXPECT_EQ(Preview().DisabledReason, "Live position samples must be finite.");
    auto mask = Properties().Get<bool>("v:deleted");
    Properties().Remove(mask);
    (void)Properties().GetOrAdd<float>("v:deleted");
    EXPECT_EQ(Preview().DisabledReason, "Deletion mask must be a count-matched bool property.");
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    auto wrong = Properties().Get<float>("v:deleted");
    Properties().Remove(wrong);
    Properties().GetOrAdd<bool>("v:deleted")[4] = true;
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    EXPECT_TRUE(Preview().Enabled);
}

TEST_F(EditorPointReadiness, HalfedgeReadinessUsesPairedEdgeMask)
{
    SupplyConstructionNormals();
    Geometry::Graph::Graph graph;
    auto a = graph.AddVertex({0,0,0}), b = graph.AddVertex({1,0,0}), c = graph.AddVertex({0,1,0});
    (void)graph.AddEdge(a,b); (void)graph.AddEdge(b,c); (void)graph.AddEdge(c,a);
    GS::PopulateFromGraph(Scene->Raw(), Entity, graph);
    auto& halves = Scene->Raw().get<GS::Halfedges>(Entity).Properties;
    halves.GetOrAdd<glm::vec3>("samples").Vector() =
        {{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0}, {NAN,0,0}, {NAN,0,0}};
    halves.GetOrAdd<glm::vec3>("directions").Vector() =
        {{0,0,1}, {0,1,0}, {1,0,0}, {0,0,1}, {NAN,0,0}, {0,0,0}};
    Bilateral.Positions.Domain = Bilateral.Normals.Domain = Bilateral.Output.Domain =
        Descriptors.Positions.Domain = Descriptors.Normals.Domain = Runtime::GeometryElementDomain::GraphHalfedge;
    Descriptors.Outputs = Runtime::MakeDescriptorOutputProperties(Descriptors.Positions.Domain, "descriptor");
    auto& edges = Scene->Raw().get<GS::Edges>(Entity).Properties;
    edges.GetOrAdd<bool>("e:deleted")[2] = true;
    Construction.Positions.Domain = Construction.Normals.Domain = Runtime::GeometryElementDomain::GraphHalfedge;
    Keypoints.Positions.Domain = Runtime::GeometryElementDomain::GraphHalfedge;
    Keypoints.Mask.Domain = Keypoints.Score.Domain = Keypoints.Positions.Domain;
    Normals.Positions.Domain = Normals.Output.Domain = Keypoints.Positions.Domain;
    Density.Positions.Domain = Density.Density.Domain = Keypoints.Positions.Domain;
    Spacing.Positions.Domain = Spacing.Radii.Domain = Keypoints.Positions.Domain;
    Weights.Positions.Domain = Weights.Weights.Domain = Keypoints.Positions.Domain;
    for (const auto& preview : PreviewScalars()) EXPECT_FALSE(preview.Enabled);
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    EXPECT_FALSE(PreviewConstruction().Ready);
    EXPECT_FALSE(Preview().Enabled);
    EXPECT_FALSE(PreviewNormals().Enabled);
    Drain();
    for (const auto& preview : PreviewOriented()) EXPECT_TRUE(preview.Enabled);
    EXPECT_TRUE(PreviewConstruction().Ready);
    EXPECT_TRUE(Preview().Enabled);
    EXPECT_TRUE(PreviewNormals().Enabled);
    for (const auto& preview : PreviewScalars()) EXPECT_TRUE(preview.Enabled);
    edges.Get<bool>("e:deleted")[2] = false;
    for (const auto& preview : PreviewOriented()) EXPECT_FALSE(preview.Enabled);
    EXPECT_FALSE(PreviewConstruction().Ready);
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Live position samples must be finite.");
    EXPECT_EQ(Preview().DisabledReason, "Live position samples must be finite.");
    EXPECT_EQ(PreviewNormals().DisabledReason, "Live position samples must be finite.");
    for (const auto& preview : PreviewScalars())
        EXPECT_EQ(preview.DisabledReason, "Live position samples must be finite.");
    for (const auto& preview : PreviewOriented())
        EXPECT_EQ(preview.DisabledReason, "Live position samples must be finite.");
    halves.Resize(5);
    EXPECT_EQ(PreviewConstruction().Diagnostic, "Invalid deletion domain/cardinality.");
    EXPECT_EQ(Preview().DisabledReason, "Invalid deletion domain/cardinality.");
    EXPECT_EQ(PreviewNormals().DisabledReason, "Invalid deletion domain/cardinality.");
    for (const auto& preview : PreviewScalars())
        EXPECT_EQ(preview.DisabledReason, "Invalid deletion domain/cardinality.");
    for (const auto& preview : PreviewOriented())
        EXPECT_EQ(preview.DisabledReason, "Invalid deletion domain/cardinality.");
    EXPECT_EQ(Stats().PropertyScans, 4u);
}

TEST_F(EditorPointReadiness, CommandsRecaptureDespiteWarmReadiness)
{
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    ASSERT_TRUE(Preview().Enabled);
    Properties().Get<glm::vec3>("samples")[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Runtime::ApplyEditorKeypointAnalysisCommand(Commands, Keypoints).Succeeded());
    EXPECT_FALSE(Runtime::ApplyEditorOutlierAnalysisCommand(Commands, Outliers).Succeeded());
    const auto normalResult = Runtime::ApplyEditorNormalEstimationCommand(
        Runtime::PrepareEditorNormalFrame(Attachment).Commands, Normals);
    EXPECT_EQ(normalResult.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(normalResult.Message, "Live position samples must be finite.");
    EXPECT_FALSE(Properties().Exists("keypoints"));
    EXPECT_FALSE(Properties().Exists("outliers"));
    EXPECT_FALSE(Properties().Exists("normals"));
    EXPECT_EQ(Stats().PropertyScans, 1u);
}

TEST_F(EditorPointReadiness, DetachWorldEpochAndDestroyedEntityDiscardQueuedWork)
{
    EXPECT_FALSE(Preview().Enabled);
    auto old = Commands;
    Attachment.Detach();
    Drain();
    EXPECT_FALSE(old.IsBound());
    Attachment.Attach(Engine.Worlds(), Engine.Services());
    PrepareFrame();
    EXPECT_FALSE(Preview().Enabled);
    auto& jobs = RequiredEngineService<Runtime::JobService>(Engine);
    jobs.AdvanceWorldGeneration(Engine.Worlds().ActiveWorld());
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 0u);
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    EXPECT_TRUE(Preview().Enabled);
    Properties().Get<glm::vec3>("samples")[0].x = 9;
    EXPECT_FALSE(Preview().Enabled);
    Scene->Destroy(Entity);
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_FALSE(Preview().Enabled);
}

TEST_F(EditorPointReadiness, UnrequestedSourcesExpireWithoutScanning)
{
    EXPECT_FALSE(Preview().Enabled);
    PrepareFrame();
    PrepareFrame();
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 0u);
    EXPECT_FALSE(Preview().Enabled);
    Drain();
    EXPECT_TRUE(Preview().Enabled);
    EXPECT_EQ(Stats().ChecksQueued, 2u);
    EXPECT_EQ(Stats().PropertyScans, 1u);
}

TEST_F(EditorPointReadiness, LargeCatalogCompletesWithoutEntryCapStarvation)
{
    for (unsigned i = 0; i < 80; ++i)
        Properties().GetOrAdd<glm::vec3>("extra" + std::to_string(i)).Vector().assign(5, glm::vec3(1));
    EXPECT_TRUE(Runtime::GetEditorPointInputCatalog(Commands, Keypoints.StableEntityId).Empty());
    EXPECT_EQ(Stats().ChecksQueued, 81u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    EXPECT_EQ(Runtime::GetEditorPointInputCatalog(Commands, Keypoints.StableEntityId).Size(), 81u);
    PrepareFrame();
    EXPECT_EQ(Runtime::GetEditorPointInputCatalog(Commands, Keypoints.StableEntityId).Size(), 81u);
    EXPECT_EQ(Stats().ChecksQueued, 81u);
    EXPECT_EQ(Stats().PropertyScans, 81u);
}

TEST_F(EditorPointReadiness, ActiveWorldSwitchRejectsRetainedCommandsAndQueuedWork)
{
    EXPECT_FALSE(Preview().Enabled);
    auto old = Commands;
    const auto originalWorld = Engine.Worlds().ActiveWorld();
    auto world = Engine.Worlds().CreateWorld("other readiness world");
    ASSERT_TRUE(Engine.Worlds().RequestSetActiveWorld(world));
    auto& jobs = RequiredEngineService<Runtime::JobService>(Engine);
    (void)Engine.Worlds().ApplyMaintenance(Engine.Events(), jobs);
    EXPECT_FALSE(old.IsBound());
    Drain();
    // Return before preparing a new frame to inspect the unchanged old cache.
    ASSERT_TRUE(Engine.Worlds().RequestSetActiveWorld(originalWorld));
    (void)Engine.Worlds().ApplyMaintenance(Engine.Events(), jobs);
    ASSERT_TRUE(old.IsBound());
    EXPECT_EQ(Runtime::GetEditorPointInputReadinessStats(old).PropertyScans, 0u);
}

TEST_F(EditorPointReadiness, MissingCommandQueueNeverFallsBackToPanelScanning)
{
    SetNormalInputs();
    SetRegistrationInputs();
    Attachment.Detach();
    ASSERT_TRUE(Engine.Services().Withdraw<Runtime::CommandBus>(Engine.Commands()));
    Attachment.Attach(Engine.Worlds(), Engine.Services());
    PrepareFrame();
    const auto preview = Preview();
    EXPECT_FALSE(preview.Enabled);
    EXPECT_NE(preview.DisabledReason.find("command queue"), std::string::npos);
    EXPECT_FALSE(PreviewNormals().Enabled);
    EXPECT_EQ(PreviewNormals().DisabledReason, preview.DisabledReason);
    for (const auto& scalar : PreviewScalars())
    {
        EXPECT_FALSE(scalar.Enabled);
        EXPECT_EQ(scalar.DisabledReason, preview.DisabledReason);
    }
    EXPECT_TRUE(Runtime::GetEditorPointInputCatalog(Commands, Keypoints.StableEntityId).Empty());
    for (const auto& oriented : PreviewOriented())
        EXPECT_EQ(oriented.DisabledReason, preview.DisabledReason);
    EXPECT_TRUE(Runtime::GetEditorBilateralFilterInputCatalog(
        Runtime::PrepareEditorPointSetFrame(Attachment).Commands, Bilateral.StableEntityId).Empty());
    EXPECT_EQ(PreviewRegistration().DisabledReason, preview.DisabledReason);
    EXPECT_TRUE(Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.SourceStableEntityId).Empty());
    EXPECT_TRUE(Runtime::GetEditorRegistrationInputCatalog(Commands, Registration.TargetStableEntityId).Empty());
    EXPECT_EQ(Stats().ChecksQueued, 0u);
    EXPECT_EQ(Stats().PropertyScans, 0u);
}

TEST_F(EditorPointReadiness, IdleRuntimeFramesAndSelectedModelsReuseTheVerdict)
{
    Properties().GetOrAdd<glm::vec3>("v:position").Vector() =
        {{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0}, {2,2,0}};
    Keypoints.Positions.Name = "v:position";
    auto& selection = RequiredEngineService<Runtime::SelectionController>(Engine);
    ASSERT_TRUE(selection.SetSelectedByStableEntityId(*Scene, Keypoints.StableEntityId));
    ASSERT_FALSE(Preview().Enabled);
    unsigned frames = 0;
    FrameProbe->OnFrame = [&] {
        if (++frames == 4) Engine.RequestExit();
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(Attachment));
        Commands = Runtime::PrepareEditorPointAnalysisFrame(Attachment).Commands;
        EXPECT_TRUE(Preview().Enabled);
        EXPECT_EQ(Stats().ChecksQueued, 1u);
        EXPECT_EQ(Stats().PropertyScans, 1u);
    };
    Engine.Run();
    FrameProbe->OnFrame = {};
    EXPECT_EQ(frames, 4u);
}

TEST_F(EditorPointReadiness, BoundMeshFieldsReuseVerdictsAndRevalidateChangedFeatures)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(Scene->Raw(), Entity, mesh);
    using D = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    Properties().GetOrAdd<glm::vec3>("alternate").Vector() = Properties().Get<glm::vec3>("v:position").Vector();
    auto& faces = Scene->Raw().get<GS::Faces>(Entity).Properties;
    faces.GetOrAdd<std::uint64_t>("signal")[0] = 1u;
    Runtime::EditorCurvatureSegmentationCommand command{.StableEntityId = Keypoints.StableEntityId};
    command.Config.Positions = {D::MeshVertex, "alternate", K::Vec3};
    command.Config.Features = {{D::MeshFace, "signal", K::UInt64}};
    command.Config.SelectionMode = Runtime::CurvatureSegmentationSelectionMode::FixedCount;
    command.Config.FixedComponentCount = 1;
    const auto preview = [&] {
        return Runtime::PreviewEditorCurvatureSegmentationCommand(
            Runtime::PrepareEditorMeshFieldFrame(Attachment).Commands, command);
    };
    EXPECT_FALSE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 0u);
    Drain();
    EXPECT_FALSE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, 1u);
    Drain();
    ASSERT_TRUE(preview().Enabled);
    const auto readyScans = Stats().PropertyScans;
    for (int frame = 0; frame < 3; ++frame) { PrepareFrame(); EXPECT_TRUE(preview().Enabled); }
    EXPECT_EQ(Stats().PropertyScans, readyScans);
    faces.GetOrAdd<float>("unrelated")[0] = NAN;
    EXPECT_TRUE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, readyScans);
    faces.Get<std::uint64_t>("signal")[0] = (std::uint64_t{1} << 53u) + 1u;
    const auto rejected = Runtime::ApplyEditorCurvatureSegmentationCommand(
        Runtime::PrepareEditorMeshFieldFrame(Attachment).Commands, command);
    EXPECT_FALSE(rejected.Succeeded());
    EXPECT_FALSE(faces.Exists(command.Config.Components.Name));
    EXPECT_FALSE(preview().Enabled);
    EXPECT_EQ(Stats().PropertyScans, readyScans);
    Drain();
    EXPECT_NE(preview().DisabledReason.find("precision loss"), std::string::npos);
    faces.Get<std::uint64_t>("signal")[0] = 2u;
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_TRUE(preview().Enabled);
    faces.GetOrAdd<glm::vec2>("paired")[0] = {NAN, 0};
    command.Config.Features = {{D::MeshFace, "paired", K::Vec2}};
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_FALSE(preview().Enabled);
    command.Config.Features = {{D::MeshFace, "signal", K::UInt64}};
    EXPECT_TRUE(preview().Enabled);
    Properties().Get<glm::vec3>("alternate")[0].x = NAN;
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_FALSE(preview().Enabled);
}

TEST_F(EditorPointReadiness, BoundMeshFieldsDiscardSupersededAndDetachedChecks)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(Scene->Raw(), Entity, mesh);
    auto& faces = Scene->Raw().get<GS::Faces>(Entity).Properties;
    faces.GetOrAdd<double>("signal")[0] = 1.;
    Runtime::EditorCurvatureSegmentationCommand command{.StableEntityId = Keypoints.StableEntityId};
    command.Config.Features = {{Runtime::GeometryElementDomain::MeshFace, "signal", Geometry::PropertyValueKind::Double}};
    const auto preview = [&] { return Runtime::PreviewEditorCurvatureSegmentationCommand(
        Runtime::PrepareEditorMeshFieldFrame(Attachment).Commands, command); };
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_FALSE(preview().Enabled);
    faces.Get<double>("signal")[0] = NAN;
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 2u);
    EXPECT_FALSE(preview().Enabled);
    faces.Get<double>("signal")[0] = 1.;
    EXPECT_FALSE(preview().Enabled);
    const auto expired = Runtime::PrepareEditorMeshFieldFrame(Attachment);
    Attachment.Detach();
    Attachment.Attach(Engine.Worlds(), Engine.Services());
    PrepareFrame();
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 0u);
    EXPECT_FALSE(Runtime::PreviewEditorCurvatureSegmentationCommand(expired.Commands, command).Enabled);
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_FALSE(preview().Enabled);
    RequiredEngineService<Runtime::JobService>(Engine).AdvanceWorldGeneration(Engine.Worlds().ActiveWorld());
    Drain();
    EXPECT_EQ(Stats().PropertyScans, 1u);
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_FALSE(preview().Enabled);
    Drain();
    EXPECT_TRUE(preview().Enabled);
}
