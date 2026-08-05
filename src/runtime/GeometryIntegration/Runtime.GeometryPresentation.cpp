module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.GeometryPresentation;

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.GeometryAvailability;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;

        template <typename EnumT, std::size_t N>
        [[nodiscard]] std::string_view EnumToString(
            const EnumT value,
            const std::pair<EnumT, std::string_view> (&table)[N],
            const std::string_view fallback) noexcept
        {
            for (const auto& [candidate, name] : table)
            {
                if (candidate == value)
                    return name;
            }
            return fallback;
        }

        template <typename EnumT, std::size_t N>
        [[nodiscard]] bool TryEnumFromString(
            const std::string_view value,
            const std::pair<EnumT, std::string_view> (&table)[N],
            EnumT& out) noexcept
        {
            for (const auto& [candidate, name] : table)
            {
                if (name == value)
                {
                    out = candidate;
                    return true;
                }
            }
            return false;
        }

        constexpr std::pair<GeometryPresentationShape, std::string_view>
            kShapes[]{
                {GeometryPresentationShape::Unknown, "Unknown"},
                {GeometryPresentationShape::Composition, "Composition"},
                {GeometryPresentationShape::Mesh, "Mesh"},
                {GeometryPresentationShape::Graph, "Graph"},
                {GeometryPresentationShape::PointCloud, "PointCloud"},
                {GeometryPresentationShape::Procedural, "Procedural"},
            };

        constexpr std::pair<GeometryPresentationKind, std::string_view>
            kKinds[]{
                {GeometryPresentationKind::SurfaceMaterial,
                 "SurfaceMaterial"},
                {GeometryPresentationKind::PointPresentation,
                 "PointPresentation"},
                {GeometryPresentationKind::LinePresentation,
                 "LinePresentation"},
            };

        constexpr std::pair<GeometryPresentationSlotSemantic,
                            std::string_view>
            kSemantics[]{
                {GeometryPresentationSlotSemantic::Albedo, "Albedo"},
                {GeometryPresentationSlotSemantic::Normal, "Normal"},
                {GeometryPresentationSlotSemantic::Roughness, "Roughness"},
                {GeometryPresentationSlotSemantic::Metallic, "Metallic"},
                {GeometryPresentationSlotSemantic::ScalarField,
                 "ScalarField"},
                {GeometryPresentationSlotSemantic::Displacement,
                 "Displacement"},
                {GeometryPresentationSlotSemantic::PointColor, "PointColor"},
                {GeometryPresentationSlotSemantic::PointScalarField,
                 "PointScalarField"},
                {GeometryPresentationSlotSemantic::PointSize, "PointSize"},
                {GeometryPresentationSlotSemantic::PointNormalOrientation,
                 "PointNormalOrientation"},
                {GeometryPresentationSlotSemantic::LineColor, "LineColor"},
                {GeometryPresentationSlotSemantic::LineScalarField,
                 "LineScalarField"},
                {GeometryPresentationSlotSemantic::LineWidth, "LineWidth"},
            };

        constexpr std::pair<GeometryPresentationSourceKind, std::string_view>
            kSourceKinds[]{
                {GeometryPresentationSourceKind::UniformDefault,
                 "UniformDefault"},
                {GeometryPresentationSourceKind::AuthoredTextureAsset,
                 "AuthoredTextureAsset"},
                {GeometryPresentationSourceKind::GeneratedTextureAsset,
                 "GeneratedTextureAsset"},
                {GeometryPresentationSourceKind::PropertyBake,
                 "PropertyBake"},
                {GeometryPresentationSourceKind::PropertyBuffer,
                 "PropertyBuffer"},
            };

        constexpr std::pair<GeometryPresentationReadiness, std::string_view>
            kReadiness[]{
                {GeometryPresentationReadiness::Unset, "Unset"},
                {GeometryPresentationReadiness::DefaultValue,
                 "DefaultValue"},
                {GeometryPresentationReadiness::Pending, "Pending"},
                {GeometryPresentationReadiness::Ready, "Ready"},
                {GeometryPresentationReadiness::Failed, "Failed"},
                {GeometryPresentationReadiness::Unsupported, "Unsupported"},
                {GeometryPresentationReadiness::Stale, "Stale"},
            };

        constexpr std::pair<GeometryPresentationFallback, std::string_view>
            kFallbacks[]{
                {GeometryPresentationFallback::None, "None"},
                {GeometryPresentationFallback::UniformDefault,
                 "UniformDefault"},
                {GeometryPresentationFallback::PreviousGeneratedTexture,
                 "PreviousGeneratedTexture"},
            };

        constexpr std::pair<GeometryGeneratedOutputPolicy, std::string_view>
            kGeneratedPolicies[]{
                {GeometryGeneratedOutputPolicy::SessionCache,
                 "SessionCache"},
                {GeometryGeneratedOutputPolicy::DeterministicChildAsset,
                 "DeterministicChildAsset"},
                {GeometryGeneratedOutputPolicy::PersistOnSave,
                 "PersistOnSave"},
            };

        constexpr std::pair<GeometryPresentationNormalSpace, std::string_view>
            kNormalSpaces[]{
                {GeometryPresentationNormalSpace::Object, "Object"},
                {GeometryPresentationNormalSpace::World, "World"},
            };

        constexpr std::pair<GeometryPresentationProvenance, std::string_view>
            kProvenance[]{
                {GeometryPresentationProvenance::None, "None"},
                {GeometryPresentationProvenance::AuthoredAsset,
                 "AuthoredAsset"},
                {GeometryPresentationProvenance::GeneratedTextureAsset,
                 "GeneratedTextureAsset"},
                {GeometryPresentationProvenance::PropertyBuffer,
                 "PropertyBuffer"},
                {GeometryPresentationProvenance::UniformDefault,
                 "UniformDefault"},
                {GeometryPresentationProvenance::PropertyBinding,
                 "PropertyBinding"},
            };

        void AppendDiagnostic(
            GeometryPresentationSlotSnapshot& slot,
            std::string diagnostic)
        {
            if (diagnostic.empty())
                return;
            if (!slot.Diagnostic.empty())
                slot.Diagnostic += "; ";
            slot.Diagnostic += std::move(diagnostic);
        }

        void UseUniformFallback(GeometryPresentationSlotSnapshot& slot) noexcept
        {
            slot.UsesUniformDefault = true;
            slot.Fallback = GeometryPresentationFallback::UniformDefault;
        }

        void CountSlot(GeometryPresentationSnapshotStats& stats,
                       const GeometryPresentationSlotSnapshot& slot) noexcept
        {
            ++stats.SlotCount;
            if (slot.UsesUniformDefault)
                ++stats.DefaultSlotCount;
            if (slot.TextureReady)
                ++stats.ReadyTextureSlotCount;
            if (slot.PropertyBufferReady)
                ++stats.PropertyBufferReadyCount;
            if (slot.PreviousOutputRetained)
                ++stats.PreviousOutputRetainedCount;
            if (slot.Unsupported)
                ++stats.UnsupportedSlotCount;
            if (slot.Readiness == GeometryPresentationReadiness::Pending ||
                slot.Readiness == GeometryPresentationReadiness::Stale)
            {
                ++stats.PendingSlotCount;
            }
            if (slot.Readiness == GeometryPresentationReadiness::Failed)
                ++stats.FailedSlotCount;
            if (!slot.Diagnostic.empty())
                ++stats.DiagnosticCount;
        }

        [[nodiscard]] GeometryPresentationProvenance DefaultProvenance(
            const GeometryPresentationSourceKind sourceKind) noexcept
        {
            switch (sourceKind)
            {
            case GeometryPresentationSourceKind::UniformDefault:
                return GeometryPresentationProvenance::UniformDefault;
            case GeometryPresentationSourceKind::AuthoredTextureAsset:
                return GeometryPresentationProvenance::AuthoredAsset;
            case GeometryPresentationSourceKind::GeneratedTextureAsset:
                return GeometryPresentationProvenance::GeneratedTextureAsset;
            case GeometryPresentationSourceKind::PropertyBake:
                return GeometryPresentationProvenance::PropertyBinding;
            case GeometryPresentationSourceKind::PropertyBuffer:
                return GeometryPresentationProvenance::PropertyBuffer;
            }
            return GeometryPresentationProvenance::None;
        }

        void ResolveGeneratedTexture(
            GeometryPresentationSlotSnapshot& slot,
            const Assets::AssetId texture,
            const bool hasExplicitStatus)
        {
            if (texture.IsValid())
            {
                slot.TextureAsset = texture;
                slot.TextureReady = true;
                if (!hasExplicitStatus ||
                    slot.Readiness == GeometryPresentationReadiness::Ready)
                {
                    slot.Readiness = GeometryPresentationReadiness::Ready;
                    return;
                }

                slot.PreviousOutputRetained = true;
                slot.Fallback =
                    GeometryPresentationFallback::PreviousGeneratedTexture;
                AppendDiagnostic(slot,
                                 "previous generated texture retained");
                return;
            }

            UseUniformFallback(slot);
            if (slot.Readiness == GeometryPresentationReadiness::Unset)
            {
                slot.Readiness = GeometryPresentationReadiness::Pending;
            }
            AppendDiagnostic(slot, "generated texture is not ready");
        }
    }

    std::string_view ToString(
        const GeometryPresentationShape value) noexcept
    {
        return EnumToString(value, kShapes, "Unknown");
    }

    std::string_view ToString(
        const GeometryPresentationKind value) noexcept
    {
        return EnumToString(value, kKinds, "SurfaceMaterial");
    }

    std::string_view ToString(
        const GeometryPresentationSlotSemantic value) noexcept
    {
        return EnumToString(value, kSemantics, "Albedo");
    }

    std::string_view ToString(
        const GeometryPresentationSourceKind value) noexcept
    {
        return EnumToString(value, kSourceKinds, "UniformDefault");
    }

    std::string_view ToString(
        const GeometryPresentationReadiness value) noexcept
    {
        return EnumToString(value, kReadiness, "Unset");
    }

    std::string_view ToString(
        const GeometryPresentationFallback value) noexcept
    {
        return EnumToString(value, kFallbacks, "None");
    }

    std::string_view ToString(
        const GeometryGeneratedOutputPolicy value) noexcept
    {
        return EnumToString(value, kGeneratedPolicies, "SessionCache");
    }

    std::string_view ToString(
        const GeometryPresentationNormalSpace value) noexcept
    {
        return EnumToString(value, kNormalSpaces, "Object");
    }

    std::string_view ToString(
        const GeometryPresentationProvenance value) noexcept
    {
        return EnumToString(value, kProvenance, "None");
    }

    bool TryParseGeometryPresentationShape(
        const std::string_view value,
        GeometryPresentationShape& out) noexcept
    {
        if (TryEnumFromString(value, kShapes, out))
            return true;

        // Legacy scene spellings remain read-compatible; new documents emit
        // the neutral names above.
        if (value == "MeshLeaf")
        {
            out = GeometryPresentationShape::Mesh;
            return true;
        }
        if (value == "GraphLeaf")
        {
            out = GeometryPresentationShape::Graph;
            return true;
        }
        if (value == "PointCloudLeaf")
        {
            out = GeometryPresentationShape::PointCloud;
            return true;
        }
        return false;
    }

    bool TryParseGeometryRenderLane(
        const std::string_view value,
        GeometryRenderLane& out) noexcept
    {
        if (value == "Surface")
        {
            out = GeometryRenderLane::Surface;
            return true;
        }
        if (value == "Edges")
        {
            out = GeometryRenderLane::Edges;
            return true;
        }
        if (value == "Points")
        {
            out = GeometryRenderLane::Points;
            return true;
        }
        return false;
    }

    bool TryParseGeometryPresentationKind(
        const std::string_view value,
        GeometryPresentationKind& out) noexcept
    {
        return TryEnumFromString(value, kKinds, out);
    }

    bool TryParseGeometryPresentationSlotSemantic(
        const std::string_view value,
        GeometryPresentationSlotSemantic& out) noexcept
    {
        return TryEnumFromString(value, kSemantics, out);
    }

    bool TryParseGeometryPresentationSourceKind(
        const std::string_view value,
        GeometryPresentationSourceKind& out) noexcept
    {
        return TryEnumFromString(value, kSourceKinds, out);
    }

    bool TryParseGeometryGeneratedOutputPolicy(
        const std::string_view value,
        GeometryGeneratedOutputPolicy& out) noexcept
    {
        return TryEnumFromString(value, kGeneratedPolicies, out);
    }

    bool TryParseGeometryPresentationNormalSpace(
        const std::string_view value,
        GeometryPresentationNormalSpace& out) noexcept
    {
        return TryEnumFromString(value, kNormalSpaces, out);
    }

    GeometryGeneratedOutputPolicy DefaultGeometryGeneratedOutputPolicyFor(
        const GeometryPresentationSourceKind sourceKind) noexcept
    {
        switch (sourceKind)
        {
        case GeometryPresentationSourceKind::GeneratedTextureAsset:
        case GeometryPresentationSourceKind::PropertyBake:
            return GeometryGeneratedOutputPolicy::DeterministicChildAsset;
        case GeometryPresentationSourceKind::UniformDefault:
        case GeometryPresentationSourceKind::AuthoredTextureAsset:
        case GeometryPresentationSourceKind::PropertyBuffer:
            return GeometryGeneratedOutputPolicy::SessionCache;
        }
        return GeometryGeneratedOutputPolicy::SessionCache;
    }

    std::vector<GeometryPresentationPropertyOption>
    EnumerateGeometryPresentationPropertyOptions(
        const GS::ConstSourceView& source,
        const GeometryElementDomain domain,
        const GeometryPropertyValueKindFilter expectedValueKind,
        const std::uint64_t observedSourceGeneration)
    {
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(source);
        const GeometryPropertyCatalogSnapshot catalog =
            BuildGeometryPropertyCatalogSnapshot(
                availability,
                0u,
                observedSourceGeneration);

        std::vector<GeometryPresentationPropertyOption> options{};
        for (const GeometryPropertyCatalogEntry& entry : catalog.Entries)
        {
            if (entry.Ref.Domain != domain)
                continue;

            const bool compatible = MatchesGeometryPropertyValueKind(
                expectedValueKind,
                entry.Ref.ValueKind);
            options.push_back(GeometryPresentationPropertyOption{
                .Property = entry.Ref,
                .ElementCount = entry.ElementCount,
                .SourceGeneration = entry.PropertyGeneration,
                .Compatible = compatible,
                .DisabledReason = compatible
                    ? std::string{}
                    : std::string{"property value kind is incompatible"},
            });
        }

        std::stable_sort(
            options.begin(),
            options.end(),
            [](const GeometryPresentationPropertyOption& lhs,
               const GeometryPresentationPropertyOption& rhs) noexcept
            {
                if (lhs.Compatible != rhs.Compatible)
                    return lhs.Compatible;
                return lhs.Property.Name < rhs.Property.Name;
            });
        return options;
    }

    GeometryPresentationLaneRecipe* FindGeometryPresentationLane(
        GeometryPresentationRecipe& recipe,
        const GeometryRenderLane lane) noexcept
    {
        for (GeometryPresentationLaneRecipe& candidate : recipe.Lanes)
        {
            if (candidate.Lane == lane)
                return &candidate;
        }
        return nullptr;
    }

    const GeometryPresentationLaneRecipe* FindGeometryPresentationLane(
        const GeometryPresentationRecipe& recipe,
        const GeometryRenderLane lane) noexcept
    {
        for (const GeometryPresentationLaneRecipe& candidate : recipe.Lanes)
        {
            if (candidate.Lane == lane)
                return &candidate;
        }
        return nullptr;
    }

    GeometryPresentationBindingRecipe* FindGeometryPresentationBinding(
        GeometryPresentationRecipe& recipe,
        const std::string_view key) noexcept
    {
        for (GeometryPresentationBindingRecipe& candidate :
             recipe.Presentations)
        {
            if (candidate.Key == key)
                return &candidate;
        }
        return nullptr;
    }

    const GeometryPresentationBindingRecipe* FindGeometryPresentationBinding(
        const GeometryPresentationRecipe& recipe,
        const std::string_view key) noexcept
    {
        for (const GeometryPresentationBindingRecipe& candidate :
             recipe.Presentations)
        {
            if (candidate.Key == key)
                return &candidate;
        }
        return nullptr;
    }

    GeometryPresentationSlotRecipe* FindGeometryPresentationSlot(
        GeometryPresentationBindingRecipe& presentation,
        const GeometryPresentationSlotSemantic semantic) noexcept
    {
        for (GeometryPresentationSlotRecipe& candidate : presentation.Slots)
        {
            if (candidate.Semantic == semantic)
                return &candidate;
        }
        return nullptr;
    }

    const GeometryPresentationSlotRecipe* FindGeometryPresentationSlot(
        const GeometryPresentationBindingRecipe& presentation,
        const GeometryPresentationSlotSemantic semantic) noexcept
    {
        for (const GeometryPresentationSlotRecipe& candidate :
             presentation.Slots)
        {
            if (candidate.Semantic == semantic)
                return &candidate;
        }
        return nullptr;
    }

    GeometryPresentationSlotStatus* FindGeometryPresentationSlotStatus(
        GeometryPresentationRuntimeState& state,
        const std::string_view presentationKey,
        const GeometryPresentationSlotSemantic semantic) noexcept
    {
        for (GeometryPresentationSlotStatus& candidate : state.Slots)
        {
            if (candidate.PresentationKey == presentationKey &&
                candidate.Semantic == semantic)
            {
                return &candidate;
            }
        }
        return nullptr;
    }

    const GeometryPresentationSlotStatus* FindGeometryPresentationSlotStatus(
        const GeometryPresentationRuntimeState& state,
        const std::string_view presentationKey,
        const GeometryPresentationSlotSemantic semantic) noexcept
    {
        for (const GeometryPresentationSlotStatus& candidate : state.Slots)
        {
            if (candidate.PresentationKey == presentationKey &&
                candidate.Semantic == semantic)
            {
                return &candidate;
            }
        }
        return nullptr;
    }

    bool IsSurfaceTextureSemantic(
        const GeometryPresentationSlotSemantic semantic) noexcept
    {
        switch (semantic)
        {
        case GeometryPresentationSlotSemantic::Albedo:
        case GeometryPresentationSlotSemantic::Normal:
        case GeometryPresentationSlotSemantic::Roughness:
        case GeometryPresentationSlotSemantic::Metallic:
        case GeometryPresentationSlotSemantic::ScalarField:
        case GeometryPresentationSlotSemantic::Displacement:
            return true;
        case GeometryPresentationSlotSemantic::PointColor:
        case GeometryPresentationSlotSemantic::PointScalarField:
        case GeometryPresentationSlotSemantic::PointSize:
        case GeometryPresentationSlotSemantic::PointNormalOrientation:
        case GeometryPresentationSlotSemantic::LineColor:
        case GeometryPresentationSlotSemantic::LineScalarField:
        case GeometryPresentationSlotSemantic::LineWidth:
            return false;
        }
        return false;
    }

    GeometryPresentationSnapshot BuildGeometryPresentationSnapshot(
        const GS::ConstSourceView& source,
        const GeometryPresentationRecipe& recipe,
        const GeometryPresentationRuntimeState& runtimeState,
        const std::uint64_t observedSourceGeneration)
    {
        GeometryPresentationSnapshot snapshot{
            .Shape = recipe.Shape,
            .RecipeGeneration = runtimeState.RecipeGeneration,
            .ObservedSourceGeneration = observedSourceGeneration,
        };
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(source);

        for (const GeometryPresentationLaneRecipe& lane : recipe.Lanes)
        {
            const GeometryPresentationBindingRecipe* presentation =
                FindGeometryPresentationBinding(recipe,
                                                 lane.PresentationKey);
            ++snapshot.Stats.LaneCount;
            if (presentation == nullptr)
            {
                GeometryPresentationSlotSnapshot missing{
                    .Lane = lane.Lane,
                    .PresentationKey = lane.PresentationKey,
                    .Readiness = GeometryPresentationReadiness::Failed,
                    .Diagnostic = "presentation recipe is missing",
                };
                snapshot.Slots.push_back(std::move(missing));
                CountSlot(snapshot.Stats, snapshot.Slots.back());
                continue;
            }

            for (const GeometryPresentationSlotRecipe& sourceSlot :
                 presentation->Slots)
            {
                GeometryPresentationSlotSnapshot slot{
                    .Lane = lane.Lane,
                    .PresentationKey = lane.PresentationKey,
                    .PresentationKind = presentation->Kind,
                    .Semantic = sourceSlot.Semantic,
                    .SourceKind = sourceSlot.SourceKind,
                    .UniformDefault = sourceSlot.UniformDefault,
                    .Property = sourceSlot.Property,
                    .GeneratedOutputName = sourceSlot.GeneratedOutputName,
                    .TextureColormap = sourceSlot.TextureColormap,
                    .NormalSpace = sourceSlot.NormalSpace,
                    .Provenance = DefaultProvenance(sourceSlot.SourceKind),
                    .SourceGeneration = observedSourceGeneration,
                    .Enabled = sourceSlot.Enabled,
                };

                const GeometryPresentationSlotStatus* status =
                    FindGeometryPresentationSlotStatus(
                        runtimeState,
                        lane.PresentationKey,
                        sourceSlot.Semantic);
                const bool hasStatus = status != nullptr;
                if (status != nullptr)
                {
                    slot.Readiness = status->Readiness;
                    slot.Provenance = status->Provenance ==
                            GeometryPresentationProvenance::None
                        ? slot.Provenance
                        : status->Provenance;
                    slot.SourceGeneration = status->SourceGeneration == 0u
                        ? observedSourceGeneration
                        : status->SourceGeneration;
                    slot.OutputGeneration = status->OutputGeneration;
                }

                if (!sourceSlot.Enabled)
                {
                    slot.Readiness = GeometryPresentationReadiness::Unset;
                    slot.Diagnostic = "slot disabled";
                    snapshot.Slots.push_back(std::move(slot));
                    CountSlot(snapshot.Stats, snapshot.Slots.back());
                    continue;
                }

                switch (sourceSlot.SourceKind)
                {
                case GeometryPresentationSourceKind::UniformDefault:
                    slot.UsesUniformDefault = true;
                    slot.Readiness =
                        GeometryPresentationReadiness::DefaultValue;
                    break;

                case GeometryPresentationSourceKind::AuthoredTextureAsset:
                    if (sourceSlot.TextureAsset.IsValid())
                    {
                        slot.TextureAsset = sourceSlot.TextureAsset;
                        slot.TextureReady = true;
                        slot.Readiness = GeometryPresentationReadiness::Ready;
                    }
                    else
                    {
                        UseUniformFallback(slot);
                        slot.Readiness = GeometryPresentationReadiness::Failed;
                        AppendDiagnostic(
                            slot,
                            "authored texture asset is invalid");
                    }
                    break;

                case GeometryPresentationSourceKind::GeneratedTextureAsset:
                    ResolveGeneratedTexture(
                        slot,
                        status != nullptr && status->GeneratedTexture.IsValid()
                            ? status->GeneratedTexture
                            : sourceSlot.TextureAsset,
                        hasStatus);
                    break;

                case GeometryPresentationSourceKind::PropertyBake:
                    slot.PropertyResolution = ResolveGeometryProperty(
                        availability,
                        sourceSlot.Property,
                        std::nullopt,
                        false,
                        observedSourceGeneration);
                    if (!slot.PropertyResolution.Resolved())
                    {
                        slot.Readiness =
                            GeometryPresentationReadiness::Failed;
                        AppendDiagnostic(
                            slot,
                            std::string{ToString(
                                slot.PropertyResolution.Status)});
                    }
                    if (status != nullptr &&
                        status->SourceGeneration != 0u &&
                        observedSourceGeneration != 0u &&
                        status->SourceGeneration != observedSourceGeneration)
                    {
                        slot.Readiness =
                            GeometryPresentationReadiness::Stale;
                        AppendDiagnostic(slot,
                                         "source generation is stale");
                    }
                    ResolveGeneratedTexture(
                        slot,
                        status != nullptr ? status->GeneratedTexture
                                          : Assets::AssetId{},
                        hasStatus);
                    break;

                case GeometryPresentationSourceKind::PropertyBuffer:
                    slot.PropertyResolution = ResolveGeometryProperty(
                        availability,
                        sourceSlot.Property,
                        std::nullopt,
                        false,
                        observedSourceGeneration);
                    if (slot.PropertyResolution.Resolved())
                    {
                        slot.PropertyBufferReady = true;
                        slot.Readiness = GeometryPresentationReadiness::Ready;
                    }
                    else
                    {
                        UseUniformFallback(slot);
                        slot.Readiness =
                            GeometryPresentationReadiness::Failed;
                        AppendDiagnostic(
                            slot,
                            std::string{ToString(
                                slot.PropertyResolution.Status)});
                    }
                    break;
                }

                if (status != nullptr)
                    AppendDiagnostic(slot, status->Diagnostic);

                if (sourceSlot.SourceKind ==
                        GeometryPresentationSourceKind::PropertyBuffer &&
                    presentation->Kind ==
                        GeometryPresentationKind::SurfaceMaterial &&
                    sourceSlot.Semantic !=
                        GeometryPresentationSlotSemantic::ScalarField)
                {
                    slot.Unsupported = true;
                    AppendDiagnostic(
                        slot,
                        "surface property-buffer material path is not backend-resident");
                }

                snapshot.Slots.push_back(std::move(slot));
                CountSlot(snapshot.Stats, snapshot.Slots.back());
            }
        }

        return snapshot;
    }
}
