#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.SandboxEditorUi;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;

namespace Runtime = Extrinsic::Runtime;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace Plat = Extrinsic::Platform;

namespace
{
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

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
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
    EXPECT_FALSE(frame.FileImport.Enabled);
    EXPECT_TRUE(HasDiagnostic(frame.FileImport.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));
    EXPECT_TRUE(HasDiagnostic(frame.Inspector.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingScene));
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

    ASSERT_EQ(frame.Selection.SelectedStableIds.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedStableIds[0],
              Runtime::SelectionController::ToStableEntityId(first));
    ASSERT_EQ(frame.Selection.SelectedEntities.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedEntities[0].Name, "Cloud A");
    EXPECT_FALSE(frame.FileImport.Enabled);
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
    EXPECT_FALSE(ui.GetLastFrame().FileImport.Enabled);
    EXPECT_TRUE(HasDiagnostic(ui.GetLastFrame().FileImport.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));

    ui.Detach();
    engine.Shutdown();
}
