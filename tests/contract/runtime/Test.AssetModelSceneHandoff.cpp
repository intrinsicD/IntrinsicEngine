#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Geometry.HalfedgeMesh.IO;
import Geometry.Properties;

#include "MockRHI.hpp"

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace Graphics = Extrinsic::Graphics;
namespace RHI = Extrinsic::RHI;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    struct SceneHandoffFixture
    {
        Extrinsic::Tests::MockDevice Device{};
        RHI::BufferManager BufferMgr;
        RHI::TextureManager TextureMgr;
        RHI::SamplerManager SamplerMgr;
        Extrinsic::Tests::MockTransferQueue Transfer{};
        Graphics::GpuAssetCache Cache;
        Graphics::MaterialSystem Materials{};
        Assets::AssetService Service{};
        ECS::Scene::Registry Scene{};

        SceneHandoffFixture()
            : BufferMgr(Device)
            , TextureMgr(Device, Device.Bindless)
            , SamplerMgr(Device)
            , Cache(BufferMgr, TextureMgr, SamplerMgr, Transfer)
        {
            Materials.Initialize(Device, BufferMgr);
        }
    };

    struct TmpFile
    {
        std::filesystem::path Path;

        explicit TmpFile(std::string_view name)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << "asset-model-scene-handoff";
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

    [[nodiscard]] Assets::AssetTexture2DPayload MakeEmbeddedTexturePayload()
    {
        Assets::AssetTexture2DPayload payload{};
        payload.Metadata.Width = 1u;
        payload.Metadata.Height = 1u;
        payload.Metadata.Components = 4u;
        payload.Metadata.PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm;
        payload.Metadata.ColorSpace = Assets::AssetTextureColorSpace::SRGB;
        payload.Metadata.SourceKind = Assets::AssetTextureSourceKind::Embedded;
        payload.Metadata.SourceFormat = Assets::AssetFileFormat::PNG;
        payload.Metadata.SourcePath = "embedded://albedo";
        payload.Metadata.DebugName = "embedded-albedo";
        payload.PixelBytes.assign(4u, std::byte{0x7F});
        return payload;
    }

    [[nodiscard]] Geometry::MeshIO::MeshIOResult MakeTriangleMeshPayload()
    {
        Geometry::MeshIO::MeshIOResult mesh{};
        mesh.SourcePath = "/models/triangle.gltf";
        mesh.BasePath = "/models";
        mesh.Vertices.Resize(3u);
        auto positions = mesh.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3{0.0f});
        positions[0] = glm::vec3{0.0f, 0.0f, 0.0f};
        positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};
        positions[2] = glm::vec3{0.0f, 1.0f, 0.0f};

        mesh.Faces.Resize(1u);
        auto faces = mesh.Faces.GetOrAdd<std::vector<std::uint32_t>>("f:vertices", {});
        faces[0] = {0u, 1u, 2u};
        return mesh;
    }

    [[nodiscard]] Assets::AssetModelScenePayload MakeModelScenePayload()
    {
        Assets::AssetModelScenePayload payload{};
        payload.SourcePath = "/models/triangle.gltf";
        payload.EmbeddedImages.push_back(MakeEmbeddedTexturePayload());

        Assets::AssetModelMaterialPayload material{};
        material.Name = "Mat";
        material.BaseColorFactor = {0.25f, 0.5f, 0.75f, 1.0f};
        material.MetallicFactor = 0.25f;
        material.RoughnessFactor = 0.75f;
        material.BaseColorTexture.ImageIndex = 0u;
        payload.Materials.push_back(std::move(material));

        payload.GeometryPayloads.push_back(Assets::AssetGeometryPayload::Make(
            Assets::AssetPayloadKind::Mesh,
            MakeTriangleMeshPayload(),
            "Geometry::MeshIO::MeshIOResult"));
        payload.Primitives.push_back(Assets::AssetModelPrimitivePayload{
            .Name = "Triangle",
            .GeometryKind = Assets::AssetPayloadKind::Mesh,
            .GeometryPayloadIndex = 0u,
            .MaterialIndex = 0u,
            .VertexCount = 3u,
            .IndexCount = 3u,
        });
        return payload;
    }

    [[nodiscard]] Core::Expected<Assets::AssetId> LoadModel(
        Assets::AssetService& service,
        std::string_view path,
        Assets::AssetModelScenePayload payload)
    {
        return service.Load<Assets::AssetModelScenePayload>(
            path,
            [payload = std::move(payload)](
                std::string_view,
                Assets::AssetId) -> Core::Expected<Assets::AssetModelScenePayload>
            {
                return payload;
            });
    }

    void FlushAssetEvents(Assets::AssetService& service)
    {
        if (Core::Tasks::Scheduler::IsInitialized())
        {
            Core::Tasks::Scheduler::WaitForAll();
        }
        service.Tick();
    }
}

