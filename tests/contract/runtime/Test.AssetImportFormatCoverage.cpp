#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.HalfedgeMesh.IO;

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace Graphics = Extrinsic::Graphics;
namespace Runtime = Extrinsic::Runtime;
namespace Tasks = Extrinsic::Core::Tasks;

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

    class WaitForConditionApplication final : public Runtime::IApplication
    {
    public:
        explicit WaitForConditionApplication(
            std::function<bool(Runtime::Engine&)> ready,
            std::uint32_t maxFrames = 512u)
            : m_Ready(std::move(ready))
            , m_MaxFrames(maxFrames)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_ObservedFrames;
            if ((m_Ready && m_Ready(engine)) || m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::function<bool(Runtime::Engine&)> m_Ready{};
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    void WaitUntilTrueFor(const std::atomic<bool>& flag,
                          const std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!flag.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void WaitUntilTrue(const std::atomic<bool>& flag)
    {
        WaitUntilTrueFor(flag, std::chrono::seconds(2));
    }

    struct BlockingReadBackendState
    {
        std::atomic<bool> ReadStarted{false};
        std::atomic<bool> ReleaseRead{false};
        std::atomic<bool> ReadFinished{false};
    };

    class BlockingReadIOBackend final : public Core::IO::IIOBackend
    {
    public:
        explicit BlockingReadIOBackend(
            std::shared_ptr<BlockingReadBackendState> state)
            : m_State(std::move(state))
        {
        }

        Core::Expected<Core::IO::IOReadResult> Read(
            const Core::IO::IORequest& request) override
        {
            m_State->ReadStarted.store(true, std::memory_order_release);
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!m_State->ReleaseRead.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            auto result = m_File.Read(request);
            m_State->ReadFinished.store(true, std::memory_order_release);
            return result;
        }

        Core::Result Write(const Core::IO::IORequest& request,
                           std::span<const std::byte> data) override
        {
            return m_File.Write(request, data);
        }

    private:
        std::shared_ptr<BlockingReadBackendState> m_State;
        Core::IO::FileIOBackend m_File{};
    };

    class SlowImportProbeApplication final : public Runtime::IApplication
    {
    public:
        explicit SlowImportProbeApplication(
            std::shared_ptr<BlockingReadBackendState> state)
            : m_State(std::move(state))
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_Frames;
            if (!m_ReleasedRead && engine.GetLastAssetImportEvent().has_value())
                m_EventArrivedBeforeRelease = true;

            if (!m_ReleasedRead &&
                m_State->ReadStarted.load(std::memory_order_acquire) &&
                !m_State->ReadFinished.load(std::memory_order_acquire))
            {
                m_FrameAdvancedWhileReadBlocked = true;
                m_State->ReleaseRead.store(true, std::memory_order_release);
                m_ReleasedRead = true;
            }

            if (m_ReleasedRead && engine.GetLastAssetImportEvent().has_value())
            {
                engine.RequestExit();
                return;
            }

            if (m_Frames >= 256u)
            {
                m_TimedOut = true;
                m_State->ReleaseRead.store(true, std::memory_order_release);
                engine.RequestExit();
            }
        }
        void OnShutdown(Runtime::Engine&) override {}

        [[nodiscard]] bool FrameAdvancedWhileReadBlocked() const noexcept
        {
            return m_FrameAdvancedWhileReadBlocked;
        }
        [[nodiscard]] bool EventArrivedBeforeRelease() const noexcept
        {
            return m_EventArrivedBeforeRelease;
        }
        [[nodiscard]] bool TimedOut() const noexcept { return m_TimedOut; }
        [[nodiscard]] std::uint32_t Frames() const noexcept { return m_Frames; }

    private:
        std::shared_ptr<BlockingReadBackendState> m_State;
        std::uint32_t m_Frames{0u};
        bool m_FrameAdvancedWhileReadBlocked{false};
        bool m_EventArrivedBeforeRelease{false};
        bool m_ReleasedRead{false};
        bool m_TimedOut{false};
    };

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

    void ExpectMeshVertexTexcoordsFinite(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        ASSERT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        ASSERT_NE(view.VertexSource, nullptr);

        auto texcoords = view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
        ASSERT_TRUE(texcoords.IsValid());
        ASSERT_EQ(texcoords.Vector().size(), view.VerticesAlive());
        for (const glm::vec2 texcoord : texcoords.Vector())
        {
            EXPECT_TRUE(std::isfinite(texcoord.x));
            EXPECT_TRUE(std::isfinite(texcoord.y));
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
        EXPECT_EQ(binding->NormalSpace,
                  Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal);

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

    [[nodiscard]] bool HasGeneratedNormalTextureBinding(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity) noexcept
    {
        const std::uint32_t stableId =
            Runtime::StableEntityLookup::ToRenderId(entity);
        const auto binding =
            engine.GetMaterialTextureAssetBindingsForTest(stableId);
        return binding.has_value() && binding->Normal.IsValid();
    }

    void ExpectMeshLacksVertexProperty(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity,
        const std::string_view propertyName)
    {
        auto& raw = registry.Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        ASSERT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        ASSERT_NE(view.VertexSource, nullptr);
        EXPECT_FALSE(view.VertexSource->Properties.Exists(propertyName));
    }

    [[nodiscard]] bool MeshHasVertexProperty(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::string_view propertyName)
    {
        if (!engine.GetScene().IsValid(entity))
        {
            return false;
        }

        auto& raw = engine.GetScene().Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        return view.Valid() &&
            view.ActiveDomain == GS::Domain::Mesh &&
            view.VertexSource != nullptr &&
            view.VertexSource->Properties.Exists(propertyName);
    }

    [[nodiscard]] bool DirectMeshPostProcessReady(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity)
    {
        return MeshHasVertexProperty(engine, entity, "v:texcoord") &&
            MeshHasVertexProperty(engine, entity, "v:normal") &&
            HasGeneratedNormalTextureBinding(engine, entity);
    }

    void ExpectMaterialDrivenImportedSurface(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity)
    {
        auto& raw = engine.GetScene().Raw();
        ASSERT_TRUE(raw.all_of<G::VisualizationConfig>(entity));
        const G::VisualizationConfig& visualization =
            raw.get<G::VisualizationConfig>(entity);
        EXPECT_EQ(visualization.Source, G::VisualizationConfig::ColorSource::Material);

        Runtime::RenderExtractionCache extraction;
        const Runtime::RuntimeRenderExtractionStats stats =
            extraction.ExtractAndSubmit(
                engine.GetScene(),
                engine.GetRenderer(),
                &engine.GetGpuAssetCache());
        EXPECT_EQ(stats.MeshGeometryUploads, 1u);

        const auto sidecar = extraction.FindRenderableSidecarForTest(
            Runtime::StableEntityLookup::ToRenderId(entity));
        ASSERT_TRUE(sidecar.has_value());
        ASSERT_TRUE(sidecar->HasMaterialLease);
        const std::uint32_t baseSlot =
            engine.GetRenderer().GetMaterialSystem().GetMaterialSlot(
                sidecar->MaterialHandle);
        EXPECT_EQ(sidecar->MaterialSlot, baseSlot);
        EXPECT_EQ(
            engine.GetRenderer().GetVisualizationSyncSystem().GetOverrideLeaseCount(),
            0u);

        extraction.Shutdown(engine.GetRenderer());
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

    std::optional<ECS::EntityHandle> meshEntity{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            }));
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 0u);
    EXPECT_EQ(imported->TextureUploadRequests, 0u);
    EXPECT_EQ(imported->GeneratedTextureUploadRequests, 0u);

    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, -1.0f},
        });
    ExpectMeshLacksVertexProperty(engine.GetScene(), *meshEntity, "v:texcoord");
    EXPECT_FALSE(HasGeneratedNormalTextureBinding(engine, *meshEntity));

    engine.Run();

    EXPECT_TRUE(engine.GetScene().IsValid(*meshEntity));
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));
    ExpectMeshVertexTexcoordsFinite(engine.GetScene(), *meshEntity);
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

