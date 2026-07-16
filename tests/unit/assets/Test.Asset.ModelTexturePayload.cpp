#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;

using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;
using Extrinsic::Core::Expected;

namespace
{
    struct FakeGeometryPayload
    {
        std::uint32_t VertexCount{0};
    };

    [[nodiscard]] AssetGeometryPayload MakeGeometryPayload(
        const AssetPayloadKind kind = AssetPayloadKind::Mesh,
        const std::uint32_t vertexCount = 3u)
    {
        return AssetGeometryPayload::Make(
            kind,
            FakeGeometryPayload{.VertexCount = vertexCount},
            "FakeGeometryPayload");
    }

    AssetTexture2DPayload MakeTexturePayload(
        const std::uint32_t width = 2u,
        const std::uint32_t height = 1u)
    {
        AssetTexture2DPayload payload{};
        payload.Metadata.Width = width;
        payload.Metadata.Height = height;
        payload.Metadata.Components = 4u;
        payload.Metadata.PixelFormat = AssetTexturePixelFormat::Rgba8Unorm;
        payload.Metadata.ColorSpace = AssetTextureColorSpace::SRGB;
        payload.Metadata.SourceKind = AssetTextureSourceKind::ExternalFile;
        payload.Metadata.SourceFormat = AssetFileFormat::PNG;
        payload.Metadata.SourcePath = "/tmp/albedo.png";
        payload.Metadata.DebugName = "albedo";
        payload.PixelBytes.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
            std::byte{0x7F});
        return payload;
    }

    AssetModelScenePayload MakeModelPayload()
    {
        AssetModelScenePayload payload{};
        payload.SourcePath = "/tmp/scene.glb";
        payload.GeometryPayloads.push_back(MakeGeometryPayload());
        payload.EmbeddedImages.push_back(MakeTexturePayload(1u, 1u));
        payload.Materials.push_back(AssetModelMaterialPayload{
            .Name = "mat",
            .BaseColorTexture = AssetModelTextureReference{.ImageIndex = 0u}});
        payload.Primitives.push_back(AssetModelPrimitivePayload{
            .Name = "triangle",
            .GeometryKind = AssetPayloadKind::Mesh,
            .GeometryPayloadIndex = 0u,
            .MaterialIndex = 0u,
            .VertexCount = 3u,
            .IndexCount = 3u});
        payload.RootNodeIndices.push_back(0u);
        payload.Nodes.push_back(AssetModelNodePayload{
            .Name = "root",
            .PrimitiveIndices = {0u}});
        payload.ExternalResourceDiagnostics.push_back(
            AssetModelExternalResourceDiagnostic{
                .ResourceKind = AssetModelResourceKind::Buffer,
                .Status = AssetModelResourceStatus::Ready,
                .Error = ErrorCode::Success,
                .Uri = "scene.bin"});
        return payload;
    }

    void AddHierarchyOnlyChild(AssetModelScenePayload& payload)
    {
        const auto childNodeIndex =
            static_cast<std::uint32_t>(payload.Nodes.size());
        payload.Nodes[0].ChildNodeIndices.push_back(childNodeIndex);
        payload.Nodes.push_back(AssetModelNodePayload{
            .Name = "group",
            .ParentNodeIndex = 0u});
    }

    struct TmpFile
    {
        std::filesystem::path Path;

        explicit TmpFile(std::string_view name)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << "asset-payload";
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };
}

TEST(AssetModelTexturePayload, TexturePayloadValidationAcceptsCpuPixelsAndMetadata)
{
    AssetTexture2DPayload payload = MakeTexturePayload();

    EXPECT_TRUE(ValidateAssetTexture2DPayload(payload).has_value());
    EXPECT_EQ(BytesPerPixel(payload.Metadata.PixelFormat), 4u);
    EXPECT_EQ(ComponentCountFor(payload.Metadata.PixelFormat), 4u);
    EXPECT_TRUE(IsSupportedTextureImportFormat(payload.Metadata.SourceFormat));
    EXPECT_STREQ(
        DebugNameForAssetTexturePixelFormat(payload.Metadata.PixelFormat),
        "Rgba8Unorm");
    EXPECT_STREQ(
        DebugNameForAssetTextureColorSpace(payload.Metadata.ColorSpace),
        "SRGB");
}

TEST(AssetModelTexturePayload, TexturePayloadValidationAcceptsGeneratedTexturesWithoutSourceFormat)
{
    AssetTexture2DPayload payload = MakeTexturePayload();
    payload.Metadata.SourceKind = AssetTextureSourceKind::Generated;
    payload.Metadata.SourceFormat = AssetFileFormat::Unknown;
    payload.Metadata.SourcePath = "generated://v:normal";

    EXPECT_TRUE(ValidateAssetTexture2DPayload(payload).has_value());
}

