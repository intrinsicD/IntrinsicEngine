#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>
#include <gtest/gtest.h>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
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
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxEditorUi;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;

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

namespace
{
    constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool HasDiagnostic(
        const std::vector<Runtime::SandboxEditorDiagnostic>& diagnostics,
        const Runtime::SandboxEditorDiagnosticCode code)
    {
        for (const Runtime::SandboxEditorDiagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.Code == code)
                return true;
        }
        return false;
    }

    [[nodiscard]] ECS::EntityHandle MakeSelectable(
        ECS::Scene::Registry& registry,
        std::string name)
    {
        const ECS::EntityHandle entity = registry.Create();
        auto& raw = registry.Raw();
        raw.emplace<ECSC::MetaData>(entity, std::move(name));
        raw.emplace<ECSC::Transform::Component>(entity);
        raw.emplace<ECSC::Transform::WorldMatrix>(entity);
        raw.emplace<Sel::SelectableTag>(entity);
        return entity;
    }

    void AddPointCloudSource(ECS::Scene::Registry& registry,
                             const ECS::EntityHandle entity,
                             const std::size_t pointCount)
    {
        auto& vertices = registry.Raw().emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(pointCount);
        registry.Raw().emplace<G::RenderPoints>(entity);
    }

    void SetNodePositions(GS::Nodes& nodes,
                          const std::vector<glm::vec3>& positions)
    {
        nodes.Properties.Resize(positions.size());
        auto pos = nodes.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        pos.Vector() = positions;
    }

    void SetPositions(GS::Vertices& vertices,
                      const std::vector<glm::vec3>& positions)
    {
        vertices.Properties.Resize(positions.size());
        auto pos = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        pos.Vector() = positions;
    }

    void ExpectKMeansVertexProperties(Geometry::PropertySet& properties,
                                      const std::size_t expectedCount,
                                      const bool pointCloudNames)
    {
        const std::string labelName =
            pointCloudNames ? "p:kmeans_label" : "v:kmeans_label";
        const std::string colorName =
            pointCloudNames ? "p:kmeans_color" : "v:kmeans_color";

        auto labels = properties.Get<std::uint32_t>(labelName);
        auto colors = properties.Get<glm::vec4>(colorName);
        ASSERT_TRUE(labels);
        ASSERT_TRUE(colors);
        ASSERT_EQ(labels.Vector().size(), expectedCount);
        ASSERT_EQ(colors.Vector().size(), expectedCount);
        for (std::size_t i = 0u; i < expectedCount; ++i)
        {
            EXPECT_LT(labels.Vector()[i], expectedCount);
            EXPECT_FLOAT_EQ(colors.Vector()[i].w, 1.0f);
        }

        if (pointCloudNames)
        {
            EXPECT_FALSE(properties.Get<float>("v:kmeans_label_f"));
        }
        else
        {
            auto labelFloats = properties.Get<float>("v:kmeans_label_f");
            ASSERT_TRUE(labelFloats);
            ASSERT_EQ(labelFloats.Vector().size(), expectedCount);
            for (std::size_t i = 0u; i < expectedCount; ++i)
            {
                EXPECT_FLOAT_EQ(labelFloats.Vector()[i],
                                static_cast<float>(labels.Vector()[i]));
            }
        }
    }

    [[nodiscard]] const Runtime::SandboxEditorVisualizationPropertyInfo*
    FindVisualizationProperty(
        const std::vector<Runtime::SandboxEditorVisualizationPropertyInfo>& properties,
        const Runtime::SandboxEditorVisualizationPropertyDomain domain,
        const std::string& name)
    {
        for (const Runtime::SandboxEditorVisualizationPropertyInfo& property :
             properties)
        {
            if (property.Domain == domain && property.Name == name)
                return &property;
        }
        return nullptr;
    }

    void SetEdges(GS::Edges& edges,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1)
    {
        edges.Properties.Resize(v0.size());
        auto p0 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV0},
            0u);
        auto p1 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV1},
            0u);
        p0.Vector() = v0;
        p1.Vector() = v1;
    }

    void SetHalfedges(GS::Halfedges& halfedges,
                      const std::vector<std::uint32_t>& toVertex,
                      const std::vector<std::uint32_t>& next,
                      const std::vector<std::uint32_t>& face)
    {
        halfedges.Properties.Resize(toVertex.size());
        auto to = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeToVertex},
            kInvalidIndex);
        auto nx = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeNext},
            kInvalidIndex);
        auto fa = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeFace},
            kInvalidIndex);
        to.Vector() = toVertex;
        nx.Vector() = next;
        fa.Vector() = face;
    }

    void SetFaces(GS::Faces& faces,
                  const std::vector<std::uint32_t>& faceHalfedge)
    {
        faces.Properties.Resize(faceHalfedge.size());
        auto halfedge = faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kFaceHalfedge},
            kInvalidIndex);
        halfedge.Vector() = faceHalfedge;
    }

    void AddTriangleMeshSource(ECS::Scene::Registry& registry,
                               const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices,
                     {
                         {0.0f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f},
                         {0.0f, 1.0f, 0.0f},
                     });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u, 2u}, {1u, 2u, 0u});
        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        SetHalfedges(halfedges,
                     {1u, 2u, 0u, 0u, 2u, 1u},
                     {1u, 2u, 0u, 5u, 3u, 4u},
                     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        auto& faces = raw.emplace<GS::Faces>(entity);
        SetFaces(faces, {0u});
    }

    void AddGraphSource(ECS::Scene::Registry& registry,
                        const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        SetNodePositions(nodes,
                         {
                             {0.0f, 0.0f, 0.0f},
                             {1.0f, 0.0f, 0.0f},
                             {2.0f, 0.0f, 0.0f},
                         });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u}, {1u, 2u});
        raw.emplace<GS::HasGraphTopology>(entity);
        raw.emplace<G::RenderLines>(entity);
        raw.emplace<G::RenderPoints>(entity);
    }

    [[nodiscard]] std::string ReadRepositoryTextFile(
        const std::filesystem::path& relativePath)
    {
        const std::filesystem::path path =
            std::filesystem::path{ENGINE_ROOT_DIR} / relativePath;
        std::ifstream file{path};
        if (!file)
            return {};
        return std::string{std::istreambuf_iterator<char>{file},
                           std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] Runtime::SandboxEditorContext MakeContext(
        ECS::Scene::Registry& registry,
        Runtime::SelectionController& selection,
        const bool imguiAvailable = true,
        const std::optional<Runtime::PrimitiveSelectionResult>* lastPrimitive = nullptr)
    {
        return Runtime::SandboxEditorContext{
            .Scene = &registry,
            .Selection = &selection,
            .LastRefinedPrimitive = lastPrimitive,
            .ImGuiAdapterAvailable = imguiAvailable,
            .AssetImportCommandsAvailable = false,
            .CameraRenderCommandsAvailable = false,
            .VisualizationCommandsAvailable = false,
        };
    }

    class FakeWindow final : public Plat::IWindow
    {
    public:
        FakeWindow(const int width, const int height)
            : m_Extent{width, height}
            , m_Framebuffer{width, height}
        {
        }

        void PollEvents() override {}
        [[nodiscard]] bool ShouldClose() const override { return false; }
        [[nodiscard]] bool IsMinimized() const override { return false; }
        [[nodiscard]] bool WasResized() const override { return false; }
        void AcknowledgeResize() override {}
        [[nodiscard]] bool ConsumeInputActivity() override { return false; }
        [[nodiscard]] Plat::Extent2D GetWindowExtent() const override { return m_Extent; }
        [[nodiscard]] Plat::Extent2D GetFramebufferExtent() const override { return m_Framebuffer; }
        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }
        void Listen(EventCallbackFn) override {}
        [[nodiscard]] std::vector<Plat::Event> DrainEvents() override { return {}; }
        void OnUpdate() override {}
        void WaitForEventsTimeout(double) override {}
        void SetClipboardText(std::string_view text) override { m_Clipboard = std::string(text); }
        [[nodiscard]] std::string GetClipboardText() const override { return m_Clipboard; }
        void SetCursorMode(Plat::CursorMode mode) override { m_CursorMode = mode; }
        [[nodiscard]] Plat::CursorMode GetCursorMode() const override { return m_CursorMode; }

    private:
        Plat::Extent2D m_Extent{};
        Plat::Extent2D m_Framebuffer{};
        std::string m_Clipboard{};
        Plat::CursorMode m_CursorMode{Plat::CursorMode::Normal};
    };

    class OneFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}
    };

    class WaitForAssetImportEventApplication final : public Runtime::IApplication
    {
    public:
        explicit WaitForAssetImportEventApplication(std::uint32_t maxFrames)
            : m_MaxFrames(maxFrames)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_ObservedFrames;
            if (engine.GetLastAssetImportEvent().has_value() ||
                m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
            }
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }

    struct TmpFile
    {
        std::filesystem::path Path;

        TmpFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << contents;
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

    [[nodiscard]] std::optional<ECS::EntityHandle> FindFirstEntityWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        auto& raw = registry.Raw();
        std::optional<ECS::EntityHandle> found{};
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (!raw.all_of<Sel::SelectableTag>(entity))
                return;
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
                found = entity;
        });
        return found;
    }

    [[nodiscard]] std::size_t CountEntitiesWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        std::size_t count = 0u;
        auto& raw = registry.Raw();
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (!raw.all_of<Sel::SelectableTag>(entity))
                return;
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
                ++count;
        });
        return count;
    }
}

TEST(SandboxEditorUi, EmptyContextProducesDeterministicDisabledDiagnostics)
{
    const Runtime::SandboxEditorContext context{};
    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_TRUE(HasDiagnostic(frame.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingScene));
    EXPECT_TRUE(HasDiagnostic(frame.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingSelectionController));
    EXPECT_TRUE(HasDiagnostic(frame.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingImGuiAdapter));
    EXPECT_FALSE(frame.SceneFile.Enabled);
    EXPECT_TRUE(HasDiagnostic(frame.SceneFile.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::SceneFileUnavailable));
    EXPECT_FALSE(frame.FileImport.Enabled);
    EXPECT_TRUE(HasDiagnostic(frame.FileImport.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));
    EXPECT_FALSE(frame.RenderGraph.Enabled);
    EXPECT_TRUE(HasDiagnostic(frame.RenderGraph.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::RenderGraphStatsUnavailable));
    EXPECT_TRUE(HasDiagnostic(frame.Inspector.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingScene));
}

