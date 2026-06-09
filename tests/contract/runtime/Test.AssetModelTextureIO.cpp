#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTextureIOBridge;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.Runtime.AssetModelTextureIO;
import Geometry.HalfedgeMesh.IO;

using namespace Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace CoreIO = Extrinsic::Core::IO;
using Core::ErrorCode;
using Core::Expected;

namespace
{
    [[nodiscard]] std::vector<std::byte> Bytes(std::string_view text)
    {
        std::vector<std::byte> out;
        out.reserve(text.size());
        for (const char c : text)
        {
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        return out;
    }

    [[nodiscard]] std::vector<std::byte> TinyPngBytes()
    {
        static constexpr unsigned char bytes[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x04, 0x00, 0x00, 0x00, 0xB5, 0x1C, 0x0C,
            0x02, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41,
            0x54, 0x78, 0xDA, 0x63, 0xFC, 0xFF, 0x1F, 0x00,
            0x03, 0x03, 0x02, 0x00, 0xEF, 0xBF, 0xA7, 0xDB,
            0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
            0xAE, 0x42, 0x60, 0x82,
        };

        std::vector<std::byte> out;
        out.reserve(sizeof(bytes) / sizeof(bytes[0]));
        for (const unsigned char byte : bytes)
        {
            out.push_back(static_cast<std::byte>(byte));
        }
        return out;
    }

    template <class T>
    void AppendScalar(std::vector<std::byte>& out, const T value)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    [[nodiscard]] std::vector<std::byte> TriangleBufferBytes()
    {
        std::vector<std::byte> out;
        out.reserve(44u);
        const float positions[] = {
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
        };
        for (const float position : positions)
        {
            AppendScalar(out, position);
        }
        const std::uint16_t indices[] = {0u, 1u, 2u};
        for (const std::uint16_t index : indices)
        {
            AppendScalar(out, index);
        }
        out.push_back(std::byte{0});
        out.push_back(std::byte{0});
        return out;
    }

    [[nodiscard]] std::string TriangleGltfJson()
    {
        constexpr std::string_view pngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII=";
        return std::string(R"json({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "triangle.bin", "byteLength": 44}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963}
  ],
  "accessors": [
    {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "byteOffset": 0, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "images": [{"name": "EmbeddedAlbedo", "uri": "data:image/png;base64,)json")
            + std::string(pngBase64)
            + std::string(R"json(", "mimeType": "image/png"}],
  "textures": [{"source": 0}],
  "materials": [{
    "name": "Mat",
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.5, 0.25, 1.0],
      "metallicFactor": 0.25,
      "roughnessFactor": 0.75,
      "baseColorTexture": {"index": 0}
    }
  }],
  "meshes": [{"name": "TriMesh", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})json");
    }

    class FakeIOBackend final : public CoreIO::IIOBackend
    {
    public:
        void Add(std::string path, std::vector<std::byte> data)
        {
            Files[std::move(path)] = std::move(data);
        }

        void AddText(std::string path, std::string_view text)
        {
            Add(std::move(path), Bytes(text));
        }

        [[nodiscard]] Expected<CoreIO::IOReadResult> Read(
            const CoreIO::IORequest& request) override
        {
            const auto it = Files.find(request.Path);
            if (it == Files.end())
            {
                return Core::Err<CoreIO::IOReadResult>(ErrorCode::FileNotFound);
            }

            const std::vector<std::byte>& file = it->second;
            if (request.Offset > file.size())
            {
                return Core::Err<CoreIO::IOReadResult>(ErrorCode::OutOfRange);
            }

            const std::size_t available = file.size() - request.Offset;
            const std::size_t size = request.Size == 0u ? available : request.Size;
            if (size > available)
            {
                return Core::Err<CoreIO::IOReadResult>(ErrorCode::OutOfRange);
            }

            CoreIO::IOReadResult result{};
            result.Data.assign(
                file.begin() + static_cast<std::ptrdiff_t>(request.Offset),
                file.begin() + static_cast<std::ptrdiff_t>(request.Offset + size));
            return result;
        }

        [[nodiscard]] Core::Result Write(
            const CoreIO::IORequest& request,
            std::span<const std::byte> data) override
        {
            Files[request.Path] = std::vector<std::byte>(data.begin(), data.end());
            return Core::Ok();
        }

    private:
        std::unordered_map<std::string, std::vector<std::byte>> Files{};
    };
}

TEST(RuntimeAssetModelTextureIO, RegistersConcretePromotedModelAndTextureDecoders)
{
    AssetModelTextureIOBridge bridge;
    const auto result = Extrinsic::Runtime::RegisterPromotedModelTextureIOCallbacks(bridge);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(bridge.HasModelSceneImporter(AssetFileFormat::GLTF));
    EXPECT_TRUE(bridge.HasModelSceneImporter(AssetFileFormat::GLB));
    EXPECT_TRUE(bridge.HasTextureImporter(AssetFileFormat::PNG));
    EXPECT_TRUE(bridge.HasTextureImporter(AssetFileFormat::JPEG));
    EXPECT_TRUE(bridge.HasTextureImporter(AssetFileFormat::TGA));
    EXPECT_TRUE(bridge.HasTextureImporter(AssetFileFormat::BMP));
    EXPECT_TRUE(bridge.HasTextureImporter(AssetFileFormat::HDR));
    EXPECT_FALSE(bridge.HasTextureImporter(AssetFileFormat::KTX));
}

TEST(RuntimeAssetModelTextureIO, DecodesTextureBytesThroughPromotedBridge)
{
    FakeIOBackend backend;
    backend.Add("/textures/albedo.png", TinyPngBytes());

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(Extrinsic::Runtime::RegisterPromotedModelTextureIOCallbacks(bridge).has_value());

    auto texture = bridge.ImportTexture2D("/textures/albedo.png", backend);
    ASSERT_TRUE(texture.has_value()) << static_cast<int>(texture.error());
    EXPECT_EQ(texture->Metadata.Width, 1u);
    EXPECT_EQ(texture->Metadata.Height, 1u);
    EXPECT_EQ(texture->Metadata.Components, 4u);
    EXPECT_EQ(texture->Metadata.PixelFormat, AssetTexturePixelFormat::Rgba8Unorm);
    EXPECT_EQ(texture->Metadata.ColorSpace, AssetTextureColorSpace::SRGB);
    EXPECT_EQ(texture->Metadata.SourceFormat, AssetFileFormat::PNG);
    EXPECT_EQ(texture->PixelBytes.size(), 4u);
}

TEST(RuntimeAssetModelTextureIO, KtxTextureImportFailsClosedWithoutDecoder)
{
    FakeIOBackend backend;
    backend.AddText("/textures/compressed.ktx2", "ktx2");

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(Extrinsic::Runtime::RegisterPromotedModelTextureIOCallbacks(bridge).has_value());
    EXPECT_FALSE(bridge.HasTextureImporter(AssetFileFormat::KTX));

    auto texture = bridge.ImportTexture2D("/textures/compressed.ktx2", backend);
    ASSERT_FALSE(texture.has_value());
    EXPECT_EQ(texture.error(), ErrorCode::AssetUnsupportedFormat);
}

TEST(RuntimeAssetModelTextureIO, DecodesGltfSceneGeometryImagesAndMaterials)
{
    FakeIOBackend backend;
    backend.AddText("/scene/triangle.gltf", TriangleGltfJson());
    backend.Add("/scene/triangle.bin", TriangleBufferBytes());

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(Extrinsic::Runtime::RegisterPromotedModelTextureIOCallbacks(bridge).has_value());

    auto model = bridge.ImportModelScene("/scene/triangle.gltf", backend);
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());
    ASSERT_EQ(model->GeometryPayloads.size(), 1u);
    ASSERT_EQ(model->Primitives.size(), 1u);
    ASSERT_EQ(model->EmbeddedImages.size(), 1u);
    ASSERT_EQ(model->Materials.size(), 1u);

