// Graphics-owned visualization packets and upload descriptors shared by runtime
// producers and render consumers without live scene ownership.
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

    // One instanced arrow per sampled live source row. Anchor and vector
    // buffers hold one object-space vec3 per source element; the optional row
    // buffer lists live element indices (empty key = every element is live).
    // Instance `i` draws live row `i * RowStride`. The glyph tip is
    // `anchor + Scale * (NormalizeLength ? normalize(v) : v)` in object space,
    // then both endpoints are transformed by `ObjectToWorld`.
    struct VectorFieldOverlayPacket
    {
        std::string Name{};
        std::string PositionBufferSourceKey{};
        std::string VectorBufferSourceKey{};
        std::string RowBufferSourceKey{};
        VisualizationAttributeDomain Domain{VisualizationAttributeDomain::Vertex};
        std::uint32_t ElementCount{0u};
        std::uint32_t RowCount{0u};
        std::uint32_t RowStride{1u};
        std::uint64_t PositionBufferBDA{0u};
        std::uint64_t VectorBufferBDA{0u};
        std::uint64_t RowBufferBDA{0u};
        glm::mat4 ObjectToWorld{1.f};
        float Scale{1.f};
        bool NormalizeLength{true};
        float LineWidthPx{2.f};
        glm::vec4 Color{1.f};
        // Selects the depth-tested or always-on-top pipeline variant.
        bool DepthTested{true};
    };

    inline constexpr float kVectorFieldMinLineWidthPx = 0.5f;
    inline constexpr float kVectorFieldMaxLineWidthPx = 32.f;

    // Instances drawn for a packet: ceil(RowCount / RowStride), 0 if invalid.
    [[nodiscard]] std::uint32_t VectorFieldGlyphCount(
        const VectorFieldOverlayPacket& packet) noexcept;
    // True when every buffer address is resolved and the style is in range.
    [[nodiscard]] bool IsRenderableVectorFieldPacket(
        const VectorFieldOverlayPacket& packet) noexcept;

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

    // Scalar payloads may contain infinity for unreachable samples, but never
    // NaN. Vector/color payloads remain finite; scalar display ranges are finite.
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
        // Identifies the element-to-buffer layout independently of property
        // content; zero denotes the producer's canonical layout. Any change
        // invalidates residency even when DirtyStamp is unchanged (for
        // example, after mesh corner splitting).
        std::uint64_t SourceLayoutStamp{0u};
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
        std::uint64_t SourceLayoutStamp{0u};
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
        // Content changes upload into a fresh buffer; the superseded lease
        // retires through the device's deferred destruction.
        std::uint32_t ReplacedBufferCount{0u};
        // Resident keys absent from this submission are released.
        std::uint32_t EvictedBufferCount{0u};
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
    // Counts one input and checks key, type, stride, count and byte size
    // without reading payload values. Residency uses it to reuse an unchanged
    // buffer in O(1); the payload scan below runs only before an upload.
    [[nodiscard]] bool ValidateVisualizationPropertyBufferShape(
        const VisualizationPropertyBufferUploadDescriptor& descriptor,
        VisualizationPropertyBufferDiagnostics& diagnostics) noexcept;
    // Scans a shape-valid payload and counts it accepted or non-finite.
    [[nodiscard]] bool ValidateVisualizationPropertyBufferPayload(
        const VisualizationPropertyBufferUploadDescriptor& descriptor,
        VisualizationPropertyBufferDiagnostics& diagnostics) noexcept;
    // Shape plus payload validation.
    [[nodiscard]] bool ValidateVisualizationPropertyBufferUploadDescriptor(
        const VisualizationPropertyBufferUploadDescriptor& descriptor,
        VisualizationPropertyBufferDiagnostics& diagnostics) noexcept;
    [[nodiscard]] VisualizationPropertyBufferDiagnostics ValidateVisualizationPropertyBufferUploads(
        std::span<const VisualizationPropertyBufferUploadDescriptor> descriptors) noexcept;
    [[nodiscard]] VisualizationDiagnostics ValidateVisualizationPackets(const VisualizationPacketBatch& batch) noexcept;
    [[nodiscard]] VisualizationOverlaySummary BuildVisualizationOverlaySummary(const VisualizationPacketBatch& batch) noexcept;
}
