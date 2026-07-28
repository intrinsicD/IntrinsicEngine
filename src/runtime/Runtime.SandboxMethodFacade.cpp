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
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RegistrationAlignment;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.SandboxConfigSections;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectedMeshTextureBake;
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
        namespace SurfaceSampling = Geometry::PointCloud::SurfaceSampling;
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

        // RUNTIME-194 Slice B5d: the result payload for this file's method
        // jobs. The computed result itself already reaches the main thread in
        // the shared job state the worker fills, so the envelope carries only
        // the diagnostic the retired `DerivedJobOutput` exposed — and exists at
        // all because an empty envelope is how `JobService` reports a dropped
        // job.
        struct SandboxMethodJobResult
        {
            std::string Diagnostic{};
        };

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

        [[nodiscard]] std::optional<SandboxEditorJobRecord>
        FindActiveEditorJob(
            const SandboxEditorContext& context,
            const SandboxEditorJobIdentity& identity)
        {
            return Detail::FindActiveSandboxMethodJob(context, identity);
        }

        [[nodiscard]] std::string BuildActiveDerivedJobMessage(
            const std::string_view label,
            const SandboxEditorJobRecord& job)
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

        [[nodiscard]] bool SamePositionSnapshot(
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
                resolved.FallbackReason = "Vulkan compute requested but GPU execution is "
                                          "unavailable; ran CPU reference.";
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

        [[nodiscard]] SandboxEditorJobScope
        ToProgressivePoissonJobScope(
            const SandboxEditorProgressivePoissonCpuJobSource source) noexcept
        {
            return source ==
                       SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                ? SandboxEditorJobScope::MeshSurface
                : SandboxEditorJobScope::PointCloudPoint;
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
            const JobToken handle,
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

        [[nodiscard]] JobApplyValidation
        ValidateProgressivePoissonPointCloudApply(
            const SandboxEditorContext& context,
            const SandboxEditorProgressivePoissonCommand& command,
            const std::vector<glm::vec3>& positions)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
                view.VertexSource == nullptr)
            {
                return JobApplyValidation::StaleGeneration;
            }

            std::optional<std::vector<glm::vec3>> current =
                CollectFiniteVertexPositions(view.VertexSource->Properties);
            if (!current.has_value() ||
                !SamePositionSnapshot(*current, positions))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }

        [[nodiscard]] JobApplyValidation
        ValidateProgressivePoissonMeshSurfaceApply(
            const SandboxEditorContext& context,
            const SandboxEditorProgressivePoissonCpuJobState& job)
        {
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.Command.StableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh)
                return JobApplyValidation::StaleGeneration;

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                job.GeometryMetadataSignature)
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (view.VertexSource == nullptr)
                return JobApplyValidation::StaleGeneration;

            std::optional<std::vector<glm::vec3>> current =
                CollectFiniteVertexPositions(view.VertexSource->Properties);
            if (!current.has_value() ||
                !SamePositionSnapshot(*current, job.SnapshotPositions))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
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

        [[nodiscard]] JobResultEnvelope
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
            return JobResultEnvelope::Make<SandboxMethodJobResult>(
                SandboxMethodJobResult{
                    .Diagnostic = state->Result.Succeeded()
                        ? "Progressive Poisson CPU result ready"
                        : state->Result.Message,
                });
        }

        [[nodiscard]] JobResultEnvelope
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
                return JobResultEnvelope::Make<SandboxMethodJobResult>(
                    SandboxMethodJobResult{
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
            return JobResultEnvelope::Make<SandboxMethodJobResult>(
                SandboxMethodJobResult{
                    .Diagnostic = state->Result.Succeeded()
                        ? "Progressive Poisson mesh CPU result ready"
                        : state->Result.Message,
                });
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

            // The retired key carried `SourcePropertyGeneration`; the dedup
            // guard never compared it, and the staleness it stood for is
            // re-checked by `ValidateBeforeApply` below.
            (void)geometryMetadataSignature;
            const SandboxEditorJobIdentity identity{
                .EntityId = command.StableEntityId,
                .Scope = ToProgressivePoissonJobScope(source),
                .OutputSemantic = GeometryPresentationSlotSemantic::PointScalarField,
                .OutputName = ProgressivePoissonOutputName(command.Config),
            };
            JobDesc desc{
                .DebugName = source == SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
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
                                   SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                            ? RunProgressivePoissonMeshSurfaceCpuWorker(state)
                            : RunProgressivePoissonPointCloudCpuWorker(state);
                    },
                .ValidateBeforeApply =
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
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<SandboxMethodJobResult>() == nullptr)
                            return false;
                        const Core::Result published =
                            state->Source ==
                                    SandboxEditorProgressivePoissonCpuJobSource::MeshSurface
                                ? PublishProgressivePoissonMeshSurfaceCpuJob(
                                      context,
                                      *state)
                                : PublishProgressivePoissonPointCloudCpuJob(
                                      context,
                                      *state);
                        return published.has_value();
                    },
            };

            if (const std::optional<SandboxEditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                SandboxEditorProgressivePoissonResult pending =
                    MakePendingProgressivePoissonCpuJobResult(
                        command,
                        active->Token,
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

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::GeometryProcessingFailed, command.Config.Channel,
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

    std::vector<SandboxEditorGeometryProcessingDomain>
    GetAvailableSandboxEditorKMeansDomains(const ECS::Scene::Registry& registry,
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
                SandboxEditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
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
                SandboxEditorCommandStatus::UnsupportedGeometryDomain, command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires selected point-cloud or mesh "
                "GeometrySources.");
        }

        if (availability.ProvenanceDomain == GS::Domain::PointCloud)
        {
            if (view.VertexSource == nullptr)
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain, command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires selected point-cloud "
                    "vertices.");
            }

            std::optional<std::vector<glm::vec3>> positions =
                CollectFiniteVertexPositions(view.VertexSource->Properties);
            if (!positions.has_value())
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
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
                SandboxEditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson mesh sampling requires a positive surface sample "
                "count and finite positive minimum triangle area.");
        }

        const GS::ConstSourceView constView =
            GS::BuildConstView(raw, *entity);
        Detail::SandboxEditorMeshSourceSnapshot source =
            Detail::BuildSandboxEditorMeshSourceSnapshot(constView);
        if (source.Status != SandboxEditorCommandStatus::Applied)
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

    RuntimeEngineConfigApplyResult ApplySandboxEditorClusteringConfig(
        const SandboxEditorContext& context,
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

    std::optional<ClusteringConfig> GetSandboxEditorClusteringConfig(
        const SandboxEditorContext& context) noexcept
    {
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        return GetClusteringConfig(
            context.EngineConfigControlState->ActiveConfig);
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

}