TEST(SandboxEditorUi, RenderGraphPanelModelCopiesRendererStats)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    Graphics::RenderGraphFrameStats stats{};
    stats.Compile.Succeeded = true;
    stats.Compile.PassCount = 14u;
    stats.Compile.CulledPassCount = 2u;
    stats.Compile.ResourceCount = 9u;
    stats.Compile.BarrierCount = 17u;
    stats.Compile.QueueHandoffEdgeCount = 3u;
    stats.Compile.CrossQueueTimelineEdgeCount = 4u;
    stats.Compile.CrossQueueTimelineSignalCount = 5u;
    stats.Compile.CrossQueueTimelineWaitCount = 6u;
    stats.Compile.CrossQueueOwnershipTransferCount = 7u;
    stats.Compile.TransientMemoryEstimateBytes = 4096u;
    stats.Compile.TimeMicros = 123u;
    stats.Execute.Succeeded = true;
    stats.Execute.DeviceOperational = true;
    stats.Execute.TimeMicros = 456u;
    stats.CommandRecords.Recorded = 2u;
    stats.CommandRecords.Skipped = 1u;
    stats.CommandRecords.SkippedNonOperational = 0u;
    stats.CommandRecords.SkippedUnavailable = 1u;
    stats.CommandRecords.Passes.push_back(
        Graphics::RenderGraphCommandPassStats{
            .Name = "DepthPrepass",
            .Id = Graphics::FramePassId{11u},
            .Status = Graphics::RenderCommandPassStatus::Recorded,
        });
    stats.CommandRecords.Passes.push_back(
        Graphics::RenderGraphCommandPassStats{
            .Name = "DebugViewPass",
            .Id = Graphics::FramePassId{},
            .Status = Graphics::RenderCommandPassStatus::SkippedUnavailable,
        });
    stats.AsyncComputeUtilizedFrames = 1u;
    stats.DebugDump = "passes:\\n  DepthPrepass -> DebugViewPass";
    stats.Diagnostic = "Frame graph diagnostic.";
    stats.LifecycleDiagnostic = "Renderer lifecycle diagnostic.";
    context.RenderGraphStats = &stats;

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_TRUE(frame.RenderGraph.Enabled);
    EXPECT_TRUE(frame.RenderGraph.CompileSucceeded);
    EXPECT_TRUE(frame.RenderGraph.ExecuteSucceeded);
    EXPECT_TRUE(frame.RenderGraph.DeviceOperational);
    EXPECT_EQ(frame.RenderGraph.PassCount, 14u);
    EXPECT_EQ(frame.RenderGraph.CulledPassCount, 2u);
    EXPECT_EQ(frame.RenderGraph.ResourceCount, 9u);
    EXPECT_EQ(frame.RenderGraph.BarrierCount, 17u);
    EXPECT_EQ(frame.RenderGraph.QueueHandoffEdgeCount, 3u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueTimelineEdgeCount, 4u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueTimelineSignalCount, 5u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueTimelineWaitCount, 6u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueOwnershipTransferCount, 7u);
    EXPECT_EQ(frame.RenderGraph.TransientMemoryEstimateBytes, 4096u);
    EXPECT_EQ(frame.RenderGraph.CompileTimeMicros, 123u);
    EXPECT_EQ(frame.RenderGraph.ExecuteTimeMicros, 456u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesRecorded, 2u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesSkipped, 1u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesSkippedNonOperational, 0u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesSkippedUnavailable, 1u);
    EXPECT_EQ(frame.RenderGraph.AsyncComputeUtilizedFrames, 1u);
    EXPECT_EQ(frame.RenderGraph.DebugDump,
              "passes:\\n  DepthPrepass -> DebugViewPass");
    EXPECT_EQ(frame.RenderGraph.StatusText, "Frame graph diagnostic.");
    EXPECT_EQ(frame.RenderGraph.LifecycleDiagnostic,
              "Renderer lifecycle diagnostic.");
    ASSERT_EQ(frame.RenderGraph.CommandPasses.size(), 2u);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[0].Name, "DepthPrepass");
    EXPECT_TRUE(frame.RenderGraph.CommandPasses[0].HasTypedId);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[0].TypedId, 11u);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[0].Status, "Recorded");
    EXPECT_EQ(frame.RenderGraph.CommandPasses[1].Name, "DebugViewPass");
    EXPECT_FALSE(frame.RenderGraph.CommandPasses[1].HasTypedId);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[1].Status,
              "SkippedUnavailable");
}

TEST(SandboxEditorUi, FileImportCommandRoutesThroughRuntimeOwnedSurface)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    bool commandObserved = false;
    Runtime::SandboxEditorFileImportCommand observedCommand{};
    context.AssetImportCommands =
        Runtime::SandboxEditorAssetImportCommandSurface{
            .Import =
                [&](const Runtime::SandboxEditorFileImportCommand& command)
                {
                    commandObserved = true;
                    observedCommand = command;
                    return Runtime::SandboxEditorFileImportResult{
                        .Status = Runtime::SandboxEditorCommandStatus::Applied,
                        .Asset = Assets::AssetId{7u, 2u},
                        .PayloadKind = Assets::AssetPayloadKind::ModelScene,
                        .PrimitiveEntitiesCreated = 1u,
                        .EmbeddedTextureAssetsCreated = 2u,
                        .TextureUploadRequests = 3u,
                        .MaterializedModelScene = true,
                        .Message = "Imported fake model.",
                    };
                },
        };
    context.PendingAssetImportPath = "assets/models/Duck.gltf";
    context.PendingAssetImportPayloadKind = Assets::AssetPayloadKind::Graph;

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(frame.FileImport.Enabled);
    EXPECT_EQ(frame.FileImport.PendingPath, "assets/models/Duck.gltf");
    EXPECT_EQ(frame.FileImport.PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));

    const Runtime::SandboxEditorFileImportResult result =
        Runtime::ApplySandboxEditorFileImportCommand(
            context,
            Runtime::SandboxEditorFileImportCommand{
                .Path = "assets/models/Duck.gltf",
                .PayloadKind = Assets::AssetPayloadKind::Unknown,
            });

    EXPECT_TRUE(commandObserved);
    EXPECT_EQ(observedCommand.Path, "assets/models/Duck.gltf");
    EXPECT_EQ(observedCommand.PayloadKind, Assets::AssetPayloadKind::Unknown);
    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Asset, (Assets::AssetId{7u, 2u}));
    EXPECT_EQ(result.PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_EQ(result.PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(result.EmbeddedTextureAssetsCreated, 2u);
    EXPECT_EQ(result.TextureUploadRequests, 3u);
    EXPECT_TRUE(result.MaterializedModelScene);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(result.Status),
                 "Applied");

    context.LastAssetImportResult = &result;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.FileImport.LastResult.has_value());
    EXPECT_EQ(frame.FileImport.StatusText, "Imported fake model.");
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));

    Runtime::SandboxEditorContext missingSurface =
        MakeContext(registry, selection);
    const Runtime::SandboxEditorFileImportResult missing =
        Runtime::ApplySandboxEditorFileImportCommand(
            missingSurface,
            Runtime::SandboxEditorFileImportCommand{
                .Path = "assets/models/Duck.gltf",
            });
    EXPECT_EQ(missing.Status,
              Runtime::SandboxEditorCommandStatus::MissingAssetImportCommands);
    EXPECT_EQ(missing.Error, Core::ErrorCode::InvalidState);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     missing.Status),
                 "MissingAssetImportCommands");

    const Runtime::SandboxEditorFileImportResult empty =
        Runtime::ApplySandboxEditorFileImportCommand(
            context,
            Runtime::SandboxEditorFileImportCommand{});
    EXPECT_EQ(empty.Status,
              Runtime::SandboxEditorCommandStatus::AssetImportFailed);
    EXPECT_EQ(empty.Error, Core::ErrorCode::InvalidPath);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDiagnosticCode(
                     Runtime::SandboxEditorDiagnosticCode::AssetImportFailed),
                 "AssetImportFailed");

    context.LastAssetImportResult = &empty;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));
}

