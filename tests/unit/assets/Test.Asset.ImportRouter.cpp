#include <gtest/gtest.h>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Error;

using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;

TEST(AssetImportRouter, RoutesObjMeshImportFromPath)
{
    const auto route =
        ResolveAssetImportRoute("/tmp/Models/Hero.OBJ");
    ASSERT_TRUE(route.has_value());
    EXPECT_EQ(route->Format, AssetFileFormat::OBJ);
    EXPECT_EQ(route->Operation, AssetRouteOperation::Import);
    EXPECT_EQ(route->PayloadKind, AssetPayloadKind::Mesh);
    EXPECT_EQ(route->CanonicalExtension, "obj");
    EXPECT_FALSE(route->PayloadHintRequired);
}

TEST(AssetImportRouter, PlyRequiresExplicitPayloadHint)
{
    const AssetRouteDiagnostic diagnostic =
        DiagnoseAssetImportRoute("scan.ply");
    EXPECT_EQ(diagnostic.Status, AssetRouteStatus::AmbiguousPayloadKind);
    EXPECT_EQ(diagnostic.Error, ErrorCode::InvalidArgument);
    EXPECT_EQ(diagnostic.Extension, "ply");

    const auto unresolved = ResolveAssetImportRoute("scan.ply");
    ASSERT_FALSE(unresolved.has_value());
    EXPECT_EQ(unresolved.error(), ErrorCode::InvalidArgument);

    const auto meshRoute =
        ResolveAssetImportRoute(
            "scan.ply",
            AssetRouteOperation::Import,
            AssetImportHint{.PayloadKind = AssetPayloadKind::Mesh});
    ASSERT_TRUE(meshRoute.has_value());
    EXPECT_EQ(meshRoute->PayloadKind, AssetPayloadKind::Mesh);
    EXPECT_TRUE(meshRoute->PayloadHintRequired);

    const auto cloudRoute =
        ResolveAssetImportRoute(
            "scan.ply",
            AssetRouteOperation::Import,
            AssetImportHint{.PayloadKind = AssetPayloadKind::PointCloud});
    ASSERT_TRUE(cloudRoute.has_value());
    EXPECT_EQ(cloudRoute->PayloadKind, AssetPayloadKind::PointCloud);
}

TEST(AssetImportRouter, RejectsMissingAndUnsupportedExtensions)
{
    const AssetRouteDiagnostic missing =
        DiagnoseAssetImportRoute("/tmp/no_extension");
    EXPECT_EQ(missing.Status, AssetRouteStatus::MissingExtension);
    EXPECT_EQ(missing.Error, ErrorCode::InvalidPath);

    const AssetRouteDiagnostic unsupported =
        DiagnoseAssetImportRoute("scene.usd");
    EXPECT_EQ(unsupported.Status, AssetRouteStatus::UnsupportedExtension);
    EXPECT_EQ(unsupported.Error, ErrorCode::AssetUnsupportedFormat);
    EXPECT_EQ(unsupported.Extension, "usd");

    const auto route = ResolveAssetImportRoute("scene.usd");
    ASSERT_FALSE(route.has_value());
    EXPECT_EQ(route.error(), ErrorCode::AssetUnsupportedFormat);
}

TEST(AssetImportRouter, RoutesModelSceneAndTextureImports)
{
    const auto gltf = ResolveAssetImportRoute("robot.gltf");
    ASSERT_TRUE(gltf.has_value());
    EXPECT_EQ(gltf->Format, AssetFileFormat::GLTF);
    EXPECT_EQ(gltf->PayloadKind, AssetPayloadKind::ModelScene);

    const auto glb = ResolveAssetImportRoute(".GLB");
    ASSERT_TRUE(glb.has_value());
    EXPECT_EQ(glb->Format, AssetFileFormat::GLB);
    EXPECT_EQ(glb->PayloadKind, AssetPayloadKind::ModelScene);

    const auto jpeg = ResolveAssetImportRoute("albedo.JPEG");
    ASSERT_TRUE(jpeg.has_value());
    EXPECT_EQ(jpeg->Format, AssetFileFormat::JPEG);
    EXPECT_EQ(jpeg->PayloadKind, AssetPayloadKind::Texture2D);
    EXPECT_EQ(jpeg->CanonicalExtension, "jpg");
}

TEST(AssetImportRouter, ExportRoutesRespectPromotedGeometrySupport)
{
    const auto stlExport =
        ResolveAssetImportRoute(
            "mesh.stl",
            AssetRouteOperation::Export,
            AssetImportHint{.PayloadKind = AssetPayloadKind::Mesh});
    ASSERT_TRUE(stlExport.has_value());
    EXPECT_EQ(stlExport->Format, AssetFileFormat::STL);
    EXPECT_EQ(stlExport->PayloadKind, AssetPayloadKind::Mesh);

    const auto pcdExport =
        ResolveAssetImportRoute(
            "points.pcd",
            AssetRouteOperation::Export,
            AssetImportHint{.PayloadKind = AssetPayloadKind::PointCloud});
    ASSERT_TRUE(pcdExport.has_value());
    EXPECT_EQ(pcdExport->Format, AssetFileFormat::PCD);
    EXPECT_EQ(pcdExport->PayloadKind, AssetPayloadKind::PointCloud);

    const AssetRouteDiagnostic offExport =
        DiagnoseAssetImportRoute(
            "mesh.off",
            AssetRouteOperation::Export,
            AssetImportHint{.PayloadKind = AssetPayloadKind::Mesh});
    EXPECT_EQ(offExport.Status, AssetRouteStatus::PayloadKindNotSupported);
    EXPECT_EQ(offExport.Error, ErrorCode::AssetUnsupportedFormat);
}

TEST(AssetImportRouter, PublishesDeterministicDebugNamesAndFormatTable)
{
    const auto formats = SupportedAssetFileFormats();
    ASSERT_GE(formats.size(), 18u);
    EXPECT_NE(FindAssetFileFormat("mesh.edges"), nullptr);
    EXPECT_NE(FindAssetFileFormat("mesh.edgelist"), nullptr);
    EXPECT_STREQ(DebugNameForAssetPayloadKind(AssetPayloadKind::PointCloud),
                 "PointCloud");
    EXPECT_STREQ(DebugNameForAssetFileFormat(AssetFileFormat::KTX), "KTX");
    EXPECT_STREQ(DebugNameForAssetRouteStatus(
                     AssetRouteStatus::UnsupportedExtension),
                 "UnsupportedExtension");
}
