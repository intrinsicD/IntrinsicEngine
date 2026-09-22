module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <functional>

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
#include <numeric>
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

module Extrinsic.Runtime.PointSetOperations;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.WorldHandle;
// Connectivity property tags only; this unit never materializes a graph or a
// halfedge mesh. `Geometry.HalfedgeMesh.Fwd` re-exports the graph tags.
import Geometry.HalfedgeMesh.Fwd;
import Geometry.Properties;

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace G = Extrinsic::Graphics::Components;
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
        namespace MeshSupport = GeometryProcessingDetail::MeshSupport;

        // The result payload for this file's method jobs. The computed result
        // itself already reaches the main thread in the shared job state the
        // worker fills, so the envelope carries only a diagnostic — and exists
        // at all because an empty envelope is how `JobService` reports a
        // dropped job.
        using EditorGeometryJobResult = MeshSupport::EditorJobResult;

        using EditorFeatureDetail::ResolveStableEntity;
        using EditorFeatureDetail::ToEditorCommandStatus;
        using MeshSupport::BuildActiveDerivedJobMessage;
        using MeshSupport::CollectFiniteGeometryPositions;
        using MeshSupport::FindActiveEditorJob;
        using MeshSupport::InvalidateSelectedModelCache;

        inline constexpr const char* kProgressivePoissonCpuBackendDisplayName =
            "CPU reference";
        inline constexpr const char* kProgressivePoissonGpuBackendId =
            "gpu_vulkan_compute";
        inline constexpr const char* kProgressivePoissonGpuBackendDisplayName =
            "Vulkan compute";

        [[nodiscard]] const char* ProgressivePoissonBackendId(
            const ProgressivePoissonPlaygroundBackend backend) noexcept
        {
            switch (backend)
            {
            case ProgressivePoissonPlaygroundBackend::CpuReference:
                return PPR::kBackendId;
            case ProgressivePoissonPlaygroundBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendId;
            }
            return PPR::kBackendId;
        }

        [[nodiscard]] const char* ProgressivePoissonBackendDisplayName(
            const ProgressivePoissonPlaygroundBackend backend) noexcept
        {
            switch (backend)
            {
            case ProgressivePoissonPlaygroundBackend::CpuReference:
                return kProgressivePoissonCpuBackendDisplayName;
            case ProgressivePoissonPlaygroundBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendDisplayName;
            }
            return kProgressivePoissonCpuBackendDisplayName;
        }

        [[nodiscard]] const char* ProgressivePoissonChannelPropertyName(
            const ProgressivePoissonPlaygroundConfig& config) noexcept
        {
            switch (config.Channel)
            {
            case ProgressivePoissonPlaygroundChannel::Level:
                return config.Level.Name.c_str();
            case ProgressivePoissonPlaygroundChannel::Rank:
                return config.Rank.Name.c_str();
            case ProgressivePoissonPlaygroundChannel::SplatRadius:
                return config.SplatRadius.Name.c_str();
            case ProgressivePoissonPlaygroundChannel::PrefixVisible:
                return config.PrefixVisible.Name.c_str();
            }
            return config.Level.Name.c_str();
        }

        [[nodiscard]] EditorProgressivePoissonResult
        MakeProgressivePoissonResult(
            const EditorCommandStatus status,
            const ProgressivePoissonPlaygroundChannel channel,
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

        // The serialized config keeps `double` knobs, but both sampler backends
        // take `float`. Narrow once, here, and validate the narrowed values so a
        // double that collapses to zero or infinity is rejected rather than
        // reaching a backend with a different number than it was checked against.
        [[nodiscard]] float ProgressivePoissonHashLoadFactor(
            const ProgressivePoissonPlaygroundConfig& config) noexcept
        {
            return static_cast<float>(config.HashLoadFactor);
        }

        [[nodiscard]] float ProgressivePoissonRadiusAlpha(
            const ProgressivePoissonPlaygroundConfig& config) noexcept
        {
            return static_cast<float>(config.RadiusAlpha);
        }

        [[nodiscard]] bool IsValidProgressivePoissonConfig(
            const ProgressivePoissonPlaygroundConfig& config) noexcept
        {
            const float hashLoadFactor = ProgressivePoissonHashLoadFactor(config);
            const float radiusAlpha = ProgressivePoissonRadiusAlpha(config);
            return (config.Dimension == 2u || config.Dimension == 3u) &&
                   config.GridWidth > 0u &&
                   config.MaxLevels > 0u &&
                   std::isfinite(hashLoadFactor) &&
                   hashLoadFactor > 0.0f &&
                   std::isfinite(radiusAlpha);
        }

        [[nodiscard]] PPR::Config ToProgressivePoissonReferenceConfig(
            const ProgressivePoissonPlaygroundConfig& config) noexcept
        {
            PPR::Config out{};
            out.Dimension = config.Dimension;
            out.GridWidth = config.GridWidth;
            out.MaxLevels = config.MaxLevels;
            out.HashLoadFactor = ProgressivePoissonHashLoadFactor(config);
            out.RadiusAlpha = ProgressivePoissonRadiusAlpha(config);
            out.RandomizeGridOrigin = config.RandomizeGridOrigin;
            out.GridOriginSeed = config.GridOriginSeed;
            out.ShuffleWithinLevels = config.ShuffleWithinLevels;
            out.ShuffleSeed = config.ShuffleSeed;
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
            const std::uint32_t prefixCount, const ProgressivePoissonPlaygroundConfig& config)
        {
            const std::size_t pointCount = properties.Size();
            std::vector<double> levels(pointCount, -1.0);
            std::vector<double> ranks(pointCount, -1.0);
            std::vector<double> splatRadii(pointCount, 0.0);
            std::vector<double> prefixVisible(pointCount, 0.0);

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

                    levels[pointIndex] = static_cast<double>(level);
                    ranks[pointIndex] = static_cast<double>(rank);
                    if (rank < method.SplatRadii.size())
                        splatRadii[pointIndex] = method.SplatRadii[rank];
                    prefixVisible[pointIndex] = rank < prefixCount ? 1.0f : 0.0f;
                }
            }

            const std::array refs{&config.Level, &config.Rank, &config.SplatRadius, &config.PrefixVisible};
            const std::array values{&levels, &ranks, &splatRadii, &prefixVisible};
            std::vector<std::uint32_t> slots(pointCount);
            std::iota(slots.begin(), slots.end(), 0u);
            std::array<GeometryScalarPropertySnapshot, 4> outputs;
            for (std::size_t i = 0; i < refs.size(); ++i)
            {
                outputs[i] = CaptureGeometryScalarProperty(properties, *refs[i]);
                if ((properties.Exists(refs[i]->Name) && !outputs[i].Exists) ||
                    !PrepareGeometryScalarProperty(outputs[i], refs[i]->ValueKind, pointCount, slots, *values[i]))
                    return false;
            }
            Geometry::PropertySet staged = properties;
            for (std::size_t i = 0; i < refs.size(); ++i)
                if (!ApplyGeometryScalarProperty(staged, *refs[i], outputs[i])) return false;
            properties = std::move(staged);
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
            ProgressivePoissonPlaygroundBackend Requested{
                ProgressivePoissonPlaygroundBackend::CpuReference};
            ProgressivePoissonPlaygroundBackend Actual{
                ProgressivePoissonPlaygroundBackend::CpuReference};
            std::string FallbackReason{};
        };

        [[nodiscard]] ProgressivePoissonGpuConfig ToProgressivePoissonGpuConfig(
            const ProgressivePoissonPlaygroundConfig& config) noexcept
        {
            return ProgressivePoissonGpuConfig{
                .Dimension = config.Dimension,
                .GridWidth = config.GridWidth,
                .MaxLevels = config.MaxLevels,
                .HashLoadFactor = ProgressivePoissonHashLoadFactor(config),
                .RadiusAlpha = ProgressivePoissonRadiusAlpha(config),
                .RandomizeGridOrigin = config.RandomizeGridOrigin,
                .GridOriginSeed = config.GridOriginSeed,
                .ShuffleWithinLevels = config.ShuffleWithinLevels,
                .ShuffleSeed = config.ShuffleSeed,
            };
        }

        [[nodiscard]] ProgressivePoissonBackendResolution
        ResolveProgressivePoissonBackend(
            const ProgressivePoissonPlaygroundBackend requested,
            const ProgressivePoissonPlaygroundConfig& config,
            const std::uint32_t inputCount,
            RHI::IDevice* device)
        {
            ProgressivePoissonBackendResolution resolved{};
            resolved.Requested = requested;
            if (requested == ProgressivePoissonPlaygroundBackend::CpuReference)
            {
                resolved.Actual = ProgressivePoissonPlaygroundBackend::CpuReference;
                return resolved;
            }

            resolved.Actual = ProgressivePoissonPlaygroundBackend::CpuReference;
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
                    ProgressivePoissonPlaygroundBackend::VulkanCompute;
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
            const ProgressivePoissonPlaygroundConfig& config,
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
                    ProgressivePoissonPlaygroundBackend::CpuReference;
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
            const ProgressivePoissonPlaygroundConfig& config,
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
            const PPR::Result& method,
            EditorProgressivePoissonResult result, const ProgressivePoissonPlaygroundConfig& config)
        {
            if (!result.Succeeded())
                return result;

            if (!PublishProgressivePoissonProperties(
                    properties,
                    method,
                    result.PrefixCount, config))
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Progressive Poisson output storage cannot represent the result or has an incompatible shape.";
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
            const ProgressivePoissonPlaygroundConfig& config,
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
                computed.Method,
                std::move(computed.Result), config);
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
                DebugNameForProgressivePoissonChannel(
                    result.Channel);
            if (!result.LevelAcceptedCounts.empty())
            {
                result.Message += ", level_counts=[";
                result.Message += FormatProgressivePoissonLevelCounts(
                    result.LevelAcceptedCounts);
                result.Message += "]";
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
            bool HasMeshTopology{false};
            bool HasGraphTopology{false};
            std::optional<G::RenderSurface> RenderSurface{};
            std::optional<G::RenderPoints> RenderPoints{};
            std::optional<G::VisualizationConfig> Visualization{};
        };

        [[nodiscard]] Geometry::PropertySet* ProgressivePoissonProperties(
            ProgressivePoissonEntityState& state, const GeometryElementDomain domain)
        {
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
            case GeometryElementDomain::GraphNode:
            case GeometryElementDomain::PointCloudPoint: return state.Vertices ? &state.Vertices->Properties : nullptr;
            case GeometryElementDomain::MeshFace: return state.Faces ? &state.Faces->Properties : nullptr;
            case GeometryElementDomain::MeshEdge:
            case GeometryElementDomain::GraphEdge: return state.Edges ? &state.Edges->Properties : nullptr;
            case GeometryElementDomain::MeshHalfedge:
            case GeometryElementDomain::GraphHalfedge: return state.Halfedges ? &state.Halfedges->Properties : nullptr;
            default: return nullptr;
            }
        }

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
            return GeometryValueComparison::BitEqual(lhs, rhs);
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<
            Geometry::Graph::VertexConnectivity>(
            const Geometry::Graph::VertexConnectivity& lhs,
            const Geometry::Graph::VertexConnectivity& rhs) noexcept
        {
            return lhs.Halfedge == rhs.Halfedge;
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<
            Geometry::Graph::HalfedgeConnectivity>(
            const Geometry::Graph::HalfedgeConnectivity& lhs,
            const Geometry::Graph::HalfedgeConnectivity& rhs) noexcept
        {
            return lhs.Vertex == rhs.Vertex &&
                   lhs.Next == rhs.Next &&
                   lhs.Prev == rhs.Prev;
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<
            Geometry::HalfedgeMesh::HalfedgeFaceConnectivity>(
            const Geometry::HalfedgeMesh::HalfedgeFaceConnectivity& lhs,
            const Geometry::HalfedgeMesh::HalfedgeFaceConnectivity& rhs) noexcept
        {
            return lhs.Face == rhs.Face;
        }

        template <>
        [[nodiscard]] bool SameProgressivePoissonValue<
            Geometry::HalfedgeMesh::FaceConnectivity>(
            const Geometry::HalfedgeMesh::FaceConnectivity& lhs,
            const Geometry::HalfedgeMesh::FaceConnectivity& rhs) noexcept
        {
            return lhs.Halfedge == rhs.Halfedge;
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

        [[nodiscard]] bool SameProgressivePoissonUnknownProperty(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected,
            const Geometry::PropertyDescriptor& descriptor) noexcept
        {
            const Geometry::Internal::TypeID type = descriptor.Type;
            if (type == Geometry::Internal::TypeInfo<
                            Geometry::Graph::VertexConnectivity>::ID())
            {
                return SameProgressivePoissonProperty<
                    Geometry::Graph::VertexConnectivity>(
                    current,
                    expected,
                    descriptor.Name);
            }
            if (type == Geometry::Internal::TypeInfo<
                            Geometry::Graph::HalfedgeConnectivity>::ID())
            {
                return SameProgressivePoissonProperty<
                    Geometry::Graph::HalfedgeConnectivity>(
                    current,
                    expected,
                    descriptor.Name);
            }
            if (type == Geometry::Internal::TypeInfo<
                            Geometry::HalfedgeMesh::HalfedgeFaceConnectivity>::ID())
            {
                return SameProgressivePoissonProperty<
                    Geometry::HalfedgeMesh::HalfedgeFaceConnectivity>(
                    current,
                    expected,
                    descriptor.Name);
            }
            if (type == Geometry::Internal::TypeInfo<
                            Geometry::HalfedgeMesh::FaceConnectivity>::ID())
            {
                return SameProgressivePoissonProperty<
                    Geometry::HalfedgeMesh::FaceConnectivity>(
                    current,
                    expected,
                    descriptor.Name);
            }
            // An erased value that cannot be compared must fail closed. A
            // queued publication may be retried, but it must never overwrite
            // an unverified property snapshot.
            return false;
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
                    same = SameProgressivePoissonUnknownProperty(
                        currentView,
                        expectedView,
                        rhs);
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
            const ProgressivePoissonPlaygroundConfig& bindings, const GeometryElementDomain domain)
        {
            if (domain != GeometryElementDomain::MeshVertex && domain != GeometryElementDomain::GraphNode &&
                domain != GeometryElementDomain::PointCloudPoint) return;
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
            config.ScalarFieldName = ProgressivePoissonChannelPropertyName(bindings);
            config.Scalar.AutoRange = true;
            config.Scalar.BinCount = 0u;
            config.Scalar.Isolines.Num = 0u;
            state.Visualization = std::move(config);
        }

        struct ProgressivePoissonMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        void StampProgressivePoissonMutation(
            const ProgressivePoissonMutationIdentity& identity)
        {
            entt::registry& raw = identity.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, identity.StableEntityId);
            if (!entity.has_value())
                return;

            Dirty::MarkVertexAttributesDirty(raw, *entity);
        }

        [[nodiscard]] EditorCommandStatus
        CommitProgressivePoissonMutation(
            const EditorProcessingContext& context,
            const std::uint32_t stableEntityId,
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
                        "Run progressive Poisson sampling",
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

        [[nodiscard]] std::optional<GeometryElementDomain>
        ResolveProgressivePoissonVertexDomain(
            const GeometryEntityAvailability& availability) noexcept
        {
            GeometryElementDomain domain = GeometryElementDomain::Unknown;
            switch (availability.Sources.ProvenanceDomain)
            {
            case GS::Domain::Mesh:
                domain = GeometryElementDomain::MeshVertex;
                break;
            case GS::Domain::Graph:
                domain = GeometryElementDomain::GraphNode;
                break;
            case GS::Domain::PointCloud:
                domain = GeometryElementDomain::PointCloudPoint;
                break;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                return std::nullopt;
            }
            return SupportsGeometryElementDomain(availability, domain)
                ? std::optional{domain}
                : std::nullopt;
        }

        [[nodiscard]] std::optional<GeometryElementDomain> ResolveProgressivePoissonInputDomain(
            const GeometryEntityAvailability& availability, const ProgressivePoissonPlaygroundConfig& config)
        {
            if (config.Positions.Domain == GeometryElementDomain::Unknown)
                return ResolveProgressivePoissonVertexDomain(availability);
            return SupportsGeometryElementDomain(availability, config.Positions.Domain)
                ? std::optional{config.Positions.Domain} : std::nullopt;
        }

        struct ProgressivePoissonInput
        {
            ECS::EntityHandle Entity;
            GeometryElementDomain Domain;
            const Geometry::PropertySet* Properties;
        };

        [[nodiscard]] std::optional<ProgressivePoissonInput> ResolveProgressivePoissonInput(
            const EditorProcessingContext& context, const EditorProgressivePoissonCommand& command,
            EditorProgressivePoissonResult& result)
        {
            if (context.Scene == nullptr)
            {
                result = MakeProgressivePoissonResult(
                    EditorCommandStatus::MissingScene,
                    command.Config.Channel,
                    Core::ErrorCode::InvalidState,
                    "Progressive Poisson sampling requires an attached scene.");
                return std::nullopt;
            }
            if (!IsValidProgressivePoissonConfig(command.Config))
            {
                result = MakeProgressivePoissonResult(
                    EditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires dimension 2 or 3, positive "
                    "grid/max-level/hash settings, and finite radius alpha.");
                return std::nullopt;
            }

            if (!IsValidProgressivePoissonPropertyBindings(command.Config))
            {
                result = MakeProgressivePoissonResult(EditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                    Core::ErrorCode::InvalidArgument, "Invalid Progressive Poisson property bindings.");
                return std::nullopt;
            }
            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
            {
                result = MakeProgressivePoissonResult(
                    EditorCommandStatus::StaleEntity,
                    command.Config.Channel,
                    Core::ErrorCode::ResourceNotFound,
                    "Progressive Poisson target entity is stale or no longer live.");
                return std::nullopt;
            }

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            const std::optional<GeometryElementDomain> vertexDomain =
                ResolveProgressivePoissonInputDomain(availability, command.Config);
            if (!vertexDomain.has_value())
            {
                result = MakeProgressivePoissonResult(
                    EditorCommandStatus::UnsupportedGeometryDomain, command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires a supported position-property element domain.");
                return std::nullopt;
            }
            const auto* properties = ResolveGeometryPropertySet(availability, *vertexDomain);
            const auto positions = properties->Get<glm::vec3>(command.Config.Positions.Name);
            if (!positions || positions.Vector().empty() ||
                positions.Vector().size() != properties->Size())
            {
                result = MakeProgressivePoissonResult(
                    EditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires a non-empty vec3 position property at source cardinality.");
                return std::nullopt;
            }
            for (const auto* output : {&command.Config.Level, &command.Config.Rank,
                                       &command.Config.SplatRadius, &command.Config.PrefixVisible})
            {
                auto ref = *output;
                ref.Domain = *vertexDomain;
                if (IsTopologyProperty(ref.Domain, ref.Name) ||
                    ((ref.Domain == GeometryElementDomain::MeshVertex || ref.Domain == GeometryElementDomain::GraphNode ||
                      ref.Domain == GeometryElementDomain::PointCloudPoint) && ref.Name == "v:position") ||
                    (properties->Exists(ref.Name) && !ResolveGeometryProperty(availability, ref, properties->Size(), false).Resolved()))
                {
                    result = MakeProgressivePoissonResult(EditorCommandStatus::InvalidProcessingParameters,
                        command.Config.Channel, Core::ErrorCode::InvalidArgument,
                        "Progressive Poisson outputs require non-structural, count-matched storage of the declared scalar kind.");
                    return std::nullopt;
                }
            }
            return ProgressivePoissonInput{*entity, *vertexDomain, properties};
        }

        [[nodiscard]] const char* ProgressivePoissonOutputName(
            const ProgressivePoissonPlaygroundConfig& config) noexcept
        {
            return ProgressivePoissonChannelPropertyName(config);
        }

        [[nodiscard]] Core::ErrorCode ProgressivePoissonResultError(
            const EditorProgressivePoissonResult& result) noexcept
        {
            return result.Error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : result.Error;
        }

        [[nodiscard]] EditorProgressivePoissonResult
        MakePendingProgressivePoissonCpuJobResult(
            const EditorProgressivePoissonCommand& command,
            const JobToken handle,
            const std::uint32_t inputCount,
            const ProgressivePoissonBackendResolution& backend)
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
                    ProgressivePoissonPlaygroundBackend::CpuReference;
            result.BackendFallbackReason = backend.FallbackReason;
            result.Error = Core::ErrorCode::Success;
            result.Message = "Progressive Poisson CPU job queued";
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

        struct EditorProgressivePoissonCpuJobState
        {
            EditorProgressivePoissonCommand Command{};
            GeometryElementDomain Domain{GeometryElementDomain::Unknown};
            ProgressivePoissonBackendResolution Backend{};
            std::vector<glm::vec3> SnapshotPositions{};
            ProgressivePoissonEntitySnapshot BeforeState{};
            std::optional<PPR::Result> Method{};
            // A rejected publication also runs the unpublished finalizer;
            // Delivered prevents a second terminal callback. Duplicate active
            // requests register no sink.
            std::function<void(EditorProgressivePoissonResult)> Sink{};
            bool Delivered{false};
            // Seeded with the submit-time channel/backend identity so a job that
            // never reached the worker still reports which channel and backend
            // it was queued for.
            EditorProgressivePoissonResult Result{};
            // Last answer this job's `ValidateBeforeApply` gave the drain. The
            // finalizer takes no arguments, so the reason a completion was
            // refused has to be recorded where it was decided. `Current` means
            // the gate never rejected, so the job ended for another reason —
            // cancellation, or a publisher that refused the envelope.
            JobApplyValidation LastApplyValidation{JobApplyValidation::Current};
        };

        void PublishProgressivePoissonResultSink(
            EditorProgressivePoissonCpuJobState& job,
            EditorProgressivePoissonResult result)
        {
            if (job.Delivered)
                return;
            job.Delivered = true;
            if (job.Sink)
                job.Sink(std::move(result));
        }

        [[nodiscard]] JobApplyValidation
        ValidateProgressivePoissonApply(
            const EditorProcessingContext& context,
            const EditorProgressivePoissonCpuJobState& job)
        {
            // Epoch first: after detachment the borrowed scene pointer may name
            // a freed registry, so nothing below may read it.
            if (context.AttachmentActive && !context.AttachmentActive())
                return JobApplyValidation::StaleWorld;
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
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            if (ResolveProgressivePoissonInputDomain(availability, job.Command.Config) !=
                std::optional{job.Domain})
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

        [[nodiscard]] Core::Result PublishProgressivePoissonCpuJob(
            const EditorProcessingContext& context,
            EditorProgressivePoissonCpuJobState& job)
        {
            // Report publication failures here while their diagnostics are
            // available; Delivered suppresses the subsequent finalizer callback.
            const auto abandon = [&job](std::string message, const Core::ErrorCode error)
            {
                EditorProgressivePoissonResult result = job.Result;
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = error;
                result.Message = std::move(message);
                PublishProgressivePoissonResultSink(job, std::move(result));
                return Core::Err(error);
            };
            // `ValidateProgressivePoissonApply` has just answered `Current` on
            // this thread, so the attachment, the scene and the owned
            // submit-time snapshot are all live here.
            if (!job.Method.has_value())
                return abandon("Progressive Poisson produced no result to publish.",
                               Core::ErrorCode::Unknown);

            if (!job.Result.Succeeded())
            {
                PublishProgressivePoissonResultSink(job, job.Result);
                return Core::Err(ProgressivePoissonResultError(job.Result));
            }

            ProgressivePoissonEntityState after = *job.BeforeState;
            if (!ProgressivePoissonProperties(after, job.Domain))
                return abandon("Progressive Poisson vertex source state is unavailable.",
                               Core::ErrorCode::InvalidState);
            EditorProgressivePoissonResult result =
                PublishProgressivePoissonComputedResult(
                    *ProgressivePoissonProperties(after, job.Domain),
                    *job.Method,
                    job.Result, job.Command.Config);
            if (result.Succeeded())
            {
                ApplyProgressivePoissonVisualization(
                    after,
                    job.Command.Config, job.Domain);
                const EditorCommandStatus committed =
                    CommitProgressivePoissonMutation(
                        context,
                        job.Command.StableEntityId,
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

            PublishProgressivePoissonResultSink(job, result);
            return result.Succeeded()
                ? Core::Ok()
                : Core::Err(ProgressivePoissonResultError(result));
        }

        // A queued job that terminates without publishing — cancelled, stale,
        // detached, or dropped — still owes the editor exactly one terminal
        // result, otherwise its panel row stays `Pending` forever. Reads only
        // the job's own state: the scene and the spatial caches may already be
        // gone by the time this runs, and nothing here is published to them.
        void FinalizeUnpublishedProgressivePoissonJob(
            EditorProgressivePoissonCpuJobState& job)
        {
            if (job.Delivered)
                return;
            auto failure = MeshSupport::BuildUnpublishedEditorJobFailure(
                job.LastApplyValidation,
                "Sandbox.ProgressivePoisson.CPU",
                job.Result.Status == EditorCommandStatus::GeometryProcessingFailed
                    ? std::string_view{job.Result.Message} : std::string_view{});
            EditorProgressivePoissonResult result = job.Result;
            result.Status = failure.Status;
            result.Error = failure.Error;
            result.Message = std::move(failure.Message);
            PublishProgressivePoissonResultSink(job, std::move(result));
        }

        [[nodiscard]] JobResultEnvelope
        RunProgressivePoissonCpuWorker(
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

        [[nodiscard]] EditorProgressivePoissonResult
        SubmitProgressivePoissonCpuDerivedJob(
            const EditorProcessingContext& context,
            const EditorProgressivePoissonCommand& command,
            const GeometryElementDomain domain,
            std::vector<glm::vec3> snapshotPositions,
            ProgressivePoissonEntityState beforeState,
            const std::uint32_t inputCount,
            ProgressivePoissonBackendResolution backend,
            std::function<void(EditorProgressivePoissonResult)> onComplete)
        {
            auto state =
                std::make_shared<EditorProgressivePoissonCpuJobState>();
            state->Command = command;
            state->Domain = domain;
            state->Backend = std::move(backend);
            state->SnapshotPositions = std::move(snapshotPositions);
            state->BeforeState =
                std::make_shared<ProgressivePoissonEntityState>(
                    std::move(beforeState));
            state->Result = MakePendingProgressivePoissonCpuJobResult(
                command, JobToken{}, inputCount, state->Backend);

            const EditorJobIdentity identity{
                .EntityId = command.StableEntityId,
                .Scope = ToEditorJobScope(domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::PointScalarField,
                .OutputName = ProgressivePoissonOutputName(command.Config),
            };
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                // The active job already owns the callback that will deliver
                // this output's terminal result; a duplicate request registers
                // none, so this state is discarded without a sink.
                EditorProgressivePoissonResult pending =
                    MakePendingProgressivePoissonCpuJobResult(
                        command,
                        active->Token,
                        inputCount,
                        state->Backend);
                pending.Message = BuildActiveDerivedJobMessage(
                    "Progressive Poisson CPU",
                    *active);
                return pending;
            }

            state->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            JobDesc desc{
                .DebugName = "Sandbox.ProgressivePoisson.CPU",
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = std::max<std::uint32_t>(
                    1u,
                    (inputCount + 1023u) / 1024u),
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunProgressivePoissonCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        const JobApplyValidation validation =
                            ValidateProgressivePoissonApply(context, *state);
                        state->LastApplyValidation = validation;
                        return validation;
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorGeometryJobResult>() == nullptr)
                            return false;
                        const Core::Result published =
                            PublishProgressivePoissonCpuJob(context, *state);
                        return published.has_value();
                    },
                .FinalizeUnpublishedOnMainThread =
                    [state]() { FinalizeUnpublishedProgressivePoissonJob(*state); },
            };

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                // The job was never enqueued, so no finalizer will run for it
                // and this state dies here with its sink uncalled.
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
                state->Backend);
        }

    }

    const char* DebugNameForProgressivePoissonChannel(
        const ProgressivePoissonPlaygroundChannel channel) noexcept
    {
        switch (channel)
        {
        case ProgressivePoissonPlaygroundChannel::Level:
            return "Level";
        case ProgressivePoissonPlaygroundChannel::Rank:
            return "Rank";
        case ProgressivePoissonPlaygroundChannel::SplatRadius:
            return "Splat radius";
        case ProgressivePoissonPlaygroundChannel::PrefixVisible:
            return "Prefix visible";
        }
        return "Unknown";
    }

    const char* DebugNameForProgressivePoissonBackend(
        const ProgressivePoissonPlaygroundBackend backend) noexcept
    {
        switch (backend)
        {
        case ProgressivePoissonPlaygroundBackend::CpuReference:
            return "CPU reference";
        case ProgressivePoissonPlaygroundBackend::VulkanCompute:
            return "Vulkan compute";
        }
        return "Unknown";
    }

    ActionReadiness PreviewEditorProgressivePoissonCommand(
        const EditorProcessingCommands& commands, const EditorProgressivePoissonCommand& command)
    {
        EditorProgressivePoissonResult result{};
        const auto input = ResolveProgressivePoissonInput(
            EditorProcessingCommandsAccess::Resolve(commands), command, result);
        return {input.has_value(), std::move(result.Message)};
    }

    EditorProgressivePoissonResult
    ApplyEditorProgressivePoissonCommand(
        const EditorProcessingCommands& commands,
        const EditorProgressivePoissonCommand& command,
        std::function<void(EditorProgressivePoissonResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorProgressivePoissonResult admissionResult{};
        const auto input = ResolveProgressivePoissonInput(context, command, admissionResult);
        if (!input) return admissionResult;
        const ProgressivePoissonEntityState beforeState =
            CaptureProgressivePoissonEntityState(context.Scene->Raw(), input->Entity);
        std::optional<std::vector<glm::vec3>> positions =
            CollectFiniteGeometryPositions(*input->Properties, command.Config.Positions.Name);
        if (!positions)
        {
            return MakeProgressivePoissonResult(
                EditorCommandStatus::InvalidProcessingParameters, command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires every selected position value to be finite.");
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
                input->Domain,
                std::move(*positions),
                beforeState,
                pointCount,
                backend,
                std::move(onComplete));
        }

        ProgressivePoissonEntityState afterState = beforeState;
        if (ProgressivePoissonProperties(afterState, input->Domain) == nullptr)
        {
            return MakeProgressivePoissonResult(
                EditorCommandStatus::UnsupportedGeometryDomain,
                command.Config.Channel,
                Core::ErrorCode::InvalidState,
                "Progressive Poisson vertex source state is unavailable.");
        }
        EditorProgressivePoissonResult result =
            RunProgressivePoissonAndPublish(
                std::span<const glm::vec3>{
                    positions->data(),
                    positions->size()},
                *ProgressivePoissonProperties(afterState, input->Domain),
                command.Config,
                context.Device);
        if (!result.Succeeded())
            return result;

        ApplyProgressivePoissonVisualization(afterState, command.Config, input->Domain);
        const EditorCommandStatus publishStatus =
            CommitProgressivePoissonMutation(
                context,
                command.StableEntityId,
                beforeState,
                std::move(afterState));
        if (publishStatus != EditorCommandStatus::Applied)
        {
            result.Status = publishStatus;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Progressive Poisson vertex publication became stale.";
            return result;
        }

        AppendProgressivePoissonSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    RuntimeEngineConfigApplyResult ApplyEditorProgressivePoissonConfig(
        const EditorProcessingCommands& commands,
        const ProgressivePoissonPlaygroundConfig& config, std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateProgressivePoissonConfigSection(SerializeProgressivePoissonPlaygroundConfig(config), {},
                                                    kProgressivePoissonConfigSectionName),
            sourceId.empty() ? std::string{kProgressivePoissonConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetProgressivePoissonPlaygroundConfig(candidate, config); });
    }

    std::optional<ProgressivePoissonPlaygroundConfig>
    GetEditorProgressivePoissonConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        return GetProgressivePoissonPlaygroundConfig(
            context.EngineConfigControlState->ActiveConfig);
    }
}
