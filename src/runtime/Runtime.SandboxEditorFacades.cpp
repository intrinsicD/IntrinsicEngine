module;

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <array>
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

module Extrinsic.Runtime.SandboxEditorFacades;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.SpatialDebugBinding;
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
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.KMeansBackend;
import Extrinsic.Runtime.KMeansGpuBackend;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RegistrationAlignment;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.SandboxConfigSections;
import Extrinsic.Runtime.SceneDocument;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Graph;
import Geometry.Graph.Vertex.Normals;
import Geometry.Curvature;
import Geometry.CatmullClark;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.AdaptiveRemeshing;
import Geometry.HalfedgeMesh.SubdivisionSqrt3;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.KMeans;
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

#include "Runtime.SandboxEditorFacades.Internal.hpp"

namespace Extrinsic::Runtime
{
    void SandboxEditorSelectedModelCache::Clear() noexcept
    {
        for (SandboxEditorSelectedAnalysisCacheEntry& entry : SelectedAnalysis)
            entry.Valid = false;
        for (SandboxEditorVisualizationModelCacheEntry& entry : Visualization)
            entry.Valid = false;
        ++Counters.Invalidations;
    }

    SandboxEditorSelectedModelCacheStats SandboxEditorSelectedModelCache::Stats()
        const noexcept
    {
        SandboxEditorSelectedModelCacheStats stats = Counters;
        for (const SandboxEditorSelectedAnalysisCacheEntry& entry :
             SelectedAnalysis)
        {
            if (entry.Valid)
                ++stats.Entries;
        }
        for (const SandboxEditorVisualizationModelCacheEntry& entry :
             Visualization)
        {
            if (entry.Valid)
                ++stats.Entries;
        }
        return stats;
    }

    namespace
    {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;
        namespace GK = Geometry::KMeans;
        namespace GN = Geometry::HalfedgeMesh::VertexNormals;
        namespace GraphNormals = Geometry::Graph::VertexNormals;
        namespace PointNormals = Geometry::PointCloud::Normals;
        namespace SurfaceSampling = Geometry::PointCloud::SurfaceSampling;
        namespace Smooth = Geometry::Smoothing;
        namespace Curv = Geometry::Curvature;
        namespace Remesh = Geometry::Remeshing;
        namespace AdaptiveRemesh = Geometry::AdaptiveRemeshing;
        namespace LoopSubdivide = Geometry::Subdivision;
        namespace CatmullClark = Geometry::CatmullClark;
        namespace Sqrt3Subdivide = Geometry::SubdivisionSqrt3;
        namespace Simpl = Geometry::Simplification;
        namespace Reg = Geometry::Registration;
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

        using SandboxEditorModelBuildClock = std::chrono::steady_clock;

        [[nodiscard]] std::uint64_t SandboxEditorElapsedNs(
            const SandboxEditorModelBuildClock::time_point start) noexcept
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    SandboxEditorModelBuildClock::now() - start)
                    .count();
            return elapsed > 0
                ? static_cast<std::uint64_t>(elapsed)
                : 1u;
        }

        class ScopedSandboxEditorStatTimer final
        {
        public:
            explicit ScopedSandboxEditorStatTimer(
                std::uint64_t* target) noexcept
                : m_Target(target)
            {
                if (m_Target != nullptr)
                    m_Start = SandboxEditorModelBuildClock::now();
            }

            ScopedSandboxEditorStatTimer(
                const ScopedSandboxEditorStatTimer&) = delete;
            ScopedSandboxEditorStatTimer& operator=(
                const ScopedSandboxEditorStatTimer&) = delete;

            ~ScopedSandboxEditorStatTimer()
            {
                if (m_Target != nullptr)
                    *m_Target += SandboxEditorElapsedNs(m_Start);
            }

        private:
            std::uint64_t* m_Target{nullptr};
            SandboxEditorModelBuildClock::time_point m_Start{};
        };

        [[nodiscard]] bool IsModelTextureImportPayload(
            const A::AssetPayloadKind payloadKind) noexcept
        {
            return payloadKind == A::AssetPayloadKind::ModelScene ||
                   payloadKind == A::AssetPayloadKind::Texture2D;
        }

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
            std::array<SandboxEditorFileImportPayloadOption, 6> PayloadOptions{};
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
                for (SandboxEditorFileImportPayloadOption& option :
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
                const std::string reason = automaticDiagnostic.Message.empty()
                    ? std::string{
                          "The selected asset format has no supported import payload."}
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
            for (SandboxEditorFileImportPayloadOption& option :
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
                &SandboxEditorFileImportPayloadOption::Kind);
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

        // Validation tables belong to the non-ImGui command facade. App
        // presentation consumes the exported option records instead of
        // duplicating runtime-owned availability policy.
        inline constexpr std::array<SandboxEditorMeshDenoiseStage, 1>
            kMeshDenoiseStages{{
                SandboxEditorMeshDenoiseStage::FullBilateral,
            }};
        inline constexpr std::array<SandboxEditorMeshCurvatureOutput, 4>
            kMeshCurvatureOutputs{{
                SandboxEditorMeshCurvatureOutput::All,
                SandboxEditorMeshCurvatureOutput::Mean,
                SandboxEditorMeshCurvatureOutput::Gaussian,
                SandboxEditorMeshCurvatureOutput::PrincipalDirections,
            }};
        inline constexpr std::array<SandboxEditorMeshRemeshMode, 2>
            kMeshRemeshModes{{
                SandboxEditorMeshRemeshMode::Uniform,
                SandboxEditorMeshRemeshMode::Adaptive,
            }};
        inline constexpr std::array<SandboxEditorMeshRemeshSizingLaw, 2>
            kMeshRemeshSizingLaws{{
                SandboxEditorMeshRemeshSizingLaw::MeanCurvature,
                SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
            }};
        inline constexpr std::array<SandboxEditorMeshSimplifyMetric, 2>
            kMeshSimplifyMetrics{{
                SandboxEditorMeshSimplifyMetric::ClassicalQEM,
                SandboxEditorMeshSimplifyMetric::FA_QEM,
            }};
        inline constexpr std::array<SandboxEditorICPVariant, 2>
            kSandboxEditorICPVariants{{
                SandboxEditorICPVariant::PointToPoint,
                SandboxEditorICPVariant::PointToPlane,
            }};
        inline constexpr std::array<SandboxEditorMeshSubdivideOperator, 3>
            kMeshSubdivideOperators{{
                SandboxEditorMeshSubdivideOperator::Loop,
                SandboxEditorMeshSubdivideOperator::CatmullClark,
                SandboxEditorMeshSubdivideOperator::Sqrt3,
            }};

        [[nodiscard]] GS::Domain ExpectedDomainForWindowKind(
            const SandboxEditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                return GS::Domain::Mesh;
            case SandboxEditorDomainWindowKind::Graph:
                return GS::Domain::Graph;
            case SandboxEditorDomainWindowKind::PointCloud:
                return GS::Domain::PointCloud;
            }
            return GS::Domain::None;
        }

        [[nodiscard]] SandboxEditorSelectedAnalysisCacheConsumer
        SelectedAnalysisCacheConsumerForWindowKind(
            const SandboxEditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                return SandboxEditorSelectedAnalysisCacheConsumer::MeshDomainWindow;
            case SandboxEditorDomainWindowKind::Graph:
                return SandboxEditorSelectedAnalysisCacheConsumer::GraphDomainWindow;
            case SandboxEditorDomainWindowKind::PointCloud:
                return SandboxEditorSelectedAnalysisCacheConsumer::
                    PointCloudDomainWindow;
            }
            return SandboxEditorSelectedAnalysisCacheConsumer::Inspector;
        }

        [[nodiscard]] std::string ErrorName(const Core::ErrorCode error)
        {
            return std::string(Core::Error::ToString(error));
        }

        [[nodiscard]] std::string BuildImportSuccessMessage(
            const SandboxEditorFileImportCommand& command,
            const SandboxEditorFileImportResult& result)
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
            const SandboxEditorFileImportCommand& command,
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

        [[nodiscard]] SandboxEditorFileImportResult BuildFileImportResultFromRuntimeEvent(
            const RuntimeAssetImportEvent& event)
        {
            if (!event.Result.has_value())
            {
                return SandboxEditorFileImportResult{
                    .Status = SandboxEditorCommandStatus::AssetImportFailed,
                    .PayloadKind = event.RequestedPayloadKind,
                    .Error = event.Error,
                    .Message = BuildImportFailureMessage(event.Error),
                };
            }

            const RuntimeAssetImportResult& imported = *event.Result;
            SandboxEditorFileImportResult result{
                .Status = SandboxEditorCommandStatus::Applied,
                .Asset = imported.Asset,
                .PayloadKind = imported.PayloadKind,
                .Error = Core::ErrorCode::Success,
                .PrimitiveEntitiesCreated = imported.PrimitiveEntitiesCreated,
                .EmbeddedTextureAssetsCreated = imported.EmbeddedTextureAssetsCreated,
                .GeneratedTextureAssetsCreated = imported.GeneratedTextureAssetsCreated,
                .TextureUploadRequests = imported.TextureUploadRequests,
                .GeneratedTextureUploadRequests = imported.GeneratedTextureUploadRequests,
                .MaterializedModelScene = imported.MaterializedModelScene,
                .RequestedTextureUpload = imported.RequestedTextureUpload,
            };
            result.Message = BuildImportSuccessMessage(
                SandboxEditorFileImportCommand{
                    .Path = event.Path,
                    .PayloadKind = event.RequestedPayloadKind,
                },
                result);
            return result;
        }

        [[nodiscard]] std::string BuildSceneFileSuccessMessage(
            const SandboxEditorSceneFileCommand& command,
            const SandboxEditorSceneFileResult& result)
        {
            std::string message{};
            switch (result.Operation)
            {
            case SandboxEditorSceneFileOperation::New:
                message = "Created new scene";
                break;
            case SandboxEditorSceneFileOperation::Save:
                message = "Saved scene";
                break;
            case SandboxEditorSceneFileOperation::Load:
                message = "Opened scene";
                break;
            case SandboxEditorSceneFileOperation::Close:
                message = "Closed scene";
                break;
            }
            if (!command.Path.empty())
            {
                if (result.Operation == SandboxEditorSceneFileOperation::Save)
                    message += " to ";
                else if (result.Operation == SandboxEditorSceneFileOperation::Load)
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
            const SandboxEditorSceneFileOperation operation,
            const Core::ErrorCode error)
        {
            std::string message{};
            switch (operation)
            {
            case SandboxEditorSceneFileOperation::New:
                message = "Scene new failed: ";
                break;
            case SandboxEditorSceneFileOperation::Save:
                message = "Scene save failed: ";
                break;
            case SandboxEditorSceneFileOperation::Load:
                message = "Scene open failed: ";
                break;
            case SandboxEditorSceneFileOperation::Close:
                message = "Scene close failed: ";
                break;
            }
            message += ErrorName(error);
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildSceneFilePendingMessage(
            const SandboxEditorSceneFileCommand& command,
            const SandboxEditorSceneFileOperation operation)
        {
            std::string message{};
            switch (operation)
            {
            case SandboxEditorSceneFileOperation::New:
                message = "Queued scene new";
                break;
            case SandboxEditorSceneFileOperation::Save:
                message = "Queued scene save";
                break;
            case SandboxEditorSceneFileOperation::Load:
                message = "Queued scene open";
                break;
            case SandboxEditorSceneFileOperation::Close:
                message = "Queued scene close";
                break;
            }
            if (!command.Path.empty())
            {
                if (operation == SandboxEditorSceneFileOperation::Save)
                    message += " to ";
                else if (operation == SandboxEditorSceneFileOperation::Load)
                    message += " from ";
                else
                    message += " ";
                message += command.Path;
            }
            message += ".";
            return message;
        }

        [[nodiscard]] SandboxEditorSceneFileOperation ToSandboxSceneFileOperation(
            const RuntimeSceneFileOperation operation) noexcept
        {
            switch (operation)
            {
            case RuntimeSceneFileOperation::Save:
                return SandboxEditorSceneFileOperation::Save;
            case RuntimeSceneFileOperation::Load:
                return SandboxEditorSceneFileOperation::Load;
            case RuntimeSceneFileOperation::None:
                break;
            }
            return SandboxEditorSceneFileOperation::Load;
        }

        [[nodiscard]] SandboxEditorSceneFileResult
        BuildSceneFileResultFromRuntimeEvent(const RuntimeSceneFileEvent& event)
        {
            const SandboxEditorSceneFileOperation operation =
                ToSandboxSceneFileOperation(event.Operation);
            if (!event.Succeeded())
            {
                return SandboxEditorSceneFileResult{
                    .Status = operation == SandboxEditorSceneFileOperation::Save
                        ? SandboxEditorCommandStatus::SceneSaveFailed
                        : SandboxEditorCommandStatus::SceneLoadFailed,
                    .Operation = operation,
                    .Task = event.Task,
                    .Error = event.Error,
                    .Message = BuildSceneFileFailureMessage(operation, event.Error),
                };
            }

            SandboxEditorSceneFileResult result{
                .Status = SandboxEditorCommandStatus::Applied,
                .Operation = operation,
                .Task = event.Task,
                .Error = Core::ErrorCode::Success,
            };
            if (operation == SandboxEditorSceneFileOperation::Load &&
                event.LoadResult.has_value())
            {
                result.Stats = event.LoadResult->Stats;
            }
            else if (operation == SandboxEditorSceneFileOperation::Save &&
                     event.SaveResult.has_value())
            {
                result.Stats = event.SaveResult->Stats;
            }
            result.Message = BuildSceneFileSuccessMessage(
                SandboxEditorSceneFileCommand{.Path = event.Path},
                result);
            return result;
        }

        [[nodiscard]] SandboxEditorSpatialDebugBindingModel FromSpatialDebugBinding(
            const ECSC::SpatialDebugBinding& binding) noexcept
        {
            return SandboxEditorSpatialDebugBindingModel{
                .HasBinding = true,
                .Kind = binding.Kind,
                .RegistryKey = binding.RegistryKey,
                .LeafOnly = binding.LeafOnly,
                .OccupancyOnly = binding.OccupancyOnly,
                .MaxDepth = binding.MaxDepth,
            };
        }

        [[nodiscard]] ECSC::SpatialDebugBinding ToSpatialDebugBinding(
            const SandboxEditorSpatialDebugBindingCommand& command) noexcept
        {
            return ECSC::SpatialDebugBinding{
                .Kind = command.Kind,
                .RegistryKey = command.RegistryKey,
                .LeafOnly = command.LeafOnly,
                .OccupancyOnly = command.OccupancyOnly,
                .MaxDepth = command.MaxDepth,
            };
        }

        [[nodiscard]] bool SameSpatialDebugBinding(
            const ECSC::SpatialDebugBinding& lhs,
            const ECSC::SpatialDebugBinding& rhs) noexcept
        {
            return lhs.Kind == rhs.Kind &&
                   lhs.RegistryKey == rhs.RegistryKey &&
                   lhs.LeafOnly == rhs.LeafOnly &&
                   lhs.OccupancyOnly == rhs.OccupancyOnly &&
                   lhs.MaxDepth == rhs.MaxDepth;
        }

        [[nodiscard]] SandboxEditorVisualizationConfigModel FromVisualizationConfig(
            const G::VisualizationConfig& config)
        {
            return SandboxEditorVisualizationConfigModel{
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

        [[nodiscard]] G::VisualizationConfig ToVisualizationConfig(
            const SandboxEditorVisualizationConfigCommand& command)
        {
            G::VisualizationConfig config{};
            config.Source = command.Source;
            config.Color = command.Color;
            config.ScalarFieldName = command.ScalarFieldName;
            config.ScalarDomain = command.ScalarDomain;
            config.ColorBufferName = command.ColorBufferName;
            config.Scalar.AutoRange = command.ScalarAutoRange;
            config.Scalar.RangeMin = command.ScalarRangeMin;
            config.Scalar.RangeMax = command.ScalarRangeMax;
            config.Scalar.BinCount = command.ScalarBinCount;
            config.Scalar.Map = command.ScalarColormap;
            config.Scalar.Isolines.Num = command.IsolineCount;
            config.Scalar.Isolines.Width = command.IsolineWidth;
            config.Scalar.Isolines.Color = command.IsolineColor;
            config.Scalar.Isolines.Values = command.IsolineValues;
            config.Scalar.Isolines.ValueCount = std::min<std::uint32_t>(
                command.IsolineValueCount,
                G::ScalarFieldConfig::kMaxIsolineValues);
            return config;
        }

        [[nodiscard]] const std::optional<G::VisualizationConfig>*
        LaneOverrideForTarget(const G::VisualizationLaneOverrides& overrides,
                              const SandboxEditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case SandboxEditorVisualizationTarget::Surface:
                return &overrides.Surface;
            case SandboxEditorVisualizationTarget::Edges:
                return &overrides.Edges;
            case SandboxEditorVisualizationTarget::Points:
                return &overrides.Points;
            case SandboxEditorVisualizationTarget::Entity:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>*
        MutableLaneOverrideForTarget(G::VisualizationLaneOverrides& overrides,
                                     const SandboxEditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case SandboxEditorVisualizationTarget::Surface:
                return &overrides.Surface;
            case SandboxEditorVisualizationTarget::Edges:
                return &overrides.Edges;
            case SandboxEditorVisualizationTarget::Points:
                return &overrides.Points;
            case SandboxEditorVisualizationTarget::Entity:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>
        StoredVisualizationConfigForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorVisualizationTarget target)
        {
            if (target == SandboxEditorVisualizationTarget::Entity)
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
            const SandboxEditorVisualizationTarget target)
        {
            if (std::optional<G::VisualizationConfig> stored =
                    StoredVisualizationConfigForTarget(raw, entity, target);
                stored.has_value())
            {
                return stored;
            }
            if (target == SandboxEditorVisualizationTarget::Entity)
                return std::nullopt;
            return StoredVisualizationConfigForTarget(
                raw,
                entity,
                SandboxEditorVisualizationTarget::Entity);
        }

        [[nodiscard]] SandboxEditorVisualizationConfigModel
        BuildVisualizationConfigModelForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorVisualizationTarget target)
        {
            const std::optional<G::VisualizationConfig> config =
                EffectiveVisualizationConfigForTarget(raw, entity, target);
            return config.has_value()
                ? FromVisualizationConfig(*config)
                : SandboxEditorVisualizationConfigModel{};
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyVisualizationConfigTarget(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const SandboxEditorVisualizationTarget target,
            const std::optional<G::VisualizationConfig>& config)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            if (target == SandboxEditorVisualizationTarget::Entity)
            {
                if (config.has_value())
                    raw.emplace_or_replace<G::VisualizationConfig>(entity, *config);
                else
                    raw.remove<G::VisualizationConfig>(entity);
                return EditorCommandHistoryStatus::Applied;
            }

            if (!config.has_value() &&
                !raw.all_of<G::VisualizationLaneOverrides>(entity))
            {
                return EditorCommandHistoryStatus::Applied;
            }

            auto& overrides =
                raw.get_or_emplace<G::VisualizationLaneOverrides>(entity);
            std::optional<G::VisualizationConfig>* lane =
                MutableLaneOverrideForTarget(overrides, target);
            if (lane == nullptr)
                return EditorCommandHistoryStatus::UnsupportedOperation;

            *lane = config;
            if (overrides.Empty())
                raw.remove<G::VisualizationLaneOverrides>(entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandRecord MakeVisualizationConfigTargetCommand(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const SandboxEditorVisualizationTarget target,
            std::optional<G::VisualizationConfig> before,
            std::optional<G::VisualizationConfig> after,
            std::string label)
        {
            return EditorCommandRecord{
                .Label = std::move(label),
                .Redo = [scene, stableEntityId, target, after]()
                {
                    return ApplyVisualizationConfigTarget(
                        scene,
                        stableEntityId,
                        target,
                        after);
                },
                .Undo = [scene, stableEntityId, target, before]()
                {
                    return ApplyVisualizationConfigTarget(
                        scene,
                        stableEntityId,
                        target,
                        before);
                },
                .Dirtying = true,
            };
        }

        [[nodiscard]] bool SameVisualizationConfig(
            const G::VisualizationConfig& lhs,
            const G::VisualizationConfig& rhs) noexcept
        {
            if (lhs.Scalar.Isolines.ValueCount != rhs.Scalar.Isolines.ValueCount)
                return false;
            for (std::uint32_t i = 0u; i < lhs.Scalar.Isolines.ValueCount; ++i)
            {
                if (lhs.Scalar.Isolines.Values[i] != rhs.Scalar.Isolines.Values[i])
                    return false;
            }
            return lhs.Source == rhs.Source &&
                   lhs.Color.x == rhs.Color.x &&
                   lhs.Color.y == rhs.Color.y &&
                   lhs.Color.z == rhs.Color.z &&
                   lhs.Color.w == rhs.Color.w &&
                   lhs.ScalarFieldName == rhs.ScalarFieldName &&
                   lhs.ScalarDomain == rhs.ScalarDomain &&
                   lhs.ColorBufferName == rhs.ColorBufferName &&
                   lhs.Scalar.Map == rhs.Scalar.Map &&
                   lhs.Scalar.AutoRange == rhs.Scalar.AutoRange &&
                   lhs.Scalar.RangeMin == rhs.Scalar.RangeMin &&
                   lhs.Scalar.RangeMax == rhs.Scalar.RangeMax &&
                   lhs.Scalar.BinCount == rhs.Scalar.BinCount &&
                   lhs.Scalar.Isolines.Num == rhs.Scalar.Isolines.Num &&
                   lhs.Scalar.Isolines.Width == rhs.Scalar.Isolines.Width &&
                   lhs.Scalar.Isolines.Color == rhs.Scalar.Isolines.Color;
        }

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

        [[nodiscard]] std::optional<SandboxEditorVisualizationPropertyValueKind>
        DetectVisualizationPropertyKind(
            const Geometry::PropertySet& properties,
            const std::string& name)
        {
            if (properties.Get<float>(name))
                return SandboxEditorVisualizationPropertyValueKind::ScalarFloat;
            if (properties.Get<double>(name))
                return SandboxEditorVisualizationPropertyValueKind::ScalarDouble;
            if (properties.Get<glm::vec3>(name))
                return SandboxEditorVisualizationPropertyValueKind::Vec3;
            if (properties.Get<glm::vec4>(name))
                return SandboxEditorVisualizationPropertyValueKind::Vec4;
            if (properties.Get<std::uint32_t>(name))
                return SandboxEditorVisualizationPropertyValueKind::UInt32;
            return std::nullopt;
        }

        [[nodiscard]] bool IsScalarVisualizationKind(
            const SandboxEditorVisualizationPropertyValueKind kind) noexcept
        {
            return kind ==
                       SandboxEditorVisualizationPropertyValueKind::ScalarFloat ||
                   kind ==
                       SandboxEditorVisualizationPropertyValueKind::ScalarDouble;
        }

        [[nodiscard]] bool DomainSupportsVisualizationConfig(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
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

        [[nodiscard]] G::VisualizationConfig::Domain ToVisualizationConfigDomain(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshEdges:
            case Domain::GraphEdges:
                return G::VisualizationConfig::Domain::Edge;
            case Domain::MeshFaces:
                return G::VisualizationConfig::Domain::Face;
            case Domain::MeshVertices:
            case Domain::GraphVertices:
            case Domain::PointCloudPoints:
                return G::VisualizationConfig::Domain::Vertex;
            }
            return G::VisualizationConfig::Domain::Vertex;
        }

        [[nodiscard]] G::VisualizationConfig::ColorSource ToColorBufferSource(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshEdges:
            case Domain::GraphEdges:
                return G::VisualizationConfig::ColorSource::PerEdgeBuffer;
            case Domain::MeshFaces:
                return G::VisualizationConfig::ColorSource::PerFaceBuffer;
            case Domain::MeshVertices:
            case Domain::GraphVertices:
            case Domain::PointCloudPoints:
                return G::VisualizationConfig::ColorSource::PerVertexBuffer;
            }
            return G::VisualizationConfig::ColorSource::PerVertexBuffer;
        }

        [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
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
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            return ResolveGeometryPropertySet(
                availability,
                ToGeometryElementDomain(domain));
        }

        void AppendVisualizationPropertiesForDomain(
            std::vector<SandboxEditorVisualizationPropertyInfo>& out,
            const Geometry::PropertySet& properties,
            SandboxEditorVisualizationPropertyDomain domain);

        [[nodiscard]] SandboxEditorVisualizationTarget
        VisualizationTargetForWindowKind(
            const SandboxEditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                return SandboxEditorVisualizationTarget::Surface;
            case SandboxEditorDomainWindowKind::Graph:
                return SandboxEditorVisualizationTarget::Edges;
            case SandboxEditorDomainWindowKind::PointCloud:
                return SandboxEditorVisualizationTarget::Points;
            }
            return SandboxEditorVisualizationTarget::Entity;
        }

        [[nodiscard]] bool VisualizationTargetAvailableForView(
            const GeometryEntityAvailability& availability,
            const SandboxEditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case SandboxEditorVisualizationTarget::Entity:
                return availability.HasGeometry();
            case SandboxEditorVisualizationTarget::Surface:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Surface).Ready();
            case SandboxEditorVisualizationTarget::Edges:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Edges).Ready();
            case SandboxEditorVisualizationTarget::Points:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Points).Ready();
            }
            return false;
        }

        void AppendVisualizationPropertiesForTarget(
            std::vector<SandboxEditorVisualizationPropertyInfo>& out,
            const GeometryEntityAvailability& availability,
            const SandboxEditorVisualizationTarget target)
        {
            const auto append =
                [&](const SandboxEditorVisualizationPropertyDomain domain)
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
            case SandboxEditorVisualizationTarget::Entity:
                append(SandboxEditorVisualizationPropertyDomain::MeshVertices);
                append(SandboxEditorVisualizationPropertyDomain::MeshEdges);
                append(SandboxEditorVisualizationPropertyDomain::MeshFaces);
                append(SandboxEditorVisualizationPropertyDomain::GraphVertices);
                append(SandboxEditorVisualizationPropertyDomain::GraphEdges);
                append(SandboxEditorVisualizationPropertyDomain::PointCloudPoints);
                break;
            case SandboxEditorVisualizationTarget::Surface:
                append(SandboxEditorVisualizationPropertyDomain::MeshVertices);
                append(SandboxEditorVisualizationPropertyDomain::MeshFaces);
                break;
            case SandboxEditorVisualizationTarget::Edges:
                append(SandboxEditorVisualizationPropertyDomain::MeshEdges);
                append(SandboxEditorVisualizationPropertyDomain::GraphEdges);
                break;
            case SandboxEditorVisualizationTarget::Points:
                append(SandboxEditorVisualizationPropertyDomain::MeshVertices);
                append(SandboxEditorVisualizationPropertyDomain::GraphVertices);
                append(SandboxEditorVisualizationPropertyDomain::PointCloudPoints);
                break;
            }
        }

        void AppendVisualizationPropertiesForDomain(
            std::vector<SandboxEditorVisualizationPropertyInfo>& out,
            const Geometry::PropertySet& properties,
            const SandboxEditorVisualizationPropertyDomain domain)
        {
            if (!DomainSupportsVisualizationConfig(domain))
                return;

            for (const std::string& name : properties.Properties())
            {
                const std::optional<SandboxEditorVisualizationPropertyValueKind>
                    kind = DetectVisualizationPropertyKind(properties, name);
                if (!kind.has_value())
                    continue;

                const bool internal = IsInternalVisualizationProperty(name);
                const bool connectivity =
                    IsConnectivityVisualizationProperty(name);
                const bool scalar =
                    !internal && IsScalarVisualizationKind(*kind);
                const bool color =
                    !internal &&
                    *kind == SandboxEditorVisualizationPropertyValueKind::Vec4;
                const bool vector =
                    !connectivity &&
                    *kind == SandboxEditorVisualizationPropertyValueKind::Vec3;
                const bool integer =
                    !internal && !connectivity &&
                    *kind == SandboxEditorVisualizationPropertyValueKind::UInt32;
                if (!scalar && !color && !vector && !integer)
                {
                    continue;
                }

                out.push_back(SandboxEditorVisualizationPropertyInfo{
                    .Name = name,
                    .Domain = domain,
                    .ValueKind = *kind,
                    .ElementCount = properties.Size(),
                    .ScalarPresetAvailable = scalar,
                    .IsolinePresetAvailable = scalar,
                    .ColorBufferPresetAvailable = color,
                    .VectorFieldCandidate = vector,
                });
            }
        }

        [[nodiscard]] std::vector<SandboxEditorVisualizationPropertyInfo>
        BuildVisualizationProperties(const GeometryEntityAvailability& availability)
        {
            std::vector<SandboxEditorVisualizationPropertyInfo> out{};
            AppendVisualizationPropertiesForTarget(
                out,
                availability,
                SandboxEditorVisualizationTarget::Entity);
            return out;
        }

        [[nodiscard]] ProgressivePropertyValueKind DefaultExpectedValueKindForSlot(
            ProgressiveSlotSemantic semantic) noexcept;

        [[nodiscard]] ProgressiveGeometryDomain DefaultDomainForProgressiveSlot(
            GS::Domain sourceDomain,
            ProgressiveRenderLane lane,
            ProgressiveSlotSemantic semantic) noexcept;
        void AddDiagnostic(
            std::vector<SandboxEditorDiagnostic>& diagnostics,
            SandboxEditorDiagnosticCode code,
            std::string message);

        [[nodiscard]] const Geometry::PropertySet* PropertySetForCatalogDomain(
            const GeometryEntityAvailability& availability,
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            using Domain = SandboxEditorPropertyCatalogDomain;
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

        [[nodiscard]] ProgressiveGeometryDomain ToProgressiveGeometryDomain(
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            using Domain = SandboxEditorPropertyCatalogDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return ProgressiveGeometryDomain::MeshVertex;
            case Domain::MeshEdges:
                return ProgressiveGeometryDomain::MeshEdge;
            case Domain::MeshHalfedges:
                return ProgressiveGeometryDomain::MeshHalfedge;
            case Domain::MeshFaces:
                return ProgressiveGeometryDomain::MeshFace;
            case Domain::GraphVertices:
                return ProgressiveGeometryDomain::GraphVertex;
            case Domain::GraphEdges:
                return ProgressiveGeometryDomain::GraphEdge;
            case Domain::PointCloudPoints:
                return ProgressiveGeometryDomain::Point;
            }
            return ProgressiveGeometryDomain::Unknown;
        }

        [[nodiscard]] SandboxEditorPropertyCatalogValueKind ToPropertyCatalogValueKind(
            const ProgressivePropertyValueKind kind) noexcept
        {
            using Out = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case ProgressivePropertyValueKind::ScalarFloat:
                return Out::ScalarFloat;
            case ProgressivePropertyValueKind::ScalarDouble:
                return Out::ScalarDouble;
            case ProgressivePropertyValueKind::UInt32:
                return Out::UInt32;
            case ProgressivePropertyValueKind::Vec2:
                return Out::Vec2;
            case ProgressivePropertyValueKind::Vec3:
                return Out::Vec3;
            case ProgressivePropertyValueKind::Vec4:
                return Out::Vec4;
            case ProgressivePropertyValueKind::Any:
            case ProgressivePropertyValueKind::Unknown:
                break;
            }
            return Out::Unknown;
        }

        [[nodiscard]] ProgressivePropertyValueKind ToProgressivePropertyValueKind(
            const SandboxEditorPropertyCatalogValueKind kind) noexcept
        {
            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
                return ProgressivePropertyValueKind::ScalarFloat;
            case Kind::ScalarDouble:
                return ProgressivePropertyValueKind::ScalarDouble;
            case Kind::UInt32:
                return ProgressivePropertyValueKind::UInt32;
            case Kind::Vec2:
                return ProgressivePropertyValueKind::Vec2;
            case Kind::Vec3:
                return ProgressivePropertyValueKind::Vec3;
            case Kind::Vec4:
                return ProgressivePropertyValueKind::Vec4;
            case Kind::Unknown:
                break;
            }
            return ProgressivePropertyValueKind::Unknown;
        }

        [[nodiscard]] std::uint8_t ComponentCountForPropertyCatalogKind(
            const SandboxEditorPropertyCatalogValueKind kind) noexcept
        {
            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
            case Kind::ScalarDouble:
            case Kind::UInt32:
                return 1u;
            case Kind::Vec2:
                return 2u;
            case Kind::Vec3:
                return 3u;
            case Kind::Vec4:
                return 4u;
            case Kind::Unknown:
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

        [[nodiscard]] SandboxEditorPropertyValuePreview BuildPropertyValuePreview(
            const Geometry::PropertySet& properties,
            const std::string& name,
            const SandboxEditorPropertyCatalogValueKind kind,
            const std::optional<std::size_t> index)
        {
            if (!index.has_value() || *index >= properties.Size())
                return {};

            SandboxEditorPropertyValuePreview preview{
                .HasValue = true,
                .ElementIndex = *index,
            };

            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
                if (const auto prop = properties.Get<float>(name); prop)
                    preview.Text = std::to_string(prop.Vector()[*index]);
                break;
            case Kind::ScalarDouble:
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
                preview.HasValue = false;
                break;
            }

            if (preview.Text.empty())
                preview.HasValue = false;
            return preview;
        }

        [[nodiscard]] std::optional<std::size_t> PreviewIndexForCatalogDomain(
            const SandboxEditorPropertyCatalogDomain domain,
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

            using Domain = SandboxEditorPropertyCatalogDomain;
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
            std::vector<SandboxEditorPropertyCatalogRow>& out,
            const Geometry::PropertySet& properties,
            const SandboxEditorPropertyCatalogDomain domain,
            const std::optional<std::size_t> previewIndex)
        {
            for (const std::string& name : properties.Properties())
            {
                const SandboxEditorPropertyCatalogValueKind kind =
                    ToPropertyCatalogValueKind(
                        DetectPropertyValueKind(properties, name));
                const bool supported =
                    kind != SandboxEditorPropertyCatalogValueKind::Unknown;
                SandboxEditorPropertyCatalogRow row{
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
                    .Descriptor = ProgressivePropertyBindingDescriptor{
                        .Domain = ToProgressiveGeometryDomain(domain),
                        .PropertyName = name,
                        .ExpectedValueKind =
                            ToProgressivePropertyValueKind(kind),
                        .ExpectedElementCount = properties.Size(),
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
            std::vector<SandboxEditorPropertyCatalogRow>& out,
            const GS::ConstSourceView& view,
            const PrimitiveSelectionResult* primitive,
            const std::uint32_t selectedStableId)
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            const auto append =
                [&](const SandboxEditorPropertyCatalogDomain domain)
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

            append(SandboxEditorPropertyCatalogDomain::MeshVertices);
            append(SandboxEditorPropertyCatalogDomain::MeshEdges);
            append(SandboxEditorPropertyCatalogDomain::MeshHalfedges);
            append(SandboxEditorPropertyCatalogDomain::MeshFaces);
            append(SandboxEditorPropertyCatalogDomain::GraphVertices);
            append(SandboxEditorPropertyCatalogDomain::GraphEdges);
            append(SandboxEditorPropertyCatalogDomain::PointCloudPoints);
        }

        [[nodiscard]] SandboxEditorPropertyBindingTargetModel
        BuildPropertyBindingTargetModel(
            const GS::ConstSourceView& view,
            const ProgressiveSlotExtraction& slot)
        {
            ProgressiveGeometryDomain domain = slot.Property.Domain;
            if (domain == ProgressiveGeometryDomain::Unknown)
            {
                const GS::SourceAvailability availability =
                    GS::BuildSourceAvailability(view);
                domain = DefaultDomainForProgressiveSlot(
                    availability.ProvenanceDomain,
                    slot.Lane,
                    slot.Semantic);
            }

            ProgressivePropertyValueKind expected =
                slot.Property.ExpectedValueKind;
            if (expected == ProgressivePropertyValueKind::Any ||
                expected == ProgressivePropertyValueKind::Unknown)
            {
                expected = DefaultExpectedValueKindForSlot(slot.Semantic);
            }

            SandboxEditorPropertyBindingTargetModel model{
                .Lane = slot.Lane,
                .PresentationKey = slot.PresentationKey,
                .PresentationKind = slot.PresentationKind,
                .Semantic = slot.Semantic,
                .SourceKind = slot.SourceKind,
                .RequiredDomain = domain,
                .ExpectedValueKind = expected,
                .ExpectedElementCount = ResolvePropertyElementCount(
                    view,
                    domain),
            };

            if (domain != ProgressiveGeometryDomain::Unknown)
            {
                std::vector<ProgressivePropertyOption> options =
                    EnumeratePropertyOptions(
                        view,
                        domain,
                        expected,
                        model.ExpectedElementCount);
                model.Options.reserve(options.size());
                for (const ProgressivePropertyOption& option : options)
                {
                    model.Options.push_back(
                        SandboxEditorProgressivePropertyOptionModel{
                            .Descriptor = option.Descriptor,
                            .ActualValueKind = option.ActualValueKind,
                            .ElementCount = option.ElementCount,
                            .Compatible = option.Compatible,
                            .DisabledReason = option.DisabledReason,
                        });
                }
            }
            return model;
        }

        [[nodiscard]] std::optional<SandboxEditorPropertyCatalogDomain>
        VertexChannelCatalogDomainForView(
            const GS::ConstSourceView& view) noexcept
        {
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            using Domain = SandboxEditorPropertyCatalogDomain;
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
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            return PropertySetForCatalogDomain(availability, domain);
        }

        [[nodiscard]] std::optional<AttributeSourceType>
        ToAttributeSourceType(
            const SandboxEditorPropertyCatalogValueKind kind) noexcept
        {
            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
                return AttributeSourceType::Float32;
            case Kind::Vec2:
                return AttributeSourceType::Vec2;
            case Kind::Vec3:
                return AttributeSourceType::Vec3;
            case Kind::Vec4:
                return AttributeSourceType::Vec4;
            case Kind::ScalarDouble:
            case Kind::UInt32:
            case Kind::Unknown:
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
            SandboxEditorModelBuildStats* stats,
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
            SandboxEditorModelBuildStats* modelBuildStats)
        {
            ScopedSandboxEditorStatTimer timer{
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

        [[nodiscard]] VertexChannelSourceBinding*
        FindMutableVertexChannelBinding(
            VertexChannelBindingSet& bindings,
            const VertexChannel channel) noexcept
        {
            switch (channel)
            {
            case VertexChannel::Normal:
                return &bindings.Normal;
            case VertexChannel::Color:
                return &bindings.Color;
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] bool AnyVertexChannelBindingEnabled(
            const VertexChannelBindingSet& bindings) noexcept
        {
            return IsVertexChannelBindingEnabled(bindings.Normal) ||
                   IsVertexChannelBindingEnabled(bindings.Color);
        }

        [[nodiscard]] bool SameVertexChannelSourceBinding(
            const VertexChannelSourceBinding& lhs,
            const VertexChannelSourceBinding& rhs) noexcept
        {
            return lhs.Enabled == rhs.Enabled &&
                   lhs.SourceType == rhs.SourceType &&
                   lhs.SourceProperty == rhs.SourceProperty;
        }

        void MarkVertexChannelDirty(entt::registry& raw,
                                    const entt::entity entity,
                                    const VertexChannel channel)
        {
            switch (channel)
            {
            case VertexChannel::Position:
                Dirty::MarkVertexPositionsDirty(raw, entity);
                break;
            case VertexChannel::Texcoord:
                Dirty::MarkVertexTexcoordsDirty(raw, entity);
                break;
            case VertexChannel::Normal:
                Dirty::MarkVertexNormalsDirty(raw, entity);
                break;
            case VertexChannel::Color:
                Dirty::MarkVertexColorsDirty(raw, entity);
                break;
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                Dirty::MarkVertexAttributesDirty(raw, entity);
                break;
            }
        }

        [[nodiscard]] SandboxEditorVertexChannelBindingTargetModel
        BuildVertexChannelBindingTargetModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& view,
            const std::vector<SandboxEditorPropertyCatalogRow>& rows,
            const SandboxEditorPropertyCatalogDomain domain,
            const VertexChannel channel,
            SandboxEditorModelBuildStats* modelBuildStats)
        {
            SandboxEditorVertexChannelBindingTargetModel model{
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
                model.Resolver = EvaluateVertexChannelBinding(
                    *properties,
                    channel,
                    binding->SourceProperty,
                    binding->SourceType,
                    expectedCount,
                    modelBuildStats);
                model.Diagnostic =
                    BuildVertexChannelResolverDiagnostic(model.Resolver);
            }

            for (const SandboxEditorPropertyCatalogRow& row : rows)
            {
                if (row.Domain != domain || !row.Supported)
                    continue;

                SandboxEditorVertexChannelBindingOptionModel option{
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
            SandboxEditorPropertyCatalogModel& model,
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& view)
        {
            const std::optional<SandboxEditorPropertyCatalogDomain> domain =
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

        [[nodiscard]] SandboxEditorPropertyCatalogModel BuildPropertyCatalogModel(
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            ScopedSandboxEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->PropertyCatalogModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->PropertyCatalogModelBuilds;
            }
            SandboxEditorPropertyCatalogModel model{};
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
                    raw.try_get<ProgressivePresentationBindings>(entity);
                bindings != nullptr)
            {
                const ProgressivePresentationExtractionSnapshot snapshot =
                    BuildProgressivePresentationSnapshot(view, *bindings);
                model.BindingTargets.reserve(snapshot.Slots.size());
                for (const ProgressiveSlotExtraction& slot : snapshot.Slots)
                    model.BindingTargets.push_back(
                        BuildPropertyBindingTargetModel(view, slot));
            }

            AppendVertexChannelBindingTargets(model, context, raw, entity, view);

            if (!view.Valid() && model.Rows.empty())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                    "Selected entity has no valid geometry property catalog.");
            }
            return model;
        }

        [[nodiscard]] bool PropertySupportsPreset(
            const SandboxEditorVisualizationPropertyInfo& property,
            const SandboxEditorVisualizationPropertyPreset preset) noexcept
        {
            switch (preset)
            {
            case SandboxEditorVisualizationPropertyPreset::Scalar:
                return property.ScalarPresetAvailable;
            case SandboxEditorVisualizationPropertyPreset::Isoline:
                return property.IsolinePresetAvailable;
            case SandboxEditorVisualizationPropertyPreset::ColorBuffer:
                return property.ColorBufferPresetAvailable;
            }
            return false;
        }

        [[nodiscard]] SandboxEditorVisualizationAdapterBindingModel
        FromVisualizationAdapterBinding(
            const RenderExtractionCache::VisualizationAdapterBinding& binding)
        {
            return SandboxEditorVisualizationAdapterBindingModel{
                .HasBinding = true,
                .AdapterKey = binding.AdapterKey,
                .BufferBDA = binding.BufferBDA,
                .Kind = binding.Kind,
                .Options = binding.Options,
            };
        }

        [[nodiscard]] RenderExtractionCache::VisualizationAdapterBinding
        ToVisualizationAdapterBinding(
            const SandboxEditorVisualizationAdapterBindingCommand& command)
        {
            return RenderExtractionCache::VisualizationAdapterBinding{
                .AdapterKey = command.AdapterKey,
                .BufferBDA = command.BufferBDA,
                .Kind = command.Kind,
                .Options = command.Options,
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

        struct SandboxEditorRenderHintState
        {
            std::optional<G::RenderSurface> Surface{};
            std::optional<G::RenderEdges> Edges{};
            std::optional<G::RenderPoints> Points{};
        };

        [[nodiscard]] SandboxEditorRenderHintState ReadRenderHintState(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorRenderHintState state{};
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

        [[nodiscard]] bool SameRenderEdges(
            const G::RenderEdges& lhs,
            const G::RenderEdges& rhs)
        {
            return lhs.Domain == rhs.Domain &&
                   lhs.WidthSource == rhs.WidthSource;
        }

        [[nodiscard]] bool SameRenderPoints(
            const G::RenderPoints& lhs,
            const G::RenderPoints& rhs)
        {
            return lhs.Type == rhs.Type &&
                   lhs.SizeSource == rhs.SizeSource;
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
            const SandboxEditorRenderHintState& lhs,
            const SandboxEditorRenderHintState& rhs)
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

        [[nodiscard]] bool AnyRenderHintEdit(
            const SandboxEditorRenderHintCommand& command) noexcept
        {
            return command.SetSurface ||
                   command.SetEdges ||
                   command.SetUniformEdgeWidth ||
                   command.SetPoints ||
                   command.SetPointRenderType ||
                   command.SetUniformPointSize;
        }

        [[nodiscard]] bool RenderHintCommandMatchesDomain(
            const SandboxEditorRenderHintCommand& command,
            const GeometryEntityAvailability& availability) noexcept
        {
            const bool editsSurface = command.SetSurface;
            const bool editsEdges =
                command.SetEdges || command.SetUniformEdgeWidth;
            const bool editsPoints =
                command.SetPoints ||
                command.SetPointRenderType ||
                command.SetUniformPointSize;

            if (!editsSurface && !editsEdges && !editsPoints)
                return false;

            if (editsSurface &&
                availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
                return false;

            if (editsSurface &&
                (!availability.Sources.Has(GS::SourceCapability::VertexPoints) ||
                 !availability.Sources.Has(GS::SourceCapability::Halfedges) ||
                 !availability.Sources.Has(GS::SourceCapability::Faces)))
                return false;

            if (editsEdges)
            {
                if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud ||
                    !availability.Sources.HasPointSource())
                    return false;

                const bool hasExplicitEdges =
                    availability.Sources.Has(GS::SourceCapability::Edges);
                const bool hasMeshWireTopology =
                    availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
                    availability.Sources.Has(GS::SourceCapability::Halfedges) &&
                    availability.Sources.Has(GS::SourceCapability::Faces);
                if (!hasExplicitEdges && !hasMeshWireTopology)
                    return false;
            }

            if (editsPoints && !availability.Sources.HasPointSource())
                return false;

            return true;
        }

        [[nodiscard]] SandboxEditorRenderHintState ApplyRenderHintCommandToState(
            SandboxEditorRenderHintState state,
            const SandboxEditorRenderHintCommand& command)
        {
            if (command.SetSurface)
            {
                if (command.EnableSurface)
                {
                    G::RenderSurface surface =
                        state.Surface.value_or(G::RenderSurface{});
                    surface.Domain = command.SurfaceDomain;
                    state.Surface = surface;
                }
                else
                {
                    state.Surface.reset();
                }
            }

            if (command.SetEdges)
            {
                if (command.EnableEdges)
                {
                    G::RenderEdges lines =
                        state.Edges.value_or(G::RenderEdges{});
                    lines.Domain = command.EdgeDomain;
                    if (command.SetUniformEdgeWidth)
                        lines.WidthSource = command.UniformEdgeWidth;
                    state.Edges = lines;
                }
                else
                {
                    state.Edges.reset();
                }
            }
            else if (command.SetUniformEdgeWidth && state.Edges.has_value())
            {
                state.Edges->WidthSource = command.UniformEdgeWidth;
            }

            if (command.SetPoints)
            {
                if (command.EnablePoints)
                {
                    G::RenderPoints points =
                        state.Points.value_or(G::RenderPoints{});
                    points.Type = command.PointType;
                    if (command.SetUniformPointSize)
                        points.SizeSource = command.UniformPointSize;
                    state.Points = points;
                }
                else
                {
                    state.Points.reset();
                }
            }
            else if (state.Points.has_value())
            {
                if (command.SetPointRenderType)
                    state.Points->Type = command.PointType;
                if (command.SetUniformPointSize)
                    state.Points->SizeSource = command.UniformPointSize;
            }

            return state;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyRenderHintState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const SandboxEditorRenderHintState& state)
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

        [[nodiscard]] bool SameVec4(
            const glm::vec4 lhs,
            const glm::vec4 rhs) noexcept
        {
            return lhs.x == rhs.x &&
                   lhs.y == rhs.y &&
                   lhs.z == rhs.z &&
                   lhs.w == rhs.w;
        }

        [[nodiscard]] bool SameVisualizationAdapterOptions(
            const VisualizationAdapterOptions& lhs,
            const VisualizationAdapterOptions& rhs) noexcept
        {
            return lhs.SourceName == rhs.SourceName &&
                   lhs.OutputName == rhs.OutputName &&
                   lhs.Domain == rhs.Domain &&
                   lhs.BufferBDA == rhs.BufferBDA &&
                   lhs.ColorBufferBDA == rhs.ColorBufferBDA &&
                   lhs.PositionBufferBDA == rhs.PositionBufferBDA &&
                   lhs.VectorBufferBDA == rhs.VectorBufferBDA &&
                   lhs.AutoRange == rhs.AutoRange &&
                   lhs.RangeMin == rhs.RangeMin &&
                   lhs.RangeMax == rhs.RangeMax &&
                   lhs.Colormap == rhs.Colormap &&
                   lhs.IsoValueCount == rhs.IsoValueCount &&
                   lhs.LineWidth == rhs.LineWidth &&
                   SameVec4(lhs.OverlayColor, rhs.OverlayColor) &&
                   lhs.VectorScale == rhs.VectorScale &&
                   SameVec4(lhs.VectorColor, rhs.VectorColor) &&
                   lhs.DepthTested == rhs.DepthTested &&
                   lhs.EmitHtexPreview == rhs.EmitHtexPreview &&
                   lhs.EmitFragmentBake == rhs.EmitFragmentBake &&
                   lhs.SourceAttributeName == rhs.SourceAttributeName &&
                   lhs.FragmentBakeMapping == rhs.FragmentBakeMapping &&
                   lhs.MeshHasTexcoords == rhs.MeshHasTexcoords &&
                   lhs.PatchCount == rhs.PatchCount &&
                   lhs.FaceCount == rhs.FaceCount &&
                   lhs.AtlasWidth == rhs.AtlasWidth &&
                   lhs.AtlasHeight == rhs.AtlasHeight &&
                   lhs.TexcoordBufferBDA == rhs.TexcoordBufferBDA &&
                   lhs.HtexRecreatePayloadToken == rhs.HtexRecreatePayloadToken;
        }

        [[nodiscard]] bool SameVisualizationAdapterBinding(
            const RenderExtractionCache::VisualizationAdapterBinding& lhs,
            const RenderExtractionCache::VisualizationAdapterBinding& rhs) noexcept
        {
            return lhs.AdapterKey == rhs.AdapterKey &&
                   lhs.BufferBDA == rhs.BufferBDA &&
                   lhs.Kind == rhs.Kind &&
                   SameVisualizationAdapterOptions(lhs.Options, rhs.Options);
        }

        [[nodiscard]] bool SameProgressiveDefaultValue(
            const ProgressiveDefaultValue& lhs,
            const ProgressiveDefaultValue& rhs) noexcept
        {
            return lhs.Kind == rhs.Kind &&
                   SameVec4(lhs.Vector, rhs.Vector) &&
                   lhs.Scalar == rhs.Scalar &&
                   lhs.UInt == rhs.UInt;
        }

        [[nodiscard]] bool IsFiniteDefaultValue(
            const ProgressiveDefaultValue& value) noexcept
        {
            return std::isfinite(value.Vector.x) &&
                   std::isfinite(value.Vector.y) &&
                   std::isfinite(value.Vector.z) &&
                   std::isfinite(value.Vector.w) &&
                   std::isfinite(value.Scalar);
        }

        [[nodiscard]] bool SameProgressivePropertyDescriptor(
            const ProgressivePropertyBindingDescriptor& lhs,
            const ProgressivePropertyBindingDescriptor& rhs) noexcept
        {
            return lhs.Domain == rhs.Domain &&
                   lhs.PropertyName == rhs.PropertyName &&
                   lhs.ExpectedValueKind == rhs.ExpectedValueKind &&
                   lhs.ExpectedElementCount == rhs.ExpectedElementCount &&
                   lhs.SourceGeneration == rhs.SourceGeneration;
        }

        [[nodiscard]] bool IsActiveDerivedJobStatus(
            const DerivedJobStatus status) noexcept
        {
            return status == DerivedJobStatus::Blocked ||
                   status == DerivedJobStatus::Queued ||
                   status == DerivedJobStatus::Running ||
                   status == DerivedJobStatus::Applying;
        }

        [[nodiscard]] bool IsFailedDerivedJobStatus(
            const DerivedJobStatus status) noexcept
        {
            return status == DerivedJobStatus::Failed ||
                   status == DerivedJobStatus::Cancelled ||
                   status == DerivedJobStatus::StaleDiscarded;
        }

        constexpr std::string_view kUvRegenerationJobOutputName{
            "uv_regeneration"};

        [[nodiscard]] ProgressivePropertyValueKind DefaultExpectedValueKindForSlot(
            const ProgressiveSlotSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case ProgressiveSlotSemantic::Normal:
            case ProgressiveSlotSemantic::PointNormalOrientation:
                return ProgressivePropertyValueKind::Vec3;
            case ProgressiveSlotSemantic::Albedo:
            case ProgressiveSlotSemantic::PointColor:
            case ProgressiveSlotSemantic::LineColor:
                return ProgressivePropertyValueKind::Vec4;
            case ProgressiveSlotSemantic::Roughness:
            case ProgressiveSlotSemantic::Metallic:
            case ProgressiveSlotSemantic::ScalarField:
            case ProgressiveSlotSemantic::Displacement:
            case ProgressiveSlotSemantic::PointScalarField:
            case ProgressiveSlotSemantic::PointSize:
            case ProgressiveSlotSemantic::LineScalarField:
            case ProgressiveSlotSemantic::LineWidth:
                return ProgressivePropertyValueKind::ScalarFloat;
            }
            return ProgressivePropertyValueKind::Any;
        }

        [[nodiscard]] ProgressiveGeometryDomain DefaultDomainForProgressiveSlot(
            const GS::Domain sourceDomain,
            const ProgressiveRenderLane lane,
            const ProgressiveSlotSemantic semantic) noexcept
        {
            switch (sourceDomain)
            {
            case GS::Domain::Mesh:
                if (semantic == ProgressiveSlotSemantic::LineColor ||
                    semantic == ProgressiveSlotSemantic::LineScalarField ||
                    semantic == ProgressiveSlotSemantic::LineWidth)
                {
                    return ProgressiveGeometryDomain::MeshEdge;
                }
                if (semantic == ProgressiveSlotSemantic::ScalarField)
                    return ProgressiveGeometryDomain::MeshFace;
                if (lane == ProgressiveRenderLane::Edges)
                    return ProgressiveGeometryDomain::MeshEdge;
                return ProgressiveGeometryDomain::MeshVertex;
            case GS::Domain::Graph:
                if (lane == ProgressiveRenderLane::Edges ||
                    semantic == ProgressiveSlotSemantic::LineColor ||
                    semantic == ProgressiveSlotSemantic::LineScalarField ||
                    semantic == ProgressiveSlotSemantic::LineWidth)
                {
                    return ProgressiveGeometryDomain::GraphEdge;
                }
                return ProgressiveGeometryDomain::GraphVertex;
            case GS::Domain::PointCloud:
                return ProgressiveGeometryDomain::Point;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return ProgressiveGeometryDomain::Unknown;
        }

        [[nodiscard]] SandboxEditorProgressivePropertyOptionModel
        ToProgressivePropertyOptionModel(const ProgressivePropertyOption& option)
        {
            return SandboxEditorProgressivePropertyOptionModel{
                .Descriptor = option.Descriptor,
                .ActualValueKind = option.ActualValueKind,
                .ElementCount = option.ElementCount,
                .Compatible = option.Compatible,
                .DisabledReason = option.DisabledReason,
            };
        }

        [[nodiscard]] SandboxEditorProgressiveJobDependencyModel
        ToProgressiveJobDependencyModel(const DerivedJobDependency& dependency)
        {
            return SandboxEditorProgressiveJobDependencyModel{
                .Job = dependency.Job,
                .Reason = dependency.Reason,
            };
        }

        [[nodiscard]] SandboxEditorProgressiveJobModel ToProgressiveJobModel(
            const DerivedJobSnapshot& job)
        {
            SandboxEditorProgressiveJobModel model{
                .Handle = job.Handle,
                .Key = job.Key,
                .Name = job.Name,
                .RequestedJobDomain = job.RequestedJobDomain,
                .ResolvedJobDomain = job.ResolvedJobDomain,
                .Status = job.Status,
                .NormalizedProgress = job.NormalizedProgress,
                .ProgressDeterminate = job.ProgressDeterminate,
                .PreviousOutputRetained = job.PreviousOutputRetained,
                .PayloadToken = job.PayloadToken,
                .ElapsedMilliseconds = job.ElapsedMilliseconds,
                .Diagnostic = job.Diagnostic,
            };
            model.Dependencies.reserve(job.Dependencies.size());
            for (const DerivedJobDependency& dependency : job.Dependencies)
                model.Dependencies.push_back(
                    ToProgressiveJobDependencyModel(dependency));
            return model;
        }

        [[nodiscard]] std::optional<SandboxEditorProgressiveJobModel>
        FindDerivedJobModelForOutput(
            const DerivedJobQueueSnapshot* jobs,
            const std::uint32_t stableEntityId,
            const std::string_view outputName)
        {
            if (jobs == nullptr)
                return std::nullopt;

            const DerivedJobSnapshot* selected = nullptr;
            for (const DerivedJobSnapshot& job : jobs->Entries)
            {
                if (job.Key.EntityId != stableEntityId ||
                    std::string_view{job.Key.OutputName} != outputName)
                {
                    continue;
                }

                if (selected == nullptr)
                {
                    selected = &job;
                    continue;
                }

                const bool jobActive = IsActiveDerivedJobStatus(job.Status);
                const bool selectedActive =
                    IsActiveDerivedJobStatus(selected->Status);
                if (jobActive || !selectedActive)
                    selected = &job;
            }

            if (selected == nullptr)
                return std::nullopt;
            return ToProgressiveJobModel(*selected);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyProgressiveBindingsState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const ProgressivePresentationBindings& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            raw.emplace_or_replace<ProgressivePresentationBindings>(entity, state);
            return EditorCommandHistoryStatus::Applied;
        }

        struct ProgressiveSlotLookup
        {
            ProgressivePresentationBinding* Presentation{nullptr};
            ProgressiveSlotBinding* Slot{nullptr};
        };

        [[nodiscard]] ProgressiveSlotLookup FindMutableProgressiveSlot(
            ProgressivePresentationBindings& bindings,
            const std::string& presentationKey,
            const ProgressiveSlotSemantic semantic)
        {
            if (!presentationKey.empty())
            {
                ProgressivePresentationBinding* presentation =
                    FindPresentationBinding(bindings, presentationKey);
                if (presentation == nullptr)
                    return {};
                return ProgressiveSlotLookup{
                    .Presentation = presentation,
                    .Slot = FindSlotBinding(*presentation, semantic),
                };
            }

            for (ProgressivePresentationBinding& presentation :
                 bindings.Presentations)
            {
                if (ProgressiveSlotBinding* slot =
                        FindSlotBinding(presentation, semantic))
                {
                    return ProgressiveSlotLookup{
                        .Presentation = &presentation,
                        .Slot = slot,
                    };
                }
            }
            return {};
        }

        [[nodiscard]] SandboxEditorCommandStatus ToSandboxEditorCommandStatus(
            EditorCommandHistoryStatus status) noexcept;

        [[nodiscard]] SandboxEditorCommandStatus CommitProgressiveBindingsChange(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            ProgressivePresentationBindings before,
            ProgressivePresentationBindings after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult result =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Change Progressive Presentation",
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyProgressiveBindingsState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyProgressiveBindingsState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(result.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyProgressiveBindingsState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        [[nodiscard]] bool PropertySourceKindAllowedForProgressiveSlotCommand(
            const ProgressiveSlotSourceKind sourceKind) noexcept
        {
            return sourceKind == ProgressiveSlotSourceKind::PropertyBake ||
                   sourceKind == ProgressiveSlotSourceKind::PropertyBuffer;
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

        [[nodiscard]] SandboxEditorCommandStatus ToSandboxEditorCommandStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            switch (status)
            {
            case EditorCommandHistoryStatus::Applied:
            case EditorCommandHistoryStatus::Recorded:
            case EditorCommandHistoryStatus::Undone:
            case EditorCommandHistoryStatus::Redone:
                return SandboxEditorCommandStatus::Applied;
            case EditorCommandHistoryStatus::NoChange:
                return SandboxEditorCommandStatus::NoChange;
            case EditorCommandHistoryStatus::MissingScene:
                return SandboxEditorCommandStatus::MissingScene;
            case EditorCommandHistoryStatus::MissingSelectionController:
                return SandboxEditorCommandStatus::MissingSelectionController;
            case EditorCommandHistoryStatus::StaleEntity:
                return SandboxEditorCommandStatus::StaleEntity;
            case EditorCommandHistoryStatus::MissingTransform:
                return SandboxEditorCommandStatus::MissingTransform;
            case EditorCommandHistoryStatus::EmptyUndoStack:
            case EditorCommandHistoryStatus::EmptyRedoStack:
            case EditorCommandHistoryStatus::InvalidCommand:
            case EditorCommandHistoryStatus::CommandFailed:
            case EditorCommandHistoryStatus::UndoFailed:
            case EditorCommandHistoryStatus::RedoFailed:
            case EditorCommandHistoryStatus::UnsupportedOperation:
                return SandboxEditorCommandStatus::NoChange;
            }
            return SandboxEditorCommandStatus::NoChange;
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

        constexpr SandboxEditorGeometryProcessingDomain kMeshTopologyDomains =
            SandboxEditorGeometryProcessingDomain::MeshVertices |
            SandboxEditorGeometryProcessingDomain::MeshEdges |
            SandboxEditorGeometryProcessingDomain::MeshHalfedges |
            SandboxEditorGeometryProcessingDomain::MeshFaces;

        [[nodiscard]] constexpr bool IsSurfaceTopologyAlgorithm(
            const SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
            case SandboxEditorGeometryProcessingAlgorithm::MeshDenoise:
            case SandboxEditorGeometryProcessingAlgorithm::Curvature:
            case SandboxEditorGeometryProcessingAlgorithm::Remeshing:
            case SandboxEditorGeometryProcessingAlgorithm::Simplification:
            case SandboxEditorGeometryProcessingAlgorithm::Smoothing:
            case SandboxEditorGeometryProcessingAlgorithm::Subdivision:
            case SandboxEditorGeometryProcessingAlgorithm::Repair:
                return true;
            case SandboxEditorGeometryProcessingAlgorithm::KMeans:
            case SandboxEditorGeometryProcessingAlgorithm::NormalEstimation:
            case SandboxEditorGeometryProcessingAlgorithm::ShortestPath:
            case SandboxEditorGeometryProcessingAlgorithm::ConvexHull:
            case SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            case SandboxEditorGeometryProcessingAlgorithm::VectorHeat:
            case SandboxEditorGeometryProcessingAlgorithm::Parameterization:
            case SandboxEditorGeometryProcessingAlgorithm::BooleanCSG:
            case SandboxEditorGeometryProcessingAlgorithm::Registration:
            case SandboxEditorGeometryProcessingAlgorithm::BilateralFilter:
            case SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation:
            case SandboxEditorGeometryProcessingAlgorithm::KernelDensity:
            case SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            case SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            case SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
                return false;
            }
            return false;
        }

        [[nodiscard]] SandboxEditorGeometryProcessingDomain
        DomainsForSourceView(const GS::ConstSourceView& view) noexcept
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            SandboxEditorGeometryProcessingDomain domains =
                SandboxEditorGeometryProcessingDomain::None;

            if (availability.Sources.ProvenanceDomain == GS::Domain::Mesh)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshVertex))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshEdge))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshEdges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshHalfedge))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshHalfedges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshFace))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshFaces;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::Graph)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphNode))
                    domains |= SandboxEditorGeometryProcessingDomain::GraphVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphEdge))
                    domains |= SandboxEditorGeometryProcessingDomain::GraphEdges;
                if (availability.Sources.Has(
                        GS::SourceCapability::Halfedges))
                    domains |= SandboxEditorGeometryProcessingDomain::GraphHalfedges;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::PointCloudPoint))
                    domains |= SandboxEditorGeometryProcessingDomain::PointCloudPoints;
            }

            return domains;
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

        void AppendDiagnostics(std::vector<SandboxEditorDiagnostic>& destination,
                               const std::vector<SandboxEditorDiagnostic>& source)
        {
            destination.insert(destination.end(), source.begin(), source.end());
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

        [[nodiscard]] ProgressiveEntityShape InferProgressiveEntityShape(
            const GS::ConstSourceView& view)
        {
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            switch (availability.ProvenanceDomain)
            {
            case GS::Domain::Mesh:
                return ProgressiveEntityShape::MeshLeaf;
            case GS::Domain::Graph:
                return ProgressiveEntityShape::GraphLeaf;
            case GS::Domain::PointCloud:
                return ProgressiveEntityShape::PointCloudLeaf;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return ProgressiveEntityShape::Unknown;
        }

        void AppendProgressiveJobRowsForEntity(
            SandboxEditorProgressiveRenderDataModel& model,
            const DerivedJobQueueSnapshot* jobs,
            const std::uint32_t stableEntityId)
        {
            if (jobs == nullptr)
                return;

            for (const DerivedJobSnapshot& job : jobs->Entries)
            {
                if (job.Key.EntityId == stableEntityId)
                    model.Jobs.push_back(ToProgressiveJobModel(job));
            }
        }

        void AccumulateProgressiveJobSummaryForEntity(
            SandboxEditorProgressiveCompositionSummary& summary,
            const DerivedJobQueueSnapshot* jobs,
            const std::uint32_t stableEntityId)
        {
            if (jobs == nullptr)
                return;

            for (const DerivedJobSnapshot& job : jobs->Entries)
            {
                if (job.Key.EntityId != stableEntityId)
                    continue;

                ++summary.ChildJobCount;
                if (IsActiveDerivedJobStatus(job.Status))
                    ++summary.ChildActiveJobCount;
                if (IsFailedDerivedJobStatus(job.Status))
                    ++summary.ChildFailedJobCount;
            }
        }

        [[nodiscard]] std::vector<SandboxEditorProgressivePropertyOptionModel>
        BuildProgressiveSlotPropertyOptions(
            const GS::ConstSourceView& view,
            const ProgressiveSlotExtraction& extractedSlot)
        {
            ProgressiveGeometryDomain domain = extractedSlot.Property.Domain;
            if (domain == ProgressiveGeometryDomain::Unknown)
            {
                const GS::SourceAvailability availability =
                    GS::BuildSourceAvailability(view);
                domain = DefaultDomainForProgressiveSlot(
                    availability.ProvenanceDomain,
                    extractedSlot.Lane,
                    extractedSlot.Semantic);
            }
            if (domain == ProgressiveGeometryDomain::Unknown)
                return {};

            ProgressivePropertyValueKind expected =
                extractedSlot.Property.ExpectedValueKind;
            if (expected == ProgressivePropertyValueKind::Any ||
                expected == ProgressivePropertyValueKind::Unknown)
            {
                expected = DefaultExpectedValueKindForSlot(extractedSlot.Semantic);
            }

            const std::size_t expectedCount =
                ResolvePropertyElementCount(view, domain);
            std::vector<ProgressivePropertyOption> options =
                EnumeratePropertyOptions(view, domain, expected, expectedCount);

            std::vector<SandboxEditorProgressivePropertyOptionModel> out{};
            out.reserve(options.size());
            for (const ProgressivePropertyOption& option : options)
                out.push_back(ToProgressivePropertyOptionModel(option));
            return out;
        }

        [[nodiscard]] SandboxEditorProgressiveSlotModel ToProgressiveSlotModel(
            const GS::ConstSourceView& view,
            const ProgressivePresentationBindings& bindings,
            const ProgressiveSlotExtraction& extractedSlot)
        {
            SandboxEditorProgressiveSlotModel model{
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

            if (const ProgressivePresentationBinding* presentation =
                    FindPresentationBinding(bindings, extractedSlot.PresentationKey))
            {
                if (const ProgressiveSlotBinding* slot =
                        FindSlotBinding(*presentation, extractedSlot.Semantic))
                {
                    model.AuthoredTexture = slot->AuthoredTexture;
                    model.GeneratedTexture = slot->GeneratedTexture;
                }
            }

            model.PropertyOptions =
                BuildProgressiveSlotPropertyOptions(view, extractedSlot);
            return model;
        }

        void AccumulateProgressiveChildSummary(
            const entt::registry& raw,
            SandboxEditorProgressiveCompositionSummary& summary,
            const ECS::EntityHandle child,
            const DerivedJobQueueSnapshot* jobs)
        {
            if (!raw.valid(child))
                return;

            ++summary.ChildCount;
            const std::uint32_t childStableId =
                SelectionController::ToStableEntityId(child);
            AccumulateProgressiveJobSummaryForEntity(summary, jobs, childStableId);

            const auto* bindings =
                raw.try_get<ProgressivePresentationBindings>(child);
            if (bindings == nullptr)
                return;

            ++summary.ChildBindingsCount;
            const GS::ConstSourceView childView = GS::BuildConstView(raw, child);
            const ProgressivePresentationExtractionSnapshot snapshot =
                BuildProgressivePresentationSnapshot(childView, *bindings);

            summary.ChildSlotCount += snapshot.Stats.SlotCount;
            summary.ChildPendingSlotCount += snapshot.Stats.PendingSlotCount;
            summary.ChildFailedSlotCount += snapshot.Stats.FailedSlotCount;
            summary.ChildFailedSlotCount += snapshot.Stats.UnsupportedSlotCount;
        }

        void AccumulateProgressiveCompositionSummary(
            const entt::registry& raw,
            SandboxEditorProgressiveRenderDataModel& model,
            const ECS::EntityHandle entity,
            const DerivedJobQueueSnapshot* jobs)
        {
            const ECS::Hierarchy::Structure::HierarchyQueryResult children =
                ECS::Hierarchy::Structure::CollectChildren(raw, entity);
            if (!children.Succeeded())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    SandboxEditorDiagnosticCode::CorruptHierarchy,
                    std::string{"Progressive composition hierarchy query failed: "} +
                        ECS::Hierarchy::Structure::
                            DebugNameForHierarchyQueryStatus(children.Status) +
                        ".");
                return;
            }
            if (children.Entities.empty())
                return;

            SandboxEditorProgressiveCompositionSummary& summary =
                model.Composition;
            summary.HasChildren = true;
            for (const ECS::EntityHandle child : children.Entities)
                AccumulateProgressiveChildSummary(raw, summary, child, jobs);
        }

        [[nodiscard]] SandboxEditorProgressiveRenderDataModel
        BuildProgressiveRenderDataModel(
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->ProgressiveModelBuilds;
            }
            SandboxEditorProgressiveRenderDataModel model{};
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            model.Shape = InferProgressiveEntityShape(view);

            const std::uint32_t stableEntityId =
                SelectionController::ToStableEntityId(entity);
            AppendProgressiveJobRowsForEntity(
                model,
                context.DerivedJobs,
                stableEntityId);
            AccumulateProgressiveCompositionSummary(
                raw,
                model,
                entity,
                context.DerivedJobs);
            if (model.Composition.HasChildren)
                model.Shape = ProgressiveEntityShape::Composition;

            const auto* bindings =
                raw.try_get<ProgressivePresentationBindings>(entity);
            if (bindings == nullptr)
                return model;

            model.HasBindings = true;
            model.Shape = bindings->Shape;
            model.BindingGeneration = bindings->BindingGeneration;

            const ProgressivePresentationExtractionSnapshot snapshot =
                BuildProgressivePresentationSnapshot(view, *bindings);
            model.Stats = snapshot.Stats;
            model.Slots.reserve(snapshot.Slots.size());
            for (const ProgressiveSlotExtraction& slot : snapshot.Slots)
                model.Slots.push_back(ToProgressiveSlotModel(view, *bindings, slot));

            if (snapshot.Stats.DiagnosticCount > 0u)
            {
                AddDiagnostic(
                    model.Diagnostics,
                    SandboxEditorDiagnosticCode::InvalidVisualizationProperty,
                    "Progressive render-data has slot diagnostics.");
            }
            return model;
        }

        [[nodiscard]] std::optional<std::size_t> FindCatalogMatchIndex(
            const SandboxEditorPropertyCatalogModel& catalog,
            const ProgressivePropertyBindingDescriptor& descriptor)
        {
            if (descriptor.Domain == ProgressiveGeometryDomain::Unknown ||
                descriptor.PropertyName.empty())
            {
                return std::nullopt;
            }

            for (std::size_t i = 0u; i < catalog.Rows.size(); ++i)
            {
                const SandboxEditorPropertyCatalogRow& row = catalog.Rows[i];
                if (row.Descriptor.Domain == descriptor.Domain &&
                    row.Name == descriptor.PropertyName)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] SandboxEditorBoundRenderStateRow MakeRenderHintRow(
            std::string label,
            const ProgressiveRenderLane lane,
            const bool enabled,
            std::string sourceDescription,
            std::string disabledReason = {})
        {
            return SandboxEditorBoundRenderStateRow{
                .Kind = SandboxEditorBoundRenderStateRowKind::RenderHint,
                .Label = std::move(label),
                .Lane = lane,
                .Readiness = enabled
                    ? ProgressiveReadinessState::Ready
                    : ProgressiveReadinessState::Unset,
                .Enabled = enabled,
                .SourceDescription = std::move(sourceDescription),
                .DisabledReason = std::move(disabledReason),
            };
        }

        void AppendRenderHintRows(
            std::vector<SandboxEditorBoundRenderStateRow>& rows,
            const SandboxEditorRenderHintModel& hints)
        {
            rows.push_back(
                MakeRenderHintRow(
                    "Surface render hint",
                    ProgressiveRenderLane::Surface,
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
                    ProgressiveRenderLane::Edges,
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
                    ProgressiveRenderLane::Points,
                    hints.HasRenderPoints,
                    std::move(pointSource),
                    hints.HasRenderPoints ? std::string{} : "point rendering is not enabled"));
        }

        void AppendBoundSlotRows(
            std::vector<SandboxEditorBoundRenderStateRow>& rows,
            const SandboxEditorPropertyCatalogModel& catalog,
            const SandboxEditorProgressiveRenderDataModel& progressive)
        {
            for (const SandboxEditorProgressiveSlotModel& slot :
                 progressive.Slots)
            {
                const std::optional<std::size_t> match =
                    FindCatalogMatchIndex(catalog, slot.Property);
                rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::ProgressiveSlot,
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
            std::vector<SandboxEditorBoundRenderStateRow>& rows,
            const SandboxEditorProgressiveRenderDataModel& progressive)
        {
            for (const SandboxEditorProgressiveJobModel& job :
                 progressive.Jobs)
            {
                rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::DerivedJob,
                    .Label = job.Name,
                    .Lane = ProgressiveRenderLane::Surface,
                    .Semantic = job.Key.OutputSemantic,
                    .Readiness = IsFailedDerivedJobStatus(job.Status)
                        ? ProgressiveReadinessState::Failed
                        : (IsActiveDerivedJobStatus(job.Status)
                               ? ProgressiveReadinessState::Pending
                               : ProgressiveReadinessState::Ready),
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

        [[nodiscard]] SandboxEditorBoundRenderStateModel BuildBoundRenderStateModel(
            const SandboxEditorContext& context,
            const SandboxEditorPropertyCatalogModel& catalog,
            const SandboxEditorProgressiveRenderDataModel& progressive,
            const SandboxEditorRenderHintModel& renderHints,
            const SandboxEditorGeometryDomainModel& geometry,
            const std::uint32_t stableEntityId)
        {
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->BoundStateModelBuilds;
            }
            SandboxEditorBoundRenderStateModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = stableEntityId;
            model.Shape = progressive.Shape;
            model.BindingGeneration = progressive.BindingGeneration;
            model.Composition = progressive.Composition;

            AppendRenderHintRows(model.Rows, renderHints);
            AppendBoundSlotRows(model.Rows, catalog, progressive);
            AppendBoundJobRows(model.Rows, progressive);

            if (progressive.Composition.HasChildren)
            {
                model.Rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::CompositionSummary,
                    .Label = "Composition summary",
                    .Readiness = progressive.Composition.ChildFailedSlotCount > 0u ||
                                         progressive.Composition.ChildFailedJobCount > 0u
                                     ? ProgressiveReadinessState::Failed
                                     : (progressive.Composition.ChildPendingSlotCount > 0u ||
                                                progressive.Composition.ChildActiveJobCount > 0u
                                            ? ProgressiveReadinessState::Pending
                                            : ProgressiveReadinessState::Ready),
                    .Enabled = true,
                    .SourceDescription =
                        "children:" + std::to_string(progressive.Composition.ChildCount),
                });
            }

            if (!progressive.HasBindings)
            {
                model.Rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::DisabledCommand,
                    .Label = "Progressive bindings",
                    .Readiness = ProgressiveReadinessState::Unsupported,
                    .Enabled = false,
                    .DisabledReason =
                        "selected entity has no progressive presentation bindings",
                });
            }

            if (geometry.Domain == GS::Domain::Graph ||
                geometry.Domain == GS::Domain::PointCloud)
            {
                model.Rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::DisabledCommand,
                    .Label = "Texture bake",
                    .Readiness = ProgressiveReadinessState::Unsupported,
                    .Enabled = false,
                    .DisabledReason =
                        "texture baking is available for mesh surface slots only",
                });
            }

            if (model.Rows.empty())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    SandboxEditorDiagnosticCode::InvalidVisualizationProperty,
                    "No bound render state rows were available.");
            }
            return model;
        }

        enum class MeshFaceRingStatus : std::uint8_t
        {
            Triangulate,
            Skip,
            Invalid,
        };

        [[nodiscard]] MeshFaceRingStatus BuildMeshFaceRing(
            const std::vector<std::uint32_t>& faceHalfedges,
            const std::vector<std::uint32_t>& halfedgeFaces,
            const std::vector<std::uint32_t>& nextHalfedges,
            const std::vector<std::uint32_t>& toVertices,
            const std::size_t faceIndex,
            const std::uint32_t vertexCount,
            std::vector<std::uint32_t>& outRing)
        {
            constexpr std::uint32_t invalid = std::numeric_limits<std::uint32_t>::max();
            outRing.clear();
            if (faceIndex >= faceHalfedges.size())
                return MeshFaceRingStatus::Invalid;

            const std::size_t halfedgeCount = toVertices.size();
            const std::uint32_t first = faceHalfedges[faceIndex];
            if (first == invalid)
                return MeshFaceRingStatus::Skip;
            if (first >= halfedgeCount)
                return MeshFaceRingStatus::Invalid;

            const std::uint32_t owner = halfedgeFaces[first];
            if (owner == invalid || owner >= faceHalfedges.size())
                return MeshFaceRingStatus::Skip;
            if (owner != static_cast<std::uint32_t>(faceIndex))
                return MeshFaceRingStatus::Skip;

            std::uint32_t halfedge = first;
            for (std::size_t step = 0u; step <= halfedgeCount; ++step)
            {
                if (halfedge >= halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                if (halfedgeFaces[halfedge] != static_cast<std::uint32_t>(faceIndex))
                    return MeshFaceRingStatus::Invalid;

                const std::uint32_t vertex = toVertices[halfedge];
                if (vertex >= vertexCount)
                    return MeshFaceRingStatus::Invalid;
                outRing.push_back(vertex);

                const std::uint32_t next = nextHalfedges[halfedge];
                if (next == first)
                    break;
                if (next == invalid || step == halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                halfedge = next;
            }

            return outRing.size() >= 3u
                ? MeshFaceRingStatus::Triangulate
                : MeshFaceRingStatus::Skip;
        }

        struct MeshSoupFromGeometrySourcesResult
        {
            Geometry::MeshSoup::IndexedMesh Mesh{};
            std::vector<std::uint32_t> SourceFaceForSoupFace{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshSoupFromGeometrySourcesResult BuildMeshSoupFromGeometrySources(
            const GS::ConstSourceView& view)
        {
            MeshSoupFromGeometrySourcesResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Diagnostic = "UV regeneration requires selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has no vertex position property";
                return result;
            }
            if (positions.Vector().size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has too many vertices for UV regeneration";
                return result;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces || !faceHalfedges ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().empty())
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has invalid halfedge/face topology";
                return result;
            }

            for (const glm::vec3 position : positions.Vector())
                (void)result.Mesh.AddVertex(position);

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid)
                {
                    result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Diagnostic = "selected mesh topology is not valid for UV regeneration";
                    return result;
                }
                if (status == MeshFaceRingStatus::Skip)
                    continue;

                for (std::size_t i = 1u; i + 1u < ring.size(); ++i)
                {
                    (void)result.Mesh.AddTriangle(ring[0u], ring[i], ring[i + 1u]);
                    result.SourceFaceForSoupFace.push_back(
                        static_cast<std::uint32_t>(faceIndex));
                }
            }

            if (result.Mesh.FaceCount() == 0u)
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has no valid surface faces";
                return result;
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            return result;
        }

        template <typename T>
        void CopyTypedPropertyByXref(
            const Geometry::ConstPropertySet& source,
            const std::string& name,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            const auto sourceProperty = source.Get<T>(name);
            if (!sourceProperty)
                return;

            auto targetProperty = target.GetOrAdd<T>(name, T{});
            for (std::size_t outputIndex = 0u; outputIndex < xrefs.size(); ++outputIndex)
            {
                const std::uint32_t sourceIndex = xrefs[outputIndex];
                if (sourceIndex < sourceProperty.Vector().size())
                    targetProperty[outputIndex] = sourceProperty[sourceIndex];
            }
        }

        void CopyKnownPropertiesByXref(
            const Geometry::ConstPropertySet& source,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            for (const std::string& name : source.Properties())
            {
                CopyTypedPropertyByXref<float>(source, name, xrefs, target);
                CopyTypedPropertyByXref<double>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::uint32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::int32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<bool>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec2>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec3>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec4>(source, name, xrefs, target);
            }
        }

        void CopyUvOutputPropertiesToHalfedgeMesh(
            const Geometry::ConstPropertySet& sourceFaceProperties,
            const bool hasSourceFaceProperties,
            const MeshSoupFromGeometrySourcesResult& soup,
            Geometry::UvAtlas::UvAtlasResult& atlas,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            std::vector<std::uint32_t> sourceVertexForOutputVertex;
            sourceVertexForOutputVertex.reserve(atlas.OutputMesh.VertexCount());
            for (std::size_t i = 0u; i < atlas.OutputMesh.VertexCount(); ++i)
                sourceVertexForOutputVertex.push_back(static_cast<std::uint32_t>(i));

            CopyKnownPropertiesByXref(
                Geometry::ConstPropertySet(atlas.OutputMesh.VertexProperties()),
                sourceVertexForOutputVertex,
                mesh.VertexProperties());

            if (!hasSourceFaceProperties)
                return;

            std::vector<std::uint32_t> sourceFaceForOutputFace;
            sourceFaceForOutputFace.reserve(atlas.SourceFaceForOutputFace.size());
            for (const std::uint32_t soupFace : atlas.SourceFaceForOutputFace)
            {
                sourceFaceForOutputFace.push_back(
                    soupFace < soup.SourceFaceForSoupFace.size()
                        ? soup.SourceFaceForSoupFace[soupFace]
                        : std::numeric_limits<std::uint32_t>::max());
            }

            CopyKnownPropertiesByXref(
                sourceFaceProperties,
                sourceFaceForOutputFace,
                mesh.FaceProperties());
        }

        [[nodiscard]] bool IsTextureBakeSourceDomain(
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            return domain == SandboxEditorPropertyCatalogDomain::MeshVertices ||
                   domain == SandboxEditorPropertyCatalogDomain::MeshFaces;
        }

        [[nodiscard]] bool IsNonBakeableMeshAttribute(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == "v:point" ||
                   name == "v:texcoord" ||
                   name == "v:tex";
        }

        [[nodiscard]] SandboxEditorTextureBakeSourceRow BuildTextureBakeSourceRow(
            const SandboxEditorPropertyCatalogRow& row)
        {
            SandboxEditorTextureBakeSourceRow out{
                .Name = row.Name,
                .CatalogDomain = row.Domain,
                .BakeDomain = ToProgressiveGeometryDomain(row.Domain),
                .ValueKind = row.ValueKind,
                .ExpectedValueKind = ToProgressivePropertyValueKind(row.ValueKind),
                .ElementCount = row.ElementCount,
                .Descriptor = row.Descriptor,
            };

            if (!IsTextureBakeSourceDomain(row.Domain))
            {
                out.Category = SandboxEditorTextureBakeSourceCategory::WrongDomain;
                out.DisabledReason = "texture baking supports mesh vertex and face properties";
                return out;
            }
            if (row.Connectivity || IsNonBakeableMeshAttribute(row.Name))
            {
                out.Category = row.Connectivity
                    ? SandboxEditorTextureBakeSourceCategory::Connectivity
                    : SandboxEditorTextureBakeSourceCategory::Internal;
                out.DisabledReason = row.Connectivity
                    ? "connectivity properties are visible but not texture-bake sources"
                    : "internal mesh coordinate properties are visible but not bake sources";
                return out;
            }
            if (!row.Supported ||
                row.ValueKind == SandboxEditorPropertyCatalogValueKind::Unknown)
            {
                out.Category = SandboxEditorTextureBakeSourceCategory::Unsupported;
                out.DisabledReason = row.UnsupportedReason.empty()
                    ? "unsupported property value type"
                    : row.UnsupportedReason;
                return out;
            }

            out.Category = row.Internal
                ? SandboxEditorTextureBakeSourceCategory::Internal
                : SandboxEditorTextureBakeSourceCategory::Bakeable;
            out.Bakeable = true;
            return out;
        }

        [[nodiscard]] SandboxEditorUvDiagnosticsModel BuildUvDiagnosticsModel(
            const SandboxEditorContext& context,
            const GS::ConstSourceView& view)
        {
            ScopedSandboxEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->UvDiagnosticsModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->UvDiagnosticsModelBuilds;
            }
            SandboxEditorUvDiagnosticsModel model{};
            model.HasSelectedEntity = true;
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            model.IsMesh =
                availability.ProvenanceDomain == GS::Domain::Mesh &&
                availability.Has(GS::SourceCapability::VertexPoints) &&
                availability.Has(GS::SourceCapability::Halfedges) &&
                availability.Has(GS::SourceCapability::Faces);
            if (!model.IsMesh)
            {
                model.Provenance = "unavailable";
                model.UvRegenerationDisabledReason =
                    "UV diagnostics require a selected mesh";
                return model;
            }

            model.VertexCount = view.VerticesAlive();
            model.FaceCount = view.FacesAlive();
            model.BackendId = "xatlas";
            model.Provenance = "missing";
            model.UvRegenerationAvailable = true;

            if (view.VertexSource == nullptr)
                return model;

            const auto texcoords =
                view.VertexSource->Properties.Get<glm::vec2>(
                    model.TexcoordPropertyName);
            model.HasTexcoords = texcoords.IsValid();
            if (model.HasTexcoords)
            {
                model.TexcoordCount = texcoords.Vector().size();
                model.TexcoordCountMatchesVertices =
                    model.TexcoordCount == model.VertexCount;
                model.Provenance = "geometry property";
                model.CheckerPreviewAvailable =
                    model.TexcoordCountMatchesVertices;
                if (!model.TexcoordCountMatchesVertices)
                {
                    model.LastFailure =
                        "texcoord count does not match mesh vertex count";
                }
                else
                {
                    model.TexcoordsFinite = true;
                    SandboxEditorModelBuildStats* stats =
                        context.ModelBuildStats;
                    for (const glm::vec2 uv : texcoords.Vector())
                    {
                        if (stats != nullptr)
                            ++stats->UvDiagnosticsTexcoordElementsScanned;
                        if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                        {
                            model.TexcoordsFinite = false;
                            model.LastFailure =
                                "texcoord property contains non-finite values";
                            break;
                        }
                    }
                    model.CheckerPreviewAvailable = model.TexcoordsFinite;
                }
            }
            return model;
        }

        [[nodiscard]] SandboxEditorTextureBakeControlsModel
        BuildTextureBakeControlsModel(
            const SandboxEditorContext& context,
            const GS::ConstSourceView& view,
            const SandboxEditorPropertyCatalogModel& catalog,
            const std::uint32_t stableEntityId)
        {
            ScopedSandboxEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->TextureBakeModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->TextureBakeModelBuilds;
            }
            SandboxEditorTextureBakeControlsModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = stableEntityId;
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            model.IsMesh =
                availability.ProvenanceDomain == GS::Domain::Mesh &&
                availability.Has(GS::SourceCapability::VertexPoints) &&
                availability.Has(GS::SourceCapability::Halfedges) &&
                availability.Has(GS::SourceCapability::Faces);
            model.HasRuntimeBakeCommand = context.AssetService != nullptr;
            const bool hasOperationalGpu =
                context.Device != nullptr && context.Device->IsOperational();
            model.Uv = BuildUvDiagnosticsModel(context, view);
            model.Uv.UvRegenerationJob = FindDerivedJobModelForOutput(
                context.DerivedJobs,
                stableEntityId,
                kUvRegenerationJobOutputName);

            model.Sources.reserve(catalog.Rows.size());
            SandboxEditorModelBuildStats* stats = context.ModelBuildStats;
            for (const SandboxEditorPropertyCatalogRow& row : catalog.Rows)
            {
                if (stats != nullptr)
                    ++stats->TextureBakeSourceRowsEnumerated;
                model.Sources.push_back(BuildTextureBakeSourceRow(row));
            }

            const bool hasBakeableSource =
                std::any_of(
                    model.Sources.begin(),
                    model.Sources.end(),
                    [](const SandboxEditorTextureBakeSourceRow& row)
                    {
                        return row.Bakeable;
                    });

            model.CanBake = model.IsMesh &&
                            model.HasRuntimeBakeCommand &&
                            hasOperationalGpu &&
                            model.Uv.HasTexcoords &&
                            model.Uv.TexcoordCountMatchesVertices &&
                            model.Uv.TexcoordsFinite &&
                            hasBakeableSource;
            if (!model.IsMesh)
                model.DisabledReason = "texture baking requires a selected mesh";
            else if (!hasOperationalGpu)
                model.DisabledReason = "texture baking requires an operational GPU backend";
            else if (!model.HasRuntimeBakeCommand)
                model.DisabledReason = "runtime selected-mesh bake command is unavailable";
            else if (!model.Uv.HasTexcoords)
                model.DisabledReason = "selected mesh has no resolved texcoord property";
            else if (!model.Uv.TexcoordCountMatchesVertices)
                model.DisabledReason = model.Uv.LastFailure;
            else if (!model.Uv.TexcoordsFinite)
                model.DisabledReason = model.Uv.LastFailure;
            else if (!hasBakeableSource)
                model.DisabledReason = "no bakeable mesh vertex or face properties";
            return model;
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

        [[nodiscard]] const Geometry::PropertySet*
        ResolveSelectedMeshVertexProperties(
            const SandboxEditorContext& context)
        {
            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value() || context.Scene == nullptr)
                return nullptr;

            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(context.Scene->Raw(), *selected);
            return PropertySetForCatalogDomain(
                availability,
                SandboxEditorPropertyCatalogDomain::MeshVertices);
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

        void InvalidateSelectedModelCache(const SandboxEditorContext& context)
        {
            if (context.SelectedModelCache != nullptr)
                context.SelectedModelCache->Clear();
        }

        [[nodiscard]] SandboxEditorCommandStatus InvalidateSelectedModelCacheIfApplied(
            const SandboxEditorContext& context,
            const SandboxEditorCommandStatus status)
        {
            if (status == SandboxEditorCommandStatus::Applied)
                InvalidateSelectedModelCache(context);
            return status;
        }

        [[nodiscard]] std::vector<std::uint32_t> BuildSelectedStableIdsForCacheKey(
            const SandboxEditorContext& context,
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
            const SandboxEditorContext& context)
        {
            if (context.CommandHistory == nullptr)
                return 0u;
            return context.CommandHistory->Snapshot().Revision;
        }

        [[nodiscard]] std::uint64_t CurrentSelectionGeneration(
            const SandboxEditorContext& context)
        {
            if (context.Selection == nullptr)
                return 0u;
            return context.Selection->SelectionGeneration();
        }

        [[nodiscard]] std::uint64_t CurrentPrimitiveSelectionGeneration(
            const SandboxEditorContext& context,
            const SandboxEditorSelectedModelCacheSection section)
        {
            if (section !=
                SandboxEditorSelectedModelCacheSection::SelectedAnalysis)
            {
                return 0u;
            }
            return context.LastRefinedPrimitiveGeneration;
        }

        [[nodiscard]] std::uint64_t CurrentVisualizationAdapterBindingRevision(
            const SandboxEditorContext& context,
            const SandboxEditorSelectedModelCacheSection section)
        {
            if (section != SandboxEditorSelectedModelCacheSection::Visualization ||
                !context.VisualizationAdapterBindings.Available())
            {
                return 0u;
            }
            return context.VisualizationAdapterBindingRevision;
        }

        [[nodiscard]] std::uint64_t VertexBindingGenerationForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            const auto* bindings =
                raw.try_get<VertexChannelBindingSet>(entity);
            return bindings != nullptr ? bindings->BindingGeneration : 0u;
        }

        [[nodiscard]] std::uint64_t ProgressiveBindingGenerationForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorSelectedModelCacheSection section)
        {
            if (section !=
                SandboxEditorSelectedModelCacheSection::SelectedAnalysis)
            {
                return 0u;
            }
            const auto* bindings =
                raw.try_get<ProgressivePresentationBindings>(entity);
            return bindings != nullptr ? bindings->BindingGeneration : 0u;
        }

        constexpr std::uint64_t kSandboxEditorSignatureOffset =
            1469598103934665603ull;
        constexpr std::uint64_t kSandboxEditorSignaturePrime =
            1099511628211ull;

        void MixSignatureByte(std::uint64_t& signature,
                              const std::uint8_t value) noexcept
        {
            signature ^= value;
            signature *= kSandboxEditorSignaturePrime;
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
            const SandboxEditorSelectedModelCacheSection section)
        {
            if (section !=
                SandboxEditorSelectedModelCacheSection::SelectedAnalysis)
            {
                return 0u;
            }

            std::uint64_t signature = kSandboxEditorSignatureOffset;
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

        void AppendSpatialDebugBindingSignature(
            std::uint64_t& signature,
            const ECSC::SpatialDebugBinding* binding) noexcept
        {
            if (binding == nullptr)
            {
                MixSignature(signature, 0u);
                return;
            }

            MixSignature(signature, 1u);
            MixSignature(signature,
                         static_cast<std::uint64_t>(binding->Kind));
            MixSignature(signature, binding->RegistryKey);
            MixSignature(signature, binding->LeafOnly ? 1u : 0u);
            MixSignature(signature, binding->OccupancyOnly ? 1u : 0u);
            MixSignature(signature,
                         static_cast<std::uint64_t>(binding->MaxDepth));
        }

        [[nodiscard]] std::uint64_t VisualizationStateSignatureForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorSelectedModelCacheSection section,
            const SandboxEditorVisualizationTarget target)
        {
            if (section != SandboxEditorSelectedModelCacheSection::Visualization)
                return 0u;

            std::uint64_t signature = kSandboxEditorSignatureOffset;
            AppendSpatialDebugBindingSignature(
                signature,
                raw.try_get<ECSC::SpatialDebugBinding>(entity));
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
            std::uint64_t signature = kSandboxEditorSignatureOffset;
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
            AppendPropertySetMetadataSignature(
                signature,
                5u,
                view.NodeSource != nullptr ? &view.NodeSource->Properties
                                           : nullptr,
                view.NodeSource != nullptr ? view.NodeSource->NumDeleted
                                           : 0u);
            return signature;
        }

        [[nodiscard]] std::uint64_t DerivedJobStateSignatureForEntity(
            const DerivedJobQueueSnapshot* jobs,
            const std::uint32_t stableEntityId,
            const SandboxEditorSelectedModelCacheSection section)
        {
            if (section != SandboxEditorSelectedModelCacheSection::SelectedAnalysis ||
                jobs == nullptr)
            {
                return 0u;
            }

            std::uint64_t signature = kSandboxEditorSignatureOffset;
            std::uint64_t order = 0u;
            for (const DerivedJobSnapshot& job : jobs->Entries)
            {
                if (job.Key.EntityId != stableEntityId)
                    continue;

                MixSignature(signature, order++);
                MixSignature(signature, job.Handle.Index);
                MixSignature(signature, job.Handle.Generation);
                MixSignature(signature, static_cast<std::uint64_t>(job.Status));
                MixSignatureFloat(signature, job.NormalizedProgress);
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 job.RequestedJobDomain));
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 job.ResolvedJobDomain));
                MixSignature(signature,
                             static_cast<std::uint64_t>(job.Key.Domain));
                MixSignature(signature,
                             static_cast<std::uint64_t>(
                                 job.Key.OutputSemantic));
                MixSignatureString(signature, job.Key.OutputName);
                MixSignature(signature, job.PayloadToken);
                MixSignature(signature, job.PreviousOutputRetained ? 1u : 0u);
                MixSignatureString(signature, job.Diagnostic);
            }
            MixSignature(signature, order);
            return order == 0u ? 0u : signature;
        }

        [[nodiscard]] SandboxEditorSelectedModelCacheKey
        BuildSelectedModelCacheKey(
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorGeometryDomainModel& geometry,
            const SandboxEditorSelectedModelCacheSection section,
            const SandboxEditorSelectedAnalysisCacheConsumer
                selectedAnalysisConsumer =
                    SandboxEditorSelectedAnalysisCacheConsumer::Inspector,
            const SandboxEditorVisualizationTarget visualizationTarget =
                SandboxEditorVisualizationTarget::Entity)
        {
            const std::uint32_t stableId =
                SelectionController::ToStableEntityId(entity);
            return SandboxEditorSelectedModelCacheKey{
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
                .ProgressiveBindingGeneration =
                    ProgressiveBindingGenerationForEntity(raw, entity, section),
                .DerivedJobStateSignature = DerivedJobStateSignatureForEntity(
                    context.DerivedJobs,
                    stableId,
                    section),
                .CommandHistoryRevision = CurrentCommandHistoryRevision(context),
                .VisualizationAdapterBindingRevision =
                    CurrentVisualizationAdapterBindingRevision(context, section),
                .ViewportWidth =
                    static_cast<std::uint32_t>(context.CameraViewport.Width),
                .ViewportHeight =
                    static_cast<std::uint32_t>(context.CameraViewport.Height),
                .VisualizationCommandsAvailable =
                    context.VisualizationCommandsAvailable,
                .VisualizationAdapterBindingsAvailable =
                    context.VisualizationAdapterBindings.Available(),
            };
        }

        void RecordSelectedAnalysisCacheHit(const SandboxEditorContext& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.SelectedAnalysisCacheHits;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->SelectedAnalysisCacheHits;
        }

        void RecordSelectedAnalysisCacheMiss(const SandboxEditorContext& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.SelectedAnalysisCacheMisses;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->SelectedAnalysisCacheMisses;
        }

        void RecordVisualizationCacheHit(const SandboxEditorContext& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.VisualizationModelCacheHits;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->VisualizationModelCacheHits;
        }

        void RecordVisualizationCacheMiss(const SandboxEditorContext& context)
        {
            if (context.SelectedModelCache != nullptr)
                ++context.SelectedModelCache->Counters.VisualizationModelCacheMisses;
            if (context.ModelBuildStats != nullptr)
                ++context.ModelBuildStats->VisualizationModelCacheMisses;
        }

        [[nodiscard]] SandboxEditorSelectedAnalysisCacheEntry*
        ResolveSelectedAnalysisCacheEntry(
            SandboxEditorSelectedModelCache& cache,
            const SandboxEditorSelectedAnalysisCacheConsumer consumer)
        {
            const std::size_t index = static_cast<std::size_t>(consumer);
            return index < cache.SelectedAnalysis.size()
                ? &cache.SelectedAnalysis[index]
                : nullptr;
        }

        [[nodiscard]] SandboxEditorSelectedAnalysisModel
        BuildSelectedAnalysisModelUncached(
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& sourceView,
            const SandboxEditorRenderHintModel& renderHints,
            const SandboxEditorGeometryDomainModel& geometry,
            const std::uint32_t stableId)
        {
            ScopedSandboxEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->SelectedAnalysisModelBuildTimeNs
                    : nullptr};
            SandboxEditorSelectedAnalysisModel model{};
            model.PropertyCatalog =
                BuildPropertyCatalogModel(context, raw, entity);
            model.Progressive =
                BuildProgressiveRenderDataModel(context, raw, entity);
            model.BoundState =
                BuildBoundRenderStateModel(
                    context,
                    model.PropertyCatalog,
                    model.Progressive,
                    renderHints,
                    geometry,
                    stableId);
            model.TextureBake =
                BuildTextureBakeControlsModel(
                    context,
                    sourceView,
                    model.PropertyCatalog,
                    stableId);
            return model;
        }

        [[nodiscard]] SandboxEditorSelectedAnalysisModel BuildSelectedAnalysisModel(
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& sourceView,
            const SandboxEditorRenderHintModel& renderHints,
            const SandboxEditorGeometryDomainModel& geometry,
            const std::uint32_t stableId,
            const SandboxEditorSelectedAnalysisCacheConsumer consumer =
                SandboxEditorSelectedAnalysisCacheConsumer::Inspector)
        {
            SandboxEditorSelectedModelCache* cache = context.SelectedModelCache;
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

            SandboxEditorSelectedAnalysisCacheEntry* entry =
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

            const SandboxEditorSelectedModelCacheKey key =
                BuildSelectedModelCacheKey(
                    context,
                    raw,
                    entity,
                    geometry,
                    SandboxEditorSelectedModelCacheSection::SelectedAnalysis,
                    consumer);
            if (entry->Valid && entry->Key == key)
            {
                RecordSelectedAnalysisCacheHit(context);
                return entry->Model;
            }

            RecordSelectedAnalysisCacheMiss(context);
            SandboxEditorSelectedAnalysisModel model =
                BuildSelectedAnalysisModelUncached(
                    context,
                    raw,
                    entity,
                    sourceView,
                    renderHints,
                    geometry,
                    stableId);
            *entry = SandboxEditorSelectedAnalysisCacheEntry{
                .Valid = true,
                .Key = key,
                .Model = model,
            };
            return model;
        }

        [[nodiscard]] bool IsFiniteGeometryPosition(
            const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>>
        CollectFiniteGeometryPositions(
            const Geometry::PropertySet& properties)
        {
            const auto positions =
                properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
                return std::nullopt;
            if (positions.Vector().size() != properties.Size())
                return std::nullopt;

            std::vector<glm::vec3> points{};
            points.reserve(positions.Vector().size());
            for (const glm::vec3& position : positions.Vector())
            {
                if (!IsFiniteGeometryPosition(position))
                    return std::nullopt;
                points.push_back(position);
            }
            return points;
        }

        [[nodiscard]] bool SameGeometryPositions(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (lhs[i].x != rhs[i].x ||
                    lhs[i].y != rhs[i].y ||
                    lhs[i].z != rhs[i].z)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsActiveEditorDerivedJobStatus(
            const DerivedJobStatus status) noexcept
        {
            return status == DerivedJobStatus::Blocked ||
                   status == DerivedJobStatus::Queued ||
                   status == DerivedJobStatus::Running ||
                   status == DerivedJobStatus::Applying;
        }

        [[nodiscard]] bool SameEditorDerivedJobOutput(
            const DerivedJobKey& lhs,
            const DerivedJobKey& rhs) noexcept
        {
            return lhs.EntityId == rhs.EntityId &&
                   lhs.Domain == rhs.Domain &&
                   lhs.OutputSemantic == rhs.OutputSemantic &&
                   lhs.OutputName == rhs.OutputName;
        }

        [[nodiscard]] std::optional<DerivedJobSnapshot>
        FindActiveEditorDerivedJob(
            const SandboxEditorContext& context,
            const DerivedJobKey& key)
        {
            if (context.DerivedJobs == nullptr)
                return std::nullopt;

            for (const DerivedJobSnapshot& entry : context.DerivedJobs->Entries)
            {
                if (IsActiveEditorDerivedJobStatus(entry.Status) &&
                    SameEditorDerivedJobOutput(entry.Key, key))
                {
                    return entry;
                }
            }
            return std::nullopt;
        }

        void AppendDerivedJobHandleToMessage(
            std::string& message,
            const DerivedJobHandle handle)
        {
            if (!handle.IsValid())
                return;

            message += " (job ";
            message += std::to_string(handle.Index);
            message += ":";
            message += std::to_string(handle.Generation);
            message += ")";
        }

        [[nodiscard]] std::string BuildActiveDerivedJobMessage(
            const std::string_view label,
            const DerivedJobSnapshot& job)
        {
            std::string message{label};
            message += " already has an active ";
            message += std::string{ToString(job.Status)};
            message += " job";
            AppendDerivedJobHandleToMessage(message, job.Handle);
            message += ".";
            return message;
        }

        struct MeshForVertexNormalsResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshForVertexNormalsResult
        BuildHalfedgeMeshForVertexNormalRecompute(const GS::MutableSourceView& view)
        {
            MeshForVertexNormalsResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh vertex normals require selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty() ||
                positions.Vector().size() != view.VertexSource->Properties.Size())
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh requires count-matched v:position for normal recompute";
                return result;
            }
            if (positions.Vector().size() >
                static_cast<std::size_t>(
                    std::numeric_limits<Geometry::PropertyIndex>::max()))
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has too many vertices for normal recompute";
                return result;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces ||
                !faceHalfedges ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().size() !=
                    view.FaceSource->Properties.Size())
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has invalid halfedge/face topology for normal recompute";
                return result;
            }

            result.Mesh.Reserve(positions.Vector().size(),
                                view.EdgeSource != nullptr
                                    ? view.EdgeSource->Properties.Size()
                                    : 0u,
                                faceHalfedges.Vector().size());
            for (const glm::vec3 position : positions.Vector())
                (void)result.Mesh.AddVertex(position);

            std::vector<std::uint32_t> ring{};
            ring.reserve(8u);
            std::vector<Geometry::VertexHandle> faceVertices{};
            faceVertices.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid)
                {
                    result.Status =
                        SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "selected mesh topology is not valid for normal recompute";
                    return result;
                }
                if (status == MeshFaceRingStatus::Skip)
                    continue;

                faceVertices.clear();
                for (const std::uint32_t vertex : ring)
                {
                    faceVertices.push_back(
                        Geometry::VertexHandle{
                            static_cast<Geometry::PropertyIndex>(vertex)});
                }
                if (!result.Mesh.AddFace(faceVertices).has_value())
                {
                    result.Status =
                        SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "selected mesh face ring could not be reconstructed for normal recompute";
                    return result;
                }
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            return result;
        }

        [[nodiscard]] SandboxEditorMeshVertexNormalsResult MakeMeshNormalsResult(
            const SandboxEditorCommandStatus status,
            const GN::RecomputeStatus normalStatus,
            const GN::AveragingMode weighting,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorMeshVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .Weighting = weighting,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyMeshNormalCounters(const GN::Result& source,
                                    SandboxEditorMeshVertexNormalsResult& target)
        {
            target.NormalStatus = source.Status;
            target.Weighting = source.Weighting;
            target.VertexSlotCount = source.VertexSlotCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalVertexCount = source.ValidNormalVertexCount;
            target.ProcessedFaceCount = source.ProcessedFaceCount;
            target.DegenerateFaceCount = source.DegenerateFaceCount;
            target.NonFiniteFaceCount = source.NonFiniteFaceCount;
            target.InvalidTopologyFaceCount = source.InvalidTopologyFaceCount;
            target.DegenerateCornerCount = source.DegenerateCornerCount;
            target.FallbackVertexCount = source.FallbackVertexCount;
            target.SkippedDeletedFaceCount = source.SkippedDeletedFaceCount;
            target.SkippedDeletedVertexCount = source.SkippedDeletedVertexCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] bool PublishMeshVertexNormals(
            GS::MutableSourceView& view,
            const GN::Result& result)
        {
            if (view.VertexSource == nullptr || !result.Normals.IsValid())
                return false;

            Geometry::PropertySet& properties = view.VertexSource->Properties;
            if (properties.Exists(GS::PropertyNames::kNormal) &&
                !properties.Get<glm::vec3>(GS::PropertyNames::kNormal))
            {
                return false;
            }

            auto normals = properties.GetOrAdd<glm::vec3>(
                std::string{GS::PropertyNames::kNormal},
                glm::vec3{0.0f, 1.0f, 0.0f});
            if (!normals ||
                normals.Vector().size() != result.Normals.Vector().size())
            {
                return false;
            }

            normals.Vector() = result.Normals.Vector();
            return true;
        }

        [[nodiscard]] bool PublishCanonicalVec3Normals(
            Geometry::PropertySet& properties,
            const std::vector<glm::vec3>& normals)
        {
            if (normals.empty() || normals.size() != properties.Size())
                return false;
            if (properties.Exists(GS::PropertyNames::kNormal) &&
                !properties.Get<glm::vec3>(GS::PropertyNames::kNormal))
            {
                return false;
            }

            auto target = properties.GetOrAdd<glm::vec3>(
                std::string{GS::PropertyNames::kNormal},
                glm::vec3{0.0f, 0.0f, 1.0f});
            if (!target || target.Vector().size() != normals.size())
                return false;

            target.Vector() = normals;
            return true;
        }

        [[nodiscard]] std::string BuildMeshNormalsSuccessMessage(
            const SandboxEditorMeshVertexNormalsResult& result)
        {
            std::string message = "Mesh vertex normals recomputed (weighting=";
            message += std::string(GN::DebugName(result.Weighting));
            message += ", written=";
            message += std::to_string(result.WrittenCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackVertexCount);
            message += ").";
            return message;
        }

        [[nodiscard]] bool IsPositiveFinite(const double value) noexcept
        {
            return std::isfinite(value) && value > 0.0;
        }

        [[nodiscard]] SandboxEditorGraphVertexNormalsResult MakeGraphNormalsResult(
            const SandboxEditorCommandStatus status,
            const GraphNormals::RecomputeStatus normalStatus,
            const bool orientTowardFallback,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorGraphVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .OrientTowardFallback = orientTowardFallback,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyGraphNormalCounters(
            const GraphNormals::Diagnostics& source,
            SandboxEditorGraphVertexNormalsResult& target)
        {
            target.VertexSlotCount = source.VertexSlotCount;
            target.EdgeSlotCount = source.EdgeSlotCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalVertexCount = source.ValidNormalVertexCount;
            target.FallbackVertexCount = source.FallbackVertexCount;
            target.IsolatedVertexCount = source.IsolatedVertexCount;
            target.DegreeOneVertexCount = source.DegreeOneVertexCount;
            target.CollinearNeighborhoodCount =
                source.CollinearNeighborhoodCount;
            target.DuplicatePositionCount = source.DuplicatePositionCount;
            target.NonFinitePositionCount = source.NonFinitePositionCount;
            target.InvalidEdgeCount = source.InvalidEdgeCount;
            target.SkippedDeletedVertexCount =
                source.SkippedDeletedVertexCount;
            target.SkippedDeletedEdgeCount = source.SkippedDeletedEdgeCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] Core::ErrorCode ErrorForGraphNormalStatus(
            const GraphNormals::RecomputeStatus status) noexcept
        {
            using Status = GraphNormals::RecomputeStatus;
            switch (status)
            {
            case Status::Success:
                return Core::ErrorCode::Success;
            case Status::PropertyTypeConflict:
                return Core::ErrorCode::TypeMismatch;
            case Status::EmptyGraph:
            case Status::InvalidPositionProperty:
            case Status::InvalidTopologyProperty:
            case Status::InvalidOutputProperty:
            case Status::CountMismatch:
                return Core::ErrorCode::InvalidArgument;
            }
            return Core::ErrorCode::Unknown;
        }

        struct GraphForVertexNormalsResult
        {
            Geometry::Halfedges Halfedges{};
            std::size_t EdgeSlotCount{0};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] GraphForVertexNormalsResult
        BuildGraphConnectivityForVertexNormalRecompute(
            const GS::MutableSourceView& view)
        {
            GraphForVertexNormalsResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Graph ||
                view.NodeSource == nullptr ||
                view.EdgeSource == nullptr)
            {
                result.Status =
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Graph vertex normals require selected graph GeometrySources.";
                return result;
            }

            const auto edgeV0 =
                view.EdgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV0);
            const auto edgeV1 =
                view.EdgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV1);
            const std::size_t edgeSlotCount =
                view.EdgeSource->Properties.Size();
            if (!edgeV0 || !edgeV1 ||
                edgeV0.Vector().size() != edgeSlotCount ||
                edgeV1.Vector().size() != edgeSlotCount)
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected graph requires count-matched edge endpoint properties for normal recompute";
                return result;
            }

            result.Halfedges.Resize(edgeSlotCount * 2u);
            auto connectivity =
                result.Halfedges.GetOrAdd<Geometry::Graph::HalfedgeConnectivity>(
                    "h:connectivity",
                    {});
            if (!connectivity ||
                connectivity.Vector().size() != edgeSlotCount * 2u)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::Unknown;
                result.Diagnostic =
                    "graph normal recompute could not build halfedge connectivity scratch storage";
                return result;
            }

            for (std::size_t edgeIndex = 0u;
                 edgeIndex < edgeSlotCount;
                 ++edgeIndex)
            {
                const std::size_t h0 = edgeIndex * 2u;
                const std::size_t h1 = h0 + 1u;
                connectivity.Vector()[h0].Vertex =
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(
                            edgeV1.Vector()[edgeIndex])};
                connectivity.Vector()[h1].Vertex =
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(
                            edgeV0.Vector()[edgeIndex])};
            }

            result.EdgeSlotCount = edgeSlotCount;
            result.Status = SandboxEditorCommandStatus::Applied;
            return result;
        }

        [[nodiscard]] std::string BuildGraphNormalsSuccessMessage(
            const SandboxEditorGraphVertexNormalsResult& result)
        {
            std::string message = "Graph vertex normals recomputed (written=";
            message += std::to_string(result.WrittenCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackVertexCount);
            message += ", invalidEdges=";
            message += std::to_string(result.InvalidEdgeCount);
            message += ").";
            return message;
        }

        [[nodiscard]] SandboxEditorPointCloudVertexNormalsResult
        MakePointCloudNormalsResult(
            const SandboxEditorCommandStatus status,
            const PointNormals::RecomputeStatus normalStatus,
            const SandboxEditorPointCloudVertexNormalsCommand& command,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorPointCloudVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .Orientation = command.Orientation,
                .KNeighbors = command.KNeighbors,
                .MinimumNeighbors = command.MinimumNeighbors,
                .UseRadiusSearch = command.UseRadiusSearch,
                .Radius = command.Radius,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyPointCloudNormalCounters(
            const PointNormals::Diagnostics& source,
            SandboxEditorPointCloudVertexNormalsResult& target)
        {
            target.PointSlotCount = source.PointSlotCount;
            target.FinitePointCount = source.FinitePointCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalPointCount = source.ValidNormalPointCount;
            target.FallbackPointCount = source.FallbackPointCount;
            target.DegenerateNeighborhoodCount =
                source.DegenerateNeighborhoodCount;
            target.TooFewNeighborCount = source.TooFewNeighborCount;
            target.CollinearNeighborhoodCount =
                source.CollinearNeighborhoodCount;
            target.DuplicatePositionCount = source.DuplicatePositionCount;
            target.NonFinitePointCount = source.NonFinitePointCount;
            target.SkippedDeletedPointCount =
                source.SkippedDeletedPointCount;
            target.SpatialQueryFailureCount =
                source.SpatialQueryFailureCount;
            target.FlippedOrientationCount = source.FlippedOrientationCount;
            target.KNNVisitedNodeCount = source.KNNVisitedNodeCount;
            target.KNNDistanceEvaluationCount =
                source.KNNDistanceEvaluationCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] Core::ErrorCode ErrorForPointCloudNormalStatus(
            const PointNormals::RecomputeStatus status) noexcept
        {
            using Status = PointNormals::RecomputeStatus;
            switch (status)
            {
            case Status::Success:
                return Core::ErrorCode::Success;
            case Status::PropertyTypeConflict:
                return Core::ErrorCode::TypeMismatch;
            case Status::EmptyInput:
            case Status::TooFewFinitePoints:
            case Status::InvalidPositionProperty:
            case Status::InvalidOutputProperty:
            case Status::CountMismatch:
                return Core::ErrorCode::InvalidArgument;
            case Status::SpatialIndexBuildFailed:
            case Status::SpatialIndexQueryFailed:
                return Core::ErrorCode::Unknown;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] std::string BuildPointCloudNormalsSuccessMessage(
            const SandboxEditorPointCloudVertexNormalsResult& result)
        {
            std::string message =
                "Point-cloud vertex normals recomputed (backend=";
            message += std::string(PointNormals::DebugName(result.Backend));
            message += ", written=";
            message += std::to_string(result.WrittenCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackPointCount);
            message += ").";
            return message;
        }

        [[nodiscard]] Core::ErrorCode ErrorForDenoiseStatus(
            const Smooth::DenoiseStatus status) noexcept
        {
            switch (status)
            {
            case Smooth::DenoiseStatus::Success:
                return Core::ErrorCode::Success;
            case Smooth::DenoiseStatus::EmptyMesh:
                return Core::ErrorCode::ResourceNotFound;
            case Smooth::DenoiseStatus::NonManifoldInput:
            case Smooth::DenoiseStatus::DegenerateGeometry:
            case Smooth::DenoiseStatus::NonFiniteInput:
            case Smooth::DenoiseStatus::InvalidParams:
                return Core::ErrorCode::InvalidArgument;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] bool IsFiniteVec3(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool AllFiniteVec3(
            const std::span<const glm::vec3> values) noexcept
        {
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteVec3(value))
                    return false;
            }
            return true;
        }

        struct MeshDenoiseSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> BeforePositions{};
            std::vector<bool> DeletedVertices{};
            std::vector<std::uint32_t> SourceFaceForMeshFace{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshDenoiseSourceResult BuildHalfedgeMeshForDenoise(
            const GS::ConstSourceView& view)
        {
            MeshDenoiseSourceResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status =
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise requires selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise requires a non-empty v:position property.";
                return result;
            }

            result.BeforePositions = positions.Vector();
            result.DeletedVertices.assign(result.BeforePositions.size(), false);
            if (const auto deleted =
                    view.VertexSource->Properties.Get<bool>("v:deleted"))
            {
                if (deleted.Vector().size() != result.BeforePositions.size())
                {
                    result.Status =
                        SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "Mesh denoise requires v:deleted to match v:position when present.";
                    return result;
                }
                for (std::size_t i = 0u; i < deleted.Vector().size(); ++i)
                    result.DeletedVertices[i] = deleted.Vector()[i];
            }

            MeshSoupFromGeometrySourcesResult soup =
                BuildMeshSoupFromGeometrySources(view);
            if (!soup.Succeeded())
            {
                result.Status = soup.Status;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic = soup.Diagnostic.empty()
                    ? "Mesh denoise could not build a triangle soup from GeometrySources."
                    : soup.Diagnostic;
                return result;
            }

            auto converted =
                Geometry::Mesh::Conversion::ToHalfedgeMesh(soup.Mesh);
            if (!converted.Succeeded())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise could not convert selected GeometrySources to halfedge topology.";
                return result;
            }
            if (converted.Mesh.VerticesSize() != result.BeforePositions.size())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise conversion changed the vertex slot count.";
                return result;
            }
            if (converted.Mesh.FacesSize() !=
                soup.SourceFaceForSoupFace.size())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise conversion changed the face slot count.";
                return result;
            }

            result.Mesh = std::move(converted.Mesh);
            result.SourceFaceForMeshFace =
                std::move(soup.SourceFaceForSoupFace);
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] std::vector<glm::vec3> ExtractMeshPositions(
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            std::vector<glm::vec3> positions(mesh.VerticesSize());
            for (std::size_t i = 0u; i < positions.size(); ++i)
            {
                positions[i] = mesh.Position(
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(i)});
            }
            return positions;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshDenoisePositionState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const std::vector<glm::vec3>& positions)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            auto currentPositions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!currentPositions ||
                currentPositions.Vector().size() != positions.size() ||
                !AllFiniteVec3(std::span<const glm::vec3>{
                    positions.data(),
                    positions.size()}))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            currentPositions.Vector() = positions;
            Dirty::MarkVertexPositionsDirty(raw, *entity);
            Dirty::MarkVertexAttributesDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitMeshDenoisePositions(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            std::vector<glm::vec3> before,
            std::vector<glm::vec3> after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Denoise mesh vertices",
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyMeshDenoisePositionState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyMeshDenoisePositionState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyMeshDenoisePositionState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        void CopyMeshDenoiseCounters(
            const Smooth::BilateralDenoiseResult& source,
            SandboxEditorMeshDenoiseResult& target)
        {
            target.NormalIterations =
                static_cast<std::uint32_t>(
                    source.NormalIterationsPerformed);
            target.VertexIterations =
                static_cast<std::uint32_t>(
                    source.VertexIterationsPerformed);
            target.VertexSlotCount = source.VertexCount;
            target.MovedVertexCount = source.MovedVertexCount;
            target.ProcessedFaceCount = source.ProcessedFaceCount;
            target.DegenerateFaceCount = source.DegenerateFaceCount;
            target.NonFiniteFaceCount = source.NonFiniteFaceCount;
            target.SkippedDeletedFaceCount = source.SkippedDeletedFaceCount;
            target.PinnedBoundaryVertexCount =
                source.PinnedBoundaryVertexCount;
            target.SigmaSpatialUsed = source.SigmaSpatialUsed;
            target.SigmaRangeUsed = source.SigmaRangeUsed;
        }

        [[nodiscard]] std::string BuildMeshDenoiseSuccessMessage(
            const SandboxEditorMeshDenoiseResult& result)
        {
            std::string message = "Mesh denoise completed (written=";
            message += std::to_string(result.WrittenCount);
            message += ", moved=";
            message += std::to_string(result.MovedVertexCount);
            message += ", sigmaSpatial=";
            message += std::to_string(result.SigmaSpatialUsed);
            message += ", sigmaRange=";
            message += std::to_string(result.SigmaRangeUsed);
            message += ").";
            return message;
        }

        struct MeshCurvatureSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::size_t VertexSlotCount{0u};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshCurvatureSourceResult BuildHalfedgeMeshForCurvature(
            const GS::ConstSourceView& view)
        {
            MeshDenoiseSourceResult source = BuildHalfedgeMeshForDenoise(view);
            MeshCurvatureSourceResult result{};
            result.VertexSlotCount = source.BeforePositions.size();
            result.Status = source.Status;
            result.Error = source.Error;
            if (source.Succeeded())
            {
                result.Mesh = std::move(source.Mesh);
                result.Diagnostic.clear();
            }
            else if (source.Status ==
                     SandboxEditorCommandStatus::UnsupportedGeometryDomain)
            {
                result.Diagnostic =
                    "Mesh curvature requires selected mesh GeometrySources.";
            }
            else if (source.Status ==
                     SandboxEditorCommandStatus::InvalidProcessingParameters)
            {
                result.Diagnostic =
                    "Mesh curvature requires finite count-matched vertex positions and valid mesh topology.";
            }
            else
            {
                result.Diagnostic = source.Diagnostic.empty()
                    ? "Mesh curvature could not build a halfedge mesh from GeometrySources."
                    : source.Diagnostic;
            }
            return result;
        }

        struct MeshCurvaturePropertyState
        {
            bool HadMean{false};
            bool HadGaussian{false};
            bool HadDir1{false};
            bool HadDir2{false};
            std::vector<double> Mean{};
            std::vector<double> Gaussian{};
            std::vector<glm::vec3> Dir1{};
            std::vector<glm::vec3> Dir2{};
        };

        [[nodiscard]] bool SameVec3PropertyValues(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (lhs[i].x != rhs[i].x ||
                    lhs[i].y != rhs[i].y ||
                    lhs[i].z != rhs[i].z)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SameMeshCurvaturePropertyState(
            const MeshCurvaturePropertyState& lhs,
            const MeshCurvaturePropertyState& rhs) noexcept
        {
            return lhs.HadMean == rhs.HadMean &&
                   lhs.HadGaussian == rhs.HadGaussian &&
                   lhs.HadDir1 == rhs.HadDir1 &&
                   lhs.HadDir2 == rhs.HadDir2 &&
                   lhs.Mean == rhs.Mean &&
                   lhs.Gaussian == rhs.Gaussian &&
                   SameVec3PropertyValues(lhs.Dir1, rhs.Dir1) &&
                   SameVec3PropertyValues(lhs.Dir2, rhs.Dir2);
        }

        template <typename T>
        [[nodiscard]] bool CaptureCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const std::size_t expectedCount,
            bool& hadProperty,
            std::vector<T>& values,
            std::string& diagnostic)
        {
            hadProperty = false;
            values.clear();
            if (!properties.Exists(name))
                return true;

            auto property = properties.Get<T>(name);
            if (!property || property.Vector().size() != expectedCount)
            {
                diagnostic = "existing curvature property has an incompatible type or count: ";
                diagnostic += std::string{name};
                return false;
            }

            hadProperty = true;
            values = property.Vector();
            return true;
        }

        [[nodiscard]] bool CaptureMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const std::size_t expectedCount,
            MeshCurvaturePropertyState& out,
            std::string& diagnostic)
        {
            return CaptureCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kMeanCurvature,
                       expectedCount,
                       out.HadMean,
                       out.Mean,
                       diagnostic) &&
                   CaptureCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kGaussianCurvature,
                       expectedCount,
                       out.HadGaussian,
                       out.Gaussian,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir1,
                       expectedCount,
                       out.HadDir1,
                       out.Dir1,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir2,
                       expectedCount,
                       out.HadDir2,
                       out.Dir2,
                       diagnostic);
        }

        template <typename T>
        [[nodiscard]] bool ApplyCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const bool hasProperty,
            const std::vector<T>& values,
            const T& defaultValue)
        {
            if (!hasProperty)
            {
                auto property = properties.Get<T>(name);
                if (property)
                {
                    properties.Remove(property);
                    return true;
                }
                return !properties.Exists(name);
            }

            auto property =
                properties.GetOrAdd<T>(std::string{name}, defaultValue);
            if (!property || property.Vector().size() != values.size())
                return false;
            property.Vector() = values;
            return true;
        }

        [[nodiscard]] bool ApplyMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const MeshCurvaturePropertyState& state)
        {
            return ApplyCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kMeanCurvature,
                       state.HadMean,
                       state.Mean,
                       0.0) &&
                   ApplyCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kGaussianCurvature,
                       state.HadGaussian,
                       state.Gaussian,
                       0.0) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir1,
                       state.HadDir1,
                       state.Dir1,
                       glm::vec3{0.0f}) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir2,
                       state.HadDir2,
                       state.Dir2,
                       glm::vec3{0.0f});
        }

        [[nodiscard]] std::size_t CountNonFiniteScalars(
            const std::span<const double> values) noexcept
        {
            std::size_t count = 0u;
            for (const double value : values)
            {
                if (!std::isfinite(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] std::size_t CountNonFiniteVectors(
            const std::span<const glm::vec3> values) noexcept
        {
            std::size_t count = 0u;
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteVec3(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] bool CurvatureOutputRequestsDirections(
            const SandboxEditorMeshCurvatureOutput output) noexcept
        {
            return output == SandboxEditorMeshCurvatureOutput::All ||
                   output == SandboxEditorMeshCurvatureOutput::PrincipalDirections;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshCurvatureState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const MeshCurvaturePropertyState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            if (!ApplyMeshCurvaturePropertyState(
                    view.VertexSource->Properties,
                    state))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            Dirty::MarkVertexAttributesDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitMeshCurvatureProperties(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            MeshCurvaturePropertyState before,
            MeshCurvaturePropertyState after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Compute mesh curvature",
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyMeshCurvatureState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyMeshCurvatureState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyMeshCurvatureState(context.Scene, stableEntityId, after));
        }

        [[nodiscard]] std::string BuildMeshCurvatureSuccessMessage(
            const SandboxEditorMeshCurvatureResult& result)
        {
            std::string message = "Mesh curvature computed (vertices=";
            message += std::to_string(result.VertexSlotCount);
            message += ", scalars=";
            message += std::to_string(result.ScalarWrittenCount);
            message += ", directions=";
            message += result.DirectionsPublished ? "published" : "not published";
            message += ").";
            if (result.DirectionsRequested && !result.DirectionsPublished)
                message += " Principal directions were not published for this run.";
            return message;
        }

        [[nodiscard]] bool ValidMeshRemeshMode(
            const SandboxEditorMeshRemeshMode mode) noexcept
        {
            return std::find(kMeshRemeshModes.begin(),
                             kMeshRemeshModes.end(),
                             mode) != kMeshRemeshModes.end();
        }

        [[nodiscard]] bool ValidMeshRemeshSizingLaw(
            const SandboxEditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            return std::find(kMeshRemeshSizingLaws.begin(),
                             kMeshRemeshSizingLaws.end(),
                             sizingLaw) != kMeshRemeshSizingLaws.end();
        }

        [[nodiscard]] bool ValidMeshSubdivideOperator(
            const SandboxEditorMeshSubdivideOperator op) noexcept
        {
            return std::find(kMeshSubdivideOperators.begin(),
                             kMeshSubdivideOperators.end(),
                             op) != kMeshSubdivideOperators.end();
        }

        [[nodiscard]] bool ValidMeshSimplifyMetric(
            const SandboxEditorMeshSimplifyMetric metric) noexcept
        {
            return std::find(kMeshSimplifyMetrics.begin(),
                             kMeshSimplifyMetrics.end(),
                             metric) != kMeshSimplifyMetrics.end();
        }

        [[nodiscard]] bool ValidSandboxEditorICPVariant(
            const SandboxEditorICPVariant variant) noexcept
        {
            return std::find(kSandboxEditorICPVariants.begin(),
                             kSandboxEditorICPVariants.end(),
                             variant) != kSandboxEditorICPVariants.end();
        }

        [[nodiscard]] Reg::ICPVariant ToGeometryICPVariant(
            const SandboxEditorICPVariant variant) noexcept
        {
            return variant == SandboxEditorICPVariant::PointToPlane
                       ? Reg::ICPVariant::PointToPlane
                       : Reg::ICPVariant::PointToPoint;
        }

        struct MeshTopologySourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshTopologySourceResult BuildHalfedgeMeshForTopologyEdit(
            const GS::ConstSourceView& view,
            std::string_view operationName)
        {
            MeshDenoiseSourceResult source = BuildHalfedgeMeshForDenoise(view);
            MeshTopologySourceResult result{};
            result.Status = source.Status;
            result.Error = source.Error;
            if (source.Succeeded())
            {
                result.Mesh = std::move(source.Mesh);
                return result;
            }

            result.Diagnostic = std::string{operationName};
            if (source.Status ==
                SandboxEditorCommandStatus::UnsupportedGeometryDomain)
            {
                result.Diagnostic +=
                    " requires selected mesh GeometrySources.";
            }
            else if (source.Status ==
                     SandboxEditorCommandStatus::InvalidProcessingParameters)
            {
                result.Diagnostic +=
                    " requires a non-empty finite mesh with valid topology.";
            }
            else
            {
                result.Diagnostic +=
                    " could not build a halfedge mesh from GeometrySources.";
            }
            if (!source.Diagnostic.empty())
            {
                result.Diagnostic += " ";
                result.Diagnostic += source.Diagnostic;
            }
            return result;
        }

        void MarkMeshTopologyReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkEdgeTopologyDirty(raw, entity);
            Dirty::MarkFaceTopologyDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshTopologyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            Geometry::HalfedgeMesh::Mesh published = mesh;
            if (published.HasGarbage())
                published.GarbageCollection();
            GS::PopulateFromMesh(raw, *entity, published);
            MarkMeshTopologyReplacementDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitMeshTopologyReplacement(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            Geometry::HalfedgeMesh::Mesh before,
            Geometry::HalfedgeMesh::Mesh after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = label,
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyMeshTopologyState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyMeshTopologyState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyMeshTopologyState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyUvMeshTopologyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            Geometry::HalfedgeMesh::Mesh published = mesh;
            if (published.HasGarbage())
                published.GarbageCollection();
            GS::PopulateFromMesh(raw, *entity, published);
            MarkMeshTopologyReplacementDirty(raw, *entity);
            Dirty::MarkGpuDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitUvMeshTopologyReplacement(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            Geometry::HalfedgeMesh::Mesh before,
            Geometry::HalfedgeMesh::Mesh after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = label,
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyUvMeshTopologyState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyUvMeshTopologyState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyUvMeshTopologyState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        // UI-027: outlier removal changes the point count, so the published
        // point GeometrySources must be fully rebuilt (mirrors the mesh
        // topology-replacement path but for point clouds). A full re-upload is
        // requested because the count changed.
        void MarkPointCloudReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            Dirty::MarkGpuDirty(raw, entity);
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkVertexNormalsDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyPointCloudPointState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::PointCloud::Cloud& cloud)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            // PopulateFromCloud takes a mutable cloud (it may garbage-collect);
            // operate on an owned copy so the captured undo/redo state is not
            // mutated.
            Geometry::PointCloud::Cloud published = cloud;
            GS::PopulateFromCloud(raw, *entity, published);
            MarkPointCloudReplacementDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] bool MirrorGeometrySourcePositionsToPointCloudStorage(
            Geometry::PropertySet& properties)
        {
            const auto positions =
                properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().size() != properties.Size())
                return false;

            // GeometrySources uses `v:position`; Geometry.PointCloud::Cloud
            // uses `v:point`. Seed the cloud slot before borrowing the property
            // set so point-cloud algorithms see the promoted source positions.
            auto points = properties.GetOrAdd<glm::vec3>(
                "v:point",
                glm::vec3{0.0f});
            if (!points)
                return false;
            points.Vector() = positions.Vector();
            return true;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitPointCloudReplacement(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            Geometry::PointCloud::Cloud before,
            Geometry::PointCloud::Cloud after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = label,
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyPointCloudPointState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyPointCloudPointState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyPointCloudPointState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        [[nodiscard]] const char* DebugNameForOutlierRemovalStatus(
            const Geometry::PointCloud::OutlierRemovalStatus status) noexcept
        {
            using Status = Geometry::PointCloud::OutlierRemovalStatus;
            switch (status)
            {
            case Status::Success:
                return "Success";
            case Status::EmptyInput:
                return "EmptyInput";
            case Status::InsufficientPoints:
                return "InsufficientPoints";
            case Status::InvalidParameters:
                return "InvalidParameters";
            case Status::BuildFailed:
                return "BuildFailed";
            }
            return "Unknown";
        }

        [[nodiscard]] AdaptiveRemesh::SizingLaw ToAdaptiveSizingLaw(
            const SandboxEditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            switch (sizingLaw)
            {
            case SandboxEditorMeshRemeshSizingLaw::MeanCurvature:
                return AdaptiveRemesh::SizingLaw::MeanCurvature;
            case SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
                return AdaptiveRemesh::SizingLaw::ErrorBoundedTaubin;
            }
            return AdaptiveRemesh::SizingLaw::MeanCurvature;
        }

        void CopyRemeshCounters(
            const Geometry::RemeshingOperationResult& source,
            SandboxEditorMeshRemeshResult& target)
        {
            target.IterationsPerformed =
                static_cast<std::uint32_t>(source.IterationsPerformed);
            target.OutputVertexCount = source.FinalVertexCount;
            target.OutputFaceCount = source.FinalFaceCount;
            target.SplitCount = source.SplitCount;
            target.CollapseCount = source.CollapseCount;
            target.FlipCount = source.FlipCount;
        }

        [[nodiscard]] std::string BuildMeshRemeshSuccessMessage(
            const SandboxEditorMeshRemeshResult& result)
        {
            std::string message = "Mesh remesh completed (mode=";
            message += DebugNameForSandboxEditorMeshRemeshMode(result.Mode);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ").";
            return message;
        }

        [[nodiscard]] std::string BuildMeshSubdivideSuccessMessage(
            const SandboxEditorMeshSubdivideResult& result)
        {
            std::string message = "Mesh subdivide completed (operator=";
            message += DebugNameForSandboxEditorMeshSubdivideOperator(
                result.Operator);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ").";
            return message;
        }

        [[nodiscard]] std::string BuildMeshSimplifySuccessMessage(
            const SandboxEditorMeshSimplifyResult& result)
        {
            std::string message = "Mesh simplify completed (metric=";
            message += DebugNameForSandboxEditorMeshSimplifyMetric(
                result.Metric);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", collapses=";
            message += std::to_string(result.CollapseCount);
            message += ").";
            return message;
        }

        [[nodiscard]] SandboxEditorMeshCurvatureResult
        MakeMeshCurvatureBaseResult(
            const SandboxEditorMeshCurvatureCommand& command,
            const bool directionsAvailable)
        {
            return SandboxEditorMeshCurvatureResult{
                .Status = SandboxEditorCommandStatus::NoChange,
                .Output = command.Output,
                .DirectionsRequested =
                    command.PublishPrincipalDirections &&
                    CurvatureOutputRequestsDirections(command.Output),
                .DirectionsAvailable = directionsAvailable,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] SandboxEditorMeshDenoiseResult MakeMeshDenoiseBaseResult(
            const SandboxEditorMeshDenoiseCommand& command)
        {
            return SandboxEditorMeshDenoiseResult{
                .Status = SandboxEditorCommandStatus::NoChange,
                .DenoiseStatus = Smooth::DenoiseStatus::Success,
                .Stage = command.Stage,
                .NormalIterations = command.NormalIterations,
                .VertexIterations = command.VertexIterations,
                .SigmaSpatial = command.SigmaSpatial,
                .SigmaRange = command.SigmaRange,
                .PreserveBoundary = command.PreserveBoundary,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] SandboxEditorMeshRemeshResult MakeMeshRemeshBaseResult(
            const SandboxEditorMeshRemeshCommand& command)
        {
            return SandboxEditorMeshRemeshResult{
                .Status = SandboxEditorCommandStatus::NoChange,
                .Mode = command.Mode,
                .SizingLaw = command.SizingLaw,
                .IterationsRequested = command.Iterations,
                .TargetEdgeLength = command.TargetEdgeLength,
                .ProjectToSurface = command.ProjectToSurface,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] SandboxEditorMeshSubdivideResult
        MakeMeshSubdivideBaseResult(
            const SandboxEditorMeshSubdivideCommand& command)
        {
            return SandboxEditorMeshSubdivideResult{
                .Status = SandboxEditorCommandStatus::NoChange,
                .Operator = command.Operator,
                .IterationsRequested = command.Iterations,
                .PreserveLoopFeatureEdges = command.PreserveLoopFeatureEdges,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] SandboxEditorMeshSimplifyResult MakeMeshSimplifyBaseResult(
            const SandboxEditorMeshSimplifyCommand& command)
        {
            return SandboxEditorMeshSimplifyResult{
                .Status = SandboxEditorCommandStatus::NoChange,
                .Metric = command.Metric,
                .TargetFaces = command.TargetFaces,
                .MaxError = command.MaxError,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] SandboxEditorMeshCurvatureResult
        MakePendingMeshCurvatureResult(
            const SandboxEditorMeshCurvatureCommand& command,
            const bool directionsAvailable,
            const std::size_t vertexSlotCount,
            const DerivedJobHandle handle)
        {
            SandboxEditorMeshCurvatureResult result =
                MakeMeshCurvatureBaseResult(command, directionsAvailable);
            result.Status = SandboxEditorCommandStatus::Pending;
            result.VertexSlotCount = vertexSlotCount;
            result.Message = "Mesh curvature CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorMeshDenoiseResult MakePendingMeshDenoiseResult(
            const SandboxEditorMeshDenoiseCommand& command,
            const MeshDenoiseSourceResult& source,
            const DerivedJobHandle handle)
        {
            SandboxEditorMeshDenoiseResult result =
                MakeMeshDenoiseBaseResult(command);
            result.Status = SandboxEditorCommandStatus::Pending;
            result.VertexSlotCount = source.BeforePositions.size();
            result.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(source.DeletedVertices.begin(),
                               source.DeletedVertices.end(),
                               true));
            result.WrittenCount =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;
            result.Message = "Mesh denoise CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorMeshRemeshResult MakePendingMeshRemeshResult(
            const SandboxEditorMeshRemeshCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const DerivedJobHandle handle)
        {
            SandboxEditorMeshRemeshResult result =
                MakeMeshRemeshBaseResult(command);
            result.Status = SandboxEditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh remesh CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorMeshSubdivideResult
        MakePendingMeshSubdivideResult(
            const SandboxEditorMeshSubdivideCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const DerivedJobHandle handle)
        {
            SandboxEditorMeshSubdivideResult result =
                MakeMeshSubdivideBaseResult(command);
            result.Status = SandboxEditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh subdivide CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorMeshSimplifyResult
        MakePendingMeshSimplifyResult(
            const SandboxEditorMeshSimplifyCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const DerivedJobHandle handle)
        {
            SandboxEditorMeshSimplifyResult result =
                MakeMeshSimplifyBaseResult(command);
            result.Status = SandboxEditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh simplify CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] Core::ErrorCode ResultErrorOrUnknown(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : error;
        }

        [[nodiscard]] SandboxEditorPointCloudOutlierRemovalResult
        MakePointCloudOutlierRemovalBaseResult(
            const SandboxEditorPointCloudOutlierRemovalCommand& command)
        {
            return SandboxEditorPointCloudOutlierRemovalResult{
                .Status = SandboxEditorCommandStatus::NoChange,
                .Method = command.Method,
                .GeometryStatus =
                    Geometry::PointCloud::OutlierRemovalStatus::Success,
                .Error = Core::ErrorCode::Success,
            };
        }

        void CopyPointCloudOutlierRemovalCounters(
            const Geometry::PointCloud::OutlierRemovalResult& source,
            SandboxEditorPointCloudOutlierRemovalResult& target)
        {
            target.GeometryStatus = source.Status;
            target.OriginalCount = source.OriginalCount;
            target.KeptCount = source.KeptCount;
            target.RejectedCount = source.RejectedCount;
            target.NonFiniteCount = source.NonFiniteCount;
            target.MeanDistance = source.MeanDistance;
            target.StdDevDistance = source.StdDevDistance;
            target.DistanceThreshold = source.DistanceThreshold;
        }

        [[nodiscard]] std::string BuildPointCloudOutlierRemovalSuccessMessage(
            const SandboxEditorPointCloudOutlierRemovalResult& result)
        {
            std::string message = "Removed ";
            message += std::to_string(result.RejectedCount);
            message += " of ";
            message += std::to_string(result.OriginalCount);
            message += " points (kept ";
            message += std::to_string(result.KeptCount);
            if (result.NonFiniteCount > 0u)
            {
                message += ", non-finite ";
                message += std::to_string(result.NonFiniteCount);
            }
            message += ").";
            return message;
        }

        [[nodiscard]] SandboxEditorPointCloudOutlierRemovalResult
        MakePendingPointCloudOutlierRemovalResult(
            const SandboxEditorPointCloudOutlierRemovalCommand& command,
            const std::size_t livePointCount,
            const DerivedJobHandle handle)
        {
            SandboxEditorPointCloudOutlierRemovalResult result =
                MakePointCloudOutlierRemovalBaseResult(command);
            result.Status = SandboxEditorCommandStatus::Pending;
            result.OriginalCount = livePointCount;
            result.KeptCount = livePointCount;
            result.Message = "Point-cloud outlier-removal CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        struct SandboxEditorPointCloudOutlierRemovalCpuJobState
        {
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            std::vector<glm::vec3> SnapshotPositions{};
            Geometry::PointCloud::Cloud BeforeCloud{};
            Geometry::PointCloud::Cloud WorkCloud{};
            Geometry::PointCloud::Cloud AfterCloud{};
            SandboxEditorPointCloudOutlierRemovalCommand Command{};
            SandboxEditorPointCloudOutlierRemovalResult Result{};
        };

        [[nodiscard]] DerivedJobApplyValidation
        ValidatePointCloudOutlierRemovalCpuJobApply(
            const SandboxEditorContext& context,
            const SandboxEditorPointCloudOutlierRemovalCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
                return DerivedJobApplyValidation::MissingEntity;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
                view.VertexSource == nullptr)
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                job.GeometryMetadataSignature)
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            std::optional<std::vector<glm::vec3>> current =
                CollectFiniteGeometryPositions(view.VertexSource->Properties);
            if (!current.has_value() ||
                !SameGeometryPositions(*current, job.SnapshotPositions))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            return DerivedJobApplyValidation::Current;
        }

        void PublishPointCloudOutlierRemovalResultSink(
            const SandboxEditorContext& context,
            SandboxEditorPointCloudOutlierRemovalResult result)
        {
            if (context.MethodResultSinks.PointCloudOutlierRemoval)
            {
                context.MethodResultSinks.PointCloudOutlierRemoval(
                    std::move(result));
            }
        }

        [[nodiscard]] DerivedJobWorkerResult
        RunPointCloudOutlierRemovalCpuWorker(
            const std::shared_ptr<
                SandboxEditorPointCloudOutlierRemovalCpuJobState>& state)
        {
            const bool statistical =
                state->Command.Method ==
                SandboxEditorPointCloudOutlierMethod::Statistical;
            Geometry::PointCloud::OutlierRemovalResult removal{};
            if (statistical)
            {
                Geometry::PointCloud::StatisticalOutlierRemovalParams params{};
                params.KNeighbors = state->Command.KNeighbors;
                params.StdDevMultiplier = state->Command.StdDevMultiplier;
                removal =
                    Geometry::PointCloud::RemoveStatisticalOutliers(
                        state->WorkCloud,
                        params);
            }
            else
            {
                Geometry::PointCloud::RadiusOutlierRemovalParams params{};
                params.SearchRadius = state->Command.SearchRadius;
                params.MinNeighbors = state->Command.MinNeighbors;
                removal =
                    Geometry::PointCloud::RemoveRadiusOutliers(
                        state->WorkCloud,
                        params);
            }

            SandboxEditorPointCloudOutlierRemovalResult& result =
                state->Result;
            CopyPointCloudOutlierRemovalCounters(removal, result);
            if (removal.Status !=
                Geometry::PointCloud::OutlierRemovalStatus::Success)
            {
                result.Status =
                    removal.Status ==
                            Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters
                        ? SandboxEditorCommandStatus::InvalidProcessingParameters
                        : SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.PointCloud outlier removal failed with ";
                result.Message +=
                    DebugNameForOutlierRemovalStatus(removal.Status);
                result.Message += ".";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            state->AfterCloud = state->WorkCloud;
            for (const std::size_t rejected : removal.RejectedIndices)
            {
                state->AfterCloud.DeletePoint(
                    Geometry::VertexHandle{
                        static_cast<std::uint32_t>(rejected)});
            }
            state->AfterCloud.GarbageCollection();
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Point-cloud outlier-removal CPU result ready",
            };
        }

        [[nodiscard]] Core::Result PublishPointCloudOutlierRemovalCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorPointCloudOutlierRemovalCpuJobState& job)
        {
            SandboxEditorPointCloudOutlierRemovalResult result = job.Result;
            if (!result.Succeeded())
            {
                PublishPointCloudOutlierRemovalResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            const bool statistical =
                job.Command.Method ==
                SandboxEditorPointCloudOutlierMethod::Statistical;
            const SandboxEditorCommandStatus commitStatus =
                CommitPointCloudReplacement(
                    context,
                    job.StableEntityId,
                    statistical
                        ? "Remove statistical point-cloud outliers"
                        : "Remove radius point-cloud outliers",
                    job.BeforeCloud,
                    job.AfterCloud);
            if (commitStatus != SandboxEditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Point-cloud outlier-removal publication failed during editor history commit.";
                PublishPointCloudOutlierRemovalResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message =
                BuildPointCloudOutlierRemovalSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishPointCloudOutlierRemovalResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] DerivedJobDesc MakePointCloudOutlierRemovalCpuJobDesc(
            const SandboxEditorContext& context,
            const std::shared_ptr<
                SandboxEditorPointCloudOutlierRemovalCpuJobState>& state)
        {
            return DerivedJobDesc{
                .Key = DerivedJobKey{
                    .EntityId = state->StableEntityId,
                    .Domain = ProgressiveGeometryDomain::Point,
                    .OutputSemantic = ProgressiveSlotSemantic::Displacement,
                    .SourcePropertyGeneration =
                        state->GeometryMetadataSignature,
                    .OutputName = "point_cloud_outlier_removal",
                },
                .Name = "Sandbox.PointCloudOutlierRemoval.CPU",
                .RequestedJobDomain = ProgressiveJobDomain::Cpu,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (state->WorkCloud.VertexCount() + 1023u) / 1024u)),
                .Scope = context.World,
                .Execute =
                    [state]() -> DerivedJobWorkerResult
                    {
                        return RunPointCloudOutlierRemovalCpuWorker(state);
                    },
                .ValidateOnMainThread =
                    [context, state]()
                    {
                        return ValidatePointCloudOutlierRemovalCpuJobApply(
                            context,
                            *state);
                    },
                .ApplyOnMainThread =
                    [context, state](DerivedJobApplyContext&) -> Core::Result
                    {
                        return PublishPointCloudOutlierRemovalCpuJob(
                            context,
                            *state);
                    },
            };
        }

        [[nodiscard]] SandboxEditorPointCloudOutlierRemovalResult
        SubmitPointCloudOutlierRemovalCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorPointCloudOutlierRemovalCommand& command,
            Geometry::PointCloud::Cloud beforeCloud,
            Geometry::PointCloud::Cloud workCloud,
            std::vector<glm::vec3> snapshotPositions,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<
                SandboxEditorPointCloudOutlierRemovalCpuJobState>();
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(snapshotPositions);
            state->BeforeCloud = std::move(beforeCloud);
            state->WorkCloud = std::move(workCloud);
            state->Command = command;
            state->Result = MakePointCloudOutlierRemovalBaseResult(command);
            state->Result.OriginalCount = state->WorkCloud.VertexCount();
            state->Result.KeptCount = state->WorkCloud.VertexCount();

            DerivedJobDesc desc =
                MakePointCloudOutlierRemovalCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorPointCloudOutlierRemovalResult pending =
                    MakePendingPointCloudOutlierRemovalResult(
                        command,
                        state->WorkCloud.VertexCount(),
                        active->Handle);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Point-cloud outlier-removal CPU",
                    *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                SandboxEditorPointCloudOutlierRemovalResult result =
                    MakePointCloudOutlierRemovalBaseResult(command);
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.GeometryStatus =
                    Geometry::PointCloud::OutlierRemovalStatus::BuildFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Point-cloud outlier-removal CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingPointCloudOutlierRemovalResult(
                command,
                state->WorkCloud.VertexCount(),
                handle);
        }

        enum class SandboxEditorVertexNormalsCpuJobKind : std::uint8_t
        {
            Mesh,
            Graph,
            PointCloud,
        };

        [[nodiscard]] const char* VertexNormalsCpuJobName(
            const SandboxEditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
                return "Sandbox.MeshVertexNormals.CPU";
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return "Sandbox.GraphVertexNormals.CPU";
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return "Sandbox.PointCloudVertexNormals.CPU";
            }
            return "Sandbox.VertexNormals.CPU";
        }

        [[nodiscard]] const char* VertexNormalsCpuJobOutputName(
            const SandboxEditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
                return "mesh_vertex_normals";
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return "graph_vertex_normals";
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return "point_cloud_vertex_normals";
            }
            return "vertex_normals";
        }

        [[nodiscard]] ProgressiveGeometryDomain VertexNormalsCpuJobDomain(
            const SandboxEditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
                return ProgressiveGeometryDomain::MeshVertex;
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return ProgressiveGeometryDomain::GraphVertex;
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return ProgressiveGeometryDomain::Point;
            }
            return ProgressiveGeometryDomain::Unknown;
        }

        [[nodiscard]] GS::Domain VertexNormalsExpectedSourceDomain(
            const SandboxEditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
                return GS::Domain::Mesh;
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return GS::Domain::Graph;
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return GS::Domain::PointCloud;
            }
            return GS::Domain::None;
        }

        [[nodiscard]] SandboxEditorMeshVertexNormalsResult
        MakePendingMeshVertexNormalsResult(
            const SandboxEditorMeshVertexNormalsCommand& command,
            const std::size_t vertexSlotCount,
            const DerivedJobHandle handle)
        {
            SandboxEditorMeshVertexNormalsResult result =
                MakeMeshNormalsResult(
                    SandboxEditorCommandStatus::Pending,
                    GN::RecomputeStatus::Success,
                    command.Weighting,
                    Core::ErrorCode::Success,
                    "Mesh vertex-normal CPU job queued");
            result.VertexSlotCount = vertexSlotCount;
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorGraphVertexNormalsResult
        MakePendingGraphVertexNormalsResult(
            const SandboxEditorGraphVertexNormalsCommand& command,
            const std::size_t vertexSlotCount,
            const std::size_t edgeSlotCount,
            const DerivedJobHandle handle)
        {
            SandboxEditorGraphVertexNormalsResult result =
                MakeGraphNormalsResult(
                    SandboxEditorCommandStatus::Pending,
                    GraphNormals::RecomputeStatus::Success,
                    command.OrientTowardFallback,
                    Core::ErrorCode::Success,
                    "Graph vertex-normal CPU job queued");
            result.VertexSlotCount = vertexSlotCount;
            result.EdgeSlotCount = edgeSlotCount;
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorPointCloudVertexNormalsResult
        MakePendingPointCloudVertexNormalsResult(
            const SandboxEditorPointCloudVertexNormalsCommand& command,
            const std::size_t pointSlotCount,
            const DerivedJobHandle handle)
        {
            SandboxEditorPointCloudVertexNormalsResult result =
                MakePointCloudNormalsResult(
                    SandboxEditorCommandStatus::Pending,
                    PointNormals::RecomputeStatus::Success,
                    command,
                    Core::ErrorCode::Success,
                    "Point-cloud vertex-normal CPU job queued");
            result.PointSlotCount = pointSlotCount;
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        struct SandboxEditorVertexNormalsCpuJobState
        {
            SandboxEditorVertexNormalsCpuJobKind Kind{
                SandboxEditorVertexNormalsCpuJobKind::Mesh};
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            std::vector<glm::vec3> SnapshotPositions{};
            std::vector<glm::vec3> Normals{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            Geometry::Vertices GraphNodes{};
            Geometry::PropertySet GraphEdges{};
            Geometry::Halfedges GraphHalfedges{};
            std::size_t GraphEdgeSlotCount{0u};
            Geometry::Vertices PointCloudPoints{};
            SandboxEditorMeshVertexNormalsCommand MeshCommand{};
            SandboxEditorGraphVertexNormalsCommand GraphCommand{};
            SandboxEditorPointCloudVertexNormalsCommand PointCloudCommand{};
            SandboxEditorMeshVertexNormalsResult MeshResult{};
            SandboxEditorGraphVertexNormalsResult GraphResult{};
            SandboxEditorPointCloudVertexNormalsResult PointCloudResult{};
        };

        [[nodiscard]] const Geometry::PropertySet*
        VertexNormalsSourceProperties(
            const GS::ConstSourceView& view,
            const SandboxEditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return view.NodeSource != nullptr
                    ? &view.NodeSource->Properties
                    : nullptr;
            }
            return nullptr;
        }

        [[nodiscard]] Geometry::PropertySet* VertexNormalsTargetProperties(
            GS::MutableSourceView& view,
            const SandboxEditorVertexNormalsCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return view.NodeSource != nullptr
                    ? &view.NodeSource->Properties
                    : nullptr;
            }
            return nullptr;
        }

        [[nodiscard]] DerivedJobApplyValidation ValidateVertexNormalsCpuJobApply(
            const SandboxEditorContext& context,
            const SandboxEditorVertexNormalsCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
                return DerivedJobApplyValidation::MissingEntity;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain !=
                VertexNormalsExpectedSourceDomain(job.Kind))
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                job.GeometryMetadataSignature)
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            const Geometry::PropertySet* properties =
                VertexNormalsSourceProperties(view, job.Kind);
            if (properties == nullptr)
                return DerivedJobApplyValidation::StaleGeometryGeneration;

            std::optional<std::vector<glm::vec3>> current =
                CollectFiniteGeometryPositions(*properties);
            if (!current.has_value() ||
                !SameGeometryPositions(*current, job.SnapshotPositions))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            return DerivedJobApplyValidation::Current;
        }

        void PublishMeshVertexNormalsResultSink(
            const SandboxEditorContext& context,
            SandboxEditorMeshVertexNormalsResult result)
        {
            if (context.MethodResultSinks.MeshVertexNormals)
                context.MethodResultSinks.MeshVertexNormals(std::move(result));
        }

        void PublishGraphVertexNormalsResultSink(
            const SandboxEditorContext& context,
            SandboxEditorGraphVertexNormalsResult result)
        {
            if (context.MethodResultSinks.GraphVertexNormals)
                context.MethodResultSinks.GraphVertexNormals(std::move(result));
        }

        void PublishPointCloudVertexNormalsResultSink(
            const SandboxEditorContext& context,
            SandboxEditorPointCloudVertexNormalsResult result)
        {
            if (context.MethodResultSinks.PointCloudVertexNormals)
            {
                context.MethodResultSinks.PointCloudVertexNormals(
                    std::move(result));
            }
        }

        [[nodiscard]] DerivedJobWorkerResult RunMeshVertexNormalsCpuWorker(
            const std::shared_ptr<SandboxEditorVertexNormalsCpuJobState>& state)
        {
            GN::Params params{};
            params.Weighting = state->MeshCommand.Weighting;
            params.OutputProperty = GN::kDefaultOutputProperty;
            params.FallbackNormal = state->MeshCommand.FallbackNormal;
            params.DegenerateNormalLengthEpsilon =
                state->MeshCommand.DegenerateNormalLengthEpsilon;
            params.SkipDeleted = true;

            const GN::Result normalResult = GN::Recompute(state->Mesh, params);
            SandboxEditorMeshVertexNormalsResult& result = state->MeshResult;
            result.Status = normalResult.Status == GN::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = normalResult.Status == GN::RecomputeStatus::Success
                ? Core::ErrorCode::Success
                : Core::ErrorCode::Unknown;
            CopyMeshNormalCounters(normalResult, result);

            if (normalResult.Status != GN::RecomputeStatus::Success)
            {
                result.Message =
                    "Geometry.HalfedgeMesh.Vertices.Normals failed with ";
                result.Message += std::string(GN::DebugName(normalResult.Status));
                result.Message += ".";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            if (!normalResult.Normals.IsValid() ||
                normalResult.Normals.Vector().size() !=
                    state->SnapshotPositions.size())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus = GN::RecomputeStatus::InvalidOutputProperty;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.HalfedgeMesh.Vertices.Normals produced missing or count-mismatched normals.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            state->Normals = normalResult.Normals.Vector();
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Mesh vertex-normal CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult RunGraphVertexNormalsCpuWorker(
            const std::shared_ptr<SandboxEditorVertexNormalsCpuJobState>& state)
        {
            GraphNormals::Params params{};
            params.PositionProperty = GS::PropertyNames::kPosition;
            params.OutputProperty = GraphNormals::kDefaultOutputProperty;
            params.FallbackNormal = state->GraphCommand.FallbackNormal;
            params.DegenerateNormalLengthEpsilon =
                state->GraphCommand.DegenerateNormalLengthEpsilon;
            params.CollinearEigenvalueRatioEpsilon =
                state->GraphCommand.CollinearEigenvalueRatioEpsilon;
            params.SkipDeleted = true;
            params.OrientTowardFallback =
                state->GraphCommand.OrientTowardFallback;

            const GraphNormals::PropertySetResult normalResult =
                GraphNormals::Recompute(
                    state->GraphNodes,
                    Geometry::ConstPropertySet(state->GraphNodes)
                        .Get<glm::vec3>(GS::PropertyNames::kPosition),
                    Geometry::ConstPropertySet(state->GraphHalfedges)
                        .Get<Geometry::Graph::HalfedgeConnectivity>(
                            "h:connectivity"),
                    state->GraphEdgeSlotCount,
                    params,
                    Geometry::ConstPropertySet(state->GraphNodes)
                        .Get<bool>("v:deleted"),
                    Geometry::ConstPropertySet(state->GraphEdges)
                        .Get<bool>("e:deleted"));

            SandboxEditorGraphVertexNormalsResult& result =
                state->GraphResult;
            result.Status = normalResult.Status ==
                    GraphNormals::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus = normalResult.Status;
            result.OrientTowardFallback =
                state->GraphCommand.OrientTowardFallback;
            result.Error = ErrorForGraphNormalStatus(normalResult.Status);
            CopyGraphNormalCounters(normalResult.Diagnostics, result);

            if (normalResult.Status != GraphNormals::RecomputeStatus::Success)
            {
                result.Message = "Geometry.Graph.Vertex.Normals failed with ";
                result.Message +=
                    std::string(GraphNormals::DebugName(normalResult.Status));
                result.Message += ".";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            if (!normalResult.Normals.IsValid() ||
                normalResult.Normals.Vector().size() !=
                    state->SnapshotPositions.size())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus =
                    GraphNormals::RecomputeStatus::InvalidOutputProperty;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Graph.Vertex.Normals produced missing or count-mismatched normals.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            state->Normals = normalResult.Normals.Vector();
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Graph vertex-normal CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult
        RunPointCloudVertexNormalsCpuWorker(
            const std::shared_ptr<SandboxEditorVertexNormalsCpuJobState>& state)
        {
            Geometry::PointCloud::Cloud scratchCloud{state->PointCloudPoints};
            PointNormals::Params params{};
            params.PositionProperty = GS::PropertyNames::kPosition;
            params.OutputProperty = PointNormals::kDefaultOutputProperty;
            params.KNeighbors = state->PointCloudCommand.KNeighbors;
            params.MinimumNeighbors = state->PointCloudCommand.MinimumNeighbors;
            params.UseRadiusSearch = state->PointCloudCommand.UseRadiusSearch;
            params.Radius = state->PointCloudCommand.Radius;
            params.Orientation = state->PointCloudCommand.Orientation;
            params.FallbackNormal = state->PointCloudCommand.FallbackNormal;
            params.DegenerateNormalLengthEpsilon =
                state->PointCloudCommand.DegenerateNormalLengthEpsilon;
            params.CollinearEigenvalueRatioEpsilon =
                state->PointCloudCommand.CollinearEigenvalueRatioEpsilon;
            params.SkipDeleted = true;

            const PointNormals::Result normalResult =
                PointNormals::Recompute(scratchCloud, params);

            SandboxEditorPointCloudVertexNormalsResult& result =
                state->PointCloudResult;
            result.Status = normalResult.Status ==
                    PointNormals::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus = normalResult.Status;
            result.Backend = normalResult.Backend;
            result.Orientation = state->PointCloudCommand.Orientation;
            result.KNeighbors = state->PointCloudCommand.KNeighbors;
            result.MinimumNeighbors =
                state->PointCloudCommand.MinimumNeighbors;
            result.UseRadiusSearch = state->PointCloudCommand.UseRadiusSearch;
            result.Radius = state->PointCloudCommand.Radius;
            result.Error = ErrorForPointCloudNormalStatus(normalResult.Status);
            CopyPointCloudNormalCounters(normalResult.Diagnostics, result);

            if (normalResult.Status != PointNormals::RecomputeStatus::Success)
            {
                result.Message = "Geometry.PointCloud.Normals failed with ";
                result.Message +=
                    std::string(PointNormals::DebugName(normalResult.Status));
                result.Message += ".";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            if (!normalResult.Normals.IsValid() ||
                normalResult.Normals.Vector().size() !=
                    state->SnapshotPositions.size())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus =
                    PointNormals::RecomputeStatus::InvalidOutputProperty;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.PointCloud.Normals produced missing or count-mismatched normals.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            state->Normals = normalResult.Normals.Vector();
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Point-cloud vertex-normal CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult RunVertexNormalsCpuWorker(
            const std::shared_ptr<SandboxEditorVertexNormalsCpuJobState>& state)
        {
            switch (state->Kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
                return RunMeshVertexNormalsCpuWorker(state);
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return RunGraphVertexNormalsCpuWorker(state);
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return RunPointCloudVertexNormalsCpuWorker(state);
            }
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        }

        [[nodiscard]] Core::Result PublishMeshVertexNormalsCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorVertexNormalsCpuJobState& job)
        {
            SandboxEditorMeshVertexNormalsResult result = job.MeshResult;
            if (!result.Succeeded())
            {
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }
            if (context.Scene == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Scene registry is unavailable for mesh vertex-normal publication.";
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = SandboxEditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "Mesh vertex-normal target entity is stale before publication.";
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            Geometry::PropertySet* properties =
                VertexNormalsTargetProperties(
                    view,
                    SandboxEditorVertexNormalsCpuJobKind::Mesh);
            if (properties == nullptr ||
                !PublishCanonicalVec3Normals(*properties, job.Normals))
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus = GN::RecomputeStatus::PropertyTypeConflict;
                result.Error = Core::ErrorCode::TypeMismatch;
                result.Message =
                    "Mesh vertex-normal publication failed because v:normal has an incompatible type or size.";
                PublishMeshVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            Dirty::MarkVertexNormalsDirty(raw, *entity);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Recompute mesh vertex normals");
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshNormalsSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshVertexNormalsResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishGraphVertexNormalsCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorVertexNormalsCpuJobState& job)
        {
            SandboxEditorGraphVertexNormalsResult result = job.GraphResult;
            if (!result.Succeeded())
            {
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }
            if (context.Scene == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Scene registry is unavailable for graph vertex-normal publication.";
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = SandboxEditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "Graph vertex-normal target entity is stale before publication.";
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            Geometry::PropertySet* properties =
                VertexNormalsTargetProperties(
                    view,
                    SandboxEditorVertexNormalsCpuJobKind::Graph);
            if (properties == nullptr ||
                !PublishCanonicalVec3Normals(*properties, job.Normals))
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus =
                    GraphNormals::RecomputeStatus::PropertyTypeConflict;
                result.Error = Core::ErrorCode::TypeMismatch;
                result.Message =
                    "Graph vertex-normal publication failed because v:normal has an incompatible type or size.";
                PublishGraphVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            Dirty::MarkVertexNormalsDirty(raw, *entity);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Recompute graph vertex normals");
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildGraphNormalsSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishGraphVertexNormalsResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishPointCloudVertexNormalsCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorVertexNormalsCpuJobState& job)
        {
            SandboxEditorPointCloudVertexNormalsResult result =
                job.PointCloudResult;
            if (!result.Succeeded())
            {
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }
            if (context.Scene == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Scene registry is unavailable for point-cloud vertex-normal publication.";
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = SandboxEditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "Point-cloud vertex-normal target entity is stale before publication.";
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            Geometry::PropertySet* properties =
                VertexNormalsTargetProperties(
                    view,
                    SandboxEditorVertexNormalsCpuJobKind::PointCloud);
            if (properties == nullptr ||
                !PublishCanonicalVec3Normals(*properties, job.Normals))
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.NormalStatus =
                    PointNormals::RecomputeStatus::PropertyTypeConflict;
                result.Error = Core::ErrorCode::TypeMismatch;
                result.Message =
                    "Point-cloud vertex-normal publication failed because v:normal has an incompatible type or size.";
                PublishPointCloudVertexNormalsResultSink(context, result);
                return Core::Err(result.Error);
            }

            Dirty::MarkVertexNormalsDirty(raw, *entity);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Recompute point-cloud vertex normals");
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildPointCloudNormalsSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishPointCloudVertexNormalsResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishVertexNormalsCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorVertexNormalsCpuJobState& job)
        {
            switch (job.Kind)
            {
            case SandboxEditorVertexNormalsCpuJobKind::Mesh:
                return PublishMeshVertexNormalsCpuJob(context, job);
            case SandboxEditorVertexNormalsCpuJobKind::Graph:
                return PublishGraphVertexNormalsCpuJob(context, job);
            case SandboxEditorVertexNormalsCpuJobKind::PointCloud:
                return PublishPointCloudVertexNormalsCpuJob(context, job);
            }
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        [[nodiscard]] DerivedJobDesc MakeVertexNormalsCpuJobDesc(
            const SandboxEditorContext& context,
            const std::shared_ptr<SandboxEditorVertexNormalsCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SnapshotPositions.size(),
                                  state->GraphEdgeSlotCount) +
                         1023u) /
                        1024u));
            return DerivedJobDesc{
                .Key = DerivedJobKey{
                    .EntityId = state->StableEntityId,
                    .Domain = VertexNormalsCpuJobDomain(state->Kind),
                    .OutputSemantic = ProgressiveSlotSemantic::Normal,
                    .SourcePropertyGeneration =
                        state->GeometryMetadataSignature,
                    .OutputName = VertexNormalsCpuJobOutputName(state->Kind),
                },
                .Name = VertexNormalsCpuJobName(state->Kind),
                .RequestedJobDomain = ProgressiveJobDomain::Cpu,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = estimatedCost,
                .Scope = context.World,
                .Execute =
                    [state]() -> DerivedJobWorkerResult
                    {
                        return RunVertexNormalsCpuWorker(state);
                    },
                .ValidateOnMainThread =
                    [context, state]()
                    {
                        return ValidateVertexNormalsCpuJobApply(
                            context,
                            *state);
                    },
                .ApplyOnMainThread =
                    [context, state](DerivedJobApplyContext&) -> Core::Result
                    {
                        return PublishVertexNormalsCpuJob(context, *state);
                    },
            };
        }

        [[nodiscard]] SandboxEditorMeshVertexNormalsResult
        SubmitMeshVertexNormalsCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorMeshVertexNormalsCommand& command,
            Geometry::HalfedgeMesh::Mesh mesh,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state =
                std::make_shared<SandboxEditorVertexNormalsCpuJobState>();
            state->Kind = SandboxEditorVertexNormalsCpuJobKind::Mesh;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(mesh);
            state->Mesh = std::move(mesh);
            state->MeshCommand = command;
            state->MeshResult = MakeMeshNormalsResult(
                SandboxEditorCommandStatus::NoChange,
                GN::RecomputeStatus::Success,
                command.Weighting,
                Core::ErrorCode::Success,
                {});
            state->MeshResult.VertexSlotCount =
                state->SnapshotPositions.size();

            DerivedJobDesc desc = MakeVertexNormalsCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorMeshVertexNormalsResult pending =
                    MakePendingMeshVertexNormalsResult(
                        command,
                        state->SnapshotPositions.size(),
                        active->Handle);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Mesh vertex-normal CPU",
                    *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                return MakeMeshNormalsResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    GN::RecomputeStatus::InvalidOutputProperty,
                    command.Weighting,
                    Core::ErrorCode::InvalidState,
                    "Mesh vertex-normal CPU job submission was rejected by the runtime job lane.");
            }

            return MakePendingMeshVertexNormalsResult(
                command,
                state->SnapshotPositions.size(),
                handle);
        }

        [[nodiscard]] SandboxEditorGraphVertexNormalsResult
        SubmitGraphVertexNormalsCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorGraphVertexNormalsCommand& command,
            Geometry::Vertices nodes,
            Geometry::PropertySet edges,
            Geometry::Halfedges halfedges,
            const std::size_t edgeSlotCount,
            const std::uint64_t geometryMetadataSignature)
        {
            std::optional<std::vector<glm::vec3>> positions =
                CollectFiniteGeometryPositions(nodes);
            if (!positions.has_value())
            {
                return MakeGraphNormalsResult(
                    SandboxEditorCommandStatus::InvalidProcessingParameters,
                    GraphNormals::RecomputeStatus::InvalidPositionProperty,
                    command.OrientTowardFallback,
                    Core::ErrorCode::InvalidArgument,
                    "selected graph requires count-matched finite v:position for normal recompute");
            }

            auto state =
                std::make_shared<SandboxEditorVertexNormalsCpuJobState>();
            state->Kind = SandboxEditorVertexNormalsCpuJobKind::Graph;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(*positions);
            state->GraphNodes = std::move(nodes);
            state->GraphEdges = std::move(edges);
            state->GraphHalfedges = std::move(halfedges);
            state->GraphEdgeSlotCount = edgeSlotCount;
            state->GraphCommand = command;
            state->GraphResult = MakeGraphNormalsResult(
                SandboxEditorCommandStatus::NoChange,
                GraphNormals::RecomputeStatus::Success,
                command.OrientTowardFallback,
                Core::ErrorCode::Success,
                {});
            state->GraphResult.VertexSlotCount =
                state->SnapshotPositions.size();
            state->GraphResult.EdgeSlotCount = edgeSlotCount;

            DerivedJobDesc desc = MakeVertexNormalsCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorGraphVertexNormalsResult pending =
                    MakePendingGraphVertexNormalsResult(
                        command,
                        state->SnapshotPositions.size(),
                        edgeSlotCount,
                        active->Handle);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Graph vertex-normal CPU",
                    *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                return MakeGraphNormalsResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    GraphNormals::RecomputeStatus::InvalidOutputProperty,
                    command.OrientTowardFallback,
                    Core::ErrorCode::InvalidState,
                    "Graph vertex-normal CPU job submission was rejected by the runtime job lane.");
            }

            return MakePendingGraphVertexNormalsResult(
                command,
                state->SnapshotPositions.size(),
                edgeSlotCount,
                handle);
        }

        [[nodiscard]] SandboxEditorPointCloudVertexNormalsResult
        SubmitPointCloudVertexNormalsCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorPointCloudVertexNormalsCommand& command,
            Geometry::Vertices points,
            const std::uint64_t geometryMetadataSignature)
        {
            std::optional<std::vector<glm::vec3>> positions =
                CollectFiniteGeometryPositions(points);
            if (!positions.has_value())
            {
                return MakePointCloudNormalsResult(
                    SandboxEditorCommandStatus::InvalidProcessingParameters,
                    PointNormals::RecomputeStatus::InvalidPositionProperty,
                    command,
                    Core::ErrorCode::InvalidArgument,
                    "selected point-cloud requires count-matched finite v:position for normal recompute");
            }

            auto state =
                std::make_shared<SandboxEditorVertexNormalsCpuJobState>();
            state->Kind = SandboxEditorVertexNormalsCpuJobKind::PointCloud;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(*positions);
            state->PointCloudPoints = std::move(points);
            state->PointCloudCommand = command;
            state->PointCloudResult = MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::NoChange,
                PointNormals::RecomputeStatus::Success,
                command,
                Core::ErrorCode::Success,
                {});
            state->PointCloudResult.PointSlotCount =
                state->SnapshotPositions.size();

            DerivedJobDesc desc = MakeVertexNormalsCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorPointCloudVertexNormalsResult pending =
                    MakePendingPointCloudVertexNormalsResult(
                        command,
                        state->SnapshotPositions.size(),
                        active->Handle);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Point-cloud vertex-normal CPU",
                    *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                return MakePointCloudNormalsResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    PointNormals::RecomputeStatus::InvalidOutputProperty,
                    command,
                    Core::ErrorCode::InvalidState,
                    "Point-cloud vertex-normal CPU job submission was rejected by the runtime job lane.");
            }

            return MakePendingPointCloudVertexNormalsResult(
                command,
                state->SnapshotPositions.size(),
                handle);
        }

        enum class SandboxEditorMeshCpuJobKind : std::uint8_t
        {
            Curvature,
            Denoise,
            Remesh,
            Subdivide,
            Simplify,
        };

        [[nodiscard]] const char* MeshCpuJobName(
            const SandboxEditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorMeshCpuJobKind::Curvature:
                return "Sandbox.MeshCurvature.CPU";
            case SandboxEditorMeshCpuJobKind::Denoise:
                return "Sandbox.MeshDenoise.CPU";
            case SandboxEditorMeshCpuJobKind::Remesh:
                return "Sandbox.MeshRemesh.CPU";
            case SandboxEditorMeshCpuJobKind::Subdivide:
                return "Sandbox.MeshSubdivide.CPU";
            case SandboxEditorMeshCpuJobKind::Simplify:
                return "Sandbox.MeshSimplify.CPU";
            }
            return "Sandbox.MeshProcessing.CPU";
        }

        [[nodiscard]] const char* MeshCpuJobOutputName(
            const SandboxEditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorMeshCpuJobKind::Curvature:
                return "mesh_curvature_properties";
            case SandboxEditorMeshCpuJobKind::Denoise:
                return "mesh_denoise_positions";
            case SandboxEditorMeshCpuJobKind::Remesh:
                return "mesh_remesh_topology";
            case SandboxEditorMeshCpuJobKind::Subdivide:
                return "mesh_subdivide_topology";
            case SandboxEditorMeshCpuJobKind::Simplify:
                return "mesh_simplify_topology";
            }
            return "mesh_processing";
        }

        [[nodiscard]] ProgressiveSlotSemantic MeshCpuJobOutputSemantic(
            const SandboxEditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorMeshCpuJobKind::Curvature:
                return ProgressiveSlotSemantic::ScalarField;
            case SandboxEditorMeshCpuJobKind::Denoise:
            case SandboxEditorMeshCpuJobKind::Remesh:
            case SandboxEditorMeshCpuJobKind::Subdivide:
            case SandboxEditorMeshCpuJobKind::Simplify:
                return ProgressiveSlotSemantic::Displacement;
            }
            return ProgressiveSlotSemantic::Displacement;
        }

        void CopyMeshSimplifyAuxiliaryProperties(
            const GS::ConstSourceView& view,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (view.VertexSource == nullptr)
                return;

            const auto sourceTexcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (!sourceTexcoords ||
                sourceTexcoords.Vector().size() != mesh.VerticesSize())
            {
                return;
            }

            auto meshTexcoords = mesh.VertexProperties().GetOrAdd<glm::vec2>(
                "v:texcoord",
                glm::vec2{0.0f});
            for (std::size_t i = 0u;
                 i < sourceTexcoords.Vector().size();
                 ++i)
            {
                meshTexcoords[i] = sourceTexcoords.Vector()[i];
            }
        }

        struct SandboxEditorMeshCpuJobState
        {
            SandboxEditorMeshCpuJobKind Kind{
                SandboxEditorMeshCpuJobKind::Denoise};
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            std::vector<glm::vec3> SnapshotPositions{};
            std::vector<bool> DeletedVertices{};
            Geometry::HalfedgeMesh::Mesh BeforeMesh{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            MeshCurvaturePropertyState CurvatureBefore{};
            MeshCurvaturePropertyState CurvatureAfter{};
            std::vector<glm::vec3> DenoiseAfterPositions{};
            SandboxEditorMeshCurvatureCommand CurvatureCommand{};
            SandboxEditorMeshDenoiseCommand DenoiseCommand{};
            SandboxEditorMeshRemeshCommand RemeshCommand{};
            SandboxEditorMeshSubdivideCommand SubdivideCommand{};
            SandboxEditorMeshSimplifyCommand SimplifyCommand{};
            SandboxEditorMeshCurvatureResult CurvatureResult{};
            SandboxEditorMeshDenoiseResult DenoiseResult{};
            SandboxEditorMeshRemeshResult RemeshResult{};
            SandboxEditorMeshSubdivideResult SubdivideResult{};
            SandboxEditorMeshSimplifyResult SimplifyResult{};
        };

        [[nodiscard]] DerivedJobApplyValidation ValidateMeshCpuJobApply(
            const SandboxEditorContext& context,
            const SandboxEditorMeshCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
                return DerivedJobApplyValidation::MissingEntity;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh)
                return DerivedJobApplyValidation::StaleGeometryGeneration;

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                job.GeometryMetadataSignature)
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            if (view.VertexSource == nullptr)
                return DerivedJobApplyValidation::StaleGeometryGeneration;

            std::optional<std::vector<glm::vec3>> current =
                CollectFiniteGeometryPositions(view.VertexSource->Properties);
            if (!current.has_value() ||
                !SameGeometryPositions(*current, job.SnapshotPositions))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            if (job.Kind == SandboxEditorMeshCpuJobKind::Curvature)
            {
                GS::MutableSourceView mutableView =
                    GS::BuildMutableView(raw, *entity);
                if (!mutableView.Valid() ||
                    mutableView.VertexSource == nullptr)
                {
                    return DerivedJobApplyValidation::StaleGeometryGeneration;
                }

                MeshCurvaturePropertyState currentCurvature{};
                std::string diagnostic{};
                if (!CaptureMeshCurvaturePropertyState(
                        mutableView.VertexSource->Properties,
                        job.SnapshotPositions.size(),
                        currentCurvature,
                        diagnostic) ||
                    !SameMeshCurvaturePropertyState(
                        currentCurvature,
                        job.CurvatureBefore))
                {
                    return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
                }
            }

            return DerivedJobApplyValidation::Current;
        }

        void PublishMeshCurvatureResultSink(
            const SandboxEditorContext& context,
            SandboxEditorMeshCurvatureResult result)
        {
            if (context.MethodResultSinks.MeshCurvature)
                context.MethodResultSinks.MeshCurvature(std::move(result));
        }

        void PublishMeshDenoiseResultSink(
            const SandboxEditorContext& context,
            SandboxEditorMeshDenoiseResult result)
        {
            if (context.MethodResultSinks.MeshDenoise)
                context.MethodResultSinks.MeshDenoise(std::move(result));
        }

        void PublishMeshRemeshResultSink(
            const SandboxEditorContext& context,
            SandboxEditorMeshRemeshResult result)
        {
            if (context.MethodResultSinks.MeshRemesh)
                context.MethodResultSinks.MeshRemesh(std::move(result));
        }

        void PublishMeshSubdivideResultSink(
            const SandboxEditorContext& context,
            SandboxEditorMeshSubdivideResult result)
        {
            if (context.MethodResultSinks.MeshSubdivide)
                context.MethodResultSinks.MeshSubdivide(std::move(result));
        }

        void PublishMeshSimplifyResultSink(
            const SandboxEditorContext& context,
            SandboxEditorMeshSimplifyResult result)
        {
            if (context.MethodResultSinks.MeshSimplify)
                context.MethodResultSinks.MeshSimplify(std::move(result));
        }

        [[nodiscard]] DerivedJobWorkerResult RunMeshCurvatureCpuWorker(
            const std::shared_ptr<SandboxEditorMeshCpuJobState>& state)
        {
            SandboxEditorMeshCurvatureResult& result = state->CurvatureResult;
            result.VertexSlotCount = state->SnapshotPositions.size();

            Curv::CurvatureField curvature =
                Curv::ComputeCurvature(state->Mesh);
            if (!curvature.MeanCurvatureProperty ||
                !curvature.GaussianCurvatureProperty ||
                curvature.MeanCurvatureProperty.Vector().size() !=
                    result.VertexSlotCount ||
                curvature.GaussianCurvatureProperty.Vector().size() !=
                    result.VertexSlotCount)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced missing or count-mismatched scalar properties.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            const std::vector<double>& mean =
                curvature.MeanCurvatureProperty.Vector();
            const std::vector<double>& gaussian =
                curvature.GaussianCurvatureProperty.Vector();
            result.NonFiniteScalarCount =
                CountNonFiniteScalars(
                    std::span<const double>{mean.data(), mean.size()}) +
                CountNonFiniteScalars(
                    std::span<const double>{gaussian.data(), gaussian.size()});
            if (result.NonFiniteScalarCount != 0u)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced non-finite scalar curvature values.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            state->CurvatureAfter = state->CurvatureBefore;
            state->CurvatureAfter.HadMean = true;
            state->CurvatureAfter.Mean = mean;
            state->CurvatureAfter.HadGaussian = true;
            state->CurvatureAfter.Gaussian = gaussian;
            result.ScalarPropertyCount = 2u;
            result.ScalarWrittenCount = mean.size() + gaussian.size();

            if (result.DirectionsRequested &&
                result.DirectionsAvailable)
            {
                if (!curvature.PrincipalDir1Property ||
                    !curvature.PrincipalDir2Property ||
                    curvature.PrincipalDir1Property.Vector().size() !=
                        result.VertexSlotCount ||
                    curvature.PrincipalDir2Property.Vector().size() !=
                        result.VertexSlotCount)
                {
                    result.Status =
                        SandboxEditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Geometry.Curvature produced missing or count-mismatched principal-direction properties.";
                    return DerivedJobOutput{
                        .PayloadToken = 0u,
                        .NormalizedProgress = 1.0f,
                        .ProgressDeterminate = true,
                        .Diagnostic = result.Message,
                    };
                }

                const std::vector<glm::vec3>& dir1 =
                    curvature.PrincipalDir1Property.Vector();
                const std::vector<glm::vec3>& dir2 =
                    curvature.PrincipalDir2Property.Vector();
                result.NonFiniteDirectionCount =
                    CountNonFiniteVectors(
                        std::span<const glm::vec3>{dir1.data(), dir1.size()}) +
                    CountNonFiniteVectors(
                        std::span<const glm::vec3>{dir2.data(), dir2.size()});
                if (result.NonFiniteDirectionCount != 0u)
                {
                    result.Status =
                        SandboxEditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Geometry.Curvature produced non-finite principal directions.";
                    return DerivedJobOutput{
                        .PayloadToken = 0u,
                        .NormalizedProgress = 1.0f,
                        .ProgressDeterminate = true,
                        .Diagnostic = result.Message,
                    };
                }

                state->CurvatureAfter.HadDir1 = true;
                state->CurvatureAfter.Dir1 = dir1;
                state->CurvatureAfter.HadDir2 = true;
                state->CurvatureAfter.Dir2 = dir2;
                result.DirectionPropertyCount = 2u;
                result.DirectionWrittenCount = dir1.size() + dir2.size();
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.DirectionsPublished =
                result.DirectionPropertyCount == 2u &&
                result.DirectionWrittenCount == result.VertexSlotCount * 2u;
            result.Error = Core::ErrorCode::Success;
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Mesh curvature CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult RunMeshDenoiseCpuWorker(
            const std::shared_ptr<SandboxEditorMeshCpuJobState>& state)
        {
            Smooth::BilateralDenoiseParams params{};
            params.NormalIterations = state->DenoiseCommand.NormalIterations;
            params.VertexIterations = state->DenoiseCommand.VertexIterations;
            params.SigmaSpatial = state->DenoiseCommand.SigmaSpatial;
            params.SigmaRange = state->DenoiseCommand.SigmaRange;
            params.PreserveBoundary = state->DenoiseCommand.PreserveBoundary;
            params.DegenerateNormalLengthEpsilon =
                state->DenoiseCommand.DegenerateNormalLengthEpsilon;

            const Smooth::BilateralDenoiseResult denoise =
                Smooth::DenoiseBilateral(state->Mesh, params);
            SandboxEditorMeshDenoiseResult& result = state->DenoiseResult;
            result.DenoiseStatus = denoise.Status;
            result.Error = ErrorForDenoiseStatus(denoise.Status);
            CopyMeshDenoiseCounters(denoise, result);
            result.VertexSlotCount = state->SnapshotPositions.size();
            result.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(state->DeletedVertices.begin(),
                               state->DeletedVertices.end(),
                               true));
            result.WrittenCount =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;

            if (denoise.Status != Smooth::DenoiseStatus::Success)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Message = "Geometry.Smoothing denoise failed with ";
                result.Message += std::string(Smooth::DebugName(denoise.Status));
                result.Message += ".";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            std::vector<glm::vec3> afterPositions =
                ExtractMeshPositions(state->Mesh);
            if (afterPositions.size() != state->SnapshotPositions.size() ||
                !AllFiniteVec3(std::span<const glm::vec3>{
                    afterPositions.data(),
                    afterPositions.size()}))
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.DenoiseStatus = Smooth::DenoiseStatus::NonFiniteInput;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Smoothing denoise produced invalid or count-mismatched positions.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            std::size_t movedPublishedVertices = 0u;
            for (std::size_t i = 0u; i < afterPositions.size(); ++i)
            {
                if (i < state->DeletedVertices.size() &&
                    state->DeletedVertices[i])
                {
                    afterPositions[i] = state->SnapshotPositions[i];
                    continue;
                }
                if (afterPositions[i] != state->SnapshotPositions[i])
                    ++movedPublishedVertices;
            }
            result.MovedVertexCount = movedPublishedVertices;
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            state->DenoiseAfterPositions = std::move(afterPositions);
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Mesh denoise CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult RunMeshRemeshCpuWorker(
            const std::shared_ptr<SandboxEditorMeshCpuJobState>& state)
        {
            SandboxEditorMeshRemeshResult& result = state->RemeshResult;
            result.InputVertexCount = state->Mesh.VertexCount();
            result.InputFaceCount = state->Mesh.FaceCount();

            std::optional<Geometry::RemeshingOperationResult> remeshResult{};
            if (state->RemeshCommand.Mode == SandboxEditorMeshRemeshMode::Uniform)
            {
                Remesh::RemeshingParams params{};
                params.TargetLength = state->RemeshCommand.TargetEdgeLength;
                params.Iterations = state->RemeshCommand.Iterations;
                params.Lambda = state->RemeshCommand.Lambda;
                params.PreserveBoundary =
                    state->RemeshCommand.PreserveBoundary;
                params.ProjectToSurface =
                    state->RemeshCommand.ProjectToSurface;
                params.ReferenceProjectionK =
                    state->RemeshCommand.ReferenceProjectionK;
                params.MaxReferenceProjectionDistance =
                    state->RemeshCommand.MaxReferenceProjectionDistance;
                remeshResult = Remesh::Remesh(state->Mesh, params);
            }
            else
            {
                AdaptiveRemesh::AdaptiveRemeshingParams params{};
                if (state->RemeshCommand.TargetEdgeLength > 0.0)
                {
                    params.MinEdgeLength =
                        state->RemeshCommand.TargetEdgeLength * 0.5;
                    params.MaxEdgeLength =
                        state->RemeshCommand.TargetEdgeLength * 2.0;
                }
                params.CurvatureAdaptation =
                    state->RemeshCommand.CurvatureAdaptation;
                params.Sizing =
                    ToAdaptiveSizingLaw(state->RemeshCommand.SizingLaw);
                params.ApproximationError =
                    state->RemeshCommand.ApproximationError;
                params.Iterations = state->RemeshCommand.Iterations;
                params.Lambda = state->RemeshCommand.Lambda;
                params.PreserveBoundary =
                    state->RemeshCommand.PreserveBoundary;
                params.EnableReferenceProjection =
                    state->RemeshCommand.ProjectToSurface;
                params.ReferenceProjectionK =
                    state->RemeshCommand.ReferenceProjectionK;
                params.MaxReferenceProjectionDistance =
                    state->RemeshCommand.MaxReferenceProjectionDistance;
                remeshResult = AdaptiveRemesh::AdaptiveRemesh(state->Mesh, params);
            }

            if (!remeshResult.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry remeshing failed for the selected mesh and parameters.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            if (state->Mesh.HasGarbage())
                state->Mesh.GarbageCollection();
            CopyRemeshCounters(*remeshResult, result);
            result.OutputVertexCount = state->Mesh.VertexCount();
            result.OutputFaceCount = state->Mesh.FaceCount();
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Mesh remesh CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult RunMeshSubdivideCpuWorker(
            const std::shared_ptr<SandboxEditorMeshCpuJobState>& state)
        {
            SandboxEditorMeshSubdivideResult& result =
                state->SubdivideResult;
            result.InputVertexCount = state->Mesh.VertexCount();
            result.InputFaceCount = state->Mesh.FaceCount();

            Geometry::HalfedgeMesh::Mesh output{};
            if (state->SubdivideCommand.Operator ==
                SandboxEditorMeshSubdivideOperator::Loop)
            {
                LoopSubdivide::SubdivisionParams params{};
                params.Iterations = state->SubdivideCommand.Iterations;
                params.MaxOutputFaces =
                    state->SubdivideCommand.MaxOutputFaces;
                params.PreserveFeatureEdges =
                    state->SubdivideCommand.PreserveLoopFeatureEdges;
                params.FeatureEdgePropertyName =
                    state->SubdivideCommand.FeatureEdgePropertyName;
                const std::optional<LoopSubdivide::SubdivisionResult>
                    subdivision =
                        LoopSubdivide::Subdivide(state->Mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        SandboxEditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Geometry.Subdivision Loop subdivision failed for the selected mesh and parameters.";
                    return DerivedJobOutput{
                        .PayloadToken = 0u,
                        .NormalizedProgress = 1.0f,
                        .ProgressDeterminate = true,
                        .Diagnostic = result.Message,
                    };
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }
            else if (state->SubdivideCommand.Operator ==
                     SandboxEditorMeshSubdivideOperator::CatmullClark)
            {
                CatmullClark::SubdivisionParams params{};
                params.Iterations = state->SubdivideCommand.Iterations;
                const std::optional<CatmullClark::SubdivisionResult>
                    subdivision =
                        CatmullClark::Subdivide(state->Mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        SandboxEditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Geometry.CatmullClark subdivision failed for the selected mesh and parameters.";
                    return DerivedJobOutput{
                        .PayloadToken = 0u,
                        .NormalizedProgress = 1.0f,
                        .ProgressDeterminate = true,
                        .Diagnostic = result.Message,
                    };
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }
            else
            {
                Sqrt3Subdivide::Sqrt3Params params{};
                params.Iterations = state->SubdivideCommand.Iterations;
                params.MaxOutputFaces =
                    state->SubdivideCommand.MaxOutputFaces;
                const std::optional<Sqrt3Subdivide::Sqrt3Result>
                    subdivision =
                        Sqrt3Subdivide::Subdivide(state->Mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        SandboxEditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Geometry.HalfedgeMesh.SubdivisionSqrt3 failed for the selected mesh and parameters.";
                    return DerivedJobOutput{
                        .PayloadToken = 0u,
                        .NormalizedProgress = 1.0f,
                        .ProgressDeterminate = true,
                        .Diagnostic = result.Message,
                    };
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }

            if (output.HasGarbage())
                output.GarbageCollection();
            result.OutputVertexCount = output.VertexCount();
            result.OutputFaceCount = output.FaceCount();
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            state->Mesh = std::move(output);
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Mesh subdivide CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult RunMeshSimplifyCpuWorker(
            const std::shared_ptr<SandboxEditorMeshCpuJobState>& state)
        {
            SandboxEditorMeshSimplifyResult& result = state->SimplifyResult;
            result.InputVertexCount = state->Mesh.VertexCount();
            result.InputFaceCount = state->Mesh.FaceCount();

            Simpl::Params params{};
            params.Metric =
                state->SimplifyCommand.Metric ==
                        SandboxEditorMeshSimplifyMetric::FA_QEM
                    ? Simpl::Metric::FA_QEM
                    : Simpl::Metric::ClassicalQEM;
            params.TargetFaces = state->SimplifyCommand.TargetFaces;
            params.MaxError = state->SimplifyCommand.MaxError > 0.0
                ? state->SimplifyCommand.MaxError
                : 1.0e30;
            params.PreserveBoundary =
                state->SimplifyCommand.PreserveBoundary;
            params.FeatureAngleThresholdDegrees =
                state->SimplifyCommand.FeatureAngleThresholdDegrees;
            params.NormalWeight = state->SimplifyCommand.NormalWeight;
            params.BoundaryWeight = state->SimplifyCommand.BoundaryWeight;
            params.CurvatureWeight = state->SimplifyCommand.CurvatureWeight;
            params.PreserveSharpFeatures =
                state->SimplifyCommand.PreserveSharpFeatures;
            params.PreserveUvSeams =
                state->SimplifyCommand.PreserveUvSeams;

            const std::optional<Simpl::Result> simplification =
                Simpl::Simplify(state->Mesh, params);
            if (!simplification.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Simplification failed for the selected mesh and parameters.";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            if (state->Mesh.HasGarbage())
                state->Mesh.GarbageCollection();
            result.OutputVertexCount = state->Mesh.VertexCount();
            result.OutputFaceCount = state->Mesh.FaceCount();
            result.CollapseCount = simplification->CollapseCount;
            result.MaxCollapseError = simplification->MaxCollapseError;
            result.CollapsesRejectedTopology =
                simplification->CollapsesRejectedTopology;
            result.CollapsesRejectedQuality =
                simplification->CollapsesRejectedQuality;
            result.SharpFeatureVerticesPinned =
                simplification->SharpFeatureVerticesPinned;
            result.SeamVerticesPinned = simplification->SeamVerticesPinned;
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "Mesh simplify CPU result ready",
            };
        }

        [[nodiscard]] DerivedJobWorkerResult RunMeshCpuWorker(
            const std::shared_ptr<SandboxEditorMeshCpuJobState>& state)
        {
            switch (state->Kind)
            {
            case SandboxEditorMeshCpuJobKind::Curvature:
                return RunMeshCurvatureCpuWorker(state);
            case SandboxEditorMeshCpuJobKind::Denoise:
                return RunMeshDenoiseCpuWorker(state);
            case SandboxEditorMeshCpuJobKind::Remesh:
                return RunMeshRemeshCpuWorker(state);
            case SandboxEditorMeshCpuJobKind::Subdivide:
                return RunMeshSubdivideCpuWorker(state);
            case SandboxEditorMeshCpuJobKind::Simplify:
                return RunMeshSimplifyCpuWorker(state);
            }
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        }

        [[nodiscard]] Core::Result PublishMeshDenoiseCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorMeshCpuJobState& job)
        {
            SandboxEditorMeshDenoiseResult result = job.DenoiseResult;
            if (!result.Succeeded())
            {
                PublishMeshDenoiseResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            const SandboxEditorCommandStatus commitStatus =
                CommitMeshDenoisePositions(
                    context,
                    job.StableEntityId,
                    job.SnapshotPositions,
                    job.DenoiseAfterPositions);
            if (commitStatus != SandboxEditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh denoise position publication failed during editor history commit.";
                PublishMeshDenoiseResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshDenoiseSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshDenoiseResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshCurvatureCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorMeshCpuJobState& job)
        {
            SandboxEditorMeshCurvatureResult result = job.CurvatureResult;
            if (!result.Succeeded())
            {
                PublishMeshCurvatureResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            const SandboxEditorCommandStatus commitStatus =
                CommitMeshCurvatureProperties(
                    context,
                    job.StableEntityId,
                    job.CurvatureBefore,
                    job.CurvatureAfter);
            if (commitStatus != SandboxEditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh curvature property publication failed during editor history commit.";
                PublishMeshCurvatureResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.DirectionsPublished =
                result.DirectionPropertyCount == 2u &&
                result.DirectionWrittenCount == result.VertexSlotCount * 2u;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshCurvatureSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshCurvatureResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshRemeshCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorMeshCpuJobState& job)
        {
            SandboxEditorMeshRemeshResult result = job.RemeshResult;
            if (!result.Succeeded())
            {
                PublishMeshRemeshResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            const SandboxEditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Remesh mesh",
                    job.BeforeMesh,
                    job.Mesh);
            if (commitStatus != SandboxEditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh remesh publication failed during editor history commit.";
                PublishMeshRemeshResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshRemeshSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshRemeshResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshSubdivideCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorMeshCpuJobState& job)
        {
            SandboxEditorMeshSubdivideResult result = job.SubdivideResult;
            if (!result.Succeeded())
            {
                PublishMeshSubdivideResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            const SandboxEditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Subdivide mesh",
                    job.BeforeMesh,
                    job.Mesh);
            if (commitStatus != SandboxEditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh subdivide publication failed during editor history commit.";
                PublishMeshSubdivideResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSubdivideSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshSubdivideResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshSimplifyCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorMeshCpuJobState& job)
        {
            SandboxEditorMeshSimplifyResult result = job.SimplifyResult;
            if (!result.Succeeded())
            {
                PublishMeshSimplifyResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            const SandboxEditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Simplify mesh",
                    job.BeforeMesh,
                    job.Mesh);
            if (commitStatus != SandboxEditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh simplify publication failed during editor history commit.";
                PublishMeshSimplifyResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSimplifySuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshSimplifyResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorMeshCpuJobState& job)
        {
            switch (job.Kind)
            {
            case SandboxEditorMeshCpuJobKind::Curvature:
                return PublishMeshCurvatureCpuJob(context, job);
            case SandboxEditorMeshCpuJobKind::Denoise:
                return PublishMeshDenoiseCpuJob(context, job);
            case SandboxEditorMeshCpuJobKind::Remesh:
                return PublishMeshRemeshCpuJob(context, job);
            case SandboxEditorMeshCpuJobKind::Subdivide:
                return PublishMeshSubdivideCpuJob(context, job);
            case SandboxEditorMeshCpuJobKind::Simplify:
                return PublishMeshSimplifyCpuJob(context, job);
            }
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        [[nodiscard]] DerivedJobDesc MakeMeshCpuJobDesc(
            const SandboxEditorContext& context,
            const std::shared_ptr<SandboxEditorMeshCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SnapshotPositions.size(),
                                  state->BeforeMesh.FaceCount()) +
                         1023u) /
                        1024u));
            return DerivedJobDesc{
                .Key = DerivedJobKey{
                    .EntityId = state->StableEntityId,
                    .Domain = ProgressiveGeometryDomain::MeshSurface,
                    .OutputSemantic = MeshCpuJobOutputSemantic(state->Kind),
                    .SourcePropertyGeneration =
                        state->GeometryMetadataSignature,
                    .OutputName = MeshCpuJobOutputName(state->Kind),
                },
                .Name = MeshCpuJobName(state->Kind),
                .RequestedJobDomain = ProgressiveJobDomain::Cpu,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = estimatedCost,
                .Scope = context.World,
                .Execute =
                    [state]() -> DerivedJobWorkerResult
                    {
                        return RunMeshCpuWorker(state);
                    },
                .ValidateOnMainThread =
                    [context, state]()
                    {
                        return ValidateMeshCpuJobApply(context, *state);
                    },
                .ApplyOnMainThread =
                    [context, state](DerivedJobApplyContext&) -> Core::Result
                    {
                        return PublishMeshCpuJob(context, *state);
                },
            };
        }

        [[nodiscard]] SandboxEditorMeshCurvatureResult
        SubmitMeshCurvatureCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorMeshCurvatureCommand& command,
            MeshCurvatureSourceResult source,
            MeshCurvaturePropertyState before,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<SandboxEditorMeshCpuJobState>();
            state->Kind = SandboxEditorMeshCpuJobKind::Curvature;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            state->Mesh = std::move(source.Mesh);
            state->CurvatureBefore = std::move(before);
            state->CurvatureAfter = state->CurvatureBefore;
            state->CurvatureCommand = command;
            state->CurvatureResult = MakeMeshCurvatureBaseResult(
                command,
                context.MeshCurvatureDirectionsAvailable);
            state->CurvatureResult.VertexSlotCount = source.VertexSlotCount;

            DerivedJobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorMeshCurvatureResult pending =
                    MakePendingMeshCurvatureResult(
                        command,
                        context.MeshCurvatureDirectionsAvailable,
                        source.VertexSlotCount,
                        active->Handle);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh curvature CPU", *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                SandboxEditorMeshCurvatureResult result =
                    MakeMeshCurvatureBaseResult(
                        command,
                        context.MeshCurvatureDirectionsAvailable);
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.VertexSlotCount = source.VertexSlotCount;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh curvature CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingMeshCurvatureResult(
                command,
                context.MeshCurvatureDirectionsAvailable,
                source.VertexSlotCount,
                handle);
        }

        [[nodiscard]] SandboxEditorMeshDenoiseResult SubmitMeshDenoiseCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorMeshDenoiseCommand& command,
            MeshDenoiseSourceResult source,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<SandboxEditorMeshCpuJobState>();
            state->Kind = SandboxEditorMeshCpuJobKind::Denoise;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(source.BeforePositions);
            state->DeletedVertices = source.DeletedVertices;
            state->Mesh = std::move(source.Mesh);
            state->DenoiseCommand = command;
            state->DenoiseResult = MakeMeshDenoiseBaseResult(command);
            state->DenoiseResult.VertexSlotCount =
                state->SnapshotPositions.size();
            state->DenoiseResult.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(state->DeletedVertices.begin(),
                               state->DeletedVertices.end(),
                               true));
            state->DenoiseResult.WrittenCount =
                state->DenoiseResult.VertexSlotCount -
                state->DenoiseResult.SkippedDeletedVertexCount;

            DerivedJobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                MeshDenoiseSourceResult pendingSource{};
                pendingSource.BeforePositions = state->SnapshotPositions;
                pendingSource.DeletedVertices = state->DeletedVertices;
                SandboxEditorMeshDenoiseResult pending =
                    MakePendingMeshDenoiseResult(
                        command,
                        pendingSource,
                        active->Handle);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh denoise CPU", *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                SandboxEditorMeshDenoiseResult result =
                    MakeMeshDenoiseBaseResult(command);
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh denoise CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            MeshDenoiseSourceResult pendingSource{};
            pendingSource.BeforePositions = state->SnapshotPositions;
            pendingSource.DeletedVertices = state->DeletedVertices;
            return MakePendingMeshDenoiseResult(
                command,
                pendingSource,
                handle);
        }

        [[nodiscard]] SandboxEditorMeshRemeshResult SubmitMeshRemeshCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorMeshRemeshCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<SandboxEditorMeshCpuJobState>();
            state->Kind = SandboxEditorMeshCpuJobKind::Remesh;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            state->Mesh = std::move(source.Mesh);
            state->RemeshCommand = command;
            state->RemeshResult = MakeMeshRemeshBaseResult(command);
            state->RemeshResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->RemeshResult.InputFaceCount = state->BeforeMesh.FaceCount();

            DerivedJobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorMeshRemeshResult pending =
                    MakePendingMeshRemeshResult(
                        command,
                        state->BeforeMesh,
                        active->Handle);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh remesh CPU", *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                SandboxEditorMeshRemeshResult result =
                    MakeMeshRemeshBaseResult(command);
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh remesh CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingMeshRemeshResult(command, state->BeforeMesh, handle);
        }

        [[nodiscard]] SandboxEditorMeshSubdivideResult
        SubmitMeshSubdivideCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorMeshSubdivideCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<SandboxEditorMeshCpuJobState>();
            state->Kind = SandboxEditorMeshCpuJobKind::Subdivide;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            state->Mesh = std::move(source.Mesh);
            state->SubdivideCommand = command;
            state->SubdivideResult = MakeMeshSubdivideBaseResult(command);
            state->SubdivideResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->SubdivideResult.InputFaceCount =
                state->BeforeMesh.FaceCount();

            DerivedJobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorMeshSubdivideResult pending =
                    MakePendingMeshSubdivideResult(
                        command,
                        state->BeforeMesh,
                        active->Handle);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh subdivide CPU", *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                SandboxEditorMeshSubdivideResult result =
                    MakeMeshSubdivideBaseResult(command);
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh subdivide CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingMeshSubdivideResult(
                command,
                state->BeforeMesh,
                handle);
        }

        [[nodiscard]] SandboxEditorMeshSimplifyResult SubmitMeshSimplifyCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorMeshSimplifyCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature)
        {
            auto state = std::make_shared<SandboxEditorMeshCpuJobState>();
            state->Kind = SandboxEditorMeshCpuJobKind::Simplify;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            state->Mesh = std::move(source.Mesh);
            state->SimplifyCommand = command;
            state->SimplifyResult = MakeMeshSimplifyBaseResult(command);
            state->SimplifyResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->SimplifyResult.InputFaceCount = state->BeforeMesh.FaceCount();

            DerivedJobDesc desc = MakeMeshCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorMeshSimplifyResult pending =
                    MakePendingMeshSimplifyResult(
                        command,
                        state->BeforeMesh,
                        active->Handle);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh simplify CPU", *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                SandboxEditorMeshSimplifyResult result =
                    MakeMeshSimplifyBaseResult(command);
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh simplify CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingMeshSimplifyResult(
                command,
                state->BeforeMesh,
                handle);
        }

        [[nodiscard]] std::string BuildRegistrationSuccessMessage(
            const SandboxEditorRegistrationResult& result)
        {
            std::string message = "ICP registration completed (variant=";
            message += DebugNameForSandboxEditorICPVariant(result.Variant);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ", step=";
            message += std::to_string(result.AppliedStep);
            message += "/";
            message += std::to_string(result.TrajectoryLength);
            message += ", converged=";
            message += result.Converged ? "yes" : "no";
            message += ").";
            return message;
        }

        // Compose an entity model matrix (translate * rotate * scale) from its
        // local Transform::Component so ICP can run in world space.
        [[nodiscard]] glm::mat4 ModelMatrixFromTransform(
            const ECSC::Transform::Component& transform) noexcept
        {
            glm::mat4 model = glm::mat4_cast(transform.Rotation);
            model[0] *= transform.Scale.x;
            model[1] *= transform.Scale.y;
            model[2] *= transform.Scale.z;
            model[3] = glm::vec4(transform.Position, 1.0f);
            return model;
        }

        // Decompose a composed model matrix back into a Transform::Component.
        // The ICP delta is rigid, so the scale carried by the source model is
        // preserved; rotation is recovered from the scale-normalized columns.
        void DecomposeModelToTransform(
            const glm::mat4& model,
            ECSC::Transform::Component& out) noexcept
        {
            out.Position = glm::vec3(model[3]);
            const glm::vec3 col0(model[0]);
            const glm::vec3 col1(model[1]);
            const glm::vec3 col2(model[2]);
            const glm::vec3 scale(
                glm::length(col0), glm::length(col1), glm::length(col2));
            const glm::mat3 rotation(
                scale.x > 0.0f ? col0 / scale.x : glm::vec3(1.0f, 0.0f, 0.0f),
                scale.y > 0.0f ? col1 / scale.y : glm::vec3(0.0f, 1.0f, 0.0f),
                scale.z > 0.0f ? col2 / scale.z : glm::vec3(0.0f, 0.0f, 1.0f));
            out.Rotation = glm::quat_cast(rotation);
            out.Scale = scale;
        }

        [[nodiscard]] glm::vec3 ComputePointCentroid(
            const std::span<const glm::vec3> points) noexcept
        {
            if (points.empty())
                return glm::vec3(0.0f);

            glm::dvec3 sum(0.0);
            for (const glm::vec3& point : points)
                sum += glm::dvec3(point);
            return glm::vec3(sum / static_cast<double>(points.size()));
        }

        [[nodiscard]] SandboxEditorRegistrationResult
        MakeRegistrationBaseResult(
            const SandboxEditorRegistrationCommand& command)
        {
            return SandboxEditorRegistrationResult{
                .Status = SandboxEditorCommandStatus::NoChange,
                .Variant = command.Variant,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] SandboxEditorRegistrationResult
        MakePendingRegistrationResult(
            const SandboxEditorRegistrationCommand& command,
            const std::size_t sourcePointCount,
            const std::size_t targetPointCount,
            const DerivedJobHandle handle)
        {
            SandboxEditorRegistrationResult result =
                MakeRegistrationBaseResult(command);
            result.Status = SandboxEditorCommandStatus::Pending;
            result.SourcePointCount = sourcePointCount;
            result.TargetPointCount = targetPointCount;
            result.Message = "ICP registration CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
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

        struct SandboxEditorRegistrationCpuJobState
        {
            std::uint32_t SourceStableEntityId{0u};
            std::uint32_t TargetStableEntityId{0u};
            std::uint64_t SourceGeometryMetadataSignature{0u};
            std::uint64_t TargetGeometryMetadataSignature{0u};
            SandboxEditorRegistrationCommand Command{};
            std::vector<glm::vec3> SourceLocalPoints{};
            std::vector<glm::vec3> TargetLocalPoints{};
            ECSC::Transform::Component SourceBeforeTransform{};
            bool TargetHadTransform{false};
            ECSC::Transform::Component TargetBeforeTransform{};
            SandboxEditorRegistrationResult Result{};
            ECSC::Transform::Component SourceAfterTransform{};
        };

        [[nodiscard]] std::vector<glm::vec3> TransformPointsToWorld(
            const std::vector<glm::vec3>& points,
            const ECSC::Transform::Component& transform)
        {
            const glm::mat4 model = ModelMatrixFromTransform(transform);
            std::vector<glm::vec3> world;
            world.reserve(points.size());
            for (const glm::vec3& point : points)
                world.push_back(glm::vec3(model * glm::vec4(point, 1.0f)));
            return world;
        }

        [[nodiscard]] DerivedJobApplyValidation
        ValidateRegistrationCpuJobApply(
            const SandboxEditorContext& context,
            const SandboxEditorRegistrationCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> sourceEntity =
                ResolveStableEntity(raw, job.SourceStableEntityId);
            const std::optional<ECS::EntityHandle> targetEntity =
                ResolveStableEntity(raw, job.TargetStableEntityId);
            if (!sourceEntity.has_value() || !targetEntity.has_value())
                return DerivedJobApplyValidation::MissingEntity;

            const GS::ConstSourceView sourceView =
                GS::BuildConstView(raw, *sourceEntity);
            const GS::ConstSourceView targetView =
                GS::BuildConstView(raw, *targetEntity);
            if (GS::BuildSourceAvailability(sourceView).ProvenanceDomain !=
                    GS::Domain::PointCloud ||
                GS::BuildSourceAvailability(targetView).ProvenanceDomain !=
                    GS::Domain::PointCloud ||
                sourceView.VertexSource == nullptr ||
                targetView.VertexSource == nullptr)
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            if (GeometryMetadataSignatureForEntity(raw, *sourceEntity) !=
                    job.SourceGeometryMetadataSignature ||
                GeometryMetadataSignatureForEntity(raw, *targetEntity) !=
                    job.TargetGeometryMetadataSignature)
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            const std::optional<std::vector<glm::vec3>> sourcePoints =
                CollectFiniteGeometryPositions(
                    sourceView.VertexSource->Properties);
            const std::optional<std::vector<glm::vec3>> targetPoints =
                CollectFiniteGeometryPositions(
                    targetView.VertexSource->Properties);
            if (!sourcePoints.has_value() || !targetPoints.has_value() ||
                !SameGeometryPositions(*sourcePoints,
                                          job.SourceLocalPoints) ||
                !SameGeometryPositions(*targetPoints,
                                          job.TargetLocalPoints))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            const ECSC::Transform::Component* sourceTransform =
                raw.try_get<ECSC::Transform::Component>(*sourceEntity);
            if (sourceTransform == nullptr ||
                !SameTransformComponent(*sourceTransform,
                                        job.SourceBeforeTransform))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            const ECSC::Transform::Component* targetTransform =
                raw.try_get<ECSC::Transform::Component>(*targetEntity);
            if (targetTransform == nullptr)
                return job.TargetHadTransform
                    ? DerivedJobApplyValidation::StaleSourcePropertyGeneration
                    : DerivedJobApplyValidation::Current;
            if (!job.TargetHadTransform ||
                !SameTransformComponent(*targetTransform,
                                        job.TargetBeforeTransform))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            return DerivedJobApplyValidation::Current;
        }

        void PublishRegistrationResultSink(
            const SandboxEditorContext& context,
            SandboxEditorRegistrationResult result)
        {
            if (context.MethodResultSinks.Registration)
                context.MethodResultSinks.Registration(std::move(result));
        }

        [[nodiscard]] DerivedJobWorkerResult RunRegistrationCpuWorker(
            const std::shared_ptr<SandboxEditorRegistrationCpuJobState>& state)
        {
            SandboxEditorRegistrationResult& result = state->Result;
            result.SourcePointCount = state->SourceLocalPoints.size();
            result.TargetPointCount = state->TargetLocalPoints.size();

            const std::vector<glm::vec3> sourceWorld =
                TransformPointsToWorld(state->SourceLocalPoints,
                                       state->SourceBeforeTransform);
            const std::vector<glm::vec3> targetWorld =
                state->TargetHadTransform
                    ? TransformPointsToWorld(state->TargetLocalPoints,
                                             state->TargetBeforeTransform)
                    : state->TargetLocalPoints;

            const glm::vec3 prealignDelta =
                ComputePointCentroid(std::span<const glm::vec3>(targetWorld)) -
                ComputePointCentroid(std::span<const glm::vec3>(sourceWorld));
            std::vector<glm::vec3> prealignedSourceWorld = sourceWorld;
            for (glm::vec3& point : prealignedSourceWorld)
                point += prealignDelta;
            glm::mat4 prealignPose(1.0f);
            prealignPose[3] = glm::vec4(prealignDelta, 1.0f);

            Reg::RegistrationParams params{};
            params.Variant = ToGeometryICPVariant(state->Command.Variant);
            params.MaxIterations = state->Command.MaxIterations;
            params.MaxCorrespondenceDistance =
                state->Command.MaxCorrespondenceDistance > 0.0
                    ? state->Command.MaxCorrespondenceDistance
                    : 1.0e6;
            params.InlierRatio = state->Command.InlierRatio;

            const RegistrationAlignmentOutcome outcome =
                AlignPointClouds(prealignedSourceWorld, targetWorld, {}, params);
            if (!outcome.HasResult)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "ICP rejected the selected point clouds (fewer than 3 points or invalid parameters).";
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = result.Message,
                };
            }

            result.HasResult = true;
            result.IterationsPerformed = outcome.Result.IterationsPerformed;
            result.TrajectoryLength = outcome.IterationCount();
            result.FinalRMSE = outcome.Result.FinalRMSE;
            result.Converged = outcome.Result.Converged;
            result.FinalInlierCount = outcome.Result.FinalInlierCount;

            const std::size_t step =
                std::min(state->Command.TrajectoryStep,
                         outcome.IterationCount());
            result.AppliedStep = step;
            const glm::mat4 pose =
                step == 0u ? glm::mat4(1.0f)
                           : TrajectoryPose(outcome, step) * prealignPose;

            state->SourceAfterTransform = state->SourceBeforeTransform;
            DecomposeModelToTransform(
                pose * ModelMatrixFromTransform(state->SourceBeforeTransform),
                state->SourceAfterTransform);

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = "ICP registration CPU result ready",
            };
        }

        [[nodiscard]] Core::Result PublishRegistrationCpuJob(
            const SandboxEditorContext& context,
            SandboxEditorRegistrationCpuJobState& job)
        {
            SandboxEditorRegistrationResult result = job.Result;
            if (!result.Succeeded())
            {
                PublishRegistrationResultSink(context, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (context.Scene == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "ICP registration requires an attached scene.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> sourceEntity =
                ResolveStableEntity(raw, job.SourceStableEntityId);
            if (!sourceEntity.has_value())
            {
                result.Status = SandboxEditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "ICP registration source entity is stale or no longer live.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            ECSC::Transform::Component* transform =
                raw.try_get<ECSC::Transform::Component>(*sourceEntity);
            if (transform == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::MissingTransform;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "ICP registration source entity has no Transform to drive.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            if (context.CommandHistory != nullptr)
            {
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        MakeTransformEditCommand(
                            EditorTransformEditCommand{
                                .Scene = context.Scene,
                                .StableEntityId = job.SourceStableEntityId,
                                .Before = job.SourceBeforeTransform,
                                .After = job.SourceAfterTransform,
                                .Label = "Align point clouds (ICP)",
                            }));
                result.Status = ToSandboxEditorCommandStatus(history.Status);
            }
            else
            {
                *transform = job.SourceAfterTransform;
                raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(
                    *sourceEntity);
                result.Status = SandboxEditorCommandStatus::Applied;
            }

            if (result.Status != SandboxEditorCommandStatus::Applied)
            {
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "ICP registration pose failed during editor history commit.";
                PublishRegistrationResultSink(context, result);
                return Core::Err(result.Error);
            }

            result.Error = Core::ErrorCode::Success;
            result.Message = BuildRegistrationSuccessMessage(result);
            PublishRegistrationResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] DerivedJobDesc MakeRegistrationCpuJobDesc(
            const SandboxEditorContext& context,
            const std::shared_ptr<SandboxEditorRegistrationCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SourceLocalPoints.size(),
                                  state->TargetLocalPoints.size()) +
                         1023u) /
                        1024u));
            return DerivedJobDesc{
                .Key = DerivedJobKey{
                    .EntityId = state->SourceStableEntityId,
                    .Domain = ProgressiveGeometryDomain::Point,
                    .OutputSemantic = ProgressiveSlotSemantic::Displacement,
                    .SourcePropertyGeneration =
                        state->SourceGeometryMetadataSignature,
                    .BindingGeneration =
                        state->TargetGeometryMetadataSignature,
                    .OutputName = "registration_transform",
                },
                .Name = "Sandbox.RegistrationICP.CPU",
                .RequestedJobDomain = ProgressiveJobDomain::Cpu,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = estimatedCost,
                .Scope = context.World,
                .Execute =
                    [state]() -> DerivedJobWorkerResult
                    {
                        return RunRegistrationCpuWorker(state);
                    },
                .ValidateOnMainThread =
                    [context, state]()
                    {
                        return ValidateRegistrationCpuJobApply(context, *state);
                    },
                .ApplyOnMainThread =
                    [context, state](DerivedJobApplyContext&) -> Core::Result
                    {
                        return PublishRegistrationCpuJob(context, *state);
                    },
            };
        }

        [[nodiscard]] SandboxEditorRegistrationResult
        SubmitRegistrationCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorRegistrationCommand& command,
            std::vector<glm::vec3> sourcePoints,
            std::vector<glm::vec3> targetPoints,
            const ECSC::Transform::Component& sourceTransform,
            const ECSC::Transform::Component* targetTransform,
            const std::uint64_t sourceGeometryMetadataSignature,
            const std::uint64_t targetGeometryMetadataSignature)
        {
            auto state =
                std::make_shared<SandboxEditorRegistrationCpuJobState>();
            state->SourceStableEntityId = command.SourceStableEntityId;
            state->TargetStableEntityId = command.TargetStableEntityId;
            state->SourceGeometryMetadataSignature =
                sourceGeometryMetadataSignature;
            state->TargetGeometryMetadataSignature =
                targetGeometryMetadataSignature;
            state->Command = command;
            state->SourceLocalPoints = std::move(sourcePoints);
            state->TargetLocalPoints = std::move(targetPoints);
            state->SourceBeforeTransform = sourceTransform;
            if (targetTransform != nullptr)
            {
                state->TargetHadTransform = true;
                state->TargetBeforeTransform = *targetTransform;
            }
            state->Result = MakeRegistrationBaseResult(command);
            state->Result.SourcePointCount = state->SourceLocalPoints.size();
            state->Result.TargetPointCount = state->TargetLocalPoints.size();

            DerivedJobDesc desc = MakeRegistrationCpuJobDesc(context, state);
            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorRegistrationResult pending =
                    MakePendingRegistrationResult(
                        command,
                        state->SourceLocalPoints.size(),
                        state->TargetLocalPoints.size(),
                        active->Handle);
                pending.Message =
                    BuildActiveDerivedJobMessage("ICP registration CPU", *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                SandboxEditorRegistrationResult result =
                    MakeRegistrationBaseResult(command);
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.SourcePointCount = state->SourceLocalPoints.size();
                result.TargetPointCount = state->TargetLocalPoints.size();
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "ICP registration CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingRegistrationResult(
                command,
                state->SourceLocalPoints.size(),
                state->TargetLocalPoints.size(),
                handle);
        }

        [[nodiscard]] SandboxEditorGeometryProcessingModel BuildGeometryProcessingModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorGeometryProcessingModel model{};
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for processing controls.");
                return model;
            }
            if (context.Selection == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingSelectionController,
                              "Selection controller is unavailable for processing controls.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for processing controls.");
                return model;
            }

            model.HasSelectedEntity = true;
            model.Capabilities =
                GetSandboxEditorGeometryProcessingCapabilities(
                    *context.Scene,
                    *selected);
            model.Entries =
                ResolveSandboxEditorGeometryProcessingEntries(model.Capabilities);
            model.KMeansDomains =
                GetAvailableSandboxEditorKMeansDomains(*context.Scene, *selected);
            model.MeshDenoiseAvailable =
                context.MeshDenoiseKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            model.MeshCurvatureAvailable =
                context.MeshCurvatureKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
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
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            model.MeshVertexNormalsAvailable =
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            model.GraphVertexNormalsAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::GraphVertices);
            model.PointCloudVertexNormalsAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::PointCloudPoints);
            model.PointCloudOutlierRemovalAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::PointCloudPoints);
            model.PointCloudProgressivePoissonAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::PointCloudPoints);
            model.MeshProgressivePoissonAvailable =
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            if (context.LastKMeansResult != nullptr)
            {
                model.LastKMeansResult = *context.LastKMeansResult;
                if (!context.LastKMeansResult->Succeeded() &&
                    context.LastKMeansResult->Status !=
                        SandboxEditorCommandStatus::Pending)
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
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
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastProgressivePoissonResult->Message.empty()
                            ? "Last progressive-Poisson command failed."
                            : context.LastProgressivePoissonResult->Message);
                }
            }
            if (!model.Capabilities.HasAny())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                              "Selected entity has no supported GeometrySources processing domain.");
            }
            return model;
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
            ScopedSandboxEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->InspectorModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->InspectorModelBuilds;
            }
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
            SandboxEditorSelectedAnalysisModel selectedAnalysis =
                BuildSelectedAnalysisModel(
                    context,
                    raw,
                    *selected,
                    GS::BuildConstView(raw, *selected),
                    model.RenderHints,
                    model.Geometry,
                    model.Entity.StableEntityId);
            model.PropertyCatalog = std::move(selectedAnalysis.PropertyCatalog);
            model.Progressive = std::move(selectedAnalysis.Progressive);
            model.BoundState = std::move(selectedAnalysis.BoundState);
            model.TextureBake = std::move(selectedAnalysis.TextureBake);
            model.Processing =
                GetSandboxEditorGeometryProcessingCapabilities(
                    *context.Scene,
                    *selected);

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
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->SelectionModelBuilds;
            }
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

        [[nodiscard]] SandboxEditorDocumentModel BuildDocumentModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorDocumentModel model{};
            if (context.CommandHistory == nullptr)
            {
                model.StatusText =
                    "Document history is disabled: runtime command history is unavailable.";
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::EditorCommandHistoryUnavailable,
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

        [[nodiscard]] SandboxEditorSceneFileModel BuildSceneFileModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorSceneFileModel model{};
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
                              SandboxEditorDiagnosticCode::SceneFileUnavailable,
                              model.StatusText);
            }
            if (context.LastSceneFileResult != nullptr)
            {
                model.LastResult = *context.LastSceneFileResult;
                if (!context.LastSceneFileResult->Message.empty())
                    model.StatusText = context.LastSceneFileResult->Message;
                if (!context.LastSceneFileResult->Succeeded() &&
                    context.LastSceneFileResult->Status !=
                        SandboxEditorCommandStatus::Pending)
                {
                    AddDiagnostic(model.Diagnostics,
                                  SandboxEditorDiagnosticCode::SceneFileFailed,
                                  model.StatusText);
                }
            }
            return model;
        }

        [[nodiscard]] SandboxEditorFileImportModel BuildFileImportModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorFileImportModel model{};
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
                        SandboxEditorDiagnosticCode::AssetImportUnavailable,
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
                        SandboxEditorCommandStatus::Pending)
                {
                    AddDiagnostic(model.Diagnostics,
                                  SandboxEditorDiagnosticCode::AssetImportFailed,
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

        [[nodiscard]] SandboxEditorAssetImportQueueRow
        BuildAssetImportQueueRow(
            const RuntimeAssetImportQueueEntry& entry,
            const RuntimeAssetImportQueueTimePoint now)
        {
            SandboxEditorAssetImportQueueRow row{};
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

        [[nodiscard]] SandboxEditorAssetImportQueueModel
        BuildAssetImportQueueModel(const SandboxEditorContext& context)
        {
            SandboxEditorAssetImportQueueModel model{};
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
                              SandboxEditorDiagnosticCode::AssetImportUnavailable,
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

        [[nodiscard]] SandboxEditorRenderGraphModel BuildRenderGraphModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorRenderGraphModel model{};
            if (context.RenderGraphStats == nullptr)
            {
                model.StatusText =
                    "Frame graph diagnostics are disabled: renderer stats are unavailable.";
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::RenderGraphStatsUnavailable,
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
                    SandboxEditorRenderGraphPassModel{
                        .Name = pass.Name,
                        .HasTypedId = pass.Id.IsValid(),
                        .TypedId = pass.Id.Value,
                        .Status = RenderCommandStatusName(pass.Status),
                    });
            }
            return model;
        }

        [[nodiscard]] SandboxEditorCameraRenderModel BuildCameraRenderModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorCameraRenderModel model{};
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
                              SandboxEditorDiagnosticCode::CameraRenderCommandsUnavailable,
                              "Camera/render setting command seams are unavailable.");
            }
            return model;
        }

        [[nodiscard]] SandboxEditorVisualizationModel BuildVisualizationModel(
            const SandboxEditorContext& context,
            const SandboxEditorVisualizationTarget target =
                SandboxEditorVisualizationTarget::Entity)
        {
            ScopedSandboxEditorStatTimer timer{
                context.ModelBuildStats != nullptr
                    ? &context.ModelBuildStats->VisualizationModelBuildTimeNs
                    : nullptr};
            if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->VisualizationModelBuilds;
            }
            SandboxEditorVisualizationModel model{};
            model.GeometryDomainControlsAvailable = context.VisualizationCommandsAvailable;
            model.AdapterBindingControlsAvailable =
                context.VisualizationCommandsAvailable &&
                context.VisualizationAdapterBindings.Available();
            model.Target = target;
            if (!context.VisualizationCommandsAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable,
                              "Visualization command seams are unavailable.");
                return model;
            }
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for visualization controls.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
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
                target == SandboxEditorVisualizationTarget::Entity
                    ? BuildVisualizationProperties(availability)
                    : [&availability, target]()
                      {
                          std::vector<SandboxEditorVisualizationPropertyInfo> out{};
                          AppendVisualizationPropertiesForTarget(
                              out,
                              availability,
                              target);
                          return out;
                      }();

            if (const auto* binding =
                    raw.try_get<ECSC::SpatialDebugBinding>(*selected);
                binding != nullptr)
            {
                model.SpatialDebug = FromSpatialDebugBinding(*binding);
            }

            model.Visualization =
                BuildVisualizationConfigModelForTarget(raw, *selected, target);
            if (model.AdapterBindingControlsAvailable)
            {
                const std::optional<RenderExtractionCache::VisualizationAdapterBinding>
                    binding =
                    context.VisualizationAdapterBindings.GetBinding(model.SelectedStableId);
                if (binding.has_value())
                {
                    model.AdapterBinding =
                        FromVisualizationAdapterBinding(*binding);
                }
            }
            return model;
        }

        [[nodiscard]] SandboxEditorVisualizationModelCacheEntry*
        ResolveVisualizationCacheEntry(
            SandboxEditorSelectedModelCache& cache,
            const SandboxEditorVisualizationTarget target)
        {
            const std::size_t index = static_cast<std::size_t>(target);
            return index < cache.Visualization.size()
                ? &cache.Visualization[index]
                : nullptr;
        }

        [[nodiscard]] SandboxEditorVisualizationModel BuildCachedVisualizationModel(
            const SandboxEditorContext& context,
            const SandboxEditorVisualizationTarget target =
                SandboxEditorVisualizationTarget::Entity)
        {
            SandboxEditorSelectedModelCache* cache = context.SelectedModelCache;
            if (cache == nullptr || !context.VisualizationCommandsAvailable ||
                context.Scene == nullptr)
            {
                return BuildVisualizationModel(context, target);
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
                return BuildVisualizationModel(context, target);

            SandboxEditorVisualizationModelCacheEntry* entry =
                ResolveVisualizationCacheEntry(*cache, target);
            if (entry == nullptr)
                return BuildVisualizationModel(context, target);

            const entt::registry& raw = context.Scene->Raw();
            const SandboxEditorGeometryDomainModel geometry =
                BuildGeometryDomainModel(raw, *selected);
            const SandboxEditorSelectedModelCacheKey key =
                BuildSelectedModelCacheKey(
                    context,
                    raw,
                    *selected,
                    geometry,
                    SandboxEditorSelectedModelCacheSection::Visualization,
                    SandboxEditorSelectedAnalysisCacheConsumer::Inspector,
                    target);

            if (entry->Valid && entry->Key == key)
            {
                RecordVisualizationCacheHit(context);
                return entry->Model;
            }

            RecordVisualizationCacheMiss(context);
            SandboxEditorVisualizationModel model =
                BuildVisualizationModel(context, target);
            *entry = SandboxEditorVisualizationModelCacheEntry{
                .Valid = true,
                .Key = key,
                .Model = model,
            };
            return model;
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
                return ConfigMode::Texture;
            }
            return ConfigMode::Grid;
        }

        [[nodiscard]] SandboxEditorParameterizationUvViewStatus
        ToSandboxUvViewStatus(const Graphics::UvViewStatus status) noexcept
        {
            using SandboxStatus =
                SandboxEditorParameterizationUvViewStatus;
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

        [[nodiscard]] SandboxEditorParameterizationUvViewState
        SubmitEngineParameterizationUvView(
            Engine& engine,
            const RenderExtractionCache* renderExtraction,
            SandboxEditorParameterizationUvViewRequest request)
        {
            using ConfigBackground = ParameterizationUvBackgroundMode;
            using ConfigMode = ParameterizationUvRenderMode;
            using SandboxStatus =
                SandboxEditorParameterizationUvViewStatus;

            SandboxEditorParameterizationUvViewState state{
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
                engine.GetRenderer().SubmitUvViewRequest(
                    std::move(disabledRequest));
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
            if (renderExtraction != nullptr &&
                request.View.BackgroundMode == ConfigBackground::Texture)
            {
                const auto bindings =
                    renderExtraction->GetMaterialTextureAssetBindings(
                        request.StableEntityId);
                if (bindings.has_value() && bindings->Albedo.IsValid())
                {
                    const auto view =
                        engine.GetGpuAssetCache().GetView(bindings->Albedo);
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
                .Background =
                    ToGraphicsUvViewBackground(request.View.BackgroundMode),
                .BackgroundTexture = backgroundTexture,
                .ShowDistortionHeatmap =
                    request.View.ShowDistortionHeatmap,
                .LineIndices = std::move(request.LineIndices),
                .TriangleConformalDistortion =
                    std::move(request.TriangleConformalDistortion),
            };
            engine.GetRenderer().SubmitUvViewRequest(
                std::move(graphicsRequest));

            if (!engine.GetDevice().IsOperational())
            {
                state.Status = SandboxStatus::CpuFallbackNonOperational;
                state.Message =
                    "GPU UV view is unavailable because the render device is not operational; CPU layout is active.";
                return state;
            }
            if (!geometry.has_value())
            {
                state.Status = SandboxStatus::WaitingForGeometry;
                state.Message =
                    "Selected mesh GPU surface residency is not ready; CPU layout is active.";
                return state;
            }

            const Graphics::UvViewOutput output =
                engine.GetRenderer().GetUvViewOutput();
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
                state.Message =
                    "GPU UV view target is not yet ready for this pane extent; CPU layout is active.";
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

        [[nodiscard]] SandboxEditorContext BuildContextFromEngine(Engine& engine)
        {
            const RenderExtractionCache* renderExtraction =
                engine.Services().Find<RenderExtractionCache>();
            DerivedJobRegistry* derivedJobs =
                engine.Services().Find<DerivedJobRegistry>();
            EngineConfigControl* configControl =
                engine.Services().Find<EngineConfigControl>();
            const auto activeWorld = engine.ActiveWorld();
            SandboxEditorDerivedJobCommandSurface derivedJobCommands{};
            if (derivedJobs != nullptr)
            {
                derivedJobCommands.Submit =
                    [derivedJobs, activeWorld](DerivedJobDesc desc)
                    {
                        desc.Scope = activeWorld;
                        return derivedJobs->Submit(std::move(desc));
                    };
                derivedJobCommands.Cancel =
                    [derivedJobs](const DerivedJobHandle handle)
                    {
                        derivedJobs->Cancel(handle);
                    };
            }
            SandboxEditorContext context{
                .Scene = &engine.GetScene(),
                .World = activeWorld,
                .Selection = &engine.GetSelectionController(),
                .CommandHistory = &engine.GetEditorCommandHistory(),
                .AssetService = &engine.GetAssetService(),
                .LastRefinedPrimitive = &engine.GetLastRefinedPrimitiveSelection(),
                .LastRefinedPrimitiveGeneration =
                    engine.GetLastRefinedPrimitiveSelectionGeneration(),
                .CameraControllers = &engine.GetCameraControllerRegistry(),
                .CameraViewport = Core::Extent2D{
                    engine.GetWindow().GetFramebufferExtent().Width,
                    engine.GetWindow().GetFramebufferExtent().Height},
                .Device = &engine.GetDevice(),
                .AssetImportCommands = SandboxEditorAssetImportCommandSurface{
                    .Import =
                        [&engine](const SandboxEditorFileImportCommand& command)
                        {
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
                                const RuntimeAssetImportRequest request{
                                    .Path = command.Path,
                                    .PayloadKind = route->PayloadKind,
                                };
                                auto queued = Assets::IsGeometryPayloadKind(
                                                  route->PayloadKind)
                                    ? engine.GetAssetImportPipeline().QueueGeometryImport(
                                          request)
                                    : engine.GetAssetImportPipeline().QueueModelTextureImport(
                                          request);
                                if (!queued.has_value())
                                {
                                    return SandboxEditorFileImportResult{
                                        .Status = SandboxEditorCommandStatus::AssetImportFailed,
                                        .PayloadKind = route->PayloadKind,
                                        .Error = queued.error(),
                                        .Message = BuildImportFailureMessage(queued.error()),
                                    };
                                }

                                return SandboxEditorFileImportResult{
                                    .Status = SandboxEditorCommandStatus::Pending,
                                    .Operation = queued->Operation,
                                    .PayloadKind = queued->PayloadKind,
                                    .Error = Core::ErrorCode::Success,
                                    .Message = BuildImportPendingMessage(
                                        command,
                                        queued->PayloadKind),
                                };
                            }

                            auto imported = engine.GetAssetImportPipeline().ImportAssetFromPath(
                                RuntimeAssetImportRequest{
                                    .Path = command.Path,
                                    .PayloadKind = command.PayloadKind,
                                });
                            if (!imported.has_value())
                            {
                                return SandboxEditorFileImportResult{
                                    .Status = SandboxEditorCommandStatus::AssetImportFailed,
                                    .PayloadKind = command.PayloadKind,
                                    .Error = imported.error(),
                                    .Message = BuildImportFailureMessage(imported.error()),
                                };
                            }

                            SandboxEditorFileImportResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Asset = imported->Asset,
                                .PayloadKind = imported->PayloadKind,
                                .PrimitiveEntitiesCreated =
                                    imported->PrimitiveEntitiesCreated,
                                .EmbeddedTextureAssetsCreated =
                                    imported->EmbeddedTextureAssetsCreated,
                                .GeneratedTextureAssetsCreated =
                                    imported->GeneratedTextureAssetsCreated,
                                .TextureUploadRequests =
                                    imported->TextureUploadRequests,
                                .GeneratedTextureUploadRequests =
                                    imported->GeneratedTextureUploadRequests,
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
                .AssetImportQueueCommands = SandboxEditorAssetImportQueueCommandSurface{
                    .ClearCompleted =
                        [&engine]()
                        {
                            return engine.GetAssetImportPipeline().ClearCompletedAssetImports();
                        },
                    .Cancel =
                        [&engine](const RuntimeAssetIngestHandle operation)
                        {
                            return engine.GetAssetImportPipeline().CancelAssetImport(operation);
                        },
                },
                .SceneFileCommands = SandboxEditorSceneFileCommandSurface{
                    .New =
                        [&engine]()
                        {
                            Core::Result created =
                                engine.GetSceneDocument().NewSceneDocument();
                            if (!created.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneNewFailed,
                                    .Operation = SandboxEditorSceneFileOperation::New,
                                    .Error = created.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::New,
                                        created.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Operation = SandboxEditorSceneFileOperation::New,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                    .Save =
                        [&engine](const SandboxEditorSceneFileCommand& command)
                        {
                            auto queued =
                                engine.GetSceneDocument().QueueSceneSaveToPath(
                                    command.Path);
                            if (!queued.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneSaveFailed,
                                    .Operation = SandboxEditorSceneFileOperation::Save,
                                    .Error = queued.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::Save,
                                        queued.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Pending,
                                .Operation = SandboxEditorSceneFileOperation::Save,
                                .Task = queued->Task,
                                .Error = Core::ErrorCode::Success,
                            };
                            result.Message = BuildSceneFilePendingMessage(
                                command,
                                result.Operation);
                            return result;
                        },
                    .Load =
                        [&engine](const SandboxEditorSceneFileCommand& command)
                        {
                            auto queued =
                                engine.GetSceneDocument().QueueSceneLoadFromPath(
                                    command.Path);
                            if (!queued.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneLoadFailed,
                                    .Operation = SandboxEditorSceneFileOperation::Load,
                                    .Error = queued.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::Load,
                                        queued.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Pending,
                                .Operation = SandboxEditorSceneFileOperation::Load,
                                .Task = queued->Task,
                                .Error = Core::ErrorCode::Success,
                            };
                            result.Message = BuildSceneFilePendingMessage(
                                command,
                                result.Operation);
                            return result;
                        },
                    .Close =
                        [&engine]()
                        {
                            Core::Result closed =
                                engine.GetSceneDocument().CloseSceneDocument();
                            if (!closed.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneCloseFailed,
                                    .Operation = SandboxEditorSceneFileOperation::Close,
                                    .Error = closed.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::Close,
                                        closed.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Operation = SandboxEditorSceneFileOperation::Close,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                },
                .ParameterizationUvViewCommands =
                    SandboxEditorParameterizationUvViewCommandSurface{
                        .Submit =
                            [&engine, renderExtraction](
                                SandboxEditorParameterizationUvViewRequest request)
                            {
                                return SubmitEngineParameterizationUvView(
                                    engine,
                                    renderExtraction,
                                    std::move(request));
                            },
                    },
                .VisualizationAdapterBindings = SandboxEditorVisualizationAdapterBindingCommandSurface{
                    .GetBinding =
                        [&engine](const std::uint32_t stableEntityId)
                        {
                            return engine.GetVisualizationAdapterBinding(stableEntityId);
                        },
                    .SetBinding =
                        [&engine](
                            const std::uint32_t stableEntityId,
                            RenderExtractionCache::VisualizationAdapterBinding binding)
                        {
                            engine.SetVisualizationAdapterBinding(
                                stableEntityId,
                                std::move(binding));
                        },
                    .ClearBinding =
                        [&engine](const std::uint32_t stableEntityId)
                        {
                            engine.ClearVisualizationAdapterBinding(stableEntityId);
                        },
                },
                .VisualizationAdapterBindingRevision =
                    engine.GetVisualizationAdapterBindingRevision(),
                .DerivedJobCommands = std::move(derivedJobCommands),
                .AssetImportQueue = engine.GetAssetImportPipeline().GetAssetImportQueueSnapshot(),
                .RenderGraphStats = &engine.GetRenderer().GetLastRenderGraphStats(),
                .ImGuiAdapterAvailable =
                    [&engine]
                    {
                        const EditorUiHost* host =
                            engine.Services().Find<EditorUiHost>();
                        return host != nullptr && host->IsOperational();
                    }(),
                .AssetImportCommandsAvailable = true,
                .SceneFileCommandsAvailable = true,
                .CameraRenderCommandsAvailable = true,
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

        [[nodiscard]] bool AttachmentEpochIsActive(
            const std::shared_ptr<std::atomic_bool>& epoch) noexcept
        {
            return epoch != nullptr &&
                epoch->load(std::memory_order_acquire);
        }

        template <typename Command, typename Fallback>
        [[nodiscard]] auto GuardAttachmentCommand(
            Command command,
            std::shared_ptr<std::atomic_bool> epoch,
            Fallback fallback)
        {
            return [command = std::move(command),
                    epoch = std::move(epoch),
                    fallback = std::move(fallback)](
                       auto&&... args) mutable -> decltype(auto)
            {
                if (!AttachmentEpochIsActive(epoch))
                {
                    return fallback(
                        std::forward<decltype(args)>(args)...);
                }
                return command(std::forward<decltype(args)>(args)...);
            };
        }

        void GuardAttachmentCommandSurfaces(
            SandboxEditorContext& context,
            const std::shared_ptr<std::atomic_bool>& epoch)
        {
            context.AssetImportCommands.Import = GuardAttachmentCommand(
                std::move(context.AssetImportCommands.Import),
                epoch,
                [](const SandboxEditorFileImportCommand& command)
                {
                    return SandboxEditorFileImportResult{
                        .Status = SandboxEditorCommandStatus::AssetImportFailed,
                        .PayloadKind = command.PayloadKind,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Asset import failed: editor session attachment expired.",
                    };
                });
            context.AssetImportQueueCommands.ClearCompleted =
                GuardAttachmentCommand(
                    std::move(
                        context.AssetImportQueueCommands.ClearCompleted),
                    epoch,
                    []()
                    {
                        return std::size_t{0u};
                    });
            context.AssetImportQueueCommands.Cancel = GuardAttachmentCommand(
                std::move(context.AssetImportQueueCommands.Cancel),
                epoch,
                [](const RuntimeAssetIngestHandle)
                {
                    return Core::Err(Core::ErrorCode::InvalidState);
                });
            context.SceneFileCommands.New = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.New),
                epoch,
                []()
                {
                    return SandboxEditorSceneFileResult{
                        .Status = SandboxEditorCommandStatus::SceneNewFailed,
                        .Operation = SandboxEditorSceneFileOperation::New,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "New scene failed: editor session attachment expired.",
                    };
                });
            context.SceneFileCommands.Save = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.Save),
                epoch,
                [](const SandboxEditorSceneFileCommand&)
                {
                    return SandboxEditorSceneFileResult{
                        .Status = SandboxEditorCommandStatus::SceneSaveFailed,
                        .Operation = SandboxEditorSceneFileOperation::Save,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Scene save failed: editor session attachment expired.",
                    };
                });
            context.SceneFileCommands.Load = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.Load),
                epoch,
                [](const SandboxEditorSceneFileCommand&)
                {
                    return SandboxEditorSceneFileResult{
                        .Status = SandboxEditorCommandStatus::SceneLoadFailed,
                        .Operation = SandboxEditorSceneFileOperation::Load,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Scene load failed: editor session attachment expired.",
                    };
                });
            context.SceneFileCommands.Close = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.Close),
                epoch,
                []()
                {
                    return SandboxEditorSceneFileResult{
                        .Status = SandboxEditorCommandStatus::SceneCloseFailed,
                        .Operation = SandboxEditorSceneFileOperation::Close,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Scene close failed: editor session attachment expired.",
                    };
                });
            context.ParameterizationUvViewCommands.Submit =
                GuardAttachmentCommand(
                    std::move(
                        context.ParameterizationUvViewCommands.Submit),
                    epoch,
                    [](SandboxEditorParameterizationUvViewRequest request)
                    {
                        return SandboxEditorParameterizationUvViewState{
                            .Status =
                                SandboxEditorParameterizationUvViewStatus::CpuFallbackNonOperational,
                            .RequestedMode = request.View.RenderMode,
                            .ActiveMode =
                                ParameterizationUvRenderMode::CpuLayout,
                            .RequestedBackground =
                                request.View.BackgroundMode,
                            .ActiveBackground =
                                request.View.BackgroundMode ==
                                            ParameterizationUvBackgroundMode::Grid ||
                                        request.View.BackgroundMode ==
                                            ParameterizationUvBackgroundMode::Checker
                                    ? request.View.BackgroundMode
                                    : ParameterizationUvBackgroundMode::Checker,
                            .RequestToken = request.RequestToken,
                            .Width = request.Width,
                            .Height = request.Height,
                            .Message =
                                "GPU UV view command failed because the editor session attachment expired.",
                        };
                    });
            context.VisualizationAdapterBindings.GetBinding =
                GuardAttachmentCommand(
                    std::move(
                        context.VisualizationAdapterBindings.GetBinding),
                    epoch,
                    [](const std::uint32_t)
                    {
                        return std::optional<
                            RenderExtractionCache::
                                VisualizationAdapterBinding>{};
                    });
            context.VisualizationAdapterBindings.SetBinding =
                GuardAttachmentCommand(
                    std::move(
                        context.VisualizationAdapterBindings.SetBinding),
                    epoch,
                    [](const std::uint32_t,
                       RenderExtractionCache::VisualizationAdapterBinding)
                    {
                    });
            context.VisualizationAdapterBindings.ClearBinding =
                GuardAttachmentCommand(
                    std::move(
                        context.VisualizationAdapterBindings.ClearBinding),
                    epoch,
                    [](const std::uint32_t)
                    {
                    });
            if (context.DerivedJobCommands.Submit)
            {
                context.DerivedJobCommands.Submit = GuardAttachmentCommand(
                    std::move(context.DerivedJobCommands.Submit),
                    epoch,
                    [](DerivedJobDesc)
                    {
                        return DerivedJobHandle{};
                    });
            }
            if (context.DerivedJobCommands.Cancel)
            {
                context.DerivedJobCommands.Cancel = GuardAttachmentCommand(
                    std::move(context.DerivedJobCommands.Cancel),
                    epoch,
                    [](const DerivedJobHandle)
                    {
                    });
            }
            if (context.PreviewRenderRecipeDocument)
            {
                context.PreviewRenderRecipeDocument =
                    GuardAttachmentCommand(
                        std::move(
                            context.PreviewRenderRecipeDocument),
                        epoch,
                        [](const std::string&, const std::string&)
                        {
                            return Graphics::
                                RenderRecipeConfigLoadResult{};
                        });
            }
            if (context.ApplyRenderRecipePreview)
            {
                context.ApplyRenderRecipePreview =
                    GuardAttachmentCommand(
                        std::move(
                            context.ApplyRenderRecipePreview),
                        epoch,
                        [](const Graphics::
                               RenderRecipeConfigLoadResult&)
                        {
                            return RuntimeRenderRecipeApplyResult{
                                .Status =
                                    RuntimeRenderRecipeApplyStatus::
                                        Rejected,
                            };
                        });
            }
        }

    }

    namespace Detail
    {
        std::optional<ECS::EntityHandle> ResolveSandboxMethodStableEntity(
            const entt::registry& raw,
            const std::uint32_t stableId)
        {
            return ResolveStableEntity(raw, stableId);
        }

        std::uint64_t SandboxEditorGeometryMetadataSignatureForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            return GeometryMetadataSignatureForEntity(raw, entity);
        }

        SandboxEditorMeshSourceSnapshot BuildSandboxEditorMeshSourceSnapshot(
            const GS::ConstSourceView& view)
        {
            MeshDenoiseSourceResult source =
                BuildHalfedgeMeshForDenoise(view);
            return SandboxEditorMeshSourceSnapshot{
                .Mesh = std::move(source.Mesh),
                .BeforePositions = std::move(source.BeforePositions),
                .DeletedVertices = std::move(source.DeletedVertices),
                .SourceFaceForMeshFace =
                    std::move(source.SourceFaceForMeshFace),
                .Status = source.Status,
                .Error = source.Error,
                .Diagnostic = std::move(source.Diagnostic),
            };
        }

        void InvalidateSandboxMethodSelectedModelCache(
            const SandboxEditorContext& context)
        {
            InvalidateSelectedModelCache(context);
        }

        std::optional<DerivedJobSnapshot> FindActiveSandboxMethodDerivedJob(
            const SandboxEditorContext& context,
            const DerivedJobKey& key)
        {
            return FindActiveEditorDerivedJob(context, key);
        }

        std::string BuildActiveSandboxMethodDerivedJobMessage(
            const std::string_view label,
            const DerivedJobSnapshot& job)
        {
            return BuildActiveDerivedJobMessage(label, job);
        }

        EditorCommandHistoryStatus ApplySandboxMethodPointCloudPointState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::PointCloud::Cloud& cloud)
        {
            return ApplyPointCloudPointState(
                scene,
                stableEntityId,
                cloud);
        }

        SandboxEditorCommandStatus ToSandboxMethodCommandStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            return ToSandboxEditorCommandStatus(status);
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
        case SandboxEditorDiagnosticCode::AssetImportFailed:
            return "AssetImportFailed";
        case SandboxEditorDiagnosticCode::SceneFileUnavailable:
            return "SceneFileUnavailable";
        case SandboxEditorDiagnosticCode::SceneFileFailed:
            return "SceneFileFailed";
        case SandboxEditorDiagnosticCode::NoSelectedEntity:
            return "NoSelectedEntity";
        case SandboxEditorDiagnosticCode::UnsupportedGeometryDomain:
            return "UnsupportedGeometryDomain";
        case SandboxEditorDiagnosticCode::CameraRenderCommandsUnavailable:
            return "CameraRenderCommandsUnavailable";
        case SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable:
            return "VisualizationCommandsUnavailable";
        case SandboxEditorDiagnosticCode::RenderRecipeCommandsUnavailable:
            return "RenderRecipeCommandsUnavailable";
        case SandboxEditorDiagnosticCode::InvalidVisualizationProperty:
            return "InvalidVisualizationProperty";
        case SandboxEditorDiagnosticCode::InvalidVertexChannelBinding:
            return "InvalidVertexChannelBinding";
        case SandboxEditorDiagnosticCode::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        case SandboxEditorDiagnosticCode::CorruptHierarchy:
            return "CorruptHierarchy";
        case SandboxEditorDiagnosticCode::RenderGraphStatsUnavailable:
            return "RenderGraphStatsUnavailable";
        case SandboxEditorDiagnosticCode::EditorCommandHistoryUnavailable:
            return "EditorCommandHistoryUnavailable";
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
        case SandboxEditorCommandStatus::Pending:
            return "Pending";
        case SandboxEditorCommandStatus::NoChange:
            return "NoChange";
        case SandboxEditorCommandStatus::MissingScene:
            return "MissingScene";
        case SandboxEditorCommandStatus::MissingSelectionController:
            return "MissingSelectionController";
        case SandboxEditorCommandStatus::MissingCameraControllerRegistry:
            return "MissingCameraControllerRegistry";
        case SandboxEditorCommandStatus::MissingAssetImportCommands:
            return "MissingAssetImportCommands";
        case SandboxEditorCommandStatus::MissingSceneFileCommands:
            return "MissingSceneFileCommands";
        case SandboxEditorCommandStatus::MissingPrimitiveViewCommands:
            return "MissingPrimitiveViewCommands";
        case SandboxEditorCommandStatus::MissingVisualizationCommands:
            return "MissingVisualizationCommands";
        case SandboxEditorCommandStatus::AssetImportFailed:
            return "AssetImportFailed";
        case SandboxEditorCommandStatus::SceneNewFailed:
            return "SceneNewFailed";
        case SandboxEditorCommandStatus::SceneSaveFailed:
            return "SceneSaveFailed";
        case SandboxEditorCommandStatus::SceneLoadFailed:
            return "SceneLoadFailed";
        case SandboxEditorCommandStatus::SceneCloseFailed:
            return "SceneCloseFailed";
        case SandboxEditorCommandStatus::StaleEntity:
            return "StaleEntity";
        case SandboxEditorCommandStatus::MissingTransform:
            return "MissingTransform";
        case SandboxEditorCommandStatus::UnsupportedGeometryDomain:
            return "UnsupportedGeometryDomain";
        case SandboxEditorCommandStatus::InvalidVisualizationProperty:
            return "InvalidVisualizationProperty";
        case SandboxEditorCommandStatus::InvalidVertexChannelBinding:
            return "InvalidVertexChannelBinding";
        case SandboxEditorCommandStatus::InvalidProcessingParameters:
            return "InvalidProcessingParameters";
        case SandboxEditorCommandStatus::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorUvAtlasStatus(
        const Geometry::UvAtlas::UvAtlasStatus status) noexcept
    {
        return Geometry::UvAtlas::ToString(status);
    }

    const char* DebugNameForSandboxEditorUvAtlasProvenance(
        const Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept
    {
        return Geometry::UvAtlas::ToString(provenance);
    }

    Geometry::ConstPropertySet
    ResolveSandboxEditorSelectedMeshVertexProperties(
        const SandboxEditorContext& context)
    {
        const Geometry::PropertySet* properties =
            ResolveSelectedMeshVertexProperties(context);
        return properties != nullptr
            ? Geometry::ConstPropertySet(*properties)
            : Geometry::ConstPropertySet{};
    }

    const char* DebugNameForSandboxEditorAssetPayloadKind(
        const SandboxEditorAssetPayloadKind kind) noexcept
    {
        return A::DebugNameForAssetPayloadKind(kind);
    }

    std::string_view DebugNameForSandboxEditorRenderRecipeConfigState(
        const SandboxEditorRenderRecipeConfigState state) noexcept
    {
        return Graphics::ToString(state);
    }

    std::string_view
    DebugNameForSandboxEditorRenderRecipeConfigDiagnosticCode(
        const SandboxEditorRenderRecipeConfigDiagnosticCode code) noexcept
    {
        return Graphics::ToString(code);
    }

    const char* DebugNameForSandboxEditorRenderRecipeDraftState(
        const SandboxEditorRenderRecipeDraftState state) noexcept
    {
        using State = SandboxEditorRenderRecipeDraftState;
        switch (state)
        {
        case State::InactiveDraft:
            return "InactiveDraft";
        case State::Debounced:
            return "Debounced";
        case State::Validated:
            return "Validated";
        case State::Rejected:
            return "Rejected";
        case State::Previewed:
            return "Previewed";
        case State::Activated:
            return "Activated";
        case State::Canceled:
            return "Canceled";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorRenderRecipeCommandKind(
        const SandboxEditorRenderRecipeCommandKind kind) noexcept
    {
        using Kind = SandboxEditorRenderRecipeCommandKind;
        switch (kind)
        {
        case Kind::UpdateDraft:
            return "UpdateDraft";
        case Kind::ValidateDraft:
            return "ValidateDraft";
        case Kind::PreviewDraft:
            return "PreviewDraft";
        case Kind::ActivatePreview:
            return "ActivatePreview";
        case Kind::CancelDraft:
            return "CancelDraft";
        case Kind::PublishArtifact:
            return "PublishArtifact";
        case Kind::ApplyArtifact:
            return "ApplyArtifact";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorRenderRecipeCommandStatus(
        const SandboxEditorRenderRecipeCommandStatus status) noexcept
    {
        using Status = SandboxEditorRenderRecipeCommandStatus;
        switch (status)
        {
        case Status::NoChange:
            return "NoChange";
        case Status::DraftUpdated:
            return "DraftUpdated";
        case Status::Debounced:
            return "Debounced";
        case Status::Validated:
            return "Validated";
        case Status::ValidationFailed:
            return "ValidationFailed";
        case Status::Previewed:
            return "Previewed";
        case Status::PreviewFailed:
            return "PreviewFailed";
        case Status::Activated:
            return "Activated";
        case Status::Canceled:
            return "Canceled";
        case Status::Published:
            return "Published";
        case Status::Applied:
            return "Applied";
        case Status::ActivationFailed:
            return "ActivationFailed";
        case Status::MissingRecipeContext:
            return "MissingRecipeContext";
        case Status::MissingEditorState:
            return "MissingEditorState";
        case Status::MissingArtifactRegistry:
            return "MissingArtifactRegistry";
        case Status::ArtifactCommandFailed:
            return "ArtifactCommandFailed";
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

    const char* DebugNameForSandboxEditorDomainWindowKind(
        const SandboxEditorDomainWindowKind kind) noexcept
    {
        switch (kind)
        {
        case SandboxEditorDomainWindowKind::Mesh:
            return "Mesh";
        case SandboxEditorDomainWindowKind::Graph:
            return "Graph";
        case SandboxEditorDomainWindowKind::PointCloud:
            return "PointCloud";
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

    const char* DebugNameForSandboxEditorSpatialDebugKind(
        const ECSC::SpatialDebugGeometryKind kind) noexcept
    {
        switch (kind)
        {
        case ECSC::SpatialDebugGeometryKind::Bvh:
            return "Bvh";
        case ECSC::SpatialDebugGeometryKind::KdTree:
            return "KdTree";
        case ECSC::SpatialDebugGeometryKind::Octree:
            return "Octree";
        case ECSC::SpatialDebugGeometryKind::ConvexHull:
            return "ConvexHull";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationColorSource(
        const G::VisualizationConfig::ColorSource source) noexcept
    {
        switch (source)
        {
        case G::VisualizationConfig::ColorSource::Material:
            return "Material";
        case G::VisualizationConfig::ColorSource::UniformColor:
            return "UniformColor";
        case G::VisualizationConfig::ColorSource::ScalarField:
            return "ScalarField";
        case G::VisualizationConfig::ColorSource::PerVertexBuffer:
            return "PerVertexBuffer";
        case G::VisualizationConfig::ColorSource::PerEdgeBuffer:
            return "PerEdgeBuffer";
        case G::VisualizationConfig::ColorSource::PerFaceBuffer:
            return "PerFaceBuffer";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationDomain(
        const G::VisualizationConfig::Domain domain) noexcept
    {
        switch (domain)
        {
        case G::VisualizationConfig::Domain::Vertex:
            return "Vertex";
        case G::VisualizationConfig::Domain::Edge:
            return "Edge";
        case G::VisualizationConfig::Domain::Face:
            return "Face";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationAdapterBindingKind(
        const RenderExtractionCache::VisualizationAdapterBindingKind kind) noexcept
    {
        using Kind = RenderExtractionCache::VisualizationAdapterBindingKind;
        switch (kind)
        {
        case Kind::Scalar:
            return "Scalar";
        case Kind::Color:
            return "Color";
        case Kind::VectorField:
            return "VectorField";
        case Kind::Isoline:
            return "Isoline";
        case Kind::HtexMetadata:
            return "HtexMetadata";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationPropertyDomain(
        const SandboxEditorVisualizationPropertyDomain domain) noexcept
    {
        using Domain = SandboxEditorVisualizationPropertyDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return "MeshVertices";
        case Domain::MeshEdges:
            return "MeshEdges";
        case Domain::MeshFaces:
            return "MeshFaces";
        case Domain::GraphVertices:
            return "GraphVertices";
        case Domain::GraphEdges:
            return "GraphEdges";
        case Domain::PointCloudPoints:
            return "PointCloudPoints";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationPropertyValueKind(
        const SandboxEditorVisualizationPropertyValueKind kind) noexcept
    {
        using Kind = SandboxEditorVisualizationPropertyValueKind;
        switch (kind)
        {
        case Kind::ScalarFloat:
            return "ScalarFloat";
        case Kind::ScalarDouble:
            return "ScalarDouble";
        case Kind::Vec3:
            return "Vec3";
        case Kind::Vec4:
            return "Vec4";
        case Kind::UInt32:
            return "UInt32";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationPropertyPreset(
        const SandboxEditorVisualizationPropertyPreset preset) noexcept
    {
        using Preset = SandboxEditorVisualizationPropertyPreset;
        switch (preset)
        {
        case Preset::Scalar:
            return "Scalar";
        case Preset::Isoline:
            return "Isoline";
        case Preset::ColorBuffer:
            return "ColorBuffer";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationTarget(
        const SandboxEditorVisualizationTarget target) noexcept
    {
        using Target = SandboxEditorVisualizationTarget;
        switch (target)
        {
        case Target::Entity:
            return "Entity";
        case Target::Surface:
            return "Surface";
        case Target::Edges:
            return "Edges";
        case Target::Points:
            return "Points";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorPropertyCatalogDomain(
        const SandboxEditorPropertyCatalogDomain domain) noexcept
    {
        using Domain = SandboxEditorPropertyCatalogDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return "MeshVertices";
        case Domain::MeshEdges:
            return "MeshEdges";
        case Domain::MeshHalfedges:
            return "MeshHalfedges";
        case Domain::MeshFaces:
            return "MeshFaces";
        case Domain::GraphVertices:
            return "GraphVertices";
        case Domain::GraphEdges:
            return "GraphEdges";
        case Domain::PointCloudPoints:
            return "PointCloudPoints";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorPropertyCatalogValueKind(
        const SandboxEditorPropertyCatalogValueKind kind) noexcept
    {
        using Kind = SandboxEditorPropertyCatalogValueKind;
        switch (kind)
        {
        case Kind::Unknown:
            return "Unknown";
        case Kind::ScalarFloat:
            return "ScalarFloat";
        case Kind::ScalarDouble:
            return "ScalarDouble";
        case Kind::UInt32:
            return "UInt32";
        case Kind::Vec2:
            return "Vec2";
        case Kind::Vec3:
            return "Vec3";
        case Kind::Vec4:
            return "Vec4";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorBoundRenderStateRowKind(
        const SandboxEditorBoundRenderStateRowKind kind) noexcept
    {
        using Kind = SandboxEditorBoundRenderStateRowKind;
        switch (kind)
        {
        case Kind::RenderHint:
            return "RenderHint";
        case Kind::ProgressiveSlot:
            return "ProgressiveSlot";
        case Kind::DerivedJob:
            return "DerivedJob";
        case Kind::CompositionSummary:
            return "CompositionSummary";
        case Kind::DisabledCommand:
            return "DisabledCommand";
        }
        return "Unknown";
    }

    std::vector<SandboxEditorGeometryProcessingMenuItem>
    GetSandboxEditorGeometryProcessingMenuItems(
        const SandboxEditorDomainWindowKind kind)
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        switch (kind)
        {
        case SandboxEditorDomainWindowKind::Mesh:
            return {
                {.Domain = Domain::MeshVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true,
                 .HasDenoiseMethod = true,
                 .HasCurvatureMethod = true,
                 .HasRemeshMethod = true,
                 .HasSubdivideMethod = true,
                 .HasSimplifyMethod = true},
                {.Domain = Domain::MeshEdges, .Label = "Edges"},
                {.Domain = Domain::MeshFaces, .Label = "Faces"},
            };
        case SandboxEditorDomainWindowKind::Graph:
            return {
                {.Domain = Domain::GraphVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
                {.Domain = Domain::GraphEdges, .Label = "Edges"},
                {.Domain = Domain::GraphHalfedges, .Label = "Halfedges"},
            };
        case SandboxEditorDomainWindowKind::PointCloud:
            return {
                {.Domain = Domain::PointCloudPoints,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
            };
        }
        return {};
    }

    SandboxEditorGeometryProcessingDomain
    GetSandboxEditorSupportedGeometryProcessingDomains(
        const SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        switch (algorithm)
        {
        case SandboxEditorGeometryProcessingAlgorithm::KMeans:
            return Domain::MeshVertices |
                   Domain::GraphVertices |
                   Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::MeshDenoise:
        case SandboxEditorGeometryProcessingAlgorithm::Curvature:
            return Domain::MeshVertices;
        case SandboxEditorGeometryProcessingAlgorithm::Remeshing:
        case SandboxEditorGeometryProcessingAlgorithm::Simplification:
        case SandboxEditorGeometryProcessingAlgorithm::Smoothing:
        case SandboxEditorGeometryProcessingAlgorithm::Subdivision:
        case SandboxEditorGeometryProcessingAlgorithm::Repair:
            return kMeshTopologyDomains;
        case SandboxEditorGeometryProcessingAlgorithm::NormalEstimation:
            return Domain::MeshVertices |
                   Domain::GraphVertices |
                   Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::ShortestPath:
            return Domain::MeshVertices | Domain::GraphVertices;
        case SandboxEditorGeometryProcessingAlgorithm::ConvexHull:
            return Domain::MeshVertices | Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            return Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::VectorHeat:
            return Domain::MeshVertices;
        case SandboxEditorGeometryProcessingAlgorithm::Parameterization:
            return Domain::MeshVertices | Domain::MeshFaces;
        case SandboxEditorGeometryProcessingAlgorithm::BooleanCSG:
            return Domain::MeshVertices | Domain::MeshFaces;
        case SandboxEditorGeometryProcessingAlgorithm::Registration:
        case SandboxEditorGeometryProcessingAlgorithm::BilateralFilter:
        case SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation:
        case SandboxEditorGeometryProcessingAlgorithm::KernelDensity:
        case SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
        case SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return Domain::MeshVertices | Domain::PointCloudPoints;
        }
        return Domain::None;
    }

    bool SupportsSandboxEditorGeometryProcessingDomain(
        const SandboxEditorGeometryProcessingAlgorithm algorithm,
        const SandboxEditorGeometryProcessingDomain domain) noexcept
    {
        return HasAnySandboxEditorGeometryProcessingDomain(
            GetSandboxEditorSupportedGeometryProcessingDomains(algorithm),
            domain);
    }

    SandboxEditorGeometryProcessingCapabilities
    GetSandboxEditorGeometryProcessingCapabilities(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        SandboxEditorGeometryProcessingCapabilities capabilities{};
        const entt::registry& raw = registry.Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return capabilities;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        capabilities.Domains = DomainsForSourceView(view);
        capabilities.HasEditableSurfaceMesh =
            availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshVertex) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshHalfedge) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshFace);
        return capabilities;
    }

    std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        const SandboxEditorGeometryProcessingCapabilities capabilities)
    {
        static constexpr std::array<SandboxEditorGeometryProcessingAlgorithm, 22>
            kAlgorithmOrder{
                SandboxEditorGeometryProcessingAlgorithm::KMeans,
                SandboxEditorGeometryProcessingAlgorithm::NormalEstimation,
                SandboxEditorGeometryProcessingAlgorithm::MeshDenoise,
                SandboxEditorGeometryProcessingAlgorithm::Curvature,
                SandboxEditorGeometryProcessingAlgorithm::Registration,
                SandboxEditorGeometryProcessingAlgorithm::BilateralFilter,
                SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation,
                SandboxEditorGeometryProcessingAlgorithm::KernelDensity,
                SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling,
                SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval,
                SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval,
                SandboxEditorGeometryProcessingAlgorithm::ShortestPath,
                SandboxEditorGeometryProcessingAlgorithm::VectorHeat,
                SandboxEditorGeometryProcessingAlgorithm::Parameterization,
                SandboxEditorGeometryProcessingAlgorithm::ConvexHull,
                SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction,
                SandboxEditorGeometryProcessingAlgorithm::BooleanCSG,
                SandboxEditorGeometryProcessingAlgorithm::Remeshing,
                SandboxEditorGeometryProcessingAlgorithm::Simplification,
                SandboxEditorGeometryProcessingAlgorithm::Smoothing,
                SandboxEditorGeometryProcessingAlgorithm::Subdivision,
                SandboxEditorGeometryProcessingAlgorithm::Repair,
            };

        std::vector<SandboxEditorGeometryProcessingEntry> entries{};
        entries.reserve(kAlgorithmOrder.size());
        for (const SandboxEditorGeometryProcessingAlgorithm algorithm :
             kAlgorithmOrder)
        {
            if (IsSurfaceTopologyAlgorithm(algorithm) &&
                !capabilities.HasEditableSurfaceMesh)
            {
                continue;
            }

            const SandboxEditorGeometryProcessingDomain domains =
                capabilities.Domains &
                GetSandboxEditorSupportedGeometryProcessingDomains(algorithm);
            if (domains == SandboxEditorGeometryProcessingDomain::None)
                continue;

            entries.push_back(SandboxEditorGeometryProcessingEntry{
                .Algorithm = algorithm,
                .Domains = domains,
            });
        }
        return entries;
    }

    std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        return ResolveSandboxEditorGeometryProcessingEntries(
            GetSandboxEditorGeometryProcessingCapabilities(registry, entity));
    }


    const char* DebugNameForSandboxEditorGeometryProcessingDomain(
        const SandboxEditorGeometryProcessingDomain domain) noexcept
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        switch (domain)
        {
        case Domain::None:
            return "None";
        case Domain::MeshVertices:
            return "Mesh Vertices";
        case Domain::MeshEdges:
            return "Mesh Edges";
        case Domain::MeshHalfedges:
            return "Mesh Halfedges";
        case Domain::MeshFaces:
            return "Mesh Faces";
        case Domain::GraphVertices:
            return "Graph Nodes";
        case Domain::GraphEdges:
            return "Graph Edges";
        case Domain::GraphHalfedges:
            return "Graph Halfedges";
        case Domain::PointCloudPoints:
            return "Point Cloud Points";
        }
        return "Mixed";
    }

    const char* DebugNameForSandboxEditorGeometryProcessingAlgorithm(
        const SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
        case SandboxEditorGeometryProcessingAlgorithm::KMeans:
            return "K-Means";
        case SandboxEditorGeometryProcessingAlgorithm::MeshDenoise:
            return "Mesh Denoise";
        case SandboxEditorGeometryProcessingAlgorithm::Curvature:
            return "Curvature";
        case SandboxEditorGeometryProcessingAlgorithm::Remeshing:
            return "Remeshing";
        case SandboxEditorGeometryProcessingAlgorithm::Simplification:
            return "Simplification";
        case SandboxEditorGeometryProcessingAlgorithm::Smoothing:
            return "Smoothing";
        case SandboxEditorGeometryProcessingAlgorithm::Subdivision:
            return "Subdivision";
        case SandboxEditorGeometryProcessingAlgorithm::Repair:
            return "Repair";
        case SandboxEditorGeometryProcessingAlgorithm::NormalEstimation:
            return "Normals";
        case SandboxEditorGeometryProcessingAlgorithm::ShortestPath:
            return "Shortest Path";
        case SandboxEditorGeometryProcessingAlgorithm::ConvexHull:
            return "Convex Hull";
        case SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            return "Surface Reconstruction";
        case SandboxEditorGeometryProcessingAlgorithm::VectorHeat:
            return "Vector Heat Method";
        case SandboxEditorGeometryProcessingAlgorithm::Parameterization:
            return "Parameterization";
        case SandboxEditorGeometryProcessingAlgorithm::BooleanCSG:
            return "Boolean CSG";
        case SandboxEditorGeometryProcessingAlgorithm::Registration:
            return "ICP Registration";
        case SandboxEditorGeometryProcessingAlgorithm::BilateralFilter:
            return "Bilateral Filter";
        case SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation:
            return "Outlier Estimation";
        case SandboxEditorGeometryProcessingAlgorithm::KernelDensity:
            return "Kernel Density";
        case SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            return "Statistical Outlier Removal";
        case SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return "Radius Outlier Removal";
        case SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return "Progressive Poisson Sampling";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshCurvatureOutput(
        const SandboxEditorMeshCurvatureOutput output) noexcept
    {
        switch (output)
        {
        case SandboxEditorMeshCurvatureOutput::All:
            return "All";
        case SandboxEditorMeshCurvatureOutput::Mean:
            return "Mean";
        case SandboxEditorMeshCurvatureOutput::Gaussian:
            return "Gaussian";
        case SandboxEditorMeshCurvatureOutput::PrincipalDirections:
            return "Principal";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshRemeshMode(
        const SandboxEditorMeshRemeshMode mode) noexcept
    {
        switch (mode)
        {
        case SandboxEditorMeshRemeshMode::Uniform:
            return "Uniform";
        case SandboxEditorMeshRemeshMode::Adaptive:
            return "Adaptive";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshRemeshSizingLaw(
        const SandboxEditorMeshRemeshSizingLaw sizingLaw) noexcept
    {
        switch (sizingLaw)
        {
        case SandboxEditorMeshRemeshSizingLaw::MeanCurvature:
            return "Mean curvature";
        case SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
            return "Error-bounded Taubin";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshSubdivideOperator(
        const SandboxEditorMeshSubdivideOperator op) noexcept
    {
        switch (op)
        {
        case SandboxEditorMeshSubdivideOperator::Loop:
            return "Loop";
        case SandboxEditorMeshSubdivideOperator::CatmullClark:
            return "Catmull-Clark";
        case SandboxEditorMeshSubdivideOperator::Sqrt3:
            return "Sqrt(3)";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshSimplifyMetric(
        const SandboxEditorMeshSimplifyMetric metric) noexcept
    {
        switch (metric)
        {
        case SandboxEditorMeshSimplifyMetric::ClassicalQEM:
            return "Classical QEM";
        case SandboxEditorMeshSimplifyMetric::FA_QEM:
            return "FA-QEM (feature-aware)";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorICPVariant(
        const SandboxEditorICPVariant variant) noexcept
    {
        switch (variant)
        {
        case SandboxEditorICPVariant::PointToPoint:
            return "Point-to-point";
        case SandboxEditorICPVariant::PointToPlane:
            return "Point-to-plane";
        }
        return "Unknown";
    }

    SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context)
    {
        return BuildSandboxEditorPanelFrame(
            context,
            SandboxEditorModelBuildRequest{});
    }

    SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context,
        const SandboxEditorModelBuildRequest& request)
    {
        SandboxEditorPanelFrame frame{};
        SandboxEditorModelBuildStats stats{};
        const SandboxEditorModelBuildClock::time_point frameBuildStart =
            SandboxEditorModelBuildClock::now();
        SandboxEditorContext modelContext = context;
        modelContext.ModelBuildStats = &stats;

        if (modelContext.Scene == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingScene,
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
                      [](const SandboxEditorEntityRow& lhs,
                         const SandboxEditorEntityRow& rhs)
                      {
                          if (lhs.StableEntityId != rhs.StableEntityId)
                              return lhs.StableEntityId < rhs.StableEntityId;
                          return lhs.Name < rhs.Name;
                      });
        }

        if (modelContext.Selection == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingSelectionController,
                          "Selection controller is unavailable.");
        }
        if (!modelContext.ImGuiAdapterAvailable)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingImGuiAdapter,
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
            frame.RenderRecipe = BuildSandboxEditorRenderRecipeEditorModel(modelContext);
        }
        if (request.CameraRender)
        {
            frame.CameraRender = BuildCameraRenderModel(modelContext);
        }
        if (request.Visualization)
        {
            frame.Visualization = BuildCachedVisualizationModel(modelContext);
        }
        stats.PanelFrameModelBuildTimeNs +=
            SandboxEditorElapsedNs(frameBuildStart);
        frame.ModelBuildStats = stats;
        return frame;
    }

    SandboxEditorDomainWindowModel BuildSandboxEditorDomainWindowModel(
        const SandboxEditorContext& context,
        const SandboxEditorDomainWindowKind kind)
    {
        ScopedSandboxEditorStatTimer timer{
            context.ModelBuildStats != nullptr
                ? &context.ModelBuildStats->DomainWindowModelBuildTimeNs
                : nullptr};
        if (context.ModelBuildStats != nullptr)
        {
            ++context.ModelBuildStats->DomainWindowModelBuilds;
        }
        SandboxEditorDomainWindowModel model{};
        model.Kind = kind;
        model.ExpectedDomain = ExpectedDomainForWindowKind(kind);
        model.VisualizationTarget = VisualizationTargetForWindowKind(kind);
        model.VisualizationControlsAvailable =
            context.VisualizationCommandsAvailable;

        if (context.Scene == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingScene,
                          "Scene registry is unavailable for domain window.");
            return model;
        }
        if (context.Selection == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingSelectionController,
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
                          SandboxEditorDiagnosticCode::NoSelectedEntity,
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
        const SandboxEditorGeometryDomainModel geometry =
            BuildGeometryDomainModel(raw, *selected);
        SandboxEditorSelectedAnalysisModel selectedAnalysis =
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
                std::string(DebugNameForSandboxEditorDomainWindowKind(kind));
            message += " window requires ";
            message += DebugNameForSandboxEditorGeometryDomain(model.ExpectedDomain);
            message += "-domain selection; selected domain is ";
            message += DebugNameForSandboxEditorGeometryDomain(model.SelectedDomain);
            message += ".";
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                          std::move(message));
        }

        if (!context.VisualizationCommandsAvailable)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable,
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

    bool SelectSandboxEditorEntity(const SandboxEditorContext& context,
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

    SandboxEditorFileImportResult ApplySandboxEditorFileImportCommand(
        const SandboxEditorContext& context,
        const SandboxEditorFileImportCommand& command)
    {
        const FileImportPrerequisiteEvaluation prerequisites =
            EvaluateFileImportPrerequisites(
                context.AssetImportCommands.Available(),
                command.Path,
                command.PayloadKind);
        if (!prerequisites.CanImport)
        {
            return SandboxEditorFileImportResult{
                .Status = context.AssetImportCommands.Available()
                    ? SandboxEditorCommandStatus::AssetImportFailed
                    : SandboxEditorCommandStatus::MissingAssetImportCommands,
                .PayloadKind = prerequisites.ResolvedPayloadKind ==
                        A::AssetPayloadKind::Unknown
                    ? command.PayloadKind
                    : prerequisites.ResolvedPayloadKind,
                .Error = prerequisites.Error,
                .Message = prerequisites.ImportDisabledReason,
            };
        }

        SandboxEditorFileImportCommand resolvedCommand = command;
        resolvedCommand.PayloadKind = prerequisites.ResolvedPayloadKind;
        SandboxEditorFileImportResult result =
            context.AssetImportCommands.Import(resolvedCommand);
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildImportSuccessMessage(resolvedCommand, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Status == SandboxEditorCommandStatus::Pending)
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

    SandboxEditorSceneFileResult ApplySandboxEditorSceneSaveCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command)
    {
        if (!context.SceneFileCommands.Available())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::Save,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Scene file command surface is unavailable.",
            };
        }
        if (command.Path.empty())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::SceneSaveFailed,
                .Operation = SandboxEditorSceneFileOperation::Save,
                .Error = Core::ErrorCode::InvalidPath,
                .Message = BuildSceneFileFailureMessage(
                    SandboxEditorSceneFileOperation::Save,
                    Core::ErrorCode::InvalidPath),
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.Save(command);
        result.Operation = SandboxEditorSceneFileOperation::Save;
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage(command, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Status == SandboxEditorCommandStatus::Pending)
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

    SandboxEditorSceneFileResult ApplySandboxEditorSceneLoadCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command)
    {
        if (!context.SceneFileCommands.Available())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::Load,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Scene file command surface is unavailable.",
            };
        }
        if (command.Path.empty())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::SceneLoadFailed,
                .Operation = SandboxEditorSceneFileOperation::Load,
                .Error = Core::ErrorCode::InvalidPath,
                .Message = BuildSceneFileFailureMessage(
                    SandboxEditorSceneFileOperation::Load,
                    Core::ErrorCode::InvalidPath),
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.Load(command);
        result.Operation = SandboxEditorSceneFileOperation::Load;
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage(command, result);
            result.Error = Core::ErrorCode::Success;
            InvalidateSelectedModelCache(context);
        }
        else if (result.Status == SandboxEditorCommandStatus::Pending)
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

    SandboxEditorSceneFileResult ApplySandboxEditorNewSceneCommand(
        const SandboxEditorContext& context)
    {
        if (!context.SceneFileCommands.New)
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::New,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "New scene command surface is unavailable.",
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.New();
        result.Operation = SandboxEditorSceneFileOperation::New;
        if (result.Status == SandboxEditorCommandStatus::Applied)
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

    SandboxEditorSceneFileResult ApplySandboxEditorCloseSceneCommand(
        const SandboxEditorContext& context)
    {
        if (!context.SceneFileCommands.Close)
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::Close,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Close scene command surface is unavailable.",
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.Close();
        result.Operation = SandboxEditorSceneFileOperation::Close;
        if (result.Status == SandboxEditorCommandStatus::Applied)
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
                context.CommandHistory->Execute(
                    MakeTransformEditCommand(
                        EditorTransformEditCommand{
                            .Scene = context.Scene,
                            .StableEntityId = command.StableEntityId,
                            .Before = *transform,
                            .After = next,
                            .Label = "Edit Transform",
                        }));
            return ToSandboxEditorCommandStatus(result.Status);
        }

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
        if (!command.SetEdgeView &&
            !command.SetVertexView &&
            !command.SetVertexRenderMode &&
            !command.SetVertexPointRadius)
        {
            return SandboxEditorCommandStatus::NoChange;
        }
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (command.SetVertexPointRadius &&
            !IsFinitePositive(command.VertexPointRadiusPx))
        {
            return SandboxEditorCommandStatus::InvalidProcessingParameters;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;
        if ((command.SetVertexView && command.EnableVertexView) ||
            command.SetVertexRenderMode ||
            command.SetVertexPointRadius)
        {
            if (!availability.Sources.Has(GS::SourceCapability::VertexPoints))
                return SandboxEditorCommandStatus::UnsupportedGeometryDomain;
        }
        if (command.SetEdgeView && command.EnableEdgeView)
        {
            const bool hasExplicitEdges =
                availability.Sources.Has(GS::SourceCapability::Edges);
            const bool hasMeshWireTopology =
                availability.Sources.Has(GS::SourceCapability::Halfedges) &&
                availability.Sources.Has(GS::SourceCapability::Faces);
            if (!availability.Sources.Has(GS::SourceCapability::VertexPoints) ||
                (!hasExplicitEdges && !hasMeshWireTopology))
            {
                return SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            }
        }

        const SandboxEditorRenderHintState before =
            ReadRenderHintState(raw, entity);
        SandboxEditorRenderHintState after = before;
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
            return SandboxEditorCommandStatus::NoChange;
        if (context.CommandHistory != nullptr)
        {
            const std::uint32_t stableEntityId = command.StableEntityId;
            ECS::Scene::Registry* scene = context.Scene;
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    EditorCommandRecord{
                        .Label = "Change Render Hints",
                        .Redo =
                            [scene, stableEntityId, after]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, after);
                            },
                        .Undo =
                            [scene, stableEntityId, before]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, before);
                            },
                    });
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToSandboxEditorCommandStatus(result.Status));
        }
        return InvalidateSelectedModelCacheIfApplied(
            context,
            ToSandboxEditorCommandStatus(
                ApplyRenderHintState(context.Scene, command.StableEntityId, after)));
    }

    SandboxEditorCommandStatus ApplySandboxEditorRenderHintCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRenderHintCommand& command)
    {
        if (!AnyRenderHintEdit(command))
            return SandboxEditorCommandStatus::NoChange;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if ((command.SetUniformEdgeWidth &&
             !IsFinitePositive(command.UniformEdgeWidth)) ||
            (command.SetUniformPointSize &&
             !IsFinitePositive(command.UniformPointSize)))
        {
            return SandboxEditorCommandStatus::InvalidProcessingParameters;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (!RenderHintCommandMatchesDomain(command, availability))
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const SandboxEditorRenderHintState before =
            ReadRenderHintState(raw, entity);
        const SandboxEditorRenderHintState after =
            ApplyRenderHintCommandToState(before, command);
        if (SameRenderHintState(before, after))
            return SandboxEditorCommandStatus::NoChange;

        if (context.CommandHistory != nullptr)
        {
            const std::uint32_t stableEntityId = command.StableEntityId;
            ECS::Scene::Registry* scene = context.Scene;
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    EditorCommandRecord{
                        .Label = "Change Render Hints",
                        .Redo =
                            [scene, stableEntityId, after]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, after);
                            },
                        .Undo =
                            [scene, stableEntityId, before]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, before);
                            },
                        .Dirtying = true,
                    });
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToSandboxEditorCommandStatus(result.Status));
        }

        return InvalidateSelectedModelCacheIfApplied(
            context,
            ToSandboxEditorCommandStatus(
                ApplyRenderHintState(context.Scene, command.StableEntityId, after)));
    }

    SandboxEditorCommandStatus ApplySandboxEditorSpatialDebugBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSpatialDebugBindingCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const auto* current = raw.try_get<ECSC::SpatialDebugBinding>(entity);
        const std::optional<ECSC::SpatialDebugBinding> before =
            current != nullptr
                ? std::optional<ECSC::SpatialDebugBinding>{*current}
                : std::nullopt;
        const std::optional<ECSC::SpatialDebugBinding> after =
            command.EnableBinding
                ? std::optional<ECSC::SpatialDebugBinding>{ToSpatialDebugBinding(command)}
                : std::nullopt;

        if (!after.has_value())
        {
            if (!before.has_value())
                return SandboxEditorCommandStatus::NoChange;
        }
        else if (before.has_value() &&
                 SameSpatialDebugBinding(*before, *after))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    MakeSpatialDebugBindingCommand(
                        EditorSpatialDebugBindingCommand{
                            .Scene = context.Scene,
                            .StableEntityId = command.StableEntityId,
                            .Before = before,
                            .After = after,
                            .Label = "Change Spatial Debug Binding",
                        }));
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToSandboxEditorCommandStatus(result.Status));
        }

        if (after.has_value())
            raw.emplace_or_replace<ECSC::SpatialDebugBinding>(entity, *after);
        else
            raw.remove<ECSC::SpatialDebugBinding>(entity);
        InvalidateSelectedModelCache(context);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationConfigCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const std::optional<G::VisualizationConfig> before =
            StoredVisualizationConfigForTarget(raw, entity, command.Target);
        const std::optional<G::VisualizationConfig> effectiveBefore =
            EffectiveVisualizationConfigForTarget(raw, entity, command.Target);
        const std::optional<G::VisualizationConfig> after =
            command.EnableConfig
                ? std::optional<G::VisualizationConfig>{ToVisualizationConfig(command)}
                : std::nullopt;

        if (!after.has_value())
        {
            if (!before.has_value())
                return SandboxEditorCommandStatus::NoChange;
        }
        else if (before.has_value() && SameVisualizationConfig(*before, *after))
        {
            return SandboxEditorCommandStatus::NoChange;
        }
        else if (!before.has_value() &&
                 effectiveBefore.has_value() &&
                 SameVisualizationConfig(*effectiveBefore, *after))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandRecord record =
                command.Target == SandboxEditorVisualizationTarget::Entity
                    ? MakeVisualizationConfigCommand(
                          EditorVisualizationConfigCommand{
                              .Scene = context.Scene,
                              .StableEntityId = command.StableEntityId,
                              .Before = before,
                              .After = after,
                              .Label = "Change Visualization",
                          })
                    : MakeVisualizationConfigTargetCommand(
                          context.Scene,
                          command.StableEntityId,
                          command.Target,
                          before,
                          after,
                          "Change Visualization");
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(record);
            return InvalidateSelectedModelCacheIfApplied(
                context,
                ToSandboxEditorCommandStatus(result.Status));
        }

        return InvalidateSelectedModelCacheIfApplied(
            context,
            ToSandboxEditorCommandStatus(
                ApplyVisualizationConfigTarget(
                    context.Scene,
                    command.StableEntityId,
                    command.Target,
                    after)));
    }

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationPropertyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationPropertyCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;
        if (command.PropertyName.empty())
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        const Geometry::PropertySet* properties =
            PropertySetForVisualizationDomain(availability, command.Domain);
        if (properties == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        std::optional<SandboxEditorVisualizationPropertyInfo> matched{};
        std::vector<SandboxEditorVisualizationPropertyInfo> allProperties{};
        AppendVisualizationPropertiesForDomain(
            allProperties,
            *properties,
            command.Domain);
        for (const SandboxEditorVisualizationPropertyInfo& property :
             allProperties)
        {
            if (property.Name == command.PropertyName)
            {
                matched = property;
                break;
            }
        }
        if (!matched.has_value() ||
            !PropertySupportsPreset(*matched, command.Preset))
        {
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;
        }

        SandboxEditorVisualizationConfigCommand configCommand{
            .StableEntityId = command.StableEntityId,
            .Target = command.Target,
            .EnableConfig = true,
            .ScalarFieldName = command.PropertyName,
            .ScalarDomain = ToVisualizationConfigDomain(command.Domain),
            .ColorBufferName = command.PropertyName,
            .ScalarAutoRange = command.ScalarAutoRange,
            .ScalarRangeMin = command.ScalarRangeMin,
            .ScalarRangeMax = command.ScalarRangeMax,
            .ScalarBinCount = command.ScalarBinCount,
        };
        // UI-032 — preset buttons switch the source/property but must not
        // clobber styling the user already configured on this target
        // (colormap, isoline width/color, highlight isovalues).
        if (const std::optional<G::VisualizationConfig> existing =
                EffectiveVisualizationConfigForTarget(raw, entity, command.Target);
            existing.has_value())
        {
            configCommand.ScalarColormap = existing->Scalar.Map;
            configCommand.IsolineWidth = existing->Scalar.Isolines.Width;
            configCommand.IsolineColor = existing->Scalar.Isolines.Color;
            configCommand.IsolineValues = existing->Scalar.Isolines.Values;
            configCommand.IsolineValueCount = existing->Scalar.Isolines.ValueCount;
        }

        switch (command.Preset)
        {
        case SandboxEditorVisualizationPropertyPreset::Scalar:
            if (!command.ScalarAutoRange &&
                !(command.ScalarRangeMin < command.ScalarRangeMax))
            {
                return SandboxEditorCommandStatus::InvalidVisualizationProperty;
            }
            configCommand.Source =
                G::VisualizationConfig::ColorSource::ScalarField;
            configCommand.IsolineCount = 0u;
            break;
        case SandboxEditorVisualizationPropertyPreset::Isoline:
            if (command.IsolineCount == 0u ||
                (!command.ScalarAutoRange &&
                 !(command.ScalarRangeMin < command.ScalarRangeMax)))
            {
                return SandboxEditorCommandStatus::InvalidVisualizationProperty;
            }
            configCommand.Source =
                G::VisualizationConfig::ColorSource::ScalarField;
            configCommand.IsolineCount = command.IsolineCount;
            break;
        case SandboxEditorVisualizationPropertyPreset::ColorBuffer:
            configCommand.Source = ToColorBufferSource(command.Domain);
            configCommand.IsolineCount = 0u;
            break;
        }

        return ApplySandboxEditorVisualizationConfigCommand(
            context,
            configCommand);
    }

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationAdapterBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationAdapterBindingCommand& command)
    {
        if (!context.VisualizationCommandsAvailable ||
            !context.VisualizationAdapterBindings.Available())
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (!availability.HasGeometry())
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const std::optional<RenderExtractionCache::VisualizationAdapterBinding>
            current =
            context.VisualizationAdapterBindings.GetBinding(command.StableEntityId);
        if (!command.EnableBinding)
        {
            if (!current.has_value())
                return SandboxEditorCommandStatus::NoChange;
            context.VisualizationAdapterBindings.ClearBinding(command.StableEntityId);
            InvalidateSelectedModelCache(context);
            return SandboxEditorCommandStatus::Applied;
        }

        const RenderExtractionCache::VisualizationAdapterBinding next =
            ToVisualizationAdapterBinding(command);
        if (current.has_value() &&
            SameVisualizationAdapterBinding(*current, next))
            return SandboxEditorCommandStatus::NoChange;

        context.VisualizationAdapterBindings.SetBinding(
            command.StableEntityId,
            next);
        InvalidateSelectedModelCache(context);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorVertexChannelBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVertexChannelBindingCommand& command)
    {
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (command.Channel != VertexChannel::Normal &&
            command.Channel != VertexChannel::Color)
        {
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
            return SandboxEditorCommandStatus::StaleEntity;

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        const std::optional<SandboxEditorPropertyCatalogDomain> domain =
            VertexChannelCatalogDomainForView(view);
        if (!domain.has_value())
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const Geometry::PropertySet* properties =
            VertexChannelPropertySetForView(view, *domain);
        if (properties == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const auto* current = raw.try_get<VertexChannelBindingSet>(*entity);
        VertexChannelBindingSet after = current != nullptr
            ? *current
            : VertexChannelBindingSet{};
        VertexChannelSourceBinding* target =
            FindMutableVertexChannelBinding(after, command.Channel);
        if (target == nullptr)
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        if (!command.EnableBinding)
        {
            if (current == nullptr || !IsVertexChannelBindingEnabled(*target))
                return SandboxEditorCommandStatus::NoChange;

            *target = {};
            ++after.BindingGeneration;
            if (AnyVertexChannelBindingEnabled(after))
                raw.emplace_or_replace<VertexChannelBindingSet>(*entity, after);
            else
                raw.remove<VertexChannelBindingSet>(*entity);

            MarkVertexChannelDirty(raw, *entity, command.Channel);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Change vertex channel binding");
            InvalidateSelectedModelCache(context);
            return SandboxEditorCommandStatus::Applied;
        }

        if (command.PropertyName.empty())
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        const SandboxEditorPropertyCatalogValueKind valueKind =
            ToPropertyCatalogValueKind(
                DetectPropertyValueKind(*properties, command.PropertyName));
        const std::optional<AttributeSourceType> sourceType =
            ToAttributeSourceType(valueKind);
        if (!sourceType.has_value())
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        const AttributeBindResult resolver =
            EvaluateVertexChannelBinding(
                *properties,
                command.Channel,
                command.PropertyName,
                *sourceType,
                properties->Size(),
                context.ModelBuildStats);
        if (!resolver.Ok())
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        const VertexChannelSourceBinding next{
            .Enabled = true,
            .SourceType = *sourceType,
            .SourceProperty = command.PropertyName,
        };
        if (current != nullptr &&
            SameVertexChannelSourceBinding(*target, next))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        *target = next;
        ++after.BindingGeneration;
        raw.emplace_or_replace<VertexChannelBindingSet>(*entity, after);
        MarkVertexChannelDirty(raw, *entity, command.Channel);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Change vertex channel binding");
        InvalidateSelectedModelCache(context);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorProgressiveSlotDefaultCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressiveSlotDefaultCommand& command)
    {
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (!IsFiniteDefaultValue(command.Value))
            return SandboxEditorCommandStatus::InvalidProcessingParameters;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const auto* current =
            raw.try_get<ProgressivePresentationBindings>(entity);
        if (current == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        ProgressivePresentationBindings before = *current;
        ProgressivePresentationBindings after = before;
        const ProgressiveSlotLookup lookup = FindMutableProgressiveSlot(
            after,
            command.PresentationKey,
            command.Semantic);
        if (lookup.Slot == nullptr)
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        ProgressiveSlotBinding& slot = *lookup.Slot;
        if (slot.SourceKind == ProgressiveSlotSourceKind::UniformDefault &&
            slot.Enabled == command.Enabled &&
            SameProgressiveDefaultValue(slot.UniformDefault, command.Value))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        slot.SourceKind = ProgressiveSlotSourceKind::UniformDefault;
        slot.UniformDefault = command.Value;
        slot.Property = {};
        slot.AuthoredTexture = {};
        slot.GeneratedTexture = {};
        slot.GeneratedPolicy =
            DefaultGeneratedOutputPolicyFor(slot.SourceKind);
        slot.Provenance = ProgressiveGeneratedOutputProvenance::UniformDefault;
        slot.Readiness = ProgressiveReadinessState::DefaultValue;
        slot.LastDiagnostic.clear();
        slot.Enabled = command.Enabled;
        ++after.BindingGeneration;

        return InvalidateSelectedModelCacheIfApplied(
            context,
            CommitProgressiveBindingsChange(
                context,
                command.StableEntityId,
                std::move(before),
                std::move(after)));
    }

    SandboxEditorCommandStatus ApplySandboxEditorProgressiveSlotPropertyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressiveSlotPropertyCommand& command)
    {
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (!PropertySourceKindAllowedForProgressiveSlotCommand(command.SourceKind) ||
            command.PropertyName.empty())
        {
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const auto* current =
            raw.try_get<ProgressivePresentationBindings>(entity);
        if (current == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const std::size_t expectedCount =
            ResolvePropertyElementCount(view, command.Domain);
        ProgressivePropertyBindingDescriptor descriptor{
            .Domain = command.Domain,
            .PropertyName = command.PropertyName,
            .ExpectedValueKind = command.ExpectedValueKind,
            .ExpectedElementCount = expectedCount,
        };
        ProgressivePropertyResolution resolution =
            ResolvePropertyBinding(view, descriptor);
        if (!resolution.Compatible())
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        ProgressivePresentationBindings before = *current;
        ProgressivePresentationBindings after = before;
        const ProgressiveSlotLookup lookup = FindMutableProgressiveSlot(
            after,
            command.PresentationKey,
            command.Semantic);
        if (lookup.Slot == nullptr)
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        ProgressiveSlotBinding& slot = *lookup.Slot;
        const ProgressiveReadinessState nextReadiness =
            command.SourceKind == ProgressiveSlotSourceKind::PropertyBuffer
                ? ProgressiveReadinessState::Ready
                : ProgressiveReadinessState::Pending;
        const ProgressiveGeneratedOutputProvenance nextProvenance =
            command.SourceKind == ProgressiveSlotSourceKind::PropertyBuffer
                ? ProgressiveGeneratedOutputProvenance::PropertyBuffer
                : ProgressiveGeneratedOutputProvenance::PropertyBinding;

        if (slot.SourceKind == command.SourceKind &&
            slot.Enabled &&
            slot.Readiness == nextReadiness &&
            SameProgressivePropertyDescriptor(slot.Property, descriptor))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        slot.SourceKind = command.SourceKind;
        slot.Property = std::move(descriptor);
        slot.AuthoredTexture = {};
        slot.GeneratedTexture = {};
        slot.GeneratedPolicy =
            DefaultGeneratedOutputPolicyFor(slot.SourceKind);
        slot.Provenance = nextProvenance;
        slot.Readiness = nextReadiness;
        slot.LastDiagnostic.clear();
        slot.Enabled = true;
        ++after.BindingGeneration;

        return InvalidateSelectedModelCacheIfApplied(
            context,
            CommitProgressiveBindingsChange(
                context,
                command.StableEntityId,
                std::move(before),
                std::move(after)));
    }

    SandboxEditorTextureBakeCommandResult ApplySandboxEditorTextureBakeCommand(
        const SandboxEditorContext& context,
        const SandboxEditorTextureBakeCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return SandboxEditorTextureBakeCommandResult{
                .Status = SandboxEditorCommandStatus::MissingScene,
                .BakeStatus = SelectedMeshTextureBakeStatus::MissingScene,
                .Diagnostic = "Scene registry is unavailable.",
            };
        }
        if (context.AssetService == nullptr)
        {
            return SandboxEditorTextureBakeCommandResult{
                .Status = SandboxEditorCommandStatus::AssetImportFailed,
                .BakeStatus = SelectedMeshTextureBakeStatus::MissingAssetService,
                .Diagnostic = "Asset service is unavailable for generated texture payloads.",
            };
        }
        if (context.Device == nullptr || !context.Device->IsOperational())
        {
            return SandboxEditorTextureBakeCommandResult{
                .Status = SandboxEditorCommandStatus::InvalidVisualizationProperty,
                .BakeStatus = SelectedMeshTextureBakeStatus::CommandFailed,
                .Diagnostic = "Texture baking requires an operational GPU backend.",
            };
        }
        if (command.PropertyName.empty() ||
            command.Width == 0u ||
            command.Height == 0u)
        {
            return SandboxEditorTextureBakeCommandResult{
                .Status = SandboxEditorCommandStatus::InvalidVisualizationProperty,
                .BakeStatus = command.PropertyName.empty()
                    ? SelectedMeshTextureBakeStatus::MissingProperty
                    : SelectedMeshTextureBakeStatus::InvalidResolution,
                .Diagnostic = "Texture bake command has invalid parameters.",
            };
        }

        SelectedMeshTextureBakeRequest request{};
        request.StableEntityId = command.StableEntityId;
        request.SourceDomain = command.SourceDomain;
        request.SourcePropertyName = command.PropertyName;
        request.ExpectedValueKind = command.ExpectedValueKind;
        request.Encoder = command.Encoder;
        request.RangePolicy = command.RangePolicy;
        request.RangeMin = command.RangeMin;
        request.RangeMax = command.RangeMax;
        request.Width = command.Width;
        request.Height = command.Height;
        request.TargetPresentationKey = command.PresentationKey;
        request.TargetSemantic = command.TargetSemantic;
        request.GeneratedKey = command.GeneratedKey.empty()
            ? command.PropertyName
            : command.GeneratedKey;
        request.BindGeneratedTexture = command.BindGeneratedTexture;

        const SelectedMeshTextureBakeResult bake =
            ApplySelectedMeshTextureBakeCommand(
                SelectedMeshTextureBakeContext{
                    .Scene = context.Scene,
                    .World = context.World,
                    .AssetService = context.AssetService,
                    .CommandHistory = context.CommandHistory,
                },
                request);

        SandboxEditorCommandStatus status =
            SandboxEditorCommandStatus::Applied;
        if (!bake.Succeeded())
        {
            switch (bake.Status)
            {
            case SelectedMeshTextureBakeStatus::MissingScene:
                status = SandboxEditorCommandStatus::MissingScene;
                break;
            case SelectedMeshTextureBakeStatus::MissingAssetService:
            case SelectedMeshTextureBakeStatus::AssetLoadFailed:
                status = SandboxEditorCommandStatus::AssetImportFailed;
                break;
            case SelectedMeshTextureBakeStatus::StaleEntity:
                status = SandboxEditorCommandStatus::StaleEntity;
                break;
            case SelectedMeshTextureBakeStatus::NonMeshSelection:
            case SelectedMeshTextureBakeStatus::UnsupportedSourceDomain:
                status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                break;
            case SelectedMeshTextureBakeStatus::CommandFailed:
                status = SandboxEditorCommandStatus::GeometryProcessingFailed;
                break;
            case SelectedMeshTextureBakeStatus::Success:
            case SelectedMeshTextureBakeStatus::Scheduled:
            case SelectedMeshTextureBakeStatus::NonOperationalBackend:
            case SelectedMeshTextureBakeStatus::MissingProgressiveBindings:
            case SelectedMeshTextureBakeStatus::MissingPresentation:
            case SelectedMeshTextureBakeStatus::MissingSlot:
            case SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic:
            case SelectedMeshTextureBakeStatus::IncompatibleTargetSlot:
            case SelectedMeshTextureBakeStatus::InvalidResolution:
            case SelectedMeshTextureBakeStatus::InvalidRange:
            case SelectedMeshTextureBakeStatus::MissingProperty:
            case SelectedMeshTextureBakeStatus::UnsupportedPropertyType:
            case SelectedMeshTextureBakeStatus::MismatchedPropertyCount:
            case SelectedMeshTextureBakeStatus::MissingTexcoords:
            case SelectedMeshTextureBakeStatus::NonFiniteTexcoord:
            case SelectedMeshTextureBakeStatus::NonFinitePropertyValue:
            case SelectedMeshTextureBakeStatus::DegenerateAllTriangles:
            case SelectedMeshTextureBakeStatus::DegenerateUvTriangles:
            case SelectedMeshTextureBakeStatus::ZeroCoverageBake:
            case SelectedMeshTextureBakeStatus::BakeFailed:
            case SelectedMeshTextureBakeStatus::JobSubmitFailed:
            case SelectedMeshTextureBakeStatus::StaleCompletion:
                status = SandboxEditorCommandStatus::InvalidVisualizationProperty;
                break;
            }
        }

        SandboxEditorTextureBakeCommandResult result{
            .Status = status,
            .BakeStatus = bake.Status,
            .GeneratedTexture = bake.GeneratedTexture,
            .Job = bake.Job,
            .Scheduled = bake.Status == SelectedMeshTextureBakeStatus::Scheduled,
            .BoundGeneratedTexture = bake.BoundGeneratedTexture,
            .GeneratedAssetPath = bake.GeneratedAssetPath,
            .Diagnostic = bake.Diagnostic,
        };
        if (result.Status == SandboxEditorCommandStatus::Applied)
            InvalidateSelectedModelCache(context);
        return result;
    }

    struct SandboxEditorUvRegenerationSourceSnapshot
    {
        std::vector<glm::vec3> Positions{};
        std::vector<std::uint32_t> HalfedgeToVertex{};
        std::vector<std::uint32_t> HalfedgeNext{};
        std::vector<std::uint32_t> HalfedgeFace{};
        std::vector<std::uint32_t> FaceHalfedge{};
    };

    [[nodiscard]] std::optional<std::vector<std::uint32_t>> CopyU32Property(
        const Geometry::PropertySet& properties,
        const std::string_view name)
    {
        const auto property = properties.Get<std::uint32_t>(name);
        if (!property || property.Vector().size() != properties.Size())
            return std::nullopt;
        return property.Vector();
    }

    [[nodiscard]] bool CaptureUvRegenerationSourceSnapshot(
        const GS::ConstSourceView& view,
        SandboxEditorUvRegenerationSourceSnapshot& out)
    {
        if (view.VertexSource == nullptr ||
            view.HalfedgeSource == nullptr ||
            view.FaceSource == nullptr)
        {
            return false;
        }

        std::optional<std::vector<glm::vec3>> positions =
            CollectFiniteGeometryPositions(view.VertexSource->Properties);
        std::optional<std::vector<std::uint32_t>> halfedgeToVertex =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeToVertex);
        std::optional<std::vector<std::uint32_t>> halfedgeNext =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeNext);
        std::optional<std::vector<std::uint32_t>> halfedgeFace =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeFace);
        std::optional<std::vector<std::uint32_t>> faceHalfedge =
            CopyU32Property(view.FaceSource->Properties,
                            GS::PropertyNames::kFaceHalfedge);
        if (!positions.has_value() ||
            !halfedgeToVertex.has_value() ||
            !halfedgeNext.has_value() ||
            !halfedgeFace.has_value() ||
            !faceHalfedge.has_value())
        {
            return false;
        }

        out.Positions = std::move(*positions);
        out.HalfedgeToVertex = std::move(*halfedgeToVertex);
        out.HalfedgeNext = std::move(*halfedgeNext);
        out.HalfedgeFace = std::move(*halfedgeFace);
        out.FaceHalfedge = std::move(*faceHalfedge);
        return true;
    }

    [[nodiscard]] bool SameUvRegenerationSourceSnapshot(
        const SandboxEditorUvRegenerationSourceSnapshot& lhs,
        const SandboxEditorUvRegenerationSourceSnapshot& rhs) noexcept
    {
        return SameGeometryPositions(lhs.Positions, rhs.Positions) &&
               lhs.HalfedgeToVertex == rhs.HalfedgeToVertex &&
               lhs.HalfedgeNext == rhs.HalfedgeNext &&
               lhs.HalfedgeFace == rhs.HalfedgeFace &&
               lhs.FaceHalfedge == rhs.FaceHalfedge;
    }

    [[nodiscard]] SandboxEditorUvRegenerationCommandResult
    MakeUvRegenerationResult(
        const SandboxEditorCommandStatus status,
        const Geometry::UvAtlas::UvAtlasStatus uvStatus,
        std::string diagnostic)
    {
        return SandboxEditorUvRegenerationCommandResult{
            .Status = status,
            .UvStatus = uvStatus,
            .Diagnostic = std::move(diagnostic),
        };
    }

    void CopyUvAtlasCounters(
        const Geometry::UvAtlas::UvAtlasResult& atlas,
        SandboxEditorUvRegenerationCommandResult& result)
    {
        result.UvStatus = atlas.Status;
        result.Provenance = atlas.Provenance;
        result.AtlasWidth = atlas.Diagnostics.AtlasWidth;
        result.AtlasHeight = atlas.Diagnostics.AtlasHeight;
        result.ChartCount = atlas.Diagnostics.ChartCount;
        result.SeamSplitVertexCount =
            atlas.Diagnostics.OutputVertexCount >
                    atlas.Diagnostics.InputVertexCount
                ? atlas.Diagnostics.OutputVertexCount -
                      atlas.Diagnostics.InputVertexCount
                : 0u;
    }

    [[nodiscard]] SandboxEditorUvRegenerationCommandResult
    MakePendingUvRegenerationResult(
        const DerivedJobHandle handle)
    {
        SandboxEditorUvRegenerationCommandResult result{};
        result.Status = SandboxEditorCommandStatus::Pending;
        result.Diagnostic = "UV regeneration CPU job queued";
        AppendDerivedJobHandleToMessage(result.Diagnostic, handle);
        result.Diagnostic += ".";
        return result;
    }

    struct SandboxEditorUvRegenerationCpuJobState
    {
        std::uint32_t StableEntityId{0u};
        std::uint64_t GeometryMetadataSignature{0u};
        SandboxEditorUvRegenerationSourceSnapshot Snapshot{};
        MeshSoupFromGeometrySourcesResult Soup{};
        Geometry::PropertySet SourceVertexProperties{};
        bool HasSourceVertexProperties{false};
        Geometry::PropertySet SourceFaceProperties{};
        bool HasSourceFaceProperties{false};
        std::vector<glm::vec2> AuthoredTexcoords{};
        Geometry::HalfedgeMesh::Mesh BeforeMesh{};
        Geometry::HalfedgeMesh::Mesh AfterMesh{};
        SandboxEditorUvRegenerationCommand Command{};
        SandboxEditorUvRegenerationCommandResult Result{};
    };

    [[nodiscard]] DerivedJobApplyValidation
    ValidateUvRegenerationCpuJobApply(
        const SandboxEditorContext& context,
        const SandboxEditorUvRegenerationCpuJobState& job)
    {
        if (context.Scene == nullptr)
            return DerivedJobApplyValidation::MissingEntity;

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, job.StableEntityId);
        if (!entity.has_value())
            return DerivedJobApplyValidation::MissingEntity;

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::Mesh)
            return DerivedJobApplyValidation::StaleGeometryGeneration;

        if (GeometryMetadataSignatureForEntity(raw, *entity) !=
            job.GeometryMetadataSignature)
        {
            return DerivedJobApplyValidation::StaleGeometryGeneration;
        }

        SandboxEditorUvRegenerationSourceSnapshot current{};
        if (!CaptureUvRegenerationSourceSnapshot(view, current))
            return DerivedJobApplyValidation::StaleGeometryGeneration;
        if (!SameUvRegenerationSourceSnapshot(current, job.Snapshot))
            return DerivedJobApplyValidation::StaleSourcePropertyGeneration;

        return DerivedJobApplyValidation::Current;
    }

    void PublishUvRegenerationResultSink(
        const SandboxEditorContext& context,
        SandboxEditorUvRegenerationCommandResult result)
    {
        if (context.MethodResultSinks.UvRegeneration)
            context.MethodResultSinks.UvRegeneration(std::move(result));
    }

    [[nodiscard]] DerivedJobWorkerResult RunUvRegenerationCpuWorker(
        const std::shared_ptr<SandboxEditorUvRegenerationCpuJobState>& state)
    {
        Geometry::UvAtlas::UvAtlasOptions options{};
        options.PreserveValidAuthoredUvs =
            state->Command.PreserveValidAuthoredUvs;
        options.ForceRegenerate = state->Command.ForceRegenerate;
        options.Resolution = state->Command.Resolution;
        options.Padding = state->Command.Padding;
        options.TexelsPerUnit = state->Command.TexelsPerUnit;
        options.BackendName = "xatlas";

        Geometry::UvAtlas::UvAtlasInput input{};
        input.Positions = state->Soup.Mesh.Positions();
        input.Faces = state->Soup.Mesh.Faces();
        input.AuthoredTexcoords = state->AuthoredTexcoords;
        input.VertexProperties = state->HasSourceVertexProperties
            ? Geometry::ConstPropertySet(state->SourceVertexProperties)
            : Geometry::ConstPropertySet{};
        input.HasVertexProperties = state->HasSourceVertexProperties;

        Geometry::UvAtlas::UvAtlasResult atlas =
            Geometry::UvAtlas::ResolveUvAtlas(input, options, nullptr);
        CopyUvAtlasCounters(atlas, state->Result);

        if (!atlas.Succeeded())
        {
            const bool backendFailure =
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable ||
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput ||
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendFailed;
            state->Result.Status = backendFailure
                ? SandboxEditorCommandStatus::GeometryProcessingFailed
                : SandboxEditorCommandStatus::InvalidProcessingParameters;
            state->Result.Diagnostic =
                atlas.Diagnostics.BackendDetail.empty()
                    ? std::string{Geometry::UvAtlas::ToString(atlas.Status)}
                    : atlas.Diagnostics.BackendDetail;
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = state->Result.Diagnostic,
            };
        }

        auto converted =
            Geometry::Mesh::Conversion::ToHalfedgeMesh(atlas.OutputMesh);
        if (!converted.Succeeded())
        {
            state->Result.Status =
                SandboxEditorCommandStatus::GeometryProcessingFailed;
            state->Result.Diagnostic =
                "generated UV mesh could not be converted back to halfedge topology";
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = state->Result.Diagnostic,
            };
        }

        CopyUvOutputPropertiesToHalfedgeMesh(
            Geometry::ConstPropertySet(state->SourceFaceProperties),
            state->HasSourceFaceProperties,
            state->Soup,
            atlas,
            converted.Mesh);
        state->AfterMesh = std::move(converted.Mesh);
        state->Result.Status = SandboxEditorCommandStatus::Applied;
        state->Result.Diagnostic = atlas.Diagnostics.BackendDetail;
        return DerivedJobOutput{
            .PayloadToken = 0u,
            .NormalizedProgress = 1.0f,
            .ProgressDeterminate = true,
            .Diagnostic = "UV regeneration CPU result ready",
        };
    }

    [[nodiscard]] SandboxEditorUvRegenerationCommandResult
    CommitUvRegenerationCpuJobResult(
        const SandboxEditorContext& context,
        SandboxEditorUvRegenerationCpuJobState& job)
    {
        SandboxEditorUvRegenerationCommandResult result = job.Result;
        if (!result.Succeeded())
            return result;

        const SandboxEditorCommandStatus commitStatus =
            CommitUvMeshTopologyReplacement(
                context,
                job.StableEntityId,
                "Regenerate UVs",
                job.BeforeMesh,
                job.AfterMesh);
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Diagnostic =
                "UV regeneration publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        InvalidateSelectedModelCache(context);
        return result;
    }

    [[nodiscard]] Core::Result PublishUvRegenerationCpuJob(
        const SandboxEditorContext& context,
        SandboxEditorUvRegenerationCpuJobState& job)
    {
        SandboxEditorUvRegenerationCommandResult result =
            CommitUvRegenerationCpuJobResult(context, job);
        const bool succeeded = result.Succeeded();
        PublishUvRegenerationResultSink(context, std::move(result));
        return succeeded ? Core::Ok() : Core::Err(Core::ErrorCode::Unknown);
    }

    [[nodiscard]] DerivedJobDesc MakeUvRegenerationCpuJobDesc(
        const SandboxEditorContext& context,
        const std::shared_ptr<SandboxEditorUvRegenerationCpuJobState>& state)
    {
        return DerivedJobDesc{
            .Key = DerivedJobKey{
                .EntityId = state->StableEntityId,
                .Domain = ProgressiveGeometryDomain::MeshSurface,
                .OutputSemantic = ProgressiveSlotSemantic::Albedo,
                .SourcePropertyGeneration =
                    state->GeometryMetadataSignature,
                .OutputName = std::string{kUvRegenerationJobOutputName},
            },
            .Name = "Sandbox.UvRegeneration.CPU",
            .RequestedJobDomain = ProgressiveJobDomain::Cpu,
            .Kind = RuntimeTaskKinds::GeometryProcess,
            .Priority = Core::Dag::TaskPriority::Normal,
            .EstimatedCost = std::max<std::uint32_t>(
                1u,
                static_cast<std::uint32_t>(
                    (std::max(state->Soup.Mesh.VertexCount(),
                              state->Soup.Mesh.FaceCount()) +
                     1023u) /
                    1024u)),
            .Scope = context.World,
            .Execute =
                [state]() -> DerivedJobWorkerResult
                {
                    return RunUvRegenerationCpuWorker(state);
                },
            .ValidateOnMainThread =
                [context, state]()
                {
                    return ValidateUvRegenerationCpuJobApply(
                        context,
                        *state);
                },
            .ApplyOnMainThread =
                [context, state](DerivedJobApplyContext&) -> Core::Result
                {
                    return PublishUvRegenerationCpuJob(context, *state);
                },
        };
    }

    [[nodiscard]] SandboxEditorUvRegenerationCommandResult
    SubmitUvRegenerationCpuJob(
        const SandboxEditorContext& context,
        const std::shared_ptr<SandboxEditorUvRegenerationCpuJobState>& state)
    {
        DerivedJobDesc desc = MakeUvRegenerationCpuJobDesc(context, state);
        if (const std::optional<DerivedJobSnapshot> active =
                FindActiveEditorDerivedJob(context, desc.Key))
        {
            SandboxEditorUvRegenerationCommandResult pending =
                MakePendingUvRegenerationResult(active->Handle);
            pending.Diagnostic =
                BuildActiveDerivedJobMessage("UV regeneration CPU", *active);
            return pending;
        }

        const DerivedJobHandle handle =
            context.DerivedJobCommands.Submit(std::move(desc));
        if (!handle.IsValid())
        {
            return MakeUvRegenerationResult(
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                Geometry::UvAtlas::UvAtlasStatus::BackendFailed,
                "UV regeneration CPU job submission was rejected by the runtime job lane.");
        }

        return MakePendingUvRegenerationResult(handle);
    }

    SandboxEditorUvRegenerationCommandResult ApplySandboxEditorUvRegenerationCommand(
        const SandboxEditorContext& context,
        const SandboxEditorUvRegenerationCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeUvRegenerationResult(
                SandboxEditorCommandStatus::MissingScene,
                Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                "Scene registry is unavailable.");
        }
        if (command.Resolution == 0u || command.Padding >= command.Resolution)
        {
            return MakeUvRegenerationResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration requires a positive resolution and padding smaller than the atlas.");
        }
        if (!command.BackendName.empty() && command.BackendName != "xatlas")
        {
            return MakeUvRegenerationResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable,
                "Only the promoted xatlas UV backend is available.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeUvRegenerationResult(
                SandboxEditorCommandStatus::StaleEntity,
                Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                "UV regeneration target entity is stale or no longer live.");
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshSoupFromGeometrySourcesResult soup =
            BuildMeshSoupFromGeometrySources(view);
        if (!soup.Succeeded())
        {
            return MakeUvRegenerationResult(
                soup.Status,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                soup.Diagnostic);
        }

        MeshTopologySourceResult topology =
            BuildHalfedgeMeshForTopologyEdit(view, "UV regeneration");
        if (!topology.Succeeded())
        {
            return MakeUvRegenerationResult(
                topology.Status,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                topology.Diagnostic);
        }

        SandboxEditorUvRegenerationSourceSnapshot snapshot{};
        if (!CaptureUvRegenerationSourceSnapshot(view, snapshot))
        {
            return MakeUvRegenerationResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration requires count-matched mesh position and halfedge topology properties.");
        }

        std::vector<glm::vec2> authoredTexcoords;
        if (view.VertexSource != nullptr)
        {
            const auto texcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (texcoords && texcoords.Vector().size() == soup.Mesh.VertexCount())
                authoredTexcoords = texcoords.Vector();
        }

        auto state =
            std::make_shared<SandboxEditorUvRegenerationCpuJobState>();
        state->StableEntityId = command.StableEntityId;
        state->GeometryMetadataSignature =
            GeometryMetadataSignatureForEntity(raw, *entity);
        state->Snapshot = std::move(snapshot);
        state->Soup = std::move(soup);
        state->BeforeMesh = std::move(topology.Mesh);
        state->Command = command;
        state->AuthoredTexcoords = std::move(authoredTexcoords);
        if (view.VertexSource != nullptr)
        {
            state->SourceVertexProperties = view.VertexSource->Properties;
            state->HasSourceVertexProperties = true;
        }
        if (view.FaceSource != nullptr)
        {
            state->SourceFaceProperties = view.FaceSource->Properties;
            state->HasSourceFaceProperties = true;
        }

        if (context.DerivedJobCommands.Available())
            return SubmitUvRegenerationCpuJob(context, state);

        const DerivedJobWorkerResult worker =
            RunUvRegenerationCpuWorker(state);
        if (!worker.has_value())
        {
            return MakeUvRegenerationResult(
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                Geometry::UvAtlas::UvAtlasStatus::BackendFailed,
                "UV regeneration CPU worker failed.");
        }

        return CommitUvRegenerationCpuJobResult(context, *state);
    }

    SandboxEditorMeshDenoiseResult ApplySandboxEditorMeshDenoiseCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshDenoiseCommand& command)
    {
        SandboxEditorMeshDenoiseResult result =
            MakeMeshDenoiseBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh denoise.";
            return result;
        }
        if (!context.MeshDenoiseKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Smoothing mesh denoiser is unavailable in this runtime configuration.";
            return result;
        }

        const bool validStage =
            std::find(kMeshDenoiseStages.begin(),
                      kMeshDenoiseStages.end(),
                      command.Stage) != kMeshDenoiseStages.end();
        if (!validStage ||
            command.NormalIterations == 0u ||
            command.VertexIterations == 0u ||
            !std::isfinite(command.SigmaSpatial) ||
            !std::isfinite(command.SigmaRange) ||
            command.SigmaSpatial < 0.0 ||
            command.SigmaRange < 0.0 ||
            !IsPositiveFinite(command.DegenerateNormalLengthEpsilon))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh denoise requires a valid stage, positive iteration counts, non-negative finite sigma values, and a positive finite degeneracy epsilon.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh denoise target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshDenoiseSourceResult source =
            BuildHalfedgeMeshForDenoise(view);
        result.VertexSlotCount = source.BeforePositions.size();
        result.SkippedDeletedVertexCount =
            static_cast<std::size_t>(
                std::count(source.DeletedVertices.begin(),
                           source.DeletedVertices.end(),
                           true));
        result.WrittenCount =
            result.VertexSlotCount - result.SkippedDeletedVertexCount;
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.DenoiseStatus =
                source.Status ==
                        SandboxEditorCommandStatus::UnsupportedGeometryDomain
                    ? Smooth::DenoiseStatus::EmptyMesh
                    : Smooth::DenoiseStatus::InvalidParams;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitMeshDenoiseCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Smooth::BilateralDenoiseParams params{};
        params.NormalIterations = command.NormalIterations;
        params.VertexIterations = command.VertexIterations;
        params.SigmaSpatial = command.SigmaSpatial;
        params.SigmaRange = command.SigmaRange;
        params.PreserveBoundary = command.PreserveBoundary;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;

        const Smooth::BilateralDenoiseResult denoise =
            Smooth::DenoiseBilateral(source.Mesh, params);
        result.DenoiseStatus = denoise.Status;
        result.Error = ErrorForDenoiseStatus(denoise.Status);
        CopyMeshDenoiseCounters(denoise, result);
        result.VertexSlotCount = source.BeforePositions.size();
        result.WrittenCount =
            result.VertexSlotCount - result.SkippedDeletedVertexCount;

        if (denoise.Status != Smooth::DenoiseStatus::Success)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Message = "Geometry.Smoothing denoise failed with ";
            result.Message += std::string(Smooth::DebugName(denoise.Status));
            result.Message += ".";
            return result;
        }

        std::vector<glm::vec3> afterPositions =
            ExtractMeshPositions(source.Mesh);
        if (afterPositions.size() != source.BeforePositions.size() ||
            !AllFiniteVec3(std::span<const glm::vec3>{
                afterPositions.data(),
                afterPositions.size()}))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.DenoiseStatus = Smooth::DenoiseStatus::NonFiniteInput;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Smoothing denoise produced invalid or count-mismatched positions.";
            return result;
        }

        std::size_t movedPublishedVertices = 0u;
        for (std::size_t i = 0u; i < afterPositions.size(); ++i)
        {
            if (i < source.DeletedVertices.size() && source.DeletedVertices[i])
            {
                afterPositions[i] = source.BeforePositions[i];
                continue;
            }
            if (afterPositions[i] != source.BeforePositions[i])
                ++movedPublishedVertices;
        }
        result.MovedVertexCount = movedPublishedVertices;

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshDenoisePositions(
                context,
                command.StableEntityId,
                std::move(source.BeforePositions),
                std::move(afterPositions));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh denoise position publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshDenoiseSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorMeshCurvatureResult ApplySandboxEditorMeshCurvatureCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshCurvatureCommand& command)
    {
        SandboxEditorMeshCurvatureResult result =
            MakeMeshCurvatureBaseResult(
                command,
                context.MeshCurvatureDirectionsAvailable);

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh curvature.";
            return result;
        }
        if (!context.MeshCurvatureKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Curvature mesh curvature is unavailable in this runtime configuration.";
            return result;
        }

        const bool validOutput =
            std::find(kMeshCurvatureOutputs.begin(),
                      kMeshCurvatureOutputs.end(),
                      command.Output) != kMeshCurvatureOutputs.end();
        if (!validOutput)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Mesh curvature requires a valid output mode.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh curvature target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView constView = GS::BuildConstView(raw, *entity);
        MeshCurvatureSourceResult source =
            BuildHalfedgeMeshForCurvature(constView);
        result.VertexSlotCount = source.VertexSlotCount;
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        GS::MutableSourceView publishView = GS::BuildMutableView(raw, *entity);
        if (!publishView.Valid() || publishView.VertexSource == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh curvature target has no writable vertex GeometrySources.";
            return result;
        }

        MeshCurvaturePropertyState before{};
        std::string captureDiagnostic{};
        if (!CaptureMeshCurvaturePropertyState(
                publishView.VertexSource->Properties,
                result.VertexSlotCount,
                before,
                captureDiagnostic))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message = captureDiagnostic;
            return result;
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitMeshCurvatureCpuJob(
                context,
                command,
                std::move(source),
                std::move(before),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Curv::CurvatureField curvature = Curv::ComputeCurvature(source.Mesh);
        if (!curvature.MeanCurvatureProperty ||
            !curvature.GaussianCurvatureProperty ||
            curvature.MeanCurvatureProperty.Vector().size() !=
                result.VertexSlotCount ||
            curvature.GaussianCurvatureProperty.Vector().size() !=
                result.VertexSlotCount)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Curvature produced missing or count-mismatched scalar properties.";
            return result;
        }

        const std::vector<double>& mean =
            curvature.MeanCurvatureProperty.Vector();
        const std::vector<double>& gaussian =
            curvature.GaussianCurvatureProperty.Vector();
        result.NonFiniteScalarCount =
            CountNonFiniteScalars(
                std::span<const double>{mean.data(), mean.size()}) +
            CountNonFiniteScalars(
                std::span<const double>{gaussian.data(), gaussian.size()});
        if (result.NonFiniteScalarCount != 0u)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Curvature produced non-finite scalar curvature values.";
            return result;
        }

        MeshCurvaturePropertyState after = before;
        after.HadMean = true;
        after.Mean = mean;
        after.HadGaussian = true;
        after.Gaussian = gaussian;
        result.ScalarPropertyCount = 2u;
        result.ScalarWrittenCount = mean.size() + gaussian.size();

        if (result.DirectionsRequested &&
            result.DirectionsAvailable)
        {
            if (!curvature.PrincipalDir1Property ||
                !curvature.PrincipalDir2Property ||
                curvature.PrincipalDir1Property.Vector().size() !=
                    result.VertexSlotCount ||
                curvature.PrincipalDir2Property.Vector().size() !=
                    result.VertexSlotCount)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced missing or count-mismatched principal-direction properties.";
                return result;
            }

            const std::vector<glm::vec3>& dir1 =
                curvature.PrincipalDir1Property.Vector();
            const std::vector<glm::vec3>& dir2 =
                curvature.PrincipalDir2Property.Vector();
            result.NonFiniteDirectionCount =
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir1.data(), dir1.size()}) +
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir2.data(), dir2.size()});
            if (result.NonFiniteDirectionCount != 0u)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced non-finite principal directions.";
                return result;
            }

            after.HadDir1 = true;
            after.Dir1 = dir1;
            after.HadDir2 = true;
            after.Dir2 = dir2;
            result.DirectionPropertyCount = 2u;
            result.DirectionWrittenCount = dir1.size() + dir2.size();
        }

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshCurvatureProperties(
                context,
                command.StableEntityId,
                std::move(before),
                std::move(after));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh curvature property publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.DirectionsPublished =
            result.DirectionPropertyCount == 2u &&
            result.DirectionWrittenCount == result.VertexSlotCount * 2u;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshCurvatureSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorMeshRemeshResult ApplySandboxEditorMeshRemeshCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshRemeshCommand& command)
    {
        SandboxEditorMeshRemeshResult result =
            MakeMeshRemeshBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh remesh.";
            return result;
        }
        if (!ValidMeshRemeshMode(command.Mode) ||
            !ValidMeshRemeshSizingLaw(command.SizingLaw) ||
            command.Iterations == 0u ||
            !std::isfinite(command.TargetEdgeLength) ||
            command.TargetEdgeLength < 0.0 ||
            !IsPositiveFinite(command.Lambda) ||
            !std::isfinite(command.CurvatureAdaptation) ||
            command.CurvatureAdaptation < 0.0 ||
            !std::isfinite(command.MaxReferenceProjectionDistance) ||
            command.MaxReferenceProjectionDistance < 0.0 ||
            (command.ProjectToSurface && command.ReferenceProjectionK == 0u) ||
            (command.SizingLaw ==
                 SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
             !IsPositiveFinite(command.ApproximationError)))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh remesh requires a valid mode, sizing law, positive iteration count, finite non-negative target length, positive lambda, and valid projection/sizing parameters.";
            return result;
        }
        if (command.Mode == SandboxEditorMeshRemeshMode::Uniform &&
            !context.MeshRemeshUniformKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Remeshing uniform remesher is unavailable in this runtime configuration.";
            return result;
        }
        if (command.Mode == SandboxEditorMeshRemeshMode::Adaptive &&
            !context.MeshRemeshAdaptiveKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.HalfedgeMesh.AdaptiveRemeshing is unavailable in this runtime configuration.";
            return result;
        }
        if (command.ProjectToSurface &&
            !context.MeshRemeshProjectToSurfaceAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Mesh remesh project-to-surface is unavailable in this runtime configuration.";
            return result;
        }
        if (command.SizingLaw ==
                SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
            !context.MeshRemeshErrorBoundedSizingAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Mesh remesh error-bounded Taubin sizing is unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh remesh target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh remesh");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitMeshRemeshCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;

        std::optional<Geometry::RemeshingOperationResult> remeshResult{};
        if (command.Mode == SandboxEditorMeshRemeshMode::Uniform)
        {
            Remesh::RemeshingParams params{};
            params.TargetLength = command.TargetEdgeLength;
            params.Iterations = command.Iterations;
            params.Lambda = command.Lambda;
            params.PreserveBoundary = command.PreserveBoundary;
            params.ProjectToSurface = command.ProjectToSurface;
            params.ReferenceProjectionK = command.ReferenceProjectionK;
            params.MaxReferenceProjectionDistance =
                command.MaxReferenceProjectionDistance;
            remeshResult = Remesh::Remesh(source.Mesh, params);
        }
        else
        {
            AdaptiveRemesh::AdaptiveRemeshingParams params{};
            if (command.TargetEdgeLength > 0.0)
            {
                params.MinEdgeLength = command.TargetEdgeLength * 0.5;
                params.MaxEdgeLength = command.TargetEdgeLength * 2.0;
            }
            params.CurvatureAdaptation = command.CurvatureAdaptation;
            params.Sizing = ToAdaptiveSizingLaw(command.SizingLaw);
            params.ApproximationError = command.ApproximationError;
            params.Iterations = command.Iterations;
            params.Lambda = command.Lambda;
            params.PreserveBoundary = command.PreserveBoundary;
            params.EnableReferenceProjection = command.ProjectToSurface;
            params.ReferenceProjectionK = command.ReferenceProjectionK;
            params.MaxReferenceProjectionDistance =
                command.MaxReferenceProjectionDistance;
            remeshResult = AdaptiveRemesh::AdaptiveRemesh(source.Mesh, params);
        }

        if (!remeshResult.has_value())
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry remeshing failed for the selected mesh and parameters.";
            return result;
        }

        if (source.Mesh.HasGarbage())
            source.Mesh.GarbageCollection();
        CopyRemeshCounters(*remeshResult, result);
        result.OutputVertexCount = source.Mesh.VertexCount();
        result.OutputFaceCount = source.Mesh.FaceCount();

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Remesh mesh",
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh remesh publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshRemeshSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorMeshSubdivideResult ApplySandboxEditorMeshSubdivideCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshSubdivideCommand& command)
    {
        SandboxEditorMeshSubdivideResult result =
            MakeMeshSubdivideBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh subdivide.";
            return result;
        }
        if (!ValidMeshSubdivideOperator(command.Operator) ||
            command.Iterations == 0u ||
            (command.PreserveLoopFeatureEdges &&
             command.FeatureEdgePropertyName.empty()))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh subdivide requires a valid operator, positive iteration count, and a feature-edge property name when feature preservation is enabled.";
            return result;
        }
        if (command.Operator == SandboxEditorMeshSubdivideOperator::Loop &&
            !context.MeshSubdivideLoopKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Subdivision Loop subdivision is unavailable in this runtime configuration.";
            return result;
        }
        if (command.Operator ==
                SandboxEditorMeshSubdivideOperator::CatmullClark &&
            !context.MeshSubdivideCatmullClarkKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.CatmullClark subdivision is unavailable in this runtime configuration.";
            return result;
        }
        if (command.Operator == SandboxEditorMeshSubdivideOperator::Sqrt3 &&
            !context.MeshSubdivideSqrt3KernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.HalfedgeMesh.SubdivisionSqrt3 is unavailable in this runtime configuration.";
            return result;
        }
        if (command.PreserveLoopFeatureEdges &&
            command.Operator != SandboxEditorMeshSubdivideOperator::Loop)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Loop feature-edge preservation can only be used with the Loop subdivision operator.";
            return result;
        }
        if (command.PreserveLoopFeatureEdges &&
            !context.MeshSubdivideLoopFeatureEdgesAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Loop subdivision feature-edge preservation is unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh subdivide target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh subdivide");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitMeshSubdivideCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;
        Geometry::HalfedgeMesh::Mesh output{};

        if (command.Operator == SandboxEditorMeshSubdivideOperator::Loop)
        {
            LoopSubdivide::SubdivisionParams params{};
            params.Iterations = command.Iterations;
            params.MaxOutputFaces = command.MaxOutputFaces;
            params.PreserveFeatureEdges = command.PreserveLoopFeatureEdges;
            params.FeatureEdgePropertyName = command.FeatureEdgePropertyName;
            const std::optional<LoopSubdivide::SubdivisionResult>
                subdivision = LoopSubdivide::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Subdivision Loop subdivision failed for the selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }
        else if (command.Operator ==
                 SandboxEditorMeshSubdivideOperator::CatmullClark)
        {
            CatmullClark::SubdivisionParams params{};
            params.Iterations = command.Iterations;
            const std::optional<CatmullClark::SubdivisionResult>
                subdivision = CatmullClark::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.CatmullClark subdivision failed for the selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }
        else
        {
            Sqrt3Subdivide::Sqrt3Params params{};
            params.Iterations = command.Iterations;
            params.MaxOutputFaces = command.MaxOutputFaces;
            const std::optional<Sqrt3Subdivide::Sqrt3Result>
                subdivision = Sqrt3Subdivide::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.HalfedgeMesh.SubdivisionSqrt3 failed for the selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }

        if (output.HasGarbage())
            output.GarbageCollection();
        result.OutputVertexCount = output.VertexCount();
        result.OutputFaceCount = output.FaceCount();

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Subdivide mesh",
                std::move(before),
                std::move(output));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh subdivide publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSubdivideSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorMeshSimplifyResult ApplySandboxEditorMeshSimplifyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshSimplifyCommand& command)
    {
        SandboxEditorMeshSimplifyResult result =
            MakeMeshSimplifyBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh simplify.";
            return result;
        }
        const bool hasStopCriterion =
            command.TargetFaces > 0u || command.MaxError > 0.0;
        if (!ValidMeshSimplifyMetric(command.Metric) ||
            !hasStopCriterion ||
            command.NormalWeight < 0.0 ||
            command.BoundaryWeight < 0.0 ||
            command.CurvatureWeight < 0.0 ||
            command.FeatureAngleThresholdDegrees < 0.0 ||
            command.FeatureAngleThresholdDegrees > 180.0)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh simplify requires a valid metric, a positive target face count or maximum error, non-negative weights, and a feature angle within [0, 180].";
            return result;
        }
        if (!context.MeshSimplifyKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Simplification is unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh simplify target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh simplify");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        // BuildHalfedgeMeshForTopologyEdit carries only positions + topology, so
        // the scratch halfedge mesh has no v:texcoord even when the selected mesh
        // does. Copy it in (the builder guarantees a 1:1 source->halfedge vertex
        // mapping) so FA_QEM's PreserveUvSeams can actually pin UV-seam vertices
        // instead of silently no-opping when the halfedge mesh lacks texcoords.
        CopyMeshSimplifyAuxiliaryProperties(view, source.Mesh);

        if (context.DerivedJobCommands.Available())
        {
            return SubmitMeshSimplifyCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;

        Simpl::Params params{};
        params.Metric =
            command.Metric == SandboxEditorMeshSimplifyMetric::FA_QEM
                ? Simpl::Metric::FA_QEM
                : Simpl::Metric::ClassicalQEM;
        params.TargetFaces = command.TargetFaces;
        params.MaxError = command.MaxError > 0.0 ? command.MaxError : 1.0e30;
        params.PreserveBoundary = command.PreserveBoundary;
        params.FeatureAngleThresholdDegrees =
            command.FeatureAngleThresholdDegrees;
        params.NormalWeight = command.NormalWeight;
        params.BoundaryWeight = command.BoundaryWeight;
        params.CurvatureWeight = command.CurvatureWeight;
        params.PreserveSharpFeatures = command.PreserveSharpFeatures;
        params.PreserveUvSeams = command.PreserveUvSeams;

        const std::optional<Simpl::Result> simplification =
            Simpl::Simplify(source.Mesh, params);
        if (!simplification.has_value())
        {
            result.Status =
                SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Simplification failed for the selected mesh and parameters.";
            return result;
        }

        if (source.Mesh.HasGarbage())
            source.Mesh.GarbageCollection();
        result.OutputVertexCount = source.Mesh.VertexCount();
        result.OutputFaceCount = source.Mesh.FaceCount();
        result.CollapseCount = simplification->CollapseCount;
        result.MaxCollapseError = simplification->MaxCollapseError;
        result.CollapsesRejectedTopology =
            simplification->CollapsesRejectedTopology;
        result.CollapsesRejectedQuality =
            simplification->CollapsesRejectedQuality;
        result.SharpFeatureVerticesPinned =
            simplification->SharpFeatureVerticesPinned;
        result.SeamVerticesPinned = simplification->SeamVerticesPinned;

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Simplify mesh",
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh simplify publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSimplifySuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorMeshVertexNormalsResult
    ApplySandboxEditorMeshVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeMeshNormalsResult(
                SandboxEditorCommandStatus::MissingScene,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for mesh vertex-normal recompute.");
        }
        if (!std::isfinite(command.DegenerateNormalLengthEpsilon) ||
            command.DegenerateNormalLengthEpsilon <= 0.0)
        {
            return MakeMeshNormalsResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                GN::RecomputeStatus::InvalidOutputProperty,
                command.Weighting,
                Core::ErrorCode::InvalidArgument,
                "Mesh vertex-normal recompute requires a positive finite degeneracy epsilon.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeMeshNormalsResult(
                SandboxEditorCommandStatus::StaleEntity,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                Core::ErrorCode::ResourceNotFound,
                "Mesh vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const MeshForVertexNormalsResult source =
            BuildHalfedgeMeshForVertexNormalRecompute(view);
        if (!source.Succeeded())
        {
            return MakeMeshNormalsResult(
                source.Status,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                source.Error,
                source.Diagnostic);
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitMeshVertexNormalsCpuJob(
                context,
                command,
                source.Mesh,
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Geometry::HalfedgeMesh::Mesh mesh = source.Mesh;
        GN::Params params{};
        params.Weighting = command.Weighting;
        params.OutputProperty = GN::kDefaultOutputProperty;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.SkipDeleted = true;

        const GN::Result normalResult = GN::Recompute(mesh, params);
        SandboxEditorMeshVertexNormalsResult result{
            .Status = normalResult.Status == GN::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .Weighting = normalResult.Weighting,
            .Error = normalResult.Status == GN::RecomputeStatus::Success
                ? Core::ErrorCode::Success
                : Core::ErrorCode::Unknown,
        };
        CopyMeshNormalCounters(normalResult, result);
        if (normalResult.Status != GN::RecomputeStatus::Success)
        {
            result.Message = "Geometry.HalfedgeMesh.Vertices.Normals failed with ";
            result.Message += std::string(GN::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!PublishMeshVertexNormals(view, normalResult))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus = GN::RecomputeStatus::PropertyTypeConflict;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message =
                "Mesh vertex-normal publication failed because v:normal has an incompatible type or size.";
            return result;
        }

        Dirty::MarkVertexNormalsDirty(raw, *entity);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty("Recompute mesh vertex normals");
        result.Message = BuildMeshNormalsSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorGraphVertexNormalsResult
    ApplySandboxEditorGraphVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorGraphVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeGraphNormalsResult(
                SandboxEditorCommandStatus::MissingScene,
                GraphNormals::RecomputeStatus::EmptyGraph,
                command.OrientTowardFallback,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for graph vertex-normal recompute.");
        }
        if (!IsPositiveFinite(command.DegenerateNormalLengthEpsilon) ||
            !IsPositiveFinite(command.CollinearEigenvalueRatioEpsilon))
        {
            return MakeGraphNormalsResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                GraphNormals::RecomputeStatus::InvalidOutputProperty,
                command.OrientTowardFallback,
                Core::ErrorCode::InvalidArgument,
                "Graph vertex-normal recompute requires positive finite degeneracy and collinearity epsilons.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeGraphNormalsResult(
                SandboxEditorCommandStatus::StaleEntity,
                GraphNormals::RecomputeStatus::EmptyGraph,
                command.OrientTowardFallback,
                Core::ErrorCode::ResourceNotFound,
                "Graph vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GraphForVertexNormalsResult source =
            BuildGraphConnectivityForVertexNormalRecompute(view);
        if (!source.Succeeded())
        {
            return MakeGraphNormalsResult(
                source.Status,
                GraphNormals::RecomputeStatus::InvalidTopologyProperty,
                command.OrientTowardFallback,
                source.Error,
                source.Diagnostic);
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitGraphVertexNormalsCpuJob(
                context,
                command,
                view.NodeSource->Properties,
                view.EdgeSource->Properties,
                source.Halfedges,
                source.EdgeSlotCount,
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Geometry::Vertices scratchNodes = view.NodeSource->Properties;
        GraphNormals::Params params{};
        params.PositionProperty = GS::PropertyNames::kPosition;
        params.OutputProperty = GraphNormals::kDefaultOutputProperty;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.CollinearEigenvalueRatioEpsilon =
            command.CollinearEigenvalueRatioEpsilon;
        params.SkipDeleted = true;
        params.OrientTowardFallback = command.OrientTowardFallback;

        const GraphNormals::PropertySetResult normalResult =
            GraphNormals::Recompute(
                scratchNodes,
                Geometry::ConstPropertySet(scratchNodes)
                    .Get<glm::vec3>(GS::PropertyNames::kPosition),
                Geometry::ConstPropertySet(source.Halfedges)
                    .Get<Geometry::Graph::HalfedgeConnectivity>(
                        "h:connectivity"),
                source.EdgeSlotCount,
                params,
                Geometry::ConstPropertySet(scratchNodes).Get<bool>(
                    "v:deleted"),
                Geometry::ConstPropertySet(view.EdgeSource->Properties)
                    .Get<bool>("e:deleted"));

        SandboxEditorGraphVertexNormalsResult result{
            .Status = normalResult.Status ==
                    GraphNormals::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .OrientTowardFallback = command.OrientTowardFallback,
            .Error = ErrorForGraphNormalStatus(normalResult.Status),
        };
        CopyGraphNormalCounters(normalResult.Diagnostics, result);
        if (normalResult.Status != GraphNormals::RecomputeStatus::Success)
        {
            result.Message = "Geometry.Graph.Vertex.Normals failed with ";
            result.Message +=
                std::string(GraphNormals::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!normalResult.Normals.IsValid() ||
            !PublishCanonicalVec3Normals(
                view.NodeSource->Properties,
                normalResult.Normals.Vector()))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus =
                GraphNormals::RecomputeStatus::PropertyTypeConflict;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message =
                "Graph vertex-normal publication failed because v:normal has an incompatible type or size.";
            return result;
        }

        Dirty::MarkVertexNormalsDirty(raw, *entity);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Recompute graph vertex normals");
        result.Message = BuildGraphNormalsSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorPointCloudVertexNormalsResult
    ApplySandboxEditorPointCloudVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPointCloudVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::MissingScene,
                PointNormals::RecomputeStatus::EmptyInput,
                command,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for point-cloud vertex-normal recompute.");
        }
        if (command.KNeighbors == 0u ||
            command.MinimumNeighbors == 0u ||
            !IsPositiveFinite(command.DegenerateNormalLengthEpsilon) ||
            !IsPositiveFinite(command.CollinearEigenvalueRatioEpsilon) ||
            (command.UseRadiusSearch &&
             (!std::isfinite(command.Radius) || command.Radius <= 0.0f)))
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                PointNormals::RecomputeStatus::InvalidOutputProperty,
                command,
                Core::ErrorCode::InvalidArgument,
                "Point-cloud vertex-normal recompute requires positive finite neighborhood settings.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::StaleEntity,
                PointNormals::RecomputeStatus::EmptyInput,
                command,
                Core::ErrorCode::ResourceNotFound,
                "Point-cloud vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
            view.VertexSource == nullptr)
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                PointNormals::RecomputeStatus::EmptyInput,
                command,
                Core::ErrorCode::InvalidArgument,
                "Point-cloud vertex normals require selected point-cloud GeometrySources.");
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitPointCloudVertexNormalsCpuJob(
                context,
                command,
                view.VertexSource->Properties,
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Geometry::Vertices scratchPoints = view.VertexSource->Properties;
        Geometry::PointCloud::Cloud scratchCloud{scratchPoints};
        PointNormals::Params params{};
        params.PositionProperty = GS::PropertyNames::kPosition;
        params.OutputProperty = PointNormals::kDefaultOutputProperty;
        params.KNeighbors = command.KNeighbors;
        params.MinimumNeighbors = command.MinimumNeighbors;
        params.UseRadiusSearch = command.UseRadiusSearch;
        params.Radius = command.Radius;
        params.Orientation = command.Orientation;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.CollinearEigenvalueRatioEpsilon =
            command.CollinearEigenvalueRatioEpsilon;
        params.SkipDeleted = true;

        const PointNormals::Result normalResult =
            PointNormals::Recompute(scratchCloud, params);

        SandboxEditorPointCloudVertexNormalsResult result{
            .Status = normalResult.Status ==
                    PointNormals::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .Backend = normalResult.Backend,
            .Orientation = command.Orientation,
            .KNeighbors = command.KNeighbors,
            .MinimumNeighbors = command.MinimumNeighbors,
            .UseRadiusSearch = command.UseRadiusSearch,
            .Radius = command.Radius,
            .Error = ErrorForPointCloudNormalStatus(normalResult.Status),
        };
        CopyPointCloudNormalCounters(normalResult.Diagnostics, result);
        if (normalResult.Status != PointNormals::RecomputeStatus::Success)
        {
            result.Message = "Geometry.PointCloud.Normals failed with ";
            result.Message +=
                std::string(PointNormals::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!normalResult.Normals.IsValid() ||
            !PublishCanonicalVec3Normals(
                view.VertexSource->Properties,
                normalResult.Normals.Vector()))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus =
                PointNormals::RecomputeStatus::PropertyTypeConflict;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message =
                "Point-cloud vertex-normal publication failed because v:normal has an incompatible type or size.";
            return result;
        }

        Dirty::MarkVertexNormalsDirty(raw, *entity);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Recompute point-cloud vertex normals");
        result.Message = BuildPointCloudNormalsSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorPointCloudOutlierRemovalResult
    ApplySandboxEditorPointCloudOutlierRemovalCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPointCloudOutlierRemovalCommand& command)
    {
        SandboxEditorPointCloudOutlierRemovalResult result =
            MakePointCloudOutlierRemovalBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires an attached scene.";
            return result;
        }

        const bool statistical =
            command.Method ==
            SandboxEditorPointCloudOutlierMethod::Statistical;
        if (statistical)
        {
            if (command.KNeighbors == 0u ||
                !std::isfinite(command.StdDevMultiplier))
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.GeometryStatus =
                    Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Statistical outlier removal requires KNeighbors > 0 and a finite std-dev multiplier.";
                return result;
            }
        }
        else if (!std::isfinite(command.SearchRadius) ||
                 command.SearchRadius <= 0.0f)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Radius outlier removal requires a positive finite search radius.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Point-cloud outlier-removal target entity is stale or no longer live.";
            return result;
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
            view.VertexSource == nullptr)
        {
            result.Status =
                SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires selected point-cloud GeometrySources.";
            return result;
        }

        // Snapshot the original point property set (including any deleted slots
        // and the live deletion counter) so undo restores the entity exactly.
        Geometry::Vertices originalPoints = view.VertexSource->Properties;
        std::size_t originalNumDeleted = view.VertexSource->NumDeleted;
        if (!MirrorGeometrySourcePositionsToPointCloudStorage(originalPoints))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires a count-matched v:position property.";
            return result;
        }
        Geometry::PointCloud::Cloud beforeCloud{
            originalPoints,
            originalNumDeleted};

        // Build a separate live-only working cloud: bind the deletion counter and
        // garbage-collect so the GEOM-016 operators (which iterate every slot via
        // VerticesSize()) run over the live points only, and so kept/rejected
        // indices and result counts match the live point set rather than dead
        // slots. The full property set is carried so user attributes survive.
        Geometry::Vertices workPoints = view.VertexSource->Properties;
        std::size_t workNumDeleted = view.VertexSource->NumDeleted;
        if (!MirrorGeometrySourcePositionsToPointCloudStorage(workPoints))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires a count-matched v:position property.";
            return result;
        }
        Geometry::PointCloud::Cloud workCloud{workPoints, workNumDeleted};
        if (workCloud.HasGarbage())
            workCloud.GarbageCollection();

        std::optional<std::vector<glm::vec3>> snapshotPositions =
            CollectFiniteGeometryPositions(view.VertexSource->Properties);
        if (!snapshotPositions.has_value())
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires a count-matched v:position property.";
            return result;
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitPointCloudOutlierRemovalCpuJob(
                context,
                command,
                std::move(beforeCloud),
                std::move(workCloud),
                std::move(*snapshotPositions),
                GeometryMetadataSignatureForEntity(raw, *entity));
        }

        Geometry::PointCloud::OutlierRemovalResult removal{};
        if (statistical)
        {
            Geometry::PointCloud::StatisticalOutlierRemovalParams params{};
            params.KNeighbors = command.KNeighbors;
            params.StdDevMultiplier = command.StdDevMultiplier;
            removal =
                Geometry::PointCloud::RemoveStatisticalOutliers(
                    workCloud,
                    params);
        }
        else
        {
            Geometry::PointCloud::RadiusOutlierRemovalParams params{};
            params.SearchRadius = command.SearchRadius;
            params.MinNeighbors = command.MinNeighbors;
            removal =
                Geometry::PointCloud::RemoveRadiusOutliers(
                    workCloud,
                    params);
        }

        CopyPointCloudOutlierRemovalCounters(removal, result);

        if (removal.Status !=
            Geometry::PointCloud::OutlierRemovalStatus::Success)
        {
            result.Status =
                removal.Status ==
                        Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters
                    ? SandboxEditorCommandStatus::InvalidProcessingParameters
                    : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Geometry.PointCloud outlier removal failed with ";
            result.Message += DebugNameForOutlierRemovalStatus(removal.Status);
            result.Message += ".";
            return result;
        }

        // Compact the full-property working cloud down to the kept points by
        // deleting the rejected slots and garbage-collecting. This preserves
        // every surviving per-point property (normals, K-Means labels,
        // visualization scalars, ...), unlike `removal.Filtered`, which only
        // carries the Cloud built-ins (position/normal/color/radius).
        Geometry::PointCloud::Cloud afterCloud = workCloud;
        for (const std::size_t rejected : removal.RejectedIndices)
            afterCloud.DeletePoint(
                Geometry::VertexHandle{static_cast<std::uint32_t>(rejected)});
        afterCloud.GarbageCollection();

        const SandboxEditorCommandStatus status =
            CommitPointCloudReplacement(
                context,
                command.StableEntityId,
                statistical
                    ? "Remove statistical point-cloud outliers"
                    : "Remove radius point-cloud outliers",
                beforeCloud,
                afterCloud);
        result.Status = status;
        if (status != SandboxEditorCommandStatus::Applied)
        {
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Point-cloud outlier-removal publication failed; the target entity may no longer be live.";
            return result;
        }

        result.Message = BuildPointCloudOutlierRemovalSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorRegistrationResult ApplySandboxEditorRegistrationCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRegistrationCommand& command)
    {
        SandboxEditorRegistrationResult result =
            MakeRegistrationBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "ICP registration requires an attached scene.";
            return result;
        }
        if (command.SourceStableEntityId == command.TargetStableEntityId)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration requires two distinct source and target entities.";
            return result;
        }
        if (!ValidSandboxEditorICPVariant(command.Variant) ||
            command.MaxIterations == 0u ||
            !(command.InlierRatio > 0.0 && command.InlierRatio <= 1.0) ||
            !std::isfinite(command.MaxCorrespondenceDistance))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration requires a valid variant, a positive iteration count, an inlier ratio in (0, 1], and a finite correspondence distance.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> sourceEntity =
            ResolveStableEntity(raw, command.SourceStableEntityId);
        if (!sourceEntity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "ICP registration source entity is stale or no longer live.";
            return result;
        }
        const std::optional<ECS::EntityHandle> targetEntity =
            ResolveStableEntity(raw, command.TargetStableEntityId);
        if (!targetEntity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "ICP registration target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView sourceView =
            GS::BuildConstView(raw, *sourceEntity);
        if (GS::BuildSourceAvailability(sourceView).ProvenanceDomain !=
                GS::Domain::PointCloud ||
            sourceView.VertexSource == nullptr)
        {
            result.Status =
                SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration source must be a point-cloud entity.";
            return result;
        }
        const GS::ConstSourceView targetView =
            GS::BuildConstView(raw, *targetEntity);
        if (GS::BuildSourceAvailability(targetView).ProvenanceDomain !=
                GS::Domain::PointCloud ||
            targetView.VertexSource == nullptr)
        {
            result.Status =
                SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration target must be a point-cloud entity.";
            return result;
        }

        const std::optional<std::vector<glm::vec3>> sourcePoints =
            CollectFiniteGeometryPositions(
                sourceView.VertexSource->Properties);
        const std::optional<std::vector<glm::vec3>> targetPoints =
            CollectFiniteGeometryPositions(
                targetView.VertexSource->Properties);
        if (!sourcePoints.has_value() || !targetPoints.has_value())
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration requires both point clouds to expose a count-matched, finite v:position property.";
            return result;
        }
        result.SourcePointCount = sourcePoints->size();
        result.TargetPointCount = targetPoints->size();

        ECSC::Transform::Component* transform =
            raw.try_get<ECSC::Transform::Component>(*sourceEntity);
        if (transform == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingTransform;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "ICP registration source entity has no Transform to drive.";
            return result;
        }

        const ECSC::Transform::Component* targetTransform =
            raw.try_get<ECSC::Transform::Component>(*targetEntity);

        if (context.DerivedJobCommands.Available())
        {
            return SubmitRegistrationCpuJob(
                context,
                command,
                *sourcePoints,
                *targetPoints,
                *transform,
                targetTransform,
                GeometryMetadataSignatureForEntity(raw, *sourceEntity),
                GeometryMetadataSignatureForEntity(raw, *targetEntity));
        }

        // Register in world space: transform each cloud's local v:position by its
        // entity model matrix so a non-identity source/target Transform is
        // respected (identical local clouds with a translated target must still
        // converge onto the target). The ICP delta is composed with the existing
        // source model matrix before being written back as the source Transform.
        const glm::mat4 sourceModel = ModelMatrixFromTransform(*transform);
        glm::mat4 targetModel(1.0f);
        if (targetTransform != nullptr)
            targetModel = ModelMatrixFromTransform(*targetTransform);

        std::vector<glm::vec3> sourceWorld;
        sourceWorld.reserve(sourcePoints->size());
        for (const glm::vec3& p : *sourcePoints)
            sourceWorld.push_back(glm::vec3(sourceModel * glm::vec4(p, 1.0f)));
        std::vector<glm::vec3> targetWorld;
        targetWorld.reserve(targetPoints->size());
        for (const glm::vec3& p : *targetPoints)
            targetWorld.push_back(glm::vec3(targetModel * glm::vec4(p, 1.0f)));

        const glm::vec3 prealignDelta =
            ComputePointCentroid(std::span<const glm::vec3>(targetWorld)) -
            ComputePointCentroid(std::span<const glm::vec3>(sourceWorld));
        std::vector<glm::vec3> prealignedSourceWorld = sourceWorld;
        for (glm::vec3& point : prealignedSourceWorld)
            point += prealignDelta;
        glm::mat4 prealignPose(1.0f);
        prealignPose[3] = glm::vec4(prealignDelta, 1.0f);

        Reg::RegistrationParams params{};
        params.Variant = ToGeometryICPVariant(command.Variant);
        params.MaxIterations = command.MaxIterations;
        params.MaxCorrespondenceDistance =
            command.MaxCorrespondenceDistance > 0.0
                ? command.MaxCorrespondenceDistance
                : 1.0e6;
        params.InlierRatio = command.InlierRatio;

        const RegistrationAlignmentOutcome outcome =
            AlignPointClouds(prealignedSourceWorld, targetWorld, {}, params);
        if (!outcome.HasResult)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP rejected the selected point clouds (fewer than 3 points or invalid parameters).";
            return result;
        }

        result.HasResult = true;
        result.IterationsPerformed = outcome.Result.IterationsPerformed;
        result.TrajectoryLength = outcome.IterationCount();
        result.FinalRMSE = outcome.Result.FinalRMSE;
        result.Converged = outcome.Result.Converged;
        result.FinalInlierCount = outcome.Result.FinalInlierCount;

        const std::size_t step =
            std::min(command.TrajectoryStep, outcome.IterationCount());
        result.AppliedStep = step;
        const glm::mat4 pose =
            step == 0u ? glm::mat4(1.0f)
                       : TrajectoryPose(outcome, step) * prealignPose;

        // The pose is the world-space source->target delta; compose it with the
        // source's current model matrix and decompose the result back into the
        // local Transform (position/rotation, preserving the existing scale).
        ECSC::Transform::Component next = *transform;
        DecomposeModelToTransform(pose * sourceModel, next);

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult history =
                context.CommandHistory->Execute(
                    MakeTransformEditCommand(
                        EditorTransformEditCommand{
                            .Scene = context.Scene,
                            .StableEntityId = command.SourceStableEntityId,
                            .Before = *transform,
                            .After = next,
                            .Label = "Align point clouds (ICP)",
                        }));
            result.Status = ToSandboxEditorCommandStatus(history.Status);
        }
        else
        {
            *transform = next;
            raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(*sourceEntity);
            result.Status = SandboxEditorCommandStatus::Applied;
        }

        if (result.Status != SandboxEditorCommandStatus::Applied)
        {
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "ICP registration pose failed during editor history commit.";
            return result;
        }

        result.Error = Core::ErrorCode::Success;
        result.Message = BuildRegistrationSuccessMessage(result);
        return result;
    }

    SandboxEditorSession::SandboxEditorSession()
    {
    }

    SandboxEditorSession::~SandboxEditorSession()
    {
        Detach();
    }

    void SandboxEditorSession::Attach(Engine& engine)
    {
        Detach();
        m_Engine = &engine;
        m_AttachmentEpoch = std::make_shared<std::atomic_bool>(true);
        AttachKMeansGpuQueue(engine);
        m_ClusteringService = engine.Services().Find<ClusteringService>();
        if (m_ClusteringService != nullptr &&
            m_ClusteringService->Available())
        {
            m_KMeansCompletionSubscription =
                m_ClusteringService->SubscribeRunCompleted(
                    [epoch = m_AttachmentEpoch, this](
                        const KMeansRunCompleted& completed)
                    {
                        if (!AttachmentEpochIsActive(epoch))
                            return;
                        m_LastKMeansResult =
                            Detail::MakeSandboxEditorKMeansCompletionResult(
                                completed);
                        m_SelectedModelCache.Clear();
                    });
        }
        else
        {
            m_ClusteringService = nullptr;
        }
    }

    bool SandboxEditorSession::PrepareFrame(
        const SandboxEditorModelBuildRequest& request,
        std::string pendingAssetImportPath,
        const SandboxEditorAssetPayloadKind pendingAssetImportPayloadKind,
        std::string pendingSceneFilePath)
    {
        m_FramePrepared = false;
        m_Context = {};
        m_LastFrame = {};
        if (m_Engine == nullptr ||
            !AttachmentEpochIsActive(m_AttachmentEpoch))
        {
            return false;
        }
        const std::optional<RuntimeAssetImportEvent>& runtimeImport =
            m_Engine->GetAssetImportPipeline().GetLastAssetImportEvent();
        if (runtimeImport.has_value() &&
            runtimeImport->Sequence != m_LastObservedRuntimeImportSequence)
        {
            m_LastImportResult =
                BuildFileImportResultFromRuntimeEvent(*runtimeImport);
            m_LastObservedRuntimeImportSequence = runtimeImport->Sequence;
        }
        const std::optional<RuntimeSceneFileEvent>& runtimeSceneFile =
            m_Engine->GetSceneDocument().GetLastSceneFileEvent();
        if (runtimeSceneFile.has_value() &&
            runtimeSceneFile->Sequence !=
                m_LastObservedRuntimeSceneFileSequence)
        {
            m_LastSceneFileResult =
                BuildSceneFileResultFromRuntimeEvent(*runtimeSceneFile);
            m_LastObservedRuntimeSceneFileSequence =
                runtimeSceneFile->Sequence;
        }
        m_Context = BuildContextFromEngine(*m_Engine);
        SandboxEditorContext& context = m_Context;
        GuardAttachmentCommandSurfaces(context, m_AttachmentEpoch);
        context.SelectedModelCache = &m_SelectedModelCache;
        context.KMeansCommands.Required = true;
        if (m_ClusteringService != nullptr)
        {
            context.KMeansCommands.Submit =
                [epoch = m_AttachmentEpoch,
                 service = m_ClusteringService](RunKMeans request)
                {
                    if (!AttachmentEpochIsActive(epoch))
                        return CommandCorrelationId{};
                    return service->RunKMeans(std::move(request));
                };
        }
        context.KMeansGpuCommands = SandboxEditorKMeansGpuCommandSurface{
            .Submit =
                [epoch = m_AttachmentEpoch,
                 this](RuntimeKMeansGpuJobRequest request)
                {
                    if (!AttachmentEpochIsActive(epoch) ||
                        !m_KMeansGpuJobs)
                    {
                        return RuntimeKMeansGpuJobSubmission{
                            .Status = RuntimeKMeansGpuJobStatus::GpuUnavailable,
                            .GpuStatus = KMeansGpuStatus::DeviceUnavailable,
                            .Diagnostic =
                                "Sandbox editor K-Means GPU job queue is unavailable or its attachment expired.",
                        };
                    }
                    return m_KMeansGpuJobs->Submit(std::move(request));
                },
            .ConsumeCompleted =
                [epoch = m_AttachmentEpoch,
                 this]() -> std::optional<RuntimeKMeansGpuJobResult>
                {
                    if (!AttachmentEpochIsActive(epoch) ||
                        !m_KMeansGpuJobs)
                        return std::nullopt;
                    return m_KMeansGpuJobs->ConsumeCompleted();
                },
        };
        if (context.KMeansGpuCommands.Available())
        {
            while (std::optional<RuntimeKMeansGpuJobResult> completed =
                       context.KMeansGpuCommands.ConsumeCompleted())
            {
                m_LastKMeansResult =
                    Detail::PublishSandboxEditorKMeansGpuCompletion(
                        context,
                        *completed);
            }
        }
        if (const DerivedJobRegistry* derivedJobs =
                m_Engine->Services().Find<DerivedJobRegistry>();
            derivedJobs != nullptr)
        {
            m_DerivedJobSnapshot = derivedJobs->SnapshotAll();
        }
        else
        {
            m_DerivedJobSnapshot = {};
        }
        context.DerivedJobs = &m_DerivedJobSnapshot;
        context.MethodResultSinks.KMeans =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorKMeansResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastKMeansResult = std::move(result);
            };
        context.MethodResultSinks.ProgressivePoisson =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorProgressivePoissonResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastProgressivePoissonResult =
                        std::move(result);
            };
        context.MethodResultSinks.UvRegeneration =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorUvRegenerationCommandResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastUvRegenerationResult = std::move(result);
            };
        context.MethodResultSinks.Parameterization =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorParameterizationResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastParameterizationResult = std::move(result);
            };
        context.MethodResultSinks.MeshCurvature =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorMeshCurvatureResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshCurvatureResult = std::move(result);
            };
        context.MethodResultSinks.MeshDenoise =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorMeshDenoiseResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshDenoiseResult = std::move(result);
            };
        context.MethodResultSinks.MeshRemesh =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorMeshRemeshResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshRemeshResult = std::move(result);
            };
        context.MethodResultSinks.MeshSubdivide =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorMeshSubdivideResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshSubdivideResult = std::move(result);
            };
        context.MethodResultSinks.MeshSimplify =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorMeshSimplifyResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshSimplifyResult = std::move(result);
            };
        context.MethodResultSinks.MeshVertexNormals =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorMeshVertexNormalsResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshVertexNormalsResult =
                        std::move(result);
            };
        context.MethodResultSinks.GraphVertexNormals =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorGraphVertexNormalsResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastGraphVertexNormalsResult =
                        std::move(result);
            };
        context.MethodResultSinks.PointCloudVertexNormals =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorPointCloudVertexNormalsResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastPointCloudVertexNormalsResult =
                        std::move(result);
            };
        context.MethodResultSinks.PointCloudOutlierRemoval =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorPointCloudOutlierRemovalResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastPointCloudOutlierRemovalResult =
                        std::move(result);
            };
        context.MethodResultSinks.Registration =
            [epoch = m_AttachmentEpoch, this](
                SandboxEditorRegistrationResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastRegistrationResult = std::move(result);
            };
        context.PendingAssetImportPath =
            std::move(pendingAssetImportPath);
        context.PendingAssetImportPayloadKind =
            pendingAssetImportPayloadKind;
        context.PendingSceneFilePath =
            std::move(pendingSceneFilePath);
        if (m_LastSceneFileResult.has_value())
            context.LastSceneFileResult = &*m_LastSceneFileResult;
        if (m_LastImportResult.has_value())
            context.LastAssetImportResult = &*m_LastImportResult;
        if (m_LastKMeansResult.has_value())
            context.LastKMeansResult = &*m_LastKMeansResult;
        if (m_LastMeshDenoiseResult.has_value())
            context.LastMeshDenoiseResult =
                &*m_LastMeshDenoiseResult;
        if (m_LastMeshCurvatureResult.has_value())
            context.LastMeshCurvatureResult =
                &*m_LastMeshCurvatureResult;
        if (m_LastMeshRemeshResult.has_value())
            context.LastMeshRemeshResult =
                &*m_LastMeshRemeshResult;
        if (m_LastMeshSubdivideResult.has_value())
            context.LastMeshSubdivideResult =
                &*m_LastMeshSubdivideResult;
        if (m_LastMeshSimplifyResult.has_value())
            context.LastMeshSimplifyResult =
                &*m_LastMeshSimplifyResult;
        if (m_LastMeshVertexNormalsResult.has_value())
            context.LastMeshVertexNormalsResult =
                &*m_LastMeshVertexNormalsResult;
        if (m_LastGraphVertexNormalsResult.has_value())
            context.LastGraphVertexNormalsResult =
                &*m_LastGraphVertexNormalsResult;
        if (m_LastPointCloudVertexNormalsResult.has_value())
            context.LastPointCloudVertexNormalsResult =
                &*m_LastPointCloudVertexNormalsResult;
        if (m_LastPointCloudOutlierRemovalResult.has_value())
            context.LastPointCloudOutlierRemovalResult =
                &*m_LastPointCloudOutlierRemovalResult;
        if (m_LastUvRegenerationResult.has_value())
            context.LastUvRegenerationResult =
                &*m_LastUvRegenerationResult;
        if (m_LastParameterizationResult.has_value())
            context.LastParameterizationResult =
                &*m_LastParameterizationResult;
        if (m_LastProgressivePoissonResult.has_value())
            context.LastProgressivePoissonResult =
                &*m_LastProgressivePoissonResult;
        if (m_LastRegistrationResult.has_value())
            context.LastRegistrationResult =
                &*m_LastRegistrationResult;
        const Core::Extent2D viewport =
            context.CameraViewport.Width != 0u &&
                    context.CameraViewport.Height != 0u
                ? context.CameraViewport
                : Core::Extent2D{.Width = 1280u, .Height = 720u};
        const Graphics::RenderFrameInput recipeInput{
            .Viewport = viewport,
            .Camera = Graphics::CameraViewInput{.Valid = true},
        };
        m_RenderRecipeContext = Graphics::RenderRecipeConfigContext{
            .Renderer = Graphics::MakeCurrentRendererDescriptor(),
            .BaseRecipe = Graphics::MakeCurrentRendererRecipeDescriptor(),
            .BaseViewOutput =
                Graphics::MakeCurrentRendererViewOutputRecipe(recipeInput),
            .BaseBindings = Graphics::MakeCurrentRendererBindingSet(),
        };
        context.RenderRecipeContext = &m_RenderRecipeContext;
        context.RenderRecipeEditorState = &m_RenderRecipeState;
        context.RenderArtifacts = &m_RenderArtifactRegistry;
        EngineConfigControl* configControl =
            m_Engine->Services().Find<EngineConfigControl>();
        context.RenderRecipeCommandsAvailable =
            configControl != nullptr &&
            context.PreviewRenderRecipeDocument &&
            context.ApplyRenderRecipePreview;
        if (configControl != nullptr)
        {
            context.EngineConfigControlState =
                &configControl->GetEngineConfigControlState();
            context.PreviewEngineConfigDocument =
                [epoch = m_AttachmentEpoch,
                 configControl](const std::string& document,
                                const std::string& sourceId)
                {
                    if (!AttachmentEpochIsActive(epoch))
                        return Core::Config::EngineConfigLoadResult{};
                    return configControl
                        ->PreviewEngineConfigControlDocument(
                            document,
                            sourceId);
                };
            context.ApplyEngineConfigHotSubset =
                [epoch = m_AttachmentEpoch,
                 configControl](
                    const Core::Config::EngineConfigLoadResult&
                        loadResult)
                {
                    if (!AttachmentEpochIsActive(epoch))
                    {
                        return RuntimeEngineConfigApplyResult{
                            .Status =
                                RuntimeEngineConfigApplyStatus::
                                    Rejected,
                            .Source =
                                RuntimeConfigControlSource::Editor,
                        };
                    }
                    return configControl->ApplyEngineConfigHotSubset(
                        loadResult,
                        RuntimeConfigControlSource::Editor);
                };
            context.EngineConfigCommandsAvailable = true;
        }
        m_LastFrame = BuildSandboxEditorPanelFrame(context, request);
        context.ModelBuildStats = &m_LastFrame.ModelBuildStats;
        m_FramePrepared = true;
        return true;
    }

    bool SandboxEditorSession::VisitPreparedFrame(
        const SandboxEditorPreparedFrameVisitor& visitor)
    {
        if (!m_FramePrepared || !visitor)
            return false;

        visitor(SandboxEditorPreparedFrameView{
            .Context = m_Context,
            .Frame = m_LastFrame,
            .LastAssetImportResult = m_LastImportResult,
            .LastSceneFileResult = m_LastSceneFileResult,
            .LastUvRegenerationResult = m_LastUvRegenerationResult,
        });
        return true;
    }

    void SandboxEditorSession::Detach()
    {
        if (m_AttachmentEpoch != nullptr)
        {
            m_AttachmentEpoch->store(false, std::memory_order_release);
        }
        if (m_Engine != nullptr)
        {
            if (m_ClusteringService != nullptr &&
                m_KMeansCompletionSubscription.IsValid())
            {
                m_ClusteringService->Unsubscribe(
                    m_KMeansCompletionSubscription);
            }
            m_KMeansCompletionSubscription = {};
            m_ClusteringService = nullptr;
            DetachKMeansGpuQueue();
            m_Engine = nullptr;
        }
        else
        {
            m_KMeansCompletionSubscription = {};
            m_ClusteringService = nullptr;
            m_KMeansGpuParticipant = {};
            m_KMeansGpuJobs.reset();
        }
        m_AttachmentEpoch.reset();
        ResetAttachmentState();
    }

    void SandboxEditorSession::ResetAttachmentState()
    {
        m_FramePrepared = false;
        m_Context = {};
        m_LastFrame = {};
        m_SelectedModelCache = {};
        m_LastObservedRuntimeImportSequence = 0u;
        m_LastObservedRuntimeSceneFileSequence = 0u;
        m_LastImportResult.reset();
        m_LastSceneFileResult.reset();
        m_LastKMeansResult.reset();
        m_LastMeshDenoiseResult.reset();
        m_LastMeshCurvatureResult.reset();
        m_LastMeshRemeshResult.reset();
        m_LastMeshSubdivideResult.reset();
        m_LastMeshSimplifyResult.reset();
        m_LastMeshVertexNormalsResult.reset();
        m_LastGraphVertexNormalsResult.reset();
        m_LastPointCloudVertexNormalsResult.reset();
        m_LastPointCloudOutlierRemovalResult.reset();
        m_LastProgressivePoissonResult.reset();
        m_LastUvRegenerationResult.reset();
        m_LastParameterizationResult.reset();
        m_LastRegistrationResult.reset();
        m_DerivedJobSnapshot = {};
        m_RenderRecipeContext = {};
        m_RenderRecipeState = {};
        m_RenderArtifactRegistry = {};
    }

}
