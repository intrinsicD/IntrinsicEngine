#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.StableEntityLookup;

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace Graphics = Extrinsic::Graphics;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    class OneFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }

    class TempAssetFile final
    {
    public:
        TempAssetFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string{name})
        {
            std::ofstream out(Path, std::ios::binary);
            out << contents;
        }

        TempAssetFile(std::string_view name, std::span<const std::byte> bytes)
            : Path(std::filesystem::temp_directory_path() / std::string{name})
        {
            std::ofstream out(Path, std::ios::binary);
            for (const std::byte byte : bytes)
            {
                out.put(static_cast<char>(std::to_integer<unsigned char>(byte)));
            }
        }

        ~TempAssetFile()
        {
            std::error_code ignored;
            std::filesystem::remove(Path, ignored);
        }

        std::filesystem::path Path{};
    };

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
        out.reserve(std::size(bytes));
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
  "buffers": [{"uri": "assetio004_triangle.bin", "byteLength": 44}],
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

    [[nodiscard]] std::size_t CountEntitiesWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        std::size_t count = 0u;
        auto& raw = registry.Raw();
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
            {
                ++count;
            }
        });
        return count;
    }

    [[nodiscard]] std::optional<ECS::EntityHandle> FindFirstEntityWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        std::optional<ECS::EntityHandle> found{};
        auto& raw = registry.Raw();
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (found.has_value() || !raw.all_of<Sel::SelectableTag>(entity))
            {
                return;
            }
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
            {
                found = entity;
            }
        });
        return found;
    }

    void ExpectMeshVertexNormals(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity,
        const std::vector<glm::vec3>& expected)
    {
        auto& raw = registry.Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        ASSERT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        ASSERT_NE(view.VertexSource, nullptr);

        auto normals = view.VertexSource->Properties.Get<glm::vec3>(
            GS::PropertyNames::kNormal);
        ASSERT_TRUE(normals.IsValid());
        ASSERT_EQ(normals.Vector().size(), expected.size());
        for (std::size_t i = 0u; i < expected.size(); ++i)
        {
            EXPECT_EQ(normals[i], expected[i]);
        }
    }

    void ExpectGeneratedNormalTextureBinding(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::string_view meshPath)
    {
        const std::string generatedPath =
            Runtime::BuildGeneratedTextureAssetPath(
                meshPath,
                0u,
                "normal",
                "v:normal");
        EXPECT_TRUE(engine.GetAssetService().PathIndexContains(generatedPath));

        const std::uint32_t stableId =
            Runtime::StableEntityLookup::ToRenderId(entity);
        const auto binding =
            engine.GetMaterialTextureAssetBindingsForTest(stableId);
        ASSERT_TRUE(binding.has_value());
        EXPECT_FALSE(binding->Albedo.IsValid());
        ASSERT_TRUE(binding->Normal.IsValid());

        auto boundPath = engine.GetAssetService().GetPath(binding->Normal);
        ASSERT_TRUE(boundPath.has_value()) << static_cast<int>(boundPath.error());
        EXPECT_EQ(*boundPath, generatedPath);
        auto payload = engine.GetAssetService().Read<Assets::AssetTexture2DPayload>(
            binding->Normal);
        ASSERT_TRUE(payload.has_value()) << static_cast<int>(payload.error());
        ASSERT_EQ(payload->size(), 1u);
        EXPECT_EQ((*payload)[0].Metadata.Width, 64u);
        EXPECT_EQ((*payload)[0].Metadata.Height, 64u);
        EXPECT_EQ((*payload)[0].Metadata.PixelFormat,
                  Assets::AssetTexturePixelFormat::Rgba8Unorm);
        EXPECT_EQ((*payload)[0].Metadata.ColorSpace,
                  Assets::AssetTextureColorSpace::Linear);
        EXPECT_EQ((*payload)[0].Metadata.SourceKind,
                  Assets::AssetTextureSourceKind::Generated);
        EXPECT_EQ((*payload)[0].PixelBytes.size(), 64u * 64u * 4u);
        EXPECT_EQ(engine.GetGpuAssetCache().GetState(binding->Normal),
                  Graphics::GpuAssetState::NotRequested);
    }

    void ExpectNoGeneratedNormalTextureBinding(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::string_view meshPath)
    {
        const std::string generatedPath =
            Runtime::BuildGeneratedTextureAssetPath(
                meshPath,
                0u,
                "normal",
                "v:normal");
        EXPECT_FALSE(engine.GetAssetService().PathIndexContains(generatedPath));

        const std::uint32_t stableId =
            Runtime::StableEntityLookup::ToRenderId(entity);
        EXPECT_FALSE(engine.GetMaterialTextureAssetBindingsForTest(stableId).has_value());
    }
}

