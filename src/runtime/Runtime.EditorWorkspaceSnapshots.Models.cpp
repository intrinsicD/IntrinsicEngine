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

module Extrinsic.Runtime.EditorWorkspaceSnapshots;

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
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
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
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.Private.EditorFeatures;
import Geometry.Graph;
import Geometry.Properties;
import Geometry.UvAtlas;

#include "Runtime.EditorMutation.Internal.hpp"

namespace Extrinsic::Runtime::EditorFeatureDetail {
namespace {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;

        using EditorModelBuildClock = std::chrono::steady_clock;

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
            const entt::registry& raw,
            std::uint32_t stableId);

        [[nodiscard]] std::uint64_t EditorElapsedNs(
            const EditorModelBuildClock::time_point start) noexcept
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    EditorModelBuildClock::now() - start)
                    .count();
            return elapsed > 0
                ? static_cast<std::uint64_t>(elapsed)
                : 1u;
        }

        class ScopedEditorStatTimer final
        {
        public:
            explicit ScopedEditorStatTimer(
                std::uint64_t* target) noexcept
                : m_Target(target)
            {
                if (m_Target != nullptr)
                    m_Start = EditorModelBuildClock::now();
            }

            ScopedEditorStatTimer(
                const ScopedEditorStatTimer&) = delete;
            ScopedEditorStatTimer& operator=(
                const ScopedEditorStatTimer&) = delete;

            ~ScopedEditorStatTimer()
            {
                if (m_Target != nullptr)
                    *m_Target += EditorElapsedNs(m_Start);
            }

        private:
            std::uint64_t* m_Target{nullptr};
            EditorModelBuildClock::time_point m_Start{};
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

        [[nodiscard]] GS::Domain ExpectedDomainForWindowKind(
            const EditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case EditorDomainWindowKind::Mesh:
                return GS::Domain::Mesh;
            case EditorDomainWindowKind::Graph:
                return GS::Domain::Graph;
            case EditorDomainWindowKind::PointCloud:
                return GS::Domain::PointCloud;
            }
            return GS::Domain::None;
        }

        [[nodiscard]] EditorSelectedAnalysisCacheConsumer
        SelectedAnalysisCacheConsumerForWindowKind(
            const EditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case EditorDomainWindowKind::Mesh:
                return EditorSelectedAnalysisCacheConsumer::MeshDomainWindow;
            case EditorDomainWindowKind::Graph:
                return EditorSelectedAnalysisCacheConsumer::GraphDomainWindow;
            case EditorDomainWindowKind::PointCloud:
                return EditorSelectedAnalysisCacheConsumer::
                    PointCloudDomainWindow;
            }
            return EditorSelectedAnalysisCacheConsumer::Inspector;
        }

        [[nodiscard]] EditorVisualizationConfigModel FromVisualizationConfig(
            const G::VisualizationConfig& config)
        {
            return EditorVisualizationConfigModel{
                .HasConfig = true,
                .Source = config.Source,
                .Color = config.Color,
                .ScalarFieldName = config.ScalarFieldName,
                .ScalarDomain = config.ScalarDomain,
                .ColorBufferName = config.ColorBufferName,
                .ScalarAutoRange = config.Scalar.AutoRange,
                .ScalarRangeMin = config.Scalar.RangeMin,
                .ScalarRangeMax = config.Scalar.RangeMax,
                .ScalarBinCount = config.Scalar.BinCount,
                .IsolineCount = config.Scalar.Isolines.Num,
                .ScalarColormap = config.Scalar.Map,
                .IsolineWidth = config.Scalar.Isolines.Width,
                .IsolineColor = config.Scalar.Isolines.Color,
                .IsolineValues = config.Scalar.Isolines.Values,
                .IsolineValueCount = config.Scalar.Isolines.ValueCount,
            };
        }

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

        [[nodiscard]] EditorVisualizationConfigModel
        BuildVisualizationConfigModelForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const EditorVisualizationTarget target)
        {
            const std::optional<G::VisualizationConfig> config =
                EffectiveVisualizationConfigForTarget(raw, entity, target);
            return config.has_value()
                ? FromVisualizationConfig(*config)
                : EditorVisualizationConfigModel{};
        }

        struct EditorVisualizationMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            EditorVisualizationTarget Target{
                EditorVisualizationTarget::Entity};
        };

        [[nodiscard]] bool IsInternalVisualizationProperty(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == GS::PropertyNames::kNormal ||
                   name == GS::PropertyNames::kEdgeV0 ||
                   name == GS::PropertyNames::kEdgeV1 ||
                   name == GS::PropertyNames::kHalfedgeToVertex ||
                   name == GS::PropertyNames::kHalfedgeNext ||
                   name == GS::PropertyNames::kHalfedgeFace ||
                   name == GS::PropertyNames::kFaceHalfedge ||
                   name == "v:point" ||
                   name == "v:tex" ||
                   name == "v:texcoord" ||
                   name == "p:position" ||
                   name == "p:normal";
        }

        [[nodiscard]] bool IsConnectivityVisualizationProperty(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == GS::PropertyNames::kEdgeV0 ||
                   name == GS::PropertyNames::kEdgeV1 ||
                   name == GS::PropertyNames::kHalfedgeToVertex ||
                   name == GS::PropertyNames::kHalfedgeNext ||
                   name == GS::PropertyNames::kHalfedgeFace ||
                   name == GS::PropertyNames::kFaceHalfedge ||
                   name == "v:point" ||
                   name == "v:tex" ||
                   name == "v:texcoord" ||
                   name == "p:position";
        }

        [[nodiscard]] bool IsScalarVisualizationKind(
            const Geometry::PropertyValueKind kind) noexcept
        {
            return kind == Geometry::PropertyValueKind::Float ||
                   kind == Geometry::PropertyValueKind::Double;
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
            EditorVisualizationPropertyDomain domain);

        [[nodiscard]] EditorVisualizationTarget
        VisualizationTargetForWindowKind(
            const EditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case EditorDomainWindowKind::Mesh:
                return EditorVisualizationTarget::Surface;
            case EditorDomainWindowKind::Graph:
                return EditorVisualizationTarget::Edges;
            case EditorDomainWindowKind::PointCloud:
                return EditorVisualizationTarget::Points;
            }
            return EditorVisualizationTarget::Entity;
        }

        [[nodiscard]] bool VisualizationTargetAvailableForView(
            const GeometryEntityAvailability& availability,
            const EditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case EditorVisualizationTarget::Entity:
                return availability.HasGeometry();
            case EditorVisualizationTarget::Surface:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Surface).Ready();
            case EditorVisualizationTarget::Edges:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Edges).Ready();
            case EditorVisualizationTarget::Points:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Points).Ready();
            }
            return false;
        }

        void AppendVisualizationPropertiesForTarget(
            std::vector<EditorVisualizationPropertyInfo>& out,
            const GeometryEntityAvailability& availability,
            const EditorVisualizationTarget target)
        {
            const auto append =
                [&](const EditorVisualizationPropertyDomain domain)
                {
                    if (const Geometry::PropertySet* properties =
                            PropertySetForVisualizationDomain(availability, domain))
                    {
                        AppendVisualizationPropertiesForDomain(
                            out,
                            *properties,
                            domain);
                    }
                };

            switch (target)
            {
            case EditorVisualizationTarget::Entity:
                append(EditorVisualizationPropertyDomain::MeshVertices);
                append(EditorVisualizationPropertyDomain::MeshEdges);
                append(EditorVisualizationPropertyDomain::MeshFaces);
                append(EditorVisualizationPropertyDomain::GraphVertices);
                append(EditorVisualizationPropertyDomain::GraphEdges);
                append(EditorVisualizationPropertyDomain::PointCloudPoints);
                break;
            case EditorVisualizationTarget::Surface:
                append(EditorVisualizationPropertyDomain::MeshVertices);
                append(EditorVisualizationPropertyDomain::MeshFaces);
                break;
            case EditorVisualizationTarget::Edges:
                append(EditorVisualizationPropertyDomain::MeshEdges);
                append(EditorVisualizationPropertyDomain::GraphEdges);
                break;
            case EditorVisualizationTarget::Points:
                append(EditorVisualizationPropertyDomain::MeshVertices);
                append(EditorVisualizationPropertyDomain::GraphVertices);
                append(EditorVisualizationPropertyDomain::PointCloudPoints);
                break;
            }
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
                // Kinds outside the visualization-capable set (Bool, Int32,
                // UInt64, Vec2) fall through every predicate below and are
                // skipped, exactly as the retired editor-local enum did by
                // returning nullopt for them.
                const Geometry::PropertyValueKind kind =
                    DetectGeometryPropertyValueKind(properties, name);
                if (kind == Geometry::PropertyValueKind::Unknown)
                    continue;

                const bool internal = IsInternalVisualizationProperty(name);
                const bool connectivity =
                    IsConnectivityVisualizationProperty(name);
                const bool scalar =
                    !internal && IsScalarVisualizationKind(kind);
                const bool color =
                    !internal && kind == Geometry::PropertyValueKind::Vec4;
                const bool vector =
                    !connectivity && kind == Geometry::PropertyValueKind::Vec3;
                const bool integer =
                    !internal && !connectivity &&
                    kind == Geometry::PropertyValueKind::UInt32;
                if (!scalar && !color && !vector && !integer)
                {
                    continue;
                }

                out.push_back(EditorVisualizationPropertyInfo{
                    .Name = name,
                    .Domain = domain,
                    .ValueKind = kind,
                    .ElementCount = properties.Size(),
                    .ScalarPresetAvailable = scalar,
                    .IsolinePresetAvailable = scalar,
                    .ColorBufferPresetAvailable = color,
                    .VectorFieldCandidate = vector,
                });
            }
        }

        [[nodiscard]] std::vector<EditorVisualizationPropertyInfo>
        BuildVisualizationProperties(const GeometryEntityAvailability& availability)
        {
            std::vector<EditorVisualizationPropertyInfo> out{};
            AppendVisualizationPropertiesForTarget(
                out,
                availability,
                EditorVisualizationTarget::Entity);
            return out;
        }

        [[nodiscard]] GeometryPropertyValueKindFilter DefaultExpectedValueKindForSlot(
            GeometryPresentationSlotSemantic semantic) noexcept;

        [[nodiscard]] GeometryElementDomain DefaultDomainForGeometryPresentationSlot(
            GS::Domain sourceDomain,
            GeometryRenderLane lane,
            GeometryPresentationSlotSemantic semantic) noexcept;
        void AddDiagnostic(
            std::vector<EditorDiagnostic>& diagnostics,
            EditorDiagnosticCode code,
            std::string message);

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
            case Domain::GraphEdges:
                return GeometryElementDomain::GraphEdge;
            case Domain::PointCloudPoints:
                return GeometryElementDomain::PointCloudPoint;
            }
            return GeometryElementDomain::Unknown;
        }

        // Kinds the property catalog surfaces. Bool/Int32/UInt64 were never
        // representable in the retired editor-local enum (they collapsed to
        // Unknown), so they stay unsupported here rather than silently becoming
        // bindable now that the canonical vocabulary can name them.
        [[nodiscard]] bool IsPropertyCatalogSupportedKind(
            const Geometry::PropertyValueKind kind) noexcept
        {
            switch (kind)
            {
            case Geometry::PropertyValueKind::Float:
            case Geometry::PropertyValueKind::Double:
            case Geometry::PropertyValueKind::UInt32:
            case Geometry::PropertyValueKind::Vec2:
            case Geometry::PropertyValueKind::Vec3:
            case Geometry::PropertyValueKind::Vec4:
                return true;
            case Geometry::PropertyValueKind::Unknown:
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
                break;
            }
            return false;
        }

        [[nodiscard]] std::uint8_t ComponentCountForPropertyCatalogKind(
            const Geometry::PropertyValueKind kind) noexcept
        {
            using Kind = Geometry::PropertyValueKind;
            switch (kind)
            {
            case Kind::Float:
            case Kind::Double:
            case Kind::UInt32:
                return 1u;
            case Kind::Vec2:
                return 2u;
            case Kind::Vec3:
                return 3u;
            case Kind::Vec4:
                return 4u;
            case Kind::Unknown:
            case Kind::Bool:
            case Kind::Int32:
            case Kind::UInt64:
                break;
            }
            return 0u;
        }

        [[nodiscard]] bool IsGeneratedCatalogProperty(
            const std::string& name) noexcept
        {
            return name.find("kmeans") != std::string::npos ||
                   name.find("generated") != std::string::npos ||
                   name.find("bake") != std::string::npos;
        }

        [[nodiscard]] std::string FormatVec2(const glm::vec2 value)
        {
            return "(" + std::to_string(value.x) + ", " +
                   std::to_string(value.y) + ")";
        }

        [[nodiscard]] std::string FormatVec3(const glm::vec3 value)
        {
            return "(" + std::to_string(value.x) + ", " +
                   std::to_string(value.y) + ", " +
                   std::to_string(value.z) + ")";
        }

        [[nodiscard]] std::string FormatVec4(const glm::vec4 value)
        {
            return "(" + std::to_string(value.x) + ", " +
                   std::to_string(value.y) + ", " +
                   std::to_string(value.z) + ", " +
                   std::to_string(value.w) + ")";
        }

        [[nodiscard]] EditorPropertyValuePreview BuildPropertyValuePreview(
            const Geometry::PropertySet& properties,
            const std::string& name,
            const Geometry::PropertyValueKind kind,
            const std::optional<std::size_t> index)
        {
            if (!index.has_value() || *index >= properties.Size())
                return {};

            EditorPropertyValuePreview preview{
                .HasValue = true,
                .ElementIndex = *index,
            };

            using Kind = Geometry::PropertyValueKind;
            switch (kind)
            {
            case Kind::Float:
                if (const auto prop = properties.Get<float>(name); prop)
                    preview.Text = std::to_string(prop.Vector()[*index]);
                break;
            case Kind::Double:
                if (const auto prop = properties.Get<double>(name); prop)
                    preview.Text = std::to_string(prop.Vector()[*index]);
                break;
            case Kind::UInt32:
                if (const auto prop = properties.Get<std::uint32_t>(name); prop)
                    preview.Text = std::to_string(prop.Vector()[*index]);
                break;
            case Kind::Vec2:
                if (const auto prop = properties.Get<glm::vec2>(name); prop)
                    preview.Text = FormatVec2(prop.Vector()[*index]);
                break;
            case Kind::Vec3:
                if (const auto prop = properties.Get<glm::vec3>(name); prop)
                    preview.Text = FormatVec3(prop.Vector()[*index]);
                break;
            case Kind::Vec4:
                if (const auto prop = properties.Get<glm::vec4>(name); prop)
                    preview.Text = FormatVec4(prop.Vector()[*index]);
                break;
            case Kind::Unknown:
            case Kind::Bool:
            case Kind::Int32:
            case Kind::UInt64:
                preview.HasValue = false;
                break;
            }

            if (preview.Text.empty())
                preview.HasValue = false;
            return preview;
        }

        [[nodiscard]] std::optional<std::size_t> PreviewIndexForCatalogDomain(
            const EditorPropertyCatalogDomain domain,
            const PrimitiveSelectionResult* primitive,
            const std::uint32_t selectedStableId) noexcept
        {
            if (primitive == nullptr)
                return std::nullopt;
            const bool sameEntity =
                primitive->EntityId == selectedStableId ||
                primitive->StableId == selectedStableId;
            if (!sameEntity)
                return std::nullopt;

            using Domain = EditorPropertyCatalogDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                if (primitive->Domain == GS::Domain::Mesh &&
                    primitive->VertexId != kInvalidPrimitiveIndex)
                    return primitive->VertexId;
                break;
            case Domain::MeshEdges:
                if (primitive->Domain == GS::Domain::Mesh &&
                    primitive->EdgeId != kInvalidPrimitiveIndex)
                    return primitive->EdgeId;
                break;
            case Domain::MeshFaces:
                if (primitive->Domain == GS::Domain::Mesh &&
                    primitive->FaceId != kInvalidPrimitiveIndex)
                    return primitive->FaceId;
                break;
            case Domain::GraphVertices:
                if (primitive->Domain == GS::Domain::Graph &&
                    primitive->VertexId != kInvalidPrimitiveIndex)
                    return primitive->VertexId;
                break;
            case Domain::GraphEdges:
                if (primitive->Domain == GS::Domain::Graph &&
                    primitive->EdgeId != kInvalidPrimitiveIndex)
                    return primitive->EdgeId;
                break;
            case Domain::PointCloudPoints:
                if (primitive->Domain == GS::Domain::PointCloud &&
                    primitive->PointId != kInvalidPrimitiveIndex)
                    return primitive->PointId;
                break;
            case Domain::MeshHalfedges:
                break;
            }
            return std::nullopt;
        }

        void AppendPropertyCatalogRowsForDomain(
            std::vector<EditorPropertyCatalogRow>& out,
            const Geometry::PropertySet& properties,
            const EditorPropertyCatalogDomain domain,
            const std::optional<std::size_t> previewIndex)
        {
            for (const std::string& name : properties.Properties())
            {
                const Geometry::PropertyValueKind kind =
                    DetectGeometryPropertyValueKind(properties, name);
                const bool supported = IsPropertyCatalogSupportedKind(kind);
                EditorPropertyCatalogRow row{
                    .Name = name,
                    .Domain = domain,
                    .ValueKind = kind,
                    .ElementCount = properties.Size(),
                    .ComponentCount = ComponentCountForPropertyCatalogKind(kind),
                    .Supported = supported,
                    .Bindable = supported,
                    .Canonical = IsInternalVisualizationProperty(name),
                    .Internal = IsInternalVisualizationProperty(name),
                    .Connectivity = IsConnectivityVisualizationProperty(name),
                    .Generated = IsGeneratedCatalogProperty(name),
                    .Descriptor = GeometryPropertyRef{
                        .Domain = ToGeometryElementDomain(domain),
                        .Name = name,
                        .ValueKind = kind,
                    },
                    .Preview = BuildPropertyValuePreview(
                        properties,
                        name,
                        kind,
                        previewIndex),
                };
                if (!supported)
                    row.UnsupportedReason = "unsupported property value type";
                out.push_back(std::move(row));
            }
        }

        void AppendPropertyCatalogRows(
            std::vector<EditorPropertyCatalogRow>& out,
            const GS::ConstSourceView& view,
            const PrimitiveSelectionResult* primitive,
            const std::uint32_t selectedStableId)
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            const auto append =
                [&](const EditorPropertyCatalogDomain domain)
                {
                    const Geometry::PropertySet* properties =
                        PropertySetForCatalogDomain(availability, domain);
                    if (properties == nullptr)
                        return;
                    AppendPropertyCatalogRowsForDomain(
                        out,
                        *properties,
                        domain,
                        PreviewIndexForCatalogDomain(
                            domain,
                            primitive,
                            selectedStableId));
                };

            append(EditorPropertyCatalogDomain::MeshVertices);
            append(EditorPropertyCatalogDomain::MeshEdges);
            append(EditorPropertyCatalogDomain::MeshHalfedges);
            append(EditorPropertyCatalogDomain::MeshFaces);
            append(EditorPropertyCatalogDomain::GraphVertices);
            append(EditorPropertyCatalogDomain::GraphEdges);
            append(EditorPropertyCatalogDomain::PointCloudPoints);
        }

        [[nodiscard]] EditorPropertyBindingTargetModel
        BuildPropertyBindingTargetModel(
            const GS::ConstSourceView& view,
            const GeometryPresentationSlotSnapshot& slot)
        {
            GeometryElementDomain domain = slot.Property.Domain;
            if (domain == GeometryElementDomain::Unknown)
            {
                const GS::SourceAvailability availability =
                    GS::BuildSourceAvailability(view);
                domain = DefaultDomainForGeometryPresentationSlot(
                    availability.ProvenanceDomain,
                    slot.Lane,
                    slot.Semantic);
            }

            GeometryPropertyValueKindFilter expected{};
            if (slot.Property.ValueKind != Geometry::PropertyValueKind::Unknown)
                expected = slot.Property.ValueKind;
            else
            {
                expected = DefaultExpectedValueKindForSlot(slot.Semantic);
            }

            const GeometryEntityAvailability geometryAvailability =
                BuildGeometryAvailability(view);

            EditorPropertyBindingTargetModel model{
                .Lane = slot.Lane,
                .PresentationKey = slot.PresentationKey,
                .PresentationKind = slot.PresentationKind,
                .Semantic = slot.Semantic,
                .SourceKind = slot.SourceKind,
                .RequiredDomain = domain,
                .ExpectedValueKind = expected,
                .ExpectedElementCount = ResolveGeometryElementCount(
                    geometryAvailability,
                    domain),
            };

            if (domain != GeometryElementDomain::Unknown)
            {
                std::vector<GeometryPresentationPropertyOption> options =
                    EnumerateGeometryPresentationPropertyOptions(
                        view,
                        domain,
                        expected);
                model.Options.reserve(options.size());
                for (const GeometryPresentationPropertyOption& option : options)
                {
                    model.Options.push_back(
                        EditorGeometryPresentationPropertyOptionModel{
                            .Descriptor = option.Property,
                            .ActualValueKind = option.Property.ValueKind,
                            .ElementCount = option.ElementCount,
                            .Compatible = option.Compatible,
                            .DisabledReason = option.DisabledReason,
                        });
                }
            }
            return model;
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

        [[nodiscard]] const char* VertexChannelExpectedTypeText(
            const VertexChannel channel) noexcept
        {
            switch (channel)
            {
            case VertexChannel::Normal:
                return "requires vec3";
            case VertexChannel::Color:
                return "requires vec3 or vec4";
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return "unsupported vertex channel";
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

        [[nodiscard]] std::string BuildVertexChannelResolverDiagnostic(
            const AttributeBindResult& resolver)
        {
            std::string diagnostic =
                std::string(DebugNameForAttributeBindStatus(resolver.Status));
            diagnostic += " source=";
            diagnostic += std::to_string(resolver.SourceCount);
            diagnostic += " fallback=";
            diagnostic += std::to_string(resolver.FallbackCount);
            diagnostic += " nonFinite=";
            diagnostic += std::to_string(resolver.NonFiniteCount);
            return diagnostic;
        }

        [[nodiscard]] const VertexChannelSourceBinding*
        FindVertexChannelBinding(
            const VertexChannelBindingSet* bindings,
            const VertexChannel channel) noexcept
        {
            if (bindings == nullptr)
                return nullptr;
            switch (channel)
            {
            case VertexChannel::Normal:
                return &bindings->Normal;
            case VertexChannel::Color:
                return &bindings->Color;
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return nullptr;
        }

        struct VertexChannelBindingMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            VertexChannel Channel{VertexChannel::Custom};
        };

        [[nodiscard]] EditorVertexChannelBindingTargetModel
        BuildVertexChannelBindingTargetModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& view,
            const std::vector<EditorPropertyCatalogRow>& rows,
            const EditorPropertyCatalogDomain domain,
            const VertexChannel channel,
            EditorWorkspaceSnapshotStats* modelBuildStats)
        {
            EditorVertexChannelBindingTargetModel model{
                .Channel = channel,
            };

            const Geometry::PropertySet* properties =
                VertexChannelPropertySetForView(view, domain);
            if (properties == nullptr)
                return model;

            const std::size_t expectedCount = properties->Size();
            const auto* bindings = raw.try_get<VertexChannelBindingSet>(entity);
            if (const VertexChannelSourceBinding* binding =
                    FindVertexChannelBinding(bindings, channel);
                binding != nullptr && IsVertexChannelBindingEnabled(*binding))
            {
                model.HasBinding = true;
                model.Binding = *binding;
                const std::optional<AttributeSourceType> sourceType =
                    binding->Property.Domain ==
                            ToGeometryElementDomain(domain)
                        ? ToAttributeSourceType(
                              binding->Property.ValueKind)
                        : std::nullopt;
                model.Resolver = sourceType.has_value()
                    ? EvaluateVertexChannelBinding(
                          *properties,
                          channel,
                          binding->Property.Name,
                          *sourceType,
                          expectedCount,
                          modelBuildStats)
                    : AttributeBindResult{
                          .Status = AttributeBindStatus::TypeMismatch,
                          .FullyPopulated = false,
                      };
                model.Diagnostic =
                    BuildVertexChannelResolverDiagnostic(model.Resolver);
            }

            for (const EditorPropertyCatalogRow& row : rows)
            {
                if (row.Domain != domain || !row.Supported)
                    continue;

                EditorVertexChannelBindingOptionModel option{
                    .PropertyName = row.Name,
                    .Domain = row.Domain,
                    .ValueKind = row.ValueKind,
                    .ElementCount = row.ElementCount,
                };
                const std::optional<AttributeSourceType> sourceType =
                    ToAttributeSourceType(row.ValueKind);
                if (!sourceType.has_value())
                {
                    option.Resolver = AttributeBindResult{
                        .Status = AttributeBindStatus::TypeMismatch,
                        .FullyPopulated = false,
                    };
                    option.Compatible = false;
                    option.DisabledReason =
                        VertexChannelExpectedTypeText(channel);
                    model.Options.push_back(std::move(option));
                    continue;
                }

                option.SourceType = *sourceType;
                option.Resolver = EvaluateVertexChannelBinding(
                    *properties,
                    channel,
                    row.Name,
                    *sourceType,
                    expectedCount,
                    modelBuildStats);
                option.Compatible =
                    SourceTypeAllowedForVertexChannel(channel, *sourceType) &&
                    option.Resolver.Ok();
                if (!option.Compatible)
                {
                    option.DisabledReason =
                        !SourceTypeAllowedForVertexChannel(channel, *sourceType)
                            ? VertexChannelExpectedTypeText(channel)
                            : BuildVertexChannelResolverDiagnostic(
                                  option.Resolver);
                }
                model.Options.push_back(std::move(option));
            }
            return model;
        }

        void AppendVertexChannelBindingTargets(
            EditorPropertyCatalogModel& model,
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& view)
        {
            const std::optional<EditorPropertyCatalogDomain> domain =
                VertexChannelCatalogDomainForView(view);
            if (!domain.has_value())
                return;

            if (context.ModelBuildStats != nullptr)
            {
                context.ModelBuildStats->VertexChannelTargetBuilds += 2u;
            }
            model.VertexChannelTargets.push_back(
                BuildVertexChannelBindingTargetModel(
                    raw,
                    entity,
                    view,
                    model.Rows,
                    *domain,
                    VertexChannel::Normal,
                    context.ModelBuildStats));
            model.VertexChannelTargets.push_back(
                BuildVertexChannelBindingTargetModel(
                    raw,
                    entity,
                    view,
                    model.Rows,
                    *domain,
                    VertexChannel::Color,
                    context.ModelBuildStats));
        }

        [[nodiscard]] EditorPropertyCatalogModel BuildPropertyCatalogModel(
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            ScopedEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->PropertyCatalogModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->PropertyCatalogModelBuilds;
            }
            EditorPropertyCatalogModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = SelectionController::ToStableEntityId(entity);
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            model.SelectedDomain = view.ActiveDomain;

            const PrimitiveSelectionResult* primitive = nullptr;
            if (context.LastRefinedPrimitive != nullptr &&
                context.LastRefinedPrimitive->has_value())
            {
                primitive = &**context.LastRefinedPrimitive;
            }

            AppendPropertyCatalogRows(
                model.Rows,
                view,
                primitive,
                model.SelectedStableId);

            if (const auto* bindings =
                    raw.try_get<GeometryPresentationRecipe>(entity);
                bindings != nullptr)
            {
                const GeometryPresentationSnapshot snapshot =
                    BuildGeometryPresentationSnapshot(
                        view,
                        *bindings,
                        raw.try_get<GeometryPresentationRuntimeState>(entity)
                            ? *raw.try_get<GeometryPresentationRuntimeState>(entity)
                            : GeometryPresentationRuntimeState{});
                model.BindingTargets.reserve(snapshot.Slots.size());
                for (const GeometryPresentationSlotSnapshot& slot : snapshot.Slots)
                    model.BindingTargets.push_back(
                        BuildPropertyBindingTargetModel(view, slot));
            }

            AppendVertexChannelBindingTargets(model, context, raw, entity, view);

            if (!view.Valid() && model.Rows.empty())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    EditorDiagnosticCode::UnsupportedGeometryDomain,
                    "Selected entity has no valid geometry property catalog.");
            }
            return model;
        }

        [[nodiscard]] EditorVisualizationRecipeModel
        FromVisualizationRecipe(const VisualizationRecipe& recipe)
        {
            return EditorVisualizationRecipeModel{
                .HasRecipe = true,
                .Kind = GetVisualizationRecipeKind(recipe),
                .Recipe = recipe,
            };
        }

        [[nodiscard]] const char* RenderSurfaceDomainName(
            const G::RenderSurface::SourceDomain domain) noexcept
        {
            switch (domain)
            {
            case G::RenderSurface::SourceDomain::Vertex: return "Vertex";
            case G::RenderSurface::SourceDomain::Face: return "Face";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* RenderEdgeDomainName(
            const G::RenderEdges::SourceDomain domain) noexcept
        {
            switch (domain)
            {
            case G::RenderEdges::SourceDomain::Vertex: return "Vertex";
            case G::RenderEdges::SourceDomain::Edge: return "Edge";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* RenderPointTypeName(
            const G::RenderPoints::RenderType type) noexcept
        {
            switch (type)
            {
            case G::RenderPoints::RenderType::Flat: return "Flat";
            case G::RenderPoints::RenderType::Sphere: return "Sphere";
            case G::RenderPoints::RenderType::Surfel: return "Surfel";
            }
            return "Unknown";
        }

        struct EditorRenderHintState
        {
            std::optional<G::RenderSurface> Surface{};
            std::optional<G::RenderEdges> Edges{};
            std::optional<G::RenderPoints> Points{};
        };

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

        struct EditorRenderHintMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        // RUNTIME-194 Slice B5d: result payload for this file's editor jobs.
        // The computed result already reaches the main thread in the shared job
        // state the worker fills, so the envelope carries only the diagnostic
        // the retired `DerivedJobOutput` exposed — and exists at all because an
        // empty envelope is how `JobService` reports a dropped job.
        struct EditorJobResult
        {
            std::string Diagnostic{};
        };

        using EditorJobIdentityIndex =
            std::unordered_map<JobToken,
                               EditorJobIdentity,
                               Core::StrongHandleHash<JobTokenTag>>;

        [[nodiscard]] GeometryPropertyValueKindFilter DefaultExpectedValueKindForSlot(
            const GeometryPresentationSlotSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case GeometryPresentationSlotSemantic::Normal:
            case GeometryPresentationSlotSemantic::PointNormalOrientation:
                return Geometry::PropertyValueKind::Vec3;
            case GeometryPresentationSlotSemantic::Albedo:
            case GeometryPresentationSlotSemantic::PointColor:
            case GeometryPresentationSlotSemantic::LineColor:
                return Geometry::PropertyValueKind::Vec4;
            case GeometryPresentationSlotSemantic::Roughness:
            case GeometryPresentationSlotSemantic::Metallic:
            case GeometryPresentationSlotSemantic::ScalarField:
            case GeometryPresentationSlotSemantic::Displacement:
            case GeometryPresentationSlotSemantic::PointScalarField:
            case GeometryPresentationSlotSemantic::PointSize:
            case GeometryPresentationSlotSemantic::LineScalarField:
            case GeometryPresentationSlotSemantic::LineWidth:
                return Geometry::PropertyValueKind::Float;
            }
            return std::nullopt;
        }

        [[nodiscard]] GeometryElementDomain DefaultDomainForGeometryPresentationSlot(
            const GS::Domain sourceDomain,
            const GeometryRenderLane lane,
            const GeometryPresentationSlotSemantic semantic) noexcept
        {
            switch (sourceDomain)
            {
            case GS::Domain::Mesh:
                if (semantic == GeometryPresentationSlotSemantic::LineColor ||
                    semantic == GeometryPresentationSlotSemantic::LineScalarField ||
                    semantic == GeometryPresentationSlotSemantic::LineWidth)
                {
                    return GeometryElementDomain::MeshEdge;
                }
                if (semantic == GeometryPresentationSlotSemantic::ScalarField)
                    return GeometryElementDomain::MeshFace;
                if (lane == GeometryRenderLane::Edges)
                    return GeometryElementDomain::MeshEdge;
                return GeometryElementDomain::MeshVertex;
            case GS::Domain::Graph:
                if (lane == GeometryRenderLane::Edges ||
                    semantic == GeometryPresentationSlotSemantic::LineColor ||
                    semantic == GeometryPresentationSlotSemantic::LineScalarField ||
                    semantic == GeometryPresentationSlotSemantic::LineWidth)
                {
                    return GeometryElementDomain::GraphEdge;
                }
                return GeometryElementDomain::GraphNode;
            case GS::Domain::PointCloud:
                return GeometryElementDomain::PointCloudPoint;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return GeometryElementDomain::Unknown;
        }

        [[nodiscard]] EditorGeometryPresentationPropertyOptionModel
        ToGeometryPresentationPropertyOptionModel(const GeometryPresentationPropertyOption& option)
        {
            return EditorGeometryPresentationPropertyOptionModel{
                .Descriptor = option.Property,
                .ActualValueKind = option.Property.ValueKind,
                .ElementCount = option.ElementCount,
                .Compatible = option.Compatible,
                .DisabledReason = option.DisabledReason,
            };
        }

        [[nodiscard]] EditorJobDependencyModel
        ToEditorJobDependencyModel(
            const EditorJobDependency& dependency)
        {
            return EditorJobDependencyModel{
                .Job = dependency.Job,
                .Reason = dependency.Reason,
            };
        }

        [[nodiscard]] EditorJobModel ToEditorJobModel(
            const EditorJobRecord& job)
        {
            EditorJobModel model{
                .Handle = job.Token,
                .Key = job.Identity,
                .Name = job.Name,
                .RequestedJobDomain = job.RequestedJobDomain,
                .ResolvedJobDomain = job.ResolvedJobDomain,
                .Status = job.State,
                .NormalizedProgress = job.NormalizedProgress,
                .ProgressDeterminate = job.ProgressDeterminate,
                .PreviousOutputRetained = job.PreviousOutputRetained,
                .PayloadToken = job.PayloadToken,
                .ElapsedMilliseconds = job.ElapsedMilliseconds,
                .Diagnostic = job.Diagnostic,
            };
            model.Dependencies.reserve(job.Dependencies.size());
            for (const EditorJobDependency& dependency : job.Dependencies)
                model.Dependencies.push_back(
                    ToEditorJobDependencyModel(dependency));
            return model;
        }

        struct GeometryPresentationEditorState
        {
            GeometryPresentationRecipe Recipe{};
            GeometryPresentationRuntimeState Runtime{};
        };

        struct GeometryPresentationMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        struct GeometryPresentationSlotLookup
        {
            GeometryPresentationBindingRecipe* Presentation{nullptr};
            GeometryPresentationSlotRecipe* Slot{nullptr};
        };

        [[nodiscard]] EditorDiagnostic MakeDiagnostic(
            const EditorDiagnosticCode code,
            std::string message)
        {
            return EditorDiagnostic{
                .Code = code,
                .Message = std::move(message),
            };
        }

        void AddDiagnostic(std::vector<EditorDiagnostic>& diagnostics,
                           const EditorDiagnosticCode code,
                           std::string message)
        {
            diagnostics.push_back(MakeDiagnostic(code, std::move(message)));
        }

        void AppendDiagnostics(std::vector<EditorDiagnostic>& destination,
                               const std::vector<EditorDiagnostic>& source)
        {
            destination.insert(destination.end(), source.begin(), source.end());
        }

        [[nodiscard]] std::string FallbackEntityName(const ECS::EntityHandle entity)
        {
            return "Entity " + std::to_string(
                static_cast<std::uint32_t>(entity));
        }

        [[nodiscard]] EditorEntityRow BuildEntityRow(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            EditorEntityRow row{};
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

        [[nodiscard]] EditorTransformModel BuildTransformModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            EditorTransformModel model{};
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

        [[nodiscard]] EditorRenderHintModel BuildRenderHintModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            EditorRenderHintModel model{};
            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
            {
                model.HasRenderSurface = true;
                model.SurfaceDomainValue = surface->Domain;
                model.SurfaceDomain = RenderSurfaceDomainName(surface->Domain);
            }

            if (const auto* lines = raw.try_get<G::RenderEdges>(entity))
            {
                model.HasRenderEdges = true;
                model.EdgeDomainValue = lines->Domain;
                model.EdgeDomain = RenderEdgeDomainName(lines->Domain);
                if (const auto* width = std::get_if<float>(&lines->WidthSource))
                {
                    model.HasUniformEdgeWidth = true;
                    model.UniformEdgeWidth = *width;
                }
                else if (const auto* name = std::get_if<std::string>(&lines->WidthSource))
                {
                    model.HasNamedEdgeWidth = true;
                    model.EdgeWidthName = *name;
                }
            }

            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
            {
                model.HasRenderPoints = true;
                model.PointRenderTypeValue = points->Type;
                model.PointRenderType = RenderPointTypeName(points->Type);

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

        [[nodiscard]] EditorGeometryDomainModel BuildGeometryDomainModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            return EditorGeometryDomainModel{
                .Domain = view.ActiveDomain,
                .Valid = view.Valid(),
                .VertexCount = view.HasGraphTopologyMarker
                    ? 0u
                    : view.VerticesAlive(),
                .EdgeCount = view.EdgesAlive(),
                .HalfedgeCount = view.HalfedgesTotal(),
                .FaceCount = view.FacesAlive(),
                .NodeCount = view.HasGraphTopologyMarker
                    ? view.VerticesAlive()
                    : 0u,
            };
        }

        [[nodiscard]] GeometryPresentationShape InferGeometryPresentationShape(
            const GS::ConstSourceView& view)
        {
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            switch (availability.ProvenanceDomain)
            {
            case GS::Domain::Mesh:
                return GeometryPresentationShape::Mesh;
            case GS::Domain::Graph:
                return GeometryPresentationShape::Graph;
            case GS::Domain::PointCloud:
                return GeometryPresentationShape::PointCloud;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return GeometryPresentationShape::Unknown;
        }

        void AppendGeometryPresentationJobRowsForEntity(
            EditorGeometryPresentationModel& model,
            const EditorFeatureBindings& context,
            const std::uint32_t stableEntityId)
        {
            if (!context.JobCommands.SnapshotEntity)
                return;

            for (const EditorJobRecord& job :
                 context.JobCommands.SnapshotEntity(stableEntityId))
                model.Jobs.push_back(ToEditorJobModel(job));
        }

        void AccumulateGeometryPresentationJobSummaryForEntity(
            EditorGeometryCompositionSummary& summary,
            const EditorFeatureBindings& context,
            const std::uint32_t stableEntityId)
        {
            if (!context.JobCommands.SnapshotEntity)
                return;

            for (const EditorJobRecord& job :
                 context.JobCommands.SnapshotEntity(stableEntityId))
            {
                ++summary.ChildJobCount;
                if (IsActiveEditorJobState(job.State))
                    ++summary.ChildActiveJobCount;
                if (IsFailedEditorJobState(job.State))
                    ++summary.ChildFailedJobCount;
            }
        }

        [[nodiscard]] std::vector<EditorGeometryPresentationPropertyOptionModel>
        BuildGeometryPresentationSlotPropertyOptions(
            const GS::ConstSourceView& view,
            const GeometryPresentationSlotSnapshot& extractedSlot)
        {
            GeometryElementDomain domain = extractedSlot.Property.Domain;
            if (domain == GeometryElementDomain::Unknown)
            {
                const GS::SourceAvailability availability =
                    GS::BuildSourceAvailability(view);
                domain = DefaultDomainForGeometryPresentationSlot(
                    availability.ProvenanceDomain,
                    extractedSlot.Lane,
                    extractedSlot.Semantic);
            }
            if (domain == GeometryElementDomain::Unknown)
                return {};

            GeometryPropertyValueKindFilter expected{};
            if (extractedSlot.Property.ValueKind !=
                Geometry::PropertyValueKind::Unknown)
            {
                expected = extractedSlot.Property.ValueKind;
            }
            else
            {
                expected = DefaultExpectedValueKindForSlot(extractedSlot.Semantic);
            }

            std::vector<GeometryPresentationPropertyOption> options =
                EnumerateGeometryPresentationPropertyOptions(
                    view,
                    domain,
                    expected);

            std::vector<EditorGeometryPresentationPropertyOptionModel> out{};
            out.reserve(options.size());
            for (const GeometryPresentationPropertyOption& option : options)
                out.push_back(ToGeometryPresentationPropertyOptionModel(option));
            return out;
        }

        [[nodiscard]] EditorGeometryPresentationSlotModel ToGeometryPresentationSlotModel(
            const GS::ConstSourceView& view,
            const GeometryPresentationRecipe& bindings,
            const GeometryPresentationRuntimeState& runtimeState,
            const GeometryPresentationSlotSnapshot& extractedSlot)
        {
            EditorGeometryPresentationSlotModel model{
                .Lane = extractedSlot.Lane,
                .PresentationKey = extractedSlot.PresentationKey,
                .PresentationKind = extractedSlot.PresentationKind,
                .Semantic = extractedSlot.Semantic,
                .SourceKind = extractedSlot.SourceKind,
                .Readiness = extractedSlot.Readiness,
                .UniformDefault = extractedSlot.UniformDefault,
                .Property = extractedSlot.Property,
                .PropertyResolution = extractedSlot.PropertyResolution,
                .TextureAsset = extractedSlot.TextureAsset,
                .Enabled = extractedSlot.Enabled,
                .UsesUniformDefault = extractedSlot.UsesUniformDefault,
                .TextureReady = extractedSlot.TextureReady,
                .PropertyBufferReady = extractedSlot.PropertyBufferReady,
                .PreviousOutputRetained = extractedSlot.PreviousOutputRetained,
                .Unsupported = extractedSlot.Unsupported,
                .Diagnostic = extractedSlot.Diagnostic,
            };

            if (const GeometryPresentationBindingRecipe* presentation =
                    FindGeometryPresentationBinding(bindings, extractedSlot.PresentationKey))
            {
                if (const GeometryPresentationSlotRecipe* slot =
                        FindGeometryPresentationSlot(*presentation, extractedSlot.Semantic))
                {
                    model.AuthoredTexture = slot->TextureAsset;
                }
            }
            if (const GeometryPresentationSlotStatus* status =
                    FindGeometryPresentationSlotStatus(
                        runtimeState,
                        extractedSlot.PresentationKey,
                        extractedSlot.Semantic))
            {
                model.GeneratedTexture = status->GeneratedTexture;
            }

            model.PropertyOptions =
                BuildGeometryPresentationSlotPropertyOptions(view, extractedSlot);
            return model;
        }

        void AccumulateGeometryPresentationChildSummary(
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            EditorGeometryCompositionSummary& summary,
            const ECS::EntityHandle child)
        {
            if (!raw.valid(child))
                return;

            ++summary.ChildCount;
            const std::uint32_t childStableId =
                SelectionController::ToStableEntityId(child);
            AccumulateGeometryPresentationJobSummaryForEntity(
                summary,
                context,
                childStableId);

            const auto* bindings =
                raw.try_get<GeometryPresentationRecipe>(child);
            if (bindings == nullptr)
                return;

            ++summary.ChildRecipeCount;
            const GS::ConstSourceView childView = GS::BuildConstView(raw, child);
            const GeometryPresentationRuntimeState runtimeState =
                raw.try_get<GeometryPresentationRuntimeState>(child)
                    ? *raw.try_get<GeometryPresentationRuntimeState>(child)
                    : GeometryPresentationRuntimeState{};
            const GeometryPresentationSnapshot snapshot =
                BuildGeometryPresentationSnapshot(
                    childView,
                    *bindings,
                    runtimeState);

            summary.ChildSlotCount += snapshot.Stats.SlotCount;
            summary.ChildPendingSlotCount += snapshot.Stats.PendingSlotCount;
            summary.ChildFailedSlotCount += snapshot.Stats.FailedSlotCount;
            summary.ChildFailedSlotCount += snapshot.Stats.UnsupportedSlotCount;
        }

        void AccumulateGeometryPresentationCompositionSummary(
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            EditorGeometryPresentationModel& model,
            const ECS::EntityHandle entity)
        {
            const ECS::Hierarchy::Structure::HierarchyQueryResult children =
                ECS::Hierarchy::Structure::CollectChildren(raw, entity);
            if (!children.Succeeded())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    EditorDiagnosticCode::CorruptHierarchy,
                    std::string{"Geometry-presentation composition hierarchy query failed: "} +
                        ECS::Hierarchy::Structure::
                            DebugNameForHierarchyQueryStatus(children.Status) +
                        ".");
                return;
            }
            if (children.Entities.empty())
                return;

            EditorGeometryCompositionSummary& summary =
                model.Composition;
            summary.HasChildren = true;
            for (const ECS::EntityHandle child : children.Entities)
                AccumulateGeometryPresentationChildSummary(
                    context,
                    raw,
                    summary,
                    child);
        }

        [[nodiscard]] EditorGeometryPresentationModel
        BuildGeometryPresentationModel(
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->GeometryPresentationModelBuilds;
            }
            EditorGeometryPresentationModel model{};
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            model.Shape = InferGeometryPresentationShape(view);

            const std::uint32_t stableEntityId =
                SelectionController::ToStableEntityId(entity);
            AppendGeometryPresentationJobRowsForEntity(
                model,
                context,
                stableEntityId);
            AccumulateGeometryPresentationCompositionSummary(
                context,
                raw,
                model,
                entity);
            if (model.Composition.HasChildren)
                model.Shape = GeometryPresentationShape::Composition;

            const auto* recipe =
                raw.try_get<GeometryPresentationRecipe>(entity);
            if (recipe == nullptr)
                return model;

            model.HasRecipe = true;
            model.Shape = recipe->Shape;
            const GeometryPresentationRuntimeState runtimeState =
                raw.try_get<GeometryPresentationRuntimeState>(entity)
                    ? *raw.try_get<GeometryPresentationRuntimeState>(entity)
                    : GeometryPresentationRuntimeState{};

            const GeometryPresentationSnapshot snapshot =
                BuildGeometryPresentationSnapshot(
                    view,
                    *recipe,
                    runtimeState);
            model.RecipeGeneration = snapshot.RecipeGeneration;
            model.Stats = snapshot.Stats;
            model.Slots.reserve(snapshot.Slots.size());
            for (const GeometryPresentationSlotSnapshot& slot : snapshot.Slots)
            {
                model.Slots.push_back(
                    ToGeometryPresentationSlotModel(
                        view,
                        *recipe,
                        runtimeState,
                        slot));
            }

            if (snapshot.Stats.DiagnosticCount > 0u)
            {
                AddDiagnostic(
                    model.Diagnostics,
                    EditorDiagnosticCode::InvalidVisualizationProperty,
                    "Geometry presentation has slot diagnostics.");
            }
            return model;
        }

        [[nodiscard]] std::optional<std::size_t> FindCatalogMatchIndex(
            const EditorPropertyCatalogModel& catalog,
            const GeometryPropertyRef& descriptor)
        {
            if (descriptor.Domain == GeometryElementDomain::Unknown ||
                descriptor.Name.empty())
            {
                return std::nullopt;
            }

            for (std::size_t i = 0u; i < catalog.Rows.size(); ++i)
            {
                const EditorPropertyCatalogRow& row = catalog.Rows[i];
                if (row.Descriptor.Domain == descriptor.Domain &&
                    row.Name == descriptor.Name)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] EditorBoundRenderStateRow MakeRenderHintRow(
            std::string label,
            const GeometryRenderLane lane,
            const bool enabled,
            std::string sourceDescription,
            std::string disabledReason = {})
        {
            return EditorBoundRenderStateRow{
                .Kind = EditorBoundRenderStateRowKind::RenderHint,
                .Label = std::move(label),
                .Lane = lane,
                .Readiness = enabled
                    ? GeometryPresentationReadiness::Ready
                    : GeometryPresentationReadiness::Unset,
                .Enabled = enabled,
                .SourceDescription = std::move(sourceDescription),
                .DisabledReason = std::move(disabledReason),
            };
        }

        void AppendRenderHintRows(
            std::vector<EditorBoundRenderStateRow>& rows,
            const EditorRenderHintModel& hints)
        {
            rows.push_back(
                MakeRenderHintRow(
                    "Surface render hint",
                    GeometryRenderLane::Surface,
                    hints.HasRenderSurface,
                    hints.HasRenderSurface ? hints.SurfaceDomain : "not enabled",
                    hints.HasRenderSurface ? std::string{} : "surface rendering is not enabled"));

            std::string edgeSource = "not enabled";
            if (hints.HasRenderEdges)
            {
                if (hints.HasNamedEdgeWidth)
                    edgeSource = "property:" + hints.EdgeWidthName;
                else if (hints.HasUniformEdgeWidth)
                    edgeSource = "uniform:" + std::to_string(hints.UniformEdgeWidth);
                else
                    edgeSource = hints.EdgeDomain;
            }
            rows.push_back(
                MakeRenderHintRow(
                    "Edge render hint",
                    GeometryRenderLane::Edges,
                    hints.HasRenderEdges,
                    std::move(edgeSource),
                    hints.HasRenderEdges ? std::string{} : "edge rendering is not enabled"));

            std::string pointSource = "not enabled";
            if (hints.HasRenderPoints)
            {
                if (hints.HasNamedPointSize)
                    pointSource = "property:" + hints.PointSizeName;
                else if (hints.HasUniformPointSize)
                    pointSource = "uniform:" + std::to_string(hints.UniformPointSize);
                else
                    pointSource = hints.PointRenderType;
            }
            rows.push_back(
                MakeRenderHintRow(
                    "Point render hint",
                    GeometryRenderLane::Points,
                    hints.HasRenderPoints,
                    std::move(pointSource),
                    hints.HasRenderPoints ? std::string{} : "point rendering is not enabled"));
        }

        void AppendBoundSlotRows(
            std::vector<EditorBoundRenderStateRow>& rows,
            const EditorPropertyCatalogModel& catalog,
            const EditorGeometryPresentationModel& presentation)
        {
            for (const EditorGeometryPresentationSlotModel& slot :
                 presentation.Slots)
            {
                const std::optional<std::size_t> match =
                    FindCatalogMatchIndex(catalog, slot.Property);
                rows.push_back(EditorBoundRenderStateRow{
                    .Kind = EditorBoundRenderStateRowKind::GeometryPresentationSlot,
                    .Label = std::string{ToString(slot.Semantic)},
                    .Lane = slot.Lane,
                    .PresentationKey = slot.PresentationKey,
                    .PresentationKind = slot.PresentationKind,
                    .Semantic = slot.Semantic,
                    .SourceKind = slot.SourceKind,
                    .Readiness = slot.Readiness,
                    .Property = slot.Property,
                    .PropertyResolution = slot.PropertyResolution,
                    .AuthoredTexture = slot.AuthoredTexture,
                    .GeneratedTexture = slot.GeneratedTexture,
                    .TextureAsset = slot.TextureAsset,
                    .Enabled = slot.Enabled,
                    .UsesUniformDefault = slot.UsesUniformDefault,
                    .TextureReady = slot.TextureReady,
                    .PropertyBufferReady = slot.PropertyBufferReady,
                    .PreviousOutputRetained = slot.PreviousOutputRetained,
                    .Unsupported = slot.Unsupported,
                    .HasCatalogMatch = match.has_value(),
                    .CatalogRowIndex = match,
                    .SourceDescription = std::string{ToString(slot.SourceKind)},
                    .DisabledReason = slot.Enabled ? std::string{} : "slot is disabled",
                    .Diagnostic = slot.Diagnostic,
                });
            }
        }

        void AppendBoundJobRows(
            std::vector<EditorBoundRenderStateRow>& rows,
            const EditorGeometryPresentationModel& presentation)
        {
            for (const EditorJobModel& job :
                 presentation.Jobs)
            {
                rows.push_back(EditorBoundRenderStateRow{
                    .Kind = EditorBoundRenderStateRowKind::DerivedJob,
                    .Label = job.Name,
                    .Lane = GeometryRenderLane::Surface,
                    .Semantic = job.Key.OutputSemantic,
                    .Readiness = IsFailedEditorJobState(job.Status)
                        ? GeometryPresentationReadiness::Failed
                        : (IsActiveEditorJobState(job.Status)
                               ? GeometryPresentationReadiness::Pending
                               : GeometryPresentationReadiness::Ready),
                    .Job = job.Handle,
                    .JobStatus = job.Status,
                    .JobProgress = job.NormalizedProgress,
                    .JobProgressDeterminate = job.ProgressDeterminate,
                    .Enabled = true,
                    .PreviousOutputRetained = job.PreviousOutputRetained,
                    .SourceDescription = job.Key.OutputName,
                    .Diagnostic = job.Diagnostic,
                });
            }
        }

        [[nodiscard]] EditorBoundRenderStateModel BuildBoundRenderStateModel(
            const EditorFeatureBindings& context,
            const EditorPropertyCatalogModel& catalog,
            const EditorGeometryPresentationModel& presentation,
            const EditorRenderHintModel& renderHints,
            const EditorGeometryDomainModel& geometry,
            const std::uint32_t stableEntityId)
        {
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->BoundStateModelBuilds;
            }
            EditorBoundRenderStateModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = stableEntityId;
            model.Shape = presentation.Shape;
            model.RecipeGeneration = presentation.RecipeGeneration;
            model.Composition = presentation.Composition;

            AppendRenderHintRows(model.Rows, renderHints);
            AppendBoundSlotRows(model.Rows, catalog, presentation);
            AppendBoundJobRows(model.Rows, presentation);

            if (presentation.Composition.HasChildren)
            {
                model.Rows.push_back(EditorBoundRenderStateRow{
                    .Kind = EditorBoundRenderStateRowKind::CompositionSummary,
                    .Label = "Composition summary",
                    .Readiness = presentation.Composition.ChildFailedSlotCount > 0u ||
                                         presentation.Composition.ChildFailedJobCount > 0u
                                     ? GeometryPresentationReadiness::Failed
                                     : (presentation.Composition.ChildPendingSlotCount > 0u ||
                                                presentation.Composition.ChildActiveJobCount > 0u
                                            ? GeometryPresentationReadiness::Pending
                                            : GeometryPresentationReadiness::Ready),
                    .Enabled = true,
                    .SourceDescription =
                        "children:" + std::to_string(presentation.Composition.ChildCount),
                });
            }

            if (!presentation.HasRecipe)
            {
                model.Rows.push_back(EditorBoundRenderStateRow{
                    .Kind = EditorBoundRenderStateRowKind::DisabledCommand,
                    .Label = "Geometry presentation",
                    .Readiness = GeometryPresentationReadiness::Unsupported,
                    .Enabled = false,
                    .DisabledReason =
                        "selected entity has no geometry-presentation recipe",
                });
            }

            if (geometry.Domain == GS::Domain::Graph ||
                geometry.Domain == GS::Domain::PointCloud)
            {
                model.Rows.push_back(EditorBoundRenderStateRow{
                    .Kind = EditorBoundRenderStateRowKind::DisabledCommand,
                    .Label = "Texture bake",
                    .Readiness = GeometryPresentationReadiness::Unsupported,
                    .Enabled = false,
                    .DisabledReason =
                        "texture baking is available for mesh surface slots only",
                });
            }

            if (model.Rows.empty())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    EditorDiagnosticCode::InvalidVisualizationProperty,
                    "No bound render state rows were available.");
            }
            return model;
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveFirstSelectedEntity(
            const EditorFeatureBindings& context)
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

        [[nodiscard]] std::vector<std::uint32_t>
        BuildSelectedStableIdsForCacheKey(const EditorFeatureBindings& context,
                                          const std::uint32_t fallbackStableId)
        {
            std::vector<std::uint32_t> selectedIds{};
            if (context.Selection != nullptr)
            {
                const auto selected = context.Selection->SelectedStableIds();
                selectedIds.assign(selected.begin(), selected.end());
            }
            if (selectedIds.empty() && fallbackStableId != 0u)
                selectedIds.push_back(fallbackStableId);
            return selectedIds;
        }

        [[nodiscard]] std::uint64_t CurrentCommandHistoryRevision(
            const EditorFeatureBindings& context)
        {
            if (context.CommandHistory == nullptr)
                return 0u;
            return context.CommandHistory->Snapshot().Revision;
        }

        [[nodiscard]] std::uint64_t CurrentSelectionGeneration(
            const EditorFeatureBindings& context)
        {
            if (context.Selection == nullptr)
                return 0u;
            return context.Selection->SelectionGeneration();
        }

        [[nodiscard]] std::uint64_t CurrentPrimitiveSelectionGeneration(
            const EditorFeatureBindings& context,
            const EditorSelectedModelCacheSection section)
        {
            if (section !=
                EditorSelectedModelCacheSection::SelectedAnalysis)
            {
                return 0u;
            }
            return context.LastRefinedPrimitiveGeneration;
        }

        [[nodiscard]] std::uint64_t CurrentVisualizationRecipeRevision(
            const EditorFeatureBindings& context,
            const EditorSelectedModelCacheSection section)
        {
            if (section != EditorSelectedModelCacheSection::Visualization ||
                !context.VisualizationRecipes.Available())
            {
                return 0u;
            }
            return context.VisualizationRecipeRevision;
        }

        [[nodiscard]] std::uint64_t VertexBindingGenerationForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            const auto* bindings =
                raw.try_get<VertexChannelBindingSet>(entity);
            return bindings != nullptr ? bindings->BindingGeneration : 0u;
        }

        [[nodiscard]] std::uint64_t GeometryPresentationRecipeGenerationForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const EditorSelectedModelCacheSection section)
        {
            if (section !=
                EditorSelectedModelCacheSection::SelectedAnalysis)
            {
                return 0u;
            }
            const auto* state =
                raw.try_get<GeometryPresentationRuntimeState>(entity);
            return state != nullptr ? state->RecipeGeneration : 0u;
        }

        constexpr std::uint64_t kEditorSignatureOffset =
            1469598103934665603ull;
        constexpr std::uint64_t kEditorSignaturePrime =
            1099511628211ull;

        void MixSignatureByte(std::uint64_t& signature,
                              const std::uint8_t value) noexcept
        {
            signature ^= value;
            signature *= kEditorSignaturePrime;
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

        void MixSignatureFloat(std::uint64_t& signature,
                               const float value) noexcept
        {
            MixSignature(signature, std::bit_cast<std::uint32_t>(value));
        }

        void AppendRenderHintSourceSignature(
            std::uint64_t& signature,
            const std::variant<float, std::string>& source) noexcept
        {
            if (const auto* value = std::get_if<float>(&source))
            {
                MixSignature(signature, 1u);
                MixSignatureFloat(signature, *value);
                return;
            }

            const auto* name = std::get_if<std::string>(&source);
            MixSignature(signature, 2u);
            if (name != nullptr)
                MixSignatureString(signature, *name);
            else
                MixSignatureString(signature, {});
        }

        [[nodiscard]] std::uint64_t RenderHintSignatureForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const EditorSelectedModelCacheSection section)
        {
            if (section !=
                EditorSelectedModelCacheSection::SelectedAnalysis)
            {
                return 0u;
            }

            std::uint64_t signature = kEditorSignatureOffset;
            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
            {
                MixSignature(signature, 1u);
                MixSignature(signature,
                             static_cast<std::uint64_t>(surface->Domain));
            }
            else
            {
                MixSignature(signature, 0u);
            }

            if (const auto* edges = raw.try_get<G::RenderEdges>(entity))
            {
                MixSignature(signature, 1u);
                MixSignature(signature,
                             static_cast<std::uint64_t>(edges->Domain));
                AppendRenderHintSourceSignature(signature, edges->WidthSource);
            }
            else
            {
                MixSignature(signature, 0u);
            }

            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
            {
                MixSignature(signature, 1u);
                MixSignature(signature,
                             static_cast<std::uint64_t>(points->Type));
                AppendRenderHintSourceSignature(signature, points->SizeSource);
            }
            else
            {
                MixSignature(signature, 0u);
            }
            return signature;
        }

        void AppendVec4Signature(std::uint64_t& signature,
                                 const glm::vec4& value) noexcept
        {
            MixSignatureFloat(signature, value.x);
            MixSignatureFloat(signature, value.y);
            MixSignatureFloat(signature, value.z);
            MixSignatureFloat(signature, value.w);
        }

        void AppendVisualizationConfigSignature(
            std::uint64_t& signature,
            const std::optional<G::VisualizationConfig>& config)
        {
            if (!config.has_value())
            {
                MixSignature(signature, 0u);
                return;
            }

            MixSignature(signature, 1u);
            MixSignature(signature,
                         static_cast<std::uint64_t>(config->Source));
            AppendVec4Signature(signature, config->Color);
            MixSignatureString(signature, config->ScalarFieldName);
            MixSignature(signature,
                         static_cast<std::uint64_t>(config->Scalar.Map));
            MixSignature(signature,
                         static_cast<std::uint64_t>(config->ScalarDomain));
            MixSignatureString(signature, config->ColorBufferName);
            MixSignature(signature, config->Scalar.AutoRange ? 1u : 0u);
            MixSignatureFloat(signature, config->Scalar.RangeMin);
            MixSignatureFloat(signature, config->Scalar.RangeMax);
            MixSignature(signature,
                         static_cast<std::uint64_t>(config->Scalar.BinCount));
            MixSignature(signature,
                         static_cast<std::uint64_t>(
                             config->Scalar.Isolines.Num));
            AppendVec4Signature(signature, config->Scalar.Isolines.Color);
            MixSignatureFloat(signature, config->Scalar.Isolines.Width);
        }

        [[nodiscard]] std::uint64_t VisualizationStateSignatureForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const EditorSelectedModelCacheSection section,
            const EditorVisualizationTarget target)
        {
            if (section != EditorSelectedModelCacheSection::Visualization)
                return 0u;

            std::uint64_t signature = kEditorSignatureOffset;
            AppendVisualizationConfigSignature(
                signature,
                EffectiveVisualizationConfigForTarget(raw, entity, target));
            return signature;
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

        [[nodiscard]] std::uint64_t DerivedJobStateSignatureForEntity(
            const EditorFeatureBindings& context,
            const std::uint32_t stableEntityId,
            const EditorSelectedModelCacheSection section)
        {
            if (section != EditorSelectedModelCacheSection::SelectedAnalysis ||
                !context.JobCommands.SnapshotEntity)
            {
                return 0u;
            }

            std::uint64_t signature = kEditorSignatureOffset;
            std::uint64_t order = 0u;
            for (const EditorJobRecord& job :
                 context.JobCommands.SnapshotEntity(stableEntityId))
            {
                MixSignature(signature, order++);
                MixSignature(signature, job.Token.Index);
                MixSignature(signature, job.Token.Generation);
                MixSignature(signature, static_cast<std::uint64_t>(job.State));
                MixSignatureFloat(signature, job.NormalizedProgress);
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 job.RequestedJobDomain));
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 job.ResolvedJobDomain));
                MixSignature(signature,
                             static_cast<std::uint64_t>(job.Identity.Scope));
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 job.Identity.OutputSemantic));
                MixSignatureString(signature, job.Identity.OutputName);
                MixSignature(signature, job.PayloadToken);
                MixSignature(signature, job.PreviousOutputRetained ? 1u : 0u);
                MixSignatureString(signature, job.Diagnostic);
            }
            MixSignature(signature, order);
            return order == 0u ? 0u : signature;
        }

        [[nodiscard]] EditorSelectedModelCacheKey
        BuildSelectedModelCacheKey(
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const EditorGeometryDomainModel& geometry,
            const EditorSelectedModelCacheSection section,
            const EditorSelectedAnalysisCacheConsumer
                selectedAnalysisConsumer =
                    EditorSelectedAnalysisCacheConsumer::Inspector,
            const EditorVisualizationTarget visualizationTarget =
                EditorVisualizationTarget::Entity)
        {
            const std::uint32_t stableId =
                SelectionController::ToStableEntityId(entity);
            return EditorSelectedModelCacheKey{
                .Section = section,
                .SelectedAnalysisConsumer = selectedAnalysisConsumer,
                .VisualizationTarget = visualizationTarget,
                .PrimaryStableId = stableId,
                .SelectedStableIds =
                    BuildSelectedStableIdsForCacheKey(context, stableId),
                .SelectionGeneration = CurrentSelectionGeneration(context),
                .PrimitiveSelectionGeneration =
                    CurrentPrimitiveSelectionGeneration(context, section),
                .SelectedDomain = geometry.Domain,
                .VertexCount = geometry.VertexCount,
                .EdgeCount = geometry.EdgeCount,
                .HalfedgeCount = geometry.HalfedgeCount,
                .FaceCount = geometry.FaceCount,
                .NodeCount = geometry.NodeCount,
                .GeometryMetadataSignature =
                    GeometryMetadataSignatureForEntity(raw, entity),
                .RenderHintSignature =
                    RenderHintSignatureForEntity(raw, entity, section),
                .VisualizationStateSignature =
                    VisualizationStateSignatureForEntity(
                        raw,
                        entity,
                        section,
                        visualizationTarget),
                .BindingGeneration = VertexBindingGenerationForEntity(raw, entity),
                .GeometryPresentationRecipeGeneration =
                    GeometryPresentationRecipeGenerationForEntity(raw, entity, section),
                .DerivedJobStateSignature = DerivedJobStateSignatureForEntity(
                    context,
                    stableId,
                    section),
                .CommandHistoryRevision = CurrentCommandHistoryRevision(context),
                .VisualizationRecipeRevision =
                    CurrentVisualizationRecipeRevision(context, section),
                .ViewportWidth =
                    static_cast<std::uint32_t>(context.CameraViewport.Width),
                .ViewportHeight =
                    static_cast<std::uint32_t>(context.CameraViewport.Height),
                .VisualizationCommandsAvailable =
                    context.VisualizationCommandsAvailable,
                .VisualizationRecipesAvailable =
                    context.VisualizationRecipes.Available(),
            };
        }

        void RecordSelectedAnalysisCacheHit(const EditorFeatureBindings& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.SelectedAnalysisCacheHits;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->SelectedAnalysisCacheHits;
        }

        void RecordSelectedAnalysisCacheMiss(const EditorFeatureBindings& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.SelectedAnalysisCacheMisses;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->SelectedAnalysisCacheMisses;
        }

        void RecordVisualizationCacheHit(const EditorFeatureBindings& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.VisualizationModelCacheHits;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->VisualizationModelCacheHits;
        }

        void RecordVisualizationCacheMiss(const EditorFeatureBindings& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.VisualizationModelCacheMisses;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->VisualizationModelCacheMisses;
        }

        [[nodiscard]] EditorSelectedAnalysisCacheEntry*
        ResolveSelectedAnalysisCacheEntry(
            EditorSelectedModelCache& cache,
            const EditorSelectedAnalysisCacheConsumer consumer)
        {
            const std::size_t index = static_cast<std::size_t>(consumer);
            return index < cache.SelectedAnalysis.size()
                ? &cache.SelectedAnalysis[index]
                : nullptr;
        }

        [[nodiscard]] EditorSelectedAnalysisModel
        BuildSelectedAnalysisModelUncached(
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& sourceView,
            const EditorRenderHintModel& renderHints,
            const EditorGeometryDomainModel& geometry,
            const std::uint32_t stableId)
        {
            ScopedEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->SelectedAnalysisModelBuildTimeNs
                    : nullptr};
            EditorSelectedAnalysisModel model{};
            model.PropertyCatalog =
                BuildPropertyCatalogModel(context, raw, entity);
            model.GeometryPresentation =
                BuildGeometryPresentationModel(context, raw, entity);
            model.BoundState =
                BuildBoundRenderStateModel(
                    context,
                    model.PropertyCatalog,
                    model.GeometryPresentation,
                    renderHints,
                    geometry,
                    stableId);
            model.TextureBake =
                BuildEditorTextureBakeControlsModel(
                    MakeEditorVisualizationEditingContext(context),
                    sourceView,
                    model.PropertyCatalog,
                    stableId);
            return model;
        }

        [[nodiscard]] EditorSelectedAnalysisModel BuildSelectedAnalysisModel(
            const EditorFeatureBindings& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& sourceView,
            const EditorRenderHintModel& renderHints,
            const EditorGeometryDomainModel& geometry,
            const std::uint32_t stableId,
            const EditorSelectedAnalysisCacheConsumer consumer =
                EditorSelectedAnalysisCacheConsumer::Inspector)
        {
            EditorSelectedModelCache* cache = context.SelectedModelCache;
            if (cache == nullptr)
            {
                return BuildSelectedAnalysisModelUncached(
                    context,
                    raw,
                    entity,
                    sourceView,
                    renderHints,
                    geometry,
                    stableId);
            }

            EditorSelectedAnalysisCacheEntry* entry =
                ResolveSelectedAnalysisCacheEntry(*cache, consumer);
            if (entry == nullptr)
            {
                return BuildSelectedAnalysisModelUncached(
                    context,
                    raw,
                    entity,
                    sourceView,
                    renderHints,
                    geometry,
                    stableId);
            }

            const EditorSelectedModelCacheKey key =
                BuildSelectedModelCacheKey(
                    context,
                    raw,
                    entity,
                    geometry,
                    EditorSelectedModelCacheSection::SelectedAnalysis,
                    consumer);
            if (entry->Valid && entry->Key == key)
            {
                RecordSelectedAnalysisCacheHit(context);
                return entry->Model;
            }

            RecordSelectedAnalysisCacheMiss(context);
            EditorSelectedAnalysisModel model =
                BuildSelectedAnalysisModelUncached(
                    context,
                    raw,
                    entity,
                    sourceView,
                    renderHints,
                    geometry,
                    stableId);
            *entry = EditorSelectedAnalysisCacheEntry{
                .Valid = true,
                .Key = key,
                .Model = model,
            };
            return model;
        }

        [[nodiscard]] EditorGeometryProcessingModel BuildGeometryProcessingModel(
            const EditorFeatureBindings& context)
        {
            EditorGeometryProcessingModel model{};
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for processing controls.");
                return model;
            }
            if (context.Selection == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::MissingSelectionController,
                              "Selection controller is unavailable for processing controls.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for processing controls.");
                return model;
            }

            model.HasSelectedEntity = true;
            model.Capabilities =
      GetEditorGeometryProcessingCapabilities(
                    *context.Scene,
                    *selected);

            if (const auto* enrichment =
                    context.Scene->Raw().try_get<
                        AssetImportMeshEnrichmentState>(*selected))
            {
                model.DirectMeshEnrichmentStatus = enrichment->Status;
                model.DirectMeshEnrichmentPending =
        IsActiveEditorJobState(enrichment->Status);
                model.DirectMeshEnrichmentDiagnostic = enrichment->Diagnostic;
                if (model.DirectMeshEnrichmentPending &&
                    model.DirectMeshEnrichmentDiagnostic.empty())
                {
                    model.DirectMeshEnrichmentDiagnostic =
                        "Direct mesh enrichment is pending; geometry-mutating "
                        "actions are disabled until it resolves.";
                }
            }

            if (model.DirectMeshEnrichmentPending)
                return model;

            model.Entries =
      ResolveEditorGeometryProcessingEntries(model.Capabilities);
            model.KMeansDomains =
      GetAvailableEditorKMeansDomains(*context.Scene, *selected);
            model.MeshDenoiseAvailable =
                context.MeshDenoiseKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::MeshVertices);
            model.MeshCurvatureAvailable =
                context.MeshCurvatureKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::MeshVertices);
            model.MeshCurvatureDirectionsAvailable =
                model.MeshCurvatureAvailable &&
                context.MeshCurvatureDirectionsAvailable;
            model.MeshRemeshUniformAvailable =
                context.MeshRemeshUniformKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshRemeshAdaptiveAvailable =
                context.MeshRemeshAdaptiveKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshRemeshAvailable =
                model.MeshRemeshUniformAvailable ||
                model.MeshRemeshAdaptiveAvailable;
            model.MeshRemeshProjectToSurfaceAvailable =
                model.MeshRemeshAvailable &&
                context.MeshRemeshProjectToSurfaceAvailable;
            model.MeshRemeshErrorBoundedSizingAvailable =
                model.MeshRemeshAdaptiveAvailable &&
                context.MeshRemeshErrorBoundedSizingAvailable;
            model.MeshSubdivideLoopAvailable =
                context.MeshSubdivideLoopKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshSubdivideCatmullClarkAvailable =
                context.MeshSubdivideCatmullClarkKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshSubdivideSqrt3Available =
                context.MeshSubdivideSqrt3KernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshSubdivideAvailable =
                model.MeshSubdivideLoopAvailable ||
                model.MeshSubdivideCatmullClarkAvailable ||
                model.MeshSubdivideSqrt3Available;
            model.MeshSubdivideLoopFeatureEdgesAvailable =
                model.MeshSubdivideLoopAvailable &&
                context.MeshSubdivideLoopFeatureEdgesAvailable;
            model.MeshSimplifyAvailable =
                context.MeshSimplifyKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::MeshVertices);
            model.MeshVertexNormalsAvailable =
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::MeshVertices);
            model.GraphVertexNormalsAvailable =
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::GraphVertices);
            model.PointCloudVertexNormalsAvailable =
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::PointCloudPoints);
            model.PointCloudOutlierRemovalAvailable =
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::PointCloudPoints);
            model.PointCloudProgressivePoissonAvailable =
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::PointCloudPoints);
            model.MeshProgressivePoissonAvailable =
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnyEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    EditorGeometryProcessingDomain::MeshVertices);
            if (context.LastKMeansResult != nullptr)
            {
                model.LastKMeansResult = *context.LastKMeansResult;
                if (!context.LastKMeansResult->Succeeded() &&
                    context.LastKMeansResult->Status !=
                        KMeansRunStatus::Queued)
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastKMeansResult->Message.empty()
                            ? "Last K-Means command failed."
                            : context.LastKMeansResult->Message);
                }
            }
            if (context.LastMeshDenoiseResult != nullptr)
            {
                model.LastMeshDenoiseResult =
                    *context.LastMeshDenoiseResult;
                if (!context.LastMeshDenoiseResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshDenoiseResult->Message.empty()
                            ? "Last mesh denoise command failed."
                            : context.LastMeshDenoiseResult->Message);
                }
            }
            if (context.LastMeshCurvatureResult != nullptr)
            {
                model.LastMeshCurvatureResult =
                    *context.LastMeshCurvatureResult;
                if (!context.LastMeshCurvatureResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshCurvatureResult->Message.empty()
                            ? "Last mesh curvature command failed."
                            : context.LastMeshCurvatureResult->Message);
                }
            }
            if (context.LastMeshRemeshResult != nullptr)
            {
                model.LastMeshRemeshResult =
                    *context.LastMeshRemeshResult;
                if (!context.LastMeshRemeshResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshRemeshResult->Message.empty()
                            ? "Last mesh remesh command failed."
                            : context.LastMeshRemeshResult->Message);
                }
            }
            if (context.LastMeshSubdivideResult != nullptr)
            {
                model.LastMeshSubdivideResult =
                    *context.LastMeshSubdivideResult;
                if (!context.LastMeshSubdivideResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshSubdivideResult->Message.empty()
                            ? "Last mesh subdivide command failed."
                            : context.LastMeshSubdivideResult->Message);
                }
            }
            if (context.LastMeshSimplifyResult != nullptr)
            {
                model.LastMeshSimplifyResult =
                    *context.LastMeshSimplifyResult;
                if (!context.LastMeshSimplifyResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshSimplifyResult->Message.empty()
                            ? "Last mesh simplify command failed."
                            : context.LastMeshSimplifyResult->Message);
                }
            }
            if (context.LastMeshVertexNormalsResult != nullptr)
            {
                model.LastMeshVertexNormalsResult =
                    *context.LastMeshVertexNormalsResult;
                if (!context.LastMeshVertexNormalsResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshVertexNormalsResult->Message.empty()
                            ? "Last mesh vertex-normal command failed."
                            : context.LastMeshVertexNormalsResult->Message);
                }
            }
            if (context.LastGraphVertexNormalsResult != nullptr)
            {
                model.LastGraphVertexNormalsResult =
                    *context.LastGraphVertexNormalsResult;
                if (!context.LastGraphVertexNormalsResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastGraphVertexNormalsResult->Message.empty()
                            ? "Last graph vertex-normal command failed."
                            : context.LastGraphVertexNormalsResult->Message);
                }
            }
            if (context.LastPointCloudVertexNormalsResult != nullptr)
            {
                model.LastPointCloudVertexNormalsResult =
                    *context.LastPointCloudVertexNormalsResult;
                if (!context.LastPointCloudVertexNormalsResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastPointCloudVertexNormalsResult->Message.empty()
                            ? "Last point-cloud vertex-normal command failed."
                            : context.LastPointCloudVertexNormalsResult->Message);
                }
            }
            if (context.LastPointCloudOutlierRemovalResult != nullptr)
            {
                model.LastPointCloudOutlierRemovalResult =
                    *context.LastPointCloudOutlierRemovalResult;
                if (!context.LastPointCloudOutlierRemovalResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastPointCloudOutlierRemovalResult->Message.empty()
                            ? "Last point-cloud outlier-removal command failed."
                            : context.LastPointCloudOutlierRemovalResult->Message);
                }
            }
            if (context.LastProgressivePoissonResult != nullptr)
            {
                model.LastProgressivePoissonResult =
                    *context.LastProgressivePoissonResult;
                if (!context.LastProgressivePoissonResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastProgressivePoissonResult->Message.empty()
                            ? "Last progressive-Poisson command failed."
                            : context.LastProgressivePoissonResult->Message);
                }
            }
            if (!model.Capabilities.HasAny())
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::UnsupportedGeometryDomain,
                              "Selected entity has no supported GeometrySources processing domain.");
            }
            return model;
        }

        [[nodiscard]] EditorPrimitiveDetailModel BuildPrimitiveDetailModel(
            const PrimitiveSelectionResult& primitive)
        {
            return EditorPrimitiveDetailModel{
                .HasPrimitive = true,
                .Primitive = primitive,
                .HasFaceId = primitive.FaceId != kInvalidPrimitiveIndex,
                .HasEdgeId = primitive.EdgeId != kInvalidPrimitiveIndex,
                .HasVertexId = primitive.VertexId != kInvalidPrimitiveIndex,
                .HasPointId = primitive.PointId != kInvalidPrimitiveIndex,
            };
        }

        [[nodiscard]] EditorInspectorModel BuildInspectorModel(
            const EditorFeatureBindings& context)
        {
            ScopedEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->InspectorModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->InspectorModelBuilds;
            }
            EditorInspectorModel model{};
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for inspection.");
                return model;
            }

            const entt::registry& raw = context.Scene->Raw();
            model.HasEntity = true;
            model.Entity = BuildEntityRow(raw, *selected);
            model.Transform = BuildTransformModel(raw, *selected);
            model.RenderHints = BuildRenderHintModel(raw, *selected);
            model.Geometry = BuildGeometryDomainModel(raw, *selected);
            EditorSelectedAnalysisModel selectedAnalysis =
                BuildSelectedAnalysisModel(
                    context,
                    raw,
                    *selected,
                    GS::BuildConstView(raw, *selected),
                    model.RenderHints,
                    model.Geometry,
                    model.Entity.StableEntityId);
            model.PropertyCatalog = std::move(selectedAnalysis.PropertyCatalog);
            model.GeometryPresentation = std::move(selectedAnalysis.GeometryPresentation);
            model.BoundState = std::move(selectedAnalysis.BoundState);
            model.TextureBake = std::move(selectedAnalysis.TextureBake);
            model.Processing =
      GetEditorGeometryProcessingCapabilities(
                    *context.Scene,
                    *selected);

            if (model.Geometry.Domain == GS::Domain::Unknown)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::UnsupportedGeometryDomain,
                              "Selected entity has mixed GeometrySources topology.");
            }

            return model;
        }

        [[nodiscard]] EditorSelectionModel BuildSelectionModel(
            const EditorFeatureBindings& context)
        {
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->SelectionModelBuilds;
            }
            EditorSelectionModel model{};
            if (context.Selection == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::MissingSelectionController,
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
                              EditorDiagnosticCode::MissingScene,
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
                              EditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity or refined primitive is available.");
            }

            return model;
        }

        [[nodiscard]] EditorDocumentModel BuildDocumentModel(
            const EditorFeatureBindings& context)
        {
            EditorDocumentModel model{};
            if (context.CommandHistory == nullptr)
            {
                model.StatusText =
                    "Document history is disabled: runtime command history is unavailable.";
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::EditorCommandHistoryUnavailable,
                              model.StatusText);
                return model;
            }

            const EditorCommandHistorySnapshot snapshot =
                context.CommandHistory->Snapshot();
            model.HistoryAvailable = true;
            model.Dirty = snapshot.Dirty;
            model.CanUndo = snapshot.CanUndo;
            model.CanRedo = snapshot.CanRedo;
            model.HasActivePath = snapshot.HasActivePath;
            model.ActivePath = snapshot.ActivePath;
            model.UndoLabel = snapshot.UndoLabel;
            model.RedoLabel = snapshot.RedoLabel;
            model.Revision = snapshot.Revision;
            model.SavedRevision = snapshot.SavedRevision;

            if (snapshot.Dirty)
                model.StatusText = "Scene document has unsaved changes.";
            else if (snapshot.HasActivePath)
                model.StatusText = "Scene document is saved.";
            else
                model.StatusText = "Scene document has no active file path.";
            return model;
        }

        [[nodiscard]] EditorSceneFileModel BuildSceneFileModel(
            const EditorFeatureBindings& context)
        {
            EditorSceneFileModel model{};
            model.CanNew = static_cast<bool>(context.SceneFileCommands.New);
            model.CanClose = static_cast<bool>(context.SceneFileCommands.Close);
            model.CanSave = static_cast<bool>(context.SceneFileCommands.Save);
            model.CanOpen = static_cast<bool>(context.SceneFileCommands.Load);
            model.LifecycleEnabled =
                context.SceneFileCommands.LifecycleAvailable();
            model.Enabled =
                context.SceneFileCommandsAvailable ||
                context.SceneFileCommands.Available() ||
                model.LifecycleEnabled;
            model.PendingPath = context.PendingSceneFilePath;
            if (model.Enabled)
            {
                model.StatusText =
                    "Scene path-entry commands available; native dialogs are deferred.";
            }
            else
            {
                model.StatusText =
                    "Scene workflows are disabled: runtime scene commands are unavailable.";
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::SceneFileUnavailable,
                              model.StatusText);
            }
            if (context.LastSceneFileResult != nullptr)
            {
                model.LastResult = *context.LastSceneFileResult;
                if (!context.LastSceneFileResult->Message.empty())
                    model.StatusText = context.LastSceneFileResult->Message;
                if (!context.LastSceneFileResult->Succeeded() &&
                    context.LastSceneFileResult->Status !=
                        EditorCommandStatus::Pending)
                {
                    AddDiagnostic(model.Diagnostics,
                                  EditorDiagnosticCode::SceneFileFailed,
                                  model.StatusText);
                }
            }
            return model;
        }

        [[nodiscard]] EditorFileImportModel BuildFileImportModel(
            const EditorFeatureBindings& context)
        {
            EditorFileImportModel model{};
            model.Enabled =
                context.AssetImportCommandsAvailable ||
                context.AssetImportCommands.Available();
            model.PendingPath = context.PendingAssetImportPath;
            model.PayloadKind = context.PendingAssetImportPayloadKind;
            const FileImportPrerequisiteEvaluation prerequisites =
                EvaluateFileImportPrerequisites(
                    context.AssetImportCommands.Available(),
                    model.PendingPath,
                    model.PayloadKind);
            model.CanChoosePayloadHint = prerequisites.CanChoosePayloadHint;
            model.CanImport = prerequisites.CanImport;
            model.ResolvedPayloadKind = prerequisites.ResolvedPayloadKind;
            model.PayloadOptions = prerequisites.PayloadOptions;
            model.PayloadHintDisabledReason =
                prerequisites.PayloadHintDisabledReason;
            model.ImportDisabledReason = prerequisites.ImportDisabledReason;
            if (model.CanImport)
            {
                model.StatusText = "Ready to import ";
                model.StatusText += A::DebugNameForAssetPayloadKind(
                    model.ResolvedPayloadKind);
                model.StatusText += " asset.";
            }
            else
            {
                model.StatusText = model.ImportDisabledReason;
                if (!context.AssetImportCommands.Available())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        EditorDiagnosticCode::AssetImportUnavailable,
                        model.StatusText);
                }
            }
            if (context.LastAssetImportResult != nullptr)
            {
                model.LastResult = *context.LastAssetImportResult;
                if (model.CanImport &&
                    !context.LastAssetImportResult->Message.empty())
                {
                    model.StatusText = context.LastAssetImportResult->Message;
                }
                if (!context.LastAssetImportResult->Succeeded() &&
                    context.LastAssetImportResult->Status !=
                        EditorCommandStatus::Pending)
                {
                    AddDiagnostic(model.Diagnostics,
                                  EditorDiagnosticCode::AssetImportFailed,
                                  context.LastAssetImportResult->Message.empty()
                                      ? model.StatusText
                                      : context.LastAssetImportResult->Message);
                }
            }
            return model;
        }

        [[nodiscard]] double QueueElapsedSeconds(
            const RuntimeAssetImportQueueEntry& entry,
            const RuntimeAssetImportQueueTimePoint now) noexcept
        {
            const RuntimeAssetImportQueueTimePoint end =
                entry.FinishedAt.value_or(now);
            if (end <= entry.EnqueuedAt)
            {
                return 0.0;
            }
            return std::chrono::duration<double>(end - entry.EnqueuedAt).count();
        }

        [[nodiscard]] EditorAssetImportQueueRow
        BuildAssetImportQueueRow(
            const RuntimeAssetImportQueueEntry& entry,
            const RuntimeAssetImportQueueTimePoint now)
        {
            EditorAssetImportQueueRow row{};
            row.Operation = entry.Operation;
            row.Sequence = entry.Sequence;
            row.Source = entry.Source;
            row.SourcePath = entry.SourcePath;
            row.PathBasename = entry.PathBasename.empty()
                ? entry.SourcePath
                : entry.PathBasename;
            row.PayloadKind = entry.PayloadKind;
            row.Asset = entry.Asset;
            row.Stage = entry.Stage;
            row.TerminalStatus = entry.TerminalStatus;
            row.ProgressDeterminate = entry.ProgressDeterminate;
            row.NormalizedProgress = std::clamp(entry.NormalizedProgress, 0.0f, 1.0f);
            row.StageText = entry.StageText.empty()
                ? DebugNameForRuntimeAssetImportQueueStage(entry.Stage)
                : entry.StageText;
            row.DiagnosticText = entry.DiagnosticText;
            row.ElapsedSeconds = QueueElapsedSeconds(entry, now);
            row.CanCancel = entry.CanCancel;
            row.CancelDisabledReason = entry.CancelDisabledReason;
            return row;
        }

        [[nodiscard]] EditorAssetImportQueueModel
        BuildAssetImportQueueModel(const EditorFeatureBindings& context)
        {
            EditorAssetImportQueueModel model{};
            model.ActiveCount = context.AssetImportQueue.ActiveCount;
            model.TerminalCount = context.AssetImportQueue.TerminalCount;
            model.ClearCompletedAvailable =
                context.AssetImportQueueCommands.ClearAvailable();
            model.CanClearCompleted =
                model.ClearCompletedAvailable &&
                context.AssetImportQueue.CanClearCompleted;
            model.ClearCompletedDisabledReason =
                context.AssetImportQueue.ClearCompletedDisabledReason;

            const RuntimeAssetImportQueueTimePoint now =
                std::chrono::steady_clock::now();
            model.Rows.reserve(context.AssetImportQueue.Entries.size());
            for (const RuntimeAssetImportQueueEntry& entry :
                 context.AssetImportQueue.Entries)
            {
                model.Rows.push_back(BuildAssetImportQueueRow(entry, now));
            }

            if (model.Rows.empty())
            {
                model.StatusText = "No asset imports are queued.";
            }
            else
            {
                model.StatusText =
                    "AssetIO queue: active=" +
                    std::to_string(model.ActiveCount) +
                    " terminal=" +
                    std::to_string(model.TerminalCount) + ".";
            }

            if (!model.ClearCompletedAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::AssetImportUnavailable,
                              "Asset import queue commands are unavailable.");
            }
            return model;
        }

        [[nodiscard]] const char* RenderCommandStatusName(
            const Graphics::RenderCommandPassStatus status) noexcept
        {
            switch (status)
            {
            case Graphics::RenderCommandPassStatus::Recorded:
                return "Recorded";
            case Graphics::RenderCommandPassStatus::SkippedNonOperational:
                return "SkippedNonOperational";
            case Graphics::RenderCommandPassStatus::SkippedUnavailable:
                return "SkippedUnavailable";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* GpuProfileStatusName(
            const Graphics::RenderGraphGpuProfileStatus status) noexcept
        {
            switch (status)
            {
            case Graphics::RenderGraphGpuProfileStatus::Disabled:
                return "Disabled";
            case Graphics::RenderGraphGpuProfileStatus::Unavailable:
                return "Unavailable";
            case Graphics::RenderGraphGpuProfileStatus::Unsupported:
                return "Unsupported";
            case Graphics::RenderGraphGpuProfileStatus::Recording:
                return "Recording";
            case Graphics::RenderGraphGpuProfileStatus::Submitted:
                return "Submitted";
            case Graphics::RenderGraphGpuProfileStatus::NotReady:
                return "NotReady";
            case Graphics::RenderGraphGpuProfileStatus::Resolved:
                return "Resolved";
            case Graphics::RenderGraphGpuProfileStatus::Exhausted:
                return "Exhausted";
            case Graphics::RenderGraphGpuProfileStatus::InvalidLifecycle:
                return "InvalidLifecycle";
            case Graphics::RenderGraphGpuProfileStatus::DeviceLost:
                return "DeviceLost";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* GpuTimestampSourceName(
            const RHI::GpuTimestampSource source) noexcept
        {
            switch (source)
            {
            case RHI::GpuTimestampSource::Unavailable:
                return "Unavailable";
            case RHI::GpuTimestampSource::ContractOnly:
                return "ContractOnly";
            case RHI::GpuTimestampSource::NativeGpu:
                return "NativeGpu";
            }
            return "Unknown";
        }

        [[nodiscard]] EditorGpuProfileModel BuildGpuProfileModel(
            const Graphics::RenderGraphGpuProfileStats& stats)
        {
            EditorGpuProfileModel model{
                .Status = GpuProfileStatusName(stats.Status),
                .Source = GpuTimestampSourceName(stats.Source),
                .Diagnostic = stats.Diagnostic,
                .Fresh = stats.Fresh,
                .Stale = stats.Stale,
                .HasResolvedFrame = stats.HasResolvedFrame,
                .ResolvedSubmittedFrameNumber =
                    stats.ResolvedSubmittedFrameNumber,
                .ResolvedFrameSlot = stats.ResolvedFrameSlot,
                .SampleAgeFrames = stats.SampleAgeFrames,
            };
            model.QueueEnvelopes.reserve(stats.QueueEnvelopes.size());
            for (const Graphics::RenderGraphGpuProfileQueueStats& queue :
                 stats.QueueEnvelopes)
            {
                model.QueueEnvelopes.push_back(
                    EditorGpuProfileQueueModel{
                        .Queue = RHI::QueueAffinityName(queue.Queue),
                        .Source = GpuTimestampSourceName(queue.Source),
                        .DurationNs = queue.DurationNs,
                    });
            }
            model.Passes.reserve(stats.Passes.size());
            for (const Graphics::RenderGraphGpuProfilePassStats& pass :
                 stats.Passes)
            {
                model.Passes.push_back(
                    EditorGpuProfilePassModel{
                        .Name = pass.Name,
                        .HasTypedId = pass.Id.IsValid(),
                        .TypedId = pass.Id.Value,
                        .Queue = RHI::QueueAffinityName(pass.Queue),
                        .CommandStatus =
                            RenderCommandStatusName(pass.CommandStatus),
                        .Source = GpuTimestampSourceName(pass.Source),
                        .DurationNs = pass.DurationNs,
                    });
            }
            return model;
        }

        [[nodiscard]] EditorRenderGraphModel BuildRenderGraphModel(
            const EditorFeatureBindings& context)
        {
            EditorRenderGraphModel model{};
            model.GpuProfilingToggleAvailable =
                context.EngineConfigCommandsAvailable &&
                context.EngineConfigControlState != nullptr &&
                context.PreviewEngineConfigDocument &&
                context.ApplyEngineConfigHotSubset;
            if (context.EngineConfigControlState != nullptr)
            {
                const RuntimeEngineConfigControlState& controlState =
                    *context.EngineConfigControlState;
                model.GpuProfilingEnabled =
                    controlState.ActiveConfig.Render.EnableGpuProfiling;
                if (controlState.HasLastApply &&
                    controlState.LastApply.LoadResult.SourceId ==
                        "sandbox.frame_graph.gpu_profiling")
                {
                    switch (controlState.LastApply.Status)
                    {
                    case RuntimeEngineConfigApplyStatus::Applied:
                        model.GpuProfilingControlStatusText =
                            "GPU profiling config applied.";
                        break;
                    case RuntimeEngineConfigApplyStatus::NoChange:
                        model.GpuProfilingControlStatusText =
                            "GPU profiling config unchanged.";
                        break;
                    case RuntimeEngineConfigApplyStatus::Rejected:
                        model.GpuProfilingControlStatusText =
                            "GPU profiling config hot-apply was rejected.";
                        break;
                    case RuntimeEngineConfigApplyStatus::None:
                        break;
                    }
                    for (const Core::Config::EngineConfigDiagnostic&
                             diagnostic :
                         controlState.LastApply.LoadResult.Diagnostics)
                    {
                        model.GpuProfilingControlDiagnostics.push_back(
                            diagnostic.Subject.empty()
                                ? diagnostic.Message
                                : diagnostic.Subject + ": " +
                                    diagnostic.Message);
                    }
                    for (const std::string& field :
                         controlState.LastApply.RejectedBootOnlyFields)
                    {
                        model.GpuProfilingControlDiagnostics.push_back(
                            "Boot-only field rejected: " + field);
                    }
                }
            }
            if (!model.GpuProfilingToggleAvailable)
            {
                model.GpuProfilingToggleDisabledReason =
                    "Engine config-control preview/apply commands are unavailable.";
            }
            if (context.RenderGraphStats == nullptr)
            {
                model.StatusText =
                    "Frame graph diagnostics are disabled: renderer stats are unavailable.";
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::RenderGraphStatsUnavailable,
                              model.StatusText);
                return model;
            }

            const Graphics::RenderGraphFrameStats& stats =
                *context.RenderGraphStats;
            model.Enabled = true;
            model.CompileSucceeded = stats.Compile.Succeeded;
            model.ExecuteSucceeded = stats.Execute.Succeeded;
            model.DeviceOperational = stats.Execute.DeviceOperational;
            model.PassCount = stats.Compile.PassCount;
            model.CulledPassCount = stats.Compile.CulledPassCount;
            model.ResourceCount = stats.Compile.ResourceCount;
            model.BarrierCount = stats.Compile.BarrierCount;
            model.QueueHandoffEdgeCount = stats.Compile.QueueHandoffEdgeCount;
            model.CrossQueueTimelineEdgeCount =
                stats.Compile.CrossQueueTimelineEdgeCount;
            model.CrossQueueTimelineSignalCount =
                stats.Compile.CrossQueueTimelineSignalCount;
            model.CrossQueueTimelineWaitCount =
                stats.Compile.CrossQueueTimelineWaitCount;
            model.CrossQueueOwnershipTransferCount =
                stats.Compile.CrossQueueOwnershipTransferCount;
            model.TransientMemoryEstimateBytes =
                stats.Compile.TransientMemoryEstimateBytes;
            model.CompileTimeMicros = stats.Compile.TimeMicros;
            model.ExecuteTimeMicros = stats.Execute.TimeMicros;
            model.CommandPassesRecorded = stats.CommandRecords.Recorded;
            model.CommandPassesSkipped = stats.CommandRecords.Skipped;
            model.CommandPassesSkippedNonOperational =
                stats.CommandRecords.SkippedNonOperational;
            model.CommandPassesSkippedUnavailable =
                stats.CommandRecords.SkippedUnavailable;
            model.AsyncComputeUtilizedFrames =
                stats.AsyncComputeUtilizedFrames;
            model.Diagnostic = stats.Diagnostic;
            model.LifecycleDiagnostic = stats.LifecycleDiagnostic;
            model.DebugDump = stats.DebugDump;
            model.GpuProfile = BuildGpuProfileModel(stats.GpuProfile);
            model.StatusText = model.CompileSucceeded
                ? "Frame graph compile succeeded."
                : "Frame graph compile has not succeeded yet.";
            if (!model.Diagnostic.empty())
            {
                model.StatusText = model.Diagnostic;
            }

            model.CommandPasses.reserve(stats.CommandRecords.Passes.size());
            for (const Graphics::RenderGraphCommandPassStats& pass :
                 stats.CommandRecords.Passes)
            {
                model.CommandPasses.push_back(
                    EditorRenderGraphPassModel{
                        .Name = pass.Name,
                        .HasTypedId = pass.Id.IsValid(),
                        .TypedId = pass.Id.Value,
                        .Status = RenderCommandStatusName(pass.Status),
                    });
            }
            return model;
        }

        [[nodiscard]] EditorCameraRenderModel BuildCameraRenderModel(
            const EditorFeatureBindings& context)
        {
            EditorCameraRenderModel model{};
            model.CameraControlsAvailable = context.CameraControllers != nullptr;
            model.RenderSettingsAvailable = context.CameraControllers != nullptr;

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

            if (!model.CameraControlsAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::CameraRenderCommandsUnavailable,
                              "Camera/render setting command seams are unavailable.");
            }
            return model;
        }

        [[nodiscard]] EditorVisualizationModel BuildVisualizationModel(
            const EditorFeatureBindings& context,
            const EditorVisualizationTarget target =
                EditorVisualizationTarget::Entity)
        {
            ScopedEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->VisualizationModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->VisualizationModelBuilds;
            }
            EditorVisualizationModel model{};
            model.GeometryDomainControlsAvailable = context.VisualizationCommandsAvailable;
            model.RecipeControlsAvailable =
                context.VisualizationCommandsAvailable &&
                context.VisualizationRecipes.Available();
            model.Target = target;
            if (!context.VisualizationCommandsAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::VisualizationCommandsUnavailable,
                              "Visualization command seams are unavailable.");
                return model;
            }
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for visualization controls.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              EditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for visualization controls.");
                return model;
            }

            const entt::registry& raw = context.Scene->Raw();
            model.HasSelectedEntity = true;
            model.SelectedStableId =
                SelectionController::ToStableEntityId(*selected);
            const GS::ConstSourceView sourceView =
                GS::BuildConstView(raw, *selected);
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(raw, *selected);
            model.SelectedDomain = sourceView.ActiveDomain;
            model.TargetAvailable =
                VisualizationTargetAvailableForView(availability, target);
            model.Properties =
                target == EditorVisualizationTarget::Entity
                    ? BuildVisualizationProperties(availability)
                    : [&availability, target]()
                      {
                          std::vector<EditorVisualizationPropertyInfo> out{};
                          AppendVisualizationPropertiesForTarget(
                              out,
                              availability,
                              target);
                          return out;
                      }();

            model.Visualization =
                BuildVisualizationConfigModelForTarget(raw, *selected, target);
            if (model.RecipeControlsAvailable)
            {
                const std::optional<VisualizationRecipe> recipe =
                    context.VisualizationRecipes.GetRecipe(model.SelectedStableId);
                if (recipe.has_value())
                    model.Recipe = FromVisualizationRecipe(*recipe);
            }
            return model;
        }

        [[nodiscard]] EditorVisualizationModelCacheEntry*
        ResolveVisualizationCacheEntry(
            EditorSelectedModelCache& cache,
            const EditorVisualizationTarget target)
        {
            const std::size_t index = static_cast<std::size_t>(target);
            return index < cache.Visualization.size()
                ? &cache.Visualization[index]
                : nullptr;
        }

        [[nodiscard]] EditorVisualizationModel BuildCachedVisualizationModel(
            const EditorFeatureBindings& context,
            const EditorVisualizationTarget target =
                EditorVisualizationTarget::Entity)
        {
            EditorSelectedModelCache* cache = context.SelectedModelCache;
            if (cache == nullptr || !context.VisualizationCommandsAvailable ||
                context.Scene == nullptr)
            {
                return BuildVisualizationModel(context, target);
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
                return BuildVisualizationModel(context, target);

            EditorVisualizationModelCacheEntry* entry =
                ResolveVisualizationCacheEntry(*cache, target);
            if (entry == nullptr)
                return BuildVisualizationModel(context, target);

            const entt::registry& raw = context.Scene->Raw();
            const EditorGeometryDomainModel geometry =
                BuildGeometryDomainModel(raw, *selected);
            const EditorSelectedModelCacheKey key =
                BuildSelectedModelCacheKey(
                    context,
                    raw,
                    *selected,
                    geometry,
                    EditorSelectedModelCacheSection::Visualization,
                    EditorSelectedAnalysisCacheConsumer::Inspector,
                    target);

            if (entry->Valid && entry->Key == key)
            {
                RecordVisualizationCacheHit(context);
                return entry->Model;
            }

            RecordVisualizationCacheMiss(context);
            EditorVisualizationModel model =
                BuildVisualizationModel(context, target);
            *entry = EditorVisualizationModelCacheEntry{
                .Valid = true,
                .Key = key,
                .Model = model,
            };
            return model;
        }

} // namespace

    EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshotFromBindings(
        const EditorFeatureBindings& context,
        const EditorWorkspaceSnapshotRequest& request);

    EditorWorkspaceSnapshot
