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

module Extrinsic.Runtime.Private.EditorFeatures;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Hierarchy;
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
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
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
#include "internal/Runtime.EditorFeatures.Detail.hpp"

namespace Extrinsic::Runtime::EditorFeatureDetail
{
    namespace
    {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;
        namespace SurfaceSampling = Geometry::PointCloud::SurfaceSampling;
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

        // RUNTIME-194 Slice B5d: the result payload for this file's method
        // jobs. The computed result itself already reaches the main thread in
        // the shared job state the worker fills, so the envelope carries only
        // the diagnostic the retired `DerivedJobOutput` exposed — and exists at
        // all because an empty envelope is how `JobService` reports a dropped
        // job.
        struct EditorGeometryJobResult
        {
            std::string Diagnostic{};
        };

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
            const entt::registry& raw,
            const std::uint32_t stableId)
        {
            return Detail::ResolveEditorStableEntity(raw, stableId);
        }

        void InvalidateSelectedModelCache(const EditorFeatureBindings& context)
        {
            Detail::InvalidateEditorSelectedModelCache(context);
        }

        [[nodiscard]] std::optional<EditorJobRecord>
        FindActiveEditorJob(
            const EditorFeatureBindings& context,
            const EditorJobIdentity& identity)
        {
            return Detail::FindActiveEditorGeometryJob(context, identity);
        }

        [[nodiscard]] std::string BuildActiveDerivedJobMessage(
            const std::string_view label,
            const EditorJobRecord& job)
        {
            return Detail::BuildActiveEditorGeometryJobMessage(
                label,
                job);
        }

