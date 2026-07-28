module;

#include <cstdint>
#include <string>

export module Extrinsic.Graphics.PropertyTextureBake;

import Extrinsic.Core.Error;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    inline constexpr std::uint32_t
        kPropertyTextureBakeMaxPaddingTexels = 32u;
    inline constexpr std::uint32_t
        kPropertyTextureBakeDilationOutputDescriptorSlot = 4u;
    inline constexpr std::uint32_t
        kPropertyTextureBakeDilationScratchDescriptorSlot = 5u;

    enum class PropertyTextureBakeDomain : std::uint32_t
    {
        Vertex = 0u,
        Face = 1u,
        NearestEdge = 2u,
    };

    enum class PropertyTextureBakeValueKind : std::uint32_t
    {
        Scalar = 0u,
        Label = 1u,
        Vector2 = 2u,
        Vector3 = 3u,
        Vector4 = 4u,
    };

    enum class PropertyTextureBakeEncoding : std::uint32_t
    {
        Raw = 0u,
        Normal = 1u,
        RgbaColor = 2u,
        ScalarColormap = 3u,
        LabelPalette = 4u,
        LinearScalar = 5u,
    };

    struct alignas(8) PropertyTextureBakePushConstants
    {
        std::uint64_t TexcoordBDA{0u};
        std::uint64_t PropertyBDA{0u};
        std::uint64_t IndexBDA{0u};
        std::uint32_t Domain{0u};
        std::uint32_t ValueKind{0u};
        std::uint32_t Encoding{0u};
        std::uint32_t ColormapID{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
    };
    static_assert(sizeof(PropertyTextureBakePushConstants) == 48u);

    struct alignas(16) PropertyTextureBakeDilationPushConstants
    {
        std::uint32_t SourceTextureSlot{
            kPropertyTextureBakeDilationOutputDescriptorSlot};
        std::uint32_t Reserved0{0u};
        std::uint32_t Reserved1{0u};
        std::uint32_t Reserved2{0u};
        float ClearR{0.0f};
        float ClearG{0.0f};
        float ClearB{0.0f};
        float ClearA{0.0f};
    };
    static_assert(sizeof(PropertyTextureBakeDilationPushConstants) == 32u);

    struct PropertyTextureBakeDilationResources
    {
        RHI::PipelineHandle Pipeline{};
        RHI::TextureHandle ScratchTexture{};
        RHI::TextureLayout ScratchInitialLayout{
            RHI::TextureLayout::Undefined};
        std::uint32_t OutputDescriptorSlot{
            kPropertyTextureBakeDilationOutputDescriptorSlot};
        std::uint32_t ScratchDescriptorSlot{
            kPropertyTextureBakeDilationScratchDescriptorSlot};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Pipeline.IsValid() &&
                   ScratchTexture.IsValid() &&
                   OutputDescriptorSlot != ScratchDescriptorSlot;
        }
    };

    struct PropertyTextureBakeRecordDesc
    {
        RHI::PipelineHandle Pipeline{};
        RHI::TextureHandle OutputTexture{};
        PropertyTextureBakeDilationResources Dilation{};
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t TexcoordBDA{0u};
        std::uint64_t PropertyBDA{0u};
        std::uint64_t IndexBDA{0u};
        std::uint32_t FirstIndex{0u};
        std::uint32_t IndexCount{0u};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t PaddingTexels{0u};
        PropertyTextureBakeDomain Domain{PropertyTextureBakeDomain::Vertex};
        PropertyTextureBakeValueKind ValueKind{
            PropertyTextureBakeValueKind::Scalar};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Raw};
        std::uint32_t ColormapID{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        RHI::TextureLayout InitialLayout{RHI::TextureLayout::Undefined};
        RHI::TextureLayout FinalLayout{RHI::TextureLayout::ShaderReadOnly};
    };

    [[nodiscard]] RHI::PipelineDesc MakePropertyTextureBakePipelineDesc(
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        RHI::Format colorFormat);

    [[nodiscard]] RHI::PipelineDesc
        MakePropertyTextureBakeDilationPipelineDesc(
            std::string vertexShaderPath,
            std::string fragmentShaderPath,
            RHI::Format colorFormat);

    [[nodiscard]] RHI::TextureDesc
        MakePropertyTextureBakeDilationScratchTextureDesc(
            std::uint32_t width,
            std::uint32_t height,
            RHI::Format colorFormat,
            const char* debugName =
                "PropertyTextureBake.DilationScratch") noexcept;

    [[nodiscard]] Core::Result RecordPropertyTextureBake(
        RHI::ICommandContext& commandContext,
        const PropertyTextureBakeRecordDesc& desc);
}
