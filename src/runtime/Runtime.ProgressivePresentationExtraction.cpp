module;

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

module Extrinsic.Runtime.ProgressivePresentationExtraction;

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.ProgressiveRenderData;

namespace Extrinsic::Runtime
{
    namespace
    {
        void AppendDiagnostic(ProgressiveSlotExtraction& slot, std::string diagnostic)
        {
            if (slot.Diagnostic.empty())
            {
                slot.Diagnostic = std::move(diagnostic);
                return;
            }
            slot.Diagnostic += "; ";
            slot.Diagnostic += diagnostic;
        }

        void CountSlot(ProgressivePresentationExtractionStats& stats,
                       const ProgressiveSlotExtraction& slot)
        {
            ++stats.SlotCount;
            if (slot.UsesUniformDefault)
            {
                ++stats.DefaultSlotCount;
            }
            if (slot.TextureReady)
            {
                ++stats.ReadyTextureSlotCount;
            }
            if (slot.PropertyBufferReady)
            {
                ++stats.PropertyBufferReadyCount;
            }
            if (slot.PreviousOutputRetained)
            {
                ++stats.PreviousOutputRetainedCount;
            }
            if (slot.Unsupported)
            {
                ++stats.UnsupportedSlotCount;
            }
            if (slot.Readiness == ProgressiveReadinessState::Pending ||
                slot.Readiness == ProgressiveReadinessState::Stale)
            {
                ++stats.PendingSlotCount;
            }
            if (slot.Readiness == ProgressiveReadinessState::Failed)
            {
                ++stats.FailedSlotCount;
            }
            if (!slot.Diagnostic.empty())
            {
                ++stats.DiagnosticCount;
            }
        }

        [[nodiscard]] bool GeneratedSourceKind(const ProgressiveSlotSourceKind sourceKind) noexcept
        {
            return sourceKind == ProgressiveSlotSourceKind::GeneratedTextureAsset ||
                   sourceKind == ProgressiveSlotSourceKind::PropertyBake;
        }
    }

    bool IsSurfaceTextureSemantic(const ProgressiveSlotSemantic semantic) noexcept
    {
        switch (semantic)
        {
        case ProgressiveSlotSemantic::Albedo:
        case ProgressiveSlotSemantic::Normal:
        case ProgressiveSlotSemantic::Roughness:
        case ProgressiveSlotSemantic::Metallic:
        case ProgressiveSlotSemantic::ScalarField:
        case ProgressiveSlotSemantic::Displacement:
            return true;
        case ProgressiveSlotSemantic::PointColor:
        case ProgressiveSlotSemantic::PointScalarField:
        case ProgressiveSlotSemantic::PointSize:
        case ProgressiveSlotSemantic::PointNormalOrientation:
        case ProgressiveSlotSemantic::LineColor:
        case ProgressiveSlotSemantic::LineScalarField:
        case ProgressiveSlotSemantic::LineWidth:
            return false;
        }
        return false;
    }

    std::optional<Assets::AssetId> ReadyTextureAssetForSlot(
        const ProgressiveSlotBinding& slot) noexcept
    {
        if (!slot.Enabled)
        {
            return std::nullopt;
        }

        if (slot.SourceKind == ProgressiveSlotSourceKind::AuthoredTextureAsset &&
            slot.AuthoredTexture.IsValid())
        {
            return slot.AuthoredTexture;
        }

        if ((slot.SourceKind == ProgressiveSlotSourceKind::GeneratedTextureAsset ||
             slot.SourceKind == ProgressiveSlotSourceKind::PropertyBake) &&
            slot.GeneratedTexture.IsValid() &&
            (slot.Readiness == ProgressiveReadinessState::Ready ||
             slot.Readiness == ProgressiveReadinessState::Pending ||
             slot.Readiness == ProgressiveReadinessState::Failed ||
             slot.Readiness == ProgressiveReadinessState::Stale))
        {
            return slot.GeneratedTexture;
        }

        return std::nullopt;
    }

