module;
#include <functional>
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <bit>
#include <variant>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <utility>

module Extrinsic.Runtime.EditorWorkspaceSnapshots;

import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.GeometryPayload;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Graphics.SceneHandles;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.UvView;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetWorkflowRecipePolicies;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.VertexAttributeBinding;
import Geometry.Properties;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;

#include "Editor/internal/Runtime.EditorFeatures.Internal.hpp"
#include "Editor/internal/Runtime.EditorVisualizationHelpers.hpp"
#include "Editor/internal/Runtime.EditorRenderHintHelpers.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail {
namespace
{
    namespace G = Extrinsic::Graphics::Components;
    namespace A = Extrinsic::Assets;
    namespace ECSC = Extrinsic::ECS::Components;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;

    [[nodiscard]] const std::optional<G::VisualizationConfig>*
    LaneOverrideForTarget(const G::VisualizationLaneOverrides& overrides,
                          const EditorVisualizationTarget target) noexcept
    {
        switch (target)
        {
        case EditorVisualizationTarget::Surface:
            return &overrides.Surface;
        case EditorVisualizationTarget::Edges:
            return &overrides.Edges;
        case EditorVisualizationTarget::Points:
            return &overrides.Points;
        case EditorVisualizationTarget::Entity:
            break;
        }
        return nullptr;
    }

    [[nodiscard]] std::string ErrorName(const Core::ErrorCode error)
    {
        return std::string(Core::Error::ToString(error));
    }

    constexpr std::uint64_t kEditorSignaturePrime = 1099511628211ull;

    void MixSignatureByte(std::uint64_t& signature,
                          const std::uint8_t value) noexcept
    {
        signature ^= value;
        signature *= kEditorSignaturePrime;
    }

    void AppendPropertySetMetadataSignature(
        std::uint64_t& signature,
        const std::uint64_t domainTag,
        const Geometry::PropertySet* properties,
        const std::size_t deletedCount)
    {
        MixSignature(signature, domainTag);
        if (properties == nullptr)
        {
            MixSignature(signature, 0u);
            return;
        }

        MixSignature(signature, static_cast<std::uint64_t>(properties->Size()));
        MixSignature(signature, static_cast<std::uint64_t>(deletedCount));
        const std::vector<Geometry::PropertyDescriptor> descriptors =
            properties->Registry().Descriptors(false);
        MixSignature(signature, static_cast<std::uint64_t>(descriptors.size()));
        std::uint64_t order = 0u;
        for (const Geometry::PropertyDescriptor& descriptor : descriptors)
        {
            MixSignature(signature, order++);
            MixSignatureString(signature, descriptor.Name);
            MixSignature(signature,
                         static_cast<std::uint64_t>(
                             descriptor.ValueKind));
            MixSignature(signature,
                         static_cast<std::uint64_t>(
                             descriptor.ElementCount));
            MixSignature(signature,
                         descriptor.SupportsContiguousSpan ? 1u : 0u);
            MixSignature(signature, descriptor.SupportsRawData ? 1u : 0u);
        }
    }

    struct EditorTransformMutationIdentity
    {
        ECS::Scene::Registry* Scene{nullptr};
        WorldHandle World{};
        std::uint32_t StableEntityId{0u};
    };

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

    [[nodiscard]] bool IsScalarVisualizationKind(
        const Geometry::PropertyValueKind kind) noexcept
    {
        return GeometryPropertyComponentCount(kind) == 1u;
    }