BuildEditorWorkspaceSnapshotFromBindings(
        const EditorFeatureBindings& context)
    {
        return BuildEditorWorkspaceSnapshotFromBindings(
            context,
            EditorWorkspaceSnapshotRequest{});
    }

    EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshotFromBindings(
        const EditorFeatureBindings& context,
        const EditorWorkspaceSnapshotRequest& request)
    {
        EditorWorkspaceSnapshot frame{};
        EditorWorkspaceSnapshotStats stats{};
        const EditorModelBuildClock::time_point frameBuildStart =
            EditorModelBuildClock::now();
        EditorFeatureBindings modelContext = context;
        modelContext.ModelBuildStats = &stats;

        if (modelContext.Scene == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          EditorDiagnosticCode::MissingScene,
                          "Scene registry is unavailable.");
        }
        else if (request.Hierarchy)
        {
            ++stats.HierarchyModelBuilds;
            const entt::registry& raw = modelContext.Scene->Raw();
            raw.view<entt::entity>().each(
                [&frame, &raw](const ECS::EntityHandle entity)
                {
                    frame.Hierarchy.push_back(BuildEntityRow(raw, entity));
                });
            std::sort(frame.Hierarchy.begin(),
                      frame.Hierarchy.end(),
                      [](const EditorEntityRow& lhs,
                         const EditorEntityRow& rhs)
                      {
                          if (lhs.StableEntityId != rhs.StableEntityId)
                              return lhs.StableEntityId < rhs.StableEntityId;
                          return lhs.Name < rhs.Name;
                      });
        }

        if (modelContext.Selection == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          EditorDiagnosticCode::MissingSelectionController,
                          "Selection controller is unavailable.");
        }
        if (!modelContext.ImGuiAdapterAvailable)
        {
            AddDiagnostic(frame.Diagnostics,
                          EditorDiagnosticCode::MissingImGuiAdapter,
                          "Runtime ImGui adapter is unavailable.");
        }

        if (request.Inspector)
        {
            frame.Inspector = BuildInspectorModel(modelContext);
        }
        if (request.Selection)
        {
            frame.Selection = BuildSelectionModel(modelContext);
        }
        if (request.Document)
        {
            frame.Document = BuildDocumentModel(modelContext);
        }
        if (request.SceneFile)
        {
            frame.SceneFile = BuildSceneFileModel(modelContext);
        }
        if (request.FileImport)
        {
            frame.FileImport = BuildFileImportModel(modelContext);
        }
        if (request.AssetImportQueue)
        {
            frame.AssetImportQueue = BuildAssetImportQueueModel(modelContext);
        }
        if (request.RenderGraph)
        {
            frame.RenderGraph = BuildRenderGraphModel(modelContext);
        }
        if (request.RenderRecipe)
        {
            frame.RenderRecipe = BuildEditorRenderRecipeEditorModel(
                MakeEditorRenderRecipeEditingContext(modelContext));
        }
        if (request.CameraRender)
        {
            frame.CameraRender = BuildCameraRenderModel(modelContext);
        }
        if (request.Visualization)
        {
            frame.Visualization = BuildCachedVisualizationModel(modelContext);
        }
        stats.WorkspaceSnapshotBuildTimeNs +=
            EditorElapsedNs(frameBuildStart);
        frame.ModelBuildStats = stats;
        return frame;
    }

    EditorDomainWindowModel
