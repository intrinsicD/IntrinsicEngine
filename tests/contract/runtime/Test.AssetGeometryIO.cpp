#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.AssetGeometryIO;
import Geometry.HalfedgeMesh.IO;
import Geometry.PointCloud.IO;

using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;

namespace
{
    struct TmpFile
    {
        std::filesystem::path Path;

        TmpFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << contents;
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

    [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }
}

TEST(RuntimeAssetGeometryIO, RegistersAllPromotedGeometryRoutes)
{
    AssetGeometryIOBridge bridge;
    const auto result = Extrinsic::Runtime::RegisterPromotedGeometryIOCallbacks(bridge);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::OBJ, AssetPayloadKind::Mesh));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::OFF, AssetPayloadKind::Mesh));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::STL, AssetPayloadKind::Mesh));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::PLY, AssetPayloadKind::Mesh));

    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::XYZ, AssetPayloadKind::PointCloud));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::PTS, AssetPayloadKind::PointCloud));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::XYZRGB, AssetPayloadKind::PointCloud));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::PCD, AssetPayloadKind::PointCloud));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::PLY, AssetPayloadKind::PointCloud));

    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::TGF, AssetPayloadKind::Graph));
    EXPECT_TRUE(bridge.HasImporter(AssetFileFormat::EdgeList, AssetPayloadKind::Graph));

    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::OBJ, AssetPayloadKind::Mesh));
    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::STL, AssetPayloadKind::Mesh));
    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::PLY, AssetPayloadKind::Mesh));
    EXPECT_FALSE(bridge.HasExporter(AssetFileFormat::OFF, AssetPayloadKind::Mesh));

    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::XYZ, AssetPayloadKind::PointCloud));
    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::PCD, AssetPayloadKind::PointCloud));
    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::PLY, AssetPayloadKind::PointCloud));
    EXPECT_FALSE(bridge.HasExporter(AssetFileFormat::PTS, AssetPayloadKind::PointCloud));
    EXPECT_FALSE(bridge.HasExporter(AssetFileFormat::XYZRGB, AssetPayloadKind::PointCloud));

    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::TGF, AssetPayloadKind::Graph));
    EXPECT_TRUE(bridge.HasExporter(AssetFileFormat::EdgeList, AssetPayloadKind::Graph));
}

TEST(RuntimeAssetGeometryIO, ImportsAndExportsMeshThroughPromotedCallbacks)
{
    TmpFile source(
        "assetio_promoted_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    TmpFile output("assetio_promoted_mesh_out.obj", "");

    AssetGeometryIOBridge bridge;
    ASSERT_TRUE(Extrinsic::Runtime::RegisterPromotedGeometryIOCallbacks(bridge).has_value());

    auto payload = bridge.Import(source.Path.string());
    ASSERT_TRUE(payload.has_value()) << static_cast<int>(payload.error());
    EXPECT_EQ(payload->PayloadKind, AssetPayloadKind::Mesh);
    EXPECT_EQ(payload->DebugTypeName, "Geometry::MeshIO::MeshIOResult");

    auto mesh = payload->Read<Geometry::MeshIO::MeshIOResult>();
    ASSERT_TRUE(mesh.has_value());
    EXPECT_NE((*mesh)->SourcePath.find("assetio_promoted_mesh.obj"), std::string::npos);

    const auto exported = bridge.Export(output.Path.string(), *payload);
    ASSERT_TRUE(exported.has_value()) << static_cast<int>(exported.error());
    const std::string exportedText = ReadText(output.Path);
    EXPECT_NE(exportedText.find("v 0"), std::string::npos);
    EXPECT_NE(exportedText.find("f "), std::string::npos);
}

TEST(RuntimeAssetGeometryIO, ImportsAndExportsPointCloudThroughPromotedCallbacks)
{
    TmpFile source(
        "assetio_promoted_cloud.xyz",
        "0 0 0\n"
        "1 2 3\n");
    TmpFile output("assetio_promoted_cloud_out.xyz", "");

    AssetGeometryIOBridge bridge;
    ASSERT_TRUE(Extrinsic::Runtime::RegisterPromotedGeometryIOCallbacks(bridge).has_value());

    auto payload = bridge.Import(source.Path.string());
    ASSERT_TRUE(payload.has_value()) << static_cast<int>(payload.error());
    EXPECT_EQ(payload->PayloadKind, AssetPayloadKind::PointCloud);
    EXPECT_EQ(payload->DebugTypeName, "Geometry::PointCloudIO::PointCloudIOResult");

    auto cloud = payload->Read<Geometry::PointCloudIO::PointCloudIOResult>();
    ASSERT_TRUE(cloud.has_value());
    EXPECT_NE((*cloud)->SourcePath.find("assetio_promoted_cloud.xyz"), std::string::npos);

    const auto exported = bridge.Export(output.Path.string(), *payload);
    ASSERT_TRUE(exported.has_value()) << static_cast<int>(exported.error());
    const std::string exportedText = ReadText(output.Path);
    EXPECT_NE(exportedText.find("0"), std::string::npos);
    EXPECT_NE(exportedText.find("1"), std::string::npos);
}