TEST(SandboxEditorUi, SceneFileCommandRoutesThroughRuntimeOwnedSurface)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    bool saveObserved = false;
    bool loadObserved = false;
    bool newObserved = false;
    bool closeObserved = false;
    Runtime::SandboxEditorSceneFileCommand observedSave{};
    Runtime::SandboxEditorSceneFileCommand observedLoad{};
    context.SceneFileCommands = Runtime::SandboxEditorSceneFileCommandSurface{
        .New =
            [&]()
            {
                newObserved = true;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::New,
                    .Message = "Created fake scene.",
                };
            },
        .Save =
            [&](const Runtime::SandboxEditorSceneFileCommand& command)
            {
                saveObserved = true;
                observedSave = command;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Save,
                    .Stats = Runtime::SceneSerializationStats{
                        .Entities = 3u,
                        .MeshEntities = 1u,
                        .GraphEntities = 1u,
                        .PointCloudEntities = 1u,
                    },
                    .Message = "Saved fake scene.",
                };
            },
        .Load =
            [&](const Runtime::SandboxEditorSceneFileCommand& command)
            {
                loadObserved = true;
                observedLoad = command;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Load,
                    .Stats = Runtime::SceneSerializationStats{.Entities = 2u},
                    .Message = "Loaded fake scene.",
                };
            },
        .Close =
            [&]()
            {
                closeObserved = true;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Close,
                    .Message = "Closed fake scene.",
                };
            },
    };
    context.PendingSceneFilePath = "scene.extrinsic.json";

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(frame.SceneFile.Enabled);
    EXPECT_TRUE(frame.SceneFile.LifecycleEnabled);
    EXPECT_TRUE(frame.SceneFile.CanNew);
    EXPECT_TRUE(frame.SceneFile.CanClose);
    EXPECT_TRUE(frame.SceneFile.CanSave);
    EXPECT_TRUE(frame.SceneFile.CanOpen);
    EXPECT_TRUE(frame.SceneFile.PathEntryEnabled);
    EXPECT_FALSE(frame.SceneFile.NativeDialogsAvailable);
    EXPECT_TRUE(frame.SceneFile.FileDialogBoundaryText.find("Native file dialogs") !=
                std::string::npos);
    EXPECT_EQ(frame.SceneFile.PendingPath, "scene.extrinsic.json");
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileUnavailable));

    const Runtime::SandboxEditorSceneFileResult save =
        Runtime::ApplySandboxEditorSceneSaveCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_TRUE(saveObserved);
    EXPECT_EQ(observedSave.Path, "scene.extrinsic.json");
    EXPECT_TRUE(save.Succeeded());
    EXPECT_EQ(save.Stats.Entities, 3u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(save.Status),
                 "Applied");

    const Runtime::SandboxEditorSceneFileResult created =
        Runtime::ApplySandboxEditorNewSceneCommand(context);
    EXPECT_TRUE(newObserved);
    EXPECT_TRUE(created.Succeeded());
    EXPECT_EQ(created.Operation, Runtime::SandboxEditorSceneFileOperation::New);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     Runtime::SandboxEditorCommandStatus::SceneNewFailed),
                 "SceneNewFailed");

    const Runtime::SandboxEditorSceneFileResult load =
        Runtime::ApplySandboxEditorSceneLoadCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_TRUE(loadObserved);
    EXPECT_EQ(observedLoad.Path, "scene.extrinsic.json");
    EXPECT_TRUE(load.Succeeded());
    EXPECT_EQ(load.Stats.Entities, 2u);

    const Runtime::SandboxEditorSceneFileResult closed =
        Runtime::ApplySandboxEditorCloseSceneCommand(context);
    EXPECT_TRUE(closeObserved);
    EXPECT_TRUE(closed.Succeeded());
    EXPECT_EQ(closed.Operation, Runtime::SandboxEditorSceneFileOperation::Close);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     Runtime::SandboxEditorCommandStatus::SceneCloseFailed),
                 "SceneCloseFailed");

    context.LastSceneFileResult = &load;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.SceneFile.LastResult.has_value());
    EXPECT_EQ(frame.SceneFile.StatusText, "Loaded fake scene.");
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileFailed));

    Runtime::SandboxEditorContext missingSurface =
        MakeContext(registry, selection);
    const Runtime::SandboxEditorSceneFileResult missing =
        Runtime::ApplySandboxEditorSceneSaveCommand(
            missingSurface,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_EQ(missing.Status,
              Runtime::SandboxEditorCommandStatus::MissingSceneFileCommands);
    EXPECT_EQ(missing.Error, Core::ErrorCode::InvalidState);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     missing.Status),
                 "MissingSceneFileCommands");

    const Runtime::SandboxEditorSceneFileResult emptySave =
        Runtime::ApplySandboxEditorSceneSaveCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{});
    EXPECT_EQ(emptySave.Status,
              Runtime::SandboxEditorCommandStatus::SceneSaveFailed);
    EXPECT_EQ(emptySave.Error, Core::ErrorCode::InvalidPath);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDiagnosticCode(
                     Runtime::SandboxEditorDiagnosticCode::SceneFileFailed),
                 "SceneFileFailed");

    context.LastSceneFileResult = &emptySave;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileFailed));
}

TEST(SandboxEditorUi, DocumentModelReportsRuntimeHistoryDirtyState)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    int value = 0;

    ASSERT_TRUE(history.Execute(
        Runtime::EditorCommandRecord{
            .Label = "Edit Value",
            .Redo = [&value]()
            {
                value = 1;
                return Runtime::EditorCommandHistoryStatus::Applied;
            },
            .Undo = [&value]()
            {
                value = 0;
                return Runtime::EditorCommandHistoryStatus::Applied;
            },
        }).Succeeded());

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_TRUE(frame.Document.HistoryAvailable);
    EXPECT_TRUE(frame.Document.Dirty);
    EXPECT_TRUE(frame.Document.CanUndo);
    EXPECT_FALSE(frame.Document.CanRedo);
    EXPECT_EQ(frame.Document.UndoLabel, "Edit Value");
    EXPECT_TRUE(frame.Document.StatusText.find("unsaved") != std::string::npos);
    EXPECT_TRUE(frame.Document.Diagnostics.empty());

    history.MarkSaved("scene.extrinsic.json");
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_FALSE(frame.Document.Dirty);
    EXPECT_TRUE(frame.Document.HasActivePath);
    EXPECT_EQ(frame.Document.ActivePath, "scene.extrinsic.json");
}

TEST(SandboxEditorUi, ExtrinsicSandboxAppStaysRuntimeOnly)
{
    const std::string sandboxModule =
        ReadRepositoryTextFile("src/app/Sandbox/Sandbox.cppm");
    const std::string sandboxMain =
        ReadRepositoryTextFile("src/app/Sandbox/main.cpp");
    const std::string sandboxCMake =
        ReadRepositoryTextFile("src/app/Sandbox/CMakeLists.txt");

    ASSERT_FALSE(sandboxModule.empty());
    ASSERT_FALSE(sandboxMain.empty());
    ASSERT_FALSE(sandboxCMake.empty());

    EXPECT_NE(sandboxModule.find("import Extrinsic.Runtime.Engine;"),
              std::string::npos);
    EXPECT_NE(sandboxModule.find("import Extrinsic.Runtime.SandboxEditorUi;"),
              std::string::npos);
    EXPECT_NE(sandboxMain.find("import Extrinsic.Runtime.Engine;"),
              std::string::npos);
    EXPECT_NE(sandboxMain.find("import Extrinsic.Sandbox;"),
              std::string::npos);

    for (const char* forbidden :
         {
             "import Extrinsic.Asset",
             "import Extrinsic.Core",
             "import Extrinsic.ECS",
             "import Extrinsic.Graphics",
             "import Extrinsic.Platform",
             "import Extrinsic.RHI",
             "import Extrinsic.Backends",
         })
    {
        EXPECT_EQ(sandboxModule.find(forbidden), std::string::npos)
            << forbidden;
        EXPECT_EQ(sandboxMain.find(forbidden), std::string::npos)
            << forbidden;
    }

    EXPECT_NE(sandboxCMake.find("target_link_libraries(ExtrinsicSandbox"),
              std::string::npos);
    EXPECT_NE(sandboxCMake.find("ExtrinsicRuntime"), std::string::npos);
    EXPECT_EQ(sandboxCMake.find("ExtrinsicGraphics"), std::string::npos);
    EXPECT_EQ(sandboxCMake.find("ExtrinsicPlatform"), std::string::npos);
    EXPECT_EQ(sandboxCMake.find("ExtrinsicRHI"), std::string::npos);
}

TEST(SandboxEditorUi, HierarchyInspectorModelReportsSelectionRenderHintsAndDomain)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle first = MakeSelectable(registry, "Cloud A");
    const ECS::EntityHandle second = MakeSelectable(registry, "Cloud B");
    registry.Raw().emplace<ECSC::StableId>(first, ECSC::StableId{1u, 2u});
    AddPointCloudSource(registry, first, 3u);
    AddPointCloudSource(registry, second, 1u);

    auto& raw = registry.Raw();
    auto& transform = raw.get<ECSC::Transform::Component>(first);
    transform.Position = glm::vec3{1.0f, 2.0f, 3.0f};
    transform.Rotation = glm::quat{0.5f, 0.5f, 0.5f, 0.5f};
    transform.Scale = glm::vec3{2.0f, 3.0f, 4.0f};
    raw.get<ECSC::Transform::WorldMatrix>(first).Matrix[3] =
        glm::vec4{7.0f, 8.0f, 9.0f, 1.0f};

    auto& surface = raw.emplace<G::RenderSurface>(first);
    surface.Domain = G::RenderSurface::SourceDomain::Face;
    auto& lines = raw.emplace<G::RenderLines>(first);
    lines.Domain = G::RenderLines::SourceDomain::Edge;
    lines.WidthSource = 2.5f;
    auto& points = raw.get<G::RenderPoints>(first);
    points.Type = G::RenderPoints::RenderType::Surfel;
    points.SizeSource = std::string{"v:radius"};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, first));

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(MakeContext(registry, selection));

    ASSERT_EQ(frame.Hierarchy.size(), 2u);
    const auto selected = std::find_if(
        frame.Hierarchy.begin(),
        frame.Hierarchy.end(),
        [first](const Runtime::SandboxEditorEntityRow& row)
        {
            return row.Entity == first;
        });
    ASSERT_NE(selected, frame.Hierarchy.end());
    EXPECT_EQ(selected->Name, "Cloud A");
    EXPECT_TRUE(selected->Selectable);
    EXPECT_TRUE(selected->Selected);
    EXPECT_TRUE(selected->HasDurableStableId);

    ASSERT_TRUE(frame.Inspector.HasEntity);
    EXPECT_EQ(frame.Inspector.Entity.Entity, first);
    EXPECT_EQ(frame.Inspector.Entity.Name, "Cloud A");
    EXPECT_TRUE(frame.Inspector.Transform.HasLocalTransform);
    EXPECT_TRUE(frame.Inspector.Transform.HasWorldTransform);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalPosition.x, 1.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalPosition.y, 2.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalPosition.z, 3.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalRotation.w, 0.5f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalScale.x, 2.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalScale.y, 3.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalScale.z, 4.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.WorldPosition.x, 7.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.WorldPosition.y, 8.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.WorldPosition.z, 9.0f);
    EXPECT_TRUE(frame.Inspector.RenderHints.HasRenderSurface);
    EXPECT_EQ(frame.Inspector.RenderHints.SurfaceDomain, "Face");
    EXPECT_TRUE(frame.Inspector.RenderHints.HasRenderLines);
    EXPECT_EQ(frame.Inspector.RenderHints.LineDomain, "Edge");
    EXPECT_TRUE(frame.Inspector.RenderHints.HasUniformLineWidth);
    EXPECT_FLOAT_EQ(frame.Inspector.RenderHints.UniformLineWidth, 2.5f);
    EXPECT_TRUE(frame.Inspector.RenderHints.HasRenderPoints);
    EXPECT_EQ(frame.Inspector.RenderHints.PointRenderType, "Surfel");
    EXPECT_TRUE(frame.Inspector.RenderHints.HasNamedPointSize);
    EXPECT_EQ(frame.Inspector.RenderHints.PointSizeName, "v:radius");
    EXPECT_EQ(frame.Inspector.Geometry.Domain, GS::Domain::PointCloud);
    EXPECT_TRUE(frame.Inspector.Geometry.Valid);
    EXPECT_EQ(frame.Inspector.Geometry.VertexCount, 3u);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        frame.Inspector.Processing.Domains,
        Runtime::SandboxEditorGeometryProcessingDomain::PointCloudPoints));
    EXPECT_FALSE(frame.Inspector.Processing.HasEditableSurfaceMesh);

    ASSERT_EQ(frame.Selection.SelectedStableIds.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedStableIds[0],
              Runtime::SelectionController::ToStableEntityId(first));
    ASSERT_EQ(frame.Selection.SelectedEntities.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedEntities[0].Name, "Cloud A");
    EXPECT_FALSE(frame.FileImport.Enabled);
}

