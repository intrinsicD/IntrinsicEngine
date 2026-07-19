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
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
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
    namespace
    {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;
        namespace GK = Geometry::KMeans;
        namespace SurfaceSampling = Geometry::PointCloud::SurfaceSampling;
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
            const entt::registry& raw,
            const std::uint32_t stableId)
        {
            return Detail::ResolveSandboxMethodStableEntity(raw, stableId);
        }

        [[nodiscard]] std::uint64_t GeometryMetadataSignatureForEntity(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            return Detail::SandboxEditorGeometryMetadataSignatureForEntity(
                raw,
                entity);
        }

        void InvalidateSelectedModelCache(const SandboxEditorContext& context)
        {
            Detail::InvalidateSandboxMethodSelectedModelCache(context);
        }

        [[nodiscard]] std::optional<DerivedJobSnapshot>
        FindActiveEditorDerivedJob(
            const SandboxEditorContext& context,
            const DerivedJobKey& key)
        {
            return Detail::FindActiveSandboxMethodDerivedJob(context, key);
        }

        [[nodiscard]] std::string BuildActiveDerivedJobMessage(
            const std::string_view label,
            const DerivedJobSnapshot& job)
        {
            return Detail::BuildActiveSandboxMethodDerivedJobMessage(
                label,
                job);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyPointCloudPointState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::PointCloud::Cloud& cloud)
        {
            return Detail::ApplySandboxMethodPointCloudPointState(
                scene,
                stableEntityId,
                cloud);
        }

        [[nodiscard]] SandboxEditorCommandStatus ToSandboxEditorCommandStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            return Detail::ToSandboxMethodCommandStatus(status);
        }

        [[nodiscard]] const char* KMeansBackendId(
            SandboxEditorKMeansBackend backend) noexcept;

        [[nodiscard]] const char* KMeansBackendDisplayName(
            SandboxEditorKMeansBackend backend) noexcept;

        [[nodiscard]] std::string BuildKMeansSuccessMessage(
            SandboxEditorGeometryProcessingDomain domain,
            const SandboxEditorKMeansResult& result);

        [[nodiscard]] SandboxEditorKMeansResult MakeKMeansResult(
            const SandboxEditorCommandStatus status,
            const SandboxEditorGeometryProcessingDomain domain,
            const SandboxEditorKMeansBackend requestedBackend,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorKMeansResult{
                .Status = status,
                .Domain = domain,
                .RequestedBackend = requestedBackend,
                .RequestedBackendId = KMeansBackendId(requestedBackend),
                .RequestedBackendDisplayName =
                    KMeansBackendDisplayName(requestedBackend),
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool IsKMeansExecutionDomain(
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            using Domain = SandboxEditorGeometryProcessingDomain;
            return domain == Domain::MeshVertices ||
                   domain == Domain::GraphVertices ||
                   domain == Domain::PointCloudPoints;
        }

        [[nodiscard]] bool SourceViewSupportsKMeansDomain(
            const GS::MutableSourceView& view,
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            const GS::SourceAvailability sources =
                GS::BuildSourceAvailability(view);
            using Domain = SandboxEditorGeometryProcessingDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                       sources.Has(GS::SourceCapability::VertexPoints);
            case Domain::GraphVertices:
                return sources.ProvenanceDomain == GS::Domain::Graph &&
                       sources.Has(GS::SourceCapability::NodePoints);
            case Domain::PointCloudPoints:
                return sources.ProvenanceDomain == GS::Domain::PointCloud &&
                       sources.Has(GS::SourceCapability::VertexPoints);
            case Domain::None:
            case Domain::MeshEdges:
            case Domain::MeshHalfedges:
            case Domain::MeshFaces:
            case Domain::GraphEdges:
            case Domain::GraphHalfedges:
                return false;
            }
            return false;
        }

        [[nodiscard]] Geometry::PropertySet* KMeansTargetProperties(
            GS::MutableSourceView& view,
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            using Domain = SandboxEditorGeometryProcessingDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
            case Domain::PointCloudPoints:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case Domain::GraphVertices:
                return view.NodeSource != nullptr
                    ? &view.NodeSource->Properties
                    : nullptr;
            case Domain::None:
            case Domain::MeshEdges:
            case Domain::MeshHalfedges:
            case Domain::MeshFaces:
            case Domain::GraphEdges:
            case Domain::GraphHalfedges:
                return nullptr;
            }
            return nullptr;
        }

        [[nodiscard]] bool IsFinitePosition(const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>> CollectKMeansPositions(
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
                if (!IsFinitePosition(position))
                    return std::nullopt;
                points.push_back(position);
            }
            return points;
        }

        [[nodiscard]] glm::vec4 KMeansLabelColor(const std::uint32_t label)
        {
            const float h =
                std::fmod(0.61803398875f * static_cast<float>(label), 1.0f);
            constexpr float s = 0.65f;
            constexpr float v = 0.95f;

            const float hh = h * 6.0f;
            const float c = v * s;
            const float x =
                c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
            const float m = v - c;

            glm::vec3 rgb{0.0f};
            if (hh < 1.0f)
                rgb = {c, x, 0.0f};
            else if (hh < 2.0f)
                rgb = {x, c, 0.0f};
            else if (hh < 3.0f)
                rgb = {0.0f, c, x};
            else if (hh < 4.0f)
                rgb = {0.0f, x, c};
            else if (hh < 5.0f)
                rgb = {x, 0.0f, c};
            else
                rgb = {c, 0.0f, x};
            return glm::vec4(rgb + glm::vec3(m), 1.0f);
        }

        [[nodiscard]] bool PublishKMeansProperties(
            Geometry::PropertySet& properties,
            const SandboxEditorGeometryProcessingDomain domain,
            const GK::KMeansResult& result)
        {
            if (result.Labels.empty() ||
                result.Labels.size() != properties.Size())
            {
                return false;
            }

            const bool pointCloud =
                domain == SandboxEditorGeometryProcessingDomain::PointCloudPoints;
            const std::string labelName =
                pointCloud ? "p:kmeans_label" : "v:kmeans_label";
            const std::string colorName =
                pointCloud ? "p:kmeans_color" : "v:kmeans_color";

            auto labels = properties.GetOrAdd<std::uint32_t>(labelName, 0u);
            auto colors =
                properties.GetOrAdd<glm::vec4>(colorName, glm::vec4{1.0f});
            if (!labels || !colors)
                return false;
            if (labels.Vector().size() != result.Labels.size() ||
                colors.Vector().size() != result.Labels.size())
            {
                return false;
            }

            for (std::size_t i = 0u; i < result.Labels.size(); ++i)
            {
                labels.Vector()[i] = result.Labels[i];
                colors.Vector()[i] = KMeansLabelColor(result.Labels[i]);
            }

            if (!pointCloud)
            {
                auto labelFloats =
                    properties.GetOrAdd<float>("v:kmeans_label_f", 0.0f);
                if (!labelFloats ||
                    labelFloats.Vector().size() != result.Labels.size())
                {
                    return false;
                }
                for (std::size_t i = 0u; i < result.Labels.size(); ++i)
                    labelFloats.Vector()[i] =
                        static_cast<float>(result.Labels[i]);
            }
            return true;
        }

        inline constexpr const char* kKMeansCpuBackendId = "cpu_reference";
        inline constexpr const char* kKMeansCpuBackendDisplayName =
            "CPU reference";
        inline constexpr const char* kKMeansGpuBackendId =
            "gpu_vulkan_compute";
        inline constexpr const char* kKMeansGpuBackendDisplayName =
            "Vulkan compute";

        [[nodiscard]] const char* KMeansBackendId(
            const SandboxEditorKMeansBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorKMeansBackend::CpuReference:
                return kKMeansCpuBackendId;
            case SandboxEditorKMeansBackend::VulkanCompute:
                return kKMeansGpuBackendId;
            }
            return kKMeansCpuBackendId;
        }

        [[nodiscard]] const char* KMeansBackendDisplayName(
            const SandboxEditorKMeansBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorKMeansBackend::CpuReference:
                return kKMeansCpuBackendDisplayName;
            case SandboxEditorKMeansBackend::VulkanCompute:
                return kKMeansGpuBackendDisplayName;
            }
            return kKMeansCpuBackendDisplayName;
        }

        [[nodiscard]] GK::Backend ToKMeansGeometryBackend(
            const SandboxEditorKMeansBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorKMeansBackend::CpuReference:
                return GK::Backend::CPU;
            case SandboxEditorKMeansBackend::VulkanCompute:
                return GK::Backend::GPU;
            }
            return GK::Backend::CPU;
        }

        [[nodiscard]] SandboxEditorKMeansBackend MakeSandboxEditorKMeansBackend(
            const GK::Backend backend) noexcept
        {
            switch (backend)
            {
            case GK::Backend::CPU:
                return SandboxEditorKMeansBackend::CpuReference;
            case GK::Backend::GPU:
                return SandboxEditorKMeansBackend::VulkanCompute;
            }
            return SandboxEditorKMeansBackend::CpuReference;
        }

        [[nodiscard]] ClusteringDomain ToRuntimeClusteringDomain(
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            switch (domain)
            {
            case SandboxEditorGeometryProcessingDomain::MeshVertices:
                return ClusteringDomain::MeshVertices;
            case SandboxEditorGeometryProcessingDomain::GraphVertices:
                return ClusteringDomain::GraphVertices;
            case SandboxEditorGeometryProcessingDomain::PointCloudPoints:
                return ClusteringDomain::PointCloudPoints;
            case SandboxEditorGeometryProcessingDomain::None:
            case SandboxEditorGeometryProcessingDomain::MeshEdges:
            case SandboxEditorGeometryProcessingDomain::MeshHalfedges:
            case SandboxEditorGeometryProcessingDomain::MeshFaces:
            case SandboxEditorGeometryProcessingDomain::GraphEdges:
            case SandboxEditorGeometryProcessingDomain::GraphHalfedges:
                return ClusteringDomain::PointCloudPoints;
            }
            return ClusteringDomain::PointCloudPoints;
        }

        [[nodiscard]] SandboxEditorGeometryProcessingDomain
        ToSandboxEditorGeometryProcessingDomain(
            const ClusteringDomain domain) noexcept
        {
            switch (domain)
            {
            case ClusteringDomain::MeshVertices:
                return SandboxEditorGeometryProcessingDomain::MeshVertices;
            case ClusteringDomain::GraphVertices:
                return SandboxEditorGeometryProcessingDomain::GraphVertices;
            case ClusteringDomain::PointCloudPoints:
                return SandboxEditorGeometryProcessingDomain::PointCloudPoints;
            }
            return SandboxEditorGeometryProcessingDomain::None;
        }

        [[nodiscard]] ClusteringBackend ToRuntimeClusteringBackend(
            const SandboxEditorKMeansBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorKMeansBackend::CpuReference:
                return ClusteringBackend::CpuReference;
            case SandboxEditorKMeansBackend::VulkanCompute:
                return ClusteringBackend::VulkanCompute;
            }
            return ClusteringBackend::CpuReference;
        }

        [[nodiscard]] SandboxEditorKMeansBackend ToSandboxEditorKMeansBackend(
            const ClusteringBackend backend) noexcept
        {
            switch (backend)
            {
            case ClusteringBackend::CpuReference:
                return SandboxEditorKMeansBackend::CpuReference;
            case ClusteringBackend::VulkanCompute:
                return SandboxEditorKMeansBackend::VulkanCompute;
            }
            return SandboxEditorKMeansBackend::CpuReference;
        }

        [[nodiscard]] SandboxEditorCommandStatus ToSandboxEditorCommandStatus(
            const KMeansRunStatus status) noexcept
        {
            switch (status)
            {
            case KMeansRunStatus::Queued:
                return SandboxEditorCommandStatus::Pending;
            case KMeansRunStatus::Applied:
                return SandboxEditorCommandStatus::Applied;
            case KMeansRunStatus::MissingScene:
                return SandboxEditorCommandStatus::MissingScene;
            case KMeansRunStatus::InvalidProcessingParameters:
                return SandboxEditorCommandStatus::InvalidProcessingParameters;
            case KMeansRunStatus::StaleEntity:
                return SandboxEditorCommandStatus::StaleEntity;
            case KMeansRunStatus::UnsupportedGeometryDomain:
                return SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            case KMeansRunStatus::GeometryProcessingFailed:
            case KMeansRunStatus::StaleSource:
            case KMeansRunStatus::StaleWorld:
            case KMeansRunStatus::ModuleUnavailable:
                return SandboxEditorCommandStatus::GeometryProcessingFailed;
            }
            return SandboxEditorCommandStatus::GeometryProcessingFailed;
        }

        [[nodiscard]] SandboxEditorKMeansResult
        MakeSandboxEditorKMeansResult(
            const KMeansRunCompleted& completed)
        {
            const SandboxEditorGeometryProcessingDomain domain =
                ToSandboxEditorGeometryProcessingDomain(completed.Domain);
            const SandboxEditorKMeansBackend requested =
                ToSandboxEditorKMeansBackend(completed.RequestedBackend);
            const SandboxEditorKMeansBackend actual =
                ToSandboxEditorKMeansBackend(completed.ActualBackend);
            return SandboxEditorKMeansResult{
                .Status = ToSandboxEditorCommandStatus(completed.Status),
                .Domain = domain,
                .LabelCount = completed.LabelCount,
                .ClusterCount = completed.ClusterCount,
                .Iterations = completed.Iterations,
                .Converged = completed.Converged,
                .Inertia = completed.Inertia,
                .MaxDistanceIndex = completed.MaxDistanceIndex,
                .RequestedBackend = requested,
                .ActualBackend = actual,
                .RequestedBackendId = KMeansBackendId(requested),
                .RequestedBackendDisplayName =
                    KMeansBackendDisplayName(requested),
                .BackendId = KMeansBackendId(actual),
                .BackendDisplayName = KMeansBackendDisplayName(actual),
                .FellBackToCpu = completed.FellBackToCpu,
                .BackendFallbackReason =
                    completed.FellBackToCpu
                        ? "K-Means request ran on the runtime CPU reference job lane."
                        : std::string{},
                .Error = completed.Error,
                .Message = completed.Message,
            };
        }

        [[nodiscard]] std::optional<GK::KMeansResult> RunKMeansForSandbox(
            const std::span<const glm::vec3> points,
            const GK::KMeansParams& params,
            RHI::IDevice* device)
        {
            const GK::Backend requestedBackend = params.Compute;
            if (device != nullptr)
                return ClusterKMeans(points, params, *device);

            GK::KMeansParams cpuParams = params;
            cpuParams.Compute = GK::Backend::CPU;
            std::optional<GK::KMeansResult> result =
                GK::Cluster(points, cpuParams);
            if (result.has_value())
            {
                result->RequestedBackend = requestedBackend;
                result->ActualBackend = GK::Backend::CPU;
                result->FellBackToCPU =
                    requestedBackend == GK::Backend::GPU;
            }
            return result;
        }

        [[nodiscard]] SandboxEditorKMeansResult MakePendingKMeansGpuResult(
            const SandboxEditorKMeansCommand& command,
            const RuntimeKMeansGpuJobSubmission& submission,
            const std::uint32_t pointCount)
        {
            const SandboxEditorKMeansBackend backend =
                SandboxEditorKMeansBackend::VulkanCompute;
            SandboxEditorKMeansResult result{
                .Status = SandboxEditorCommandStatus::Pending,
                .Domain = command.Domain,
                .LabelCount = pointCount,
                .ClusterCount = std::min(command.ClusterCount, pointCount),
                .RequestedBackend = backend,
                .ActualBackend = backend,
                .RequestedBackendId = KMeansBackendId(backend),
                .RequestedBackendDisplayName =
                    KMeansBackendDisplayName(backend),
                .BackendId = KMeansBackendId(backend),
                .BackendDisplayName = KMeansBackendDisplayName(backend),
                .FellBackToCpu = false,
                .Error = Core::ErrorCode::Success,
            };
            if (submission.Status == RuntimeKMeansGpuJobStatus::Busy)
            {
                result.Message = submission.Diagnostic.empty()
                    ? "K-Means Vulkan compute job is already pending."
                    : submission.Diagnostic;
            }
            else
            {
                result.Message = "K-Means Vulkan compute job queued";
                if (submission.Sequence != 0u)
                {
                    result.Message += " (sequence ";
                    result.Message += std::to_string(submission.Sequence);
                    result.Message += ")";
                }
                result.Message += ".";
            }
            return result;
        }

        [[nodiscard]] ProgressiveGeometryDomain ToKMeansDerivedJobDomain(
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            switch (domain)
            {
            case SandboxEditorGeometryProcessingDomain::MeshVertices:
                return ProgressiveGeometryDomain::MeshVertex;
            case SandboxEditorGeometryProcessingDomain::GraphVertices:
                return ProgressiveGeometryDomain::GraphVertex;
            case SandboxEditorGeometryProcessingDomain::PointCloudPoints:
                return ProgressiveGeometryDomain::Point;
            case SandboxEditorGeometryProcessingDomain::None:
            case SandboxEditorGeometryProcessingDomain::MeshEdges:
            case SandboxEditorGeometryProcessingDomain::MeshHalfedges:
            case SandboxEditorGeometryProcessingDomain::MeshFaces:
            case SandboxEditorGeometryProcessingDomain::GraphEdges:
            case SandboxEditorGeometryProcessingDomain::GraphHalfedges:
                return ProgressiveGeometryDomain::Unknown;
            }
            return ProgressiveGeometryDomain::Unknown;
        }

        [[nodiscard]] ProgressiveSlotSemantic ToKMeansDerivedJobSemantic(
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            return domain == SandboxEditorGeometryProcessingDomain::PointCloudPoints
                ? ProgressiveSlotSemantic::PointColor
                : ProgressiveSlotSemantic::Albedo;
        }

        [[nodiscard]] bool SameKMeansInputPositions(
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

        [[nodiscard]] DerivedJobApplyValidation ValidateKMeansCpuJobApply(
            const SandboxEditorContext& context,
            const SandboxEditorKMeansCommand& command,
            const std::vector<glm::vec3>& points)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
                return DerivedJobApplyValidation::MissingEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            if (!view.Valid() ||
                !SourceViewSupportsKMeansDomain(view, command.Domain))
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            Geometry::PropertySet* properties =
                KMeansTargetProperties(view, command.Domain);
            if (properties == nullptr)
                return DerivedJobApplyValidation::StaleGeometryGeneration;

            std::optional<std::vector<glm::vec3>> current =
                CollectKMeansPositions(*properties);
            if (!current.has_value() ||
                !SameKMeansInputPositions(*current, points))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            return DerivedJobApplyValidation::Current;
        }

        struct SandboxEditorKMeansCpuJobState
        {
            SandboxEditorKMeansCommand Command{};
            std::vector<glm::vec3> Points{};
            GK::KMeansParams Params{};
            std::string BackendFallbackReason{};
            std::optional<GK::KMeansResult> Clustered{};
        };

        [[nodiscard]] SandboxEditorKMeansResult MakeCompletedKMeansResult(
            const SandboxEditorGeometryProcessingDomain domain,
            const GK::KMeansResult& clustered)
        {
            const SandboxEditorKMeansBackend requestedBackend =
                MakeSandboxEditorKMeansBackend(clustered.RequestedBackend);
            const SandboxEditorKMeansBackend actualBackend =
                MakeSandboxEditorKMeansBackend(clustered.ActualBackend);
            SandboxEditorKMeansResult result{
                .Status = SandboxEditorCommandStatus::Applied,
                .Domain = domain,
                .LabelCount = static_cast<std::uint32_t>(clustered.Labels.size()),
                .ClusterCount = static_cast<std::uint32_t>(clustered.Centroids.size()),
                .Iterations = clustered.Iterations,
                .Converged = clustered.Converged,
                .Inertia = clustered.Inertia,
                .MaxDistanceIndex = clustered.MaxDistanceIndex,
                .RequestedBackend = requestedBackend,
                .ActualBackend = actualBackend,
                .RequestedBackendId = KMeansBackendId(requestedBackend),
                .RequestedBackendDisplayName =
                    KMeansBackendDisplayName(requestedBackend),
                .BackendId = KMeansBackendId(actualBackend),
                .BackendDisplayName = KMeansBackendDisplayName(actualBackend),
                .FellBackToCpu = clustered.FellBackToCPU,
                .Error = Core::ErrorCode::Success,
            };
            return result;
        }

        [[nodiscard]] SandboxEditorKMeansResult PublishCompletedKMeansGpuJob(
            const SandboxEditorContext& context,
            const RuntimeKMeansGpuJobResult& job)
        {
            const auto domain =
                static_cast<SandboxEditorGeometryProcessingDomain>(job.DomainTag);
            if (!job.Succeeded())
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    domain,
                    SandboxEditorKMeansBackend::VulkanCompute,
                    Core::ErrorCode::Unknown,
                    job.Diagnostic.empty()
                        ? "K-Means Vulkan compute job failed before publication."
                        : job.Diagnostic);
            }

            if (context.Scene == nullptr)
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::MissingScene,
                    domain,
                    SandboxEditorKMeansBackend::VulkanCompute,
                    Core::ErrorCode::InvalidState,
                    "Scene registry is unavailable for completed K-Means GPU publication.");
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::StaleEntity,
                    domain,
                    SandboxEditorKMeansBackend::VulkanCompute,
                    Core::ErrorCode::ResourceNotFound,
                    "K-Means GPU target entity is stale or no longer live.");
            }

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            if (!view.Valid() || !SourceViewSupportsKMeansDomain(view, domain))
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                    domain,
                    SandboxEditorKMeansBackend::VulkanCompute,
                    Core::ErrorCode::InvalidArgument,
                    "Completed K-Means GPU job no longer matches a writable GeometrySources domain.");
            }

            Geometry::PropertySet* properties =
                KMeansTargetProperties(view, domain);
            if (properties == nullptr)
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                    domain,
                    SandboxEditorKMeansBackend::VulkanCompute,
                    Core::ErrorCode::InvalidArgument,
                    "Completed K-Means GPU domain has no writable property set.");
            }

            if (!PublishKMeansProperties(*properties, domain, job.Result))
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    domain,
                    SandboxEditorKMeansBackend::VulkanCompute,
                    Core::ErrorCode::TypeMismatch,
                    "K-Means GPU result publication failed because output properties have incompatible types or sizes.");
            }

            Dirty::MarkVertexAttributesDirty(raw, *entity);
            SandboxEditorKMeansResult result =
                MakeCompletedKMeansResult(domain, job.Result);
            result.Message = BuildKMeansSuccessMessage(domain, result);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty("Run K-Means");
            return result;
        }

        [[nodiscard]] SandboxEditorKMeansResult MakePendingKMeansCpuJobResult(
            const SandboxEditorKMeansCommand& command,
            const DerivedJobHandle handle,
            const std::uint32_t pointCount)
        {
            const SandboxEditorKMeansBackend actual =
                SandboxEditorKMeansBackend::CpuReference;
            SandboxEditorKMeansResult result{
                .Status = SandboxEditorCommandStatus::Pending,
                .Domain = command.Domain,
                .LabelCount = pointCount,
                .ClusterCount = std::min(command.ClusterCount, pointCount),
                .RequestedBackend = command.Backend,
                .ActualBackend = actual,
                .RequestedBackendId = KMeansBackendId(command.Backend),
                .RequestedBackendDisplayName =
                    KMeansBackendDisplayName(command.Backend),
                .BackendId = KMeansBackendId(actual),
                .BackendDisplayName = KMeansBackendDisplayName(actual),
                .FellBackToCpu =
                    command.Backend ==
                    SandboxEditorKMeansBackend::VulkanCompute,
                .Error = Core::ErrorCode::Success,
            };
            result.Message = "K-Means CPU job queued";
            if (handle.IsValid())
            {
                result.Message += " (job ";
                result.Message += std::to_string(handle.Index);
                result.Message += ":";
                result.Message += std::to_string(handle.Generation);
                result.Message += ")";
            }
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorKMeansResult MakePendingRuntimeKMeansResult(
            const SandboxEditorKMeansCommand& command,
            const CommandCorrelationId correlation,
            const std::uint32_t pointCount)
        {
            const SandboxEditorKMeansBackend actual =
                SandboxEditorKMeansBackend::CpuReference;
            SandboxEditorKMeansResult result{
                .Status = SandboxEditorCommandStatus::Pending,
                .Domain = command.Domain,
                .LabelCount = pointCount,
                .ClusterCount = std::min(command.ClusterCount, pointCount),
                .RequestedBackend = command.Backend,
                .ActualBackend = actual,
                .RequestedBackendId = KMeansBackendId(command.Backend),
                .RequestedBackendDisplayName =
                    KMeansBackendDisplayName(command.Backend),
                .BackendId = KMeansBackendId(actual),
                .BackendDisplayName = KMeansBackendDisplayName(actual),
                .FellBackToCpu =
                    command.Backend ==
                    SandboxEditorKMeansBackend::VulkanCompute,
                .Error = Core::ErrorCode::Success,
            };
            result.Message = "K-Means runtime job queued";
            if (correlation.IsValid())
            {
                result.Message += " (command ";
                result.Message += std::to_string(correlation.Value);
                result.Message += ")";
            }
            result.Message += ".";
            return result;
        }

        [[nodiscard]] Core::Result PublishCompletedKMeansCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorKMeansCpuJobState& job)
        {
            if (!job.Clustered.has_value())
                return Core::Err(Core::ErrorCode::Unknown);
            if (context.Scene == nullptr)
                return Core::Err(Core::ErrorCode::InvalidState);

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.Command.StableEntityId);
            if (!entity.has_value())
                return Core::Err(Core::ErrorCode::ResourceNotFound);

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            if (!view.Valid() ||
                !SourceViewSupportsKMeansDomain(view, job.Command.Domain))
            {
                return Core::Err(Core::ErrorCode::InvalidArgument);
            }

            Geometry::PropertySet* properties =
                KMeansTargetProperties(view, job.Command.Domain);
            if (properties == nullptr)
                return Core::Err(Core::ErrorCode::InvalidArgument);

            if (!PublishKMeansProperties(
                    *properties,
                    job.Command.Domain,
                    *job.Clustered))
            {
                return Core::Err(Core::ErrorCode::TypeMismatch);
            }

            Dirty::MarkVertexAttributesDirty(raw, *entity);
            SandboxEditorKMeansResult result =
                MakeCompletedKMeansResult(job.Command.Domain, *job.Clustered);
            result.BackendFallbackReason = job.BackendFallbackReason;
            result.Message =
                BuildKMeansSuccessMessage(job.Command.Domain, result);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty("Run K-Means");
            InvalidateSelectedModelCache(context);
            if (context.MethodResultSinks.KMeans)
                context.MethodResultSinks.KMeans(std::move(result));
            return Core::Ok();
        }

        [[nodiscard]] SandboxEditorKMeansResult SubmitKMeansCpuDerivedJob(
            const SandboxEditorContext& context,
            const SandboxEditorKMeansCommand& command,
            std::vector<glm::vec3> points,
            GK::KMeansParams params,
            std::string backendFallbackReason)
        {
            auto state = std::make_shared<SandboxEditorKMeansCpuJobState>();
            state->Command = command;
            state->Points = std::move(points);
            state->Params = params;
            state->BackendFallbackReason = std::move(backendFallbackReason);

            const std::uint32_t pointCount =
                static_cast<std::uint32_t>(state->Points.size());
            DerivedJobDesc desc{
                .Key = DerivedJobKey{
                    .EntityId = command.StableEntityId,
                    .Domain = ToKMeansDerivedJobDomain(command.Domain),
                    .OutputSemantic = ToKMeansDerivedJobSemantic(command.Domain),
                    .OutputName = "kmeans_label",
                },
                .Name = "Sandbox.KMeans.CPU",
                .RequestedJobDomain = ProgressiveJobDomain::Cpu,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (state->Points.size() + 1023u) / 1024u)),
                .Scope = context.World,
                .Execute =
                    [state]() -> DerivedJobWorkerResult
                    {
                        std::optional<GK::KMeansResult> clustered =
                            RunKMeansForSandbox(
                                std::span<const glm::vec3>{
                                    state->Points.data(),
                                    state->Points.size()},
                                state->Params,
                                nullptr);
                        if (!clustered.has_value())
                            return std::unexpected(Core::ErrorCode::Unknown);

                        state->Clustered = std::move(*clustered);
                        return DerivedJobOutput{
                            .PayloadToken = 0u,
                            .NormalizedProgress = 1.0f,
                            .ProgressDeterminate = true,
                            .Diagnostic = "K-Means CPU result ready",
                        };
                    },
                .ValidateOnMainThread =
                    [context, state]()
                    {
                        return ValidateKMeansCpuJobApply(
                            context,
                            state->Command,
                            state->Points);
                    },
                .ApplyOnMainThread =
                    [context, state](DerivedJobApplyContext&) -> Core::Result
                    {
                        return PublishCompletedKMeansCpuJob(context, *state);
                    },
            };

            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorKMeansResult pending =
                    MakePendingKMeansCpuJobResult(
                        command,
                        active->Handle,
                        pointCount);
                pending.Message =
                    BuildActiveDerivedJobMessage("K-Means CPU", *active);
                pending.BackendFallbackReason = state->BackendFallbackReason;
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    command.Domain,
                    command.Backend,
                    Core::ErrorCode::InvalidState,
                    "K-Means CPU job submission was rejected by the runtime job lane.");
            }

            SandboxEditorKMeansResult pending =
                MakePendingKMeansCpuJobResult(command, handle, pointCount);
            pending.BackendFallbackReason = state->BackendFallbackReason;
            return pending;
        }

        [[nodiscard]] std::string BuildKMeansFallbackReason(
            const SandboxEditorKMeansResult& result,
            const RHI::IDevice* device)
        {
            if (!result.FellBackToCpu)
                return {};
            if (device == nullptr)
            {
                return "Vulkan compute requested but no RHI device is attached; ran CPU reference.";
            }
            if (!device->IsOperational())
            {
                return "Vulkan compute requested but the RHI device is not operational; ran CPU reference.";
            }
            return "Vulkan compute requested but the runtime K-Means GPU queue is unavailable; ran CPU reference.";
        }

        [[nodiscard]] std::string BuildKMeansSuccessMessage(
            const SandboxEditorGeometryProcessingDomain domain,
            const SandboxEditorKMeansResult& result)
        {
            std::string message = "K-Means (requested ";
            message += result.RequestedBackendId.empty()
                ? KMeansBackendId(result.RequestedBackend)
                : result.RequestedBackendId;
            message += ", actual ";
            message += result.BackendId.empty()
                ? KMeansBackendId(result.ActualBackend)
                : result.BackendId;
            message += ") completed for ";
            message += DebugNameForSandboxEditorGeometryProcessingDomain(domain);
            message += " (labels=";
            message += std::to_string(result.LabelCount);
            message += ", clusters=";
            message += std::to_string(result.ClusterCount);
            message += ", iterations=";
            message += std::to_string(result.Iterations);
            message += ").";
            return message;
        }

        inline constexpr const char* kProgressivePoissonLevelProperty =
            "p:poisson_level";
        inline constexpr const char* kProgressivePoissonPhaseProperty =
            "p:poisson_phase";
        inline constexpr const char* kProgressivePoissonSplatRadiusProperty =
            "p:poisson_splat_radius";
        inline constexpr const char* kProgressivePoissonPrefixVisibleProperty =
            "p:poisson_prefix_visible";
        inline constexpr const char* kProgressivePoissonCpuBackendDisplayName =
            "CPU reference";
        inline constexpr const char* kProgressivePoissonGpuBackendId =
            "gpu_vulkan_compute";
        inline constexpr const char* kProgressivePoissonGpuBackendDisplayName =
            "Vulkan compute";

        [[nodiscard]] const char* ProgressivePoissonBackendId(
            const SandboxEditorProgressivePoissonBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorProgressivePoissonBackend::CpuReference:
                return PPR::kBackendId;
            case SandboxEditorProgressivePoissonBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendId;
            }
            return PPR::kBackendId;
        }

        [[nodiscard]] const char* ProgressivePoissonBackendDisplayName(
            const SandboxEditorProgressivePoissonBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorProgressivePoissonBackend::CpuReference:
                return kProgressivePoissonCpuBackendDisplayName;
            case SandboxEditorProgressivePoissonBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendDisplayName;
            }
            return kProgressivePoissonCpuBackendDisplayName;
        }

        [[nodiscard]] const char* ProgressivePoissonChannelPropertyName(
            const SandboxEditorProgressivePoissonChannel channel) noexcept
        {
            switch (channel)
            {
            case SandboxEditorProgressivePoissonChannel::Level:
                return kProgressivePoissonLevelProperty;
            case SandboxEditorProgressivePoissonChannel::Phase:
                return kProgressivePoissonPhaseProperty;
            case SandboxEditorProgressivePoissonChannel::SplatRadius:
                return kProgressivePoissonSplatRadiusProperty;
            case SandboxEditorProgressivePoissonChannel::PrefixVisible:
                return kProgressivePoissonPrefixVisibleProperty;
            }
            return kProgressivePoissonLevelProperty;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        MakeProgressivePoissonResult(
            const SandboxEditorCommandStatus status,
            const SandboxEditorProgressivePoissonChannel channel,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorProgressivePoissonResult{
                .Status = status,
                .Channel = channel,
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool IsValidProgressivePoissonConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            return (config.Dimension == 2u || config.Dimension == 3u) &&
                   config.GridWidth > 0u &&
                   config.MaxLevels > 0u &&
                   std::isfinite(config.HashLoadFactor) &&
                   config.HashLoadFactor > 0.0f &&
                   std::isfinite(config.RadiusAlpha);
        }

        [[nodiscard]] bool IsValidProgressivePoissonMeshSurfaceConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            return config.MeshSurfaceSampleCount > 0u &&
                   std::isfinite(config.MeshSurfaceMinTriangleArea) &&
                   config.MeshSurfaceMinTriangleArea > 0.0;
        }

        [[nodiscard]] std::uint32_t SaturatingUint32(
            const std::size_t value) noexcept
        {
            return value > std::numeric_limits<std::uint32_t>::max()
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] PPR::Config ToProgressivePoissonReferenceConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            PPR::Config out{};
            out.Dimension = config.Dimension;
            out.GridWidth = config.GridWidth;
            out.MaxLevels = config.MaxLevels;
            out.HashLoadFactor = config.HashLoadFactor;
            out.RadiusAlpha = config.RadiusAlpha;
            out.RandomizeGridOrigin = config.RandomizeGridOrigin;
            out.GridOriginSeed = config.GridOriginSeed;
            out.ShuffleWithinLevels = config.ShuffleWithinLevels;
            out.ShuffleSeed = config.ShuffleSeed;
            return out;
        }

        [[nodiscard]] SurfaceSampling::Params ToProgressivePoissonSurfaceParams(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            SurfaceSampling::Params out{};
            out.SampleCount =
                static_cast<std::int64_t>(config.MeshSurfaceSampleCount);
            out.Seed = config.MeshSurfaceSampleSeed;
            out.MinTriangleArea = config.MeshSurfaceMinTriangleArea;
            out.InterpolateVertexNormals =
                config.MeshSurfaceInterpolateNormals;
            return out;
        }

        [[nodiscard]] std::uint32_t ClampProgressivePoissonPrefix(
            const std::uint32_t requested,
            const std::uint32_t accepted) noexcept
        {
            if (requested == 0u)
                return accepted;
            return std::min(requested, accepted);
        }

        [[nodiscard]] bool PublishProgressivePoissonProperties(
            Geometry::PropertySet& properties,
            const PPR::Result& method,
            const SandboxEditorProgressivePoissonConfig& config,
            const std::uint32_t prefixCount)
        {
            const std::size_t pointCount = properties.Size();
            std::vector<float> levels(pointCount, -1.0f);
            std::vector<float> phases(pointCount, -1.0f);
            std::vector<float> splatRadii(pointCount, 0.0f);
            std::vector<float> prefixVisible(pointCount, 0.0f);

            const std::uint32_t phaseCount = config.Dimension == 3u ? 8u : 4u;
            for (std::size_t level = 0u;
                 level + 1u < method.LevelOffsets.size();
                 ++level)
            {
                const std::uint32_t begin = method.LevelOffsets[level];
                const std::uint32_t end = method.LevelOffsets[level + 1u];
                for (std::uint32_t rank = begin; rank < end; ++rank)
                {
                    if (rank >= method.Order.size())
                        return false;
                    const std::uint32_t pointIndex = method.Order[rank];
                    if (pointIndex >= pointCount)
                        return false;

                    levels[pointIndex] = static_cast<float>(level);
                    phases[pointIndex] = static_cast<float>(
                        (rank - begin) % phaseCount);
                    if (rank < method.SplatRadii.size())
                        splatRadii[pointIndex] = method.SplatRadii[rank];
                    prefixVisible[pointIndex] = rank < prefixCount ? 1.0f : 0.0f;
                }
            }

            auto levelProp = properties.GetOrAdd<float>(
                kProgressivePoissonLevelProperty,
                -1.0f);
            auto phaseProp = properties.GetOrAdd<float>(
                kProgressivePoissonPhaseProperty,
                -1.0f);
            auto splatProp = properties.GetOrAdd<float>(
                kProgressivePoissonSplatRadiusProperty,
                0.0f);
            auto prefixProp = properties.GetOrAdd<float>(
                kProgressivePoissonPrefixVisibleProperty,
                0.0f);
            if (!levelProp || !phaseProp || !splatProp || !prefixProp)
                return false;

            levelProp.Vector() = std::move(levels);
            phaseProp.Vector() = std::move(phases);
            splatProp.Vector() = std::move(splatRadii);
            prefixProp.Vector() = std::move(prefixVisible);
            return true;
        }

        [[nodiscard]] std::string FormatProgressivePoissonLevelCounts(
            const std::vector<std::uint32_t>& counts)
        {
            if (counts.empty())
                return "none";

            std::string text{};
            for (std::size_t i = 0u; i < counts.size(); ++i)
            {
                if (i != 0u)
                    text += ", ";
                text += std::to_string(i);
                text += ":";
                text += std::to_string(counts[i]);
            }
            return text;
        }

        struct ProgressivePoissonBackendResolution
        {
            SandboxEditorProgressivePoissonBackend Requested{
                SandboxEditorProgressivePoissonBackend::CpuReference};
            SandboxEditorProgressivePoissonBackend Actual{
                SandboxEditorProgressivePoissonBackend::CpuReference};
            std::string FallbackReason{};
        };

        [[nodiscard]] ProgressivePoissonGpuConfig ToProgressivePoissonGpuConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            return ProgressivePoissonGpuConfig{
                .Dimension = config.Dimension,
                .GridWidth = config.GridWidth,
                .MaxLevels = config.MaxLevels,
                .HashLoadFactor = config.HashLoadFactor,
                .RadiusAlpha = config.RadiusAlpha,
                .RandomizeGridOrigin = config.RandomizeGridOrigin,
                .GridOriginSeed = config.GridOriginSeed,
                .ShuffleWithinLevels = config.ShuffleWithinLevels,
                .ShuffleSeed = config.ShuffleSeed,
            };
        }

        [[nodiscard]] ProgressivePoissonBackendResolution
        ResolveProgressivePoissonBackend(
            const SandboxEditorProgressivePoissonBackend requested,
            const SandboxEditorProgressivePoissonConfig& config,
            const std::uint32_t inputCount,
            RHI::IDevice* device)
        {
            ProgressivePoissonBackendResolution resolved{};
            resolved.Requested = requested;
            if (requested == SandboxEditorProgressivePoissonBackend::CpuReference)
            {
                resolved.Actual = SandboxEditorProgressivePoissonBackend::CpuReference;
                return resolved;
            }

            resolved.Actual = SandboxEditorProgressivePoissonBackend::CpuReference;
            const ProgressivePoissonGpuResolveResult gpu =
                ResolveProgressivePoissonGpuRequest(
                    ProgressivePoissonGpuResolveDesc{
                        .Device = device,
                        .Plan = ProgressivePoissonGpuPlanDesc{
                            .InputCount = inputCount,
                            .Config = ToProgressivePoissonGpuConfig(config),
                        },
                    });
            if (gpu.GpuExecutionAvailable)
            {
                resolved.Actual =
                    SandboxEditorProgressivePoissonBackend::VulkanCompute;
                return resolved;
            }

            resolved.FallbackReason = gpu.Diagnostic;
            if (!resolved.FallbackReason.empty())
            {
                resolved.FallbackReason += " Ran CPU reference.";
            }
            else
            {
                resolved.FallbackReason =
                    "Vulkan compute requested but GPU execution is unavailable; ran CPU reference.";
            }
            return resolved;
        }

        struct ProgressivePoissonComputedResult
        {
            PPR::Result Method{};
            SandboxEditorProgressivePoissonResult Result{};
        };

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        BuildProgressivePoissonResultFromMethod(
            const PPR::Result& method,
            const SandboxEditorProgressivePoissonConfig& config,
            const ProgressivePoissonBackendResolution& backend)
        {
            SandboxEditorProgressivePoissonResult result{};
            result.Channel = config.Channel;
            result.InputCount = method.Diag.InputCount;
            result.AcceptedCount = method.Diag.AcceptedCount;
            result.LevelCount = static_cast<std::uint32_t>(
                method.Diag.LevelCounts.size());
            result.RequestedBackend = backend.Requested;
            result.ActualBackend = backend.Actual;
            result.RequestedBackendId =
                ProgressivePoissonBackendId(backend.Requested);
            result.RequestedBackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Requested);
            result.BackendId = ProgressivePoissonBackendId(backend.Actual);
            result.BackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Actual);
            result.FellBackToCpu =
                backend.Requested != backend.Actual &&
                backend.Actual ==
                    SandboxEditorProgressivePoissonBackend::CpuReference;
            result.BackendFallbackReason = backend.FallbackReason;
            result.LevelAcceptedCounts = method.Diag.LevelCounts;
            result.BaseRadius = method.BaseRadius;
            result.UsedAlpha = method.Diag.UsedAlpha;
            result.AlphaDefaulted = method.Diag.AlphaDefaulted;
            result.ClampedGridWidth = method.Diag.ClampedGridWidth;
            result.ClampedMaxLevels = method.Diag.ClampedMaxLevels;

            if (method.Diag.Code != PPR::ValidationCode::Valid)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error =
                    method.Diag.Code == PPR::ValidationCode::InvalidDimension
                    ? Core::ErrorCode::InvalidArgument
                    : Core::ErrorCode::InvalidState;
                result.Message =
                    "Progressive Poisson CPU reference rejected the input/config.";
                return result;
            }

            result.PrefixCount = ClampProgressivePoissonPrefix(
                config.PrefixCount,
                result.AcceptedCount);
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] ProgressivePoissonComputedResult
        ComputeProgressivePoissonCpuReference(
            const std::span<const glm::vec3> positions,
            const SandboxEditorProgressivePoissonConfig& config,
            const ProgressivePoissonBackendResolution& backend)
        {
            const PPR::Config methodConfig =
                ToProgressivePoissonReferenceConfig(config);
            ProgressivePoissonComputedResult out{};
            out.Method = PPR::Compute(positions, methodConfig);
            out.Result = BuildProgressivePoissonResultFromMethod(
                out.Method,
                config,
                backend);
            return out;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        PublishProgressivePoissonComputedResult(
            Geometry::PropertySet& properties,
            const SandboxEditorProgressivePoissonConfig& config,
            const PPR::Result& method,
            SandboxEditorProgressivePoissonResult result)
        {
            if (!result.Succeeded())
                return result;

            if (!PublishProgressivePoissonProperties(
                    properties,
                    method,
                    config,
                    result.PrefixCount))
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Progressive Poisson property publication failed.";
                return result;
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        RunProgressivePoissonAndPublish(
            const std::span<const glm::vec3> positions,
            Geometry::PropertySet& properties,
            const SandboxEditorProgressivePoissonConfig& config,
            RHI::IDevice* device)
        {
            const ProgressivePoissonBackendResolution backend =
                ResolveProgressivePoissonBackend(
                    config.Backend,
                    config,
                    static_cast<std::uint32_t>(positions.size()),
                    device);
            ProgressivePoissonComputedResult computed =
                ComputeProgressivePoissonCpuReference(positions, config, backend);
            return PublishProgressivePoissonComputedResult(
                properties,
                config,
                computed.Method,
                std::move(computed.Result));
        }

        void AppendProgressivePoissonSuccessMessage(
            SandboxEditorProgressivePoissonResult& result)
        {
            result.Message =
                "Progressive Poisson (requested " +
                (result.RequestedBackendId.empty()
                     ? result.BackendId
                     : result.RequestedBackendId) +
                ", actual " +
                result.BackendId +
                ") accepted " +
                std::to_string(result.AcceptedCount) +
                " of " +
                std::to_string(result.InputCount) +
                " points across " +
                std::to_string(result.LevelCount) +
                " levels; prefix=" +
                std::to_string(result.PrefixCount) +
                ", channel=" +
                DebugNameForSandboxEditorProgressivePoissonChannel(
                    result.Channel);
            if (!result.LevelAcceptedCounts.empty())
            {
                result.Message += ", level_counts=[";
                result.Message += FormatProgressivePoissonLevelCounts(
                    result.LevelAcceptedCounts);
                result.Message += "]";
            }
            if (result.MeshSurfaceSamplingUsed)
            {
                result.Message +=
                    ", mesh samples=" +
                    std::to_string(result.MeshSurfaceSampleCount) +
                    ", accepted triangles=" +
                    std::to_string(result.MeshSurfaceAcceptedTriangleCount) +
                    "/" +
                    std::to_string(result.MeshSurfaceTotalFaceCount);
            }
            if (!result.BackendFallbackReason.empty())
            {
                result.Message += ", fallback=\"";
                result.Message += result.BackendFallbackReason;
                result.Message += "\"";
            }
            result.Message += ".";
        }

        void ApplyProgressivePoissonVisualization(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorProgressivePoissonChannel channel)
        {
            G::RenderPoints points = raw.all_of<G::RenderPoints>(entity)
                ? raw.get<G::RenderPoints>(entity)
                : G::RenderPoints{};
            if (!std::holds_alternative<float>(points.SizeSource) &&
                !std::holds_alternative<std::string>(points.SizeSource))
            {
                points.SizeSource = 4.0f;
            }
            raw.emplace_or_replace<G::RenderPoints>(entity, points);

            G::VisualizationConfig config = raw.all_of<G::VisualizationConfig>(entity)
                ? raw.get<G::VisualizationConfig>(entity)
                : G::VisualizationConfig{};
            config.Source = G::VisualizationConfig::ColorSource::ScalarField;
            config.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
            config.ScalarFieldName = ProgressivePoissonChannelPropertyName(channel);
            config.Scalar.AutoRange = true;
            config.Scalar.BinCount = 0u;
            config.Scalar.Isolines.Num = 0u;
            raw.emplace_or_replace<G::VisualizationConfig>(entity, config);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyPointCloudPointState(
            ECS::Scene::Registry* scene,
            std::uint32_t stableEntityId,
            const Geometry::PointCloud::Cloud& cloud);

        enum class SandboxEditorProgressivePoissonCpuJobSource : std::uint8_t
        {
            PointCloud,
            MeshSurface,
        };

        [[nodiscard]] ProgressiveGeometryDomain
        ToProgressivePoissonDerivedJobDomain(
            const SandboxEditorProgressivePoissonCpuJobSource source) noexcept
        {
            return source == SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                ? ProgressiveGeometryDomain::MeshSurface
                : ProgressiveGeometryDomain::Point;
        }

        [[nodiscard]] const char* ProgressivePoissonOutputName(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            return ProgressivePoissonChannelPropertyName(config.Channel);
        }

        [[nodiscard]] Core::ErrorCode ProgressivePoissonResultError(
            const SandboxEditorProgressivePoissonResult& result) noexcept
        {
            return result.Error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : result.Error;
        }

        void SetProgressivePoissonMeshSurfaceStats(
            SandboxEditorProgressivePoissonResult& result,
            const SurfaceSampling::Diagnostics& info)
        {
            result.MeshSurfaceSamplingUsed = true;
            result.MeshSurfaceSampleCount =
                SaturatingUint32(info.WrittenSampleCount);
            result.MeshSurfaceTotalFaceCount =
                SaturatingUint32(info.TotalFaceCount);
            result.MeshSurfaceAcceptedTriangleCount =
                SaturatingUint32(info.AcceptedTriangleCount);
            result.MeshSurfaceRejectedFaceCount = SaturatingUint32(
                info.RejectedNonTriangleFaceCount +
                info.RejectedDegenerateTriangleCount +
                info.RejectedNonFiniteTriangleCount);
            result.MeshSurfaceArea = info.TotalSurfaceArea;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        MakeProgressivePoissonMeshSurfaceSamplingResult(
            const SandboxEditorProgressivePoissonConfig& config,
            const ProgressivePoissonBackendResolution& backend,
            const SurfaceSampling::Result& sampled)
        {
            SandboxEditorProgressivePoissonResult result{};
            result.Channel = config.Channel;
            result.RequestedBackend = backend.Requested;
            result.ActualBackend = backend.Actual;
            result.RequestedBackendId =
                ProgressivePoissonBackendId(backend.Requested);
            result.RequestedBackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Requested);
            result.BackendId = ProgressivePoissonBackendId(backend.Actual);
            result.BackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Actual);
            result.FellBackToCpu =
                backend.Requested != backend.Actual &&
                backend.Actual ==
                    SandboxEditorProgressivePoissonBackend::CpuReference;
            result.BackendFallbackReason = backend.FallbackReason;
            SetProgressivePoissonMeshSurfaceStats(result, sampled.Info);
            if (sampled.Succeeded())
                return result;

            result.Status =
                sampled.Status == SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount
                    ? SandboxEditorCommandStatus::InvalidProcessingParameters
                    : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error =
                sampled.Status == SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount
                    ? Core::ErrorCode::InvalidArgument
                    : Core::ErrorCode::InvalidState;
            result.Message =
                "Progressive Poisson mesh surface sampling failed with ";
            result.Message += std::string(SurfaceSampling::ToString(sampled.Status));
            result.Message += ".";
            return result;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        MakePendingProgressivePoissonCpuJobResult(
            const SandboxEditorProgressivePoissonCommand& command,
            const DerivedJobHandle handle,
            const std::uint32_t inputCount,
            const ProgressivePoissonBackendResolution& backend,
            const SandboxEditorProgressivePoissonCpuJobSource source)
        {
            SandboxEditorProgressivePoissonResult result{};
            result.Status = SandboxEditorCommandStatus::Pending;
            result.Channel = command.Config.Channel;
            result.InputCount = inputCount;
            result.RequestedBackend = backend.Requested;
            result.ActualBackend = backend.Actual;
            result.RequestedBackendId =
                ProgressivePoissonBackendId(backend.Requested);
            result.RequestedBackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Requested);
            result.BackendId = ProgressivePoissonBackendId(backend.Actual);
            result.BackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Actual);
            result.FellBackToCpu =
                backend.Requested != backend.Actual &&
                backend.Actual ==
                    SandboxEditorProgressivePoissonBackend::CpuReference;
            result.BackendFallbackReason = backend.FallbackReason;
            result.Error = Core::ErrorCode::Success;
            if (source == SandboxEditorProgressivePoissonCpuJobSource::MeshSurface)
            {
                result.MeshSurfaceSamplingUsed = true;
                result.MeshSurfaceSampleCount =
                    command.Config.MeshSurfaceSampleCount;
            }
            result.Message = source == SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                ? "Progressive Poisson mesh CPU job queued"
                : "Progressive Poisson CPU job queued";
            if (handle.IsValid())
            {
                result.Message += " (job ";
                result.Message += std::to_string(handle.Index);
                result.Message += ":";
                result.Message += std::to_string(handle.Generation);
                result.Message += ")";
            }
            result.Message += ".";
            return result;
        }

        void PublishProgressivePoissonResultSink(
            const SandboxEditorContext& context,
            SandboxEditorProgressivePoissonResult result)
        {
            if (context.MethodResultSinks.ProgressivePoisson)
                context.MethodResultSinks.ProgressivePoisson(std::move(result));
        }

        struct SandboxEditorProgressivePoissonCpuJobState
        {
            SandboxEditorProgressivePoissonCommand Command{};
            SandboxEditorProgressivePoissonCpuJobSource Source{
                SandboxEditorProgressivePoissonCpuJobSource::PointCloud};
            ProgressivePoissonBackendResolution Backend{};
            std::vector<glm::vec3> SnapshotPositions{};
            std::uint64_t GeometryMetadataSignature{0u};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::optional<PPR::Result> Method{};
            std::optional<SurfaceSampling::Result> Sampled{};
            SandboxEditorProgressivePoissonResult Result{};
        };

        [[nodiscard]] DerivedJobApplyValidation
        ValidateProgressivePoissonPointCloudApply(
            const SandboxEditorContext& context,
            const SandboxEditorProgressivePoissonCommand& command,
            const std::vector<glm::vec3>& positions)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
                return DerivedJobApplyValidation::MissingEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
                view.VertexSource == nullptr)
            {
                return DerivedJobApplyValidation::StaleGeometryGeneration;
            }

            std::optional<std::vector<glm::vec3>> current =
                CollectKMeansPositions(view.VertexSource->Properties);
            if (!current.has_value() ||
                !SameKMeansInputPositions(*current, positions))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            return DerivedJobApplyValidation::Current;
        }

        [[nodiscard]] DerivedJobApplyValidation
        ValidateProgressivePoissonMeshSurfaceApply(
            const SandboxEditorContext& context,
            const SandboxEditorProgressivePoissonCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return DerivedJobApplyValidation::MissingEntity;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.Command.StableEntityId);
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
                CollectKMeansPositions(view.VertexSource->Properties);
            if (!current.has_value() ||
                !SameKMeansInputPositions(*current, job.SnapshotPositions))
            {
                return DerivedJobApplyValidation::StaleSourcePropertyGeneration;
            }

            return DerivedJobApplyValidation::Current;
        }

        [[nodiscard]] Core::Result PublishProgressivePoissonPointCloudCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorProgressivePoissonCpuJobState& job)
        {
            if (!job.Method.has_value())
                return Core::Err(Core::ErrorCode::Unknown);
            if (context.Scene == nullptr)
                return Core::Err(Core::ErrorCode::InvalidState);

            if (!job.Result.Succeeded())
            {
                PublishProgressivePoissonResultSink(context, job.Result);
                return Core::Err(ProgressivePoissonResultError(job.Result));
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.Command.StableEntityId);
            if (!entity.has_value())
                return Core::Err(Core::ErrorCode::ResourceNotFound);

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            if (view.VertexSource == nullptr)
                return Core::Err(Core::ErrorCode::InvalidArgument);

            SandboxEditorProgressivePoissonResult result =
                PublishProgressivePoissonComputedResult(
                    view.VertexSource->Properties,
                    job.Command.Config,
                    *job.Method,
                    job.Result);
            if (result.Succeeded())
            {
                ApplyProgressivePoissonVisualization(
                    raw,
                    *entity,
                    job.Command.Config.Channel);
                Dirty::MarkVertexAttributesDirty(raw, *entity);
                if (context.CommandHistory != nullptr)
                    (void)context.CommandHistory->MarkDirty(
                        "Run progressive Poisson sampling");

                AppendProgressivePoissonSuccessMessage(result);
                InvalidateSelectedModelCache(context);
            }

            PublishProgressivePoissonResultSink(context, result);
            return result.Succeeded()
                ? Core::Ok()
                : Core::Err(ProgressivePoissonResultError(result));
        }

        [[nodiscard]] Core::Result PublishProgressivePoissonMeshSurfaceCpuJob(
            const SandboxEditorContext& context,
            const SandboxEditorProgressivePoissonCpuJobState& job)
        {
            if (!job.Result.Succeeded())
            {
                PublishProgressivePoissonResultSink(context, job.Result);
                return Core::Err(ProgressivePoissonResultError(job.Result));
            }
            if (!job.Sampled.has_value())
                return Core::Err(Core::ErrorCode::Unknown);
            if (context.Scene == nullptr)
                return Core::Err(Core::ErrorCode::InvalidState);

            const EditorCommandHistoryStatus publishStatus =
                ApplyPointCloudPointState(
                    context.Scene,
                    job.Command.StableEntityId,
                    job.Sampled->Cloud);
            SandboxEditorProgressivePoissonResult result = job.Result;
            if (publishStatus != EditorCommandHistoryStatus::Applied)
            {
                result.Status = ToSandboxEditorCommandStatus(publishStatus);
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Progressive Poisson mesh sample publication failed.";
                PublishProgressivePoissonResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.Command.StableEntityId);
            if (entity.has_value())
            {
                if (raw.all_of<G::RenderSurface>(*entity))
                    raw.remove<G::RenderSurface>(*entity);
                ApplyProgressivePoissonVisualization(
                    raw,
                    *entity,
                    job.Command.Config.Channel);
            }
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Run progressive Poisson mesh sampling");

            AppendProgressivePoissonSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishProgressivePoissonResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] DerivedJobWorkerResult
        RunProgressivePoissonPointCloudCpuWorker(
            const std::shared_ptr<SandboxEditorProgressivePoissonCpuJobState>& state)
        {
            ProgressivePoissonComputedResult computed =
                ComputeProgressivePoissonCpuReference(
                    std::span<const glm::vec3>{
                        state->SnapshotPositions.data(),
                        state->SnapshotPositions.size()},
                    state->Command.Config,
                    state->Backend);
            state->Method = std::move(computed.Method);
            state->Result = std::move(computed.Result);
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = state->Result.Succeeded()
                    ? "Progressive Poisson CPU result ready"
                    : state->Result.Message,
            };
        }

        [[nodiscard]] DerivedJobWorkerResult
        RunProgressivePoissonMeshSurfaceCpuWorker(
            const std::shared_ptr<SandboxEditorProgressivePoissonCpuJobState>& state)
        {
            SurfaceSampling::Result sampled =
                SurfaceSampling::SampleTriangleMeshSurface(
                    state->Mesh,
                    ToProgressivePoissonSurfaceParams(state->Command.Config));
            state->Result = MakeProgressivePoissonMeshSurfaceSamplingResult(
                state->Command.Config,
                state->Backend,
                sampled);
            if (!sampled.Succeeded())
            {
                state->Sampled = std::move(sampled);
                return DerivedJobOutput{
                    .PayloadToken = 0u,
                    .NormalizedProgress = 1.0f,
                    .ProgressDeterminate = true,
                    .Diagnostic = state->Result.Message,
                };
            }

            const std::span<const glm::vec3> sampledPositions =
                sampled.Cloud.Positions();
            ProgressivePoissonComputedResult computed =
                ComputeProgressivePoissonCpuReference(
                    sampledPositions,
                    state->Command.Config,
                    state->Backend);
            state->Method = std::move(computed.Method);
            SandboxEditorProgressivePoissonResult result =
                std::move(computed.Result);
            SetProgressivePoissonMeshSurfaceStats(result, sampled.Info);
            result = PublishProgressivePoissonComputedResult(
                sampled.Cloud.PointProperties(),
                state->Command.Config,
                *state->Method,
                std::move(result));
            state->Result = std::move(result);
            state->Sampled = std::move(sampled);
            return DerivedJobOutput{
                .PayloadToken = 0u,
                .NormalizedProgress = 1.0f,
                .ProgressDeterminate = true,
                .Diagnostic = state->Result.Succeeded()
                    ? "Progressive Poisson mesh CPU result ready"
                    : state->Result.Message,
            };
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        SubmitProgressivePoissonCpuDerivedJob(
            const SandboxEditorContext& context,
            const SandboxEditorProgressivePoissonCommand& command,
            const SandboxEditorProgressivePoissonCpuJobSource source,
            std::vector<glm::vec3> snapshotPositions,
            Geometry::HalfedgeMesh::Mesh mesh,
            const std::uint64_t geometryMetadataSignature,
            const std::uint32_t inputCount,
            ProgressivePoissonBackendResolution backend)
        {
            auto state =
                std::make_shared<SandboxEditorProgressivePoissonCpuJobState>();
            state->Command = command;
            state->Source = source;
            state->Backend = std::move(backend);
            state->SnapshotPositions = std::move(snapshotPositions);
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->Mesh = std::move(mesh);

            DerivedJobDesc desc{
                .Key = DerivedJobKey{
                    .EntityId = command.StableEntityId,
                    .Domain = ToProgressivePoissonDerivedJobDomain(source),
                    .OutputSemantic = ProgressiveSlotSemantic::PointScalarField,
                    .SourcePropertyGeneration = geometryMetadataSignature,
                    .OutputName = ProgressivePoissonOutputName(command.Config),
                },
                .Name = source == SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                    ? "Sandbox.ProgressivePoisson.MeshCPU"
                    : "Sandbox.ProgressivePoisson.CPU",
                .RequestedJobDomain = ProgressiveJobDomain::Cpu,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = std::max<std::uint32_t>(
                    1u,
                    (inputCount + 1023u) / 1024u),
                .Scope = context.World,
                .Execute =
                    [state]() -> DerivedJobWorkerResult
                    {
                        return state->Source ==
                                   SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                            ? RunProgressivePoissonMeshSurfaceCpuWorker(state)
                            : RunProgressivePoissonPointCloudCpuWorker(state);
                    },
                .ValidateOnMainThread =
                    [context, state]()
                    {
                        return state->Source ==
                                   SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                            ? ValidateProgressivePoissonMeshSurfaceApply(
                                  context,
                                  *state)
                            : ValidateProgressivePoissonPointCloudApply(
                                  context,
                                  state->Command,
                                  state->SnapshotPositions);
                    },
                .ApplyOnMainThread =
                    [context, state](DerivedJobApplyContext&) -> Core::Result
                    {
                        return state->Source ==
                                   SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                            ? PublishProgressivePoissonMeshSurfaceCpuJob(
                                  context,
                                  *state)
                            : PublishProgressivePoissonPointCloudCpuJob(
                                  context,
                                  *state);
                    },
            };

            if (const std::optional<DerivedJobSnapshot> active =
                    FindActiveEditorDerivedJob(context, desc.Key))
            {
                SandboxEditorProgressivePoissonResult pending =
                    MakePendingProgressivePoissonCpuJobResult(
                        command,
                        active->Handle,
                        inputCount,
                        state->Backend,
                        source);
                pending.Message = BuildActiveDerivedJobMessage(
                    source == SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                        ? "Progressive Poisson mesh CPU"
                        : "Progressive Poisson CPU",
                    *active);
                return pending;
            }

            const DerivedJobHandle handle =
                context.DerivedJobCommands.Submit(std::move(desc));
            if (!handle.IsValid())
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    command.Config.Channel,
                    Core::ErrorCode::InvalidState,
                    "Progressive Poisson CPU job submission was rejected by the runtime job lane.");
            }

            return MakePendingProgressivePoissonCpuJobResult(
                command,
                handle,
                inputCount,
                state->Backend,
                source);
        }

    }

    namespace Detail
    {
        SandboxEditorKMeansResult MakeSandboxEditorKMeansCompletionResult(
            const KMeansRunCompleted& completed)
        {
            return MakeSandboxEditorKMeansResult(completed);
        }

        SandboxEditorKMeansResult PublishSandboxEditorKMeansGpuCompletion(
            const SandboxEditorContext& context,
            const RuntimeKMeansGpuJobResult& completed)
        {
            return PublishCompletedKMeansGpuJob(context, completed);
        }
    }

    std::vector<SandboxEditorGeometryProcessingDomain>
    GetAvailableSandboxEditorKMeansDomains(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        const Domain domains =
            GetSandboxEditorGeometryProcessingCapabilities(registry, entity)
                .Domains &
            GetSandboxEditorSupportedGeometryProcessingDomains(
                SandboxEditorGeometryProcessingAlgorithm::KMeans);

        std::vector<Domain> result{};
        result.reserve(3u);
        if (HasAnySandboxEditorGeometryProcessingDomain(
                domains,
                Domain::MeshVertices))
        {
            result.push_back(Domain::MeshVertices);
        }
        if (HasAnySandboxEditorGeometryProcessingDomain(
                domains,
                Domain::GraphVertices))
        {
            result.push_back(Domain::GraphVertices);
        }
        if (HasAnySandboxEditorGeometryProcessingDomain(
                domains,
                Domain::PointCloudPoints))
        {
            result.push_back(Domain::PointCloudPoints);
        }
        return result;
    }

    const char* DebugNameForSandboxEditorProgressivePoissonChannel(
        const SandboxEditorProgressivePoissonChannel channel) noexcept
    {
        switch (channel)
        {
        case SandboxEditorProgressivePoissonChannel::Level:
            return "Level";
        case SandboxEditorProgressivePoissonChannel::Phase:
            return "Phase";
        case SandboxEditorProgressivePoissonChannel::SplatRadius:
            return "Splat radius";
        case SandboxEditorProgressivePoissonChannel::PrefixVisible:
            return "Prefix visible";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorKMeansBackend(
        const SandboxEditorKMeansBackend backend) noexcept
    {
        switch (backend)
        {
        case SandboxEditorKMeansBackend::CpuReference:
            return "CPU reference";
        case SandboxEditorKMeansBackend::VulkanCompute:
            return "Vulkan compute";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorProgressivePoissonBackend(
        const SandboxEditorProgressivePoissonBackend backend) noexcept
    {
        switch (backend)
        {
        case SandboxEditorProgressivePoissonBackend::CpuReference:
            return "CPU reference";
        case SandboxEditorProgressivePoissonBackend::VulkanCompute:
            return "Vulkan compute";
        }
        return "Unknown";
    }

    SandboxEditorProgressivePoissonChannel MakeSandboxEditorProgressivePoissonChannel(
        const ProgressivePoissonPlaygroundChannel channel) noexcept
    {
        switch (channel)
        {
        case ProgressivePoissonPlaygroundChannel::Level:
            return SandboxEditorProgressivePoissonChannel::Level;
        case ProgressivePoissonPlaygroundChannel::Phase:
            return SandboxEditorProgressivePoissonChannel::Phase;
        case ProgressivePoissonPlaygroundChannel::SplatRadius:
            return SandboxEditorProgressivePoissonChannel::SplatRadius;
        case ProgressivePoissonPlaygroundChannel::PrefixVisible:
            return SandboxEditorProgressivePoissonChannel::PrefixVisible;
        }
        return SandboxEditorProgressivePoissonChannel::Level;
    }

    ProgressivePoissonPlaygroundChannel
    MakeProgressivePoissonPlaygroundChannel(
        const SandboxEditorProgressivePoissonChannel channel) noexcept
    {
        switch (channel)
        {
        case SandboxEditorProgressivePoissonChannel::Level:
            return ProgressivePoissonPlaygroundChannel::Level;
        case SandboxEditorProgressivePoissonChannel::Phase:
            return ProgressivePoissonPlaygroundChannel::Phase;
        case SandboxEditorProgressivePoissonChannel::SplatRadius:
            return ProgressivePoissonPlaygroundChannel::SplatRadius;
        case SandboxEditorProgressivePoissonChannel::PrefixVisible:
            return ProgressivePoissonPlaygroundChannel::PrefixVisible;
        }
        return ProgressivePoissonPlaygroundChannel::Level;
    }

    SandboxEditorProgressivePoissonBackend MakeSandboxEditorProgressivePoissonBackend(
        const ProgressivePoissonPlaygroundBackend backend) noexcept
    {
        switch (backend)
        {
        case ProgressivePoissonPlaygroundBackend::CpuReference:
            return SandboxEditorProgressivePoissonBackend::CpuReference;
        case ProgressivePoissonPlaygroundBackend::VulkanCompute:
            return SandboxEditorProgressivePoissonBackend::VulkanCompute;
        }
        return SandboxEditorProgressivePoissonBackend::CpuReference;
    }

    ProgressivePoissonPlaygroundBackend
    MakeProgressivePoissonPlaygroundBackend(
        const SandboxEditorProgressivePoissonBackend backend) noexcept
    {
        switch (backend)
        {
        case SandboxEditorProgressivePoissonBackend::CpuReference:
            return ProgressivePoissonPlaygroundBackend::CpuReference;
        case SandboxEditorProgressivePoissonBackend::VulkanCompute:
            return ProgressivePoissonPlaygroundBackend::VulkanCompute;
        }
        return ProgressivePoissonPlaygroundBackend::CpuReference;
    }

    SandboxEditorProgressivePoissonConfig MakeSandboxEditorProgressivePoissonConfig(
        const ProgressivePoissonPlaygroundConfig& config) noexcept
    {
        return SandboxEditorProgressivePoissonConfig{
            .Dimension = config.Dimension,
            .GridWidth = config.GridWidth,
            .MaxLevels = config.MaxLevels,
            .HashLoadFactor = static_cast<float>(config.HashLoadFactor),
            .RadiusAlpha = static_cast<float>(config.RadiusAlpha),
            .RandomizeGridOrigin = config.RandomizeGridOrigin,
            .GridOriginSeed = config.GridOriginSeed,
            .ShuffleWithinLevels = config.ShuffleWithinLevels,
            .ShuffleSeed = config.ShuffleSeed,
            .PrefixCount = config.PrefixCount,
            .Channel = MakeSandboxEditorProgressivePoissonChannel(config.Channel),
            .Backend = MakeSandboxEditorProgressivePoissonBackend(config.Backend),
            .MeshSurfaceSampleCount = config.MeshSurfaceSampleCount,
            .MeshSurfaceSampleSeed = config.MeshSurfaceSampleSeed,
            .MeshSurfaceMinTriangleArea = config.MeshSurfaceMinTriangleArea,
            .MeshSurfaceInterpolateNormals = config.MeshSurfaceInterpolateNormals,
            .AutoRunOnEdit = config.AutoRunOnEdit,
            .DebounceSeconds = config.DebounceSeconds,
        };
    }

    ProgressivePoissonPlaygroundConfig
    MakeProgressivePoissonPlaygroundConfig(
        const SandboxEditorProgressivePoissonConfig& config,
        const ProgressivePoissonPlaygroundConfig& defaults) noexcept
    {
        ProgressivePoissonPlaygroundConfig out = defaults;
        out.Dimension = config.Dimension;
        out.GridWidth = config.GridWidth;
        out.MaxLevels = config.MaxLevels;
        out.HashLoadFactor = static_cast<double>(config.HashLoadFactor);
        out.RadiusAlpha = static_cast<double>(config.RadiusAlpha);
        out.RandomizeGridOrigin = config.RandomizeGridOrigin;
        out.GridOriginSeed = config.GridOriginSeed;
        out.ShuffleWithinLevels = config.ShuffleWithinLevels;
        out.ShuffleSeed = config.ShuffleSeed;
        out.PrefixCount = config.PrefixCount;
        out.Channel = MakeProgressivePoissonPlaygroundChannel(config.Channel);
        out.Backend = MakeProgressivePoissonPlaygroundBackend(config.Backend);
        out.MeshSurfaceSampleCount = config.MeshSurfaceSampleCount;
        out.MeshSurfaceSampleSeed = config.MeshSurfaceSampleSeed;
        out.MeshSurfaceMinTriangleArea = config.MeshSurfaceMinTriangleArea;
        out.MeshSurfaceInterpolateNormals = config.MeshSurfaceInterpolateNormals;
        out.AutoRunOnEdit = config.AutoRunOnEdit;
        out.DebounceSeconds = config.DebounceSeconds;
        return out;
    }


    SandboxEditorKMeansResult ApplySandboxEditorKMeansCommand(
        const SandboxEditorContext& context,
        const SandboxEditorKMeansCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::MissingScene,
                command.Domain,
                command.Backend,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for K-Means.");
        }
        if (!IsKMeansExecutionDomain(command.Domain) ||
            command.ClusterCount == 0u ||
            command.MaxIterations == 0u)
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Domain,
                command.Backend,
                Core::ErrorCode::InvalidArgument,
                "K-Means requires mesh vertices, graph nodes, or point-cloud points with positive cluster and iteration counts.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::StaleEntity,
                command.Domain,
                command.Backend,
                Core::ErrorCode::ResourceNotFound,
                "K-Means target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        if (!view.Valid() || !SourceViewSupportsKMeansDomain(view, command.Domain))
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                command.Domain,
                command.Backend,
                Core::ErrorCode::InvalidArgument,
                "Selected entity does not expose the requested K-Means GeometrySources domain.");
        }

        Geometry::PropertySet* properties =
            KMeansTargetProperties(view, command.Domain);
        if (properties == nullptr)
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                command.Domain,
                command.Backend,
                Core::ErrorCode::InvalidArgument,
                "Requested K-Means GeometrySources domain has no writable property set.");
        }

        std::optional<std::vector<glm::vec3>> points =
            CollectKMeansPositions(*properties);
        if (!points.has_value())
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Domain,
                command.Backend,
                Core::ErrorCode::InvalidArgument,
                "K-Means requires a non-empty finite v:position property on the requested domain.");
        }

        GK::KMeansParams params{};
        params.ClusterCount = command.ClusterCount;
        params.MaxIterations = command.MaxIterations;
        params.Seed = command.Seed;
        params.Init = command.UseHierarchicalInitialization
            ? GK::Initialization::Hierarchical
            : GK::Initialization::Random;
        params.Compute = ToKMeansGeometryBackend(command.Backend);

        if (context.KMeansCommands.Required &&
            !context.KMeansCommands.Available())
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                command.Domain,
                command.Backend,
                Core::ErrorCode::InvalidState,
                "Runtime clustering module is not composed; K-Means is unavailable.");
        }

        std::string gpuQueueFallbackReason{};
        if (command.Backend == SandboxEditorKMeansBackend::VulkanCompute &&
            context.KMeansGpuCommands.Available())
        {
            RuntimeKMeansGpuJobRequest gpuRequest{
                .StableEntityId = command.StableEntityId,
                .DomainTag = static_cast<std::uint32_t>(command.Domain),
                .Points = *points,
                .Params = params,
            };
            const RuntimeKMeansGpuJobSubmission submission =
                context.KMeansGpuCommands.Submit(std::move(gpuRequest));
            if (submission.Accepted() ||
                submission.Status == RuntimeKMeansGpuJobStatus::Busy)
            {
                return MakePendingKMeansGpuResult(
                    command,
                    submission,
                    static_cast<std::uint32_t>(points->size()));
            }
            if (submission.Status != RuntimeKMeansGpuJobStatus::GpuUnavailable)
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    command.Domain,
                    command.Backend,
                    Core::ErrorCode::InvalidState,
                    submission.Diagnostic.empty()
                        ? "K-Means Vulkan compute job submission failed."
                        : submission.Diagnostic);
            }
            gpuQueueFallbackReason = submission.Diagnostic.empty()
                ? "K-Means Vulkan compute execution is unavailable; ran CPU reference."
                : submission.Diagnostic + " Ran CPU reference.";
        }

        if (context.KMeansCommands.Available())
        {
            const CommandCorrelationId correlation =
                context.KMeansCommands.Submit(RunKMeans{
                    .StableEntityId = command.StableEntityId,
                    .Domain = ToRuntimeClusteringDomain(command.Domain),
                    .ClusterCount = command.ClusterCount,
                    .MaxIterations = command.MaxIterations,
                    .Seed = command.Seed,
                    .UseHierarchicalInitialization =
                        command.UseHierarchicalInitialization,
                    .Backend = ToRuntimeClusteringBackend(command.Backend),
                });
            if (!correlation.IsValid())
            {
                return MakeKMeansResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed,
                    command.Domain,
                    command.Backend,
                    Core::ErrorCode::InvalidState,
                    "Runtime clustering command submission was rejected.");
            }

            SandboxEditorKMeansResult pending =
                MakePendingRuntimeKMeansResult(
                    command,
                    correlation,
                    static_cast<std::uint32_t>(points->size()));
            pending.BackendFallbackReason = std::move(gpuQueueFallbackReason);
            return pending;
        }

        if (context.DerivedJobCommands.Available())
        {
            return SubmitKMeansCpuDerivedJob(
                context,
                command,
                std::move(*points),
                params,
                std::move(gpuQueueFallbackReason));
        }

        const std::optional<GK::KMeansResult> clustered =
            RunKMeansForSandbox(
                std::span<const glm::vec3>{points->data(), points->size()},
                params,
                context.Device);
        if (!clustered.has_value())
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                command.Domain,
                command.Backend,
                Core::ErrorCode::Unknown,
                "Geometry.KMeans returned no result for the requested points.");
        }

        if (!PublishKMeansProperties(*properties, command.Domain, *clustered))
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                command.Domain,
                command.Backend,
                Core::ErrorCode::TypeMismatch,
                "K-Means result publication failed because output properties have incompatible types or sizes.");
        }

        Dirty::MarkVertexAttributesDirty(raw, *entity);
        SandboxEditorKMeansResult result =
            MakeCompletedKMeansResult(command.Domain, *clustered);
        result.BackendFallbackReason = !gpuQueueFallbackReason.empty() &&
                                               result.FellBackToCpu
            ? std::move(gpuQueueFallbackReason)
            : BuildKMeansFallbackReason(result, context.Device);
        result.Message = BuildKMeansSuccessMessage(command.Domain, result);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty("Run K-Means");
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorProgressivePoissonResult
    ApplySandboxEditorProgressivePoissonCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressivePoissonCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::MissingScene,
                command.Config.Channel,
                Core::ErrorCode::InvalidState,
                "Progressive Poisson sampling requires an attached scene.");
        }
        if (!IsValidProgressivePoissonConfig(command.Config))
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires dimension 2 or 3, positive grid/max-level/hash settings, and finite radius alpha.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::StaleEntity,
                command.Config.Channel,
                Core::ErrorCode::ResourceNotFound,
                "Progressive Poisson target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud &&
            availability.ProvenanceDomain != GS::Domain::Mesh)
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires selected point-cloud or mesh GeometrySources.");
        }

        if (availability.ProvenanceDomain == GS::Domain::PointCloud)
        {
            if (view.VertexSource == nullptr)
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                    command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires selected point-cloud vertices.");
            }

            std::optional<std::vector<glm::vec3>> positions =
                CollectKMeansPositions(view.VertexSource->Properties);
            if (!positions.has_value())
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::InvalidProcessingParameters,
                    command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires a non-empty finite v:position property.");
            }

            if (context.DerivedJobCommands.Available())
            {
                const std::uint32_t pointCount =
                    static_cast<std::uint32_t>(positions->size());
                const ProgressivePoissonBackendResolution backend =
                    ResolveProgressivePoissonBackend(
                        command.Config.Backend,
                        command.Config,
                        pointCount,
                        context.Device);
                return SubmitProgressivePoissonCpuDerivedJob(
                    context,
                    command,
                    SandboxEditorProgressivePoissonCpuJobSource::PointCloud,
                    std::move(*positions),
                    Geometry::HalfedgeMesh::Mesh{},
                    GeometryMetadataSignatureForEntity(raw, *entity),
                    pointCount,
                    backend);
            }

            SandboxEditorProgressivePoissonResult result =
                RunProgressivePoissonAndPublish(
                    std::span<const glm::vec3>{
                        positions->data(),
                        positions->size()},
                    view.VertexSource->Properties,
                    command.Config,
                    context.Device);
            if (!result.Succeeded())
                return result;

            ApplyProgressivePoissonVisualization(
                raw,
                *entity,
                command.Config.Channel);
            Dirty::MarkVertexAttributesDirty(raw, *entity);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Run progressive Poisson sampling");

            AppendProgressivePoissonSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            return result;
        }

        if (!IsValidProgressivePoissonMeshSurfaceConfig(command.Config))
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson mesh sampling requires a positive surface sample count and finite positive minimum triangle area.");
        }

        const GS::ConstSourceView constView =
            GS::BuildConstView(raw, *entity);
        Detail::SandboxEditorMeshSourceSnapshot source =
            Detail::BuildSandboxEditorMeshSourceSnapshot(constView);
        if (source.Status != SandboxEditorCommandStatus::Applied)
        {
            return MakeProgressivePoissonResult(
                source.Status,
                command.Config.Channel,
                source.Error,
                source.Diagnostic.empty()
                    ? "Progressive Poisson mesh sampling could not build selected mesh GeometrySources."
                    : source.Diagnostic);
        }

        if (context.DerivedJobCommands.Available())
        {
            const ProgressivePoissonBackendResolution backend =
                ResolveProgressivePoissonBackend(
                    command.Config.Backend,
                    command.Config,
                    command.Config.MeshSurfaceSampleCount,
                    context.Device);
            return SubmitProgressivePoissonCpuDerivedJob(
                context,
                command,
                SandboxEditorProgressivePoissonCpuJobSource::MeshSurface,
                std::move(source.BeforePositions),
                std::move(source.Mesh),
                GeometryMetadataSignatureForEntity(raw, *entity),
                command.Config.MeshSurfaceSampleCount,
                backend);
        }

        SurfaceSampling::Result sampled =
            SurfaceSampling::SampleTriangleMeshSurface(
                source.Mesh,
                ToProgressivePoissonSurfaceParams(command.Config));
        SandboxEditorProgressivePoissonResult result{};
        result.Channel = command.Config.Channel;
        result.MeshSurfaceSamplingUsed = true;
        result.MeshSurfaceSampleCount =
            SaturatingUint32(sampled.Info.WrittenSampleCount);
        result.MeshSurfaceTotalFaceCount =
            SaturatingUint32(sampled.Info.TotalFaceCount);
        result.MeshSurfaceAcceptedTriangleCount =
            SaturatingUint32(sampled.Info.AcceptedTriangleCount);
        result.MeshSurfaceRejectedFaceCount = SaturatingUint32(
            sampled.Info.RejectedNonTriangleFaceCount +
            sampled.Info.RejectedDegenerateTriangleCount +
            sampled.Info.RejectedNonFiniteTriangleCount);
        result.MeshSurfaceArea = sampled.Info.TotalSurfaceArea;
        if (!sampled.Succeeded())
        {
            result.Status =
                sampled.Status == SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount
                    ? SandboxEditorCommandStatus::InvalidProcessingParameters
                    : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error =
                sampled.Status == SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount
                    ? Core::ErrorCode::InvalidArgument
                    : Core::ErrorCode::InvalidState;
            result.Message =
                "Progressive Poisson mesh surface sampling failed with ";
            result.Message += std::string(SurfaceSampling::ToString(sampled.Status));
            result.Message += ".";
            return result;
        }

        const std::span<const glm::vec3> sampledPositions =
            sampled.Cloud.Positions();
        result = RunProgressivePoissonAndPublish(
            sampledPositions,
            sampled.Cloud.PointProperties(),
            command.Config,
            context.Device);
        result.MeshSurfaceSamplingUsed = true;
        result.MeshSurfaceSampleCount =
            SaturatingUint32(sampled.Info.WrittenSampleCount);
        result.MeshSurfaceTotalFaceCount =
            SaturatingUint32(sampled.Info.TotalFaceCount);
        result.MeshSurfaceAcceptedTriangleCount =
            SaturatingUint32(sampled.Info.AcceptedTriangleCount);
        result.MeshSurfaceRejectedFaceCount = SaturatingUint32(
            sampled.Info.RejectedNonTriangleFaceCount +
            sampled.Info.RejectedDegenerateTriangleCount +
            sampled.Info.RejectedNonFiniteTriangleCount);
        result.MeshSurfaceArea = sampled.Info.TotalSurfaceArea;
        if (!result.Succeeded())
            return result;

        const EditorCommandHistoryStatus publishStatus =
            ApplyPointCloudPointState(
                context.Scene,
                command.StableEntityId,
                sampled.Cloud);
        if (publishStatus != EditorCommandHistoryStatus::Applied)
        {
            result.Status = ToSandboxEditorCommandStatus(publishStatus);
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Progressive Poisson mesh sample publication failed.";
            return result;
        }

        if (raw.all_of<G::RenderSurface>(*entity))
            raw.remove<G::RenderSurface>(*entity);
        ApplyProgressivePoissonVisualization(
            raw,
            *entity,
            command.Config.Channel);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Run progressive Poisson mesh sampling");

        AppendProgressivePoissonSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    SandboxEditorProgressivePoissonConfigResult
    ApplySandboxEditorProgressivePoissonConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressivePoissonConfigCommand& command)
    {
        SandboxEditorProgressivePoissonConfigResult result{};
        if (context.EngineConfigControlState == nullptr ||
            !context.PreviewEngineConfigDocument ||
            !context.ApplyEngineConfigHotSubset ||
            !context.EngineConfigCommandsAvailable)
        {
            result.Status =
                SandboxEditorProgressivePoissonConfigStatus::MissingConfigFacade;
            result.Message =
                "Progressive Poisson config requires the engine config-control facade.";
            return result;
        }

        Core::Config::EngineConfig candidate =
            context.EngineConfigControlState->ActiveConfig;
        const ProgressivePoissonPlaygroundConfig current =
            GetProgressivePoissonPlaygroundConfig(candidate).value_or(
                ProgressivePoissonPlaygroundConfig{});
        SetProgressivePoissonPlaygroundConfig(
            candidate,
            MakeProgressivePoissonPlaygroundConfig(command.Config, current));
        const std::string document =
            Core::Config::SerializeEngineConfig(candidate);
        const std::string sourceId = command.SourceId.empty()
            ? std::string{"sandbox.progressive_poisson"}
            : command.SourceId;
        result.Preview =
            context.PreviewEngineConfigDocument(document, sourceId);
        if (!Core::Config::IsConfigUsable(result.Preview))
        {
            result.Status =
                SandboxEditorProgressivePoissonConfigStatus::PreviewRejected;
            result.Message =
                "Progressive Poisson config preview was rejected.";
            return result;
        }

        result.Apply = context.ApplyEngineConfigHotSubset(result.Preview);
        if (!result.Apply.Succeeded())
        {
            result.Status =
                SandboxEditorProgressivePoissonConfigStatus::ApplyRejected;
            result.Message =
                "Progressive Poisson config hot-apply was rejected.";
            return result;
        }

        result.Status =
            result.Apply.Status == RuntimeEngineConfigApplyStatus::NoChange
                ? SandboxEditorProgressivePoissonConfigStatus::NoChange
                : SandboxEditorProgressivePoissonConfigStatus::Applied;
        result.Message =
            result.Status == SandboxEditorProgressivePoissonConfigStatus::NoChange
                ? "Progressive Poisson config unchanged."
                : "Progressive Poisson config applied.";
        return result;
    }

    std::optional<SandboxEditorProgressivePoissonConfig>
    GetSandboxEditorProgressivePoissonConfig(
        const SandboxEditorContext& context) noexcept
    {
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        const auto config = GetProgressivePoissonPlaygroundConfig(
            context.EngineConfigControlState->ActiveConfig);
        if (!config.has_value())
            return std::nullopt;
        return MakeSandboxEditorProgressivePoissonConfig(*config);
    }


    void SandboxEditorSession::AttachKMeansGpuQueue(Engine& engine)
    {
        DetachKMeansGpuQueue();
        m_KMeansGpuJobs = std::make_unique<RuntimeKMeansGpuJobQueue>(
            engine.GetDevice(),
            engine.GetRenderer().GetBufferManager(),
            engine.GetDevice().GetTransferQueue());
        m_KMeansGpuParticipant = engine.Jobs().RegisterGpuQueueParticipant(
            GpuQueueParticipantDesc{
                .DebugName = "SandboxEditor.KMeansGpu",
                .RecordFrameCommands =
                    [epoch = m_AttachmentEpoch,
                     this](RHI::ICommandContext& commandContext)
                    {
                        if (epoch != nullptr &&
                            epoch->load(std::memory_order_acquire) &&
                            m_KMeansGpuJobs)
                            m_KMeansGpuJobs->AdvanceGpuWork(commandContext);
                    },
                .DrainCompletedTransfers =
                    [epoch = m_AttachmentEpoch, this]()
                    {
                        if (epoch != nullptr &&
                            epoch->load(std::memory_order_acquire) &&
                            m_KMeansGpuJobs)
                            m_KMeansGpuJobs->DrainCompletedTransfers();
                    },
                .HasInFlightWork =
                    [this]() -> bool
                    {
                        return m_KMeansGpuJobs &&
                               m_KMeansGpuJobs->HasInFlightJob();
                    },
                .ShutdownAfterDeviceIdle =
                    [this]()
                    {
                        m_KMeansGpuJobs.reset();
                        m_KMeansGpuParticipant = {};
                    },
            });
        if (!m_KMeansGpuParticipant.IsValid())
            m_KMeansGpuJobs.reset();
    }

    void SandboxEditorSession::DetachKMeansGpuQueue()
    {
        if (m_Engine != nullptr && m_KMeansGpuParticipant.IsValid())
        {
            m_Engine->Jobs().UnregisterGpuQueueParticipant(
                m_KMeansGpuParticipant,
                [engine = m_Engine]
                {
                    engine->GetDevice().WaitIdle();
                });
        }
        m_KMeansGpuParticipant = {};
        m_KMeansGpuJobs.reset();
    }

}
