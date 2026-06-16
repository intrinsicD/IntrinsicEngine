module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.ProgressivePresentationExtraction;

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.ProgressiveRenderData;

export namespace Extrinsic::Runtime
{
    struct ProgressiveSlotExtraction
    {
        ProgressiveRenderLane Lane{ProgressiveRenderLane::Surface};
        std::string PresentationKey{};
        ProgressivePresentationKind PresentationKind{ProgressivePresentationKind::SurfaceMaterial};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveSlotSourceKind SourceKind{ProgressiveSlotSourceKind::UniformDefault};
        ProgressiveReadinessState Readiness{ProgressiveReadinessState::Unset};
        ProgressiveDefaultValue UniformDefault{};
        ProgressivePropertyBindingDescriptor Property{};
        ProgressivePropertyResolution PropertyResolution{};
        Assets::AssetId TextureAsset{};
        bool Enabled{false};
        bool UsesUniformDefault{false};
        bool TextureReady{false};
        bool PropertyBufferReady{false};
        bool PreviousOutputRetained{false};
        bool Unsupported{false};
        std::string Diagnostic{};
    };

    struct ProgressivePresentationExtractionStats
    {
        std::uint32_t LaneCount{0u};
        std::uint32_t SlotCount{0u};
        std::uint32_t DefaultSlotCount{0u};
        std::uint32_t ReadyTextureSlotCount{0u};
        std::uint32_t PendingSlotCount{0u};
        std::uint32_t FailedSlotCount{0u};
        std::uint32_t UnsupportedSlotCount{0u};
        std::uint32_t PropertyBufferReadyCount{0u};
        std::uint32_t PreviousOutputRetainedCount{0u};
        std::uint32_t DiagnosticCount{0u};
    };

    struct ProgressivePresentationExtractionSnapshot
    {
        ProgressiveEntityShape Shape{ProgressiveEntityShape::Unknown};
        std::uint64_t BindingGeneration{0u};
        std::vector<ProgressiveSlotExtraction> Slots{};
        ProgressivePresentationExtractionStats Stats{};
    };

    [[nodiscard]] bool IsSurfaceTextureSemantic(ProgressiveSlotSemantic semantic) noexcept;
    [[nodiscard]] std::optional<Assets::AssetId> ReadyTextureAssetForSlot(
        const ProgressiveSlotBinding& slot) noexcept;

    [[nodiscard]] ProgressivePresentationExtractionSnapshot BuildProgressivePresentationSnapshot(
        const ECS::Components::GeometrySources::ConstSourceView& source,
        const ProgressivePresentationBindings& bindings,
        std::uint64_t observedSourceGeneration = 0u);
}