TEST(SandboxEditorUi, GeometryProcessingSupportedDomainsMatchPromotedEditorContract)
{
    using Algorithm = Runtime::SandboxEditorGeometryProcessingAlgorithm;
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    const Domain kmeans =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::KMeans);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::MeshVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::GraphVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::PointCloudPoints));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::MeshEdges));

    const Domain smoothing =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::Smoothing);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshEdges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshHalfedges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshFaces));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::GraphVertices));

    EXPECT_TRUE(Runtime::SupportsSandboxEditorGeometryProcessingDomain(
        Algorithm::ShortestPath,
        Domain::GraphVertices));
    EXPECT_FALSE(Runtime::SupportsSandboxEditorGeometryProcessingDomain(
        Algorithm::ShortestPath,
        Domain::PointCloudPoints));
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorGeometryProcessingDomain(
                     Domain::GraphVertices),
                 "Graph Nodes");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorGeometryProcessingAlgorithm(
                     Algorithm::VectorHeat),
                 "Vector Heat Method");
}

TEST(SandboxEditorUi, GeometrySourcesReportProcessingCapabilitiesAndStableEntries)
{
    using Algorithm = Runtime::SandboxEditorGeometryProcessingAlgorithm;
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);

    const Runtime::SandboxEditorGeometryProcessingCapabilities meshCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, mesh);
    EXPECT_TRUE(meshCaps.HasEditableSurfaceMesh);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshEdges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshHalfedges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshFaces));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::GraphVertices));

    const std::vector<Runtime::SandboxEditorGeometryProcessingEntry> meshEntries =
        Runtime::ResolveSandboxEditorGeometryProcessingEntries(meshCaps);
    ASSERT_EQ(meshEntries.size(), 11u);
    EXPECT_EQ(meshEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(meshEntries[1].Algorithm, Algorithm::ShortestPath);
    EXPECT_EQ(meshEntries[2].Algorithm, Algorithm::VectorHeat);
    EXPECT_EQ(meshEntries[3].Algorithm, Algorithm::Parameterization);
    EXPECT_EQ(meshEntries[4].Algorithm, Algorithm::ConvexHull);
    EXPECT_EQ(meshEntries[5].Algorithm, Algorithm::BooleanCSG);
    EXPECT_EQ(meshEntries[6].Algorithm, Algorithm::Remeshing);
    EXPECT_EQ(meshEntries[10].Algorithm, Algorithm::Repair);

    const std::vector<Domain> meshKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, mesh);
    ASSERT_EQ(meshKMeans.size(), 1u);
    EXPECT_EQ(meshKMeans[0], Domain::MeshVertices);

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    const Runtime::SandboxEditorGeometryProcessingCapabilities graphCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, graph);
    EXPECT_FALSE(graphCaps.HasEditableSurfaceMesh);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::GraphVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::GraphEdges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::GraphHalfedges));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::MeshVertices));
    const std::vector<Runtime::SandboxEditorGeometryProcessingEntry> graphEntries =
        Runtime::ResolveSandboxEditorGeometryProcessingEntries(registry, graph);
    ASSERT_EQ(graphEntries.size(), 2u);
    EXPECT_EQ(graphEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(graphEntries[1].Algorithm, Algorithm::ShortestPath);
    const std::vector<Domain> graphKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, graph);
    ASSERT_EQ(graphKMeans.size(), 1u);
    EXPECT_EQ(graphKMeans[0], Domain::GraphVertices);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 5u);
    const Runtime::SandboxEditorGeometryProcessingCapabilities cloudCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, cloud);
    EXPECT_FALSE(cloudCaps.HasEditableSurfaceMesh);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        cloudCaps.Domains,
        Domain::PointCloudPoints));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        cloudCaps.Domains,
        Domain::MeshVertices));
    const std::vector<Runtime::SandboxEditorGeometryProcessingEntry> cloudEntries =
        Runtime::ResolveSandboxEditorGeometryProcessingEntries(registry, cloud);
    ASSERT_EQ(cloudEntries.size(), 8u);
    EXPECT_EQ(cloudEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(cloudEntries[1].Algorithm, Algorithm::NormalEstimation);
    EXPECT_EQ(cloudEntries[2].Algorithm, Algorithm::Registration);
    EXPECT_EQ(cloudEntries[7].Algorithm, Algorithm::SurfaceReconstruction);
    const std::vector<Domain> cloudKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, cloud);
    ASSERT_EQ(cloudKMeans.size(), 1u);
    EXPECT_EQ(cloudKMeans[0], Domain::PointCloudPoints);

    const ECS::EntityHandle empty = MakeSelectable(registry, "Empty");
    const Runtime::SandboxEditorGeometryProcessingCapabilities emptyCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, empty);
    EXPECT_FALSE(emptyCaps.HasAny());
    EXPECT_TRUE(Runtime::ResolveSandboxEditorGeometryProcessingEntries(
                    registry,
                    empty)
                    .empty());
}

TEST(SandboxEditorUi, KMeansCommandPublishesMeshGraphAndPointCloudProperties)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    const Runtime::SandboxEditorKMeansResult meshResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Domain = Domain::MeshVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 7u,
            });
    ASSERT_TRUE(meshResult.Succeeded());
    EXPECT_EQ(meshResult.Domain, Domain::MeshVertices);
    EXPECT_EQ(meshResult.LabelCount, 3u);
    EXPECT_EQ(meshResult.ClusterCount, 2u);
    EXPECT_EQ(meshResult.Error, Core::ErrorCode::Success);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(mesh).Properties,
        3u,
        false);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    const Runtime::SandboxEditorKMeansResult graphResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(graph),
                .Domain = Domain::GraphVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 11u,
                .UseHierarchicalInitialization = false,
            });
    ASSERT_TRUE(graphResult.Succeeded());
    EXPECT_EQ(graphResult.Domain, Domain::GraphVertices);
    EXPECT_EQ(graphResult.LabelCount, 3u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Nodes>(graph).Properties,
        3u,
        false);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(graph));

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });
    const Runtime::SandboxEditorKMeansResult cloudResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
            });
    ASSERT_TRUE(cloudResult.Succeeded());
    EXPECT_EQ(cloudResult.Domain, Domain::PointCloudPoints);
    EXPECT_EQ(cloudResult.LabelCount, 4u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(cloud).Properties,
        4u,
        true);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    context.LastKMeansResult = &cloudResult;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(model.Processing.LastKMeansResult.has_value());
    EXPECT_TRUE(model.Processing.LastKMeansResult->Succeeded());
    EXPECT_EQ(model.Processing.LastKMeansResult->LabelCount, 4u);
}

TEST(SandboxEditorUi, KMeansCommandFailsClosedForInvalidTargetsAndInputs)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorKMeansCommand validShape{
        .StableEntityId = 1u,
        .Domain = Domain::PointCloudPoints,
        .ClusterCount = 2u,
        .MaxIterations = 8u,
    };

    const Runtime::SandboxEditorKMeansResult missingScene =
        Runtime::ApplySandboxEditorKMeansCommand(
            Runtime::SandboxEditorContext{},
            validShape);
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::SandboxEditorKMeansResult invalidParameters =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = 1u,
                .Domain = Domain::MeshEdges,
                .ClusterCount = 0u,
                .MaxIterations = 0u,
            });
    EXPECT_EQ(invalidParameters.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(invalidParameters.Error, Core::ErrorCode::InvalidArgument);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     invalidParameters.Status),
                 "InvalidProcessingParameters");

    const Runtime::SandboxEditorKMeansResult stale =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(stale.Status, Runtime::SandboxEditorCommandStatus::StaleEntity);
    EXPECT_EQ(stale.Error, Core::ErrorCode::ResourceNotFound);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    const Runtime::SandboxEditorKMeansResult wrongDomain =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::MeshVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    const Runtime::SandboxEditorKMeansResult missingPositions =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(missingPositions.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                 });
    const Runtime::SandboxEditorKMeansResult nonFinite =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(nonFinite.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    context.LastKMeansResult = &nonFinite;
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed));
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDiagnosticCode(
                     Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed),
                 "GeometryProcessingFailed");
}

