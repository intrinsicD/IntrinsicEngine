module;
#include <entt/entity/fwd.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.SceneEditingOperations;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"
#include "Editor/internal/Runtime.EditorRenderHintHelpers.hpp"

#include "Editor/internal/Runtime.EditorFeatureCommands.Internal.hpp"

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"

namespace Extrinsic::Runtime {
namespace {
        using EditorFeatureDetail::BuildImportSuccessMessage;
        using EditorFeatureDetail::BuildImportPendingMessage;
        using EditorFeatureDetail::BuildImportFailureMessage;
        using EditorFeatureDetail::BuildSceneFileSuccessMessage;
        using EditorFeatureDetail::BuildSceneFileFailureMessage;
        using EditorFeatureDetail::BuildSceneFilePendingMessage;
        using EditorFeatureDetail::ExecuteEditorTransformMutation;
        using EditorFeatureDetail::ToEditorCommandStatus;
        using EditorFeatureDetail::EvaluateFileImportPrerequisites;
        using EditorFeatureDetail::FileImportPrerequisiteEvaluation;
        using EditorFeatureDetail::EditorRenderHintComponents;
        using EditorFeatureDetail::ReadRenderHintComponents;
        using EditorFeatureDetail::SameRenderHintComponents;
        using EditorFeatureDetail::ApplyRenderHintComponents;
        namespace ECSC = Extrinsic::ECS::Components;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;

        [[nodiscard]] bool IsFinitePositive(const float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }
        struct EditorRenderHintMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        [[nodiscard]] EditorCommandHistoryResult ExecuteEditorRenderHintMutation(
            EditorCommandHistory& history,
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const EditorRenderHintComponents& before,
            const EditorRenderHintComponents& after)
        {
            return Internal::ExecuteUndoableEntityMutation(
                history,
                "Change Render Hints",
                EditorRenderHintMutationIdentity{
                    .Scene = scene,
                    .World = world,
                    .StableEntityId = stableEntityId,
                },
                before,
                before,
                after,
                [](
                    const EditorRenderHintMutationIdentity& identity,
                    const EditorRenderHintComponents& expected,
                    const EditorRenderHintComponents&)
                {
                    if (identity.Scene == nullptr || !identity.World.IsValid())
                        return EditorCommandHistoryStatus::MissingScene;

                    const entt::registry& raw = identity.Scene->Raw();
                    const ECS::EntityHandle entity =
                        SelectionController::ToEntityHandle(
                            identity.StableEntityId);
                    if (entity == ECS::InvalidEntityHandle ||
                        !raw.valid(entity))
                    {
                        return EditorCommandHistoryStatus::StaleEntity;
                    }
                    return SameRenderHintComponents(
                               ReadRenderHintComponents(raw, entity),
                               expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                },
                [](
                    const EditorRenderHintMutationIdentity& identity,
                    const EditorRenderHintComponents& target)
                {
                    return ApplyRenderHintComponents(
                        identity.Scene,
                        identity.StableEntityId,
                        target);
                },
                [](
                    const EditorRenderHintMutationIdentity&,
                    const EditorRenderHintComponents&,
                    const EditorRenderHintComponents& target)
                {
                    return target;
                });
        }
        [[nodiscard]] G::RenderPoints::RenderType ToRenderPointType(
            const MeshVertexViewRenderMode mode) noexcept
        {
            switch (mode)
            {
            case MeshVertexViewRenderMode::FlatCircle:
                return G::RenderPoints::RenderType::Flat;
            case MeshVertexViewRenderMode::SurfaceAlignedCircle:
                return G::RenderPoints::RenderType::Surfel;
            case MeshVertexViewRenderMode::ImpostorSphere:
                return G::RenderPoints::RenderType::Sphere;
            }
            return G::RenderPoints::RenderType::Sphere;
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

        void InvalidateSelectedModelCache(const EditorSceneEditingContext& context)
        {
            if (context.InvalidateWorkspaceSnapshotCache)
                context.InvalidateWorkspaceSnapshotCache();
        }

        [[nodiscard]] EditorCommandStatus
        InvalidateSelectedModelCacheIfApplied(const EditorSceneEditingContext& context,
                                              const EditorCommandStatus status)
        {
            if (status == EditorCommandStatus::Applied)
                InvalidateSelectedModelCache(context);
            return status;
        }
} // namespace

    bool SelectEditorEntity(const EditorSceneEditingContext& context,
                                   const std::uint32_t stableEntityId)
    {
        if (context.Scene == nullptr || context.Selection == nullptr)
            return false;
        if (context.CommandHistory != nullptr)
        {
            std::optional<std::uint32_t> before{};
            const auto selected = context.Selection->SelectedStableIds();
            if (selected.size() == 1u)
                before = selected.front();
            else if (!selected.empty())
            {
                const bool changed =
                    context.Selection->SetSelectedByStableEntityId(
                        *context.Scene,
                        stableEntityId);
                if (changed)
                    InvalidateSelectedModelCache(context);
                return changed;
            }

            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    MakeSelectionReplaceCommand(
                        EditorSelectionReplaceCommand{
                            .Scene = context.Scene,
                            .Selection = context.Selection,
                            .BeforeStableEntityId = before,
                            .AfterStableEntityId = stableEntityId,
                            .Label = "Select Entity",
                        }));
            if (result.Succeeded())
                InvalidateSelectedModelCache(context);
            return result.Succeeded();
        }
        const bool changed =
            context.Selection->SetSelectedByStableEntityId(*context.Scene,
                                                           stableEntityId);
        if (changed)
            InvalidateSelectedModelCache(context);
        return changed;
    }

