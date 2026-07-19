// ARCH-006 runtime Sandbox editor SessionLifecycle contract partition.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
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
#include <gtest/gtest.h>
#include <glm/gtc/quaternion.hpp>
#include "ProgressivePoissonReference.hpp"

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
import Extrinsic.ECS.Component.SpatialDebugBinding;
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
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxConfigSections;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.StreamingExecutor;
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
[[nodiscard]] Runtime::RenderArtifactDeclaration
    MakeSandboxRenderArtifact(std::string artifactId)
    {
        return Runtime::RenderArtifactDeclaration{
            .Metadata =
                Graphics::RenderArtifactMetadata{
                    .ArtifactId = std::move(artifactId),
                    .RendererId =
                        std::string{Graphics::kCurrentRendererContractId},
                    .SnapshotId = "sandbox-snapshot",
                    .ViewOutputRecipeId =
                        std::string{Graphics::kCurrentRendererDefaultViewRecipeId},
                    .SourceRevisions = {"scene:1"},
                    .Status = Graphics::RenderArtifactStatus::Available,
                    .Lifetime = Graphics::RenderArtifactLifetime::Cached,
                    .Purpose = "color",
                },
            .Kind =
                Runtime::RenderArtifactPublicationKind::CandidateProjectResult,
            .PayloadUri = "memory://sandbox-render-artifact",
            .ProducerLabel = "sandbox editor test",
        };
    }

