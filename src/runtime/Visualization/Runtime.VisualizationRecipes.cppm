// Property visualization recipes separate authored interpretation from source identity.
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.VisualizationRecipes;

import Geometry.Properties;
import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.JobService;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    struct VisualizationEncodingBatch
    {
        std::vector<Graphics::VisualizationPropertyBufferUploadDescriptor> PropertyBuffers{};
        std::vector<std::vector<std::byte>> PropertyBufferPayloads{};
        std::vector<Graphics::VisualizationAttributeBufferPacket> AttributeBuffers{};
        std::vector<Graphics::ScalarAttributePacket> Scalars{};
        std::vector<Graphics::ColorAttributePacket> Colors{};
        std::vector<Graphics::VectorFieldOverlayPacket> VectorFields{};
        std::vector<Graphics::IsolineOverlayPacket> Isolines{};
        std::vector<Graphics::HtexPatchPreviewAtlasPacket> HtexAtlases{};
        std::vector<Graphics::FragmentBakeAtlasPacket> FragmentBakeAtlases{};

        void Clear() noexcept;
        void Append(VisualizationEncodingBatch&& other);

        [[nodiscard]] Graphics::VisualizationPacketBatch AsPacketBatch(
            bool enforceDomain = false,
            Graphics::VisualizationAttributeDomain expectedDomain =
                Graphics::VisualizationAttributeDomain::Vertex) const noexcept;
    };

    struct VisualizationEncodingDiagnostics
    {
        std::uint32_t PacketAppendCount{0u};
        std::uint32_t MissingSourceCount{0u};
        std::uint32_t UnsupportedSourceTypeCount{0u};
        std::uint32_t EmptySourceCount{0u};
        std::uint32_t InvalidBufferCount{0u};
        std::uint32_t InvalidResourceCount{0u};
        std::uint32_t MissingTexcoordCount{0u};
        std::uint32_t InvalidRangeCount{0u};
        std::uint32_t NonFiniteValueCount{0u};
        std::uint32_t ElementCountOverflowCount{0u};
        std::uint32_t ManualRangeCount{0u};
        std::uint32_t FlatAutoRangeExpandedCount{0u};
        std::uint32_t RobustAutoRangeClampedCount{0u};
        std::uint64_t ScalarValueScanCount{0u};
        // Live-row indices bounds-checked by `AppendVectorFieldPacket`.
        std::uint64_t VectorRowIndexCheckCount{0u};
    };

    // RUNTIME-198 Slice A — closed authored data. Each alternative names only
    // the property and metadata it actually consumes; no adapter identity,
    // object lifetime, registry key, ECS handle, or service pointer is stored.
    struct ScalarVisualizationRecipe
    {
        GeometryPropertyRef Source{};
        std::string OutputName{};
        std::uint64_t BufferBDA{0u};
        std::string BufferSourceKey{};
        // CPU-backed property buffers use the property's canonical content
        // revision. This authored value remains for external GPU-address
        // sources that do not publish through Geometry::PropertySet.
        std::uint64_t DirtyStamp{0u};
        bool AutoRange{true};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        Graphics::Colormap::Type Colormap{Graphics::Colormap::Type::Viridis};
    };

    struct ColorVisualizationRecipe
    {
        GeometryPropertyRef Source{};
        std::string OutputName{};
        std::uint64_t BufferBDA{0u};
        std::string BufferSourceKey{};
        std::uint64_t DirtyStamp{0u};
        Graphics::Components::VisualizationConfig::ColorInterpretation Interpretation{};
    };

    struct LabelVisualizationRecipe
    {
        GeometryPropertyRef Source{};
        std::string OutputName{};
        std::uint64_t BufferBDA{0u};
        std::string BufferSourceKey{};
        std::uint64_t DirtyStamp{0u};
    };

    // Explicit vector-field recipe with an authored anchor property on the
    // vector's domain. Persistent Appearance vector fields are
    // `GeometryVectorFieldLayerRecipe`s with derived anchors instead.
    struct VectorFieldVisualizationRecipe
    {
        GeometryPropertyRef Source{};
        GeometryPropertyRef PositionSource{};
        std::string OutputName{};
        std::uint64_t PositionBufferBDA{0u};
        std::uint64_t VectorBufferBDA{0u};
        std::string PositionBufferSourceKey{};
        std::string VectorBufferSourceKey{};
        std::uint64_t DirtyStamp{0u};
        float Scale{1.0f};
        glm::vec4 Color{1.0f};
        bool DepthTested{true};
    };

    struct IsolineVisualizationRecipe
    {
        GeometryPropertyRef Source{};
        std::string OutputName{};
        std::uint64_t BufferBDA{0u};
        std::string BufferSourceKey{};
        std::uint64_t DirtyStamp{0u};
        bool AutoRange{true};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint32_t IsoValueCount{0u};
        float LineWidth{1.0f};
        glm::vec4 Color{0.0f, 0.0f, 0.0f, 1.0f};
        bool DepthTested{true};
    };

    struct HtexPreviewVisualizationRecipe
    {
        std::string Name{};
        std::uint32_t PatchCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
    };

    struct FragmentBakeVisualizationRecipe
    {
        std::string Name{};
        GeometryPropertyRef Source{};
        Graphics::VisualizationFragmentBakeMapping Mapping{
            Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords};
        bool MeshHasTexcoords{false};
        std::uint32_t FaceCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::string TexcoordBufferSourceKey{};
        std::uint64_t TexcoordBufferBDA{0u};
        Assets::AssetId AtlasTextureAsset{};
        Graphics::VisualizationGeneratedTextureSemantic GeneratedTextureSemantic{
            Graphics::VisualizationGeneratedTextureSemantic::Unknown};
        std::uint64_t TexcoordDirtyStamp{0u};
        std::uint64_t SourceAttributeDirtyStamp{0u};
    };

    using VisualizationRecipeData = std::variant<
        std::monostate,
        ScalarVisualizationRecipe,
        ColorVisualizationRecipe,
        LabelVisualizationRecipe,
        VectorFieldVisualizationRecipe,
        IsolineVisualizationRecipe,
        HtexPreviewVisualizationRecipe,
        FragmentBakeVisualizationRecipe>;

    struct VisualizationRecipe
    {
        VisualizationRecipeData Data{};
    };

    enum class VisualizationRecipeKind : std::uint8_t
    {
        Empty,
        Scalar,
        Color,
        Label,
        VectorField,
        Isoline,
        HtexPreview,
        FragmentBake,
    };

    [[nodiscard]] VisualizationRecipeKind GetVisualizationRecipeKind(
        const VisualizationRecipe& recipe) noexcept;

    [[nodiscard]] std::string_view ToString(
        VisualizationRecipeKind kind) noexcept;

    [[nodiscard]] bool SameVisualizationRecipe(
        const VisualizationRecipe& lhs,
        const VisualizationRecipe& rhs) noexcept;

    enum class VisualizationRecipeStatus : std::uint8_t
    {
        Encoded,
        EmptyRecipe,
        UnsupportedDomain,
        MissingSource,
        UnsupportedSourceType,
        EmptySource,
        InvalidBuffer,
        InvalidResource,
        MissingTexcoord,
        InvalidRange,
        NonFiniteValue,
        ElementCountMismatch,
        ElementCountOverflow,
    };

    struct VisualizationEncodingResult
    {
        VisualizationRecipeStatus Status{VisualizationRecipeStatus::EmptyRecipe};
        VisualizationEncodingBatch Batch{};
        VisualizationEncodingDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == VisualizationRecipeStatus::Encoded;
        }
    };

    [[nodiscard]] std::string_view ToString(
        VisualizationRecipeStatus status) noexcept;

    [[nodiscard]] VisualizationEncodingResult EncodeVisualizationRecipe(
        const GeometryEntityAvailability& availability,
        const VisualizationRecipe& recipe);

    // Inputs for one vector-field overlay packet. Anchor and vector spans hold
    // one object-space vec3 per source element; `Rows` optionally lists live
    // element indices (empty = every element is live). A nonzero BDA replaces
    // the corresponding span with an externally resident buffer.
    //
    // With `CopyPayloads == false` the batch descriptors borrow the spans, so
    // they must stay valid and unmodified until the batch has been submitted;
    // borrowed payloads must already be finite. Copied payloads are sanitized:
    // a row whose anchor or vector is not finite gets a zero vector, which the
    // renderer draws as nothing. Descriptors whose key is already in the batch
    // are shared rather than appended again.
    struct VectorFieldPacketInputs
    {
        std::string Name{};
        Graphics::VisualizationAttributeDomain Domain{
            Graphics::VisualizationAttributeDomain::Vertex};
        std::uint32_t ElementCount{0u};
        std::string AnchorKey{};
        std::span<const glm::vec3> Anchors{};
        std::uint64_t AnchorBDA{0u};
        std::uint64_t AnchorStamp{0u};
        std::string VectorKey{};
        std::span<const glm::vec3> Vectors{};
        std::uint64_t VectorBDA{0u};
        std::uint64_t VectorStamp{0u};
        std::string RowKey{};
        std::span<const std::uint32_t> Rows{};
        std::uint64_t RowStamp{0u};
        // The producer guarantees every row is < ElementCount (for example a
        // cache that built the rows from the element range). The append then
        // skips its O(rows) bounds check; unverified rows are always checked.
        bool RowsPrevalidated{false};
        std::uint32_t Stride{1u};
        std::uint32_t MaxGlyphs{0u};
        glm::mat4 ObjectToWorld{1.0f};
        float Scale{1.0f};
        bool NormalizeLength{true};
        float LineWidthPx{2.0f};
        glm::vec4 Color{1.0f};
        bool DepthTested{true};
        bool CopyPayloads{true};
    };

    // Smallest stride >= `stride` that draws at most `maxGlyphs` of
    // `rowCount` rows (`maxGlyphs == 0` keeps `stride`).
    [[nodiscard]] std::uint32_t ResolveVectorFieldRowStride(
        std::uint32_t rowCount,
        std::uint32_t stride,
        std::uint32_t maxGlyphs) noexcept;

    // Appends the packet and its buffer descriptors; on failure the batch is
    // unchanged and the status names the first violated input.
    [[nodiscard]] VisualizationRecipeStatus AppendVectorFieldPacket(
        VisualizationEncodingBatch& batch,
        const VectorFieldPacketInputs& inputs,
        VisualizationEncodingDiagnostics* diagnostics = nullptr);

    struct VisualizationHtexRecreateRequest
    {
        std::string DebugName{};
        WorldHandle World{DefaultWorldHandle};
        std::uint64_t PayloadToken{0u};
    };

    struct VisualizationHtexRecreateResult
    {
        JobToken Task{};
        std::string Diagnostic{};

        [[nodiscard]] bool Scheduled() const noexcept
        {
            return Task.IsValid();
        }
    };

    [[nodiscard]] VisualizationHtexRecreateResult
        ScheduleVisualizationHtexRecreate(
            JobService& jobs,
            const VisualizationHtexRecreateRequest& request);

}