    ProgressivePresentationExtractionSnapshot BuildProgressivePresentationSnapshot(
        const ECS::Components::GeometrySources::ConstSourceView& source,
        const ProgressivePresentationBindings& bindings,
        const std::uint64_t observedSourceGeneration)
    {
        ProgressivePresentationExtractionSnapshot snapshot{};
        snapshot.Shape = bindings.Shape;
        snapshot.BindingGeneration = bindings.BindingGeneration;

        for (const ProgressiveRenderLaneBinding& lane : bindings.Lanes)
        {
            const ProgressivePresentationBinding* presentation =
                FindPresentationBinding(bindings, lane.PresentationKey);
            if (presentation == nullptr)
            {
                ProgressiveSlotExtraction missing{};
                missing.Lane = lane.Lane;
                missing.PresentationKey = lane.PresentationKey;
                missing.Enabled = false;
                missing.Readiness = ProgressiveReadinessState::Failed;
                missing.Diagnostic = "presentation binding is missing";
                snapshot.Slots.push_back(std::move(missing));
                ++snapshot.Stats.LaneCount;
                CountSlot(snapshot.Stats, snapshot.Slots.back());
                continue;
            }

            ++snapshot.Stats.LaneCount;
            for (const ProgressiveSlotBinding& sourceSlot : presentation->Slots)
            {
                ProgressiveSlotExtraction slot{};
                slot.Lane = lane.Lane;
                slot.PresentationKey = lane.PresentationKey;
                slot.PresentationKind = presentation->Kind;
                slot.Semantic = sourceSlot.Semantic;
                slot.SourceKind = sourceSlot.SourceKind;
                slot.Readiness = sourceSlot.Readiness;
                slot.UniformDefault = sourceSlot.UniformDefault;
                slot.Property = sourceSlot.Property;
                slot.Enabled = sourceSlot.Enabled;

                if (!sourceSlot.Enabled)
                {
                    slot.Readiness = ProgressiveReadinessState::Unset;
                    slot.Diagnostic = "slot disabled";
                    snapshot.Slots.push_back(std::move(slot));
                    CountSlot(snapshot.Stats, snapshot.Slots.back());
                    continue;
                }

                switch (sourceSlot.SourceKind)
                {
                case ProgressiveSlotSourceKind::UniformDefault:
                    slot.UsesUniformDefault = true;
                    slot.Readiness = ProgressiveReadinessState::DefaultValue;
                    break;
                case ProgressiveSlotSourceKind::AuthoredTextureAsset:
                    if (sourceSlot.AuthoredTexture.IsValid())
                    {
                        slot.TextureAsset = sourceSlot.AuthoredTexture;
                        slot.TextureReady = true;
                        slot.Readiness = ProgressiveReadinessState::Ready;
                    }
                    else
                    {
                        slot.UsesUniformDefault = true;
                        slot.Readiness = ProgressiveReadinessState::Failed;
                        AppendDiagnostic(slot, "authored texture asset is invalid");
                    }
                    break;
                case ProgressiveSlotSourceKind::GeneratedTextureAsset:
                case ProgressiveSlotSourceKind::PropertyBake:
                    if (sourceSlot.SourceKind == ProgressiveSlotSourceKind::PropertyBake)
                    {
                        slot.PropertyResolution = ResolvePropertyBinding(
                            source,
                            sourceSlot.Property,
                            observedSourceGeneration);
                        if (!slot.PropertyResolution.Compatible())
                        {
                            AppendDiagnostic(slot, slot.PropertyResolution.Diagnostic);
                        }
                    }

                    if (const auto texture = ReadyTextureAssetForSlot(sourceSlot); texture.has_value())
                    {
                        slot.TextureAsset = *texture;
                        slot.TextureReady = true;
                        slot.PreviousOutputRetained =
                            sourceSlot.Readiness != ProgressiveReadinessState::Ready;
                        if (slot.PreviousOutputRetained)
                        {
                            AppendDiagnostic(slot, "previous generated texture retained");
                        }
                    }
                    else
                    {
                        slot.UsesUniformDefault = true;
                        if (slot.Readiness == ProgressiveReadinessState::Unset)
                        {
                            slot.Readiness = ProgressiveReadinessState::Pending;
                        }
                        AppendDiagnostic(slot, GeneratedSourceKind(sourceSlot.SourceKind)
                            ? "generated texture is not ready"
                            : "slot is not texture backed");
                    }
                    break;
                case ProgressiveSlotSourceKind::PropertyBuffer:
                    slot.PropertyResolution = ResolvePropertyBinding(
                        source,
                        sourceSlot.Property,
                        observedSourceGeneration);
                    if (slot.PropertyResolution.Compatible())
                    {
                        slot.PropertyBufferReady = true;
                        slot.Readiness = ProgressiveReadinessState::Ready;
                    }
                    else
                    {
                        slot.UsesUniformDefault = true;
                        slot.Readiness = ProgressiveReadinessState::Failed;
                        AppendDiagnostic(slot, slot.PropertyResolution.Diagnostic);
                    }
                    break;
                }

                if (sourceSlot.LastDiagnostic.empty() == false)
                {
                    AppendDiagnostic(slot, sourceSlot.LastDiagnostic);
                }

                if (sourceSlot.SourceKind == ProgressiveSlotSourceKind::PropertyBuffer &&
                    presentation->Kind == ProgressivePresentationKind::SurfaceMaterial)
                {
                    slot.Unsupported = true;
                    AppendDiagnostic(slot, "surface property-buffer material path is not backend-resident");
                }

                snapshot.Slots.push_back(std::move(slot));
                CountSlot(snapshot.Stats, snapshot.Slots.back());
            }
        }

        return snapshot;
    }
}