TEST(AssetModelTexturePayload, TexturePayloadValidationRejectsBadMetadata)
{
    AssetTexture2DPayload wrongSize = MakeTexturePayload();
    wrongSize.PixelBytes.pop_back();
    EXPECT_EQ(
        ValidateAssetTexture2DPayload(wrongSize).error(),
        ErrorCode::AssetInvalidData);

    AssetTexture2DPayload wrongComponents = MakeTexturePayload();
    wrongComponents.Metadata.Components = 3u;
    EXPECT_EQ(
        ValidateAssetTexture2DPayload(wrongComponents).error(),
        ErrorCode::InvalidFormat);

    AssetTexture2DPayload wrongFormat = MakeTexturePayload();
    wrongFormat.Metadata.SourceFormat = AssetFileFormat::OBJ;
    EXPECT_EQ(
        ValidateAssetTexture2DPayload(wrongFormat).error(),
        ErrorCode::AssetUnsupportedFormat);
}

TEST(AssetModelTexturePayload, ModelScenePayloadValidatesPrimitiveMaterialAndImageRefs)
{
    const AssetModelScenePayload payload = MakeModelPayload();
    constexpr std::array<float, 16> identity{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};

    EXPECT_TRUE(ValidateAssetModelScenePayload(payload).has_value());
    EXPECT_TRUE(IsSupportedModelSceneImportFormat(AssetFileFormat::GLB));
    EXPECT_TRUE(IsAssetModelSceneGeometryKind(payload.Primitives[0].GeometryKind));
    ASSERT_EQ(payload.GeometryPayloads.size(), 1u);
    EXPECT_EQ(payload.GeometryPayloads[0].PayloadKind, AssetPayloadKind::Mesh);
    EXPECT_TRUE(payload.GeometryPayloads[0].Read<FakeGeometryPayload>().has_value());
    ASSERT_EQ(payload.RootNodeIndices.size(), 1u);
    ASSERT_EQ(payload.Nodes.size(), 1u);
    EXPECT_EQ(payload.Nodes[0].ParentNodeIndex, kInvalidAssetModelIndex);
    EXPECT_EQ(payload.Nodes[0].LocalTransform, identity);
    EXPECT_EQ(payload.Nodes[0].PrimitiveIndices, std::vector<std::uint32_t>{0u});
    EXPECT_STREQ(
        DebugNameForAssetModelResourceStatus(
            payload.ExternalResourceDiagnostics[0].Status),
        "Ready");
}

TEST(AssetModelTexturePayload, ModelScenePayloadRejectsInvalidReferences)
{
    AssetModelScenePayload missingPrimitiveGeometry = MakeModelPayload();
    missingPrimitiveGeometry.Primitives[0].GeometryPayloadIndex =
        kInvalidAssetModelIndex;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(missingPrimitiveGeometry).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload outOfRangePrimitiveGeometry = MakeModelPayload();
    outOfRangePrimitiveGeometry.Primitives[0].GeometryPayloadIndex = 99u;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(outOfRangePrimitiveGeometry).error(),
        ErrorCode::OutOfRange);

    AssetModelScenePayload nonGeometryPrimitive = MakeModelPayload();
    nonGeometryPrimitive.Primitives[0].GeometryKind = AssetPayloadKind::Texture2D;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(nonGeometryPrimitive).error(),
        ErrorCode::AssetTypeMismatch);

    AssetModelScenePayload mismatchedGeometryKind = MakeModelPayload();
    mismatchedGeometryKind.Primitives[0].GeometryKind = AssetPayloadKind::PointCloud;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(mismatchedGeometryKind).error(),
        ErrorCode::AssetTypeMismatch);

    AssetModelScenePayload badMaterialReference = MakeModelPayload();
    badMaterialReference.Materials[0].BaseColorTexture.ImageIndex = 99u;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(badMaterialReference).error(),
        ErrorCode::OutOfRange);
}