    [[nodiscard]] bool DomainSupportsVisualizationConfig(
        const EditorVisualizationPropertyDomain domain) noexcept
    {
        using Domain = EditorVisualizationPropertyDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
        case Domain::MeshEdges:
        case Domain::MeshFaces:
        case Domain::GraphVertices:
        case Domain::GraphEdges:
        case Domain::PointCloudPoints:
            return true;
        }
        return false;
    }

    void RecordVertexChannelResolverScratch(
        EditorWorkspaceSnapshotStats* stats,
        const std::size_t byteCount)
    {
        if (stats == nullptr)
            return;

        ++stats->VertexChannelResolverScans;
        ++stats->VertexChannelScratchAllocations;
        stats->VertexChannelScratchBytes +=
            static_cast<std::uint64_t>(byteCount);
    }
}

    [[nodiscard]] std::optional<G::VisualizationConfig>
    StoredVisualizationConfigForTarget(
        const entt::registry& raw,
        const ECS::EntityHandle entity,
        const EditorVisualizationTarget target)
    {
        if (target == EditorVisualizationTarget::Entity)
        {
            if (const auto* config = raw.try_get<G::VisualizationConfig>(entity))
                return *config;
            return std::nullopt;
        }

        const auto* overrides =
            raw.try_get<G::VisualizationLaneOverrides>(entity);
        if (overrides == nullptr)
            return std::nullopt;

        const std::optional<G::VisualizationConfig>* lane =
            LaneOverrideForTarget(*overrides, target);
        return lane != nullptr ? *lane : std::nullopt;
    }

    [[nodiscard]] std::optional<G::VisualizationConfig>
    EffectiveVisualizationConfigForTarget(
        const entt::registry& raw,
        const ECS::EntityHandle entity,
        const EditorVisualizationTarget target)
    {
        if (std::optional<G::VisualizationConfig> stored =
                StoredVisualizationConfigForTarget(raw, entity, target);
            stored.has_value())
        {
            return stored;
        }
        if (target == EditorVisualizationTarget::Entity)
            return std::nullopt;
        return StoredVisualizationConfigForTarget(
            raw,
            entity,
            EditorVisualizationTarget::Entity);
    }

    void MixSignature(std::uint64_t& signature,
                      std::uint64_t value) noexcept
    {
        for (std::uint32_t i = 0u; i < 8u; ++i)
        {
            MixSignatureByte(
                signature,
                static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu));
        }
    }

    void MixSignatureString(std::uint64_t& signature,
                            const std::string_view value) noexcept
    {
        MixSignature(signature, static_cast<std::uint64_t>(value.size()));
        for (const char c : value)
        {
            MixSignatureByte(signature, static_cast<std::uint8_t>(c));
        }
    }

    [[nodiscard]] std::uint64_t GeometryMetadataSignatureForEntity(
        const entt::registry& raw,
        const ECS::EntityHandle entity)
    {
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        std::uint64_t signature = kEditorSignatureOffset;
        MixSignature(signature,
                     static_cast<std::uint64_t>(view.ActiveDomain));
        MixSignature(signature, view.HasMeshTopologyMarker ? 1u : 0u);
        MixSignature(signature, view.HasGraphTopologyMarker ? 1u : 0u);
        AppendPropertySetMetadataSignature(
            signature,
            1u,
            view.VertexSource != nullptr
                ? &view.VertexSource->Properties
                : nullptr,
            view.VertexSource != nullptr ? view.VertexSource->NumDeleted
                                         : 0u);
        AppendPropertySetMetadataSignature(
            signature,
            2u,
            view.EdgeSource != nullptr ? &view.EdgeSource->Properties
                                       : nullptr,
            view.EdgeSource != nullptr ? view.EdgeSource->NumDeleted
                                       : 0u);
        AppendPropertySetMetadataSignature(
            signature,
            3u,
            view.HalfedgeSource != nullptr
                ? &view.HalfedgeSource->Properties
                : nullptr,
            0u);
        AppendPropertySetMetadataSignature(
            signature,
            4u,
            view.FaceSource != nullptr ? &view.FaceSource->Properties
                                       : nullptr,
            view.FaceSource != nullptr ? view.FaceSource->NumDeleted
                                       : 0u);
        return signature;
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

ScopedEditorStatTimer::ScopedEditorStatTimer(std::uint64_t* target) noexcept
    : m_Target(target)
{
    if (m_Target != nullptr)
        m_Start = EditorModelBuildClock::now();
}

ScopedEditorStatTimer::~ScopedEditorStatTimer()
{
    if (m_Target != nullptr)
        *m_Target += EditorElapsedNs(m_Start);
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

    [[nodiscard]] bool IsInternalVisualizationProperty(
        const std::string& name) noexcept
    {
        return name == GS::PropertyNames::kPosition ||
               name == GS::PropertyNames::kVertexConnectivity ||
               name == GS::PropertyNames::kEdgeV0 ||
               name == GS::PropertyNames::kEdgeV1 ||
               name == GS::PropertyNames::kHalfedgeToVertex ||
               name == GS::PropertyNames::kHalfedgeNext ||
               name == GS::PropertyNames::kHalfedgeFace ||
               name == GS::PropertyNames::kHalfedgeConnectivity ||
               name == GS::PropertyNames::kFaceHalfedge ||
               name == "v:point" ||
               name == "v:tex" ||
               name == "v:texcoord" ||
               // Same reserved property, on the domain that can
               // carry a seam.
               name == "h:texcoord" ||
               name == "p:position";
    }

    [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
        const EditorVisualizationPropertyDomain domain) noexcept
    {
        using Domain = EditorVisualizationPropertyDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return GeometryElementDomain::MeshVertex;
        case Domain::MeshEdges:
            return GeometryElementDomain::MeshEdge;
        case Domain::MeshFaces:
            return GeometryElementDomain::MeshFace;
        case Domain::GraphVertices:
            return GeometryElementDomain::GraphNode;
        case Domain::GraphEdges:
            return GeometryElementDomain::GraphEdge;
        case Domain::PointCloudPoints:
            return GeometryElementDomain::PointCloudPoint;
        }
        return GeometryElementDomain::Unknown;
    }

    [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
        const EditorPropertyCatalogDomain domain) noexcept
    {
        using Domain = EditorPropertyCatalogDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return GeometryElementDomain::MeshVertex;
        case Domain::MeshEdges:
            return GeometryElementDomain::MeshEdge;
        case Domain::MeshHalfedges:
            return GeometryElementDomain::MeshHalfedge;
        case Domain::MeshFaces:
            return GeometryElementDomain::MeshFace;
        case Domain::GraphVertices:
            return GeometryElementDomain::GraphNode;
        case Domain::GraphHalfedges:
            return GeometryElementDomain::GraphHalfedge;
        case Domain::GraphEdges:
            return GeometryElementDomain::GraphEdge;
        case Domain::PointCloudPoints:
            return GeometryElementDomain::PointCloudPoint;
        }
        return GeometryElementDomain::Unknown;
    }

    [[nodiscard]] const Geometry::PropertySet* PropertySetForVisualizationDomain(
        const GeometryEntityAvailability& availability,
        const EditorVisualizationPropertyDomain domain) noexcept
    {
        return ResolveGeometryPropertySet(
            availability,
            ToGeometryElementDomain(domain));
    }

    void AppendVisualizationPropertiesForDomain(
        std::vector<EditorVisualizationPropertyInfo>& out,
        const Geometry::PropertySet& properties,
        const EditorVisualizationPropertyDomain domain)
    {
        if (!DomainSupportsVisualizationConfig(domain))
            return;

        for (const std::string& name : properties.Properties())
        {
            const Geometry::PropertyValueKind kind =
                DetectGeometryPropertyValueKind(properties, name);
            if (kind == Geometry::PropertyValueKind::Unknown)
                continue;

            const bool connectivity =
                IsTopologyProperty(ToGeometryElementDomain(domain), name);
            const bool scalar =
                !connectivity && IsScalarVisualizationKind(kind);
            const bool color =
                !connectivity &&
                (kind == Geometry::PropertyValueKind::Vec2 ||
                 kind == Geometry::PropertyValueKind::Vec3 ||
                 kind == Geometry::PropertyValueKind::Vec4);
            const bool vector =
                !connectivity && kind == Geometry::PropertyValueKind::Vec3;
            const bool integer =
                !connectivity && IsScalarVisualizationKind(kind);
            if (!scalar && !color && !vector && !integer)
            {
                continue;
            }

            out.push_back(EditorVisualizationPropertyInfo{
                .Name = name,
                .Domain = domain,
                .ElementDomain = ToGeometryElementDomain(domain),
                .ValueKind = kind,
                .ElementCount = properties.Size(),
                .ScalarPresetAvailable = scalar,
                .IsolinePresetAvailable = scalar,
                .ColorBufferPresetAvailable = color || integer,
                .VectorFieldCandidate = vector,
            });
        }
    }

    [[nodiscard]] const Geometry::PropertySet* PropertySetForCatalogDomain(
        const GeometryEntityAvailability& availability,
        const EditorPropertyCatalogDomain domain) noexcept
    {
        using Domain = EditorPropertyCatalogDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::MeshVertex);
        case Domain::MeshEdges:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::MeshEdge);
        case Domain::MeshHalfedges:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::MeshHalfedge);
        case Domain::MeshFaces:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::MeshFace);
        case Domain::GraphVertices:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::GraphNode);
        case Domain::GraphHalfedges:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::GraphHalfedge);
        case Domain::GraphEdges:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::GraphEdge);
        case Domain::PointCloudPoints:
            return ResolveGeometryPropertySet(
                availability,
                GeometryElementDomain::PointCloudPoint);
        }
        return nullptr;
    }

    [[nodiscard]] bool IsPropertyCatalogSupportedKind(
        const Geometry::PropertyValueKind kind) noexcept
    {
        switch (kind)
        {
        case Geometry::PropertyValueKind::Bool:
        case Geometry::PropertyValueKind::Int32:
        case Geometry::PropertyValueKind::UInt64:
        case Geometry::PropertyValueKind::Float:
        case Geometry::PropertyValueKind::Double:
        case Geometry::PropertyValueKind::UInt32:
        case Geometry::PropertyValueKind::Vec2:
        case Geometry::PropertyValueKind::Vec3:
        case Geometry::PropertyValueKind::Vec4:
            return true;
        case Geometry::PropertyValueKind::Unknown:
            break;
        }
        return false;
    }

    [[nodiscard]] std::optional<EditorPropertyCatalogDomain>
    VertexChannelCatalogDomainForView(
        const GS::ConstSourceView& view) noexcept
    {
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        using Domain = EditorPropertyCatalogDomain;
        switch (availability.ProvenanceDomain)
        {
        case GS::Domain::Mesh:
            return Domain::MeshVertices;
        case GS::Domain::Graph:
            return Domain::GraphVertices;
        case GS::Domain::PointCloud:
            return Domain::PointCloudPoints;
        case GS::Domain::None:
        case GS::Domain::Unknown:
            break;
        }
        return std::nullopt;
    }

    [[nodiscard]] const Geometry::PropertySet*
    VertexChannelPropertySetForView(
        const GS::ConstSourceView& view,
        const EditorPropertyCatalogDomain domain) noexcept
    {
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        return PropertySetForCatalogDomain(availability, domain);
    }

    [[nodiscard]] std::optional<AttributeSourceType>
    ToAttributeSourceType(
        const Geometry::PropertyValueKind kind) noexcept
    {
        using Kind = Geometry::PropertyValueKind;
        switch (kind)
        {
        case Kind::Float:
            return AttributeSourceType::Float32;
        case Kind::Vec2:
            return AttributeSourceType::Vec2;
        case Kind::Vec3:
            return AttributeSourceType::Vec3;
        case Kind::Vec4:
            return AttributeSourceType::Vec4;
        case Kind::Double:
        case Kind::UInt32:
        case Kind::Unknown:
        case Kind::Bool:
        case Kind::Int32:
        case Kind::UInt64:
            break;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool SourceTypeAllowedForVertexChannel(
        const VertexChannel channel,
        const AttributeSourceType type) noexcept
    {
        switch (channel)
        {
        case VertexChannel::Normal:
            return type == AttributeSourceType::Vec3;
        case VertexChannel::Color:
            return type == AttributeSourceType::Vec3 ||
                   type == AttributeSourceType::Vec4;
        case VertexChannel::Position:
        case VertexChannel::Texcoord:
        case VertexChannel::Tangent:
        case VertexChannel::Custom:
            break;
        }
        return false;
    }

    [[nodiscard]] AttributeBindResult EvaluateVertexChannelBinding(
        const Geometry::PropertySet& properties,
        const VertexChannel channel,
        const std::string_view propertyName,
        const AttributeSourceType sourceType,
        const std::size_t elementCount,
        EditorWorkspaceSnapshotStats* modelBuildStats)
    {
        ScopedEditorStatTimer timer{
            modelBuildStats != nullptr
                ? &modelBuildStats->VertexChannelValidationTimeNs
                : nullptr};
        if (propertyName.empty())
        {
            return AttributeBindResult{
                .Status = AttributeBindStatus::EmptyBinding,
                .FullyPopulated = false,
            };
        }
        if (elementCount > std::numeric_limits<std::uint32_t>::max())
        {
            return AttributeBindResult{
                .Status = AttributeBindStatus::CountMismatch,
                .FullyPopulated = false,
            };
        }
        if (!SourceTypeAllowedForVertexChannel(channel, sourceType))
        {
            return AttributeBindResult{
                .Status = AttributeBindStatus::TypeMismatch,
                .FullyPopulated = false,
            };
        }

        const std::uint32_t count =
            static_cast<std::uint32_t>(elementCount);
        const VertexAttributeBinding binding{
            .Channel = channel,
            .SourceType = sourceType,
            .SourceProperty = propertyName,
            .AllowFallback = false,
            .Normalize = channel == VertexChannel::Normal,
            .Fallback = channel == VertexChannel::Normal
                ? glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}
                : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        };

        if (channel == VertexChannel::Normal)
        {
            RecordVertexChannelResolverScratch(
                modelBuildStats,
                elementCount * sizeof(glm::vec3));
            std::vector<glm::vec3> scratch(elementCount);
            return ResolveVec3Channel(properties, binding, count, scratch);
        }
        if (channel == VertexChannel::Color)
        {
            RecordVertexChannelResolverScratch(
                modelBuildStats,
                elementCount * sizeof(std::uint32_t));
            std::vector<std::uint32_t> scratch(elementCount);
            return ResolveColorChannelPackedUnorm8(
                properties,
                binding,
                count,
                scratch);
        }
        return AttributeBindResult{
            .Status = AttributeBindStatus::TypeMismatch,
            .FullyPopulated = false,
        };
    }

    [[nodiscard]] std::uint64_t EditorElapsedNs(
        const EditorModelBuildClock::time_point start) noexcept
    {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                EditorModelBuildClock::now() - start)
                .count();
        return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 1u;
    }

