module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

module Extrinsic.Asset.ModelTexturePayload;

namespace Extrinsic::Assets
{
    namespace
    {
        [[nodiscard]] bool IsValidReference(
            const AssetModelTextureReference& reference,
            const std::size_t imageCount) noexcept
        {
            return !reference.IsValid()
                || reference.ImageIndex < imageCount;
        }

        [[nodiscard]] Core::Result ValidateMaterial(
            const AssetModelMaterialPayload& material,
            const std::size_t imageCount) noexcept
        {
            if (!IsValidReference(material.BaseColorTexture, imageCount)
                || !IsValidReference(material.NormalTexture, imageCount)
                || !IsValidReference(material.MetallicRoughnessTexture, imageCount)
                || !IsValidReference(material.OcclusionTexture, imageCount))
            {
                return Core::Err(Core::ErrorCode::OutOfRange);
            }
            return Core::Ok();
        }

        [[nodiscard]] Core::Result ValidateGeometryPayload(
            const AssetGeometryPayload& payload) noexcept
        {
            if (!payload.IsValid())
            {
                return Core::Err(Core::ErrorCode::AssetInvalidData);
            }
            if (!IsAssetModelSceneGeometryKind(payload.PayloadKind)
                || !IsGeometryPayloadKind(payload.PayloadKind))
            {
                return Core::Err(Core::ErrorCode::AssetTypeMismatch);
            }
            return Core::Ok();
        }

        [[nodiscard]] Core::Result ValidatePrimitive(
            const AssetModelPrimitivePayload& primitive,
            const std::span<const AssetGeometryPayload> geometryPayloads,
            const std::size_t materialCount) noexcept
        {
            if (!IsAssetModelSceneGeometryKind(primitive.GeometryKind))
            {
                return Core::Err(Core::ErrorCode::AssetTypeMismatch);
            }
            if (primitive.GeometryPayloadIndex == kInvalidAssetModelIndex
                || primitive.VertexCount == 0u)
            {
                return Core::Err(Core::ErrorCode::AssetInvalidData);
            }
            if (primitive.GeometryPayloadIndex >= geometryPayloads.size())
            {
                return Core::Err(Core::ErrorCode::OutOfRange);
            }
            if (geometryPayloads[primitive.GeometryPayloadIndex].PayloadKind
                != primitive.GeometryKind)
            {
                return Core::Err(Core::ErrorCode::AssetTypeMismatch);
            }
            if (primitive.MaterialIndex != kInvalidAssetModelIndex
                && primitive.MaterialIndex >= materialCount)
            {
                return Core::Err(Core::ErrorCode::OutOfRange);
            }
            return Core::Ok();
        }

        [[nodiscard]] Core::Result ValidateExternalDiagnostic(
            const AssetModelExternalResourceDiagnostic& diagnostic) noexcept
        {
            if (diagnostic.Status == AssetModelResourceStatus::Ready)
            {
                return diagnostic.Error == Core::ErrorCode::Success
                    ? Core::Ok()
                    : Core::Err(Core::ErrorCode::InvalidArgument);
            }
            return diagnostic.Error == Core::ErrorCode::Success
                ? Core::Err(Core::ErrorCode::InvalidArgument)
                : Core::Ok();
        }

        [[nodiscard]] bool TextureByteCountFits(
            const AssetTexture2DMetadata& metadata,
            const std::uint32_t bytesPerPixel,
            std::size_t& outExpectedBytes) noexcept
        {
            const auto width = static_cast<std::size_t>(metadata.Width);
            const auto height = static_cast<std::size_t>(metadata.Height);
            const auto bpp = static_cast<std::size_t>(bytesPerPixel);
            constexpr std::size_t max = std::numeric_limits<std::size_t>::max();
            if (width == 0u || height == 0u || bpp == 0u)
            {
                outExpectedBytes = 0u;
                return false;
            }
            if (width > max / height)
            {
                return false;
            }
            const std::size_t texels = width * height;
            if (texels > max / bpp)
            {
                return false;
            }
            outExpectedBytes = texels * bpp;
            return true;
        }
    }

    bool IsSupportedTextureImportFormat(const AssetFileFormat format) noexcept
    {
        return format == AssetFileFormat::PNG
            || format == AssetFileFormat::JPEG
            || format == AssetFileFormat::TGA
            || format == AssetFileFormat::BMP
            || format == AssetFileFormat::HDR;
    }

    bool IsSupportedModelSceneImportFormat(const AssetFileFormat format) noexcept
    {
        return format == AssetFileFormat::GLTF
            || format == AssetFileFormat::GLB;
    }

    bool IsAssetModelSceneGeometryKind(const AssetPayloadKind kind) noexcept
    {
        return kind == AssetPayloadKind::Mesh
            || kind == AssetPayloadKind::PointCloud
            || kind == AssetPayloadKind::Graph;
    }

    std::uint32_t BytesPerPixel(const AssetTexturePixelFormat format) noexcept
    {
        switch (format)
        {
        case AssetTexturePixelFormat::R8Unorm:
            return 1u;
        case AssetTexturePixelFormat::Rg8Unorm:
            return 2u;
        case AssetTexturePixelFormat::Rgb8Unorm:
            return 3u;
        case AssetTexturePixelFormat::Rgba8Unorm:
            return 4u;
        case AssetTexturePixelFormat::Rgb32Float:
            return 12u;
        case AssetTexturePixelFormat::Rgba32Float:
            return 16u;
        case AssetTexturePixelFormat::Unknown:
            break;
        }
        return 0u;
    }

