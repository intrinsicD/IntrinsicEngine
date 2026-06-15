module;

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.VisualizationPackets;

import Extrinsic.Asset.Registry;
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
        ScalarDouble,
        RgbaFloat4,
        Count,
    };

    enum class VisualizationFragmentBakeMapping : std::uint8_t
    {
        ExistingTexcoords = 0,
        ExistingHtex,
        RecreateHtex,
    };

    enum class VisualizationTexcoordProvenance : std::uint8_t
    {
        Unknown = 0,
        Authored,
        GeneratedAtlas,
        RuntimeResolved,
    };

    enum class VisualizationGeneratedTextureSemantic : std::uint8_t
    {
        Unknown = 0,
        ScalarAttribute,
        LabelAttribute,
        Vector2Attribute,
        Vector3Attribute,
        Vector4Attribute,
        PbrAlbedo,
        PbrNormal,
        PbrMetallicRoughness,
        PbrEmissive,
        Displacement,
        Count,
    };

    struct VisualizationAttributeBufferPacket
    {
        std::string Name{};
        std::string SourceBufferKey{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        VisualizationValueType ValueType{VisualizationValueType::ScalarFloat};
        std::uint32_t ElementCount{0u};
        std::uint64_t BufferBDA{0u};
    };

    struct ScalarAttributePacket
    {
        std::string Name{};
        std::string SourceBufferKey{};
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
        std::string SourceBufferKey{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        std::uint32_t ElementCount{0u};
        std::uint64_t ColorBufferBDA{0u};
    };

    struct VectorFieldOverlayPacket
    {
        std::string Name{};
        std::string PositionBufferSourceKey{};
        std::string VectorBufferSourceKey{};
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
        std::string ScalarBufferSourceKey{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Face};
        std::uint32_t IsoValueCount{0u};
        std::uint64_t ScalarBufferBDA{0u};
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
        std::string TexcoordBufferSourceKey{};
        VisualizationFragmentBakeMapping Mapping{VisualizationFragmentBakeMapping::ExistingTexcoords};
        bool MeshHasTexcoords{false};
        std::uint32_t FaceCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint64_t TexcoordBufferBDA{0u};
        Assets::AssetId AtlasTextureAsset{};
        VisualizationGeneratedTextureSemantic GeneratedTextureSemantic{
            VisualizationGeneratedTextureSemantic::Unknown};
        VisualizationTexcoordProvenance TexcoordProvenance{
            VisualizationTexcoordProvenance::Unknown};
        std::uint64_t TexcoordDirtyStamp{0u};
        std::uint64_t SourceAttributeDirtyStamp{0u};
    };

    struct VisualizationPropertyBufferUploadDescriptor
    {
        std::string SourceKey{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        VisualizationValueType ValueType{VisualizationValueType::ScalarFloat};
        std::uint32_t ElementCount{0u};
        std::uint32_t StrideBytes{0u};
        // DirtyStamp == 0 means the producer has no stable dirty stamp yet,
        // so residency must upload this descriptor every submission.
        std::uint64_t DirtyStamp{0u};
        std::span<const std::byte> Bytes{};
    };

    struct VisualizationPropertyBufferAddress
    {
        std::string SourceKey{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        VisualizationValueType ValueType{VisualizationValueType::ScalarFloat};
        std::uint32_t ElementCount{0u};
        std::uint32_t StrideBytes{0u};
        std::uint64_t DirtyStamp{0u};
        std::uint64_t BufferBDA{0u};
    };

    struct VisualizationPropertyBufferDiagnostics
    {
        std::uint32_t InputBufferCount{0u};
        std::uint32_t AcceptedBufferCount{0u};
        std::uint32_t UploadedBufferCount{0u};
        std::uint32_t ReusedBufferCount{0u};
        std::uint32_t UnsupportedTypeCount{0u};
        std::uint32_t InvalidSourceKeyCount{0u};
        std::uint32_t ZeroElementCount{0u};
        std::uint32_t InvalidStrideCount{0u};
        std::uint32_t InvalidByteSizeCount{0u};
        std::uint32_t NonFiniteValueCount{0u};
        std::uint32_t StaleDirtyStampCount{0u};
        std::uint32_t UploadDeferralCount{0u};
        std::uint32_t InvalidResourceCount{0u};
        bool HasErrors{false};
    };

    struct VisualizationPacketBatch
    {
        std::span<const VisualizationPropertyBufferUploadDescriptor> PropertyBuffers{};
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
        std::uint32_t AuthoredUvBakeAtlasDescriptorCount{0u};
        std::uint32_t GeneratedUvBakeAtlasDescriptorCount{0u};
        std::uint32_t RuntimeResolvedUvBakeAtlasDescriptorCount{0u};
        std::uint32_t HtexBakeAtlasDescriptorCount{0u};
        std::uint32_t HtexRecreateRequestCount{0u};
        std::uint32_t FragmentBakeTextureAssetDescriptorCount{0u};
        std::uint32_t ScalarBakeTextureAssetDescriptorCount{0u};
        std::uint32_t LabelBakeTextureAssetDescriptorCount{0u};
        std::uint32_t VectorBakeTextureAssetDescriptorCount{0u};
        std::uint32_t PbrBakeTextureAssetDescriptorCount{0u};
        std::uint32_t DisplacementBakeTextureAssetDescriptorCount{0u};
        bool RequiresTextureResidency{false};
    };

    [[nodiscard]] std::uint32_t ExpectedVisualizationValueStride(VisualizationValueType type) noexcept;
    [[nodiscard]] bool ValidateVisualizationPropertyBufferUploadDescriptor(
        const VisualizationPropertyBufferUploadDescriptor& descriptor,
        VisualizationPropertyBufferDiagnostics& diagnostics) noexcept;
    [[nodiscard]] VisualizationPropertyBufferDiagnostics ValidateVisualizationPropertyBufferUploads(
        std::span<const VisualizationPropertyBufferUploadDescriptor> descriptors) noexcept;
    [[nodiscard]] VisualizationDiagnostics ValidateVisualizationPackets(const VisualizationPacketBatch& batch) noexcept;
    [[nodiscard]] VisualizationOverlaySummary BuildVisualizationOverlaySummary(const VisualizationPacketBatch& batch) noexcept;
}
