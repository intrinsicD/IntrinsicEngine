module;

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ProgressivePoissonReference.hpp"

module Extrinsic.Runtime.SceneEditingOperations;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.GeometryPayload;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Structure;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.UvView;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetWorkflowRecipePolicies;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Graph;
import Geometry.Graph.Vertex.Normals;
import Geometry.Curvature;
import Geometry.CatmullClark;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.AdaptiveRemeshing;
import Geometry.HalfedgeMesh.SubdivisionSqrt3;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.Mesh.Conversion;
import Geometry.MeshOperator;
import Geometry.MeshSoup;
import Geometry.PointCloud;
import Geometry.PointCloud.Normals;
import Geometry.PointCloud.SurfaceSampling;
import Geometry.PointCloud.Utils;
import Geometry.Properties;
import Geometry.Registration;
import Geometry.Remeshing;
import Geometry.Simplification;
import Geometry.Smoothing;
import Geometry.Subdivision;
import Geometry.UvAtlas;

#include "Runtime.EditorMutation.Internal.hpp"

namespace Extrinsic::Runtime {
namespace {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;
        namespace GN = Geometry::HalfedgeMesh::VertexNormals;
        namespace GraphNormals = Geometry::Graph::VertexNormals;
        inline constexpr std::array<A::AssetPayloadKind, 6>
            kFileImportPayloadKinds{{
                A::AssetPayloadKind::Unknown,
                A::AssetPayloadKind::Mesh,
                A::AssetPayloadKind::PointCloud,
                A::AssetPayloadKind::Graph,
                A::AssetPayloadKind::ModelScene,
                A::AssetPayloadKind::Texture2D,
            }};

        inline constexpr std::string_view kImportSurfaceUnavailableReason =
            "Asset import requires an available runtime import command surface.";
        inline constexpr std::string_view kImportPathEmptyReason =
            "Enter an asset path before choosing a payload or importing.";
        inline constexpr std::string_view kImportExtensionMissingReason =
            "Add a supported file extension to the asset path before importing.";

        struct FileImportPrerequisiteEvaluation
        {
            bool CanChoosePayloadHint{false};
            bool CanImport{false};
            A::AssetPayloadKind ResolvedPayloadKind{
                A::AssetPayloadKind::Unknown};
            std::array<EditorFileImportPayloadOption, 6> PayloadOptions{};
            std::string PayloadHintDisabledReason{};
            std::string ImportDisabledReason{};
            Core::ErrorCode Error{Core::ErrorCode::Success};
        };

        [[nodiscard]] bool HasPromotedFileImporter(
            const A::AssetFileFormat format,
            const A::AssetPayloadKind payloadKind) noexcept
        {
            if (payloadKind == A::AssetPayloadKind::ModelScene)
                return A::IsSupportedModelSceneImportFormat(format);
            if (payloadKind == A::AssetPayloadKind::Texture2D)
                return A::IsSupportedTextureImportFormat(format);
            return true;
        }

        [[nodiscard]] std::string PayloadChoicesText(
            const std::span<const A::AssetPayloadKind> payloads)
        {
            std::string text{};
            for (std::size_t i = 0u; i < payloads.size(); ++i)
            {
                if (i > 0u)
                    text += i + 1u == payloads.size() ? " or " : ", ";
                text += A::DebugNameForAssetPayloadKind(payloads[i]);
            }
            return text;
        }

        [[nodiscard]] std::string BuildUnsupportedExtensionReason(
            const A::AssetRouteDiagnostic& diagnostic)
        {
            std::string reason = "Asset extension";
            if (!diagnostic.Extension.empty())
            {
                reason += " '.";
                reason += diagnostic.Extension;
                reason += "'";
            }
            reason +=
                " is unsupported; choose a path with a supported asset file extension.";
            return reason;
        }

