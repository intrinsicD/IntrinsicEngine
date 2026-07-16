module;

#include <cmath>
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

        [[nodiscard]] Core::Result ValidateNodeTopology(
            const std::span<const AssetModelNodePayload> nodes,
            const std::span<const std::uint32_t> rootNodeIndices,
            const std::size_t primitiveCount)
        {
            if (nodes.empty() || rootNodeIndices.empty() || primitiveCount == 0u)
            {
                return Core::Err(Core::ErrorCode::AssetInvalidData);
            }

            std::vector<std::uint8_t> rootMembership(nodes.size(), 0u);
            for (const std::uint32_t rootNodeIndex : rootNodeIndices)
            {
                if (rootNodeIndex >= nodes.size())
                {
                    return Core::Err(Core::ErrorCode::OutOfRange);
                }
                if (rootMembership[rootNodeIndex] != 0u)
                {
                    return Core::Err(Core::ErrorCode::AssetInvalidData);
                }
                rootMembership[rootNodeIndex] = 1u;
            }

            std::vector<std::uint8_t> primitiveReferenced(primitiveCount, 0u);
            for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
            {
                const AssetModelNodePayload& node = nodes[nodeIndex];
                for (const float component : node.LocalTransform)
                {
                    if (!std::isfinite(component))
                    {
                        return Core::Err(Core::ErrorCode::AssetInvalidData);
                    }
                }

                if (node.ParentNodeIndex != kInvalidAssetModelIndex
                    && node.ParentNodeIndex >= nodes.size())
                {
                    return Core::Err(Core::ErrorCode::OutOfRange);
                }

                for (const std::uint32_t childNodeIndex : node.ChildNodeIndices)
                {
                    if (childNodeIndex >= nodes.size())
                    {
                        return Core::Err(Core::ErrorCode::OutOfRange);
                    }
                }

                for (const std::uint32_t primitiveIndex : node.PrimitiveIndices)
                {
                    if (primitiveIndex >= primitiveCount)
                    {
                        return Core::Err(Core::ErrorCode::OutOfRange);
                    }
                    primitiveReferenced[primitiveIndex] = 1u;
                }
            }

            std::vector<std::uint8_t> childReferenceCount(nodes.size(), 0u);
            for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
            {
                for (const std::uint32_t childNodeIndex :
                     nodes[nodeIndex].ChildNodeIndices)
                {
                    if (childReferenceCount[childNodeIndex] != 0u)
                    {
                        return Core::Err(Core::ErrorCode::AssetInvalidData);
                    }
                    childReferenceCount[childNodeIndex] = 1u;
                    if (nodes[childNodeIndex].ParentNodeIndex != nodeIndex)
                    {
                        return Core::Err(Core::ErrorCode::AssetInvalidData);
                    }
                }
            }

            for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
            {
                const AssetModelNodePayload& node = nodes[nodeIndex];
                if (rootMembership[nodeIndex] != 0u)
                {
                    if (node.ParentNodeIndex != kInvalidAssetModelIndex
                        || childReferenceCount[nodeIndex] != 0u)
                    {
                        return Core::Err(Core::ErrorCode::AssetInvalidData);
                    }
                }
                else if (node.ParentNodeIndex == kInvalidAssetModelIndex
                    || childReferenceCount[nodeIndex] != 1u)
                {
                    return Core::Err(Core::ErrorCode::AssetInvalidData);
                }
            }

            for (const std::uint8_t referenced : primitiveReferenced)
            {
                if (referenced == 0u)
                {
                    return Core::Err(Core::ErrorCode::AssetInvalidData);
                }
            }

            std::vector<std::uint8_t> reachable(nodes.size(), 0u);
            std::vector<std::size_t> pendingNodes{};
            pendingNodes.reserve(nodes.size());
            for (const std::uint32_t rootNodeIndex : rootNodeIndices)
            {
                pendingNodes.push_back(rootNodeIndex);
            }
            while (!pendingNodes.empty())
            {
                const std::size_t nodeIndex = pendingNodes.back();
                pendingNodes.pop_back();
                if (reachable[nodeIndex] != 0u)
                {
                    return Core::Err(Core::ErrorCode::AssetInvalidData);
                }
                reachable[nodeIndex] = 1u;
                for (const std::uint32_t childNodeIndex :
                     nodes[nodeIndex].ChildNodeIndices)
                {
                    pendingNodes.push_back(childNodeIndex);
                }
            }

            for (const std::uint8_t isReachable : reachable)
            {
                if (isReachable == 0u)
                {
                    return Core::Err(Core::ErrorCode::AssetInvalidData);
                }
            }
            return Core::Ok();
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
        if (metadata.SourceKind != AssetTextureSourceKind::Generated &&
            !IsSupportedTextureImportFormat(metadata.SourceFormat))
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
        if (payload.RootNodeIndices.empty()
            || payload.Nodes.empty()
            || payload.Primitives.empty())
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

        if (Core::Result validNodeTopology = ValidateNodeTopology(
                payload.Nodes,
                payload.RootNodeIndices,
                payload.Primitives.size());
            !validNodeTopology.has_value())
        {
            return validNodeTopology;
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
