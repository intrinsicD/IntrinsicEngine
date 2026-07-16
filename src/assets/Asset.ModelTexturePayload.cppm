module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

export module Extrinsic.Asset.ModelTexturePayload;

import Extrinsic.Core.Error;
import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;

export namespace Extrinsic::Assets
{
    inline constexpr std::uint32_t kInvalidAssetModelIndex = 0xFFFFFFFFu;

    enum class AssetTexturePixelFormat : std::uint8_t
    {
        Unknown,
        R8Unorm,
        Rg8Unorm,
        Rgb8Unorm,
        Rgba8Unorm,
        Rgb32Float,
        Rgba32Float,
    };

    enum class AssetTextureColorSpace : std::uint8_t
    {
        Unknown,
        Linear,
        SRGB,
    };

    enum class AssetTextureSourceKind : std::uint8_t
    {
        Unknown,
        ExternalFile,
        Embedded,
        Generated,
    };

    enum class AssetModelResourceKind : std::uint8_t
    {
        Unknown,
        Buffer,
        Image,
        Texture,
        Material,
    };

    enum class AssetModelResourceStatus : std::uint8_t
    {
        Ready,
        MissingUri,
        FileReadFailed,
        DecodeFailed,
        UnsupportedFormat,
        PayloadRegistrationFailed,
    };

    struct AssetTexture2DMetadata
    {
        std::uint32_t Width{0};
        std::uint32_t Height{0};
        std::uint32_t Components{0};
        AssetTexturePixelFormat PixelFormat{AssetTexturePixelFormat::Unknown};
        AssetTextureColorSpace ColorSpace{AssetTextureColorSpace::Unknown};
        AssetTextureSourceKind SourceKind{AssetTextureSourceKind::Unknown};
        AssetFileFormat SourceFormat{AssetFileFormat::Unknown};
        std::string SourcePath{};
        std::string DebugName{};
    };

    struct AssetTexture2DPayload
    {
        AssetTexture2DMetadata Metadata{};
        std::vector<std::byte> PixelBytes{};
    };

    struct AssetModelTextureReference
    {
        std::uint32_t ImageIndex{kInvalidAssetModelIndex};
        std::string Uri{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return ImageIndex != kInvalidAssetModelIndex;
        }
    };

    struct AssetModelMaterialPayload
    {
        std::string Name{};
        std::array<float, 4> BaseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        float MetallicFactor{1.0f};
        float RoughnessFactor{1.0f};
        AssetModelTextureReference BaseColorTexture{};
        AssetModelTextureReference NormalTexture{};
        AssetModelTextureReference MetallicRoughnessTexture{};
        AssetModelTextureReference OcclusionTexture{};
    };

    struct AssetModelPrimitivePayload
    {
        std::string Name{};
        AssetPayloadKind GeometryKind{AssetPayloadKind::Mesh};
        std::uint32_t GeometryPayloadIndex{kInvalidAssetModelIndex};
        std::uint32_t MaterialIndex{kInvalidAssetModelIndex};
        std::uint32_t VertexCount{0};
        std::uint32_t IndexCount{0};
    };

    struct AssetModelNodePayload
    {
        std::string Name{};
        std::uint32_t ParentNodeIndex{kInvalidAssetModelIndex};
        std::array<float, 16> LocalTransform{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
        std::vector<std::uint32_t> ChildNodeIndices{};
        std::vector<std::uint32_t> PrimitiveIndices{};
    };

    struct AssetModelExternalResourceDiagnostic
    {
        AssetModelResourceKind ResourceKind{AssetModelResourceKind::Unknown};
        AssetModelResourceStatus Status{AssetModelResourceStatus::Ready};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Uri{};
        std::string Message{};
    };

    struct AssetModelScenePayload
    {
        std::string SourcePath{};
        std::vector<std::uint32_t> RootNodeIndices{};
        std::vector<AssetModelNodePayload> Nodes{};
        std::vector<AssetGeometryPayload> GeometryPayloads{};
        std::vector<AssetModelPrimitivePayload> Primitives{};
        std::vector<AssetTexture2DPayload> EmbeddedImages{};
        std::vector<AssetModelMaterialPayload> Materials{};
        std::vector<AssetModelExternalResourceDiagnostic> ExternalResourceDiagnostics{};
    };

    [[nodiscard]] bool IsSupportedTextureImportFormat(AssetFileFormat format) noexcept;
    [[nodiscard]] bool IsSupportedModelSceneImportFormat(AssetFileFormat format) noexcept;
    [[nodiscard]] bool IsAssetModelSceneGeometryKind(AssetPayloadKind kind) noexcept;
    [[nodiscard]] std::uint32_t BytesPerPixel(AssetTexturePixelFormat format) noexcept;
    [[nodiscard]] std::uint32_t ComponentCountFor(AssetTexturePixelFormat format) noexcept;
    [[nodiscard]] Core::Result ValidateAssetTexture2DPayload(
        const AssetTexture2DPayload& payload);
    [[nodiscard]] Core::Result ValidateAssetModelScenePayload(
        const AssetModelScenePayload& payload);
    [[nodiscard]] const char* DebugNameForAssetTexturePixelFormat(
        AssetTexturePixelFormat format) noexcept;
    [[nodiscard]] const char* DebugNameForAssetTextureColorSpace(
        AssetTextureColorSpace colorSpace) noexcept;
    [[nodiscard]] const char* DebugNameForAssetModelResourceStatus(
        AssetModelResourceStatus status) noexcept;
}