TEST(SandboxEditorUi, VisualizationModelEnumeratesPromotedGeometryProperties)
{
    using Domain = Runtime::SandboxEditorVisualizationPropertyDomain;
    using Kind = Runtime::SandboxEditorVisualizationPropertyValueKind;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "PropertyMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& meshVertices = registry.Raw().get<GS::Vertices>(mesh);
    meshVertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};
    meshVertices.Properties.GetOrAdd<glm::vec4>("v:kmeans_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}};
    meshVertices.Properties.GetOrAdd<std::uint32_t>("v:kmeans_label", 0u)
        .Vector() = {0u, 1u, 1u};
    meshVertices.Properties.GetOrAdd<float>("v:kmeans_label_f", 0.0f)
        .Vector() = {0.0f, 1.0f, 1.0f};
    meshVertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f})
        .Vector() = {glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, 1.0f}};

    auto& meshEdges = registry.Raw().get<GS::Edges>(mesh);
    meshEdges.Properties.GetOrAdd<double>("e:weight", 0.0)
        .Vector() = {1.0, 2.0, 3.0};
    meshEdges.Properties.GetOrAdd<glm::vec4>("e:debug_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{1.0f}, glm::vec4{1.0f}};

    auto& meshFaces = registry.Raw().get<GS::Faces>(mesh);
    meshFaces.Properties.GetOrAdd<float>("f:curvature", 0.0f)
        .Vector() = {0.25f};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorDomainWindowModel meshModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    const auto& properties = meshModel.Visualization.Properties;

    EXPECT_EQ(FindVisualizationProperty(properties, Domain::MeshVertices, "v:position"),
              nullptr);
    ASSERT_NE(FindVisualizationProperty(properties, Domain::MeshVertices, "v:temperature"),
              nullptr);
    const auto* scalar =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:temperature");
    EXPECT_EQ(scalar->ValueKind, Kind::ScalarFloat);
    EXPECT_TRUE(scalar->ScalarPresetAvailable);
    EXPECT_TRUE(scalar->IsolinePresetAvailable);
    EXPECT_FALSE(scalar->ColorBufferPresetAvailable);

    const auto* color =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:kmeans_color");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->ValueKind, Kind::Vec4);
    EXPECT_TRUE(color->ColorBufferPresetAvailable);
    EXPECT_FALSE(color->ScalarPresetAvailable);

    const auto* label =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:kmeans_label");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->ValueKind, Kind::UInt32);
    EXPECT_FALSE(label->ScalarPresetAvailable);
    EXPECT_FALSE(label->ColorBufferPresetAvailable);

    const auto* normal =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:normal");
    ASSERT_NE(normal, nullptr);
    EXPECT_TRUE(normal->VectorFieldCandidate);
    EXPECT_FALSE(normal->ColorBufferPresetAvailable);

    const auto* edgeWeight =
        FindVisualizationProperty(properties, Domain::MeshEdges, "e:weight");
    ASSERT_NE(edgeWeight, nullptr);
    EXPECT_EQ(edgeWeight->ValueKind, Kind::ScalarDouble);
    EXPECT_TRUE(edgeWeight->ScalarPresetAvailable);
    EXPECT_EQ(FindVisualizationProperty(properties,
                                        Domain::MeshEdges,
                                        std::string{GS::PropertyNames::kEdgeV0}),
              nullptr);
    EXPECT_EQ(FindVisualizationProperty(properties,
                                        Domain::MeshEdges,
                                        std::string{GS::PropertyNames::kEdgeV1}),
              nullptr);

    const auto* faceScalar =
        FindVisualizationProperty(properties, Domain::MeshFaces, "f:curvature");
    ASSERT_NE(faceScalar, nullptr);
    EXPECT_EQ(faceScalar->ElementCount, 1u);

    const ECS::EntityHandle graph = MakeSelectable(registry, "PropertyGraph");
    AddGraphSource(registry, graph);
    auto& graphNodes = registry.Raw().get<GS::Nodes>(graph);
    graphNodes.Properties.GetOrAdd<float>("v:centrality", 0.0f)
        .Vector() = {0.0f, 1.0f, 2.0f};
    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_NE(FindVisualizationProperty(graphModel.Visualization.Properties,
                                        Domain::GraphVertices,
                                        "v:centrality"),
              nullptr);
    EXPECT_EQ(FindVisualizationProperty(graphModel.Visualization.Properties,
                                        Domain::GraphEdges,
                                        std::string{GS::PropertyNames::kEdgeV0}),
              nullptr);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PropertyCloud");
    AddPointCloudSource(registry, cloud, 2u);
    auto& cloudVertices = registry.Raw().get<GS::Vertices>(cloud);
    cloudVertices.Properties.GetOrAdd<glm::vec4>("p:kmeans_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}};
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel cloudModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_NE(FindVisualizationProperty(cloudModel.Visualization.Properties,
                                        Domain::PointCloudPoints,
                                        "p:kmeans_color"),
              nullptr);

    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationPropertyDomain(
                     Domain::PointCloudPoints),
                 "PointCloudPoints");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationPropertyValueKind(
                     Kind::Vec4),
                 "Vec4");
}

TEST(SandboxEditorUi, VisualizationPropertyPresetCommandRoutesThroughConfig)
{
    using Domain = Runtime::SandboxEditorVisualizationPropertyDomain;
    using Preset = Runtime::SandboxEditorVisualizationPropertyPreset;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "PresetMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};
    vertices.Properties.GetOrAdd<glm::vec4>("v:kmeans_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}};
    vertices.Properties.GetOrAdd<std::uint32_t>("v:kmeans_label", 0u)
        .Vector() = {0u, 1u, 1u};
    auto& edges = registry.Raw().get<GS::Edges>(mesh);
    edges.Properties.GetOrAdd<glm::vec4>("e:debug_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{1.0f}, glm::vec4{1.0f}};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                      .ScalarAutoRange = false,
                      .ScalarRangeMin = -1.0f,
                      .ScalarRangeMax = 2.0f,
                      .ScalarBinCount = 8u,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
    const auto& scalar = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(scalar.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(scalar.ScalarFieldName, "v:temperature");
    EXPECT_EQ(scalar.ScalarDomain, G::VisualizationConfig::Domain::Vertex);
    EXPECT_FALSE(scalar.Scalar.AutoRange);
    EXPECT_FLOAT_EQ(scalar.Scalar.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(scalar.Scalar.RangeMax, 2.0f);
    EXPECT_EQ(scalar.Scalar.BinCount, 8u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                      .ScalarAutoRange = false,
                      .ScalarRangeMin = -1.0f,
                      .ScalarRangeMax = 2.0f,
                      .ScalarBinCount = 8u,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Isoline,
                      .PropertyName = "v:temperature",
                      .IsolineCount = 6u,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    const auto& isoline = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(isoline.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(isoline.ScalarFieldName, "v:temperature");
    EXPECT_EQ(isoline.Scalar.Isolines.Num, 6u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::ColorBuffer,
                      .PropertyName = "v:kmeans_color",
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    const auto& vertexColor = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(vertexColor.Source,
              G::VisualizationConfig::ColorSource::PerVertexBuffer);
    EXPECT_EQ(vertexColor.ColorBufferName, "v:kmeans_color");

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshEdges,
                      .Preset = Preset::ColorBuffer,
                      .PropertyName = "e:debug_color",
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    const auto& edgeColor = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(edgeColor.Source,
              G::VisualizationConfig::ColorSource::PerEdgeBuffer);
    EXPECT_EQ(edgeColor.ColorBufferName, "e:debug_color");

    const G::VisualizationConfig invalidBaseline = edgeColor;
    const auto expectVisualizationConfigUnchanged =
        [&registry, mesh, &invalidBaseline]()
        {
            const auto& current =
                registry.Raw().get<G::VisualizationConfig>(mesh);
            EXPECT_EQ(current.Source, invalidBaseline.Source);
            EXPECT_EQ(current.ColorBufferName, invalidBaseline.ColorBufferName);
            EXPECT_EQ(current.ScalarFieldName, invalidBaseline.ScalarFieldName);
            EXPECT_EQ(current.ScalarDomain, invalidBaseline.ScalarDomain);
            EXPECT_EQ(current.Scalar.AutoRange, invalidBaseline.Scalar.AutoRange);
            EXPECT_FLOAT_EQ(current.Scalar.RangeMin,
                            invalidBaseline.Scalar.RangeMin);
            EXPECT_FLOAT_EQ(current.Scalar.RangeMax,
                            invalidBaseline.Scalar.RangeMax);
            EXPECT_EQ(current.Scalar.BinCount,
                      invalidBaseline.Scalar.BinCount);
            EXPECT_EQ(current.Scalar.Isolines.Num,
                      invalidBaseline.Scalar.Isolines.Num);
        };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::GraphVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:position",
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:kmeans_label",
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "missing",
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                      .ScalarAutoRange = false,
                      .ScalarRangeMin = 2.0f,
                      .ScalarRangeMax = -1.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId =
                          std::numeric_limits<std::uint32_t>::max(),
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                  }),
              Runtime::SandboxEditorCommandStatus::StaleEntity);

    Runtime::SandboxEditorContext unavailable = context;
    unavailable.VisualizationCommandsAvailable = false;
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  unavailable,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                  }),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);
    expectVisualizationConfigUnchanged();

    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationPropertyPreset(
                     Preset::ColorBuffer),
                 "ColorBuffer");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty),
                 "InvalidVisualizationProperty");
}