EditorSceneEditingContext
MakeEditorSceneEditingContext(const EditorFeatureBindings &bindings) {
  return EditorSceneEditingContext{
      .Scene = bindings.Scene,
      .World = bindings.World,
      .Selection = bindings.Selection,
      .CommandHistory = bindings.CommandHistory,
      .LastRefinedPrimitive = bindings.LastRefinedPrimitive,
      .LastRefinedPrimitiveGeneration = bindings.LastRefinedPrimitiveGeneration,
      .CameraControllers = bindings.CameraControllers,
      .CameraViewport = bindings.CameraViewport,
      .AssetImportCommands = bindings.AssetImportCommands,
      .AssetImportQueueCommands = bindings.AssetImportQueueCommands,
      .SceneFileCommands = bindings.SceneFileCommands,
      .PrimitiveViewCommands = bindings.PrimitiveViewCommands,
      .AssetImportQueue = bindings.AssetImportQueue,
      .PendingAssetImportPath = bindings.PendingAssetImportPath,
      .PendingSceneFilePath = bindings.PendingSceneFilePath,
      .PendingAssetImportPayloadKind = bindings.PendingAssetImportPayloadKind,
      .LastAssetImportResult = bindings.LastAssetImportResult,
      .LastSceneFileResult = bindings.LastSceneFileResult,
      .AttachmentActive = bindings.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          bindings.InvalidateWorkspaceSnapshotCache,
      .ImGuiAdapterAvailable = bindings.ImGuiAdapterAvailable,
      .AssetImportCommandsAvailable = bindings.AssetImportCommandsAvailable,
      .SceneFileCommandsAvailable = bindings.SceneFileCommandsAvailable,
      .CameraRenderCommandsAvailable = bindings.CameraRenderCommandsAvailable,
  };
}

EditorProcessingContext
MakeEditorProcessingContext(const EditorFeatureBindings& bindings) {
  return EditorProcessingContext{
      .Scene = bindings.Scene,
      .World = bindings.World,
      .CommandHistory = bindings.CommandHistory,
      .SpatialIndices = bindings.SpatialIndices,
      .Device = bindings.Device,
      .JobCommands = bindings.JobCommands,
      .EngineConfigControlState = bindings.EngineConfigControlState,
      .PreviewEngineConfigDocument = bindings.PreviewEngineConfigDocument,
      .ApplyEngineConfigHotSubset = bindings.ApplyEngineConfigHotSubset,
      .AttachmentActive = bindings.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache = bindings.InvalidateWorkspaceSnapshotCache,
      .EngineConfigCommandsAvailable = bindings.EngineConfigCommandsAvailable,
      .Selection = bindings.Selection,
      .MeshDenoiseKernelAvailable = bindings.MeshDenoiseKernelAvailable,
      .MeshCurvatureKernelAvailable = bindings.MeshCurvatureKernelAvailable,
      .MeshCurvatureDirectionsAvailable =
          bindings.MeshCurvatureDirectionsAvailable,
      .CurvatureSegmentationKernelAvailable =
          bindings.CurvatureSegmentationKernelAvailable,
      .MeshRemeshUniformKernelAvailable =
          bindings.MeshRemeshUniformKernelAvailable,
      .MeshRemeshAdaptiveKernelAvailable =
          bindings.MeshRemeshAdaptiveKernelAvailable,
      .MeshRemeshProjectToSurfaceAvailable =
          bindings.MeshRemeshProjectToSurfaceAvailable,
      .MeshRemeshErrorBoundedSizingAvailable =
          bindings.MeshRemeshErrorBoundedSizingAvailable,
      .MeshSubdivideLoopKernelAvailable =
          bindings.MeshSubdivideLoopKernelAvailable,
      .MeshSubdivideCatmullClarkKernelAvailable =
          bindings.MeshSubdivideCatmullClarkKernelAvailable,
      .MeshSubdivideSqrt3KernelAvailable =
          bindings.MeshSubdivideSqrt3KernelAvailable,
      .MeshSubdivideLoopFeatureEdgesAvailable =
          bindings.MeshSubdivideLoopFeatureEdgesAvailable,
      .MeshSimplifyKernelAvailable = bindings.MeshSimplifyKernelAvailable,
  };
}

EditorVisualizationEditingContext
MakeEditorVisualizationEditingContext(const EditorFeatureBindings &bindings) {
  return EditorVisualizationEditingContext{
      .Scene = bindings.Scene,
      .World = bindings.World,
      .Selection = bindings.Selection,
      .CommandHistory = bindings.CommandHistory,
      .TextureBake = bindings.TextureBake,
      .VisualizationRecipes = bindings.VisualizationRecipes,
      .VisualizationRecipeRevision = bindings.VisualizationRecipeRevision,
      .JobCommands = bindings.JobCommands,
      .ModelBuildStats = bindings.ModelBuildStats,
      .AttachmentActive = bindings.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          bindings.InvalidateWorkspaceSnapshotCache,
      .OperationalGpuAvailable =
          bindings.Device != nullptr && bindings.Device->IsOperational(),
      .VisualizationCommandsAvailable = bindings.VisualizationCommandsAvailable,
  };
}

EditorRenderRecipeEditingContext
MakeEditorRenderRecipeEditingContext(const EditorFeatureBindings &bindings) {
  return EditorRenderRecipeEditingContext{
      .RenderGraphStats = bindings.RenderGraphStats,
      .RenderRecipeContext = bindings.RenderRecipeContext,
      .RenderRecipeEditorState = bindings.RenderRecipeEditorState,
      .RenderRecipeRuntimeState = bindings.RenderRecipeRuntimeState,
      .PreviewRenderRecipeDocument = bindings.PreviewRenderRecipeDocument,
      .ApplyRenderRecipePreview = bindings.ApplyRenderRecipePreview,
      .EngineConfigControlState = bindings.EngineConfigControlState,
      .PreviewEngineConfigDocument = bindings.PreviewEngineConfigDocument,
      .ApplyEngineConfigHotSubset = bindings.ApplyEngineConfigHotSubset,
      .RenderArtifacts = bindings.RenderArtifacts,
      .AttachmentActive = bindings.AttachmentActive,
      .RenderRecipeCommandsAvailable = bindings.RenderRecipeCommandsAvailable,
      .EngineConfigCommandsAvailable = bindings.EngineConfigCommandsAvailable,
  };
}

