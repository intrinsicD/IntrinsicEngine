module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.SharedRenderRecipeExecution;

import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] bool ContainsCapability(
            const RendererDescriptor& renderer,
            const RendererCapability capability) noexcept
        {
            return std::find(renderer.SupportedCapabilities.begin(),
                             renderer.SupportedCapabilities.end(),
                             capability) != renderer.SupportedCapabilities.end();
        }

        [[nodiscard]] bool ContainsProduct(
            const std::span<const SharedRecipeProductKind> products,
            const SharedRecipeProductKind product) noexcept
        {
            return std::find(products.begin(), products.end(), product) != products.end();
        }

        [[nodiscard]] bool IsVisibilityProduct(
            const SharedRecipeProductKind product) noexcept
        {
            switch (product)
            {
            case SharedRecipeProductKind::VisibleItemSet:
            case SharedRecipeProductKind::RejectedItemDiagnostics:
            case SharedRecipeProductKind::GroupingKeys:
            case SharedRecipeProductKind::BatchGroups:
            case SharedRecipeProductKind::InstanceGroups:
            case SharedRecipeProductKind::LodSelections:
            case SharedRecipeProductKind::SpatialPartitions:
            case SharedRecipeProductKind::AccelerationStructureBuildRequests:
                return true;
            case SharedRecipeProductKind::LightSet:
            case SharedRecipeProductKind::EmissiveGeometry:
            case SharedRecipeProductKind::EnvironmentMap:
            case SharedRecipeProductKind::ProbeSet:
            case SharedRecipeProductKind::VolumeSet:
            case SharedRecipeProductKind::Tags:
            case SharedRecipeProductKind::QualitySettings:
            case SharedRecipeProductKind::ShadowIntent:
            case SharedRecipeProductKind::ProbeIntent:
            case SharedRecipeProductKind::GlobalIlluminationIntent:
            case SharedRecipeProductKind::DebugMode:
            case SharedRecipeProductKind::Fallbacks:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool IsLightingProduct(
            const SharedRecipeProductKind product) noexcept
        {
            return !IsVisibilityProduct(product);
        }

        void AddDiagnostic(std::vector<SharedRecipeDiagnostic>& diagnostics,
                           const SharedRecipeDiagnosticCode code,
                           const SharedRecipeDiagnosticSeverity severity,
                           std::string subject,
                           std::string message)
        {
            diagnostics.push_back(SharedRecipeDiagnostic{
                .Code = code,
                .Severity = severity,
                .Subject = std::move(subject),
                .Message = std::move(message),
            });
        }

        void AddWarning(std::vector<SharedRecipeDiagnostic>& diagnostics,
                        const SharedRecipeDiagnosticCode code,
                        std::string subject,
                        std::string message)
        {
            AddDiagnostic(diagnostics,
                          code,
                          SharedRecipeDiagnosticSeverity::Warning,
                          std::move(subject),
                          std::move(message));
        }

        void AddError(std::vector<SharedRecipeDiagnostic>& diagnostics,
                      const SharedRecipeDiagnosticCode code,
                      std::string subject,
                      std::string message)
        {
            AddDiagnostic(diagnostics,
                          code,
                          SharedRecipeDiagnosticSeverity::Error,
                          std::move(subject),
                          std::move(message));
        }

        [[nodiscard]] bool IsFiniteVec3(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFiniteBounds(const RHI::GpuBounds& bounds) noexcept
        {
            const glm::vec4 sphere = bounds.WorldSphere;
            return std::isfinite(sphere.x) && std::isfinite(sphere.y) &&
                   std::isfinite(sphere.z) && std::isfinite(sphere.w) &&
                   sphere.w >= 0.0f;
        }

        [[nodiscard]] glm::vec3 BoundsCenter(const RHI::GpuBounds& bounds) noexcept
        {
            return glm::vec3{bounds.WorldSphere.x,
                             bounds.WorldSphere.y,
                             bounds.WorldSphere.z};
        }

        [[nodiscard]] std::uint32_t HashCell(const int x, const int y, const int z) noexcept
        {
            std::uint32_t hash = 2166136261u;
            const auto mix = [&hash](const int value) noexcept {
                hash ^= static_cast<std::uint32_t>(value);
                hash *= 16777619u;
            };
            mix(x);
            mix(y);
            mix(z);
            return hash;
        }

        [[nodiscard]] std::uint32_t ComputeSpatialPartition(
            const glm::vec3 center,
            const float cellSize) noexcept
        {
            const float safeCellSize = cellSize > 0.0f && std::isfinite(cellSize)
                ? cellSize
                : 32.0f;
            return HashCell(static_cast<int>(std::floor(center.x / safeCellSize)),
                            static_cast<int>(std::floor(center.y / safeCellSize)),
                            static_cast<int>(std::floor(center.z / safeCellSize)));
        }

        [[nodiscard]] float ComputeSortDepth(
            const CameraViewSnapshot& camera,
            const glm::vec3 center) noexcept
        {
            const glm::vec3 delta = center - camera.Position;
            if (!camera.Valid || !IsFiniteVec3(camera.Position) || !IsFiniteVec3(camera.Forward))
                return glm::length(delta);
            return glm::dot(delta, camera.Forward);
        }

        [[nodiscard]] std::uint32_t ComputeLodLevel(
            const float sortDepth,
            const VisibilityRecipeOptions& options) noexcept
        {
            if (!std::isfinite(sortDepth))
                return 2u;
            if (sortDepth <= options.NearLodDistance)
                return 0u;
            if (sortDepth <= options.FarLodDistance)
                return 1u;
            return 2u;
        }

        [[nodiscard]] std::uint32_t DomainIndex(
            const VisibilityRecipeDomain domain) noexcept
        {
            return static_cast<std::uint32_t>(domain);
        }

        [[nodiscard]] std::uint32_t GroupKeyFor(
            const RenderableSnapshot& renderable,
            const VisibilityRecipeDomain domain) noexcept
        {
            const std::uint32_t materialKey =
                renderable.HasMaterialSlot ? renderable.MaterialSlot : 0u;
            return (DomainIndex(domain) << 24u) ^ materialKey;
        }

        void AppendVisibleItem(VisibilityRecipeExecutionResult& result,
                               const RenderableSnapshot& renderable,
                               const VisibilityRecipeDomain domain,
                               const VisibilityRecipeOptions& options,
                               const float sortDepth,
                               const std::uint32_t spatialPartition)
        {
            const std::uint32_t groupKey = GroupKeyFor(renderable, domain);
            const std::uint32_t instanceGroup =
                (renderable.Instance.Index << 8u) ^ renderable.Instance.Generation;
            result.VisibleItems.push_back(VisibilityRecipeVisibleItem{
                .StableId = renderable.StableId,
                .Domain = domain,
                .GroupKey = groupKey,
                .BatchGroup = groupKey,
                .InstanceGroup = instanceGroup,
                .LodLevel = ComputeLodLevel(sortDepth, options),
                .SpatialPartition = spatialPartition,
                .SortDepth = sortDepth,
                .AccelerationStructureRequested =
                    options.RequestAccelerationStructures &&
                    domain == VisibilityRecipeDomain::Surface,
            });
            if (options.RequestAccelerationStructures &&
                domain == VisibilityRecipeDomain::Surface)
            {
                result.AccelerationStructures.push_back(
                    VisibilityRecipeAccelerationStructureRequest{
                        .StableId = renderable.StableId,
                        .Domain = domain,
                        .GeometryIndex = renderable.Geometry.Index,
                        .InstanceIndex = renderable.Instance.Index,
                    });
            }
        }

        void AppendRejected(VisibilityRecipeExecutionResult& result,
                            const RenderableSnapshot& renderable,
                            const SharedRecipeDiagnosticCode code,
                            std::string message)
        {
            result.RejectedItems.push_back(VisibilityRecipeRejectedItem{
                .StableId = renderable.StableId,
                .Reason = code,
                .Message = message,
            });
            AddWarning(result.Diagnostics,
                       code,
                       std::to_string(renderable.StableId),
                       std::move(message));
            result.Degraded = true;
        }

        [[nodiscard]] bool SnapshotIsStale(const SnapshotEnvelope& snapshot) noexcept
        {
            return snapshot.Stale ||
                   snapshot.ValidationState == SnapshotValidationState::Stale;
        }

        [[nodiscard]] bool IsRenderableVisible(const RenderableSnapshot& renderable) noexcept
        {
            return (renderable.RenderFlags & RHI::GpuRender_Visible) != 0u;
        }

        [[nodiscard]] bool HasSurface(const RenderableSnapshot& renderable) noexcept
        {
            return (renderable.RenderFlags & RHI::GpuRender_Surface) != 0u;
        }

        [[nodiscard]] bool HasLine(const RenderableSnapshot& renderable) noexcept
        {
            return (renderable.RenderFlags & RHI::GpuRender_Line) != 0u;
        }

        [[nodiscard]] bool HasPoint(const RenderableSnapshot& renderable) noexcept
        {
            return (renderable.RenderFlags & RHI::GpuRender_Point) != 0u;
        }

        [[nodiscard]] bool CastsShadow(const RenderableSnapshot& renderable) noexcept
        {
            return (renderable.RenderFlags & RHI::GpuRender_CastShadow) != 0u;
        }

        [[nodiscard]] bool IsSelectable(const RenderableSnapshot& renderable) noexcept
        {
            return (renderable.RenderFlags & RHI::GpuRender_Selectable) != 0u;
        }

        [[nodiscard]] bool IsValidLight(const LightSnapshot& light) noexcept
        {
            return IsFiniteVec3(light.Position) && IsFiniteVec3(light.Direction) &&
                   IsFiniteVec3(light.Color) && std::isfinite(light.Range) &&
                   std::isfinite(light.Intensity) &&
                   std::isfinite(light.InnerConeCos) &&
                   std::isfinite(light.OuterConeCos) && light.Intensity >= 0.0f;
        }

        [[nodiscard]] bool IsSupportedLight(const LightSnapshot& light) noexcept
        {
            switch (light.LightType)
            {
            case LightSnapshot::Type::Directional:
                return true;
            case LightSnapshot::Type::Point:
                return light.Range > 0.0f;
            case LightSnapshot::Type::Spot:
                return light.Range > 0.0f &&
                       light.InnerConeCos >= light.OuterConeCos &&
                       light.InnerConeCos <= 1.0f &&
                       light.OuterConeCos >= -1.0f;
            }
            return false;
        }

        [[nodiscard]] LightingRecipeResolvedLightType MapLightType(
            const LightSnapshot& light,
            const bool fallback) noexcept
        {
            if (fallback)
                return LightingRecipeResolvedLightType::FallbackDirectional;
            switch (light.LightType)
            {
            case LightSnapshot::Type::Directional:
                return LightingRecipeResolvedLightType::Directional;
            case LightSnapshot::Type::Point:
                return LightingRecipeResolvedLightType::Point;
            case LightSnapshot::Type::Spot:
                return LightingRecipeResolvedLightType::Spot;
            }
            return LightingRecipeResolvedLightType::Directional;
        }

        [[nodiscard]] LightingRecipeResolvedLight MakeResolvedLight(
            const LightSnapshot& light,
            const bool fallback = false) noexcept
        {
            return LightingRecipeResolvedLight{
                .Type = MapLightType(light, fallback),
                .Position = light.Position,
                .Range = light.Range,
                .Direction = light.Direction,
                .Intensity = light.Intensity,
                .Color = light.Color,
                .InnerConeCos = light.InnerConeCos,
                .OuterConeCos = light.OuterConeCos,
            };
        }

        void AppendIntent(std::vector<LightingRecipeIntent>& intents,
                          const SharedRecipeProductKind product,
                          std::string name,
                          const std::uint32_t count)
        {
            intents.push_back(LightingRecipeIntent{
                .Product = product,
                .Name = std::move(name),
                .Count = count,
            });
        }
    }

    bool SharedRecipeCompatibilityResult::Compatible() const noexcept
    {
        return UnsupportedProducts.empty();
    }

    std::string_view ToString(const SharedRecipeProductKind value) noexcept
    {
        switch (value)
        {
        case SharedRecipeProductKind::VisibleItemSet:
            return "VisibleItemSet";
        case SharedRecipeProductKind::RejectedItemDiagnostics:
            return "RejectedItemDiagnostics";
        case SharedRecipeProductKind::GroupingKeys:
            return "GroupingKeys";
        case SharedRecipeProductKind::BatchGroups:
            return "BatchGroups";
        case SharedRecipeProductKind::InstanceGroups:
            return "InstanceGroups";
        case SharedRecipeProductKind::LodSelections:
            return "LodSelections";
        case SharedRecipeProductKind::SpatialPartitions:
            return "SpatialPartitions";
        case SharedRecipeProductKind::AccelerationStructureBuildRequests:
            return "AccelerationStructureBuildRequests";
        case SharedRecipeProductKind::LightSet:
            return "LightSet";
        case SharedRecipeProductKind::EmissiveGeometry:
            return "EmissiveGeometry";
        case SharedRecipeProductKind::EnvironmentMap:
            return "EnvironmentMap";
        case SharedRecipeProductKind::ProbeSet:
            return "ProbeSet";
        case SharedRecipeProductKind::VolumeSet:
            return "VolumeSet";
        case SharedRecipeProductKind::Tags:
            return "Tags";
        case SharedRecipeProductKind::QualitySettings:
            return "QualitySettings";
        case SharedRecipeProductKind::ShadowIntent:
            return "ShadowIntent";
        case SharedRecipeProductKind::ProbeIntent:
            return "ProbeIntent";
        case SharedRecipeProductKind::GlobalIlluminationIntent:
            return "GlobalIlluminationIntent";
        case SharedRecipeProductKind::DebugMode:
            return "DebugMode";
        case SharedRecipeProductKind::Fallbacks:
            return "Fallbacks";
        }
        return "Unknown";
    }

    std::string_view ToString(const SharedRecipeDiagnosticCode value) noexcept
    {
        switch (value)
        {
        case SharedRecipeDiagnosticCode::None:
            return "None";
        case SharedRecipeDiagnosticCode::EmptyVisibilityInput:
            return "EmptyVisibilityInput";
        case SharedRecipeDiagnosticCode::EmptyLightingInput:
            return "EmptyLightingInput";
        case SharedRecipeDiagnosticCode::InvalidRenderable:
            return "InvalidRenderable";
        case SharedRecipeDiagnosticCode::MissingGeometry:
            return "MissingGeometry";
        case SharedRecipeDiagnosticCode::MissingInstance:
            return "MissingInstance";
        case SharedRecipeDiagnosticCode::NonFiniteBounds:
            return "NonFiniteBounds";
        case SharedRecipeDiagnosticCode::NotVisible:
            return "NotVisible";
        case SharedRecipeDiagnosticCode::UnsupportedRenderDomain:
            return "UnsupportedRenderDomain";
        case SharedRecipeDiagnosticCode::UnsupportedProduct:
            return "UnsupportedProduct";
        case SharedRecipeDiagnosticCode::StaleInput:
            return "StaleInput";
        case SharedRecipeDiagnosticCode::DegradedOutput:
            return "DegradedOutput";
        case SharedRecipeDiagnosticCode::InvalidLight:
            return "InvalidLight";
        case SharedRecipeDiagnosticCode::UnsupportedLight:
            return "UnsupportedLight";
        case SharedRecipeDiagnosticCode::MissingEnvironment:
            return "MissingEnvironment";
        case SharedRecipeDiagnosticCode::FallbackUsed:
            return "FallbackUsed";
        case SharedRecipeDiagnosticCode::MissingRendererCapability:
            return "MissingRendererCapability";
        case SharedRecipeDiagnosticCode::ProductNotProduced:
            return "ProductNotProduced";
        }
        return "Unknown";
    }

    std::string_view ToString(const VisibilityRecipeDomain value) noexcept
    {
        switch (value)
        {
        case VisibilityRecipeDomain::Surface:
            return "Surface";
        case VisibilityRecipeDomain::Line:
            return "Line";
        case VisibilityRecipeDomain::Point:
            return "Point";
        case VisibilityRecipeDomain::Shadow:
            return "Shadow";
        case VisibilityRecipeDomain::Selection:
            return "Selection";
        }
        return "Unknown";
    }

    std::string_view ToString(const LightingRecipeResolvedLightType value) noexcept
    {
        switch (value)
        {
        case LightingRecipeResolvedLightType::Directional:
            return "Directional";
        case LightingRecipeResolvedLightType::Point:
            return "Point";
        case LightingRecipeResolvedLightType::Spot:
            return "Spot";
        case LightingRecipeResolvedLightType::FallbackDirectional:
            return "FallbackDirectional";
        }
        return "Unknown";
    }

    bool HasDiagnostic(const std::span<const SharedRecipeDiagnostic> diagnostics,
                       const SharedRecipeDiagnosticCode code) noexcept
    {
        return std::any_of(diagnostics.begin(),
                           diagnostics.end(),
                           [code](const SharedRecipeDiagnostic& diagnostic) {
                               return diagnostic.Code == code;
                           });
    }

    std::uint32_t CountDiagnostics(
        const std::span<const SharedRecipeDiagnostic> diagnostics,
        const SharedRecipeDiagnosticCode code) noexcept
    {
        return static_cast<std::uint32_t>(
            std::count_if(diagnostics.begin(),
                          diagnostics.end(),
                          [code](const SharedRecipeDiagnostic& diagnostic) {
                              return diagnostic.Code == code;
                          }));
    }

    VisibilityRecipeExecutionResult ExecuteVisibilityRecipe(
        const RenderWorld& world,
        const SnapshotEnvelope& snapshot,
        const VisibilityRecipeOptions& options)
    {
        VisibilityRecipeExecutionResult result{};
        result.Products = {
            SharedRecipeProductKind::VisibleItemSet,
            SharedRecipeProductKind::RejectedItemDiagnostics,
            SharedRecipeProductKind::GroupingKeys,
            SharedRecipeProductKind::BatchGroups,
            SharedRecipeProductKind::InstanceGroups,
            SharedRecipeProductKind::LodSelections,
            SharedRecipeProductKind::SpatialPartitions,
        };
        if (options.RequestAccelerationStructures)
        {
            result.Products.push_back(
                SharedRecipeProductKind::AccelerationStructureBuildRequests);
        }

        if (SnapshotIsStale(snapshot))
        {
            result.StaleInput = true;
            result.Degraded = true;
            AddWarning(result.Diagnostics,
                       SharedRecipeDiagnosticCode::StaleInput,
                       snapshot.Id,
                       "visibility recipe consumed a stale snapshot");
        }
        if (world.Renderables.empty())
        {
            AddWarning(result.Diagnostics,
                       SharedRecipeDiagnosticCode::EmptyVisibilityInput,
                       snapshot.Id,
                       "visibility recipe received no renderables");
            return result;
        }

        for (const RenderableSnapshot& renderable : world.Renderables)
        {
            if (renderable.StableId == 0u)
            {
                AppendRejected(result,
                               renderable,
                               SharedRecipeDiagnosticCode::InvalidRenderable,
                               "renderable has no stable id");
                continue;
            }
            if (!renderable.Instance.IsValid())
            {
                AppendRejected(result,
                               renderable,
                               SharedRecipeDiagnosticCode::MissingInstance,
                               "renderable has no valid GPU instance handle");
                continue;
            }
            if (!renderable.Geometry.IsValid())
            {
                AppendRejected(result,
                               renderable,
                               SharedRecipeDiagnosticCode::MissingGeometry,
                               "renderable has no valid geometry handle");
                continue;
            }
            if (!IsFiniteBounds(renderable.Bounds))
            {
                AppendRejected(result,
                               renderable,
                               SharedRecipeDiagnosticCode::NonFiniteBounds,
                               "renderable bounds are non-finite or negative-radius");
                continue;
            }
            if (!IsRenderableVisible(renderable))
            {
                AppendRejected(result,
                               renderable,
                               SharedRecipeDiagnosticCode::NotVisible,
                               "renderable is not visible in this snapshot");
                continue;
            }

            const glm::vec3 center = BoundsCenter(renderable.Bounds);
            const float sortDepth = ComputeSortDepth(world.Camera, center);
            const std::uint32_t partition =
                ComputeSpatialPartition(center, options.SpatialPartitionCellSize);
            bool emitted = false;

            if (options.IncludeSurface && HasSurface(renderable))
            {
                AppendVisibleItem(result,
                                  renderable,
                                  VisibilityRecipeDomain::Surface,
                                  options,
                                  sortDepth,
                                  partition);
                ++result.SurfaceItemCount;
                emitted = true;
            }
            if (options.IncludeLines && HasLine(renderable))
            {
                AppendVisibleItem(result,
                                  renderable,
                                  VisibilityRecipeDomain::Line,
                                  options,
                                  sortDepth,
                                  partition);
                ++result.LineItemCount;
                emitted = true;
            }
            if (options.IncludePoints && HasPoint(renderable))
            {
                AppendVisibleItem(result,
                                  renderable,
                                  VisibilityRecipeDomain::Point,
                                  options,
                                  sortDepth,
                                  partition);
                ++result.PointItemCount;
                emitted = true;
            }
            if (options.IncludeShadowCasters && CastsShadow(renderable))
            {
                AppendVisibleItem(result,
                                  renderable,
                                  VisibilityRecipeDomain::Shadow,
                                  options,
                                  sortDepth,
                                  partition);
                ++result.ShadowItemCount;
                emitted = true;
            }
            if (options.IncludeSelectionCandidates && IsSelectable(renderable))
            {
                AppendVisibleItem(result,
                                  renderable,
                                  VisibilityRecipeDomain::Selection,
                                  options,
                                  sortDepth,
                                  partition);
                ++result.SelectionItemCount;
                emitted = true;
            }

            if (!emitted)
            {
                AppendRejected(result,
                               renderable,
                               SharedRecipeDiagnosticCode::UnsupportedRenderDomain,
                               "renderable did not match an enabled visibility domain");
            }
        }

        if (result.Degraded)
        {
            AddWarning(result.Diagnostics,
                       SharedRecipeDiagnosticCode::DegradedOutput,
                       snapshot.Id,
                       "visibility recipe emitted degraded products");
        }
        return result;
    }

    LightingRecipeExecutionResult ExecuteLightingRecipe(
        const RenderWorld& world,
        const SnapshotEnvelope& snapshot,
        const LightingRecipeOptions& options)
    {
        LightingRecipeExecutionResult result{};
        result.Products = {
            SharedRecipeProductKind::LightSet,
            SharedRecipeProductKind::EmissiveGeometry,
            SharedRecipeProductKind::EnvironmentMap,
            SharedRecipeProductKind::ProbeSet,
            SharedRecipeProductKind::VolumeSet,
            SharedRecipeProductKind::Tags,
            SharedRecipeProductKind::QualitySettings,
            SharedRecipeProductKind::Fallbacks,
        };

        if (options.RequestShadowIntents)
            result.Products.push_back(SharedRecipeProductKind::ShadowIntent);
        if (options.RequestProbeIntents)
            result.Products.push_back(SharedRecipeProductKind::ProbeIntent);
        if (options.RequestGlobalIlluminationIntent)
            result.Products.push_back(SharedRecipeProductKind::GlobalIlluminationIntent);
        if (options.DebugMode)
            result.Products.push_back(SharedRecipeProductKind::DebugMode);

        if (SnapshotIsStale(snapshot))
        {
            result.StaleInput = true;
            result.Degraded = true;
            AddWarning(result.Diagnostics,
                       SharedRecipeDiagnosticCode::StaleInput,
                       snapshot.Id,
                       "lighting recipe consumed a stale snapshot");
        }

        const std::uint32_t lightLimit = options.MaxLights > 0u
            ? options.MaxLights
            : 1u;
        for (const LightSnapshot& light : world.Lights)
        {
            if (result.Lights.size() >= lightLimit)
            {
                ++result.RejectedLightCount;
                result.Degraded = true;
                AddWarning(result.Diagnostics,
                           SharedRecipeDiagnosticCode::UnsupportedLight,
                           "lights",
                           "lighting recipe light limit was reached");
                continue;
            }
            if (!IsValidLight(light))
            {
                ++result.RejectedLightCount;
                result.Degraded = true;
                AddWarning(result.Diagnostics,
                           SharedRecipeDiagnosticCode::InvalidLight,
                           "lights",
                           "light snapshot contains non-finite or negative values");
                continue;
            }
            if (!IsSupportedLight(light))
            {
                ++result.RejectedLightCount;
                result.Degraded = true;
                AddWarning(result.Diagnostics,
                           SharedRecipeDiagnosticCode::UnsupportedLight,
                           "lights",
                           "light snapshot is outside the shared recipe support contract");
                continue;
            }

            result.Lights.push_back(MakeResolvedLight(light));
            switch (light.LightType)
            {
            case LightSnapshot::Type::Directional:
                ++result.DirectionalLightCount;
                break;
            case LightSnapshot::Type::Point:
                ++result.PointLightCount;
                break;
            case LightSnapshot::Type::Spot:
                ++result.SpotLightCount;
                break;
            }
        }

        if (result.Lights.empty())
        {
            AddWarning(result.Diagnostics,
                       SharedRecipeDiagnosticCode::EmptyLightingInput,
                       snapshot.Id,
                       "lighting recipe received no usable authored lights");
            if (options.EnableFallbackDirectional)
            {
                result.Lights.push_back(MakeResolvedLight(options.FallbackDirectional, true));
                result.UsedFallbackDirectional = true;
                result.Degraded = true;
                AddWarning(result.Diagnostics,
                           SharedRecipeDiagnosticCode::FallbackUsed,
                           "directional-light",
                           "lighting recipe substituted the fallback directional light");
            }
        }

        result.Environment = EnvironmentRecipeProduct{
            .HasEnvironmentMap = options.HasEnvironmentMap,
            .EnvironmentMapId = options.EnvironmentMapId,
            .UsedFallbackEnvironment = !options.HasEnvironmentMap,
            .ProbeIds = options.ProbeIds,
            .VolumeIds = options.VolumeIds,
            .Tags = options.Tags,
            .QualityPreset = options.QualityPreset.empty() ? "balanced" : options.QualityPreset,
            .DebugMode = options.DebugMode,
        };
        if (!options.HasEnvironmentMap)
        {
            result.Degraded = true;
            AddWarning(result.Diagnostics,
                       SharedRecipeDiagnosticCode::MissingEnvironment,
                       "environment",
                       "lighting recipe used the default environment fallback");
        }

        for (const std::uint32_t stableId : options.EmissiveGeometryStableIds)
        {
            const bool found = std::any_of(world.Renderables.begin(),
                                           world.Renderables.end(),
                                           [stableId](const RenderableSnapshot& renderable) {
                                               return renderable.StableId == stableId;
                                           });
            if (found)
            {
                result.EmissiveGeometryStableIds.push_back(stableId);
            }
            else
            {
                result.Degraded = true;
                AddWarning(result.Diagnostics,
                           SharedRecipeDiagnosticCode::InvalidRenderable,
                           std::to_string(stableId),
                           "emissive geometry id was not present in the snapshot");
            }
        }

        if (options.RequestShadowIntents)
        {
            AppendIntent(result.Intents,
                         SharedRecipeProductKind::ShadowIntent,
                         "shadow-map-candidates",
                         result.DirectionalLightCount + result.SpotLightCount);
        }
        if (options.RequestProbeIntents)
        {
            AppendIntent(result.Intents,
                         SharedRecipeProductKind::ProbeIntent,
                         "environment-probes",
                         static_cast<std::uint32_t>(options.ProbeIds.size()));
        }
        if (options.RequestGlobalIlluminationIntent)
        {
            AppendIntent(result.Intents,
                         SharedRecipeProductKind::GlobalIlluminationIntent,
                         "gi-volumes",
                         static_cast<std::uint32_t>(options.VolumeIds.size()));
        }

        if (result.Degraded)
        {
            AddWarning(result.Diagnostics,
                       SharedRecipeDiagnosticCode::DegradedOutput,
                       snapshot.Id,
                       "lighting recipe emitted degraded products");
        }
        return result;
    }

    SharedRecipeCompatibilityResult CheckSharedRecipeCompatibility(
        const SharedRecipeRendererProductDeclaration& declaration,
        const std::span<const SharedRecipeProductKind> producedProducts)
    {
        SharedRecipeCompatibilityResult result{};
        for (const SharedRecipeProductKind product : declaration.ConsumedProducts)
        {
            bool supported = true;
            if (IsVisibilityProduct(product) &&
                !ContainsCapability(declaration.Renderer, RendererCapability::VisibilityRecipe))
            {
                supported = false;
                AddError(result.Diagnostics,
                         SharedRecipeDiagnosticCode::MissingRendererCapability,
                         std::string{ToString(product)},
                         "renderer consumes a visibility product without VisibilityRecipe capability");
            }
            if (IsLightingProduct(product) &&
                !ContainsCapability(declaration.Renderer, RendererCapability::LightingRecipe))
            {
                supported = false;
                AddError(result.Diagnostics,
                         SharedRecipeDiagnosticCode::MissingRendererCapability,
                         std::string{ToString(product)},
                         "renderer consumes a lighting/environment product without LightingRecipe capability");
            }
            if (!producedProducts.empty() && !ContainsProduct(producedProducts, product))
            {
                supported = false;
                AddError(result.Diagnostics,
                         SharedRecipeDiagnosticCode::ProductNotProduced,
                         std::string{ToString(product)},
                         "renderer consumes a product not produced by this recipe execution");
            }

            if (supported)
                result.SupportedProducts.push_back(product);
            else
                result.UnsupportedProducts.push_back(product);
        }

        return result;
    }
}