    std::uint32_t ComponentCountFor(const AssetTexturePixelFormat format) noexcept
    {
        switch (format)
        {
        case AssetTexturePixelFormat::R8Unorm:
            return 1u;
        case AssetTexturePixelFormat::Rg8Unorm:
            return 2u;
        case AssetTexturePixelFormat::Rgb8Unorm:
        case AssetTexturePixelFormat::Rgb32Float:
            return 3u;
        case AssetTexturePixelFormat::Rgba8Unorm:
        case AssetTexturePixelFormat::Rgba32Float:
            return 4u;
        case AssetTexturePixelFormat::Unknown:
            break;
        }
        return 0u;
    }

    Core::Result ValidateAssetTexture2DPayload(const AssetTexture2DPayload& payload)
    {
        const AssetTexture2DMetadata& metadata = payload.Metadata;
        const std::uint32_t bytesPerPixel = BytesPerPixel(metadata.PixelFormat);
        if (metadata.Width == 0u || metadata.Height == 0u || bytesPerPixel == 0u)
        {
            return Core::Err(Core::ErrorCode::AssetInvalidData);
        }
        if (!IsSupportedTextureImportFormat(metadata.SourceFormat))
        {
            return Core::Err(Core::ErrorCode::AssetUnsupportedFormat);
        }
        if (metadata.Components != ComponentCountFor(metadata.PixelFormat))
        {
            return Core::Err(Core::ErrorCode::InvalidFormat);
        }

        std::size_t expectedBytes = 0u;
        if (!TextureByteCountFits(metadata, bytesPerPixel, expectedBytes))
        {
            return Core::Err(Core::ErrorCode::OutOfRange);
        }
        if (payload.PixelBytes.size() != expectedBytes)
        {
            return Core::Err(Core::ErrorCode::AssetInvalidData);
        }
        return Core::Ok();
    }

    Core::Result ValidateAssetModelScenePayload(const AssetModelScenePayload& payload)
    {
        if (payload.Primitives.empty())
        {
            return Core::Err(Core::ErrorCode::AssetInvalidData);
        }

        for (const AssetGeometryPayload& geometry : payload.GeometryPayloads)
        {
            if (Core::Result validGeometry = ValidateGeometryPayload(geometry);
                !validGeometry.has_value())
            {
                return validGeometry;
            }
        }

        for (const AssetTexture2DPayload& image : payload.EmbeddedImages)
        {
            if (Core::Result validImage = ValidateAssetTexture2DPayload(image);
                !validImage.has_value())
            {
                return validImage;
            }
        }

        for (const AssetModelMaterialPayload& material : payload.Materials)
        {
            if (Core::Result validMaterial =
                    ValidateMaterial(material, payload.EmbeddedImages.size());
                !validMaterial.has_value())
            {
                return validMaterial;
            }
        }

        for (const AssetModelPrimitivePayload& primitive : payload.Primitives)
        {
            if (Core::Result validPrimitive =
                    ValidatePrimitive(
                        primitive,
                        payload.GeometryPayloads,
                        payload.Materials.size());
                !validPrimitive.has_value())
            {
                return validPrimitive;
            }
        }

        for (const AssetModelExternalResourceDiagnostic& diagnostic :
             payload.ExternalResourceDiagnostics)
        {
            if (Core::Result validDiagnostic =
                    ValidateExternalDiagnostic(diagnostic);
                !validDiagnostic.has_value())
            {
                return validDiagnostic;
            }
        }
        return Core::Ok();
    }

    const char* DebugNameForAssetTexturePixelFormat(
        const AssetTexturePixelFormat format) noexcept
    {
        switch (format)
        {
        case AssetTexturePixelFormat::Unknown:
            return "Unknown";
        case AssetTexturePixelFormat::R8Unorm:
            return "R8Unorm";
        case AssetTexturePixelFormat::Rg8Unorm:
            return "Rg8Unorm";
        case AssetTexturePixelFormat::Rgb8Unorm:
            return "Rgb8Unorm";
        case AssetTexturePixelFormat::Rgba8Unorm:
            return "Rgba8Unorm";
        case AssetTexturePixelFormat::Rgb32Float:
            return "Rgb32Float";
        case AssetTexturePixelFormat::Rgba32Float:
            return "Rgba32Float";
        }
        return "Unknown";
    }

    const char* DebugNameForAssetTextureColorSpace(
        const AssetTextureColorSpace colorSpace) noexcept
    {
        switch (colorSpace)
        {
        case AssetTextureColorSpace::Unknown:
            return "Unknown";
        case AssetTextureColorSpace::Linear:
            return "Linear";
        case AssetTextureColorSpace::SRGB:
            return "SRGB";
        }
        return "Unknown";
    }

    const char* DebugNameForAssetModelResourceStatus(
        const AssetModelResourceStatus status) noexcept
    {
        switch (status)
        {
        case AssetModelResourceStatus::Ready:
            return "Ready";
        case AssetModelResourceStatus::MissingUri:
            return "MissingUri";
        case AssetModelResourceStatus::FileReadFailed:
            return "FileReadFailed";
        case AssetModelResourceStatus::DecodeFailed:
            return "DecodeFailed";
        case AssetModelResourceStatus::UnsupportedFormat:
            return "UnsupportedFormat";
        case AssetModelResourceStatus::PayloadRegistrationFailed:
            return "PayloadRegistrationFailed";
        }
        return "Unknown";
    }
}