EditorWorkspaceSnapshotContext MakeEditorWorkspaceSnapshotContext(
    const EditorFeatureBindings &bindings,
    const EditorProcessingContext &geometry) {
  return EditorWorkspaceSnapshotContext{
      .Scene = MakeEditorSceneEditingContext(bindings),
      .Geometry = geometry,
      .Visualization = MakeEditorVisualizationEditingContext(bindings),
      .RenderRecipe = MakeEditorRenderRecipeEditingContext(bindings),
      // This borrow is valid only while the session attachment is active.
      .SelectedModelCache = bindings.SelectedModelCache,
  };
}

EditorFeatureBindings
ToEditorFeatureBindingsImpl(const EditorWorkspaceSnapshotContext &context) {
  const auto &scene = context.Scene;
  const auto &geometry = context.Geometry;
  const auto &visualization = context.Visualization;
  const auto &renderRecipe = context.RenderRecipe;

  // Fail closed: one expired feature attachment makes the whole binding inert.
  const auto attachmentExpired = [](const auto &feature) {
    return feature.AttachmentActive && !feature.AttachmentActive();
  };
  if (attachmentExpired(scene) || attachmentExpired(geometry) ||
      attachmentExpired(visualization) || attachmentExpired(renderRecipe))
    return {};

  return EditorFeatureBindings{
      .Scene = scene.Scene,
      .World = scene.World,
      .Selection = scene.Selection,
      .CommandHistory = scene.CommandHistory,
      .LastRefinedPrimitive = scene.LastRefinedPrimitive,
      .LastRefinedPrimitiveGeneration = scene.LastRefinedPrimitiveGeneration,
      .CameraControllers = scene.CameraControllers,
      .CameraViewport = scene.CameraViewport,
      .Device = geometry.Device,
      .TextureBake = visualization.TextureBake,
      .SpatialIndices = geometry.SpatialIndices,
      .AssetImportCommands = scene.AssetImportCommands,
      .AssetImportQueueCommands = scene.AssetImportQueueCommands,
      .SceneFileCommands = scene.SceneFileCommands,
      .PrimitiveViewCommands = scene.PrimitiveViewCommands,
      .VisualizationRecipes = visualization.VisualizationRecipes,
      .VisualizationRecipeRevision = visualization.VisualizationRecipeRevision,
      .JobCommands = geometry.JobCommands.Available()
                         ? geometry.JobCommands
                         : visualization.JobCommands,
      .AssetImportQueue = scene.AssetImportQueue,
      .PendingAssetImportPath = scene.PendingAssetImportPath,
      .PendingSceneFilePath = scene.PendingSceneFilePath,
      .PendingAssetImportPayloadKind = scene.PendingAssetImportPayloadKind,
      .LastAssetImportResult = scene.LastAssetImportResult,
      .LastSceneFileResult = scene.LastSceneFileResult,
      .RenderGraphStats = renderRecipe.RenderGraphStats,
      .RenderRecipeContext = renderRecipe.RenderRecipeContext,
      .RenderRecipeEditorState = renderRecipe.RenderRecipeEditorState,
      .RenderRecipeRuntimeState = renderRecipe.RenderRecipeRuntimeState,
      .EngineConfigControlState =
          geometry.EngineConfigControlState != nullptr
              ? geometry.EngineConfigControlState
              : renderRecipe.EngineConfigControlState,
      .ModelBuildStats = visualization.ModelBuildStats,
      .SelectedModelCache = context.SelectedModelCache,
      .AttachmentActive = scene.AttachmentActive ? scene.AttachmentActive
                          : geometry.AttachmentActive
                              ? geometry.AttachmentActive
                          : visualization.AttachmentActive
                              ? visualization.AttachmentActive
                              : renderRecipe.AttachmentActive,
      // The render recipe context carries no cache invalidation hook, so the
      // fallback deliberately stops at the visualization context.
      .InvalidateWorkspaceSnapshotCache =
          scene.InvalidateWorkspaceSnapshotCache
              ? scene.InvalidateWorkspaceSnapshotCache
          : geometry.InvalidateWorkspaceSnapshotCache
              ? geometry.InvalidateWorkspaceSnapshotCache
              : visualization.InvalidateWorkspaceSnapshotCache,
      .PreviewRenderRecipeDocument = renderRecipe.PreviewRenderRecipeDocument,
      .ApplyRenderRecipePreview = renderRecipe.ApplyRenderRecipePreview,
      .PreviewEngineConfigDocument =
          geometry.PreviewEngineConfigDocument
              ? geometry.PreviewEngineConfigDocument
              : renderRecipe.PreviewEngineConfigDocument,
      .ApplyEngineConfigHotSubset = geometry.ApplyEngineConfigHotSubset
                                        ? geometry.ApplyEngineConfigHotSubset
                                        : renderRecipe.ApplyEngineConfigHotSubset,
      .RenderArtifacts = renderRecipe.RenderArtifacts,
      .ImGuiAdapterAvailable = scene.ImGuiAdapterAvailable,
      .AssetImportCommandsAvailable = scene.AssetImportCommandsAvailable,
      .SceneFileCommandsAvailable = scene.SceneFileCommandsAvailable,
      .CameraRenderCommandsAvailable = scene.CameraRenderCommandsAvailable,
      .VisualizationCommandsAvailable =
          visualization.VisualizationCommandsAvailable,
      .RenderRecipeCommandsAvailable =
          renderRecipe.RenderRecipeCommandsAvailable,
      .EngineConfigCommandsAvailable =
          geometry.EngineConfigCommandsAvailable ||
          renderRecipe.EngineConfigCommandsAvailable,
      .MeshDenoiseKernelAvailable = geometry.MeshDenoiseKernelAvailable,
      .MeshCurvatureKernelAvailable = geometry.MeshCurvatureKernelAvailable,
      .MeshCurvatureDirectionsAvailable =
          geometry.MeshCurvatureDirectionsAvailable,
      .CurvatureSegmentationKernelAvailable =
          geometry.CurvatureSegmentationKernelAvailable,
      .MeshRemeshUniformKernelAvailable =
          geometry.MeshRemeshUniformKernelAvailable,
      .MeshRemeshAdaptiveKernelAvailable =
          geometry.MeshRemeshAdaptiveKernelAvailable,
      .MeshRemeshProjectToSurfaceAvailable =
          geometry.MeshRemeshProjectToSurfaceAvailable,
      .MeshRemeshErrorBoundedSizingAvailable =
          geometry.MeshRemeshErrorBoundedSizingAvailable,
      .MeshSubdivideLoopKernelAvailable =
          geometry.MeshSubdivideLoopKernelAvailable,
      .MeshSubdivideCatmullClarkKernelAvailable =
          geometry.MeshSubdivideCatmullClarkKernelAvailable,
      .MeshSubdivideSqrt3KernelAvailable =
          geometry.MeshSubdivideSqrt3KernelAvailable,
      .MeshSubdivideLoopFeatureEdgesAvailable =
          geometry.MeshSubdivideLoopFeatureEdgesAvailable,
      .MeshSimplifyKernelAvailable = geometry.MeshSimplifyKernelAvailable,
  };
}

namespace
{
    namespace A = Extrinsic::Assets;
        [[nodiscard]] bool IsModelTextureImportPayload(
            const A::AssetPayloadKind payloadKind) noexcept
        {
            return payloadKind == A::AssetPayloadKind::ModelScene ||
                   payloadKind == A::AssetPayloadKind::Texture2D;
        }
        [[nodiscard]] EditorFileImportResult BuildFileImportResultFromRuntimeEvent(
            const RuntimeAssetImportEvent& event)
        {
            if (!event.Result.has_value())
            {
                return EditorFileImportResult{
                    .Status = EditorCommandStatus::AssetImportFailed,
                    .PayloadKind = event.RequestedPayloadKind,
                    .Error = event.Error,
                    .Message = BuildImportFailureMessage(event.Error),
                };
            }

            const RuntimeAssetImportResult& imported = *event.Result;
            EditorFileImportResult result{
                .Status = EditorCommandStatus::Applied,
                .Asset = imported.Asset,
                .PayloadKind = imported.PayloadKind,
                .Error = Core::ErrorCode::Success,
                .PrimitiveEntitiesCreated = imported.PrimitiveEntitiesCreated,
                .EmbeddedTextureAssetsCreated = imported.EmbeddedTextureAssetsCreated,
                .TextureUploadRequests = imported.TextureUploadRequests,
                .MaterializedModelScene = imported.MaterializedModelScene,
                .RequestedTextureUpload = imported.RequestedTextureUpload,
            };
            result.Message = BuildImportSuccessMessage(
                EditorFileImportCommand{
                    .Path = event.Path,
                    .PayloadKind = event.RequestedPayloadKind,
                },
                result);
            return result;
        }