TEST(RuntimeAssetImportFormatCoverage, DirectObjImportDefaultsToMaterialDrivenShading)
{
    TempAssetFile meshFile(
        "assetio041_material_shading.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "f 1//1 2//2 3//3\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMaterialDrivenImportedSurface(engine, *meshEntity);

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, DirectImportCompletesIngestStateMachineRecord)
{
    TempAssetFile meshFile(
        "assetio101_direct_ingest.obj",
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

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Request.Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);
    EXPECT_EQ(records[0].Request.Path, meshFile.Path.string());
    EXPECT_EQ(records[0].Request.PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[0].Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(records[0].Result.has_value());
    EXPECT_EQ(records[0].Result->Asset, imported->Asset);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, ReimportExistingMeshReloadsAssetWithoutDuplicatingSceneEntities)
{
    TempAssetFile meshFile(
        "assetio101_reimport_mesh.obj",
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
    ASSERT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const auto firstTicket =
        engine.GetAssetService().GetPayloadTicket(imported->Asset);
    ASSERT_TRUE(firstTicket.has_value());

    {
        std::ofstream out(meshFile.Path, std::ios::binary | std::ios::trunc);
        out << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "v 0 0 1\n"
               "f 1 2 3\n"
               "f 1 3 4\n";
    }

    auto reimported = engine.ReimportAsset(Runtime::RuntimeAssetReimportRequest{
        .Asset = imported->Asset,
    });
    ASSERT_TRUE(reimported.has_value()) << static_cast<int>(reimported.error());
    EXPECT_EQ(reimported->Asset, imported->Asset);
    EXPECT_EQ(reimported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(reimported->PrimitiveEntitiesCreated, 0u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);

    const auto secondTicket =
        engine.GetAssetService().GetPayloadTicket(imported->Asset);
    ASSERT_TRUE(secondTicket.has_value());
    EXPECT_EQ(secondTicket->slot, firstTicket->slot);
    EXPECT_GT(secondTicket->generation, firstTicket->generation);

    const auto meshPayload =
        engine.GetAssetService().Read<Geometry::MeshIO::MeshIOResult>(
            imported->Asset);
    ASSERT_TRUE(meshPayload.has_value());
    ASSERT_EQ(meshPayload->size(), 1u);
    EXPECT_EQ((*meshPayload)[0].Vertices.Size(), 4u);
    EXPECT_EQ((*meshPayload)[0].Faces.Size(), 2u);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[1].Request.Source,
              Runtime::RuntimeAssetIngestSource::Reimport);
    EXPECT_EQ(records[1].Request.ExistingAsset, imported->Asset);
    EXPECT_EQ(records[1].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[1].Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(records[1].Result.has_value());
    EXPECT_EQ(records[1].Result->Asset, imported->Asset);
    EXPECT_EQ(records[1].Result->PrimitiveEntitiesCreated, 0u);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->Asset, imported->Asset);

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, ImportAssetFromPathDoesNotWaitForUnrelatedSchedulerWork)
{
    TempAssetFile meshFile(
        "assetio101_import_without_global_scheduler_wait.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Core::Config::EngineConfig config = HeadlessConfig();
    config.Simulation.WorkerThreadCount = 1u;
    Runtime::Engine engine(config, std::make_unique<OneFrameApplication>());
    engine.Initialize();

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> blockerFinished{false};
    Tasks::Scheduler::Dispatch([&]()
    {
        blockerStarted.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        blockerFinished.store(true, std::memory_order_release);
    });
    WaitUntilTrue(blockerStarted);
    if (!blockerStarted.load(std::memory_order_acquire))
    {
        engine.Shutdown();
        FAIL() << "scheduler sentinel did not start";
    }

    const auto before = std::chrono::steady_clock::now();
    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    const auto after = std::chrono::steady_clock::now();
    const bool blockerFinishedBeforeImportReturned =
        blockerFinished.load(std::memory_order_acquire);
    const std::size_t meshEntityCount =
        CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh);

    WaitUntilTrue(blockerFinished);
    engine.Shutdown();

    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    ASSERT_TRUE(imported->Asset.IsValid());
    EXPECT_EQ(imported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(meshEntityCount, 1u);
    EXPECT_FALSE(blockerFinishedBeforeImportReturned);
    EXPECT_LT(after - before, std::chrono::milliseconds(150));
}

TEST(RuntimeAssetImportFormatCoverage, ReimportInvalidAssetReportsDeterministicIngestDiagnostic)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto reimported = engine.ReimportAsset(Runtime::RuntimeAssetReimportRequest{
        .Asset = Assets::AssetId{999u, 1u},
    });
    EXPECT_FALSE(reimported.has_value());
    EXPECT_EQ(reimported.error(), Core::ErrorCode::ResourceNotFound);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Request.Source,
              Runtime::RuntimeAssetIngestSource::Reimport);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Failed);
    EXPECT_EQ(records[0].Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::InvalidReimportTarget);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_FALSE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->Error, Core::ErrorCode::ResourceNotFound);
    EXPECT_EQ(lastEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::InvalidReimportTarget);

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

    std::optional<ECS::EntityHandle> meshEntity{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            }));
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 0u);
    EXPECT_EQ(imported->TextureUploadRequests, 0u);
    EXPECT_EQ(imported->GeneratedTextureUploadRequests, 0u);

    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, -1.0f},
        });
    EXPECT_FALSE(HasGeneratedNormalTextureBinding(engine, *meshEntity));

    engine.Run();

    EXPECT_TRUE(engine.GetScene().IsValid(*meshEntity));
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));
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

    std::optional<ECS::EntityHandle> meshEntity{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            }));
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 0u);
    EXPECT_EQ(imported->TextureUploadRequests, 0u);
    EXPECT_EQ(imported->GeneratedTextureUploadRequests, 0u);

    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        });
    ExpectMeshLacksVertexProperty(engine.GetScene(), *meshEntity, "v:texcoord");
    EXPECT_FALSE(HasGeneratedNormalTextureBinding(engine, *meshEntity));

    engine.Run();

    EXPECT_TRUE(engine.GetScene().IsValid(*meshEntity));
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));
    ExpectMeshVertexTexcoordsFinite(engine.GetScene(), *meshEntity);
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

    std::optional<ECS::EntityHandle> meshEntity{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            }));
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->GeneratedTextureAssetsCreated, 0u);
    EXPECT_EQ(imported->TextureUploadRequests, 0u);
    EXPECT_EQ(imported->GeneratedTextureUploadRequests, 0u);

    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    ExpectMeshVertexNormals(
        engine.GetScene(),
        *meshEntity,
        {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        });
    EXPECT_FALSE(HasGeneratedNormalTextureBinding(engine, *meshEntity));

    engine.Run();

    EXPECT_TRUE(engine.GetScene().IsValid(*meshEntity));
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));
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