[[nodiscard]] Runtime::SandboxEditorModelBuildRequest
    MakeNoSandboxEditorModelBuildRequest()
    {
        Runtime::SandboxEditorModelBuildRequest request{};
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

class PassiveApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

[[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }
}
TEST(SandboxEditorSession, UnattachedPrepareFrameFailsClosed)
{
    Runtime::SandboxEditorSession session;
    bool visited = false;

    EXPECT_FALSE(session.IsAttached());
    EXPECT_FALSE(session.PrepareFrame());
    EXPECT_FALSE(session.VisitPreparedFrame(
        [&visited](Runtime::SandboxEditorPreparedFrameView)
        {
            visited = true;
        }));
    EXPECT_FALSE(visited);
    EXPECT_FALSE(session.LastFrame().FileImport.Enabled);
}
TEST(SandboxEditorSession, AttachPrepareDetachBoundsPreparedFrameLifetime)
{
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<PassiveApplication>());
    engine.Initialize();

    Runtime::SandboxEditorSession session;
    session.Attach(engine);
    EXPECT_TRUE(session.IsAttached());
    EXPECT_FALSE(session.VisitPreparedFrame(
        [](Runtime::SandboxEditorPreparedFrameView)
        {
        }));
    ASSERT_TRUE(session.PrepareFrame());

    bool sawPreparedContext = false;
    ASSERT_TRUE(session.VisitPreparedFrame(
        [&sawPreparedContext](Runtime::SandboxEditorPreparedFrameView frame)
        {
            sawPreparedContext = frame.Context.Scene != nullptr;
            EXPECT_FALSE(frame.Context.DerivedJobCommands.Available());
            EXPECT_FALSE(
                frame.Context.RenderRecipeCommandsAvailable);
            EXPECT_FALSE(
                frame.Context.EngineConfigCommandsAvailable);
            EXPECT_EQ(
                frame.Context.RenderRecipeRuntimeState,
                nullptr);
            EXPECT_EQ(
                frame.Context.EngineConfigControlState,
                nullptr);
            EXPECT_FALSE(
                frame.Context.PreviewRenderRecipeDocument);
            EXPECT_FALSE(
                frame.Context.ApplyRenderRecipePreview);
            EXPECT_FALSE(
                frame.Context.PreviewEngineConfigDocument);
            EXPECT_FALSE(
                frame.Context.ApplyEngineConfigHotSubset);
            frame.LastAssetImportResult =
                Runtime::SandboxEditorFileImportResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .PayloadKind = Assets::AssetPayloadKind::Mesh,
                };
            ASSERT_NE(frame.Context.RenderRecipeEditorState, nullptr);
            frame.Context.RenderRecipeEditorState->DraftRevision = 41u;
            ASSERT_NE(frame.Context.RenderArtifacts, nullptr);
            ASSERT_TRUE(frame.Context.RenderArtifacts
                            ->RegisterArtifact(
                                MakeSandboxRenderArtifact(
                                    "session-attachment-artifact"))
                            .Succeeded());
        }));
    EXPECT_TRUE(sawPreparedContext);
    EXPECT_TRUE(session.LastFrame().FileImport.Enabled);

    session.Detach();
    EXPECT_FALSE(session.IsAttached());
    EXPECT_FALSE(session.LastFrame().FileImport.Enabled);
    EXPECT_FALSE(session.VisitPreparedFrame(
        [](Runtime::SandboxEditorPreparedFrameView)
        {
        }));
    EXPECT_FALSE(session.PrepareFrame());

    session.Attach(engine);
    ASSERT_TRUE(session.PrepareFrame());
    ASSERT_TRUE(session.VisitPreparedFrame(
        [](Runtime::SandboxEditorPreparedFrameView frame)
        {
            EXPECT_FALSE(frame.LastAssetImportResult.has_value());
            ASSERT_NE(frame.Context.RenderRecipeEditorState, nullptr);
            EXPECT_EQ(frame.Context.RenderRecipeEditorState->DraftRevision, 0u);
            ASSERT_NE(frame.Context.RenderArtifacts, nullptr);
            EXPECT_EQ(frame.Context.RenderArtifacts->Size(), 0u);
        }));

    session.Detach();
    engine.Shutdown();
}
TEST(SandboxEditorSession, StaleCopiedSurfacesFailAfterDetachAndReattach)
{
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<PassiveApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();

    Runtime::SandboxEditorSession session;
    session.Attach(engine);
    ASSERT_TRUE(session.PrepareFrame(MakeNoSandboxEditorModelBuildRequest()));

    std::function<void(Runtime::SandboxEditorKMeansResult)> staleResultSink{};
    std::function<Runtime::SandboxEditorFileImportResult(
        const Runtime::SandboxEditorFileImportCommand&)>
        staleImportCommand{};
    Runtime::SandboxEditorKMeansGpuCommandSurface staleGpuCommands{};
    Runtime::SandboxEditorParameterizationUvViewCommandSurface staleUvCommands{};
    Runtime::SandboxEditorDerivedJobCommandSurface staleDerivedJobCommands{};
    Runtime::DerivedJobHandle submittedDerivedJob{};
    ASSERT_TRUE(session.VisitPreparedFrame(
        [&](Runtime::SandboxEditorPreparedFrameView frame)
        {
            staleResultSink = frame.Context.MethodResultSinks.KMeans;
            staleImportCommand = frame.Context.AssetImportCommands.Import;
            staleGpuCommands = frame.Context.KMeansGpuCommands;
            staleUvCommands = frame.Context.ParameterizationUvViewCommands;
            staleDerivedJobCommands = frame.Context.DerivedJobCommands;
            submittedDerivedJob = frame.Context.DerivedJobCommands.Submit(
                Runtime::DerivedJobDesc{
                    .Name = "session world-scoped derived job",
                    .Execute = []() -> Runtime::DerivedJobWorkerResult
                    {
                        return Runtime::DerivedJobOutput{
                            .PayloadToken = 17u,
                        };
                    },
                });
        }));
    ASSERT_TRUE(staleResultSink);
    ASSERT_TRUE(staleImportCommand);
    ASSERT_TRUE(staleGpuCommands.Available());
    ASSERT_TRUE(staleUvCommands.Available());
    ASSERT_TRUE(staleDerivedJobCommands.Available());
    ASSERT_TRUE(submittedDerivedJob.IsValid());
    const Runtime::DerivedJobRegistry* derivedJobs =
        engine.Services().Find<Runtime::DerivedJobRegistry>();
    ASSERT_NE(derivedJobs, nullptr);
    const std::optional<Runtime::DerivedJobSnapshot> submittedSnapshot =
        derivedJobs->Snapshot(submittedDerivedJob);
    ASSERT_TRUE(submittedSnapshot.has_value());
    EXPECT_EQ(submittedSnapshot->Scope, engine.ActiveWorld());
    const Runtime::SandboxEditorFileImportResult activeImport =
        staleImportCommand(Runtime::SandboxEditorFileImportCommand{
            .Path = "/tmp/intrinsic-session-active-command.obj",
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    EXPECT_EQ(activeImport.Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_TRUE(activeImport.Operation.IsValid());
    EXPECT_EQ(activeImport.Error, Core::ErrorCode::Success);

    session.Detach();
    session.Attach(engine);
    ASSERT_TRUE(session.PrepareFrame(MakeNoSandboxEditorModelBuildRequest()));

    std::function<void(Runtime::SandboxEditorKMeansResult)> currentResultSink{};
    Runtime::SandboxEditorKMeansGpuCommandSurface currentGpuCommands{};
    ASSERT_TRUE(session.VisitPreparedFrame(
        [&](Runtime::SandboxEditorPreparedFrameView frame)
        {
            currentResultSink = frame.Context.MethodResultSinks.KMeans;
            currentGpuCommands = frame.Context.KMeansGpuCommands;
        }));
    ASSERT_TRUE(currentResultSink);
    ASSERT_TRUE(currentGpuCommands.Available());

    EXPECT_FALSE(
        staleDerivedJobCommands
            .Submit(Runtime::DerivedJobDesc{
                .Name = "expired session derived job",
                .Execute = []() -> Runtime::DerivedJobWorkerResult
                {
                    return Runtime::DerivedJobOutput{};
                },
            })
            .IsValid());
    currentResultSink(Runtime::SandboxEditorKMeansResult{
        .Message = "current attachment result",
    });
    staleResultSink(Runtime::SandboxEditorKMeansResult{
        .Message = "stale attachment result",
    });
    const Runtime::SandboxEditorFileImportResult staleImport =
        staleImportCommand(Runtime::SandboxEditorFileImportCommand{
            .Path = "/tmp/intrinsic-session-stale-command.obj",
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    EXPECT_EQ(staleImport.Status,
              Runtime::SandboxEditorCommandStatus::AssetImportFailed);
    EXPECT_EQ(staleImport.Error, Core::ErrorCode::InvalidState);
    const Runtime::RuntimeKMeansGpuJobSubmission staleGpuSubmission =
        staleGpuCommands.Submit({});
    EXPECT_EQ(staleGpuSubmission.Status,
              Runtime::RuntimeKMeansGpuJobStatus::GpuUnavailable);
    EXPECT_FALSE(staleGpuCommands.ConsumeCompleted().has_value());
    const Runtime::SandboxEditorParameterizationUvViewState staleUvState =
        staleUvCommands.Submit(
            Runtime::SandboxEditorParameterizationUvViewRequest{
                .Enabled = true,
                .RequestToken = 91u,
                .StableEntityId = 7u,
                .Width = 320u,
                .Height = 180u,
                .View = Runtime::ParameterizationViewConfig{
                    .RenderMode =
                        Runtime::ParameterizationUvRenderMode::GpuShaded,
                    .BackgroundMode =
                        Runtime::ParameterizationUvBackgroundMode::Texture,
                },
            });
    EXPECT_EQ(
        staleUvState.Status,
        Runtime::SandboxEditorParameterizationUvViewStatus::CpuFallbackNonOperational);
    EXPECT_EQ(staleUvState.ActiveMode,
              Runtime::ParameterizationUvRenderMode::CpuLayout);
    EXPECT_EQ(staleUvState.ActiveBackground,
              Runtime::ParameterizationUvBackgroundMode::Checker);
    EXPECT_EQ(staleUvState.RequestToken, 91u);
    EXPECT_FALSE(staleUvState.GpuReady);
    EXPECT_NE(staleUvState.Message.find("attachment expired"),
              std::string::npos);

    ASSERT_TRUE(session.PrepareFrame(MakeNoSandboxEditorModelBuildRequest()));
    ASSERT_TRUE(session.VisitPreparedFrame(
        [](Runtime::SandboxEditorPreparedFrameView frame)
        {
            ASSERT_NE(frame.Context.LastKMeansResult, nullptr);
            EXPECT_EQ(frame.Context.LastKMeansResult->Message,
                      "current attachment result");
        }));

    session.Detach();
    engine.Shutdown();
}
TEST(SandboxEditorSession, ReattachObservesEqualSequenceFromDifferentEngine)
{
    Runtime::SandboxEditorSession session;
    std::uint64_t firstSequence = 0u;

    {
        Runtime::Engine firstEngine(
            HeadlessConfig(),
            std::make_unique<PassiveApplication>());
        firstEngine.Initialize();
        EXPECT_FALSE(firstEngine.GetAssetImportPipeline()
                         .ImportAssetFromPath(
                             Runtime::RuntimeAssetImportRequest{
                                 .Path =
                                     "/tmp/intrinsic-session-first-missing.obj",
                                 .PayloadKind =
                                     Assets::AssetPayloadKind::Mesh,
                             })
                         .has_value());
        const auto& event = firstEngine.GetAssetImportPipeline()
                                .GetLastAssetImportEvent();
        ASSERT_TRUE(event.has_value());
        firstSequence = event->Sequence;

        session.Attach(firstEngine);
        ASSERT_TRUE(session.PrepareFrame(
            MakeNoSandboxEditorModelBuildRequest()));
        ASSERT_TRUE(session.VisitPreparedFrame(
            [](Runtime::SandboxEditorPreparedFrameView frame)
            {
                ASSERT_NE(frame.Context.LastAssetImportResult, nullptr);
                EXPECT_EQ(frame.Context.LastAssetImportResult->PayloadKind,
                          Assets::AssetPayloadKind::Mesh);
            }));
        session.Detach();
        firstEngine.Shutdown();
    }

    {
        Runtime::Engine secondEngine(
            HeadlessConfig(),
            std::make_unique<PassiveApplication>());
        secondEngine.Initialize();
        EXPECT_FALSE(secondEngine.GetAssetImportPipeline()
                         .ImportAssetFromPath(
                             Runtime::RuntimeAssetImportRequest{
                                 .Path =
                                     "/tmp/intrinsic-session-second-missing.xyz",
                                 .PayloadKind =
                                     Assets::AssetPayloadKind::PointCloud,
                             })
                         .has_value());
        const auto& event = secondEngine.GetAssetImportPipeline()
                                .GetLastAssetImportEvent();
        ASSERT_TRUE(event.has_value());
        ASSERT_EQ(event->Sequence, firstSequence);

        session.Attach(secondEngine);
        ASSERT_TRUE(session.PrepareFrame(
            MakeNoSandboxEditorModelBuildRequest()));
        ASSERT_TRUE(session.VisitPreparedFrame(
            [](Runtime::SandboxEditorPreparedFrameView frame)
            {
                ASSERT_NE(frame.Context.LastAssetImportResult, nullptr);
                EXPECT_EQ(frame.Context.LastAssetImportResult->PayloadKind,
                          Assets::AssetPayloadKind::PointCloud);
            }));
        session.Detach();
        secondEngine.Shutdown();
    }
}