TEST(RuntimeAssetModelSceneHandoff, BuildsDeterministicEmbeddedTextureAssetPath)
{
    const Assets::AssetTexture2DPayload image = MakeEmbeddedTexturePayload();

    const std::string path = Runtime::BuildEmbeddedTextureAssetPath(
        "/models/triangle.gltf",
        2u,
        image);

    EXPECT_EQ(path, "/models/triangle.gltf.embedded-texture-2.png");
}

TEST(RuntimeAssetModelSceneHandoff, MaterializeModelSceneCreatesMeshEntityAndUploadsChildTexture)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_triangle.gltf");
    auto model = LoadModel(fx.Service, modelFile.Path.string(), MakeModelScenePayload());
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    auto state = Runtime::MaterializeModelSceneAsset(
        fx.Service,
        fx.Cache,
        fx.Scene,
        fx.Materials,
        *model,
        {},
        &diagnostics);
    ASSERT_TRUE(state.has_value()) << static_cast<int>(state.error());

    ASSERT_EQ(state->Record.EmbeddedTextureAssets.size(), 1u);
    const Assets::AssetId childTexture = state->Record.EmbeddedTextureAssets[0];
    EXPECT_TRUE(childTexture.IsValid());
    EXPECT_EQ(fx.Cache.GetState(childTexture), Graphics::GpuAssetState::GpuUploading);
    ASSERT_EQ(fx.Transfer.TextureUploads.size(), 1u);
    EXPECT_EQ(fx.Transfer.TextureUploads[0].SizeBytes, 4u);

    ASSERT_EQ(state->Record.Primitives.size(), 1u);
    const auto entity = state->Record.Primitives[0].Entity;
    EXPECT_TRUE(fx.Scene.IsValid(entity));
    auto& raw = fx.Scene.Raw();
    EXPECT_TRUE(raw.all_of<Graphics::Components::RenderSurface>(entity));

    const auto view = ECS::Components::GeometrySources::BuildConstView(raw, entity);
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(view.ActiveDomain, ECS::Components::GeometrySources::Domain::Mesh);
    EXPECT_EQ(view.VerticesAlive(), 3u);
    EXPECT_EQ(view.FacesAlive(), 1u);

    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.Albedo, childTexture);
    EXPECT_TRUE(state->Record.Materials[0].HasMaterialSlot);
    EXPECT_FALSE(state->Record.Materials[0].TextureBindingsResolved)
        << "The texture is pending and this fixture has no fallback texture; "
           "the handoff still records the AssetId binding for later residency work.";

    EXPECT_EQ(diagnostics.ModelSceneMaterializeRequests, 1u);
    EXPECT_EQ(diagnostics.ModelSceneMaterializeSuccesses, 1u);
    EXPECT_EQ(diagnostics.PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(diagnostics.EmbeddedTextureAssetsCreated, 1u);
    EXPECT_EQ(diagnostics.EmbeddedTextureUploadRequests, 1u);
    EXPECT_EQ(diagnostics.MaterialInstancesCreated, 1u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingFailures, 1u);
}