        [[nodiscard]] EditorCommandStatus ToEditorCommandStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            return Detail::ToEditorMethodCommandStatus(status);
        }

        [[nodiscard]] bool IsFinitePosition(const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>> CollectFiniteVertexPositions(
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
            const EditorProgressivePoissonBackend backend) noexcept
        {
            switch (backend)
            {
            case EditorProgressivePoissonBackend::CpuReference:
                return PPR::kBackendId;
            case EditorProgressivePoissonBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendId;
            }
            return PPR::kBackendId;
        }

        [[nodiscard]] const char* ProgressivePoissonBackendDisplayName(
            const EditorProgressivePoissonBackend backend) noexcept
        {
            switch (backend)
            {
            case EditorProgressivePoissonBackend::CpuReference:
                return kProgressivePoissonCpuBackendDisplayName;
            case EditorProgressivePoissonBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendDisplayName;
            }
            return kProgressivePoissonCpuBackendDisplayName;
        }

        [[nodiscard]] const char* ProgressivePoissonChannelPropertyName(
            const EditorProgressivePoissonChannel channel) noexcept
        {
            switch (channel)
            {
            case EditorProgressivePoissonChannel::Level:
                return kProgressivePoissonLevelProperty;
            case EditorProgressivePoissonChannel::Phase:
                return kProgressivePoissonPhaseProperty;
            case EditorProgressivePoissonChannel::SplatRadius:
                return kProgressivePoissonSplatRadiusProperty;
            case EditorProgressivePoissonChannel::PrefixVisible:
                return kProgressivePoissonPrefixVisibleProperty;
            }
            return kProgressivePoissonLevelProperty;
        }

        [[nodiscard]] EditorProgressivePoissonResult
        MakeProgressivePoissonResult(
            const EditorCommandStatus status,
            const EditorProgressivePoissonChannel channel,
            const Core::ErrorCode error,
            std::string message)
        {
            return EditorProgressivePoissonResult{
                .Status = status,
                .Channel = channel,
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool IsValidProgressivePoissonConfig(
            const EditorProgressivePoissonConfig& config) noexcept
        {
            return (config.Dimension == 2u || config.Dimension == 3u) &&
                   config.GridWidth > 0u &&
                   config.MaxLevels > 0u &&
                   std::isfinite(config.HashLoadFactor) &&
                   config.HashLoadFactor > 0.0f &&
                   std::isfinite(config.RadiusAlpha);
        }

        [[nodiscard]] bool IsValidProgressivePoissonMeshSurfaceConfig(
            const EditorProgressivePoissonConfig& config) noexcept
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
            const EditorProgressivePoissonConfig& config) noexcept
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
            const EditorProgressivePoissonConfig& config) noexcept
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
            const EditorProgressivePoissonConfig& config,
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
            EditorProgressivePoissonBackend Requested{
                EditorProgressivePoissonBackend::CpuReference};
            EditorProgressivePoissonBackend Actual{
                EditorProgressivePoissonBackend::CpuReference};
            std::string FallbackReason{};
        };

        [[nodiscard]] ProgressivePoissonGpuConfig ToProgressivePoissonGpuConfig(
            const EditorProgressivePoissonConfig& config) noexcept
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
            const EditorProgressivePoissonBackend requested,
            const EditorProgressivePoissonConfig& config,
            const std::uint32_t inputCount,
            RHI::IDevice* device)
        {
            ProgressivePoissonBackendResolution resolved{};
            resolved.Requested = requested;
            if (requested == EditorProgressivePoissonBackend::CpuReference)
            {
                resolved.Actual = EditorProgressivePoissonBackend::CpuReference;
                return resolved;
            }

            resolved.Actual = EditorProgressivePoissonBackend::CpuReference;
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
                    EditorProgressivePoissonBackend::VulkanCompute;
                return resolved;
            }

            resolved.FallbackReason = gpu.Diagnostic;
            if (!resolved.FallbackReason.empty())
            {
                resolved.FallbackReason += " Ran CPU reference.";
            }
            else
            {
                resolved.FallbackReason = "Vulkan compute requested but GPU execution is "
                                          "unavailable; ran CPU reference.";
            }
            return resolved;
        }

        struct ProgressivePoissonComputedResult
        {
            PPR::Result Method{};
            EditorProgressivePoissonResult Result{};
        };

        [[nodiscard]] EditorProgressivePoissonResult
        BuildProgressivePoissonResultFromMethod(
            const PPR::Result& method,
            const EditorProgressivePoissonConfig& config,
            const ProgressivePoissonBackendResolution& backend)
        {
            EditorProgressivePoissonResult result{};
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
                    EditorProgressivePoissonBackend::CpuReference;
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
                    EditorCommandStatus::GeometryProcessingFailed;
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
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] ProgressivePoissonComputedResult
        ComputeProgressivePoissonCpuReference(
            const std::span<const glm::vec3> positions,
            const EditorProgressivePoissonConfig& config,
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

        [[nodiscard]] EditorProgressivePoissonResult
        PublishProgressivePoissonComputedResult(
            Geometry::PropertySet& properties,
            const EditorProgressivePoissonConfig& config,
            const PPR::Result& method,
            EditorProgressivePoissonResult result)
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
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Progressive Poisson property publication failed.";
                return result;
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] EditorProgressivePoissonResult
        RunProgressivePoissonAndPublish(
            const std::span<const glm::vec3> positions,
            Geometry::PropertySet& properties,
            const EditorProgressivePoissonConfig& config,
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
            EditorProgressivePoissonResult& result)
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
                DebugNameForEditorProgressivePoissonChannelImpl(
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

        struct ProgressivePoissonEntityState
        {
            std::optional<GS::Vertices> Vertices{};
            std::optional<GS::Edges> Edges{};
            std::optional<GS::Halfedges> Halfedges{};
            std::optional<GS::Faces> Faces{};
            std::optional<GS::Nodes> Nodes{};
            bool HasMeshTopology{false};
            bool HasGraphTopology{false};
            std::optional<G::RenderSurface> RenderSurface{};
            std::optional<G::RenderPoints> RenderPoints{};
            std::optional<G::VisualizationConfig> Visualization{};
        };

        using ProgressivePoissonEntitySnapshot =
            std::shared_ptr<const ProgressivePoissonEntityState>;

        template <typename T>
        [[nodiscard]] std::optional<T> CaptureOptionalComponent(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            if (const T* component = raw.try_get<T>(entity))
                return *component;
            return std::nullopt;
        }

        [[nodiscard]] ProgressivePoissonEntityState
        CaptureProgressivePoissonEntityState(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            return ProgressivePoissonEntityState{
                .Vertices = CaptureOptionalComponent<GS::Vertices>(
                    raw,
                    entity),
                .Edges = CaptureOptionalComponent<GS::Edges>(raw, entity),
                .Halfedges = CaptureOptionalComponent<GS::Halfedges>(
                    raw,
                    entity),
                .Faces = CaptureOptionalComponent<GS::Faces>(raw, entity),
                .Nodes = CaptureOptionalComponent<GS::Nodes>(raw, entity),
                .HasMeshTopology =
                    raw.all_of<GS::HasMeshTopology>(entity),
                .HasGraphTopology =
                    raw.all_of<GS::HasGraphTopology>(entity),
                .RenderSurface =
                    CaptureOptionalComponent<G::RenderSurface>(raw, entity),
                .RenderPoints =
                    CaptureOptionalComponent<G::RenderPoints>(raw, entity),
                .Visualization =
                    CaptureOptionalComponent<G::VisualizationConfig>(
                        raw,
                        entity),
            };
        }

        template <typename T>
        [[nodiscard]] bool SameProgressivePoissonValue(
            const T& lhs,
            const T& rhs) noexcept
        {
            return lhs == rhs;
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<float>(
            const float& lhs,
            const float& rhs) noexcept
        {
            return std::bit_cast<std::uint32_t>(lhs) ==
                   std::bit_cast<std::uint32_t>(rhs);
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<double>(
            const double& lhs,
            const double& rhs) noexcept
        {
            return std::bit_cast<std::uint64_t>(lhs) ==
                   std::bit_cast<std::uint64_t>(rhs);
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<glm::vec2>(
            const glm::vec2& lhs,
            const glm::vec2& rhs) noexcept
        {
            return SameProgressivePoissonValue(lhs.x, rhs.x) &&
                   SameProgressivePoissonValue(lhs.y, rhs.y);
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<glm::vec3>(
            const glm::vec3& lhs,
            const glm::vec3& rhs) noexcept
        {
            return SameProgressivePoissonValue(lhs.x, rhs.x) &&
                   SameProgressivePoissonValue(lhs.y, rhs.y) &&
                   SameProgressivePoissonValue(lhs.z, rhs.z);
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<glm::vec4>(
            const glm::vec4& lhs,
            const glm::vec4& rhs) noexcept
        {
            return SameProgressivePoissonValue(lhs.x, rhs.x) &&
                   SameProgressivePoissonValue(lhs.y, rhs.y) &&
                   SameProgressivePoissonValue(lhs.z, rhs.z) &&
                   SameProgressivePoissonValue(lhs.w, rhs.w);
        }

        template <typename T>
        [[nodiscard]] bool SameProgressivePoissonProperty(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected,
            const std::string_view name) noexcept
        {
            const auto currentProperty = current.Get<T>(name);
            const auto expectedProperty = expected.Get<T>(name);
            if (!currentProperty || !expectedProperty ||
                currentProperty.Vector().size() !=
                    expectedProperty.Vector().size())
            {
                return false;
            }
            for (std::size_t i = 0u;
                 i < expectedProperty.Vector().size();
                 ++i)
            {
                if (!SameProgressivePoissonValue(
                        currentProperty.Vector()[i],
                        expectedProperty.Vector()[i]))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SameProgressivePoissonPropertySet(
            const Geometry::PropertySet& current,
            const Geometry::PropertySet& expected) noexcept
        {
            const std::vector<Geometry::PropertyDescriptor> currentDescriptors =
                current.Descriptors();
            const std::vector<Geometry::PropertyDescriptor> expectedDescriptors =
                expected.Descriptors();
            if (current.Size() != expected.Size() ||
                currentDescriptors.size() != expectedDescriptors.size())
            {
                return false;
            }

            const Geometry::ConstPropertySet currentView{current};
            const Geometry::ConstPropertySet expectedView{expected};
            for (std::size_t i = 0u;
                 i < expectedDescriptors.size();
                 ++i)
            {
                const Geometry::PropertyDescriptor& lhs =
                    currentDescriptors[i];
                const Geometry::PropertyDescriptor& rhs =
                    expectedDescriptors[i];
                if (lhs.Name != rhs.Name ||
                    lhs.Type != rhs.Type ||
                    lhs.ValueKind != rhs.ValueKind ||
                    lhs.ElementCount != rhs.ElementCount)
                {
                    return false;
                }

                bool same = true;
                switch (rhs.ValueKind)
                {
                case Geometry::PropertyValueKind::Bool:
                    same = SameProgressivePoissonProperty<bool>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::Int32:
                    same = SameProgressivePoissonProperty<std::int32_t>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::UInt32:
                    same = SameProgressivePoissonProperty<std::uint32_t>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::UInt64:
                    same = SameProgressivePoissonProperty<std::uint64_t>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::Float:
                    same = SameProgressivePoissonProperty<float>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::Double:
                    same = SameProgressivePoissonProperty<double>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::Vec2:
                    same = SameProgressivePoissonProperty<glm::vec2>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::Vec3:
                    same = SameProgressivePoissonProperty<glm::vec3>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::Vec4:
                    same = SameProgressivePoissonProperty<glm::vec4>(
                        currentView,
                        expectedView,
                        rhs.Name);
                    break;
                case Geometry::PropertyValueKind::Unknown:
                    // Geometry containers carry private connectivity
                    // properties whose erased values are intentionally not
                    // exposed. Their descriptor metadata is checked here;
                    // promoted topology/value properties are checked above.
                    break;
                }
                if (!same)
                    return false;
            }
            return true;
        }

        template <typename T>
        [[nodiscard]] bool SameOptionalGeometrySource(
            const std::optional<T>& current,
            const std::optional<T>& expected) noexcept
        {
            if (current.has_value() != expected.has_value())
                return false;
            if (!current.has_value())
                return true;
            return current->NumDeleted == expected->NumDeleted &&
                   SameProgressivePoissonPropertySet(
                       current->Properties,
                       expected->Properties);
        }

        [[nodiscard]] bool SameOptionalHalfedgeSource(
            const std::optional<GS::Halfedges>& current,
            const std::optional<GS::Halfedges>& expected) noexcept
        {
            if (current.has_value() != expected.has_value())
                return false;
            return !current.has_value() ||
                   SameProgressivePoissonPropertySet(
                       current->Properties,
                       expected->Properties);
        }

        [[nodiscard]] bool SameOptionalRenderSurface(
            const std::optional<G::RenderSurface>& current,
            const std::optional<G::RenderSurface>& expected) noexcept
        {
            return current.has_value() == expected.has_value() &&
                   (!current.has_value() ||
                    current->Domain == expected->Domain);
        }

        [[nodiscard]] bool SameRenderPoints(
            const G::RenderPoints& current,
            const G::RenderPoints& expected) noexcept
        {
            if (current.Type != expected.Type ||
                current.SizeSource.index() != expected.SizeSource.index())
            {
                return false;
            }
            if (const float* currentSize =
                    std::get_if<float>(&current.SizeSource))
            {
                const float* expectedSize =
                    std::get_if<float>(&expected.SizeSource);
                return expectedSize != nullptr &&
                       SameProgressivePoissonValue(
                           *currentSize,
                           *expectedSize);
            }
            return std::get<std::string>(current.SizeSource) ==
                   std::get<std::string>(expected.SizeSource);
        }

        [[nodiscard]] bool SameOptionalRenderPoints(
            const std::optional<G::RenderPoints>& current,
            const std::optional<G::RenderPoints>& expected) noexcept
        {
            return current.has_value() == expected.has_value() &&
                   (!current.has_value() ||
                    SameRenderPoints(*current, *expected));
        }

        [[nodiscard]] bool SameVisualizationConfig(
            const G::VisualizationConfig& current,
            const G::VisualizationConfig& expected) noexcept
        {
            if (current.Scalar.Isolines.ValueCount !=
                expected.Scalar.Isolines.ValueCount)
            {
                return false;
            }
            for (std::uint32_t i = 0u;
                 i < current.Scalar.Isolines.ValueCount;
                 ++i)
            {
                if (!SameProgressivePoissonValue(
                        current.Scalar.Isolines.Values[i],
                        expected.Scalar.Isolines.Values[i]))
                {
                    return false;
                }
            }
            return current.Source == expected.Source &&
                   SameProgressivePoissonValue(
                       current.Color,
                       expected.Color) &&
                   current.ScalarFieldName == expected.ScalarFieldName &&
                   current.ScalarDomain == expected.ScalarDomain &&
                   current.ColorBufferName == expected.ColorBufferName &&
                   current.Scalar.Map == expected.Scalar.Map &&
                   current.Scalar.AutoRange == expected.Scalar.AutoRange &&
                   SameProgressivePoissonValue(
                       current.Scalar.RangeMin,
                       expected.Scalar.RangeMin) &&
                   SameProgressivePoissonValue(
                       current.Scalar.RangeMax,
                       expected.Scalar.RangeMax) &&
                   current.Scalar.BinCount == expected.Scalar.BinCount &&
                   current.Scalar.Isolines.Num ==
                       expected.Scalar.Isolines.Num &&
                   SameProgressivePoissonValue(
                       current.Scalar.Isolines.Width,
                       expected.Scalar.Isolines.Width) &&
                   SameProgressivePoissonValue(
                       current.Scalar.Isolines.Color,
                       expected.Scalar.Isolines.Color);
        }

        [[nodiscard]] bool SameOptionalVisualizationConfig(
            const std::optional<G::VisualizationConfig>& current,
            const std::optional<G::VisualizationConfig>& expected) noexcept
        {
            return current.has_value() == expected.has_value() &&
                   (!current.has_value() ||
                    SameVisualizationConfig(*current, *expected));
        }

        [[nodiscard]] bool SameProgressivePoissonEntityState(
            const ProgressivePoissonEntityState& current,
            const ProgressivePoissonEntityState& expected) noexcept
        {
            return SameOptionalGeometrySource(
                       current.Vertices,
                       expected.Vertices) &&
                   SameOptionalGeometrySource(
                       current.Edges,
                       expected.Edges) &&
                   SameOptionalHalfedgeSource(
                       current.Halfedges,
                       expected.Halfedges) &&
                   SameOptionalGeometrySource(
                       current.Faces,
                       expected.Faces) &&
                   SameOptionalGeometrySource(
                       current.Nodes,
                       expected.Nodes) &&
                   current.HasMeshTopology ==
                       expected.HasMeshTopology &&
                   current.HasGraphTopology ==
                       expected.HasGraphTopology &&
                   SameOptionalRenderSurface(
                       current.RenderSurface,
                       expected.RenderSurface) &&
                   SameOptionalRenderPoints(
                       current.RenderPoints,
                       expected.RenderPoints) &&
                   SameOptionalVisualizationConfig(
                       current.Visualization,
                       expected.Visualization);
        }

        template <typename T>
        void ApplyOptionalComponent(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const std::optional<T>& component)
        {
            if (component.has_value())
                raw.emplace_or_replace<T>(entity, *component);
            else
                raw.remove<T>(entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus
        ApplyProgressivePoissonEntityState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const ProgressivePoissonEntityState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            ApplyOptionalComponent(raw, *entity, state.Vertices);
            ApplyOptionalComponent(raw, *entity, state.Edges);
            ApplyOptionalComponent(raw, *entity, state.Halfedges);
            ApplyOptionalComponent(raw, *entity, state.Faces);
            ApplyOptionalComponent(raw, *entity, state.Nodes);
            if (state.HasMeshTopology)
                raw.emplace_or_replace<GS::HasMeshTopology>(*entity);
            else
                raw.remove<GS::HasMeshTopology>(*entity);
            if (state.HasGraphTopology)
                raw.emplace_or_replace<GS::HasGraphTopology>(*entity);
            else
                raw.remove<GS::HasGraphTopology>(*entity);
            ApplyOptionalComponent(raw, *entity, state.RenderSurface);
            ApplyOptionalComponent(raw, *entity, state.RenderPoints);
            ApplyOptionalComponent(raw, *entity, state.Visualization);
            return EditorCommandHistoryStatus::Applied;
        }

        void ApplyProgressivePoissonVisualization(
            ProgressivePoissonEntityState& state,
            const EditorProgressivePoissonChannel channel)
        {
            G::RenderPoints points =
                state.RenderPoints.value_or(G::RenderPoints{});
            if (!std::holds_alternative<float>(points.SizeSource) &&
                !std::holds_alternative<std::string>(points.SizeSource))
            {
                points.SizeSource = 4.0f;
            }
            state.RenderPoints = std::move(points);

            G::VisualizationConfig config =
                state.Visualization.value_or(G::VisualizationConfig{});
            config.Source = G::VisualizationConfig::ColorSource::ScalarField;
            config.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
            config.ScalarFieldName = ProgressivePoissonChannelPropertyName(channel);
            config.Scalar.AutoRange = true;
            config.Scalar.BinCount = 0u;
            config.Scalar.Isolines.Num = 0u;
            state.Visualization = std::move(config);
        }

        [[nodiscard]] ProgressivePoissonEntityState
        MakeProgressivePoissonPointCloudState(
            const ProgressivePoissonEntityState& before,
            const Geometry::PointCloud::Cloud& cloud,
            const EditorProgressivePoissonChannel channel)
        {
            ProgressivePoissonEntityState after = before;
            entt::registry staged{};
            const entt::entity entity = staged.create();
            Geometry::PointCloud::Cloud published = cloud;
            GS::PopulateFromCloud(staged, entity, published);
            after.Vertices =
                CaptureOptionalComponent<GS::Vertices>(staged, entity);
            after.Edges.reset();
            after.Halfedges.reset();
            after.Faces.reset();
            after.Nodes.reset();
            after.HasMeshTopology = false;
            after.HasGraphTopology = false;
            after.RenderSurface.reset();
            ApplyProgressivePoissonVisualization(after, channel);
            return after;
        }

        enum class ProgressivePoissonMutationScope : std::uint8_t
        {
            PointAttributes,
            MeshToPointCloud,
        };

        struct ProgressivePoissonMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            ProgressivePoissonMutationScope Scope{
                ProgressivePoissonMutationScope::PointAttributes};
        };

        void StampProgressivePoissonMutation(
            const ProgressivePoissonMutationIdentity& identity)
        {
            entt::registry& raw = identity.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, identity.StableEntityId);
            if (!entity.has_value())
                return;

            if (identity.Scope ==
                ProgressivePoissonMutationScope::MeshToPointCloud)
            {
                Dirty::MarkGpuDirty(raw, *entity);
                Dirty::MarkVertexPositionsDirty(raw, *entity);
                Dirty::MarkVertexNormalsDirty(raw, *entity);
            }
            Dirty::MarkVertexAttributesDirty(raw, *entity);
        }

        [[nodiscard]] EditorCommandStatus
        CommitProgressivePoissonMutation(
            const EditorFeatureBindings& context,
            const std::uint32_t stableEntityId,
            const ProgressivePoissonMutationScope scope,
            ProgressivePoissonEntityState before,
            ProgressivePoissonEntityState after)
        {
            const ProgressivePoissonEntitySnapshot beforeState =
                std::make_shared<ProgressivePoissonEntityState>(
                    std::move(before));
            const ProgressivePoissonEntitySnapshot afterState =
                std::make_shared<ProgressivePoissonEntityState>(
                    std::move(after));
            const ProgressivePoissonMutationIdentity identity{
                .Scene = context.Scene,
                .World = context.World,
                .StableEntityId = stableEntityId,
                .Scope = scope,
            };

            const auto validate =
                [](
                    const ProgressivePoissonMutationIdentity& candidate,
                    const ProgressivePoissonEntitySnapshot& expected,
                    const ProgressivePoissonEntitySnapshot&)
                {
                    if (candidate.Scene == nullptr ||
                        !candidate.World.IsValid())
                    {
                        return EditorCommandHistoryStatus::MissingScene;
                    }
                    if (expected == nullptr)
                        return EditorCommandHistoryStatus::CommandFailed;

                    entt::registry& raw = candidate.Scene->Raw();
                    const std::optional<ECS::EntityHandle> entity =
                        ResolveStableEntity(
                            raw,
                            candidate.StableEntityId);
                    if (!entity.has_value())
                        return EditorCommandHistoryStatus::StaleEntity;
                    return SameProgressivePoissonEntityState(
                               CaptureProgressivePoissonEntityState(
                                   raw,
                                   *entity),
                               *expected)
                        ? EditorCommandHistoryStatus::Applied
                        : EditorCommandHistoryStatus::StaleEntity;
                };
            const auto apply =
                [](
                    const ProgressivePoissonMutationIdentity& candidate,
                    const ProgressivePoissonEntitySnapshot& target)
                {
                    if (target == nullptr)
                        return EditorCommandHistoryStatus::CommandFailed;
                    return ApplyProgressivePoissonEntityState(
                        candidate.Scene,
                        candidate.StableEntityId,
                        *target);
                };
            const auto stamp =
                [](
                    const ProgressivePoissonMutationIdentity& candidate,
                    const ProgressivePoissonEntitySnapshot&,
                    const ProgressivePoissonEntitySnapshot& target)
                {
                    StampProgressivePoissonMutation(candidate);
                    return target;
                };

            if (context.CommandHistory != nullptr)
            {
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        scope ==
                                ProgressivePoissonMutationScope::
                                    MeshToPointCloud
                            ? "Run progressive Poisson mesh sampling"
                            : "Run progressive Poisson sampling",
                        identity,
                        beforeState,
                        beforeState,
                        afterState,
                        validate,
                        apply,
                        stamp);
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus validation =
                validate(identity, beforeState, afterState);
            if (validation != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(validation);
            const EditorCommandHistoryStatus applied =
                apply(identity, afterState);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            (void)stamp(identity, beforeState, afterState);
            return EditorCommandStatus::Applied;
        }

        enum class EditorProgressivePoissonCpuJobSource : std::uint8_t
        {
            PointCloud,
            MeshSurface,
        };

        [[nodiscard]] EditorJobScope
        ToProgressivePoissonJobScope(
            const EditorProgressivePoissonCpuJobSource source) noexcept
        {
            return source ==
                       EditorProgressivePoissonCpuJobSource::MeshSurface
                ? EditorJobScope::MeshSurface
                : EditorJobScope::PointCloudPoint;
        }

        [[nodiscard]] const char* ProgressivePoissonOutputName(
            const EditorProgressivePoissonConfig& config) noexcept
        {
            return ProgressivePoissonChannelPropertyName(config.Channel);
        }

        [[nodiscard]] Core::ErrorCode ProgressivePoissonResultError(
            const EditorProgressivePoissonResult& result) noexcept
        {
            return result.Error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : result.Error;
        }

        void SetProgressivePoissonMeshSurfaceStats(
            EditorProgressivePoissonResult& result,
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

        [[nodiscard]] EditorProgressivePoissonResult
        MakeProgressivePoissonMeshSurfaceSamplingResult(
            const EditorProgressivePoissonConfig& config,
            const ProgressivePoissonBackendResolution& backend,
            const SurfaceSampling::Result& sampled)
        {
            EditorProgressivePoissonResult result{};
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
                    EditorProgressivePoissonBackend::CpuReference;
            result.BackendFallbackReason = backend.FallbackReason;
            SetProgressivePoissonMeshSurfaceStats(result, sampled.Info);
            if (sampled.Succeeded())
                return result;

            result.Status =
                sampled.Status == SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount
                    ? EditorCommandStatus::InvalidProcessingParameters
                    : EditorCommandStatus::GeometryProcessingFailed;
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

        [[nodiscard]] EditorProgressivePoissonResult
        MakePendingProgressivePoissonCpuJobResult(
            const EditorProgressivePoissonCommand& command,
            const JobToken handle,
            const std::uint32_t inputCount,
            const ProgressivePoissonBackendResolution& backend,
            const EditorProgressivePoissonCpuJobSource source)
        {
            EditorProgressivePoissonResult result{};
            result.Status = EditorCommandStatus::Pending;
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
                    EditorProgressivePoissonBackend::CpuReference;
            result.BackendFallbackReason = backend.FallbackReason;
            result.Error = Core::ErrorCode::Success;
            if (source == EditorProgressivePoissonCpuJobSource::MeshSurface)
            {
                result.MeshSurfaceSamplingUsed = true;
                result.MeshSurfaceSampleCount =
                    command.Config.MeshSurfaceSampleCount;
            }
            result.Message = source == EditorProgressivePoissonCpuJobSource::MeshSurface
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
            const EditorFeatureBindings& context,
            EditorProgressivePoissonResult result)
        {
            if (context.MethodResultSinks.ProgressivePoisson)
                context.MethodResultSinks.ProgressivePoisson(std::move(result));
        }

        struct EditorProgressivePoissonCpuJobState
        {
            EditorProgressivePoissonCommand Command{};
            EditorProgressivePoissonCpuJobSource Source{
                EditorProgressivePoissonCpuJobSource::PointCloud};
            ProgressivePoissonBackendResolution Backend{};
            std::vector<glm::vec3> SnapshotPositions{};
            ProgressivePoissonEntitySnapshot BeforeState{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::optional<PPR::Result> Method{};
            std::optional<SurfaceSampling::Result> Sampled{};
            EditorProgressivePoissonResult Result{};
        };

        [[nodiscard]] JobApplyValidation
        ValidateProgressivePoissonApply(
            const EditorFeatureBindings& context,
            const EditorProgressivePoissonCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;
            if (job.BeforeState == nullptr)
                return JobApplyValidation::StaleGeneration;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.Command.StableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            const GS::ConstSourceView view =
                GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            const GS::Domain expectedDomain =
                job.Source ==
                        EditorProgressivePoissonCpuJobSource::
                            MeshSurface
                    ? GS::Domain::Mesh
                    : GS::Domain::PointCloud;
            if (availability.ProvenanceDomain != expectedDomain)
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (!SameProgressivePoissonEntityState(
                    CaptureProgressivePoissonEntityState(raw, *entity),
                    *job.BeforeState))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }

        [[nodiscard]] Core::Result PublishProgressivePoissonPointCloudCpuJob(
            const EditorFeatureBindings& context,
            const EditorProgressivePoissonCpuJobState& job)
        {
            if (!job.Method.has_value())
                return Core::Err(Core::ErrorCode::Unknown);
            if (context.Scene == nullptr)
                return Core::Err(Core::ErrorCode::InvalidState);
            if (job.BeforeState == nullptr ||
                !job.BeforeState->Vertices.has_value())
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }

            if (!job.Result.Succeeded())
            {
                PublishProgressivePoissonResultSink(context, job.Result);
                return Core::Err(ProgressivePoissonResultError(job.Result));
            }

            ProgressivePoissonEntityState after = *job.BeforeState;
            EditorProgressivePoissonResult result =
                PublishProgressivePoissonComputedResult(
                    after.Vertices->Properties,
                    job.Command.Config,
                    *job.Method,
                    job.Result);
            if (result.Succeeded())
            {
                ApplyProgressivePoissonVisualization(
                    after,
                    job.Command.Config.Channel);
                const EditorCommandStatus committed =
                    CommitProgressivePoissonMutation(
                        context,
                        job.Command.StableEntityId,
                        ProgressivePoissonMutationScope::PointAttributes,
                        *job.BeforeState,
                        std::move(after));
                if (committed != EditorCommandStatus::Applied)
                {
                    result.Status = committed;
                    result.Error = Core::ErrorCode::InvalidState;
                    result.Message =
                        "Progressive Poisson point publication became stale.";
                }
                else
                {
                    AppendProgressivePoissonSuccessMessage(result);
                    InvalidateSelectedModelCache(context);
                }
            }

            PublishProgressivePoissonResultSink(context, result);
            return result.Succeeded()
                ? Core::Ok()
                : Core::Err(ProgressivePoissonResultError(result));
        }

        [[nodiscard]] Core::Result PublishProgressivePoissonMeshSurfaceCpuJob(
            const EditorFeatureBindings& context,
            const EditorProgressivePoissonCpuJobState& job)
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
            if (job.BeforeState == nullptr)
                return Core::Err(Core::ErrorCode::InvalidState);

            EditorProgressivePoissonResult result = job.Result;
            ProgressivePoissonEntityState after =
                MakeProgressivePoissonPointCloudState(
                    *job.BeforeState,
                    job.Sampled->Cloud,
                    job.Command.Config.Channel);
            const EditorCommandStatus publishStatus =
                CommitProgressivePoissonMutation(
                    context,
                    job.Command.StableEntityId,
                    ProgressivePoissonMutationScope::MeshToPointCloud,
                    *job.BeforeState,
                    std::move(after));
            if (publishStatus != EditorCommandStatus::Applied)
            {
                result.Status = publishStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Progressive Poisson mesh sample publication failed.";
                PublishProgressivePoissonResultSink(context, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            AppendProgressivePoissonSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishProgressivePoissonResultSink(context, result);
            return Core::Ok();
        }

        [[nodiscard]] JobResultEnvelope
        RunProgressivePoissonPointCloudCpuWorker(
            const std::shared_ptr<EditorProgressivePoissonCpuJobState>& state)
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
            return JobResultEnvelope::Make<EditorGeometryJobResult>(
                EditorGeometryJobResult{
                    .Diagnostic = state->Result.Succeeded()
                        ? "Progressive Poisson CPU result ready"
                        : state->Result.Message,
                });
        }

        [[nodiscard]] JobResultEnvelope
        RunProgressivePoissonMeshSurfaceCpuWorker(
            const std::shared_ptr<EditorProgressivePoissonCpuJobState>& state)
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
                return JobResultEnvelope::Make<EditorGeometryJobResult>(
                    EditorGeometryJobResult{
                        .Diagnostic = state->Result.Message,
                    });
            }

            const std::span<const glm::vec3> sampledPositions =
                sampled.Cloud.Positions();
            ProgressivePoissonComputedResult computed =
                ComputeProgressivePoissonCpuReference(
                    sampledPositions,
                    state->Command.Config,
                    state->Backend);
            state->Method = std::move(computed.Method);
            EditorProgressivePoissonResult result =
                std::move(computed.Result);
            SetProgressivePoissonMeshSurfaceStats(result, sampled.Info);
            result = PublishProgressivePoissonComputedResult(
                sampled.Cloud.PointProperties(),
                state->Command.Config,
                *state->Method,
                std::move(result));
            state->Result = std::move(result);
            state->Sampled = std::move(sampled);
            return JobResultEnvelope::Make<EditorGeometryJobResult>(
                EditorGeometryJobResult{
                    .Diagnostic = state->Result.Succeeded()
                        ? "Progressive Poisson mesh CPU result ready"
                        : state->Result.Message,
                });
        }

        [[nodiscard]] EditorProgressivePoissonResult
        SubmitProgressivePoissonCpuDerivedJob(
            const EditorFeatureBindings& context,
            const EditorProgressivePoissonCommand& command,
            const EditorProgressivePoissonCpuJobSource source,
            std::vector<glm::vec3> snapshotPositions,
            Geometry::HalfedgeMesh::Mesh mesh,
            ProgressivePoissonEntityState beforeState,
            const std::uint32_t inputCount,
            ProgressivePoissonBackendResolution backend)
        {
            auto state =
                std::make_shared<EditorProgressivePoissonCpuJobState>();
            state->Command = command;
            state->Source = source;
            state->Backend = std::move(backend);
            state->SnapshotPositions = std::move(snapshotPositions);
            state->BeforeState =
                std::make_shared<ProgressivePoissonEntityState>(
                    std::move(beforeState));
            state->Mesh = std::move(mesh);

            const EditorJobIdentity identity{
                .EntityId = command.StableEntityId,
                .Scope = ToProgressivePoissonJobScope(source),
                .OutputSemantic = GeometryPresentationSlotSemantic::PointScalarField,
                .OutputName = ProgressivePoissonOutputName(command.Config),
            };
            JobDesc desc{
                .DebugName = source == EditorProgressivePoissonCpuJobSource::MeshSurface
                    ? "Sandbox.ProgressivePoisson.MeshCPU"
                    : "Sandbox.ProgressivePoisson.CPU",
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = std::max<std::uint32_t>(
                    1u,
                    (inputCount + 1023u) / 1024u),
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return state->Source ==
                                   EditorProgressivePoissonCpuJobSource::MeshSurface
                            ? RunProgressivePoissonMeshSurfaceCpuWorker(state)
                            : RunProgressivePoissonPointCloudCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        return ValidateProgressivePoissonApply(
                            context,
                            *state);
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorGeometryJobResult>() == nullptr)
                            return false;
                        const Core::Result published =
                            state->Source ==
                                    EditorProgressivePoissonCpuJobSource::MeshSurface
                                ? PublishProgressivePoissonMeshSurfaceCpuJob(
                                      context,
                                      *state)
                                : PublishProgressivePoissonPointCloudCpuJob(
                                      context,
                                      *state);
                        return published.has_value();
                    },
            };

            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorProgressivePoissonResult pending =
                    MakePendingProgressivePoissonCpuJobResult(
                        command,
                        active->Token,
                        inputCount,
                        state->Backend,
                        source);
                pending.Message = BuildActiveDerivedJobMessage(
                    source == EditorProgressivePoissonCpuJobSource::MeshSurface
                        ? "Progressive Poisson mesh CPU"
                        : "Progressive Poisson CPU",
                    *active);
                return pending;
            }

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                return MakeProgressivePoissonResult(
                    EditorCommandStatus::GeometryProcessingFailed, command.Config.Channel,
                    Core::ErrorCode::InvalidState,
                    "Progressive Poisson CPU job submission was rejected by the runtime "
                    "job lane.");
            }

            return MakePendingProgressivePoissonCpuJobResult(
                command,
                handle,
                inputCount,
                state->Backend,
                source);
        }

    }

    std::vector<EditorGeometryProcessingDomain>
    GetAvailableEditorKMeansDomainsImpl(const ECS::Scene::Registry& registry,
                                           const ECS::EntityHandle entity)
    {
        using Domain = EditorGeometryProcessingDomain;
        const Domain domains =
            GetEditorGeometryProcessingCapabilitiesImpl(registry, entity)
                .Domains &
            GetEditorSupportedGeometryProcessingDomainsImpl(
                EditorGeometryProcessingAlgorithm::KMeans);

        std::vector<Domain> result{};
        result.reserve(3u);
        if (HasAnyEditorGeometryProcessingDomain(
                domains,
                Domain::MeshVertices))
        {
            result.push_back(Domain::MeshVertices);
        }
        if (HasAnyEditorGeometryProcessingDomain(
                domains,
                Domain::GraphVertices))
        {
            result.push_back(Domain::GraphVertices);
        }
        if (HasAnyEditorGeometryProcessingDomain(
                domains,
                Domain::PointCloudPoints))
        {
            result.push_back(Domain::PointCloudPoints);
        }
        return result;
    }

    const char* DebugNameForEditorProgressivePoissonChannelImpl(
        const EditorProgressivePoissonChannel channel) noexcept
    {
        switch (channel)
        {
        case EditorProgressivePoissonChannel::Level:
            return "Level";
        case EditorProgressivePoissonChannel::Phase:
            return "Phase";
        case EditorProgressivePoissonChannel::SplatRadius:
            return "Splat radius";
        case EditorProgressivePoissonChannel::PrefixVisible:
            return "Prefix visible";
        }
        return "Unknown";
    }

    const char* DebugNameForEditorProgressivePoissonBackendImpl(
        const EditorProgressivePoissonBackend backend) noexcept
    {
        switch (backend)
        {
        case EditorProgressivePoissonBackend::CpuReference:
            return "CPU reference";
        case EditorProgressivePoissonBackend::VulkanCompute:
            return "Vulkan compute";
        }
        return "Unknown";
    }

    EditorProgressivePoissonChannel MakeEditorProgressivePoissonChannelImpl(
        const ProgressivePoissonPlaygroundChannel channel) noexcept
    {
        switch (channel)
        {
        case ProgressivePoissonPlaygroundChannel::Level:
            return EditorProgressivePoissonChannel::Level;
        case ProgressivePoissonPlaygroundChannel::Phase:
            return EditorProgressivePoissonChannel::Phase;
        case ProgressivePoissonPlaygroundChannel::SplatRadius:
            return EditorProgressivePoissonChannel::SplatRadius;
        case ProgressivePoissonPlaygroundChannel::PrefixVisible:
            return EditorProgressivePoissonChannel::PrefixVisible;
        }
        return EditorProgressivePoissonChannel::Level;
    }

    ProgressivePoissonPlaygroundChannel
    MakeProgressivePoissonPlaygroundChannel(
        const EditorProgressivePoissonChannel channel) noexcept
    {
        switch (channel)
        {
        case EditorProgressivePoissonChannel::Level:
            return ProgressivePoissonPlaygroundChannel::Level;
        case EditorProgressivePoissonChannel::Phase:
            return ProgressivePoissonPlaygroundChannel::Phase;
        case EditorProgressivePoissonChannel::SplatRadius:
            return ProgressivePoissonPlaygroundChannel::SplatRadius;
        case EditorProgressivePoissonChannel::PrefixVisible:
            return ProgressivePoissonPlaygroundChannel::PrefixVisible;
        }
        return ProgressivePoissonPlaygroundChannel::Level;
    }

    EditorProgressivePoissonBackend MakeEditorProgressivePoissonBackendImpl(
        const ProgressivePoissonPlaygroundBackend backend) noexcept
    {
        switch (backend)
        {
        case ProgressivePoissonPlaygroundBackend::CpuReference:
            return EditorProgressivePoissonBackend::CpuReference;
        case ProgressivePoissonPlaygroundBackend::VulkanCompute:
            return EditorProgressivePoissonBackend::VulkanCompute;
        }
        return EditorProgressivePoissonBackend::CpuReference;
    }

    ProgressivePoissonPlaygroundBackend
    MakeProgressivePoissonPlaygroundBackend(
        const EditorProgressivePoissonBackend backend) noexcept
    {
        switch (backend)
        {
        case EditorProgressivePoissonBackend::CpuReference:
            return ProgressivePoissonPlaygroundBackend::CpuReference;
        case EditorProgressivePoissonBackend::VulkanCompute:
            return ProgressivePoissonPlaygroundBackend::VulkanCompute;
        }
        return ProgressivePoissonPlaygroundBackend::CpuReference;
    }

    EditorProgressivePoissonConfig MakeEditorProgressivePoissonConfigImpl(
        const ProgressivePoissonPlaygroundConfig& config) noexcept
    {
        return EditorProgressivePoissonConfig{
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
            .Channel = MakeEditorProgressivePoissonChannelImpl(config.Channel),
            .Backend = MakeEditorProgressivePoissonBackendImpl(config.Backend),
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
        const EditorProgressivePoissonConfig& config,
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


    EditorProgressivePoissonResult
    ApplyEditorProgressivePoissonCommandImpl(
        const EditorFeatureBindings& context,
        const EditorProgressivePoissonCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeProgressivePoissonResult(
                EditorCommandStatus::MissingScene,
                command.Config.Channel,
                Core::ErrorCode::InvalidState,
                "Progressive Poisson sampling requires an attached scene.");
        }
        if (!IsValidProgressivePoissonConfig(command.Config))
        {
            return MakeProgressivePoissonResult(
                EditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires dimension 2 or 3, positive "
                "grid/max-level/hash settings, and finite radius alpha.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeProgressivePoissonResult(
                EditorCommandStatus::StaleEntity,
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
                EditorCommandStatus::UnsupportedGeometryDomain, command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires selected point-cloud or mesh "
                "GeometrySources.");
        }
        const ProgressivePoissonEntityState beforeState =
            CaptureProgressivePoissonEntityState(raw, *entity);

        if (availability.ProvenanceDomain == GS::Domain::PointCloud)
        {
            if (view.VertexSource == nullptr)
            {
                return MakeProgressivePoissonResult(
                    EditorCommandStatus::UnsupportedGeometryDomain, command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires selected point-cloud "
                    "vertices.");
            }

            std::optional<std::vector<glm::vec3>> positions =
                CollectFiniteVertexPositions(view.VertexSource->Properties);
            if (!positions.has_value())
            {
                return MakeProgressivePoissonResult(
                    EditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires a non-empty finite v:position "
                    "property.");
            }

            if (context.JobCommands.Available())
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
                    EditorProgressivePoissonCpuJobSource::PointCloud,
                    std::move(*positions),
                    Geometry::HalfedgeMesh::Mesh{},
                    beforeState,
                    pointCount,
                    backend);
            }

            ProgressivePoissonEntityState afterState = beforeState;
            if (!afterState.Vertices.has_value())
            {
                return MakeProgressivePoissonResult(
                    EditorCommandStatus::UnsupportedGeometryDomain,
                    command.Config.Channel,
                    Core::ErrorCode::InvalidState,
                    "Progressive Poisson point source state is unavailable.");
            }
            EditorProgressivePoissonResult result =
                RunProgressivePoissonAndPublish(
                    std::span<const glm::vec3>{
                        positions->data(),
                        positions->size()},
                    afterState.Vertices->Properties,
                    command.Config,
                    context.Device);
            if (!result.Succeeded())
                return result;

            ApplyProgressivePoissonVisualization(
                afterState,
                command.Config.Channel);
            const EditorCommandStatus committed =
                CommitProgressivePoissonMutation(
                    context,
                    command.StableEntityId,
                    ProgressivePoissonMutationScope::PointAttributes,
                    beforeState,
                    std::move(afterState));
            if (committed != EditorCommandStatus::Applied)
            {
                result.Status = committed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Progressive Poisson point publication became stale.";
                return result;
            }

            AppendProgressivePoissonSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            return result;
        }

        if (!IsValidProgressivePoissonMeshSurfaceConfig(command.Config))
        {
            return MakeProgressivePoissonResult(
                EditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson mesh sampling requires a positive surface sample "
                "count and finite positive minimum triangle area.");
        }

        const GS::ConstSourceView constView =
            GS::BuildConstView(raw, *entity);
        Detail::EditorMeshSourceSnapshot source =
            Detail::BuildEditorMeshSourceSnapshot(constView);
        if (source.Status != EditorCommandStatus::Applied)
        {
            return MakeProgressivePoissonResult(source.Status, command.Config.Channel, source.Error,
                                                source.Diagnostic.empty()
                                                    ? "Progressive Poisson mesh sampling could "
                                                      "not build selected mesh GeometrySources."
                                                    : source.Diagnostic);
        }

        if (context.JobCommands.Available())
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
                EditorProgressivePoissonCpuJobSource::MeshSurface,
                std::move(source.BeforePositions),
                std::move(source.Mesh),
                beforeState,
                command.Config.MeshSurfaceSampleCount,
                backend);
        }

        SurfaceSampling::Result sampled =
            SurfaceSampling::SampleTriangleMeshSurface(
                source.Mesh,
                ToProgressivePoissonSurfaceParams(command.Config));
        EditorProgressivePoissonResult result{};
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
                    ? EditorCommandStatus::InvalidProcessingParameters
                    : EditorCommandStatus::GeometryProcessingFailed;
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

        ProgressivePoissonEntityState afterState =
            MakeProgressivePoissonPointCloudState(
                beforeState,
                sampled.Cloud,
                command.Config.Channel);
        const EditorCommandStatus publishStatus =
            CommitProgressivePoissonMutation(
                context,
                command.StableEntityId,
                ProgressivePoissonMutationScope::MeshToPointCloud,
                beforeState,
                std::move(afterState));
        if (publishStatus != EditorCommandStatus::Applied)
        {
            result.Status = publishStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Progressive Poisson mesh sample publication failed.";
            return result;
        }

        AppendProgressivePoissonSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    RuntimeEngineConfigApplyResult ApplyEditorClusteringConfigImpl(
        const EditorFeatureBindings& context,
        const ClusteringConfig& config,
        std::string sourceId)
    {
        RuntimeEngineConfigApplyResult result{
            .Status = RuntimeEngineConfigApplyStatus::Rejected,
            .Source = RuntimeConfigControlSource::Editor,
        };
        if (context.EngineConfigControlState == nullptr ||
            !context.PreviewEngineConfigDocument ||
            !context.ApplyEngineConfigHotSubset ||
            !context.EngineConfigCommandsAvailable)
        {
            return result;
        }

        Core::Config::EngineConfig candidate =
            context.EngineConfigControlState->ActiveConfig;
        SetClusteringConfig(candidate, config);
        if (sourceId.empty())
            sourceId = std::string{kClusteringConfigSectionName};
        result.LoadResult = context.PreviewEngineConfigDocument(
            Core::Config::SerializeEngineConfig(candidate),
            sourceId);
        if (!Core::Config::IsConfigUsable(result.LoadResult))
            return result;
        return context.ApplyEngineConfigHotSubset(result.LoadResult);
    }

    std::optional<ClusteringConfig> GetEditorClusteringConfigImpl(
        const EditorFeatureBindings& context) noexcept
    {
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        return GetClusteringConfig(
            context.EngineConfigControlState->ActiveConfig);
    }

    EditorProgressivePoissonConfigResult
    ApplyEditorProgressivePoissonConfigCommandImpl(
        const EditorFeatureBindings& context,
        const EditorProgressivePoissonConfigCommand& command)
    {
        EditorProgressivePoissonConfigResult result{};
        if (context.EngineConfigControlState == nullptr ||
            !context.PreviewEngineConfigDocument ||
            !context.ApplyEngineConfigHotSubset ||
            !context.EngineConfigCommandsAvailable)
        {
            result.Status =
                EditorProgressivePoissonConfigStatus::MissingConfigControl;
            result.Message =
                "Progressive Poisson config requires the engine config-control module.";
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
                EditorProgressivePoissonConfigStatus::PreviewRejected;
            result.Message =
                "Progressive Poisson config preview was rejected.";
            return result;
        }

        result.Apply = context.ApplyEngineConfigHotSubset(result.Preview);
        if (!result.Apply.Succeeded())
        {
            result.Status =
                EditorProgressivePoissonConfigStatus::ApplyRejected;
            result.Message =
                "Progressive Poisson config hot-apply was rejected.";
            return result;
        }

        result.Status =
            result.Apply.Status == RuntimeEngineConfigApplyStatus::NoChange
                ? EditorProgressivePoissonConfigStatus::NoChange
                : EditorProgressivePoissonConfigStatus::Applied;
        result.Message =
            result.Status == EditorProgressivePoissonConfigStatus::NoChange
                ? "Progressive Poisson config unchanged."
                : "Progressive Poisson config applied.";
        return result;
    }

    std::optional<EditorProgressivePoissonConfig>
    GetEditorProgressivePoissonConfigImpl(
        const EditorFeatureBindings& context) noexcept
    {
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        const auto config = GetProgressivePoissonPlaygroundConfig(
            context.EngineConfigControlState->ActiveConfig);
        if (!config.has_value())
            return std::nullopt;
        return MakeEditorProgressivePoissonConfigImpl(*config);
    }

}