    EditorFileImportResult
ApplyEditorFileImportCommand(
        const EditorSceneEditingContext& context,
        const EditorFileImportCommand& command)
    {
        const FileImportPrerequisiteEvaluation prerequisites =
            EvaluateFileImportPrerequisites(
                context.AssetImportCommands.Available(),
                command.Path,
                command.PayloadKind);
        if (!prerequisites.CanImport)
        {
            return EditorFileImportResult{
                .Status = context.AssetImportCommands.Available()
                    ? EditorCommandStatus::AssetImportFailed
                    : EditorCommandStatus::MissingAssetImportCommands,
                .PayloadKind = prerequisites.ResolvedPayloadKind ==
                        A::AssetPayloadKind::Unknown
                    ? command.PayloadKind
                    : prerequisites.ResolvedPayloadKind,
                .Error = prerequisites.Error,
                .Message = prerequisites.ImportDisabledReason,
            };
        }

        EditorFileImportCommand resolvedCommand = command;
        resolvedCommand.PayloadKind = prerequisites.ResolvedPayloadKind;
        EditorFileImportResult result =
            context.AssetImportCommands.Import(resolvedCommand);
        if (result.Status == EditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildImportSuccessMessage(resolvedCommand, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Status == EditorCommandStatus::Pending)
        {
            if (result.Message.empty())
                result.Message = BuildImportPendingMessage(
                    resolvedCommand,
                    result.PayloadKind);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildImportFailureMessage(result.Error);
        }
        return result;
    }

    EditorSceneFileResult
ApplyEditorSceneSaveCommand(
        const EditorSceneEditingContext& context,
        const EditorSceneFileCommand& command)
    {
        if (!context.SceneFileCommands.Available())
        {
            return EditorSceneFileResult{
                .Status = EditorCommandStatus::MissingSceneFileCommands,
                .Operation = EditorSceneFileOperation::Save,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Scene file command surface is unavailable.",
            };
        }
        if (command.Path.empty())
        {
            return EditorSceneFileResult{
                .Status = EditorCommandStatus::SceneSaveFailed,
                .Operation = EditorSceneFileOperation::Save,
                .Error = Core::ErrorCode::InvalidPath,
                .Message = BuildSceneFileFailureMessage(
                    EditorSceneFileOperation::Save,
                    Core::ErrorCode::InvalidPath),
            };
        }

        EditorSceneFileResult result = context.SceneFileCommands.Save(command);
        result.Operation = EditorSceneFileOperation::Save;
        if (result.Status == EditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage(command, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Status == EditorCommandStatus::Pending)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFilePendingMessage(
                    command,
                    result.Operation);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation, result.Error);
        }
        return result;
    }

    EditorSceneFileResult
ApplyEditorSceneLoadCommand(
        const EditorSceneEditingContext& context,
        const EditorSceneFileCommand& command)
    {
        if (!context.SceneFileCommands.Available())
        {
            return EditorSceneFileResult{
                .Status = EditorCommandStatus::MissingSceneFileCommands,
                .Operation = EditorSceneFileOperation::Load,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Scene file command surface is unavailable.",
            };
        }
        if (command.Path.empty())
        {
            return EditorSceneFileResult{
                .Status = EditorCommandStatus::SceneLoadFailed,
                .Operation = EditorSceneFileOperation::Load,
                .Error = Core::ErrorCode::InvalidPath,
                .Message = BuildSceneFileFailureMessage(
                    EditorSceneFileOperation::Load,
                    Core::ErrorCode::InvalidPath),
            };
        }

        EditorSceneFileResult result = context.SceneFileCommands.Load(command);
        result.Operation = EditorSceneFileOperation::Load;
        if (result.Status == EditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage(command, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Status == EditorCommandStatus::Pending)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFilePendingMessage(
                    command,
                    result.Operation);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation, result.Error);
        }
        return result;
    }