TEST(RuntimeAssetImportFormatCoverage, DirectObjImportPreservesVertexNormalsInGeometrySources)
{
    TempAssetFile meshFile(
        "assetio041_normals.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vn 1 0 0\n"
        "vn 0 1 0\n"
        "vn 0 0 -1\n"
        "f 1//1 2//2 3//3\n");

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 0u);
    EXPECT_EQ(imported->GeneratedTextureUploadRequests, 0u);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, -1.0f},
        });
    ExpectNoGeneratedNormalTextureBinding(engine, *meshEntity, meshFile.Path.string());

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, DirectObjImportBakesGeneratedNormalTextureFromAuthoredVertexNormals)
{
    TempAssetFile meshFile(
        "assetio007_normals_texcoords.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 1 0 0\n"
        "vn 0 1 0\n"
        "vn 0 0 -1\n"
        "f 1/1/1 2/2/2 3/3/3\n");

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 1u);
    EXPECT_EQ(imported->TextureUploadRequests,
              imported->GeneratedTextureUploadRequests);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, -1.0f},
        });
    ExpectGeneratedNormalTextureBinding(engine, *meshEntity, meshFile.Path.string());

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, DirectObjImportComputesVertexNormalsWhenMissing)
{
    TempAssetFile meshFile(
        "assetio041_no_normals.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 0u);
    EXPECT_EQ(imported->GeneratedTextureUploadRequests, 0u);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        });
    ExpectNoGeneratedNormalTextureBinding(engine, *meshEntity, meshFile.Path.string());

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, DirectObjImportComputesAndBakesGeneratedNormalTextureWhenMissingNormals)
{
    TempAssetFile meshFile(
        "assetio007_no_normals_texcoords.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n");

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 1u);
    EXPECT_EQ(imported->TextureUploadRequests,
              imported->GeneratedTextureUploadRequests);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        });
    ExpectGeneratedNormalTextureBinding(engine, *meshEntity, meshFile.Path.string());

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, RepresentativePromotedFormatsMaterializeDeterministically)
{
    const std::vector<std::byte> pngBytes = TinyPngBytes();
    const std::vector<std::byte> binBytes = TriangleBufferBytes();

    TempAssetFile meshFile(
        "assetio004_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    TempAssetFile graphFile(
        "assetio004_graph.tgf",
        "1 0 0 0 first\n"
        "2 1 0 0 second\n"
        "#\n"
        "1 2 1.0 edge\n");
    TempAssetFile cloudFile(
        "assetio004_cloud.ply",
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "2 0 0\n");
    TempAssetFile modelBin(
        "assetio004_triangle.bin",
        std::span<const std::byte>(binBytes.data(), binBytes.size()));
    TempAssetFile modelFile("assetio004_triangle.gltf", TriangleGltfJson());
    TempAssetFile textureFile(
        "assetio004_albedo.png",
        std::span<const std::byte>(pngBytes.data(), pngBytes.size()));

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto mesh = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);

    auto graph = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = graphFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Graph,
    });
    ASSERT_TRUE(graph.has_value()) << static_cast<int>(graph.error());
    EXPECT_EQ(graph->PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_EQ(graph->PrimitiveEntitiesCreated, 1u);

    auto cloud = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = cloudFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::PointCloud,
    });
    ASSERT_TRUE(cloud.has_value()) << static_cast<int>(cloud.error());
    EXPECT_EQ(cloud->PayloadKind, Assets::AssetPayloadKind::PointCloud);
    EXPECT_EQ(cloud->PrimitiveEntitiesCreated, 1u);

    auto model = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = modelFile.Path.string(),
    });
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());
    EXPECT_EQ(model->PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_TRUE(model->MaterializedModelScene);
    EXPECT_EQ(model->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(model->EmbeddedTextureAssetsCreated, 1u);
    EXPECT_EQ(model->TextureUploadRequests, 0u);

    auto texture = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = textureFile.Path.string(),
    });
    ASSERT_TRUE(texture.has_value()) << static_cast<int>(texture.error());
    EXPECT_EQ(texture->PayloadKind, Assets::AssetPayloadKind::Texture2D);
    EXPECT_FALSE(texture->RequestedTextureUpload);
    EXPECT_EQ(texture->TextureUploadRequests, 0u);
    EXPECT_EQ(
        engine.GetGpuAssetCache().GetState(texture->Asset),
        Extrinsic::Graphics::GpuAssetState::NotRequested);

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 2u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Graph), 1u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 1u);

    auto& raw = engine.GetScene().Raw();
    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderSurface>(*meshEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*meshEntity));

    const std::optional<ECS::EntityHandle> graphEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Graph);
    ASSERT_TRUE(graphEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderEdges>(*graphEntity));
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*graphEntity));

    const std::optional<ECS::EntityHandle> cloudEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::PointCloud);
    ASSERT_TRUE(cloudEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*cloudEntity));

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::Texture2D);

    engine.Shutdown();
}