TEST(SandboxEditorUi, DomainWindowModelsReportSelectedMeshGraphAndPointCloudState)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderSurface>(mesh);
    registry.Raw().emplace<ECSC::SpatialDebugBinding>(
        mesh,
        ECSC::SpatialDebugBinding{
            .Kind = ECSC::SpatialDebugGeometryKind::Bvh,
            .RegistryKey = 77u,
        });
    G::VisualizationConfig meshVisualization{};
    meshVisualization.Source = G::VisualizationConfig::ColorSource::UniformColor;
    meshVisualization.Color = glm::vec4{1.0f};
    registry.Raw().emplace<G::VisualizationConfig>(mesh, meshVisualization);

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);

    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;
    context.PrimitiveViewCommands =
        Runtime::SandboxEditorPrimitiveViewCommandSurface{
            .GetSettings =
                [meshStableId](const std::uint32_t stableId)
                {
                    EXPECT_EQ(stableId, meshStableId);
                    return Runtime::SandboxEditorPrimitiveViewSettings{
                        .EnableEdgeView = true,
                        .EnableVertexView = false,
                    };
                },
            .SetSettings =
                [](std::uint32_t,
                   Runtime::SandboxEditorPrimitiveViewSettings)
                {
                },
            .ClearSettings =
                [](std::uint32_t)
                {
                },
        };

    std::optional<Runtime::PrimitiveSelectionResult> primitive{
        Runtime::PrimitiveSelectionResult{
            .Status = Runtime::PrimitiveRefineStatus::Success,
            .EntityId = meshStableId,
            .StableId = meshStableId,
            .Domain = GS::Domain::Mesh,
            .Kind = Runtime::RefinedPrimitiveKind::Face,
            .FaceId = 0u,
            .EdgeId = Runtime::kInvalidPrimitiveIndex,
            .VertexId = 2u,
            .PointId = Runtime::kInvalidPrimitiveIndex,
        }};
    context.LastRefinedPrimitive = &primitive;

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorDomainWindowModel meshModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDomainWindowKind(
                     meshModel.Kind),
                 "Mesh");
    EXPECT_EQ(meshModel.ExpectedDomain, GS::Domain::Mesh);
    EXPECT_TRUE(meshModel.HasSelectedEntity);
    EXPECT_EQ(meshModel.SelectedEntity.Name, "Mesh");
    EXPECT_EQ(meshModel.SelectedStableId, meshStableId);
    EXPECT_EQ(meshModel.SelectedDomain, GS::Domain::Mesh);
    EXPECT_TRUE(meshModel.DomainMatches);
    EXPECT_TRUE(meshModel.RenderHints.HasRenderSurface);
    EXPECT_TRUE(meshModel.PrimitiveViewControlsAvailable);
    EXPECT_TRUE(meshModel.HasPrimitiveViewSettings);
    EXPECT_TRUE(meshModel.PrimitiveView.EnableEdgeView);
    EXPECT_FALSE(meshModel.PrimitiveView.EnableVertexView);
    EXPECT_TRUE(meshModel.VisualizationControlsAvailable);
    EXPECT_TRUE(meshModel.Visualization.HasSelectedEntity);
    EXPECT_TRUE(meshModel.Visualization.SpatialDebug.HasBinding);
    EXPECT_EQ(meshModel.Visualization.SpatialDebug.RegistryKey, 77u);
    EXPECT_TRUE(meshModel.Visualization.Visualization.HasConfig);
    ASSERT_TRUE(meshModel.Primitive.HasPrimitive);
    EXPECT_TRUE(meshModel.Primitive.HasFaceId);
    EXPECT_TRUE(meshModel.Primitive.HasVertexId);
    ASSERT_TRUE(meshModel.Processing.HasSelectedEntity);
    ASSERT_EQ(meshModel.Processing.KMeansDomains.size(), 1u);
    EXPECT_EQ(meshModel.Processing.KMeansDomains[0],
              Runtime::SandboxEditorGeometryProcessingDomain::MeshVertices);
    ASSERT_FALSE(meshModel.Processing.Entries.empty());
    EXPECT_EQ(meshModel.Processing.Entries[0].Algorithm,
              Runtime::SandboxEditorGeometryProcessingAlgorithm::KMeans);

    const Runtime::SandboxEditorDomainWindowModel graphWhileMeshSelected =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_FALSE(graphWhileMeshSelected.DomainMatches);
    EXPECT_FALSE(graphWhileMeshSelected.Processing.HasSelectedEntity);
    EXPECT_TRUE(graphWhileMeshSelected.Processing.Entries.empty());
    EXPECT_TRUE(HasDiagnostic(
        graphWhileMeshSelected.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::UnsupportedGeometryDomain));

    const std::uint32_t graphStableId =
        Runtime::SelectionController::ToStableEntityId(graph);
    primitive = Runtime::PrimitiveSelectionResult{
        .Status = Runtime::PrimitiveRefineStatus::Success,
        .EntityId = graphStableId,
        .StableId = graphStableId,
        .Domain = GS::Domain::Graph,
        .Kind = Runtime::RefinedPrimitiveKind::Edge,
        .FaceId = Runtime::kInvalidPrimitiveIndex,
        .EdgeId = 1u,
        .VertexId = 2u,
        .PointId = Runtime::kInvalidPrimitiveIndex,
    };
    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDomainWindowKind(
                     graphModel.Kind),
                 "Graph");
    EXPECT_EQ(graphModel.SelectedDomain, GS::Domain::Graph);
    EXPECT_TRUE(graphModel.DomainMatches);
    EXPECT_TRUE(graphModel.RenderHints.HasRenderLines);
    EXPECT_TRUE(graphModel.RenderHints.HasRenderPoints);
    EXPECT_FALSE(graphModel.PrimitiveViewControlsAvailable);
    ASSERT_TRUE(graphModel.Primitive.HasPrimitive);
    EXPECT_TRUE(graphModel.Primitive.HasEdgeId);
    EXPECT_TRUE(graphModel.Primitive.HasVertexId);
    ASSERT_TRUE(graphModel.Processing.HasSelectedEntity);
    ASSERT_EQ(graphModel.Processing.KMeansDomains.size(), 1u);
    EXPECT_EQ(graphModel.Processing.KMeansDomains[0],
              Runtime::SandboxEditorGeometryProcessingDomain::GraphVertices);

    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    primitive = Runtime::PrimitiveSelectionResult{
        .Status = Runtime::PrimitiveRefineStatus::Success,
        .EntityId = cloudStableId,
        .StableId = cloudStableId,
        .Domain = GS::Domain::PointCloud,
        .Kind = Runtime::RefinedPrimitiveKind::Point,
        .FaceId = Runtime::kInvalidPrimitiveIndex,
        .EdgeId = Runtime::kInvalidPrimitiveIndex,
        .VertexId = Runtime::kInvalidPrimitiveIndex,
        .PointId = 3u,
    };
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel cloudModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDomainWindowKind(
                     cloudModel.Kind),
                 "PointCloud");
    EXPECT_EQ(cloudModel.SelectedDomain, GS::Domain::PointCloud);
    EXPECT_TRUE(cloudModel.DomainMatches);
    EXPECT_TRUE(cloudModel.RenderHints.HasRenderPoints);
    ASSERT_TRUE(cloudModel.Primitive.HasPrimitive);
    EXPECT_TRUE(cloudModel.Primitive.HasPointId);
    ASSERT_TRUE(cloudModel.Processing.HasSelectedEntity);
    ASSERT_EQ(cloudModel.Processing.KMeansDomains.size(), 1u);
    EXPECT_EQ(cloudModel.Processing.KMeansDomains[0],
              Runtime::SandboxEditorGeometryProcessingDomain::PointCloudPoints);
}

TEST(SandboxEditorUi, DomainWindowModelsReportNoSelectionStaleAndWrongDomain)
{
    const Runtime::SandboxEditorDomainWindowModel missingScene =
        Runtime::BuildSandboxEditorDomainWindowModel(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        missingScene.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::MissingScene));
    EXPECT_FALSE(missingScene.HasSelectedEntity);

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    Runtime::SandboxEditorContext missingSelection = MakeContext(registry, selection);
    missingSelection.Selection = nullptr;
    const Runtime::SandboxEditorDomainWindowModel missingSelectionModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            missingSelection,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        missingSelectionModel.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::MissingSelectionController));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    const Runtime::SandboxEditorDomainWindowModel noSelection =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        noSelection.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::NoSelectedEntity));
    EXPECT_FALSE(noSelection.DomainMatches);

    const ECS::EntityHandle stale = MakeSelectable(registry, "Stale");
    ASSERT_TRUE(selection.SetSelectedEntity(registry, stale));
    registry.Raw().destroy(stale);
    const Runtime::SandboxEditorDomainWindowModel staleSelection =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        staleSelection.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::NoSelectedEntity));
    EXPECT_FALSE(staleSelection.HasSelectedEntity);

    selection.ClearSelection(registry);
    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 2u);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel wrongDomain =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(wrongDomain.HasSelectedEntity);
    EXPECT_EQ(wrongDomain.ExpectedDomain, GS::Domain::Mesh);
    EXPECT_EQ(wrongDomain.SelectedDomain, GS::Domain::PointCloud);
    EXPECT_FALSE(wrongDomain.DomainMatches);
    EXPECT_TRUE(HasDiagnostic(
        wrongDomain.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::UnsupportedGeometryDomain));
}

TEST(SandboxEditorUi, SelectEntityCommandRoutesThroughSelectionController)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle first = MakeSelectable(registry, "First");
    const ECS::EntityHandle second = MakeSelectable(registry, "Second");
    ASSERT_TRUE(selection.SetSelectedEntity(registry, first));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    EXPECT_TRUE(Runtime::SelectSandboxEditorEntity(
        context,
        Runtime::SelectionController::ToStableEntityId(second)));

    EXPECT_FALSE(selection.IsSelected(first));
    EXPECT_TRUE(selection.IsSelected(second));
    EXPECT_FALSE(registry.Raw().all_of<Sel::SelectedTag>(first));
    EXPECT_TRUE(registry.Raw().all_of<Sel::SelectedTag>(second));
}

TEST(SandboxEditorUi, SelectionDetailsModelReportsHoveredEntityAndPrimitiveIds)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle selected = MakeSelectable(registry, "SelectedMesh");
    const ECS::EntityHandle hovered = MakeSelectable(registry, "HoveredMesh");
    ASSERT_TRUE(selection.SetSelectedEntity(registry, selected));

    selection.RequestHoverPick(10u, 20u);
    const std::optional<Runtime::PendingSelectionPick> pending =
        selection.ConsumePendingPick();
    ASSERT_TRUE(pending.has_value());
    selection.ConsumeHit(registry,
                         Runtime::SelectionController::ToStableEntityId(hovered),
                         pending->Sequence);

    std::optional<Runtime::PrimitiveSelectionResult> primitive{
        Runtime::PrimitiveSelectionResult{
            .Status = Runtime::PrimitiveRefineStatus::Success,
            .EntityId = Runtime::SelectionController::ToStableEntityId(selected),
            .StableId = Runtime::SelectionController::ToStableEntityId(selected),
            .Domain = GS::Domain::Mesh,
            .Kind = Runtime::RefinedPrimitiveKind::Face,
            .FaceId = 12u,
            .EdgeId = 3u,
            .VertexId = 4u,
            .PointId = Runtime::kInvalidPrimitiveIndex,
            .HasHitPosition = true,
            .LocalHit = glm::vec3{1.0f, 2.0f, 3.0f},
            .WorldHit = glm::vec3{4.0f, 5.0f, 6.0f},
        }};

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(
            MakeContext(registry, selection, true, &primitive));

    ASSERT_EQ(frame.Selection.SelectedEntities.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedEntities[0].Entity, selected);
    ASSERT_TRUE(frame.Selection.HasHovered);
    ASSERT_TRUE(frame.Selection.HasHoveredEntity);
    EXPECT_EQ(frame.Selection.HoveredEntity.Entity, hovered);
    ASSERT_TRUE(frame.Selection.Primitive.HasPrimitive);
    EXPECT_TRUE(frame.Selection.Primitive.HasFaceId);
    EXPECT_TRUE(frame.Selection.Primitive.HasEdgeId);
    EXPECT_TRUE(frame.Selection.Primitive.HasVertexId);
    EXPECT_FALSE(frame.Selection.Primitive.HasPointId);
    EXPECT_EQ(frame.Selection.Primitive.Primitive.Kind,
              Runtime::RefinedPrimitiveKind::Face);
    EXPECT_EQ(frame.Selection.Primitive.Primitive.FaceId, 12u);
    EXPECT_FLOAT_EQ(frame.Selection.Primitive.Primitive.WorldHit.z, 6.0f);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorPrimitiveKind(
                     frame.Selection.Primitive.Primitive.Kind),
                 "Face");
}