        [[nodiscard]] std::string BuildIncompatiblePayloadReason(
            const A::AssetFileFormatInfo& format,
            const A::AssetPayloadKind payloadKind)
        {
            std::string reason = A::DebugNameForAssetFileFormat(format.Format);
            reason += " import ";
            const std::string choices = PayloadChoicesText(format.ImportPayloads);
            if (payloadKind == A::AssetPayloadKind::Unknown)
            {
                reason += "requires an explicit ";
                reason += choices;
                reason += " payload.";
                return reason;
            }

            if (format.ImportPayloads.size() == 1u)
            {
                reason += "requires the ";
                reason += choices;
                reason += " payload; ";
            }
            else
            {
                reason += "supports only ";
                reason += choices;
                reason += " payloads; ";
            }
            reason += A::DebugNameForAssetPayloadKind(payloadKind);
            reason += " is incompatible.";
            return reason;
        }

        [[nodiscard]] std::string BuildUnavailableImporterReason(
            const A::AssetFileFormatInfo& format)
        {
            std::string reason = A::DebugNameForAssetFileFormat(format.Format);
            reason += " import is unavailable because no promoted ";
            reason += PayloadChoicesText(format.ImportPayloads);
            reason +=
                " importer supports this format; choose a supported asset format.";
            return reason;
        }