BuildEditorDomainWindowModelFromBindings(
        const EditorFeatureBindings& context,
        const EditorDomainWindowKind kind)
    {
        ScopedEditorStatTimer timer{
            context.ModelBuildStats != nullptr
                ? &context.ModelBuildStats->DomainWindowModelBuildTimeNs
                : nullptr};
        if (context.ModelBuildStats != nullptr)
        {
            ++context.ModelBuildStats->DomainWindowModelBuilds;
        }
        EditorDomainWindowModel model{};
        model.Kind = kind;
        model.ExpectedDomain = ExpectedDomainForWindowKind(kind);
        model.VisualizationTarget = VisualizationTargetForWindowKind(kind);
        model.VisualizationControlsAvailable =
            context.VisualizationCommandsAvailable;

        if (context.Scene == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          EditorDiagnosticCode::MissingScene,
                          "Scene registry is unavailable for domain window.");
            return model;
        }
        if (context.Selection == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          EditorDiagnosticCode::MissingSelectionController,
                          "Selection controller is unavailable for domain window.");
            return model;
        }

        const std::optional<ECS::EntityHandle> selected =
            ResolveFirstSelectedEntity(context);
        if (!selected.has_value())
        {
            const bool hadStaleSelection =
                !context.Selection->SelectedStableIds().empty();
            AddDiagnostic(model.Diagnostics,
                          EditorDiagnosticCode::NoSelectedEntity,
                          hadStaleSelection
                              ? "Selected entity is stale or no longer live."
                              : "No selected entity is available for domain window.");
            return model;
        }

        const entt::registry& raw = context.Scene->Raw();
        model.HasSelectedEntity = true;
        model.SelectedEntity = BuildEntityRow(raw, *selected);
        model.SelectedStableId = SelectionController::ToStableEntityId(*selected);
        model.RenderHints = BuildRenderHintModel(raw, *selected);
        const GS::ConstSourceView sourceView =
            GS::BuildConstView(raw, *selected);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, *selected);
        model.SelectedDomain = sourceView.ActiveDomain;
        model.DomainMatches = model.SelectedDomain == model.ExpectedDomain;
        model.VisualizationTargetAvailable =
            VisualizationTargetAvailableForView(
                availability,
                model.VisualizationTarget);
        const EditorGeometryDomainModel geometry =
            BuildGeometryDomainModel(raw, *selected);
        EditorSelectedAnalysisModel selectedAnalysis =
            BuildSelectedAnalysisModel(
                context,
                raw,
                *selected,
                sourceView,
                model.RenderHints,
                geometry,
                model.SelectedStableId,
                SelectedAnalysisCacheConsumerForWindowKind(kind));
        model.PropertyCatalog = std::move(selectedAnalysis.PropertyCatalog);
        model.BoundState = std::move(selectedAnalysis.BoundState);
        model.TextureBake = std::move(selectedAnalysis.TextureBake);
        if (model.DomainMatches)
        {
            model.Processing = BuildGeometryProcessingModel(context);
            AppendDiagnostics(model.Diagnostics, model.Processing.Diagnostics);
        }

        if (!model.DomainMatches)
        {
            std::string message =
                std::string(DebugNameForEditorDomainWindowKind(kind));
            message += " window requires ";
            message += DebugNameForEditorGeometryDomain(model.ExpectedDomain);
            message += "-domain selection; selected domain is ";
            message += DebugNameForEditorGeometryDomain(model.SelectedDomain);
            message += ".";
            AddDiagnostic(model.Diagnostics,
                          EditorDiagnosticCode::UnsupportedGeometryDomain,
                          std::move(message));
        }

        if (!context.VisualizationCommandsAvailable)
        {
            AddDiagnostic(model.Diagnostics,
                          EditorDiagnosticCode::VisualizationCommandsUnavailable,
                          "Visualization command seams are unavailable.");
        }
        else
        {
            model.Visualization =
                BuildCachedVisualizationModel(context, model.VisualizationTarget);
            AppendDiagnostics(model.Diagnostics, model.Visualization.Diagnostics);
        }

        if (context.LastRefinedPrimitive != nullptr &&
            context.LastRefinedPrimitive->has_value())
        {
            const PrimitiveSelectionResult& primitive =
                **context.LastRefinedPrimitive;
            const bool sameEntity =
                primitive.EntityId == model.SelectedStableId ||
                primitive.StableId == model.SelectedStableId;
            if (sameEntity && primitive.Domain == model.SelectedDomain)
                model.Primitive = BuildPrimitiveDetailModel(primitive);
        }

        return model;
    }


} // namespace Extrinsic::Runtime::EditorFeatureDetail

namespace Extrinsic::Runtime
{
    EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotContext& context)
    {
        return EditorFeatureDetail::BuildEditorWorkspaceSnapshotFromBindings(
            EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
    }

    EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
        const EditorWorkspaceSnapshotContext& context,
        const EditorWorkspaceSnapshotRequest& request)
    {
        return EditorFeatureDetail::BuildEditorWorkspaceSnapshotFromBindings(
            EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), request);
    }

    EditorDomainWindowModel BuildEditorDomainWindowModel(
        const EditorWorkspaceSnapshotContext& context,
        const EditorDomainWindowKind kind)
    {
        return EditorFeatureDetail::BuildEditorDomainWindowModelFromBindings(
            EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), kind);
    }
}