TEST(RuntimeAssetModelSceneHandoff, ReadyModelSceneEventMaterializesRecordAndOwnsGeneratedEntities)
{
    SceneHandoffFixture fx;
    ECS::EntityHandle generated{};
    Assets::AssetId modelAsset{};

    {
        std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
        renderer->Initialize(fx.Device);
        Runtime::AssetModelSceneHandoff handoff(fx.Service, fx.Cache, fx.Scene, *renderer);
        TmpFile modelFile("asset_model_scene_handoff_event.gltf");

        auto model = LoadModel(fx.Service, modelFile.Path.string(), MakeModelScenePayload());
        ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());
        modelAsset = *model;

        FlushAssetEvents(fx.Service);
        FlushAssetEvents(fx.Service);

        const Runtime::AssetModelSceneHandoffRecord* record = handoff.FindRecord(modelAsset);
        ASSERT_NE(record, nullptr);
        ASSERT_EQ(record->EmbeddedTextureAssets.size(), 1u);
        EXPECT_EQ(fx.Cache.GetState(record->EmbeddedTextureAssets[0]),
                  Graphics::GpuAssetState::GpuUploading);
        ASSERT_EQ(record->Primitives.size(), 1u);
        generated = record->Primitives[0].Entity;
        EXPECT_TRUE(fx.Scene.IsValid(generated));
        EXPECT_TRUE(fx.Scene.Raw().all_of<Graphics::Components::RenderSurface>(generated));

        const auto diagnostics = handoff.GetDiagnostics();
        EXPECT_EQ(diagnostics.ReadyEventsObserved, 2u);
        EXPECT_EQ(diagnostics.ModelSceneReadyEvents, 1u);
        EXPECT_EQ(diagnostics.NonModelSceneReadyEvents, 1u);
        EXPECT_EQ(diagnostics.ModelSceneMaterializeSuccesses, 1u);
        EXPECT_EQ(diagnostics.EmbeddedTextureUploadRequests, 1u);
    }

    EXPECT_TRUE(modelAsset.IsValid());
    EXPECT_FALSE(fx.Scene.IsValid(generated));
}

TEST(RuntimeAssetModelSceneHandoff, MaterialBindingsResolveWhenChildTextureAlreadyResident)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_resident.gltf");
    auto model = LoadModel(fx.Service, modelFile.Path.string(), MakeModelScenePayload());
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());
    auto modelPath = fx.Service.GetPath(*model);
    ASSERT_TRUE(modelPath.has_value()) << static_cast<int>(modelPath.error());

    auto child = Runtime::LoadEmbeddedTextureAsset(
        fx.Service,
        *modelPath,
        0u,
        MakeEmbeddedTexturePayload());
    ASSERT_TRUE(child.has_value()) << static_cast<int>(child.error());

    ASSERT_TRUE(Runtime::RequestTextureAssetUpload(fx.Service, fx.Cache, *child).has_value());
    fx.Cache.Tick(0u, 2u);
    EXPECT_EQ(fx.Cache.GetState(*child), Graphics::GpuAssetState::Ready);

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};
    options.RequestEmbeddedTextureUploads = false;
    auto state = Runtime::MaterializeModelSceneAsset(
        fx.Service,
        fx.Cache,
        fx.Scene,
        fx.Materials,
        *model,
        options,
        &diagnostics);
    ASSERT_TRUE(state.has_value()) << static_cast<int>(state.error());

    ASSERT_EQ(state->Record.EmbeddedTextureAssets.size(), 1u);
    EXPECT_EQ(state->Record.EmbeddedTextureAssets[0], *child);
    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_TRUE(state->Record.Materials[0].TextureBindingsResolved);
    EXPECT_EQ(diagnostics.MaterialTextureBindingsResolved, 1u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingFailures, 0u);
}