TEST(SandboxEditorUi, TransformEditCommandMutatesLocalTransformAndMarksDirty)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle entity = MakeSelectable(registry, "Editable");
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(entity);

    EXPECT_EQ(Runtime::ApplySandboxEditorTransformEdit(
                  context,
                  Runtime::SandboxEditorTransformEditCommand{
                      .StableEntityId = stableId,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(entity));

    const Runtime::SandboxEditorCommandStatus status =
        Runtime::ApplySandboxEditorTransformEdit(
            context,
            Runtime::SandboxEditorTransformEditCommand{
                .StableEntityId = stableId,
                .SetPosition = true,
                .Position = glm::vec3{4.0f, 5.0f, 6.0f},
                .SetRotation = true,
                .Rotation = glm::quat{0.5f, 0.5f, 0.5f, 0.5f},
                .SetScale = true,
                .Scale = glm::vec3{2.0f, 2.5f, 3.0f},
            });

    EXPECT_EQ(status, Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(status),
                 "Applied");
    const auto& transform =
        registry.Raw().get<ECSC::Transform::Component>(entity);
    EXPECT_FLOAT_EQ(transform.Position.x, 4.0f);
    EXPECT_FLOAT_EQ(transform.Position.y, 5.0f);
    EXPECT_FLOAT_EQ(transform.Position.z, 6.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.w, 0.5f);
    EXPECT_FLOAT_EQ(transform.Scale.x, 2.0f);
    EXPECT_FLOAT_EQ(transform.Scale.y, 2.5f);
    EXPECT_FLOAT_EQ(transform.Scale.z, 3.0f);
    EXPECT_TRUE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(entity));

    Runtime::SandboxEditorContext missingSelection = context;
    missingSelection.Selection = nullptr;
    EXPECT_EQ(Runtime::ApplySandboxEditorTransformEdit(
                  missingSelection,
                  Runtime::SandboxEditorTransformEditCommand{
                      .StableEntityId = stableId,
                      .SetPosition = true,
                      .Position = glm::vec3{1.0f},
                  }),
              Runtime::SandboxEditorCommandStatus::MissingSelectionController);
}

TEST(SandboxEditorUi, CameraControllerCommandReplacesMainController)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::CameraControllerRegistry cameraControllers;
    cameraControllers.Register(
        Runtime::CameraControllerSlot::Main,
        Runtime::CreateCameraController(
            Extrinsic::Core::Config::CameraControllerKind::Orbit));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CameraControllers = &cameraControllers;
    context.CameraViewport = Extrinsic::Core::Extent2D{640, 40};

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.CameraRender.CameraControlsAvailable);
    ASSERT_TRUE(frame.CameraRender.HasMainCameraController);
    EXPECT_EQ(frame.CameraRender.MainCameraControllerKind,
              Extrinsic::Core::Config::CameraControllerKind::Orbit);

    const Runtime::SandboxEditorCommandStatus status =
        Runtime::ApplySandboxEditorCameraControllerCommand(
            context,
            Runtime::SandboxEditorCameraControllerCommand{
                .Kind = Extrinsic::Core::Config::CameraControllerKind::Fly,
            });

    EXPECT_EQ(status, Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCameraControllerKind(
                     cameraControllers.Resolve(Runtime::CameraControllerSlot::Main)
                         .Kind()),
                 "Fly");

    EXPECT_EQ(Runtime::ApplySandboxEditorCameraControllerCommand(
                  context,
                  Runtime::SandboxEditorCameraControllerCommand{
                      .Kind = Extrinsic::Core::Config::CameraControllerKind::Fly,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);

    Runtime::SandboxEditorContext missingRegistry = context;
    missingRegistry.CameraControllers = nullptr;
    EXPECT_EQ(Runtime::ApplySandboxEditorCameraControllerCommand(
                  missingRegistry,
                  Runtime::SandboxEditorCameraControllerCommand{}),
              Runtime::SandboxEditorCommandStatus::MissingCameraControllerRegistry);
}

TEST(SandboxEditorUi, PrimitiveViewCommandRoutesThroughRuntimeSettings)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    std::optional<Runtime::SandboxEditorPrimitiveViewSettings> storedSettings;
    bool hasStoredSettings = false;
    std::uint32_t setCount = 0u;
    std::uint32_t clearCount = 0u;
    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.PrimitiveViewCommands =
        Runtime::SandboxEditorPrimitiveViewCommandSurface{
            .GetSettings =
                [&](const std::uint32_t stableId)
                {
                    EXPECT_EQ(stableId, meshStableId);
                    if (hasStoredSettings && storedSettings.has_value())
                        return *storedSettings;
                    return Runtime::SandboxEditorPrimitiveViewSettings{};
                },
            .SetSettings =
                [&](const std::uint32_t stableId,
                    const Runtime::SandboxEditorPrimitiveViewSettings settings)
                {
                    EXPECT_EQ(stableId, meshStableId);
                    storedSettings = settings;
                    hasStoredSettings = true;
                    ++setCount;
                },
            .ClearSettings =
                [&](const std::uint32_t stableId)
                {
                    EXPECT_EQ(stableId, meshStableId);
                    hasStoredSettings = false;
                    ++clearCount;
                },
        };

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.CameraRender.PrimitiveViewControlsAvailable);
    ASSERT_TRUE(frame.CameraRender.HasPrimitiveViewEntity);
    EXPECT_FALSE(frame.CameraRender.PrimitiveView.EnableEdgeView);
    EXPECT_FALSE(frame.CameraRender.PrimitiveView.EnableVertexView);

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = meshStableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = true,
                      .SetVertexView = true,
                      .EnableVertexView = true,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_EQ(setCount, 1u);
    ASSERT_TRUE(hasStoredSettings);
    ASSERT_TRUE(storedSettings.has_value());
    EXPECT_TRUE(storedSettings->EnableEdgeView);
    EXPECT_TRUE(storedSettings->EnableVertexView);

    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(frame.CameraRender.PrimitiveView.EnableEdgeView);
    EXPECT_TRUE(frame.CameraRender.PrimitiveView.EnableVertexView);

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = meshStableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = false,
                      .SetVertexView = true,
                      .EnableVertexView = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_EQ(clearCount, 1u);
    EXPECT_FALSE(hasStoredSettings);

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{}),
              Runtime::SandboxEditorCommandStatus::NoChange);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 2u);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = cloudStableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = true,
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
}

TEST(SandboxEditorUi, SpatialDebugBindingCommandRoutesThroughSelectedEntity)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "DebugMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.GeometryDomainControlsAvailable);
    ASSERT_TRUE(frame.Visualization.HasSelectedEntity);
    EXPECT_FALSE(frame.Visualization.SpatialDebug.HasBinding);

    const Runtime::SandboxEditorSpatialDebugBindingCommand enable{
        .StableEntityId = stableId,
        .EnableBinding = true,
        .Kind = ECSC::SpatialDebugGeometryKind::Octree,
        .RegistryKey = 42u,
        .LeafOnly = true,
        .OccupancyOnly = true,
        .MaxDepth = 7u,
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  context,
                  enable),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(registry.Raw().all_of<ECSC::SpatialDebugBinding>(mesh));
    const auto& binding =
        registry.Raw().get<ECSC::SpatialDebugBinding>(mesh);
    EXPECT_EQ(binding.Kind, ECSC::SpatialDebugGeometryKind::Octree);
    EXPECT_EQ(binding.RegistryKey, 42u);
    EXPECT_TRUE(binding.LeafOnly);
    EXPECT_TRUE(binding.OccupancyOnly);
    EXPECT_EQ(binding.MaxDepth, 7u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorSpatialDebugKind(
                     binding.Kind),
                 "Octree");

    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.SpatialDebug.HasBinding);
    EXPECT_EQ(frame.Visualization.SpatialDebug.RegistryKey, 42u);

    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  context,
                  enable),
              Runtime::SandboxEditorCommandStatus::NoChange);

    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  context,
                  Runtime::SandboxEditorSpatialDebugBindingCommand{
                      .StableEntityId = stableId,
                      .EnableBinding = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::SpatialDebugBinding>(mesh));

    Runtime::SandboxEditorContext unavailable = context;
    unavailable.VisualizationCommandsAvailable = false;
    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  unavailable,
                  enable),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);
}

TEST(SandboxEditorUi, VisualizationConfigCommandRoutesThroughSelectedEntity)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "VisualMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorVisualizationConfigCommand scalar{
        .StableEntityId = stableId,
        .EnableConfig = true,
        .Source = G::VisualizationConfig::ColorSource::ScalarField,
        .ScalarFieldName = "curvature",
        .ScalarDomain = G::VisualizationConfig::Domain::Face,
        .ScalarAutoRange = false,
        .ScalarRangeMin = -1.0f,
        .ScalarRangeMax = 2.0f,
        .ScalarBinCount = 4u,
        .IsolineCount = 8u,
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  scalar),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
    const auto& config = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(config.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(config.ScalarFieldName, "curvature");
    EXPECT_EQ(config.ScalarDomain, G::VisualizationConfig::Domain::Face);
    EXPECT_FALSE(config.Scalar.AutoRange);
    EXPECT_FLOAT_EQ(config.Scalar.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(config.Scalar.RangeMax, 2.0f);
    EXPECT_EQ(config.Scalar.BinCount, 4u);
    EXPECT_EQ(config.Scalar.Isolines.Num, 8u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationColorSource(
                     config.Source),
                 "ScalarField");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationDomain(
                     config.ScalarDomain),
                 "Face");

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.Visualization.HasConfig);
    EXPECT_EQ(frame.Visualization.Visualization.Source,
              G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(frame.Visualization.Visualization.ScalarFieldName, "curvature");
    EXPECT_EQ(frame.Visualization.Visualization.IsolineCount, 8u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  scalar),
              Runtime::SandboxEditorCommandStatus::NoChange);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  Runtime::SandboxEditorVisualizationConfigCommand{
                      .StableEntityId = stableId,
                      .EnableConfig = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(registry.Raw().all_of<G::VisualizationConfig>(mesh));

    Runtime::SandboxEditorContext unavailable = context;
    unavailable.VisualizationCommandsAvailable = false;
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  unavailable,
                  scalar),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);
}