        [[nodiscard]] EditorSceneFileOperation ToSandboxSceneFileOperation(
            const RuntimeSceneFileOperation operation) noexcept
        {
            switch (operation)
            {
            case RuntimeSceneFileOperation::Save:
                return EditorSceneFileOperation::Save;
            case RuntimeSceneFileOperation::Load:
                return EditorSceneFileOperation::Load;
            case RuntimeSceneFileOperation::None:
                break;
            }
            return EditorSceneFileOperation::Load;
        }

        [[nodiscard]] EditorSceneFileResult
        BuildSceneFileResultFromRuntimeEvent(const RuntimeSceneFileEvent& event)
        {
            const EditorSceneFileOperation operation =
                ToSandboxSceneFileOperation(event.Operation);
            if (!event.Succeeded())
            {
                return EditorSceneFileResult{
                    .Status = operation == EditorSceneFileOperation::Save
                        ? EditorCommandStatus::SceneSaveFailed
                        : EditorCommandStatus::SceneLoadFailed,
                    .Operation = operation,
                    .Task = event.Task,
                    .Error = event.Error,
                    .Message = BuildSceneFileFailureMessage(operation, event.Error),
                };
            }

            EditorSceneFileResult result{
                .Status = EditorCommandStatus::Applied,
                .Operation = operation,
                .Task = event.Task,
                .Error = Core::ErrorCode::Success,
            };
            if (operation == EditorSceneFileOperation::Load &&
                event.LoadResult.has_value())
            {
                result.Stats = event.LoadResult->Stats;
            }
            else if (operation == EditorSceneFileOperation::Save &&
                     event.SaveResult.has_value())
            {
                result.Stats = event.SaveResult->Stats;
            }
            result.Message = BuildSceneFileSuccessMessage(
                EditorSceneFileCommand{.Path = event.Path},
                result);
            return result;
        }
        [[nodiscard]] Graphics::UvViewBackgroundMode ToGraphicsUvViewBackground(
            const ParameterizationUvBackgroundMode mode) noexcept
        {
            using ConfigMode = ParameterizationUvBackgroundMode;
            switch (mode)
            {
            case ConfigMode::Grid:
                return Graphics::UvViewBackgroundMode::Grid;
            case ConfigMode::Checker:
                return Graphics::UvViewBackgroundMode::Checker;
            case ConfigMode::TexelDensity:
                return Graphics::UvViewBackgroundMode::TexelDensity;
            case ConfigMode::Texture:
                return Graphics::UvViewBackgroundMode::Texture;
            }
            return Graphics::UvViewBackgroundMode::Grid;
        }

        [[nodiscard]] ParameterizationUvBackgroundMode
        ToConfigUvViewBackground(
            const Graphics::UvViewBackgroundMode mode) noexcept
        {
            using ConfigMode = ParameterizationUvBackgroundMode;
            switch (mode)
            {
            case Graphics::UvViewBackgroundMode::Grid:
                return ConfigMode::Grid;
            case Graphics::UvViewBackgroundMode::Checker:
                return ConfigMode::Checker;
            case Graphics::UvViewBackgroundMode::TexelDensity:
                return ConfigMode::TexelDensity;
            case Graphics::UvViewBackgroundMode::Texture:
            case Graphics::UvViewBackgroundMode::BakedTexture:
                return ConfigMode::Texture;
            }
            return ConfigMode::Grid;
        }

        [[nodiscard]] EditorParameterizationUvViewStatus
        ToSandboxUvViewStatus(const Graphics::UvViewStatus status) noexcept
        {
            using SandboxStatus =
                EditorParameterizationUvViewStatus;
            switch (status)
            {
            case Graphics::UvViewStatus::Disabled:
                return SandboxStatus::Disabled;
            case Graphics::UvViewStatus::CpuFallbackNonOperational:
                return SandboxStatus::CpuFallbackNonOperational;
            case Graphics::UvViewStatus::WaitingForGeometry:
                return SandboxStatus::WaitingForGeometry;
            case Graphics::UvViewStatus::InvalidRequest:
                return SandboxStatus::InvalidRequest;
            case Graphics::UvViewStatus::ResourceCreationFailed:
                return SandboxStatus::ResourceCreationFailed;
            case Graphics::UvViewStatus::Ready:
                return SandboxStatus::Ready;
            }
            return SandboxStatus::InvalidRequest;
        }

        void MixSandboxUvViewToken(
            std::uint64_t& token,
            const std::uint64_t value) noexcept
        {
            token ^= value + 0x9E3779B97F4A7C15ull +
                     (token << 6u) + (token >> 2u);
        }

        // Texture tabs carry an asset id packed as (generation << 32) | index.
        [[nodiscard]] Assets::AssetId UnpackTextureTabAssetId(const std::uint64_t packed) noexcept
        {
            return Assets::AssetId{static_cast<std::uint32_t>(packed & 0xFFFFFFFFull),
                                   static_cast<std::uint32_t>(packed >> 32u)};
        }

        // Bake encodings (PropertyTextureBakeEncoding values) that store a raw
        // scalar or vector are mapped through the tab's display range; colour,
        // palette and pre-colormapped encodings are shown as stored.
        [[nodiscard]] Graphics::UvViewTextureDisplayMode UvViewDisplayModeForTextureTab(
            const EditorParameterizationTextureTab& tab) noexcept
        {
            constexpr std::uint32_t kAuto = 0u;
            constexpr std::uint32_t kScalarColormap = 1u;
            constexpr std::uint32_t kLinearScalar = 2u;
            constexpr std::uint32_t kVector2 = 4u;
            constexpr std::uint32_t kVector3 = 5u;
            constexpr std::uint32_t kNormal = 6u;
            if (!tab.RawFloat)
                return Graphics::UvViewTextureDisplayMode::Color;
            switch (tab.Encoding)
            {
            case kAuto:
            case kScalarColormap:
            case kLinearScalar:
                return Graphics::UvViewTextureDisplayMode::ScalarColormap;
            case kVector2:
            case kVector3:
            case kNormal:
                return Graphics::UvViewTextureDisplayMode::VectorRange;
            default:
                return Graphics::UvViewTextureDisplayMode::Color;
            }
        }

