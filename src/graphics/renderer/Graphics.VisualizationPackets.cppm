module;

#include <cstdint>
#include <span>
#include <string>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.VisualizationPackets;

import Extrinsic.Graphics.Colormap;

export namespace Extrinsic::Graphics
{
    enum class VisualizationAttributeDomain : std::uint8_t
    {
        Vertex = 0,
        Edge,
        Face,
        Instance,
    };

    enum class VisualizationValueType : std::uint8_t
    {
        ScalarFloat = 0,
        Rgba8,
        VectorFloat3,
        LabelUint32,
    };

    enum class VisualizationFragmentBakeMapping : std::uint8_t
    {
        ExistingTexcoords = 0,
        ExistingHtex,
        RecreateHtex,
    };

    struct VisualizationAttributeBufferPacket
    {
        std::string Name{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        VisualizationValueType ValueType{VisualizationValueType::ScalarFloat};
        std::uint32_t ElementCount{0u};
        std::uint64_t BufferBDA{0u};
    };

    struct ScalarAttributePacket
    {
        std::string Name{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        std::uint32_t ElementCount{0u};
        float RangeMin{0.f};
        float RangeMax{1.f};
        Colormap::Type Colormap{Colormap::Type::Viridis};
        std::uint64_t ScalarBufferBDA{0u};
    };

    struct ColorAttributePacket
    {
        std::string Name{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        std::uint32_t ElementCount{0u};
        std::uint64_t ColorBufferBDA{0u};
    };

    struct VectorFieldOverlayPacket
    {
        std::string Name{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        std::uint32_t ElementCount{0u};
        std::uint64_t PositionBufferBDA{0u};
        std::uint64_t VectorBufferBDA{0u};
        float Scale{1.f};
        glm::vec4 Color{1.f};
        // GRAPHICS-078 Slice B — depth-tested vs always-on-top variant per packet,
        // resolved by the `VisualizationOverlayPass` to the matching pipeline
        // lease at record time. Mirrors the `DepthTested` field on
        // `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket`
        // (GRAPHICS-010Q two-variant policy). Default true preserves the
        // existing scene-depth-respecting glyph behavior.
        bool DepthTested{true};
    };

    struct IsolineOverlayPacket
    {
        std::string SourceScalarName{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Face};
        std::uint32_t IsoValueCount{0u};
        float RangeMin{0.f};
        float RangeMax{1.f};
        float LineWidth{1.f};
        glm::vec4 Color{1.f};
        // GRAPHICS-078 Slice C — depth-tested vs always-on-top variant per packet,
        // resolved by the `VisualizationOverlayPass` to the matching pipeline
        // lease at record time. Mirrors the `DepthTested` field on
        // `VectorFieldOverlayPacket` (Slice B) and on the transient-debug
        // packets (GRAPHICS-010Q two-variant policy). Default true preserves
        // the existing scene-depth-respecting isoline behavior.
        bool DepthTested{true};
    };

    struct HtexPatchPreviewAtlasPacket
    {
        std::string Name{};
        std::uint32_t PatchCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
    };

    struct FragmentBakeAtlasPacket
    {
        std::string Name{};
        std::string SourceAttributeName{};
        VisualizationFragmentBakeMapping Mapping{VisualizationFragmentBakeMapping::ExistingTexcoords};
        bool MeshHasTexcoords{false};
        std::uint32_t FaceCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint64_t TexcoordBufferBDA{0u};
    };

    struct VisualizationPacketBatch
    {
        std::span<const VisualizationAttributeBufferPacket> AttributeBuffers{};
        std::span<const ScalarAttributePacket> Scalars{};
        std::span<const ColorAttributePacket> Colors{};
        std::span<const VectorFieldOverlayPacket> VectorFields{};
        std::span<const IsolineOverlayPacket> Isolines{};
        std::span<const HtexPatchPreviewAtlasPacket> HtexAtlases{};
        std::span<const FragmentBakeAtlasPacket> FragmentBakeAtlases{};
        bool EnforceDomain{false};
        VisualizationAttributeDomain ExpectedDomain{VisualizationAttributeDomain::Vertex};
    };

    struct VisualizationDiagnostics
    {
        std::uint32_t InputPacketCount{0u};
        std::uint32_t AcceptedPacketCount{0u};
        std::uint32_t MissingAttributeCount{0u};
        std::uint32_t DomainMismatchCount{0u};
        std::uint32_t InvalidRangeCount{0u};
        std::uint32_t UnsupportedColormapCount{0u};
        std::uint32_t InvalidResourceCount{0u};
        std::uint32_t MissingTexcoordCount{0u};
        std::uint32_t HtexRecreateRequestCount{0u};
        std::uint32_t TextureResidencyDeferredCount{0u};
        bool HasErrors{false};
    };

    struct VisualizationOverlaySummary
    {
        std::uint32_t VectorFieldCount{0u};
        std::uint32_t VectorGlyphCount{0u};
        std::uint32_t IsolineLayerCount{0u};
        std::uint32_t IsolineValueCount{0u};
        std::uint32_t HtexAtlasDescriptorCount{0u};
        std::uint32_t UvBakeAtlasDescriptorCount{0u};
        std::uint32_t HtexBakeAtlasDescriptorCount{0u};
        std::uint32_t HtexRecreateRequestCount{0u};
        bool RequiresTextureResidency{false};
    };

    [[nodiscard]] VisualizationDiagnostics ValidateVisualizationPackets(const VisualizationPacketBatch& batch) noexcept;
    [[nodiscard]] VisualizationOverlaySummary BuildVisualizationOverlaySummary(const VisualizationPacketBatch& batch) noexcept;
}