TEST(RuntimeAssetImportFormatCoverage, DroppedModelSceneAndTextureImportThroughStreamingQueue)
{
    const std::vector<std::byte> pngBytes = TinyPngBytes();
    const std::vector<std::byte> binBytes = TriangleBufferBytes();

    TempAssetFile modelBin(
        "assetio004_triangle.bin",
        std::span<const std::byte>(binBytes.data(), binBytes.size()));
    TempAssetFile modelFile(
        "assetio142_drop_triangle.gltf",
        TriangleGltfJson());
    TempAssetFile textureFile(
        "assetio142_drop_albedo.png",
        std::span<const std::byte>(pngBytes.data(), pngBytes.size()));

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [](Runtime::Engine& runningEngine)
            {
                const Runtime::RuntimeAssetImportQueueSnapshot queue =
                    runningEngine.GetAssetImportQueueSnapshot();
                return queue.Entries.size() == 2u &&
                    queue.ActiveCount == 0u &&
                    queue.TerminalCount == 2u;
            },
            256u));
    engine.Initialize();

    const std::vector<std::string> droppedPaths{
        modelFile.Path.string(),
        textureFile.Path.string(),
    };
    engine.ImportDroppedFilePaths(droppedPaths);

    EXPECT_FALSE(engine.GetLastAssetImportEvent().has_value());
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 0u);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        engine.GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 2u);
    EXPECT_EQ(queue.Entries[0].SourcePath, modelFile.Path.string());
    EXPECT_EQ(queue.Entries[0].PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_EQ(queue.Entries[0].Stage,
              Runtime::RuntimeAssetImportQueueStage::Decoding);
    EXPECT_TRUE(queue.Entries[0].CanCancel);
    EXPECT_EQ(queue.Entries[1].SourcePath, textureFile.Path.string());
    EXPECT_EQ(queue.Entries[1].PayloadKind, Assets::AssetPayloadKind::Texture2D);
    EXPECT_EQ(queue.Entries[1].Stage,
              Runtime::RuntimeAssetImportQueueStage::Decoding);
    EXPECT_TRUE(queue.Entries[1].CanCancel);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";
    engine.Run();

    queue = engine.GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 0u);
    EXPECT_EQ(queue.TerminalCount, 2u);
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);
    EXPECT_EQ(queue.Entries[1].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 2u);

    const auto modelRecord = std::find_if(
        records.begin(),
        records.end(),
        [&](const Runtime::RuntimeAssetIngestRecord& record)
        {
            return record.Request.Path == modelFile.Path.string();
        });
    ASSERT_NE(modelRecord, records.end());
    EXPECT_EQ(modelRecord->Request.Source,
              Runtime::RuntimeAssetIngestSource::DroppedFile);
    EXPECT_EQ(modelRecord->Request.PayloadKind,
              Assets::AssetPayloadKind::ModelScene);
    EXPECT_EQ(modelRecord->Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(modelRecord->Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(modelRecord->Result.has_value());
    EXPECT_EQ(modelRecord->Result->PayloadKind,
              Assets::AssetPayloadKind::ModelScene);
    EXPECT_TRUE(modelRecord->Result->MaterializedModelScene);
    EXPECT_EQ(modelRecord->Result->PrimitiveEntitiesCreated, 1u);

    const auto textureRecord = std::find_if(
        records.begin(),
        records.end(),
        [&](const Runtime::RuntimeAssetIngestRecord& record)
        {
            return record.Request.Path == textureFile.Path.string();
        });
    ASSERT_NE(textureRecord, records.end());
    EXPECT_EQ(textureRecord->Request.Source,
              Runtime::RuntimeAssetIngestSource::DroppedFile);
    EXPECT_EQ(textureRecord->Request.PayloadKind,
              Assets::AssetPayloadKind::Texture2D);
    EXPECT_EQ(textureRecord->Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(textureRecord->Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(textureRecord->Result.has_value());
    EXPECT_EQ(textureRecord->Result->PayloadKind,
              Assets::AssetPayloadKind::Texture2D);
    EXPECT_TRUE(textureRecord->Result->Asset.IsValid());

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    EXPECT_TRUE(engine.GetAssetService().PathIndexContains(textureFile.Path.string()));
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, ManualModelSceneAndTextureImportQueueCompletesThroughStreaming)
{
    const std::vector<std::byte> pngBytes = TinyPngBytes();
    const std::vector<std::byte> binBytes = TriangleBufferBytes();

    TempAssetFile modelBin(
        "assetio004_triangle.bin",
        std::span<const std::byte>(binBytes.data(), binBytes.size()));
    TempAssetFile modelFile(
        "assetio142_manual_triangle.gltf",
        TriangleGltfJson());
    TempAssetFile textureFile(
        "assetio142_manual_albedo.png",
        std::span<const std::byte>(pngBytes.data(), pngBytes.size()));

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [](Runtime::Engine& runningEngine)
            {
                const Runtime::RuntimeAssetImportQueueSnapshot queue =
                    runningEngine.GetAssetImportQueueSnapshot();
                return queue.Entries.size() == 2u &&
                    queue.ActiveCount == 0u &&
                    queue.TerminalCount == 2u;
            },
            256u));
    engine.Initialize();

    auto modelQueued = engine.QueueModelTextureImport(
        Runtime::RuntimeAssetImportRequest{
            .Path = modelFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Unknown,
        });
    ASSERT_TRUE(modelQueued.has_value()) << static_cast<int>(modelQueued.error());
    EXPECT_TRUE(modelQueued->Operation.IsValid());
    EXPECT_EQ(modelQueued->PayloadKind, Assets::AssetPayloadKind::ModelScene);

    auto textureQueued = engine.QueueModelTextureImport(
        Runtime::RuntimeAssetImportRequest{
            .Path = textureFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Unknown,
        });
    ASSERT_TRUE(textureQueued.has_value()) << static_cast<int>(textureQueued.error());
    EXPECT_TRUE(textureQueued->Operation.IsValid());
    EXPECT_EQ(textureQueued->PayloadKind, Assets::AssetPayloadKind::Texture2D);

    EXPECT_FALSE(engine.GetLastAssetImportEvent().has_value());
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 0u);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        engine.GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 2u);
    EXPECT_EQ(queue.Entries[0].Operation, modelQueued->Operation);
    EXPECT_EQ(queue.Entries[0].Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);
    EXPECT_EQ(queue.Entries[0].PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_EQ(queue.Entries[0].Stage,
              Runtime::RuntimeAssetImportQueueStage::Decoding);
    EXPECT_TRUE(queue.Entries[0].CanCancel);
    EXPECT_EQ(queue.Entries[1].Operation, textureQueued->Operation);
    EXPECT_EQ(queue.Entries[1].Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);
    EXPECT_EQ(queue.Entries[1].PayloadKind, Assets::AssetPayloadKind::Texture2D);
    EXPECT_EQ(queue.Entries[1].Stage,
              Runtime::RuntimeAssetImportQueueStage::Decoding);
    EXPECT_TRUE(queue.Entries[1].CanCancel);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";
    engine.Run();

    queue = engine.GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 0u);
    EXPECT_EQ(queue.TerminalCount, 2u);
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);
    EXPECT_EQ(queue.Entries[1].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 2u);

    const auto modelRecord = std::find_if(
        records.begin(),
        records.end(),
        [&](const Runtime::RuntimeAssetIngestRecord& record)
        {
            return record.Handle == modelQueued->Operation;
        });
    ASSERT_NE(modelRecord, records.end());
    EXPECT_EQ(modelRecord->Request.Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);
    EXPECT_EQ(modelRecord->Request.PayloadKind,
              Assets::AssetPayloadKind::ModelScene);
    EXPECT_EQ(modelRecord->Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(modelRecord->Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(modelRecord->Result.has_value());
    EXPECT_EQ(modelRecord->Result->PayloadKind,
              Assets::AssetPayloadKind::ModelScene);
    EXPECT_TRUE(modelRecord->Result->MaterializedModelScene);
    EXPECT_EQ(modelRecord->Result->PrimitiveEntitiesCreated, 1u);

    const auto textureRecord = std::find_if(
        records.begin(),
        records.end(),
        [&](const Runtime::RuntimeAssetIngestRecord& record)
        {
            return record.Handle == textureQueued->Operation;
        });
    ASSERT_NE(textureRecord, records.end());
    EXPECT_EQ(textureRecord->Request.Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);
    EXPECT_EQ(textureRecord->Request.PayloadKind,
              Assets::AssetPayloadKind::Texture2D);
    EXPECT_EQ(textureRecord->Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(textureRecord->Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(textureRecord->Result.has_value());
    EXPECT_EQ(textureRecord->Result->PayloadKind,
              Assets::AssetPayloadKind::Texture2D);
    EXPECT_TRUE(textureRecord->Result->Asset.IsValid());

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());

    engine.Shutdown();
}

TEST(RuntimeAssetImportFormatCoverage, SlowQueuedTextureReadDoesNotBlockRunFrame)
{
    const std::vector<std::byte> pngBytes = TinyPngBytes();
    TempAssetFile textureFile(
        "assetio142_slow_albedo.png",
        std::span<const std::byte>(pngBytes.data(), pngBytes.size()));

    auto readState = std::make_shared<BlockingReadBackendState>();
    auto application = std::make_unique<SlowImportProbeApplication>(readState);
    SlowImportProbeApplication* app = application.get();
    Runtime::Engine engine(HeadlessConfig(), std::move(application));
    engine.Initialize();
    engine.SetModelTextureImportIOBackendFactoryForTest(
        [readState]()
        {
            return std::make_unique<BlockingReadIOBackend>(readState);
        });

    const auto queueBegin = std::chrono::steady_clock::now();
    auto queued = engine.QueueModelTextureImport(
        Runtime::RuntimeAssetImportRequest{
            .Path = textureFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Texture2D,
        });
    const auto queueElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - queueBegin);
    ASSERT_TRUE(queued.has_value()) << static_cast<int>(queued.error());
    EXPECT_LT(queueElapsed.count(), 500);
    EXPECT_FALSE(readState->ReadStarted.load(std::memory_order_acquire));

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";
    engine.Run();

    EXPECT_FALSE(app->TimedOut()) << "frames observed: " << app->Frames();
    EXPECT_TRUE(app->FrameAdvancedWhileReadBlocked())
        << "The frame loop must advance while queued texture IO is blocked.";
    EXPECT_FALSE(app->EventArrivedBeforeRelease())
        << "Import apply must not complete before the blocked worker read is released.";
    EXPECT_TRUE(readState->ReadStarted.load(std::memory_order_acquire));
    EXPECT_TRUE(readState->ReadFinished.load(std::memory_order_acquire));
    const std::optional<Runtime::RuntimeAssetImportEvent>& event =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_TRUE(event->Succeeded());
    ASSERT_TRUE(event->Result.has_value());
    EXPECT_EQ(event->Result->PayloadKind, Assets::AssetPayloadKind::Texture2D);
    EXPECT_TRUE(event->Result->Asset.IsValid());

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        engine.GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_EQ(queue.ActiveCount, 0u);
    EXPECT_EQ(queue.TerminalCount, 1u);
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);

    engine.Shutdown();
}
