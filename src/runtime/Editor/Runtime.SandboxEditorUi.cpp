module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

module Extrinsic.Runtime.SandboxEditorUi;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.SelectionController;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;

        [[nodiscard]] SandboxEditorPrimitiveViewSettings FromRuntimeSettings(
            const MeshPrimitiveViewSettings settings) noexcept
        {
            return SandboxEditorPrimitiveViewSettings{
                .EnableEdgeView = settings.EnableEdgeView,
                .EnableVertexView = settings.EnableVertexView,
            };
        }

        [[nodiscard]] MeshPrimitiveViewSettings ToRuntimeSettings(
            const SandboxEditorPrimitiveViewSettings settings) noexcept
        {
            return MeshPrimitiveViewSettings{
                .EnableEdgeView = settings.EnableEdgeView,
                .EnableVertexView = settings.EnableVertexView,
            };
        }

        [[nodiscard]] bool SamePrimitiveViewSettings(
            const SandboxEditorPrimitiveViewSettings lhs,
            const SandboxEditorPrimitiveViewSettings rhs) noexcept
        {
            return lhs.EnableEdgeView == rhs.EnableEdgeView &&
                   lhs.EnableVertexView == rhs.EnableVertexView;
        }

        [[nodiscard]] Core::Extent2D SafeViewport(
            const Core::Extent2D commandViewport,
            const Core::Extent2D contextViewport) noexcept
        {
            if (!Core::IsEmpty(commandViewport))
                return commandViewport;
            if (!Core::IsEmpty(contextViewport))
                return contextViewport;
            return Core::Extent2D{1, 1};
        }

        [[nodiscard]] SandboxEditorDiagnostic MakeDiagnostic(
            const SandboxEditorDiagnosticCode code,
            std::string message)
        {
            return SandboxEditorDiagnostic{
                .Code = code,
                .Message = std::move(message),
            };
        }

        void AddDiagnostic(std::vector<SandboxEditorDiagnostic>& diagnostics,
                           const SandboxEditorDiagnosticCode code,
                           std::string message)
        {
            diagnostics.push_back(MakeDiagnostic(code, std::move(message)));
        }

        [[nodiscard]] std::string FallbackEntityName(const ECS::EntityHandle entity)
        {
            return "Entity " + std::to_string(
                static_cast<std::uint32_t>(entity));
        }

        [[nodiscard]] SandboxEditorEntityRow BuildEntityRow(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorEntityRow row{};
            row.Entity = entity;
            row.StableEntityId = SelectionController::ToStableEntityId(entity);
            row.Name = FallbackEntityName(entity);

            if (const auto* meta = raw.try_get<ECSC::MetaData>(entity);
                meta != nullptr && !meta->EntityName.empty())
            {
                row.Name = meta->EntityName;
            }

            if (const auto* stableId = raw.try_get<ECSC::StableId>(entity))
            {
                row.DurableStableId = *stableId;
                row.HasDurableStableId = ECSC::IsValid(*stableId);
            }

            row.Selectable = raw.all_of<Sel::SelectableTag>(entity);
            row.Selected = raw.all_of<Sel::SelectedTag>(entity);
            row.Hovered = raw.all_of<Sel::HoveredTag>(entity);
            return row;
        }

        [[nodiscard]] SandboxEditorTransformModel BuildTransformModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorTransformModel model{};
            if (const auto* local = raw.try_get<ECSC::Transform::Component>(entity))
            {
                model.HasLocalTransform = true;
                model.LocalPosition = local->Position;
                model.LocalRotation = local->Rotation;
                model.LocalScale = local->Scale;
            }

            if (const auto* world = raw.try_get<ECSC::Transform::WorldMatrix>(entity))
            {
                model.HasWorldTransform = true;
                model.WorldPosition = glm::vec3(world->Matrix[3]);
            }

            return model;
        }

        [[nodiscard]] SandboxEditorRenderHintModel BuildRenderHintModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorRenderHintModel model{};
            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
            {
                model.HasRenderSurface = true;
                model.SurfaceDomain =
                    surface->Domain == G::RenderSurface::SourceDomain::Face
                        ? "Face"
                        : "Vertex";
            }

            if (const auto* lines = raw.try_get<G::RenderLines>(entity))
            {
                model.HasRenderLines = true;
                model.LineDomain =
                    lines->Domain == G::RenderLines::SourceDomain::Edge
                        ? "Edge"
                        : "Vertex";
                if (const auto* width = std::get_if<float>(&lines->WidthSource))
                {
                    model.HasUniformLineWidth = true;
                    model.UniformLineWidth = *width;
                }
                else if (const auto* name = std::get_if<std::string>(&lines->WidthSource))
                {
                    model.HasNamedLineWidth = true;
                    model.LineWidthName = *name;
                }
            }

            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
            {
                model.HasRenderPoints = true;
                switch (points->Type)
                {
                case G::RenderPoints::RenderType::Flat:
                    model.PointRenderType = "Flat";
                    break;
                case G::RenderPoints::RenderType::Sphere:
                    model.PointRenderType = "Sphere";
                    break;
                case G::RenderPoints::RenderType::Surfel:
                    model.PointRenderType = "Surfel";
                    break;
                }

                if (const auto* size = std::get_if<float>(&points->SizeSource))
                {
                    model.HasUniformPointSize = true;
                    model.UniformPointSize = *size;
                }
                else if (const auto* name = std::get_if<std::string>(&points->SizeSource))
                {
                    model.HasNamedPointSize = true;
                    model.PointSizeName = *name;
                }
            }

            return model;
        }

        [[nodiscard]] SandboxEditorGeometryDomainModel BuildGeometryDomainModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            return SandboxEditorGeometryDomainModel{
                .Domain = view.ActiveDomain,
                .Valid = view.Valid(),
                .VertexCount = view.VerticesAlive(),
                .EdgeCount = view.EdgesAlive(),
                .HalfedgeCount = view.HalfedgesTotal(),
                .FaceCount = view.FacesAlive(),
                .NodeCount = view.NodesAlive(),
            };
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveFirstSelectedEntity(
            const SandboxEditorContext& context)
        {
            if (context.Scene == nullptr || context.Selection == nullptr)
                return std::nullopt;

            const entt::registry& raw = context.Scene->Raw();
            for (const std::uint32_t stableId : context.Selection->SelectedStableIds())
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableId);
                if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                    return entity;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
            const entt::registry& raw,
            const std::uint32_t stableId)
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableId);
            if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                return entity;
            return std::nullopt;
        }

        [[nodiscard]] SandboxEditorPrimitiveDetailModel BuildPrimitiveDetailModel(
            const PrimitiveSelectionResult& primitive)
        {
            return SandboxEditorPrimitiveDetailModel{
                .HasPrimitive = true,
                .Primitive = primitive,
                .HasFaceId = primitive.FaceId != kInvalidPrimitiveIndex,
                .HasEdgeId = primitive.EdgeId != kInvalidPrimitiveIndex,
                .HasVertexId = primitive.VertexId != kInvalidPrimitiveIndex,
                .HasPointId = primitive.PointId != kInvalidPrimitiveIndex,
            };
        }

        [[nodiscard]] SandboxEditorInspectorModel BuildInspectorModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorInspectorModel model{};
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for inspection.");
                return model;
            }

            const entt::registry& raw = context.Scene->Raw();
            model.HasEntity = true;
            model.Entity = BuildEntityRow(raw, *selected);
            model.Transform = BuildTransformModel(raw, *selected);
            model.RenderHints = BuildRenderHintModel(raw, *selected);
            model.Geometry = BuildGeometryDomainModel(raw, *selected);

            if (model.Geometry.Domain == GS::Domain::Unknown)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                              "Selected entity has mixed GeometrySources topology.");
            }

            return model;
        }

        [[nodiscard]] SandboxEditorSelectionModel BuildSelectionModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorSelectionModel model{};
            if (context.Selection == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingSelectionController,
                              "Selection controller is unavailable.");
                return model;
            }

            const auto selected = context.Selection->SelectedStableIds();
            model.SelectedStableIds.assign(selected.begin(), selected.end());
            model.HasHovered = context.Selection->HasHovered();
            model.HoveredStableId = context.Selection->HoveredStableId();

            if (context.Scene != nullptr)
            {
                const entt::registry& raw = context.Scene->Raw();
                for (const std::uint32_t stableId : model.SelectedStableIds)
                {
                    if (const std::optional<ECS::EntityHandle> entity =
                            ResolveStableEntity(raw, stableId);
                        entity.has_value())
                    {
                        model.SelectedEntities.push_back(BuildEntityRow(raw, *entity));
                    }
                }

                if (model.HasHovered)
                {
                    if (const std::optional<ECS::EntityHandle> hovered =
                            ResolveStableEntity(raw, model.HoveredStableId);
                        hovered.has_value())
                    {
                        model.HasHoveredEntity = true;
                        model.HoveredEntity = BuildEntityRow(raw, *hovered);
                    }
                }
            }
            else
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for selection details.");
            }

            if (context.LastRefinedPrimitive != nullptr &&
                context.LastRefinedPrimitive->has_value())
            {
                model.Primitive = BuildPrimitiveDetailModel(**context.LastRefinedPrimitive);
            }

            if (model.SelectedStableIds.empty() && !model.Primitive.HasPrimitive)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity or refined primitive is available.");
            }

            return model;
        }

        [[nodiscard]] SandboxEditorFileImportModel BuildFileImportModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorFileImportModel model{};
            model.Enabled = context.AssetImportCommandsAvailable;
            if (model.Enabled)
            {
                model.StatusText = "Import commands available.";
            }
            else
            {
                model.StatusText = "Asset import is disabled: ASSETIO-001 is not available.";
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::AssetImportUnavailable,
                              model.StatusText);
            }
            return model;
        }

        [[nodiscard]] SandboxEditorCameraRenderModel BuildCameraRenderModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorCameraRenderModel model{};
            model.CameraControlsAvailable = context.CameraControllers != nullptr;
            model.RenderSettingsAvailable = context.CameraControllers != nullptr;
            model.PrimitiveViewControlsAvailable =
                context.PrimitiveViewCommands.Available();

            if (context.CameraControllers != nullptr)
            {
                if (const ICameraController* controller =
                        context.CameraControllers->ResolveOrNull(CameraControllerSlot::Main);
                    controller != nullptr)
                {
                    model.HasMainCameraController = true;
                    model.MainCameraControllerKind = controller->Kind();
                }
            }

            if (context.Scene != nullptr && context.PrimitiveViewCommands.Available())
            {
                if (const std::optional<ECS::EntityHandle> selected =
                        ResolveFirstSelectedEntity(context);
                    selected.has_value())
                {
                    const GS::ConstSourceView view =
                        GS::BuildConstView(context.Scene->Raw(), *selected);
                    if (view.ActiveDomain == GS::Domain::Mesh)
                    {
                        model.HasPrimitiveViewEntity = true;
                        model.PrimitiveViewStableId =
                            SelectionController::ToStableEntityId(*selected);
                        model.PrimitiveView =
                            context.PrimitiveViewCommands.GetSettings(
                                model.PrimitiveViewStableId);
                    }
                    else if (view.ActiveDomain != GS::Domain::None)
                    {
                        AddDiagnostic(model.Diagnostics,
                                      SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                                      "Mesh primitive views require a mesh-domain selection.");
                    }
                }
            }

            if (!model.CameraControlsAvailable &&
                !model.PrimitiveViewControlsAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::CameraRenderCommandsUnavailable,
                              "Camera/render setting command seams are unavailable.");
            }
            return model;
        }

        [[nodiscard]] SandboxEditorVisualizationModel BuildVisualizationModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorVisualizationModel model{};
            model.GeometryDomainControlsAvailable = context.VisualizationCommandsAvailable;
            if (!context.VisualizationCommandsAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable,
                              "Visualization adapter command routing is deferred past Slice A.");
            }
            return model;
        }

        [[nodiscard]] SandboxEditorContext BuildContextFromEngine(Engine& engine)
        {
            return SandboxEditorContext{
                .Scene = &engine.GetScene(),
                .Selection = &engine.GetSelectionController(),
                .LastRefinedPrimitive = &engine.GetLastRefinedPrimitiveSelection(),
                .CameraControllers = &engine.GetCameraControllerRegistry(),
                .CameraViewport = Core::Extent2D{
                    engine.GetWindow().GetFramebufferExtent().Width,
                    engine.GetWindow().GetFramebufferExtent().Height},
                .PrimitiveViewCommands = SandboxEditorPrimitiveViewCommandSurface{
                    .GetSettings =
                        [&engine](const std::uint32_t stableEntityId)
                        {
                            return FromRuntimeSettings(
                                engine.GetMeshPrimitiveViewSettings(stableEntityId));
                        },
                    .SetSettings =
                        [&engine](const std::uint32_t stableEntityId,
                                  const SandboxEditorPrimitiveViewSettings settings)
                        {
                            engine.SetMeshPrimitiveViewSettings(
                                stableEntityId,
                                ToRuntimeSettings(settings));
                        },
                    .ClearSettings =
                        [&engine](const std::uint32_t stableEntityId)
                        {
                            engine.ClearMeshPrimitiveViewSettings(stableEntityId);
                        },
                },
                .ImGuiAdapterAvailable = engine.GetImGuiAdapter().IsInitialized(),
                .AssetImportCommandsAvailable = false,
                .CameraRenderCommandsAvailable = true,
                .VisualizationCommandsAvailable = false,
            };
        }

        void DrawDiagnostics(const std::vector<SandboxEditorDiagnostic>& diagnostics)
        {
            for (const SandboxEditorDiagnostic& diagnostic : diagnostics)
            {
                ImGui::TextDisabled("%s: %s",
                                    DebugNameForSandboxEditorDiagnosticCode(diagnostic.Code),
                                    diagnostic.Message.c_str());
            }
        }

        void DrawVec3(const char* label, const glm::vec3 value)
        {
            ImGui::Text("%s: %.3f, %.3f, %.3f", label, value.x, value.y, value.z);
        }

        void DrawQuat(const char* label, const glm::quat value)
        {
            ImGui::Text("%s: %.3f, %.3f, %.3f, %.3f",
                        label,
                        value.w,
                        value.x,
                        value.y,
                        value.z);
        }

        void DrawPanelFrame(const SandboxEditorPanelFrame& frame,
                            const SandboxEditorContext* context)
        {
            ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Sandbox Editor"))
            {
                ImGui::TextUnformatted("Promoted runtime editor shell");
                DrawDiagnostics(frame.Diagnostics);
            }
            ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(280.0f, 420.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Scene Hierarchy"))
            {
                if (frame.Hierarchy.empty())
                {
                    ImGui::TextDisabled("No live scene entities.");
                }
                for (const SandboxEditorEntityRow& row : frame.Hierarchy)
                {
                    ImGui::PushID(static_cast<int>(row.StableEntityId));
                    const bool clicked =
                        ImGui::Selectable(row.Name.c_str(), row.Selected);
                    ImGui::PopID();
                    if (clicked && context != nullptr)
                        (void)SelectSandboxEditorEntity(*context, row.StableEntityId);
                    if (row.Hovered)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(hover)");
                    }
                }
            }
            ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Inspector"))
            {
                if (!frame.Inspector.HasEntity)
                {
                    DrawDiagnostics(frame.Inspector.Diagnostics);
                }
                else
                {
                    const SandboxEditorInspectorModel& inspector = frame.Inspector;
                    ImGui::Text("Entity: %s", inspector.Entity.Name.c_str());
                    ImGui::Text("Render id: %u", inspector.Entity.StableEntityId);
                    ImGui::Text("Durable StableId: %s",
                                inspector.Entity.HasDurableStableId ? "valid" : "none");
                    if (inspector.Transform.HasLocalTransform)
                    {
                        if (context != nullptr)
                        {
                            glm::vec3 localPosition = inspector.Transform.LocalPosition;
                            if (ImGui::DragFloat3("Local position",
                                                  &localPosition.x,
                                                  0.01f))
                            {
                                (void)ApplySandboxEditorTransformEdit(
                                    *context,
                                    SandboxEditorTransformEditCommand{
                                        .StableEntityId = inspector.Entity.StableEntityId,
                                        .SetPosition = true,
                                        .Position = localPosition,
                                    });
                            }

                            glm::vec3 localScale = inspector.Transform.LocalScale;
                            if (ImGui::DragFloat3("Local scale",
                                                  &localScale.x,
                                                  0.01f))
                            {
                                (void)ApplySandboxEditorTransformEdit(
                                    *context,
                                    SandboxEditorTransformEditCommand{
                                        .StableEntityId = inspector.Entity.StableEntityId,
                                        .SetScale = true,
                                        .Scale = localScale,
                                    });
                            }
                        }
                        else
                        {
                            DrawVec3("Local position", inspector.Transform.LocalPosition);
                            DrawVec3("Local scale", inspector.Transform.LocalScale);
                        }
                        DrawQuat("Local rotation (wxyz)", inspector.Transform.LocalRotation);
                    }
                    if (inspector.Transform.HasWorldTransform)
                        DrawVec3("World position", inspector.Transform.WorldPosition);
                    ImGui::Text("Render hints: surface=%s lines=%s points=%s",
                                inspector.RenderHints.HasRenderSurface ? "yes" : "no",
                                inspector.RenderHints.HasRenderLines ? "yes" : "no",
                                inspector.RenderHints.HasRenderPoints ? "yes" : "no");
                    if (inspector.RenderHints.HasRenderSurface)
                        ImGui::Text("Surface domain: %s",
                                    inspector.RenderHints.SurfaceDomain.c_str());
                    if (inspector.RenderHints.HasRenderLines)
                    {
                        ImGui::Text("Line domain: %s",
                                    inspector.RenderHints.LineDomain.c_str());
                        if (inspector.RenderHints.HasUniformLineWidth)
                            ImGui::Text("Line width: %.3f",
                                        inspector.RenderHints.UniformLineWidth);
                        if (inspector.RenderHints.HasNamedLineWidth)
                            ImGui::Text("Line width source: %s",
                                        inspector.RenderHints.LineWidthName.c_str());
                    }
                    if (inspector.RenderHints.HasRenderPoints)
                    {
                        ImGui::Text("Point type: %s",
                                    inspector.RenderHints.PointRenderType.c_str());
                        if (inspector.RenderHints.HasUniformPointSize)
                            ImGui::Text("Point size: %.3f",
                                        inspector.RenderHints.UniformPointSize);
                        if (inspector.RenderHints.HasNamedPointSize)
                            ImGui::Text("Point size source: %s",
                                        inspector.RenderHints.PointSizeName.c_str());
                    }
                    ImGui::Text("Geometry domain: %s",
                                DebugNameForSandboxEditorGeometryDomain(
                                    inspector.Geometry.Domain));
                    ImGui::Text("Counts: v=%zu e=%zu h=%zu f=%zu n=%zu",
                                inspector.Geometry.VertexCount,
                                inspector.Geometry.EdgeCount,
                                inspector.Geometry.HalfedgeCount,
                                inspector.Geometry.FaceCount,
                                inspector.Geometry.NodeCount);
                    DrawDiagnostics(inspector.Diagnostics);
                }
            }
            ImGui::End();

            if (ImGui::Begin("Selection Details"))
            {
                ImGui::Text("Selected entities: %zu", frame.Selection.SelectedStableIds.size());
                for (const SandboxEditorEntityRow& row : frame.Selection.SelectedEntities)
                    ImGui::BulletText("%s (%u)", row.Name.c_str(), row.StableEntityId);
                if (frame.Selection.HasHovered)
                {
                    ImGui::Text("Hovered render id: %u", frame.Selection.HoveredStableId);
                    if (frame.Selection.HasHoveredEntity)
                        ImGui::Text("Hovered entity: %s",
                                    frame.Selection.HoveredEntity.Name.c_str());
                }
                if (frame.Selection.Primitive.HasPrimitive)
                {
                    const PrimitiveSelectionResult& primitive =
                        frame.Selection.Primitive.Primitive;
                    ImGui::Text("Primitive status: %s",
                                DebugNameForPrimitiveRefineStatus(primitive.Status));
                    ImGui::Text("Primitive domain/kind: %s / %s",
                                DebugNameForSandboxEditorGeometryDomain(primitive.Domain),
                                DebugNameForSandboxEditorPrimitiveKind(primitive.Kind));
                    if (frame.Selection.Primitive.HasFaceId)
                        ImGui::Text("Face id: %u", primitive.FaceId);
                    if (frame.Selection.Primitive.HasEdgeId)
                        ImGui::Text("Edge id: %u", primitive.EdgeId);
                    if (frame.Selection.Primitive.HasVertexId)
                        ImGui::Text("Vertex id: %u", primitive.VertexId);
                    if (frame.Selection.Primitive.HasPointId)
                        ImGui::Text("Point id: %u", primitive.PointId);
                    if (primitive.HasHitPosition)
                    {
                        DrawVec3("Local hit", primitive.LocalHit);
                        DrawVec3("World hit", primitive.WorldHit);
                    }
                }
                DrawDiagnostics(frame.Selection.Diagnostics);
            }
            ImGui::End();

            if (ImGui::Begin("File / Import"))
            {
                if (!frame.FileImport.Enabled)
                    ImGui::BeginDisabled();
                ImGui::Button("Import asset");
                if (!frame.FileImport.Enabled)
                    ImGui::EndDisabled();
                ImGui::TextWrapped("%s", frame.FileImport.StatusText.c_str());
                DrawDiagnostics(frame.FileImport.Diagnostics);
            }
            ImGui::End();

            if (ImGui::Begin("Camera / Render"))
            {
                if (frame.CameraRender.HasMainCameraController)
                {
                    ImGui::Text("Main camera: %s",
                                DebugNameForSandboxEditorCameraControllerKind(
                                    frame.CameraRender.MainCameraControllerKind));
                }
                else
                {
                    ImGui::TextDisabled("Main camera: not registered");
                }

                if (context != nullptr &&
                    frame.CameraRender.CameraControlsAvailable)
                {
                    if (ImGui::Button("Orbit"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::Orbit,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Fly"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::Fly,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Free look"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::FreeLook,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Top down"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::TopDown,
                            });
                    }
                }

                if (frame.CameraRender.HasPrimitiveViewEntity)
                {
                    bool edgeView =
                        frame.CameraRender.PrimitiveView.EnableEdgeView;
                    if (context != nullptr &&
                        ImGui::Checkbox("Mesh edge view", &edgeView))
                    {
                        (void)ApplySandboxEditorPrimitiveViewCommand(
                            *context,
                            SandboxEditorPrimitiveViewCommand{
                                .StableEntityId =
                                    frame.CameraRender.PrimitiveViewStableId,
                                .SetEdgeView = true,
                                .EnableEdgeView = edgeView,
                            });
                    }

                    bool vertexView =
                        frame.CameraRender.PrimitiveView.EnableVertexView;
                    if (context != nullptr &&
                        ImGui::Checkbox("Mesh vertex view", &vertexView))
                    {
                        (void)ApplySandboxEditorPrimitiveViewCommand(
                            *context,
                            SandboxEditorPrimitiveViewCommand{
                                .StableEntityId =
                                    frame.CameraRender.PrimitiveViewStableId,
                                .SetVertexView = true,
                                .EnableVertexView = vertexView,
                            });
                    }
                }
                DrawDiagnostics(frame.CameraRender.Diagnostics);
            }
            ImGui::End();

            if (ImGui::Begin("Geometry Visualization"))
            {
                DrawDiagnostics(frame.Visualization.Diagnostics);
            }
            ImGui::End();
        }
    }

    const char* DebugNameForSandboxEditorDiagnosticCode(
        const SandboxEditorDiagnosticCode code) noexcept
    {
        switch (code)
        {
        case SandboxEditorDiagnosticCode::MissingScene:
            return "MissingScene";
        case SandboxEditorDiagnosticCode::MissingSelectionController:
            return "MissingSelectionController";
        case SandboxEditorDiagnosticCode::MissingImGuiAdapter:
            return "MissingImGuiAdapter";
        case SandboxEditorDiagnosticCode::AssetImportUnavailable:
            return "AssetImportUnavailable";
        case SandboxEditorDiagnosticCode::NoSelectedEntity:
            return "NoSelectedEntity";
        case SandboxEditorDiagnosticCode::UnsupportedGeometryDomain:
            return "UnsupportedGeometryDomain";
        case SandboxEditorDiagnosticCode::CameraRenderCommandsUnavailable:
            return "CameraRenderCommandsUnavailable";
        case SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable:
            return "VisualizationCommandsUnavailable";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorCommandStatus(
        const SandboxEditorCommandStatus status) noexcept
    {
        switch (status)
        {
        case SandboxEditorCommandStatus::Applied:
            return "Applied";
        case SandboxEditorCommandStatus::NoChange:
            return "NoChange";
        case SandboxEditorCommandStatus::MissingScene:
            return "MissingScene";
        case SandboxEditorCommandStatus::MissingSelectionController:
            return "MissingSelectionController";
        case SandboxEditorCommandStatus::MissingCameraControllerRegistry:
            return "MissingCameraControllerRegistry";
        case SandboxEditorCommandStatus::MissingPrimitiveViewCommands:
            return "MissingPrimitiveViewCommands";
        case SandboxEditorCommandStatus::StaleEntity:
            return "StaleEntity";
        case SandboxEditorCommandStatus::MissingTransform:
            return "MissingTransform";
        case SandboxEditorCommandStatus::UnsupportedGeometryDomain:
            return "UnsupportedGeometryDomain";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorGeometryDomain(
        const GS::Domain domain) noexcept
    {
        switch (domain)
        {
        case GS::Domain::None:
            return "None";
        case GS::Domain::Mesh:
            return "Mesh";
        case GS::Domain::Graph:
            return "Graph";
        case GS::Domain::PointCloud:
            return "PointCloud";
        case GS::Domain::Unknown:
            return "Unknown";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorPrimitiveKind(
        const RefinedPrimitiveKind kind) noexcept
    {
        switch (kind)
        {
        case RefinedPrimitiveKind::None:
            return "None";
        case RefinedPrimitiveKind::Entity:
            return "Entity";
        case RefinedPrimitiveKind::Face:
            return "Face";
        case RefinedPrimitiveKind::Edge:
            return "Edge";
        case RefinedPrimitiveKind::Vertex:
            return "Vertex";
        case RefinedPrimitiveKind::Point:
            return "Point";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorCameraControllerKind(
        const Core::Config::CameraControllerKind kind) noexcept
    {
        switch (kind)
        {
        case Core::Config::CameraControllerKind::Orbit:
            return "Orbit";
        case Core::Config::CameraControllerKind::Fly:
            return "Fly";
        case Core::Config::CameraControllerKind::FreeLook:
            return "FreeLook";
        case Core::Config::CameraControllerKind::TopDown:
            return "TopDown";
        }
        return "Unknown";
    }

    SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context)
    {
        SandboxEditorPanelFrame frame{};
        if (context.Scene == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingScene,
                          "Scene registry is unavailable.");
        }
        else
        {
            const entt::registry& raw = context.Scene->Raw();
            raw.view<entt::entity>().each(
                [&frame, &raw](const ECS::EntityHandle entity)
                {
                    frame.Hierarchy.push_back(BuildEntityRow(raw, entity));
                });
            std::sort(frame.Hierarchy.begin(),
                      frame.Hierarchy.end(),
                      [](const SandboxEditorEntityRow& lhs,
                         const SandboxEditorEntityRow& rhs)
                      {
                          if (lhs.StableEntityId != rhs.StableEntityId)
                              return lhs.StableEntityId < rhs.StableEntityId;
                          return lhs.Name < rhs.Name;
                      });
        }

        if (context.Selection == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingSelectionController,
                          "Selection controller is unavailable.");
        }
        if (!context.ImGuiAdapterAvailable)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingImGuiAdapter,
                          "Runtime ImGui adapter is unavailable.");
        }

        frame.Inspector = BuildInspectorModel(context);
        frame.Selection = BuildSelectionModel(context);
        frame.FileImport = BuildFileImportModel(context);
        frame.CameraRender = BuildCameraRenderModel(context);
        frame.Visualization = BuildVisualizationModel(context);
        return frame;
    }

    bool SelectSandboxEditorEntity(const SandboxEditorContext& context,
                                   const std::uint32_t stableEntityId)
    {
        if (context.Scene == nullptr || context.Selection == nullptr)
            return false;
        return context.Selection->SetSelectedByStableEntityId(*context.Scene,
                                                              stableEntityId);
    }

    SandboxEditorCommandStatus ApplySandboxEditorTransformEdit(
        const SandboxEditorContext& context,
        const SandboxEditorTransformEditCommand& command)
    {
        if (!command.SetPosition && !command.SetRotation && !command.SetScale)
            return SandboxEditorCommandStatus::NoChange;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (context.Selection == nullptr)
            return SandboxEditorCommandStatus::MissingSelectionController;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        auto* transform = raw.try_get<ECSC::Transform::Component>(entity);
        if (transform == nullptr)
            return SandboxEditorCommandStatus::MissingTransform;

        if (command.SetPosition)
            transform->Position = command.Position;
        if (command.SetRotation)
            transform->Rotation = command.Rotation;
        if (command.SetScale)
            transform->Scale = command.Scale;
        raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(entity);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorCameraControllerCommand(
        const SandboxEditorContext& context,
        const SandboxEditorCameraControllerCommand& command)
    {
        if (context.CameraControllers == nullptr)
            return SandboxEditorCommandStatus::MissingCameraControllerRegistry;

        ICameraController* existing =
            context.CameraControllers->ResolveOrNull(command.Slot);
        if (existing != nullptr && existing->Kind() == command.Kind &&
            command.PreserveCurrentView)
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        Graphics::CameraViewInput seed{};
        if (command.PreserveCurrentView && existing != nullptr)
        {
            seed = existing->GetView(
                SafeViewport(command.Viewport, context.CameraViewport));
        }

        context.CameraControllers->Replace(
            command.Slot,
            CreateCameraController(command.Kind, seed));
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorPrimitiveViewCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPrimitiveViewCommand& command)
    {
        if (!command.SetEdgeView && !command.SetVertexView)
            return SandboxEditorCommandStatus::NoChange;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (!context.PrimitiveViewCommands.Available())
            return SandboxEditorCommandStatus::MissingPrimitiveViewCommands;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        if (view.ActiveDomain != GS::Domain::Mesh)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        SandboxEditorPrimitiveViewSettings settings =
            context.PrimitiveViewCommands.GetSettings(command.StableEntityId);
        const SandboxEditorPrimitiveViewSettings prior = settings;
        if (command.SetEdgeView)
            settings.EnableEdgeView = command.EnableEdgeView;
        if (command.SetVertexView)
            settings.EnableVertexView = command.EnableVertexView;

        if (SamePrimitiveViewSettings(prior, settings))
            return SandboxEditorCommandStatus::NoChange;
        if (settings.AnyEnabled())
            context.PrimitiveViewCommands.SetSettings(command.StableEntityId, settings);
        else
            context.PrimitiveViewCommands.ClearSettings(command.StableEntityId);
        return SandboxEditorCommandStatus::Applied;
    }

    void DrawSandboxEditorPanelFrame(const SandboxEditorPanelFrame& frame)
    {
        DrawPanelFrame(frame, nullptr);
    }

    SandboxEditorUi::~SandboxEditorUi()
    {
        Detach();
    }

    void SandboxEditorUi::Attach(Engine& engine)
    {
        Detach();
        m_Engine = &engine;
        engine.SetImGuiEditorCallback(
            [this]
            {
                if (m_Engine == nullptr)
                    return;
                const SandboxEditorContext context = BuildContextFromEngine(*m_Engine);
                m_LastFrame = BuildSandboxEditorPanelFrame(context);
                DrawPanelFrame(m_LastFrame, &context);
            });
    }

    void SandboxEditorUi::Detach()
    {
        if (m_Engine != nullptr)
        {
            m_Engine->SetImGuiEditorCallback({});
            m_Engine = nullptr;
        }
    }
}