    EditorSceneFileResult
ApplyEditorNewSceneCommand(
        const EditorSceneEditingContext& context)
    {
        if (!context.SceneFileCommands.New)
        {
            return EditorSceneFileResult{
                .Status = EditorCommandStatus::MissingSceneFileCommands,
                .Operation = EditorSceneFileOperation::New,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "New scene command surface is unavailable.",
            };
        }

        EditorSceneFileResult result = context.SceneFileCommands.New();
        result.Operation = EditorSceneFileOperation::New;
        if (result.Status == EditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage({}, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation,
                                                          result.Error);
        }
        return result;
    }

    EditorSceneFileResult
ApplyEditorCloseSceneCommand(
        const EditorSceneEditingContext& context)
    {
        if (!context.SceneFileCommands.Close)
        {
            return EditorSceneFileResult{
                .Status = EditorCommandStatus::MissingSceneFileCommands,
                .Operation = EditorSceneFileOperation::Close,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Close scene command surface is unavailable.",
            };
        }

        EditorSceneFileResult result = context.SceneFileCommands.Close();
        result.Operation = EditorSceneFileOperation::Close;
        if (result.Status == EditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage({}, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation,
                                                          result.Error);
        }
        return result;
    }

    EditorCommandStatus
ApplyEditorTransformEdit(
        const EditorSceneEditingContext& context,
        const EditorTransformEditCommand& command)
    {
        if (!command.SetPosition && !command.SetRotation && !command.SetScale)
            return EditorCommandStatus::NoChange;
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;
        if (context.Selection == nullptr)
            return EditorCommandStatus::MissingSelectionController;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;

        auto* transform = raw.try_get<ECSC::Transform::Component>(entity);
        if (transform == nullptr)
            return EditorCommandStatus::MissingTransform;

        if (context.CommandHistory != nullptr)
        {
            ECSC::Transform::Component next = *transform;
            if (command.SetPosition)
                next.Position = command.Position;
            if (command.SetRotation)
                next.Rotation = command.Rotation;
            if (command.SetScale)
                next.Scale = command.Scale;

            const EditorCommandHistoryResult result =
                ExecuteEditorTransformMutation(
                    *context.CommandHistory,
                    context.Scene,
                    context.World,
                    command.StableEntityId,
                    *transform,
                    next,
                    "Edit Transform");
            return ToEditorCommandStatus(result.Status);
        }

        if (command.SetPosition)
            transform->Position = command.Position;
        if (command.SetRotation)
            transform->Rotation = command.Rotation;
        if (command.SetScale)
            transform->Scale = command.Scale;
        raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(entity);
        return EditorCommandStatus::Applied;
    }

