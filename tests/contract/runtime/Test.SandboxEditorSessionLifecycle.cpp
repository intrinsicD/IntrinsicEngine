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

#include "ProgressivePoissonReference.hpp"
#include <entt/entity/entity.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Graph.Vertex.Normals;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.KMeans;
import Geometry.PointCloud.Normals;
import Geometry.Properties;
import Geometry.Smoothing;
import Geometry.UvAtlas;

#include "MockRHI.hpp"

namespace Runtime = Extrinsic::Runtime;
namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace Graphics = Extrinsic::Graphics;
namespace Plat = Extrinsic::Platform;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
namespace GN = Geometry::HalfedgeMesh::VertexNormals;
namespace GVN = Geometry::Graph::VertexNormals;
namespace PCN = Geometry::PointCloud::Normals;
namespace Smooth = Geometry::Smoothing;
namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
namespace Tests = Extrinsic::Tests;

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
