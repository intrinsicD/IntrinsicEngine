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
import Extrinsic.Runtime.ParameterizationConfig;
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
    EXPECT_FALSE(Runtime::PrepareEditorGeometryProcessingFrame(attachment).Commands.IsBound());
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
    const Runtime::EditorGeometryProcessingPreparedFrame geometry =
        Runtime::PrepareEditorGeometryProcessingFrame(attachment);
    const Runtime::EditorRenderRecipeEditingPreparedFrame renderRecipe =
        Runtime::PrepareEditorRenderRecipeEditingFrame(attachment);
    EXPECT_TRUE(scene.SceneAvailable);
    EXPECT_FALSE(renderRecipe.CommandsAvailable);
    EXPECT_FALSE(geometry.ConfigCommandsAvailable);
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
    Runtime::EditorGeometryProcessingCommands staleGeometryCommands{};
    Runtime::EditorVisualizationEditingCommands staleVisualizationCommands{};
    Runtime::EditorRenderRecipeEditingCommands staleRenderRecipeCommands{};
    Runtime::EditorWorkspaceSnapshotQueries staleSnapshotQueries{};
    Runtime::EditorDocumentCommandSurface staleDocumentCommands{};
    staleSceneCommands =
        Runtime::PrepareEditorSceneEditingFrame(attachment).Commands;
    staleGeometryCommands =
        Runtime::PrepareEditorGeometryProcessingFrame(attachment).Commands;
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
            staleGeometryCommands,
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