        [[nodiscard]] EditorParameterizationUvViewState
        SubmitRuntimeParameterizationUvView(Graphics::IRenderer& renderer, RHI::IDevice& device,
                                            ServiceRegistry& services,
                                            const RenderExtractionCache* renderExtraction,
                                            EditorParameterizationUvViewRequest request)
        {
            using ConfigBackground = ParameterizationUvBackgroundMode;
            using ConfigMode = ParameterizationUvRenderMode;
            using SandboxStatus =
                EditorParameterizationUvViewStatus;

            EditorParameterizationUvViewState state{
                .Status = request.Enabled
                    ? SandboxStatus::WaitingForGpuFrame
                    : SandboxStatus::CpuLayout,
                .RequestedMode = request.View.RenderMode,
                .ActiveMode = ConfigMode::CpuLayout,
                .RequestedBackground = request.View.BackgroundMode,
                .ActiveBackground =
                    request.View.BackgroundMode == ConfigBackground::Grid ||
                            request.View.BackgroundMode == ConfigBackground::Checker
                        ? request.View.BackgroundMode
                        : ConfigBackground::Checker,
                .RequestToken = request.RequestToken,
                .Width = request.Width,
                .Height = request.Height,
                .Message = request.Enabled
                    ? "GPU UV view is waiting for the next rendered frame."
                    : "CPU UV layout is active.",
            };

            if (!request.Enabled)
            {
                Graphics::UvViewRequest disabledRequest{};
                disabledRequest.RequestToken = request.RequestToken;
                renderer.SubmitUvViewRequest(std::move(disabledRequest));
                return state;
            }

            std::optional<Graphics::GpuGeometryHandle> geometry{};
            if (renderExtraction != nullptr)
            {
                const auto availability =
                    renderExtraction->FindGpuRenderableAvailability(
                        request.StableEntityId);
                if (availability.has_value() &&
                    availability->Surface.HasGeometry)
                {
                    geometry = availability->Surface.Geometry;
                }
            }
            if (geometry.has_value())
            {
                MixSandboxUvViewToken(state.RequestToken, geometry->Index);
                MixSandboxUvViewToken(state.RequestToken, geometry->Generation);
            }
            else
            {
                MixSandboxUvViewToken(state.RequestToken, 0xFFFFFFFFFFFFFFFFull);
            }

            RHI::BindlessIndex backgroundTexture =
                RHI::kInvalidBindlessIndex;
            std::uint64_t backgroundTextureGeneration = 0u;
            const Graphics::GpuAssetCache* const gpuAssetCache =
                services.Find<Graphics::GpuAssetCache>();
            if (renderExtraction != nullptr &&
                gpuAssetCache != nullptr &&
                request.View.BackgroundMode == ConfigBackground::Texture)
            {
                const auto bindings =
                    renderExtraction->GetMaterialTextureAssetBindings(
                        request.StableEntityId);
                if (bindings.has_value() && bindings->Albedo.IsValid())
                {
                    const auto view =
                        gpuAssetCache->GetView(bindings->Albedo);
                    if (view.has_value() &&
                        view->Kind == Graphics::GpuAssetKind::Texture &&
                        view->BindlessIdx != RHI::kInvalidBindlessIndex)
                    {
                        backgroundTexture = view->BindlessIdx;
                        backgroundTextureGeneration = view->Generation;
                    }
                }
            }
            MixSandboxUvViewToken(state.RequestToken, backgroundTexture);
            MixSandboxUvViewToken(
                state.RequestToken,
                backgroundTextureGeneration);

            // An explicit baked texture replaces the configured background,
            // but only a fresh (ready, not stale) bake of the current atlas
            // is ever bound; otherwise the pane keeps the plain layout.
            Graphics::UvViewTextureDisplay bakedTexture{};
            bool showBakedTexture = false;
            if (request.Texture.has_value() && request.Texture->State == EditorParameterizationTextureState::Ready)
            {
                const EditorParameterizationTextureTab& tab = *request.Texture;
                if (gpuAssetCache != nullptr)
                {
                    const auto view = gpuAssetCache->GetView(UnpackTextureTabAssetId(tab.TextureAssetId));
                    const auto coverage = gpuAssetCache->GetView(UnpackTextureTabAssetId(tab.CoverageTextureAssetId));
                    if (coverage.has_value() && coverage->Kind == Graphics::GpuAssetKind::Texture &&
                        coverage->BindlessIdx != RHI::kInvalidBindlessIndex)
                    {
                        bakedTexture.CoverageTexture = coverage->BindlessIdx;
                        MixSandboxUvViewToken(state.RequestToken, coverage->Generation);
                    }
                    if (view.has_value() && view->Kind == Graphics::GpuAssetKind::Texture &&
                        view->BindlessIdx != RHI::kInvalidBindlessIndex)
                    {
                        bakedTexture.Texture = view->BindlessIdx;
                        MixSandboxUvViewToken(state.RequestToken, view->Generation);
                    }
                }
                bakedTexture.Mode = UvViewDisplayModeForTextureTab(tab);
                bakedTexture.RangeMin = tab.RangeMin;
                bakedTexture.RangeMax = tab.RangeMax;
                bakedTexture.Colormap = tab.Colormap < Graphics::Colormap::kColormapCount
                    ? static_cast<Graphics::Colormap::Type>(tab.Colormap)
                    : Graphics::Colormap::Type::Viridis;
                showBakedTexture = true;
                MixSandboxUvViewToken(state.RequestToken, bakedTexture.Texture);
                MixSandboxUvViewToken(state.RequestToken, tab.Revision);
                MixSandboxUvViewToken(state.RequestToken, static_cast<std::uint64_t>(bakedTexture.Mode));
                MixSandboxUvViewToken(state.RequestToken, std::bit_cast<std::uint32_t>(tab.RangeMin));
                MixSandboxUvViewToken(state.RequestToken, std::bit_cast<std::uint32_t>(tab.RangeMax));
                MixSandboxUvViewToken(state.RequestToken, tab.Colormap);
            }
            const Graphics::UvViewNavigation navigation{
                .CenterU = request.ViewCenter.x,
                .CenterV = request.ViewCenter.y,
                .HalfExtentV = request.ViewHalfExtent,
            };
            MixSandboxUvViewToken(state.RequestToken, std::bit_cast<std::uint32_t>(navigation.CenterU));
            MixSandboxUvViewToken(state.RequestToken, std::bit_cast<std::uint32_t>(navigation.CenterV));
            MixSandboxUvViewToken(state.RequestToken, std::bit_cast<std::uint32_t>(navigation.HalfExtentV));

            Graphics::UvViewRequest graphicsRequest{
                .Enabled = true,
                .RequestToken = state.RequestToken,
                .Geometry = geometry.value_or(Graphics::GpuGeometryHandle{}),
                .Width = request.Width,
                .Height = request.Height,
                .Bounds = Graphics::UvViewBounds{
                    .MinU = request.UvBoundsMin.x,
                    .MinV = request.UvBoundsMin.y,
                    .MaxU = request.UvBoundsMax.x,
                    .MaxV = request.UvBoundsMax.y,
                },
                .Background = showBakedTexture
                    ? Graphics::UvViewBackgroundMode::BakedTexture
                    : ToGraphicsUvViewBackground(request.View.BackgroundMode),
                .BackgroundTexture = backgroundTexture,
                .BakedTexture = bakedTexture,
                .Navigation = navigation,
                .ShowDistortionHeatmap =
                    request.View.ShowDistortionHeatmap,
                .LineIndices = std::move(request.LineIndices),
                .TriangleConformalDistortion =
                    std::move(request.TriangleConformalDistortion),
            };
            renderer.SubmitUvViewRequest(std::move(graphicsRequest));

            if (!device.IsOperational())
            {
                state.Status = SandboxStatus::CpuFallbackNonOperational;
                state.Message = "GPU UV view is unavailable because the render device is "
                                "not operational; CPU layout is active.";
                return state;
            }
            if (!geometry.has_value())
            {
                state.Status = SandboxStatus::WaitingForGeometry;
                state.Message = "Selected mesh GPU surface residency is not ready; CPU "
                                "layout is active.";
                return state;
            }

            const Graphics::UvViewOutput output = renderer.GetUvViewOutput();
            if (output.RequestToken != state.RequestToken)
                return state;

            state.Status = ToSandboxUvViewStatus(output.Status);
            state.ActiveMode = output.ActiveMode ==
                    Graphics::UvViewActiveMode::GpuShaded
                ? ConfigMode::GpuShaded
                : ConfigMode::CpuLayout;
            state.RequestedBackground =
                ToConfigUvViewBackground(output.RequestedBackground);
            state.ActiveBackground =
                ToConfigUvViewBackground(output.ActiveBackground);
            state.HeatmapActive = output.HeatmapActive;
            state.TargetGeneration = output.TargetGeneration;
            state.RecordedPassCount = output.RecordedPassCount;
            state.Message = output.Diagnostic;
            if (state.ActiveMode == ConfigMode::CpuLayout &&
                state.ActiveBackground != ConfigBackground::Grid &&
                state.ActiveBackground != ConfigBackground::Checker)
            {
                state.ActiveBackground = ConfigBackground::Checker;
            }

            const bool extentMatches = output.Width == request.Width &&
                                       output.Height == request.Height;
            if (output.Status == Graphics::UvViewStatus::Ready &&
                (!extentMatches || !output.IsGpuReady() ||
                 output.RecordedPassCount == 0u))
            {
                state.Status = SandboxStatus::WaitingForGpuFrame;
                state.ActiveMode = ConfigMode::CpuLayout;
                if (state.ActiveBackground != ConfigBackground::Grid &&
                    state.ActiveBackground != ConfigBackground::Checker)
                {
                    state.ActiveBackground = ConfigBackground::Checker;
                }
                state.Message = "GPU UV view target is not yet ready for this pane extent; "
                                "CPU layout is active.";
                return state;
            }
            if (output.Status == Graphics::UvViewStatus::Ready)
            {
                state.GpuReady = true;
                state.BindlessIndex = output.BindlessIndex;
                state.Width = output.Width;
                state.Height = output.Height;
            }
            return state;
        }

