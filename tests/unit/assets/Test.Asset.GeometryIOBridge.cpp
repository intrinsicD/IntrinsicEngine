#include <gtest/gtest.h>

#include <expected>
#include <string>

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Error;

using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;
using Extrinsic::Core::Expected;

namespace
{
    struct FakeMesh
    {
        int Vertices = 0;
    };

    struct FakeCloud
    {
        int Points = 0;
    };

    AssetGeometryImportCallback EmptyImporter()
    {
        return {};
    }

}

TEST(AssetGeometryIOBridge, RoutesTypedImportCallbacksByResolvedRoute)
{
    AssetGeometryIOBridge bridge;
    const auto registered = bridge.RegisterTypedImporter<FakeMesh>(
        AssetFileFormat::OBJ,
        AssetPayloadKind::Mesh,
        [](const AssetGeometryIORequest& request) -> Expected<FakeMesh>
        {
            EXPECT_EQ(request.Route.Format, AssetFileFormat::OBJ);
            EXPECT_EQ(request.Route.PayloadKind, AssetPayloadKind::Mesh);
            EXPECT_EQ(request.Path, "/tmp/hero.obj");
            return FakeMesh{.Vertices = 7};
        },
        "FakeMesh");
    ASSERT_TRUE(registered.has_value());

    auto payload = bridge.Import("/tmp/hero.obj");
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->PayloadKind, AssetPayloadKind::Mesh);
    EXPECT_EQ(payload->DebugTypeName, "FakeMesh");

    auto mesh = payload->Read<FakeMesh>();
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ((*mesh)->Vertices, 7);
}

TEST(AssetGeometryIOBridge, RequiresHintsAndSelectsPlyDomainCallbacks)
{
    AssetGeometryIOBridge bridge;
    ASSERT_TRUE(bridge.RegisterTypedImporter<FakeMesh>(
        AssetFileFormat::PLY,
        AssetPayloadKind::Mesh,
        [](const AssetGeometryIORequest&) -> Expected<FakeMesh>
        {
            return FakeMesh{.Vertices = 3};
        }).has_value());
    ASSERT_TRUE(bridge.RegisterTypedImporter<FakeCloud>(
        AssetFileFormat::PLY,
        AssetPayloadKind::PointCloud,
        [](const AssetGeometryIORequest&) -> Expected<FakeCloud>
        {
            return FakeCloud{.Points = 5};
        }).has_value());

    auto ambiguous = bridge.Import("scan.ply");
    ASSERT_FALSE(ambiguous.has_value());
    EXPECT_EQ(ambiguous.error(), ErrorCode::InvalidArgument);

    auto meshPayload = bridge.Import(
        "scan.ply",
        AssetImportHint{.PayloadKind = AssetPayloadKind::Mesh});
    ASSERT_TRUE(meshPayload.has_value());
    EXPECT_EQ(meshPayload->PayloadKind, AssetPayloadKind::Mesh);
    ASSERT_TRUE(meshPayload->Read<FakeMesh>().has_value());

    auto cloudPayload = bridge.Import(
        "scan.ply",
        AssetImportHint{.PayloadKind = AssetPayloadKind::PointCloud});
    ASSERT_TRUE(cloudPayload.has_value());
    EXPECT_EQ(cloudPayload->PayloadKind, AssetPayloadKind::PointCloud);
    auto cloud = cloudPayload->Read<FakeCloud>();
    ASSERT_TRUE(cloud.has_value());
    EXPECT_EQ((*cloud)->Points, 5);
}

TEST(AssetGeometryIOBridge, RejectsMissingCallbacksAndInvalidRegistrations)
{
    AssetGeometryIOBridge bridge;

    auto missing = bridge.Import("mesh.obj");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), ErrorCode::AssetLoaderMissing);

    auto noCallback = bridge.RegisterImporter(
        AssetFileFormat::OBJ,
        AssetPayloadKind::Mesh,
        EmptyImporter());
    ASSERT_FALSE(noCallback.has_value());
    EXPECT_EQ(noCallback.error(), ErrorCode::InvalidArgument);

    auto nonGeometry = bridge.RegisterImporter(
        AssetFileFormat::GLTF,
        AssetPayloadKind::ModelScene,
        [](const AssetGeometryIORequest&) -> Expected<AssetGeometryPayload>
        {
            return AssetGeometryPayload{};
        });
    ASSERT_FALSE(nonGeometry.has_value());
    EXPECT_EQ(nonGeometry.error(), ErrorCode::InvalidArgument);

    auto unsupportedExport = bridge.RegisterExporter(
        AssetFileFormat::OFF,
        AssetPayloadKind::Mesh,
        [](const AssetGeometryIORequest&, const AssetGeometryPayload&)
            -> Extrinsic::Core::Result
        {
            return Extrinsic::Core::Ok();
        });
    ASSERT_FALSE(unsupportedExport.has_value());
    EXPECT_EQ(unsupportedExport.error(), ErrorCode::AssetUnsupportedFormat);
}

TEST(AssetGeometryIOBridge, PropagatesImporterCallbackErrors)
{
    AssetGeometryIOBridge bridge;
    ASSERT_TRUE(bridge.RegisterTypedImporter<FakeMesh>(
        AssetFileFormat::OBJ,
        AssetPayloadKind::Mesh,
        [](const AssetGeometryIORequest&) -> Expected<FakeMesh>
        {
            return std::unexpected(ErrorCode::AssetDecodeFailed);
        }).has_value());

    auto decoded = bridge.Import("broken.obj");
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), ErrorCode::AssetDecodeFailed);
}

TEST(AssetGeometryIOBridge, ExportsTypedPayloadAndRejectsTypeMismatch)
{
    AssetGeometryIOBridge bridge;
    int exportCalls = 0;
    ASSERT_TRUE(bridge.RegisterTypedExporter<FakeMesh>(
        AssetFileFormat::OBJ,
        AssetPayloadKind::Mesh,
        [&exportCalls](const AssetGeometryIORequest& request, const FakeMesh& mesh)
            -> Extrinsic::Core::Result
        {
            ++exportCalls;
            EXPECT_EQ(request.Route.Format, AssetFileFormat::OBJ);
            EXPECT_EQ(request.Route.Operation, AssetRouteOperation::Export);
            EXPECT_EQ(mesh.Vertices, 4);
            return Extrinsic::Core::Ok();
        }).has_value());

    auto meshPayload = AssetGeometryPayload::Make(
        AssetPayloadKind::Mesh,
        FakeMesh{.Vertices = 4},
        "FakeMesh");
    auto exported = bridge.Export("out.obj", meshPayload);
    ASSERT_TRUE(exported.has_value());
    EXPECT_EQ(exportCalls, 1);

    auto wrongTypePayload = AssetGeometryPayload::Make(
        AssetPayloadKind::Mesh,
        FakeCloud{.Points = 4},
        "FakeCloud");
    auto typeMismatch = bridge.Export("out.obj", wrongTypePayload);
    ASSERT_FALSE(typeMismatch.has_value());
    EXPECT_EQ(typeMismatch.error(), ErrorCode::AssetTypeMismatch);

    auto unsupportedRoute = bridge.Export("out.off", meshPayload);
    ASSERT_FALSE(unsupportedRoute.has_value());
    EXPECT_EQ(unsupportedRoute.error(), ErrorCode::AssetUnsupportedFormat);
}