TEST(AssetModelTexturePayload, ModelScenePayloadRequiresCompleteReferencedTopology)
{
    AssetModelScenePayload emptyRoots = MakeModelPayload();
    emptyRoots.RootNodeIndices.clear();
    EXPECT_EQ(
        ValidateAssetModelScenePayload(emptyRoots).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload emptyNodes = MakeModelPayload();
    emptyNodes.Nodes.clear();
    EXPECT_EQ(
        ValidateAssetModelScenePayload(emptyNodes).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload unreferencedPrototype = MakeModelPayload();
    unreferencedPrototype.Primitives.push_back(
        unreferencedPrototype.Primitives.front());
    EXPECT_EQ(
        ValidateAssetModelScenePayload(unreferencedPrototype).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload hierarchyOnlyNode = MakeModelPayload();
    AddHierarchyOnlyChild(hierarchyOnlyNode);
    EXPECT_TRUE(ValidateAssetModelScenePayload(hierarchyOnlyNode).has_value());

    AssetModelScenePayload sharedPrototype = MakeModelPayload();
    AddHierarchyOnlyChild(sharedPrototype);
    sharedPrototype.Nodes[1].PrimitiveIndices.push_back(0u);
    EXPECT_TRUE(ValidateAssetModelScenePayload(sharedPrototype).has_value());
}

TEST(AssetModelTexturePayload, ModelScenePayloadRejectsOutOfRangeNodeReferences)
{
    AssetModelScenePayload badRoot = MakeModelPayload();
    badRoot.RootNodeIndices[0] = 99u;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(badRoot).error(),
        ErrorCode::OutOfRange);

    AssetModelScenePayload badParent = MakeModelPayload();
    AddHierarchyOnlyChild(badParent);
    badParent.Nodes[1].ParentNodeIndex = 99u;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(badParent).error(),
        ErrorCode::OutOfRange);

    AssetModelScenePayload badChild = MakeModelPayload();
    badChild.Nodes[0].ChildNodeIndices.push_back(99u);
    EXPECT_EQ(
        ValidateAssetModelScenePayload(badChild).error(),
        ErrorCode::OutOfRange);

    AssetModelScenePayload badPrimitive = MakeModelPayload();
    badPrimitive.Nodes[0].PrimitiveIndices[0] = 99u;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(badPrimitive).error(),
        ErrorCode::OutOfRange);
}