TEST(SandboxEditorUi, VisualizationAdapterBindingCommandRoutesThroughRuntimeSurface)
{
    using Binding = Runtime::RenderExtractionCache::VisualizationAdapterBinding;
    using Kind = Runtime::RenderExtractionCache::VisualizationAdapterBindingKind;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "AdapterMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    std::optional<Binding> storedBinding{};
    std::uint32_t storedStableId{0u};
    std::uint32_t setCount{0u};
    std::uint32_t clearCount{0u};

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;
    context.VisualizationAdapterBindings =
        Runtime::SandboxEditorVisualizationAdapterBindingCommandSurface{
            .GetBinding =
                [&](const std::uint32_t queriedStableId) -> std::optional<Binding>
                {
                    if (storedBinding.has_value() &&
                        storedStableId == queriedStableId)
                    {
                        return storedBinding;
                    }
                    return std::nullopt;
                },
            .SetBinding =
                [&](const std::uint32_t targetStableId, Binding binding)
                {
                    storedStableId = targetStableId;
                    storedBinding = std::move(binding);
                    ++setCount;
                },
            .ClearBinding =
                [&](const std::uint32_t targetStableId)
                {
                    if (storedStableId == targetStableId)
                    {
                        storedBinding.reset();
                    }
                    ++clearCount;
                },
        };

    Runtime::VisualizationAdapterOptions options{};
    options.SourceName = "velocity";
    options.OutputName = "velocity_glyphs";
    options.PositionBufferBDA = 0xAABB'1000u;
    options.VectorBufferBDA = 0xAABB'2000u;
    options.VectorScale = 2.0f;
    options.DepthTested = false;

    const Runtime::SandboxEditorVisualizationAdapterBindingCommand bindVector{
        .StableEntityId = stableId,
        .EnableBinding = true,
        .AdapterKey = 0xF00Du,
        .BufferBDA = 0xAABB'2000u,
        .Kind = Kind::VectorField,
        .Options = options,
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  bindVector),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(storedBinding.has_value());
    EXPECT_EQ(storedStableId, stableId);
    EXPECT_EQ(storedBinding->AdapterKey, 0xF00Du);
    EXPECT_EQ(storedBinding->Kind, Kind::VectorField);
    EXPECT_EQ(storedBinding->Options.SourceName, "velocity");
    EXPECT_EQ(storedBinding->Options.VectorBufferBDA, 0xAABB'2000u);
    EXPECT_FALSE(storedBinding->Options.DepthTested);
    EXPECT_EQ(setCount, 1u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationAdapterBindingKind(
                     storedBinding->Kind),
                 "VectorField");

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.AdapterBindingControlsAvailable);
    ASSERT_TRUE(frame.Visualization.AdapterBinding.HasBinding);
    EXPECT_EQ(frame.Visualization.AdapterBinding.AdapterKey, 0xF00Du);
    EXPECT_EQ(frame.Visualization.AdapterBinding.Kind, Kind::VectorField);
    EXPECT_EQ(frame.Visualization.AdapterBinding.Options.OutputName,
              "velocity_glyphs");

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  bindVector),
              Runtime::SandboxEditorCommandStatus::NoChange);
    EXPECT_EQ(setCount, 1u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId = stableId,
                      .EnableBinding = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(storedBinding.has_value());
    EXPECT_EQ(clearCount, 1u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId = stableId,
                      .EnableBinding = false,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);
    EXPECT_EQ(clearCount, 1u);

    Runtime::SandboxEditorContext missingSurface = context;
    missingSurface.VisualizationAdapterBindings = {};
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  missingSurface,
                  bindVector),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);

    const ECS::EntityHandle empty = MakeSelectable(registry, "NoGeometry");
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId =
                          Runtime::SelectionController::ToStableEntityId(empty),
                      .EnableBinding = true,
                      .AdapterKey = 0xF00Du,
                      .Kind = Kind::Scalar,
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
                      .EnableBinding = true,
                      .AdapterKey = 0xF00Du,
                  }),
              Runtime::SandboxEditorCommandStatus::StaleEntity);
}

TEST(SandboxEditorUi, AdapterCallbackDrawsDeterministicDisabledPanelFrame)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorPanelFrame captured{};

    FakeWindow window(1280, 720);
    Extrinsic::Graphics::ImGuiOverlaySystem overlay;
    Runtime::ImGuiAdapter adapter(window, overlay);
    ASSERT_TRUE(adapter.Initialize());

    adapter.SetEditorCallback(
        [&]
        {
            captured = Runtime::BuildSandboxEditorPanelFrame(
                MakeContext(registry, selection));
            Runtime::DrawSandboxEditorPanelFrame(captured);
        });

    adapter.BeginFrame(1.0 / 60.0);
    adapter.EndFrame();
    adapter.BeginFrame(1.0 / 60.0);
    adapter.EndFrame();

    EXPECT_EQ(adapter.GetDiagnostics().EditorCallbackInvocations, 2u);
    EXPECT_EQ(adapter.GetDiagnostics().FramesProduced, 2u);
    EXPECT_GE(adapter.GetDiagnostics().LastDrawListCount, 1u);
    EXPECT_FALSE(captured.FileImport.Enabled);
    EXPECT_TRUE(HasDiagnostic(captured.FileImport.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));
}

TEST(SandboxEditorUi, EngineImportFacadeReportsMissingFile)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = "/tmp/intrinsicengine-ui-001-missing.gltf",
        });
    EXPECT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error(), Core::ErrorCode::FileNotFound);

    engine.Shutdown();
}

TEST(SandboxEditorUi, EngineImportFacadeMaterializesStandaloneGeometryDomains)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    TmpFile graphFile(
        "runtime_dragdrop_import_graph.tgf",
        "1 0 0 0 first\n"
        "2 1 0 0 second\n"
        "#\n"
        "1 2 1.0 edge\n");
    TmpFile cloudFile(
        "runtime_dragdrop_import_cloud.xyz",
        "0 0 0\n"
        "1 2 3\n");

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto mesh = engine.ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);

    auto graph = engine.ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = graphFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Graph,
        });
    ASSERT_TRUE(graph.has_value()) << static_cast<int>(graph.error());
    EXPECT_TRUE(graph->Asset.IsValid());
    EXPECT_EQ(graph->PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_EQ(graph->PrimitiveEntitiesCreated, 1u);

    auto cloud = engine.ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = cloudFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::PointCloud,
        });
    ASSERT_TRUE(cloud.has_value()) << static_cast<int>(cloud.error());
    EXPECT_TRUE(cloud->Asset.IsValid());
    EXPECT_EQ(cloud->PayloadKind, Assets::AssetPayloadKind::PointCloud);
    EXPECT_EQ(cloud->PrimitiveEntitiesCreated, 1u);

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Graph), 1u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 1u);

    auto& raw = engine.GetScene().Raw();
    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderSurface>(*meshEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*meshEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*meshEntity)));
    const auto& meshWorld =
        raw.get<ECSC::Culling::World::Bounds>(*meshEntity);
    EXPECT_NEAR(meshWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(meshWorld.WorldBoundingSphere.Center.y, 0.5f, 1.0e-5f);
    EXPECT_GT(meshWorld.WorldBoundingSphere.Radius, 0.5f);

    const std::optional<ECS::EntityHandle> graphEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Graph);
    ASSERT_TRUE(graphEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderLines>(*graphEntity));
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*graphEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*graphEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*graphEntity)));
    const auto& graphWorld =
        raw.get<ECSC::Culling::World::Bounds>(*graphEntity);
    EXPECT_NEAR(graphWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(graphWorld.WorldBoundingSphere.Center.y, 0.0f, 1.0e-5f);
    EXPECT_GT(graphWorld.WorldBoundingSphere.Radius, 0.4f);

    const std::optional<ECS::EntityHandle> cloudEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::PointCloud);
    ASSERT_TRUE(cloudEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*cloudEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*cloudEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*cloudEntity)));
    const auto& cloudWorld =
        raw.get<ECSC::Culling::World::Bounds>(*cloudEntity);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.y, 1.0f, 1.0e-5f);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.z, 1.5f, 1.0e-5f);
    EXPECT_GT(cloudWorld.WorldBoundingSphere.Radius, 1.8f);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::PointCloud);
    EXPECT_NE(lastEvent->Path.find("runtime_dragdrop_import_cloud.xyz"),
              std::string::npos);

    engine.Shutdown();
}

TEST(SandboxEditorUi, EngineImportFacadeMaterializesNonManifoldObjAsRenderableMesh)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_nonmanifold.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 0 -1 0\n"
        "v 0.5 0 1\n"
        "f 1 2 3\n"
        "f 2 1 4\n"
        "f 1 2 5\n");

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto mesh = engine.ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);

    auto& raw = engine.GetScene().Raw();
    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    EXPECT_TRUE((raw.all_of<ECSC::MetaData,
                            ECSC::Hierarchy::Component,
                            ECSC::Transform::Component,
                            ECSC::Transform::WorldMatrix,
                            Sel::SelectableTag,
                            G::RenderSurface,
                            G::VisualizationConfig,
                            GS::Vertices,
                            GS::Edges,
                            GS::Halfedges,
                            GS::Faces>(*meshEntity)));
    EXPECT_EQ(raw.get<G::RenderSurface>(*meshEntity).Domain,
              G::RenderSurface::SourceDomain::Vertex);

    Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(engine.GetScene(),
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_EQ(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade)
{
    TmpFile cloudFile(
        "runtime_dragdrop_event_cloud.ply",
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "2 0 0\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForAssetImportEventApplication>(128u));
    engine.Initialize();

    Runtime::SandboxEditorUi ui;
    ui.Attach(engine);

    const std::vector<std::string> droppedPaths{cloudFile.Path.string()};
    engine.ImportDroppedFilePaths(droppedPaths);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 0u);
    EXPECT_FALSE(engine.GetLastAssetImportEvent().has_value());

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::PointCloud);

    ASSERT_TRUE(ui.GetLastFrame().FileImport.LastResult.has_value());
    EXPECT_TRUE(ui.GetLastFrame().FileImport.LastResult->Succeeded());
    EXPECT_EQ(ui.GetLastFrame().FileImport.LastResult->PayloadKind,
              Assets::AssetPayloadKind::PointCloud);
    EXPECT_FALSE(HasDiagnostic(ui.GetLastFrame().FileImport.Diagnostics,
                               Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));

    ui.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, EngineAttachmentRegistersEditorCallback)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    Runtime::SandboxEditorUi ui;
    ui.Attach(engine);
    EXPECT_TRUE(ui.IsAttached());

    if (engine.GetWindow().ShouldClose())
    {
        ui.Detach();
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; frame callback coverage requires a live window";
    }

    engine.Run();

    EXPECT_GE(engine.GetImGuiAdapter().GetDiagnostics().EditorCallbackInvocations, 1u);
    EXPECT_TRUE(ui.GetLastFrame().FileImport.Enabled);
    EXPECT_FALSE(HasDiagnostic(ui.GetLastFrame().FileImport.Diagnostics,
                               Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));

    ui.Detach();
    engine.Shutdown();
}