    EditorCommandStatus ApplyEditorCameraControllerCommand(
        const EditorSceneEditingContext& context,
        const EditorCameraControllerCommand& command)
    {
        if (context.CameraControllers == nullptr)
            return EditorCommandStatus::MissingCameraControllerRegistry;

        ICameraController* existing =
            context.CameraControllers->ResolveOrNull(command.Slot);
        if (existing != nullptr && existing->Kind() == command.Kind &&
            command.PreserveCurrentView)
        {
            return EditorCommandStatus::NoChange;
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
        return EditorCommandStatus::Applied;
    }

    namespace
    {
        [[nodiscard]] bool IsFiniteVec3(const glm::vec3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] EditorCameraPose PoseOf(const ICameraController& controller, const Core::Extent2D viewport)
        {
            const Graphics::CameraViewInput view = controller.GetView(viewport);
            return EditorCameraPose{view.Position, view.Forward, view.Up};
        }

        // Resolves stable ids to live entities; false when one is stale.
        [[nodiscard]] bool ResolveStableEntities(const entt::registry& raw,
                                                 const std::vector<std::uint32_t>& ids,
                                                 std::vector<ECS::EntityHandle>& out)
        {
            out.reserve(ids.size());
            for (const std::uint32_t id : ids)
            {
                const ECS::EntityHandle entity = SelectionController::ToEntityHandle(id);
                if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                    return false;
                out.push_back(entity);
            }
            return true;
        }

        // Up with its component along `forward` removed, normalized; zero when degenerate.
        [[nodiscard]] glm::vec3 RollFreeUp(const glm::vec3& up, const glm::vec3& forward) noexcept
        {
            const glm::dvec3 f{forward};
            glm::dvec3 u = glm::dvec3{up} - f * glm::dot(glm::dvec3{up}, f);
            const double length = glm::length(u);
            return length > 1.0e-4 ? glm::vec3{u / length} : glm::vec3{0.0f};
        }

        [[nodiscard]] bool DirectionRealized(const EditorCameraPose& got, const glm::vec3& forward) noexcept
        {
            // 4e-4 admits the 1 degree the pitch-limited controllers stop short of the poles.
            return glm::dot(glm::normalize(got.Forward), forward) >= 1.0f - 4.0e-4f;
        }

        [[nodiscard]] bool UpDiffers(const EditorCameraPose& got, const glm::vec3& forward, const glm::vec3& up) noexcept
        {
            const glm::vec3 gotUp = RollFreeUp(got.Up, forward);
            const glm::vec3 wantUp = RollFreeUp(up, forward);
            if (gotUp == glm::vec3{0.0f} || wantUp == glm::vec3{0.0f})
                return false; // looking along the up axis: roll is not observable
            return glm::dot(gotUp, wantUp) < 1.0f - 1.0e-4f;
        }
    }

    EditorCameraPoseResult ApplyEditorCameraPoseCommand(
        const EditorSceneEditingContext& context,
        const EditorCameraPoseCommand& command)
    {
        EditorCameraPoseResult result{};
        if (command.Mode != EditorCameraPoseMode::Pose && command.Mode != EditorCameraPoseMode::Preset &&
            command.Mode != EditorCameraPoseMode::Focus)
        {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            return result;
        }
        if (command.Mode == EditorCameraPoseMode::Preset &&
            static_cast<std::uint8_t>(command.Preset) > static_cast<std::uint8_t>(CameraViewPreset::Isometric))
        {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            return result;
        }
        if (context.CameraControllers == nullptr)
        {
            result.Status = EditorCommandStatus::MissingCameraControllerRegistry;
            return result;
        }
        ICameraController* controller = context.CameraControllers->ResolveOrNull(command.Slot);
        if (controller == nullptr)
        {
            result.Status = EditorCommandStatus::MissingCameraControllerRegistry;
            return result;
        }
        const Core::Extent2D viewport = SafeViewport(command.Viewport, context.CameraViewport);
        result.HasPose = true;
        result.Previous = result.Current = PoseOf(*controller, viewport);
        CameraControllerRegistry& cameras = *context.CameraControllers;

        // Tries `apply` on a clone first: refuses (touching nothing) when the controller cannot look
        // along `forward`, otherwise applies it for real and reports what was adjusted.
        const auto applyChecked = [&](const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up,
                                      const bool checkPosition, auto&& apply)
        {
            const std::unique_ptr<ICameraController> trial = controller->Clone();
            if (trial != nullptr)
            {
                apply(*trial);
                const EditorCameraPose tried = PoseOf(*trial, viewport);
                if (!DirectionRealized(tried, forward))
                {
                    result.Status = EditorCommandStatus::UnsupportedCameraPose;
                    return;
                }
                result.UpIgnored = UpDiffers(tried, forward, up);
                result.PositionClamped = checkPosition &&
                    glm::length(tried.Position - position) > 1.0e-3f * std::max(1.0f, glm::length(position));
            }
            apply(*controller);
            cameras.MarkCameraTransition(command.Slot);
            result.Current = PoseOf(*controller, viewport);
            result.Status = EditorCommandStatus::Applied;
        };

        if (command.Mode == EditorCameraPoseMode::Pose)
        {
            if (!IsFiniteVec3(command.Position) || !IsFiniteVec3(command.Target) || !IsFiniteVec3(command.Up))
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                return result;
            }
            const glm::dvec3 toTarget = glm::dvec3{command.Target} - glm::dvec3{command.Position};
            const double distance = glm::length(toTarget);
            if (!(distance > 1.0e-6))
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                return result;
            }
            const glm::vec3 forward = glm::vec3{toTarget / distance};
            const glm::vec3 up = RollFreeUp(command.Up, forward);
            if (up == glm::vec3{0.0f})
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters; // zero or parallel up
                return result;
            }
            applyChecked(command.Position, forward, up, true,
                         [&](ICameraController& camera) { camera.LookAt(command.Position, command.Target, up); });
            return result;
        }

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            return result;
        }
        const entt::registry& raw = context.Scene->Raw();

        std::vector<ECS::EntityHandle> entities;
        std::vector<std::uint32_t> ids = command.StableEntityIds;
        if (command.Mode == EditorCameraPoseMode::Focus && ids.empty())
        {
            if (context.Selection == nullptr)
            {
                result.Status = EditorCommandStatus::MissingSelectionController;
                return result;
            }
            const auto selected = context.Selection->SelectedStableIds();
            ids.assign(selected.begin(), selected.end());
            if (ids.empty())
            {
                result.Status = EditorCommandStatus::NoChange;
                return result;
            }
        }
        if (!ids.empty())
        {
            if (!ResolveStableEntities(raw, ids, entities))
            {
                result.Status = EditorCommandStatus::StaleEntity;
                return result;
            }
        }
        else
        {
            for (const ECS::EntityHandle entity : raw.view<ECS::Components::Culling::World::Bounds>())
                entities.push_back(entity);
        }

        const std::optional<CameraFocusTarget> target = ComputeFocusTargetForEntities(*context.Scene, entities);
        if (!target.has_value())
        {
            result.Status = EditorCommandStatus::NoChange;
            return result;
        }

        if (command.Mode == EditorCameraPoseMode::Preset)
        {
            const CameraPresetAxes axes = CameraPresetAxesFor(command.Preset);
            applyChecked({}, axes.Forward, axes.Up, false,
                         [&](ICameraController& camera) { SeedCameraPreset(camera, command.Preset, *target, viewport); });
        }
        else
        {
            ApplyCameraFocus(cameras, command.Slot, *target);
            result.Current = PoseOf(*controller, viewport);
            result.Status = EditorCommandStatus::Applied;
        }
        return result;
    }

    EditorCommandStatus