        [[nodiscard]] FileImportPrerequisiteEvaluation
        EvaluateFileImportPrerequisites(
            const bool commandSurfaceAvailable,
            const std::string_view path,
            const A::AssetPayloadKind selectedPayloadKind)
        {
            FileImportPrerequisiteEvaluation evaluation{};
            for (std::size_t i = 0u; i < kFileImportPayloadKinds.size(); ++i)
                evaluation.PayloadOptions[i].Kind = kFileImportPayloadKinds[i];

            const auto disableAll = [&evaluation](const std::string_view reason,
                                                  const Core::ErrorCode error)
            {
                evaluation.PayloadHintDisabledReason = reason;
                evaluation.ImportDisabledReason = reason;
                evaluation.Error = error;
                for (EditorFileImportPayloadOption& option :
                     evaluation.PayloadOptions)
                {
                    option.DisabledReason = reason;
                }
            };

            if (!commandSurfaceAvailable)
            {
                disableAll(kImportSurfaceUnavailableReason,
                           Core::ErrorCode::InvalidState);
                return evaluation;
            }
            if (path.empty())
            {
                disableAll(kImportPathEmptyReason, Core::ErrorCode::InvalidPath);
                return evaluation;
            }

            const A::AssetRouteDiagnostic automaticDiagnostic =
                A::DiagnoseAssetImportRoute(
                    path,
                    A::AssetRouteOperation::Import,
                    A::AssetImportHint{
                        .PayloadKind = A::AssetPayloadKind::Unknown,
                    });
            if (automaticDiagnostic.Status == A::AssetRouteStatus::MissingExtension)
            {
                disableAll(kImportExtensionMissingReason,
                           automaticDiagnostic.Error);
                return evaluation;
            }
            if (automaticDiagnostic.Status ==
                A::AssetRouteStatus::UnsupportedExtension)
            {
                const std::string reason =
                    BuildUnsupportedExtensionReason(automaticDiagnostic);
                disableAll(reason, automaticDiagnostic.Error);
                return evaluation;
            }

            const A::AssetFileFormatInfo* format = A::FindAssetFileFormat(path);
            if (format == nullptr || format->ImportPayloads.empty())
            {
                const std::string reason =
                    automaticDiagnostic.Message.empty()
                        ? std::string{"The selected asset format has no supported import "
                                      "payload."}
                        : automaticDiagnostic.Message;
                disableAll(reason, automaticDiagnostic.Error);
                return evaluation;
            }

            const A::AssetRouteDiagnostic selectedDiagnostic =
                A::DiagnoseAssetImportRoute(
                    path,
                    A::AssetRouteOperation::Import,
                    A::AssetImportHint{.PayloadKind = selectedPayloadKind});
            if (selectedDiagnostic.Status == A::AssetRouteStatus::Ready)
            {
                const auto selectedRoute = A::ResolveAssetImportRoute(
                    path,
                    A::AssetRouteOperation::Import,
                    A::AssetImportHint{.PayloadKind = selectedPayloadKind});
                if (selectedRoute.has_value())
                    evaluation.ResolvedPayloadKind = selectedRoute->PayloadKind;
            }

            const bool promotedImporterAvailable =
                std::ranges::any_of(
                    format->ImportPayloads,
                    [format](const A::AssetPayloadKind payloadKind)
                    {
                        return HasPromotedFileImporter(format->Format,
                                                       payloadKind);
                    });
            if (!promotedImporterAvailable)
            {
                const std::string reason = BuildUnavailableImporterReason(*format);
                disableAll(reason, Core::ErrorCode::AssetUnsupportedFormat);
                return evaluation;
            }

            evaluation.CanChoosePayloadHint = true;
            for (EditorFileImportPayloadOption& option :
                 evaluation.PayloadOptions)
            {
                const A::AssetRouteDiagnostic optionDiagnostic =
                    A::DiagnoseAssetImportRoute(
                        path,
                        A::AssetRouteOperation::Import,
                        A::AssetImportHint{.PayloadKind = option.Kind});
                if (optionDiagnostic.Status != A::AssetRouteStatus::Ready)
                {
                    option.DisabledReason =
                        BuildIncompatiblePayloadReason(*format, option.Kind);
                    continue;
                }

                const auto optionRoute = A::ResolveAssetImportRoute(
                    path,
                    A::AssetRouteOperation::Import,
                    A::AssetImportHint{.PayloadKind = option.Kind});
                if (!optionRoute.has_value() ||
                    !HasPromotedFileImporter(optionRoute->Format,
                                             optionRoute->PayloadKind))
                {
                    option.DisabledReason = BuildUnavailableImporterReason(*format);
                    continue;
                }
                option.Enabled = true;
            }

            const auto selectedOption = std::ranges::find(
                evaluation.PayloadOptions,
                selectedPayloadKind,
                &EditorFileImportPayloadOption::Kind);
            if (selectedOption == evaluation.PayloadOptions.end())
            {
                evaluation.ImportDisabledReason =
                    "Select a supported payload hint before importing.";
                evaluation.Error = Core::ErrorCode::InvalidArgument;
                return evaluation;
            }
            if (!selectedOption->Enabled)
            {
                evaluation.ImportDisabledReason = selectedOption->DisabledReason;
                evaluation.Error = selectedDiagnostic.Error == Core::ErrorCode::Success
                    ? Core::ErrorCode::AssetUnsupportedFormat
                    : selectedDiagnostic.Error;
                return evaluation;
            }

            evaluation.CanImport = true;
            evaluation.Error = Core::ErrorCode::Success;
            return evaluation;
        }
        [[nodiscard]] std::string ErrorName(const Core::ErrorCode error)
        {
            return std::string(Core::Error::ToString(error));
        }

