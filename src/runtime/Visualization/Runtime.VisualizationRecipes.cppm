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
    };

    struct LabelVisualizationRecipe
    {
        GeometryPropertyRef Source{};
        std::string OutputName{};
        std::uint64_t BufferBDA{0u};
        std::string BufferSourceKey{};
        std::uint64_t DirtyStamp{0u};
    };

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