        // Camera commands issued from the UI frame match the scene rectangle
        // the engine is currently presenting, not the whole framebuffer.
        [[nodiscard]] Core::Extent2D PresentedSceneViewportExtent(
            const Platform::IWindow* const window,
            const EditorUiHost* const host) noexcept
        {
            if (window == nullptr)
                return {};
            EditorInputCaptureSnapshot capture{};
            if (host != nullptr && host->IsVisible())
            {
                if (const auto presented = host->PresentedSceneViewport())
                {
                    capture.HasSceneViewport = true;
                    capture.SceneViewport = *presented;
                }
            }
            return ResolveSceneViewportPixels(
                window->GetWindowExtent(), window->GetFramebufferExtent(), capture).Extent;
        }

        [[nodiscard]] EditorFeatureBindings BuildContextFromRuntime(WorldRegistry& worlds,
                                                                   ServiceRegistry& services)
        {
            RenderExtractionCache* renderExtraction    = services.Find<RenderExtractionCache>();
            EngineConfigControl* configControl         = services.Find<EngineConfigControl>();
            Platform::IWindow* const window            = services.Find<Platform::IWindow>();
            RHI::IDevice* const device                 = services.Find<RHI::IDevice>();
            Graphics::IRenderer* const renderer        = services.Find<Graphics::IRenderer>();
            const auto activeWorld                     = worlds.ActiveWorld();
            ECS::Scene::Registry* const activeScene    = worlds.Get(activeWorld);
            EditorCommandHistory* const commandHistory = services.Find<EditorCommandHistory>();
            SceneDocumentModule* const sceneDocuments  = services.Find<SceneDocumentModule>();
            SceneInteractionModule* const interaction  = services.Find<SceneInteractionModule>();
            SelectionController* const selection       = services.Find<SelectionController>();
            AssetWorkflowModule* const assetWorkflow = services.Find<AssetWorkflowModule>();
            TextureBakeService* const textureBake          = services.Find<TextureBakeService>();
            EditorFeatureBindings context{
                .Scene          = activeScene,
                .World          = activeWorld,
                .Selection      = selection,
                .CommandHistory = commandHistory,
                .LastRefinedPrimitive =
                    interaction != nullptr ? &interaction->LastRefinedPrimitive() : nullptr,
                .LastRefinedPrimitiveGeneration =
                    interaction != nullptr ? interaction->LastRefinedPrimitiveGeneration() : 0u,
                .CameraControllers = services.Find<CameraControllerRegistry>(),
                .CameraViewport    = PresentedSceneViewportExtent(window, services.Find<EditorUiHost>()),
                .Device            = device,
                .TextureBake       = textureBake,
                .AssetImportCommands =
                    EditorAssetImportCommandSurface{
                        .Import =
                            [assetWorkflow](const EditorFileImportCommand& command)
                        {
                            if (assetWorkflow == nullptr)
                            {
                                return EditorFileImportResult{
                                    .Status =
                                        EditorCommandStatus::
                                            AssetImportFailed,
                                    .PayloadKind = command.PayloadKind,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildImportFailureMessage(
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            auto route = Assets::ResolveAssetImportRoute(
                                command.Path,
                                Assets::AssetRouteOperation::Import,
                                Assets::AssetImportHint{
                                    .PayloadKind = command.PayloadKind,
                                });
                            if (route.has_value() &&
                                (IsModelTextureImportPayload(route->PayloadKind) ||
                                 Assets::IsGeometryPayloadKind(route->PayloadKind)))
                            {
                                AssetImportRecipe recipe{
                                    .Path = command.Path,
                                    .PayloadKind = route->PayloadKind,
                                };
                                auto queued =
                                    assetWorkflow->QueueAssetImport(
                                        std::move(recipe));
                                if (!queued.has_value())
                                {
                                    return EditorFileImportResult{
                                        .Status = EditorCommandStatus::AssetImportFailed,
                                        .PayloadKind = route->PayloadKind,
                                        .Error = queued.error(),
                                        .Message = BuildImportFailureMessage(queued.error()),
                                    };
                                }

                                return EditorFileImportResult{
                                    .Status = EditorCommandStatus::Pending,
                                    .Operation = queued->Operation,
                                    .PayloadKind = queued->PayloadKind,
                                    .Error = Core::ErrorCode::Success,
                                    .Message = BuildImportPendingMessage(
                                        command,
                                        queued->PayloadKind),
                                };
                            }

                            auto imported =
                                assetWorkflow->ImportAssetFromPath(
                                RuntimeAssetImportRequest{
                                    .Path = command.Path,
                                    .PayloadKind = command.PayloadKind,
                                });
                            if (!imported.has_value())
                            {
                                return EditorFileImportResult{
                                    .Status = EditorCommandStatus::AssetImportFailed,
                                    .PayloadKind = command.PayloadKind,
                                    .Error = imported.error(),
                                    .Message = BuildImportFailureMessage(imported.error()),
                                };
                            }

                            EditorFileImportResult result{
                                .Status = EditorCommandStatus::Applied,
                                .Asset = imported->Asset,
                                .PayloadKind = imported->PayloadKind,
                                .PrimitiveEntitiesCreated =
                                    imported->PrimitiveEntitiesCreated,
                                .EmbeddedTextureAssetsCreated =
                                    imported->EmbeddedTextureAssetsCreated,
                                .TextureUploadRequests =
                                    imported->TextureUploadRequests,
                                .MaterializedModelScene =
                                    imported->MaterializedModelScene,
                                .RequestedTextureUpload =
                                    imported->RequestedTextureUpload,
                            };
                            result.Message =
                                BuildImportSuccessMessage(command, result);
                            return result;
                        },
                    },
                .AssetImportQueueCommands =
                    EditorAssetImportQueueCommandSurface{
                        .ClearCompleted =
                            [assetWorkflow]()
                        {
                            return assetWorkflow != nullptr
                                ? assetWorkflow->
                                      ClearCompletedAssetImports()
                                : std::size_t{0u};
                        },
                        .Cancel =
                            [assetWorkflow](const RuntimeAssetIngestHandle operation)
                        {
                            return assetWorkflow != nullptr
                                ? assetWorkflow->
                                      CancelAssetImport(operation)
                                : Core::Err(
                                      Core::ErrorCode::InvalidState);
                        },
                    },
                .SceneFileCommands =
                    EditorSceneFileCommandSurface{
                        .New =
                            [sceneDocuments]()
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneNewFailed,
                                    .Operation = EditorSceneFileOperation::New,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::New,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            Core::Result created =
                                sceneDocuments->NewSceneDocument();
                            if (!created.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneNewFailed,
                                    .Operation = EditorSceneFileOperation::New,
                                    .Error = created.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::New,
                                        created.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Applied,
                                .Operation = EditorSceneFileOperation::New,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                        .Save =
                            [sceneDocuments](const EditorSceneFileCommand& command)
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneSaveFailed,
                                    .Operation = EditorSceneFileOperation::Save,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Save,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            auto queued =
                                sceneDocuments->QueueSceneSaveToPath(
                                    command.Path);
                            if (!queued.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneSaveFailed,
                                    .Operation = EditorSceneFileOperation::Save,
                                    .Error = queued.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Save,
                                        queued.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Pending,
                                .Operation = EditorSceneFileOperation::Save,
                                .Task = queued->Task,
                                .Error = Core::ErrorCode::Success,
                            };
                            result.Message = BuildSceneFilePendingMessage(
                                command,
                                result.Operation);
                            return result;
                        },
                        .Load =
                            [sceneDocuments](const EditorSceneFileCommand& command)
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneLoadFailed,
                                    .Operation = EditorSceneFileOperation::Load,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Load,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            auto queued =
                                sceneDocuments->QueueSceneLoadFromPath(
                                    command.Path);
                            if (!queued.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneLoadFailed,
                                    .Operation = EditorSceneFileOperation::Load,
                                    .Error = queued.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Load,
                                        queued.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Pending,
                                .Operation = EditorSceneFileOperation::Load,
                                .Task = queued->Task,
                                .Error = Core::ErrorCode::Success,
                            };
                            result.Message = BuildSceneFilePendingMessage(
                                command,
                                result.Operation);
                            return result;
                        },
                        .Close =
                            [sceneDocuments]()
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneCloseFailed,
                                    .Operation = EditorSceneFileOperation::Close,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Close,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            Core::Result closed =
                                sceneDocuments->CloseSceneDocument();
                            if (!closed.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneCloseFailed,
                                    .Operation = EditorSceneFileOperation::Close,
                                    .Error = closed.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Close,
                                        closed.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Applied,
                                .Operation = EditorSceneFileOperation::Close,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                    },
                .VisualizationRecipes =
                    EditorVisualizationRecipeCommandSurface{
                        .GetRecipe =
                            [renderExtraction](const std::uint32_t stableEntityId)
                        {
                            return renderExtraction != nullptr
                                       ? renderExtraction->GetVisualizationRecipe(
                                             stableEntityId)
                                       : std::optional<VisualizationRecipe>{};
                        },
                        .SetRecipe =
                            [renderExtraction](
                                const std::uint32_t stableEntityId,
                                VisualizationRecipe recipe)
                        {
                            if (renderExtraction != nullptr)
                                renderExtraction->SetVisualizationRecipe(
                                    stableEntityId, std::move(recipe));
                        },
                        .ClearRecipe =
                            [renderExtraction](const std::uint32_t stableEntityId)
                        {
                            if (renderExtraction != nullptr)
                                renderExtraction->ClearVisualizationRecipe(stableEntityId);
                        },
                    },
                .VisualizationRecipeRevision =
                    renderExtraction != nullptr
                        ? renderExtraction->GetVisualizationRecipeRevision()
                        : 0u,
                .AssetImportQueue   = assetWorkflow != nullptr
                                          ? assetWorkflow->GetAssetImportQueueSnapshot()
                                          : RuntimeAssetImportQueueSnapshot{},
                .RenderGraphStats =
                    renderer != nullptr ? &renderer->GetLastRenderGraphStats() : nullptr,
                .ImGuiAdapterAvailable =
                    [&services]
                {
                    const EditorUiHost* host = services.Find<EditorUiHost>();
                    return host != nullptr && host->IsOperational();
                }(),
                .AssetImportCommandsAvailable   = assetWorkflow != nullptr,
                .SceneFileCommandsAvailable     = true,
                .CameraRenderCommandsAvailable  = true,
                .VisualizationCommandsAvailable = true,
            };
            if (configControl != nullptr)
            {
                context.RenderRecipeRuntimeState =
                    &configControl->GetRenderRecipeState();
                context.PreviewRenderRecipeDocument =
                    [configControl](const std::string& document,
                                    const std::string& sourceId)
                    {
                        return configControl
                            ->PreviewRenderRecipeConfigDocument(
                                document,
                                sourceId);
                    };
                context.ApplyRenderRecipePreview =
                    [configControl](
                        const Graphics::RenderRecipeConfigLoadResult&
                            loadResult)
                    {
                        return configControl
                            ->ApplyRenderRecipeConfigPreview(
                                loadResult,
                                RuntimeRenderRecipeActivationSource::Editor);
                    };
                context.RenderRecipeCommandsAvailable = true;
            }
            return context;
        }

} // namespace

EditorFileImportResult ProjectEditorFileImportResult(
    const RuntimeAssetImportEvent& event)
{
    return BuildFileImportResultFromRuntimeEvent(event);
}

EditorSceneFileResult ProjectEditorSceneFileResult(
    const RuntimeSceneFileEvent& event)
{
    return BuildSceneFileResultFromRuntimeEvent(event);
}

EditorFeatureBindings MakeEditorFeatureBindings(
    WorldRegistry& worlds,
    ServiceRegistry& services)
{
    return BuildContextFromRuntime(worlds, services);
}

EditorParameterizationUvViewCommandSurface
MakeEditorParameterizationUvViewCommandSurface(ServiceRegistry& services)
{
    RenderExtractionCache* const renderExtraction = services.Find<RenderExtractionCache>();
    RHI::IDevice* const device = services.Find<RHI::IDevice>();
    Graphics::IRenderer* const renderer = services.Find<Graphics::IRenderer>();
    return EditorParameterizationUvViewCommandSurface{
        .TextureTabs = [&services](const std::uint32_t entity)
        {
            std::vector<EditorParameterizationTextureTab> tabs{};
            const auto* bake = services.Find<TextureBakeService>();
            if (bake == nullptr) return tabs;
            const auto snapshot = bake->Snapshot(entity);
            const auto pack = [](const Assets::AssetId id)
            { return (static_cast<std::uint64_t>(id.Generation) << 32u) | id.Index; };
            for (const auto& record : snapshot.Textures)
            {
                auto state = record.State == PropertyTextureBakeOutputState::Pending
                    ? EditorParameterizationTextureState::Pending
                    : record.State == PropertyTextureBakeOutputState::Failed
                    ? EditorParameterizationTextureState::Failed
                    : record.Freshness == PropertyTextureBakeFreshness::Fresh
                    ? EditorParameterizationTextureState::Ready
                    : EditorParameterizationTextureState::Stale;
                std::string diagnostic = record.Diagnostic;
                if (state == EditorParameterizationTextureState::Stale)
                    diagnostic = DebugNameForPropertyTextureBakeFreshness(record.Freshness);
                tabs.push_back(EditorParameterizationTextureTab{
                    .Name = record.OutputName, .Diagnostic = std::move(diagnostic),
                    .TextureAssetId = pack(record.Texture), .CoverageTextureAssetId = pack(record.CoverageTexture),
                    .ResolvedTexcoords = record.ResolvedTexcoords,
                    .Width = record.Width, .Height = record.Height,
                    .RangeMin = record.RangeMin, .RangeMax = record.RangeMax, .State = state,
                    .RawFloat = record.Storage == PropertyTextureBakeStorage::RawFloat,
                    .Encoding = static_cast<std::uint32_t>(record.Encoding),
                    .Colormap = static_cast<std::uint32_t>(record.EncodingColormap),
                    .Revision = record.Generation,
                });
            }
            return tabs;
        },
        .Submit =
            [renderer, device, &services, renderExtraction](
                EditorParameterizationUvViewRequest request)
        {
            if (renderer == nullptr || device == nullptr)
            {
                return EditorParameterizationUvViewState{};
            }
            return SubmitRuntimeParameterizationUvView(
                *renderer, *device, services, renderExtraction, std::move(request));
        },
    };
}

namespace
{
    [[nodiscard]] bool SameRenderSurface(
        const Graphics::Components::RenderSurface& lhs,
        const Graphics::Components::RenderSurface& rhs) noexcept
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
        const Graphics::Components::RenderEdges& lhs,
        const Graphics::Components::RenderEdges& rhs)
    {
        return lhs.Domain == rhs.Domain &&
               SameRenderScalarSource(lhs.WidthSource, rhs.WidthSource);
    }

    [[nodiscard]] bool SameRenderPoints(
        const Graphics::Components::RenderPoints& lhs,
        const Graphics::Components::RenderPoints& rhs)
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

}

    [[nodiscard]] EditorRenderHintComponents ReadRenderHintComponents(
        const entt::registry& raw,
        const ECS::EntityHandle entity)
    {
        EditorRenderHintComponents state{};
        if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
            state.Surface = *surface;
        if (const auto* lines = raw.try_get<G::RenderEdges>(entity))
            state.Edges = *lines;
        if (const auto* points = raw.try_get<G::RenderPoints>(entity))
            state.Points = *points;
        return state;
    }

    bool SameRenderHintComponents(
        const EditorRenderHintComponents& lhs,
        const EditorRenderHintComponents& rhs)
    {
        return SameOptionalRenderComponent(lhs.Surface, rhs.Surface, SameRenderSurface) &&
               SameOptionalRenderComponent(lhs.Edges, rhs.Edges, SameRenderEdges) &&
               SameOptionalRenderComponent(lhs.Points, rhs.Points, SameRenderPoints);
    }

    [[nodiscard]] EditorCommandHistoryStatus ApplyRenderHintComponents(
        ECS::Scene::Registry* scene,
        const std::uint32_t stableEntityId,
        const EditorRenderHintComponents& state)
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

} // namespace Extrinsic::Runtime::EditorFeatureDetail
} // extern "C++"