        [[nodiscard]] std::string BuildImportSuccessMessage(
            const EditorFileImportCommand& command,
            const EditorFileImportResult& result)
        {
            std::string message = "Imported ";
            message += A::DebugNameForAssetPayloadKind(result.PayloadKind);
            message += " asset";
            if (!command.Path.empty())
            {
                message += " from ";
                message += command.Path;
            }
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildImportPendingMessage(
            const EditorFileImportCommand& command,
            const A::AssetPayloadKind payloadKind)
        {
            std::string message = "Queued ";
            message += A::DebugNameForAssetPayloadKind(payloadKind);
            message += " asset import";
            if (!command.Path.empty())
            {
                message += " from ";
                message += command.Path;
            }
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildImportFailureMessage(
            const Core::ErrorCode error)
        {
            std::string message = "Asset import failed: ";
            message += ErrorName(error);
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildSceneFileSuccessMessage(
            const EditorSceneFileCommand& command,
            const EditorSceneFileResult& result)
        {
            std::string message{};
            switch (result.Operation)
            {
            case EditorSceneFileOperation::New:
                message = "Created new scene";
                break;
            case EditorSceneFileOperation::Save:
                message = "Saved scene";
                break;
            case EditorSceneFileOperation::Load:
                message = "Opened scene";
                break;
            case EditorSceneFileOperation::Close:
                message = "Closed scene";
                break;
            }
            if (!command.Path.empty())
            {
                if (result.Operation == EditorSceneFileOperation::Save)
                    message += " to ";
                else if (result.Operation == EditorSceneFileOperation::Load)
                    message += " from ";
                else
                    message += " ";
                message += command.Path;
            }
            message += " (entities=";
            message += std::to_string(result.Stats.Entities);
            message += ", mesh=";
            message += std::to_string(result.Stats.MeshEntities);
            message += ", graph=";
            message += std::to_string(result.Stats.GraphEntities);
            message += ", pointCloud=";
            message += std::to_string(result.Stats.PointCloudEntities);
            message += ").";
            return message;
        }

        [[nodiscard]] std::string BuildSceneFileFailureMessage(
            const EditorSceneFileOperation operation,
            const Core::ErrorCode error)
        {
            std::string message{};
            switch (operation)
            {
            case EditorSceneFileOperation::New:
                message = "Scene new failed: ";
                break;
            case EditorSceneFileOperation::Save:
                message = "Scene save failed: ";
                break;
            case EditorSceneFileOperation::Load:
                message = "Scene open failed: ";
                break;
            case EditorSceneFileOperation::Close:
                message = "Scene close failed: ";
                break;
            }
            message += ErrorName(error);
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildSceneFilePendingMessage(
            const EditorSceneFileCommand& command,
            const EditorSceneFileOperation operation)
        {
            std::string message{};
            switch (operation)
            {
            case EditorSceneFileOperation::New:
                message = "Queued scene new";
                break;
            case EditorSceneFileOperation::Save:
                message = "Queued scene save";
                break;
            case EditorSceneFileOperation::Load:
                message = "Queued scene open";
                break;
            case EditorSceneFileOperation::Close:
                message = "Queued scene close";
                break;
            }
            if (!command.Path.empty())
            {
                if (operation == EditorSceneFileOperation::Save)
                    message += " to ";
                else if (operation == EditorSceneFileOperation::Load)
                    message += " from ";
                else
                    message += " ";
                message += command.Path;
            }
            message += ".";
            return message;
        }
        struct EditorRenderHintState
        {
            std::optional<G::RenderSurface> Surface{};
            std::optional<G::RenderEdges> Edges{};
            std::optional<G::RenderPoints> Points{};
        };

        [[nodiscard]] EditorRenderHintState ReadRenderHintState(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            EditorRenderHintState state{};
            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
                state.Surface = *surface;
            if (const auto* lines = raw.try_get<G::RenderEdges>(entity))
                state.Edges = *lines;
            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
                state.Points = *points;
            return state;
        }

        [[nodiscard]] bool SameRenderSurface(
            const G::RenderSurface& lhs,
            const G::RenderSurface& rhs) noexcept
        {
            return lhs.Domain == rhs.Domain;
        }

        [[nodiscard]] bool SameRenderScalarSource(
            const std::variant<float, std::string>& lhs,
            const std::variant<float, std::string>& rhs) noexcept
        {
            if (lhs.index() != rhs.index())
                return false;
            if (const auto* lhsUniform = std::get_if<float>(&lhs))
            {
                const auto* rhsUniform = std::get_if<float>(&rhs);
                return rhsUniform != nullptr &&
                       std::bit_cast<std::uint32_t>(*lhsUniform) ==
                           std::bit_cast<std::uint32_t>(*rhsUniform);
            }
            return std::get<std::string>(lhs) == std::get<std::string>(rhs);
        }

        [[nodiscard]] bool SameRenderEdges(
            const G::RenderEdges& lhs,
            const G::RenderEdges& rhs)
        {
            return lhs.Domain == rhs.Domain &&
                   SameRenderScalarSource(lhs.WidthSource, rhs.WidthSource);
        }

        [[nodiscard]] bool SameRenderPoints(
            const G::RenderPoints& lhs,
            const G::RenderPoints& rhs)
        {
            return lhs.Type == rhs.Type &&
                   SameRenderScalarSource(lhs.SizeSource, rhs.SizeSource);
        }

        template <typename T, typename SameFn>
        [[nodiscard]] bool SameOptionalRenderComponent(
            const std::optional<T>& lhs,
            const std::optional<T>& rhs,
            SameFn same)
        {
            if (lhs.has_value() != rhs.has_value())
                return false;
            if (!lhs.has_value())
                return true;
            return same(*lhs, *rhs);
        }

        [[nodiscard]] bool SameRenderHintState(
            const EditorRenderHintState& lhs,
            const EditorRenderHintState& rhs)
        {
            return SameOptionalRenderComponent(
                       lhs.Surface, rhs.Surface, SameRenderSurface) &&
                   SameOptionalRenderComponent(
                       lhs.Edges, rhs.Edges, SameRenderEdges) &&
                   SameOptionalRenderComponent(
                       lhs.Points, rhs.Points, SameRenderPoints);
        }

        [[nodiscard]] bool IsFinitePositive(const float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }
        [[nodiscard]] EditorCommandHistoryStatus ApplyRenderHintState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const EditorRenderHintState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            if (state.Surface.has_value())
                raw.emplace_or_replace<G::RenderSurface>(entity, *state.Surface);
            else if (raw.all_of<G::RenderSurface>(entity))
                raw.remove<G::RenderSurface>(entity);

            if (state.Edges.has_value())
                raw.emplace_or_replace<G::RenderEdges>(entity, *state.Edges);
            else if (raw.all_of<G::RenderEdges>(entity))
                raw.remove<G::RenderEdges>(entity);

            if (state.Points.has_value())
                raw.emplace_or_replace<G::RenderPoints>(entity, *state.Points);
            else if (raw.all_of<G::RenderPoints>(entity))
                raw.remove<G::RenderPoints>(entity);

            return EditorCommandHistoryStatus::Applied;
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
            const EditorRenderHintState& before,
            const EditorRenderHintState& after)
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
                    const EditorRenderHintState& expected,
                    const EditorRenderHintState&)
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
                    return SameRenderHintState(
                               ReadRenderHintState(raw, entity),
                               expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                },
                [](
                    const EditorRenderHintMutationIdentity& identity,
                    const EditorRenderHintState& target)
                {
                    return ApplyRenderHintState(
                        identity.Scene,
                        identity.StableEntityId,
                        target);
                },
                [](
                    const EditorRenderHintMutationIdentity&,
                    const EditorRenderHintState&,
                    const EditorRenderHintState& target)
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

        [[nodiscard]] EditorCommandStatus ToEditorCommandStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            switch (status)
            {
            case EditorCommandHistoryStatus::Applied:
            case EditorCommandHistoryStatus::Recorded:
            case EditorCommandHistoryStatus::Undone:
            case EditorCommandHistoryStatus::Redone:
                return EditorCommandStatus::Applied;
            case EditorCommandHistoryStatus::NoChange:
                return EditorCommandStatus::NoChange;
            case EditorCommandHistoryStatus::MissingScene:
                return EditorCommandStatus::MissingScene;
            case EditorCommandHistoryStatus::MissingSelectionController:
                return EditorCommandStatus::MissingSelectionController;
            case EditorCommandHistoryStatus::StaleEntity:
                return EditorCommandStatus::StaleEntity;
            case EditorCommandHistoryStatus::MissingTransform:
                return EditorCommandStatus::MissingTransform;
            case EditorCommandHistoryStatus::EmptyUndoStack:
            case EditorCommandHistoryStatus::EmptyRedoStack:
            case EditorCommandHistoryStatus::InvalidCommand:
            case EditorCommandHistoryStatus::CommandFailed:
            case EditorCommandHistoryStatus::UndoFailed:
            case EditorCommandHistoryStatus::RedoFailed:
            case EditorCommandHistoryStatus::UnsupportedOperation:
                return EditorCommandStatus::NoChange;
            }
            return EditorCommandStatus::NoChange;
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
        [[nodiscard]] bool SameTransformComponent(
            const ECSC::Transform::Component& lhs,
            const ECSC::Transform::Component& rhs) noexcept
        {
            return lhs.Position.x == rhs.Position.x &&
                   lhs.Position.y == rhs.Position.y &&
                   lhs.Position.z == rhs.Position.z &&
                   lhs.Rotation.w == rhs.Rotation.w &&
                   lhs.Rotation.x == rhs.Rotation.x &&
                   lhs.Rotation.y == rhs.Rotation.y &&
                   lhs.Rotation.z == rhs.Rotation.z &&
                   lhs.Scale.x == rhs.Scale.x &&
                   lhs.Scale.y == rhs.Scale.y &&
                   lhs.Scale.z == rhs.Scale.z;
        }

        struct EditorTransformMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        [[nodiscard]] EditorCommandHistoryResult ExecuteEditorTransformMutation(
            EditorCommandHistory& history,
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const ECSC::Transform::Component& before,
            const ECSC::Transform::Component& after,
            std::string label)
        {
            return Internal::ExecuteUndoableEntityMutation(
                history,
                std::move(label),
                EditorTransformMutationIdentity{
                    .Scene = scene,
                    .World = world,
                    .StableEntityId = stableEntityId,
                },
                before,
                before,
                after,
                [](
                    const EditorTransformMutationIdentity& identity,
                    const ECSC::Transform::Component& expected,
                    const ECSC::Transform::Component&)
                {
                    if (identity.Scene == nullptr || !identity.World.IsValid())
                        return EditorCommandHistoryStatus::MissingScene;

                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(raw, identity.StableEntityId);
                    if (!entity.has_value())
                        return EditorCommandHistoryStatus::StaleEntity;

                    const ECSC::Transform::Component* transform =
                        raw.try_get<ECSC::Transform::Component>(*entity);
                    if (transform == nullptr)
                        return EditorCommandHistoryStatus::MissingTransform;
                    return SameTransformComponent(*transform, expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                },
                [](
                    const EditorTransformMutationIdentity& identity,
                    const ECSC::Transform::Component& target)
                {
                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(raw, identity.StableEntityId);
                    if (!entity.has_value())
                        return EditorCommandHistoryStatus::StaleEntity;

                    ECSC::Transform::Component* transform =
                        raw.try_get<ECSC::Transform::Component>(*entity);
                    if (transform == nullptr)
                        return EditorCommandHistoryStatus::MissingTransform;
                    *transform = target;
                    return EditorCommandHistoryStatus::Applied;
                },
                [](
                    const EditorTransformMutationIdentity& identity,
                    const ECSC::Transform::Component&,
                    const ECSC::Transform::Component& target)
                {
                    entt::registry& raw = identity.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(raw, identity.StableEntityId);
                    if (entity.has_value())
                    {
                        raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(
                            *entity);
                    }
                    return target;
                });
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

        const EditorRenderHintState before =
            ReadRenderHintState(raw, entity);
        EditorRenderHintState after = before;
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

        if (SameRenderHintState(before, after))
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
                ApplyRenderHintState(context.Scene, command.StableEntityId, after)));
    }

} // namespace Extrinsic::Runtime