    const AssetModelPrimitivePayload& primitive = model->Primitives[0];
    EXPECT_EQ(primitive.GeometryKind, AssetPayloadKind::Mesh);
    EXPECT_EQ(primitive.GeometryPayloadIndex, 0u);
    EXPECT_EQ(primitive.MaterialIndex, 0u);
    EXPECT_EQ(primitive.VertexCount, 3u);
    EXPECT_EQ(primitive.IndexCount, 3u);

    auto meshPayload = model->GeometryPayloads[0].Read<Geometry::MeshIO::MeshIOResult>();
    ASSERT_TRUE(meshPayload.has_value());
    EXPECT_EQ((*meshPayload)->Vertices.Size(), 3u);
    EXPECT_EQ((*meshPayload)->Faces.Size(), 1u);
    auto faceVertices = (*meshPayload)->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    ASSERT_TRUE(faceVertices);
    ASSERT_EQ(faceVertices.Vector().size(), 1u);
    EXPECT_EQ(faceVertices.Vector()[0].size(), 3u);

    const AssetTexture2DPayload& image = model->EmbeddedImages[0];
    EXPECT_EQ(image.Metadata.SourceKind, AssetTextureSourceKind::Embedded);
    EXPECT_EQ(image.Metadata.SourceFormat, AssetFileFormat::PNG);
    EXPECT_EQ(image.Metadata.Width, 1u);
    EXPECT_EQ(image.Metadata.Height, 1u);
    EXPECT_EQ(image.PixelBytes.size(), 4u);

    const AssetModelMaterialPayload& material = model->Materials[0];
    EXPECT_EQ(material.Name, "Mat");
    EXPECT_EQ(material.BaseColorTexture.ImageIndex, 0u);
    EXPECT_FLOAT_EQ(material.BaseColorFactor[1], 0.5f);
    EXPECT_FLOAT_EQ(material.MetallicFactor, 0.25f);
    EXPECT_FLOAT_EQ(material.RoughnessFactor, 0.75f);

    bool sawExternalBuffer = false;
    for (const AssetModelExternalResourceDiagnostic& diagnostic : model->ExternalResourceDiagnostics)
    {
        if (diagnostic.Uri.find("triangle.bin") != std::string::npos
            && diagnostic.Status == AssetModelResourceStatus::Ready)
        {
            sawExternalBuffer = true;
        }
    }
    EXPECT_TRUE(sawExternalBuffer);
}

TEST(RuntimeAssetModelTextureIO, PropagatesGltfDecodeFailuresAsPromotedCoreErrors)
{
    FakeIOBackend backend;
    backend.AddText("/scene/bad.gltf", "{ not valid json }");

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(Extrinsic::Runtime::RegisterPromotedModelTextureIOCallbacks(bridge).has_value());

    auto model = bridge.ImportModelScene("/scene/bad.gltf", backend);
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error(), ErrorCode::AssetDecodeFailed);
}