ApplyEditorPrimitiveViewCommand(
        const EditorSceneEditingContext& context,
        const EditorPrimitiveViewCommand& command)
    {
        if (!command.SetEdgeView &&
            !command.SetVertexView &&
            !command.SetVertexRenderMode &&
            !command.SetVertexPointRadius)
        {
            return EditorCommandStatus::NoChange;
        }
        if (context.Scene == nullptr)
            return EditorCommandStatus::MissingScene;
        if (command.SetVertexPointRadius &&
            !IsFinitePositive(command.VertexPointRadiusPx))
        {
            return EditorCommandStatus::InvalidProcessingParameters;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return EditorCommandStatus::StaleEntity;

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
            return EditorCommandStatus::UnsupportedGeometryDomain;
        if ((command.SetVertexView && command.EnableVertexView) ||
            command.SetVertexRenderMode ||
            command.SetVertexPointRadius)
        {
            if (!availability.Sources.Has(GS::SourceCapability::Vertices))
                return EditorCommandStatus::UnsupportedGeometryDomain;
        }
        if (command.SetEdgeView && command.EnableEdgeView)
        {
            const bool hasExplicitEdges =
                availability.Sources.Has(GS::SourceCapability::Edges);
            const bool hasMeshWireTopology =
                availability.Sources.Has(GS::SourceCapability::Halfedges) &&
                availability.Sources.Has(GS::SourceCapability::Faces);
            if (!availability.Sources.Has(GS::SourceCapability::Vertices) ||
                (!hasExplicitEdges && !hasMeshWireTopology))
            {
                return EditorCommandStatus::UnsupportedGeometryDomain;
            }
        }

        const EditorRenderHintComponents before =
            ReadRenderHintComponents(raw, entity);
        EditorRenderHintComponents after = before;
        if (command.SetEdgeView)
        {
            if (command.EnableEdgeView)
            {
                after.Edges = after.Edges.value_or(G::RenderEdges{});
            }
            else
            {
                after.Edges.reset();
            }
        }
        if (command.SetVertexView)
        {
            if (command.EnableVertexView)
            {
                after.Points = after.Points.value_or(G::RenderPoints{});
            }
            else
            {
                after.Points.reset();
            }
        }
        if (after.Points.has_value())
        {
            if (command.SetVertexRenderMode)
                after.Points->Type = ToRenderPointType(command.VertexRenderMode);
            if (command.SetVertexPointRadius)
                after.Points->SizeSource = command.VertexPointRadiusPx;
        }

        if (SameRenderHintComponents(before, after))
            return EditorCommandStatus::NoChange;
        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult result =
                ExecuteEditorRenderHintMutation(
                    *context.CommandHistory,
                    context.Scene,
                    context.World,
                    command.StableEntityId,
                    before,
                    after);
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToEditorCommandStatus(result.Status));
        }
        return InvalidateSelectedModelCacheIfApplied(
            context,
            ToEditorCommandStatus(
                ApplyRenderHintComponents(context.Scene, command.StableEntityId, after)));
    }

} // namespace Extrinsic::Runtime