TEST(AssetModelTexturePayload, ModelScenePayloadRejectsNonFiniteNodeTransforms)
{
    AssetModelScenePayload infiniteTransform = MakeModelPayload();
    infiniteTransform.Nodes[0].LocalTransform[5] =
        std::numeric_limits<float>::infinity();
    EXPECT_EQ(
        ValidateAssetModelScenePayload(infiniteTransform).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload nanTransform = MakeModelPayload();
    nanTransform.Nodes[0].LocalTransform[12] =
        std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(
        ValidateAssetModelScenePayload(nanTransform).error(),
        ErrorCode::AssetInvalidData);
}

TEST(AssetModelTexturePayload, ModelScenePayloadRejectsInconsistentOrDuplicateParentage)
{
    AssetModelScenePayload inconsistentParent = MakeModelPayload();
    AddHierarchyOnlyChild(inconsistentParent);
    inconsistentParent.Nodes[1].ParentNodeIndex = 1u;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(inconsistentParent).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload missingParentEdge = MakeModelPayload();
    AddHierarchyOnlyChild(missingParentEdge);
    missingParentEdge.Nodes[0].ChildNodeIndices.clear();
    EXPECT_EQ(
        ValidateAssetModelScenePayload(missingParentEdge).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload duplicateChild = MakeModelPayload();
    AddHierarchyOnlyChild(duplicateChild);
    duplicateChild.Nodes[0].ChildNodeIndices.push_back(1u);
    EXPECT_EQ(
        ValidateAssetModelScenePayload(duplicateChild).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload duplicateRoot = MakeModelPayload();
    duplicateRoot.RootNodeIndices.push_back(0u);
    EXPECT_EQ(
        ValidateAssetModelScenePayload(duplicateRoot).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload duplicateParent = MakeModelPayload();
    AddHierarchyOnlyChild(duplicateParent);
    duplicateParent.RootNodeIndices.push_back(2u);
    duplicateParent.Nodes.push_back(AssetModelNodePayload{
        .Name = "second-root",
        .ChildNodeIndices = {1u}});
    EXPECT_EQ(
        ValidateAssetModelScenePayload(duplicateParent).error(),
        ErrorCode::AssetInvalidData);
}

TEST(AssetModelTexturePayload, ModelScenePayloadRejectsCyclesAndUnreachableNodes)
{
    AssetModelScenePayload cycle = MakeModelPayload();
    cycle.Nodes.push_back(AssetModelNodePayload{
        .Name = "cycle-a",
        .ParentNodeIndex = 2u,
        .ChildNodeIndices = {2u}});
    cycle.Nodes.push_back(AssetModelNodePayload{
        .Name = "cycle-b",
        .ParentNodeIndex = 1u,
        .ChildNodeIndices = {1u}});
    EXPECT_EQ(
        ValidateAssetModelScenePayload(cycle).error(),
        ErrorCode::AssetInvalidData);

    AssetModelScenePayload unreachable = MakeModelPayload();
    unreachable.Nodes.push_back(AssetModelNodePayload{.Name = "orphan"});
    EXPECT_EQ(
        ValidateAssetModelScenePayload(unreachable).error(),
        ErrorCode::AssetInvalidData);
}

TEST(AssetModelTexturePayload, ExternalResourceDiagnosticsRequireConsistentErrors)
{
    AssetModelScenePayload failureDiagnostic = MakeModelPayload();
    failureDiagnostic.ExternalResourceDiagnostics[0] =
        AssetModelExternalResourceDiagnostic{
            .ResourceKind = AssetModelResourceKind::Image,
            .Status = AssetModelResourceStatus::FileReadFailed,
            .Error = ErrorCode::FileReadError,
            .Uri = "missing.png",
            .Message = "external image could not be read"};
    EXPECT_TRUE(ValidateAssetModelScenePayload(failureDiagnostic).has_value());

    failureDiagnostic.ExternalResourceDiagnostics[0].Error = ErrorCode::Success;
    EXPECT_EQ(
        ValidateAssetModelScenePayload(failureDiagnostic).error(),
        ErrorCode::InvalidArgument);
}

TEST(AssetModelTexturePayload, PayloadsLoadThroughAssetServiceAsDistinctTypes)
{
    TmpFile textureFile("asset_texture_payload_contract.png");
    TmpFile modelFile("asset_model_payload_contract.glb");
    AssetService service;

    auto textureId = service.Load<AssetTexture2DPayload>(
        textureFile.Path.string(),
        [](std::string_view, AssetId) -> Expected<AssetTexture2DPayload>
        {
            return MakeTexturePayload();
        });
    ASSERT_TRUE(textureId.has_value());

    auto modelId = service.Load<AssetModelScenePayload>(
        modelFile.Path.string(),
        [](std::string_view, AssetId) -> Expected<AssetModelScenePayload>
        {
            return MakeModelPayload();
        });
    ASSERT_TRUE(modelId.has_value());

    const auto texture = service.Read<AssetTexture2DPayload>(*textureId);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    EXPECT_EQ((*texture)[0].Metadata.Width, 2u);

    const auto model = service.Read<AssetModelScenePayload>(*modelId);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->size(), 1u);
    EXPECT_EQ((*model)[0].Primitives[0].VertexCount, 3u);

    auto wrongType = service.Read<AssetTexture2DPayload>(*modelId);
    ASSERT_FALSE(wrongType.has_value());
    EXPECT_EQ(wrongType.error(), ErrorCode::TypeMismatch);
}

TEST(AssetModelTexturePayload, RoutesMatchPromotedPayloadKinds)
{
    const auto modelRoute = ResolveAssetImportRoute("scene.gltf");
    ASSERT_TRUE(modelRoute.has_value());
    EXPECT_EQ(modelRoute->PayloadKind, AssetPayloadKind::ModelScene);
    EXPECT_TRUE(IsSupportedModelSceneImportFormat(modelRoute->Format));

    const auto textureRoute = ResolveAssetImportRoute("albedo.jpg");
    ASSERT_TRUE(textureRoute.has_value());
    EXPECT_EQ(textureRoute->PayloadKind, AssetPayloadKind::Texture2D);
    EXPECT_TRUE(IsSupportedTextureImportFormat(textureRoute->Format));
}

TEST(AssetModelTexturePayload, KtxRouteIsRecognizedButPayloadIsUnsupported)
{
    const auto ktxRoute = ResolveAssetImportRoute("environment.ktx2");
    ASSERT_TRUE(ktxRoute.has_value());
    EXPECT_EQ(ktxRoute->Format, AssetFileFormat::KTX);
    EXPECT_EQ(ktxRoute->PayloadKind, AssetPayloadKind::Texture2D);
    EXPECT_FALSE(IsSupportedTextureImportFormat(AssetFileFormat::KTX));

    AssetTexture2DPayload payload = MakeTexturePayload();
    payload.Metadata.SourceFormat = AssetFileFormat::KTX;
    EXPECT_EQ(
        ValidateAssetTexture2DPayload(payload).error(),
        ErrorCode::AssetUnsupportedFormat);
}
