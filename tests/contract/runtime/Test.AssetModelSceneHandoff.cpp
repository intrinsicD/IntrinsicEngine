#include <gtest/gtest.h>

#include <array>
#include <cmath>
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

import Extrinsic.Asset.EventBus;
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

    [[nodiscard]] Geometry::MeshIO::MeshIOResult MakeTriangleMeshPayload(
        const bool includeTexcoords = true,
        const bool includeVertexColor = true)
    {
        Geometry::MeshIO::MeshIOResult mesh{};
        mesh.SourcePath = "/models/triangle.gltf";
        mesh.BasePath = "/models";
        mesh.Vertices.Resize(3u);
        auto positions = mesh.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3{0.0f});
        positions[0] = glm::vec3{0.0f, 0.0f, 0.0f};
        positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};
        positions[2] = glm::vec3{0.0f, 1.0f, 0.0f};
        auto normals = mesh.Vertices.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f});
        normals[0] = glm::vec3{1.0f, 0.0f, 0.0f};
        normals[1] = glm::vec3{0.0f, 1.0f, 0.0f};
        normals[2] = glm::vec3{0.0f, 0.0f, -1.0f};
        if (includeTexcoords)
        {
            auto texcoords = mesh.Vertices.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f});
            texcoords[0] = glm::vec2{0.0f, 0.0f};
            texcoords[1] = glm::vec2{1.0f, 0.0f};
            texcoords[2] = glm::vec2{0.0f, 1.0f};
        }
        if (includeVertexColor)
        {
            auto colors = mesh.Vertices.GetOrAdd<glm::vec4>("v:color", glm::vec4{1.0f});
            colors[0] = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
            colors[1] = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
            colors[2] = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
        }

        mesh.Faces.Resize(1u);
        auto faces = mesh.Faces.GetOrAdd<std::vector<std::uint32_t>>("f:vertices", {});
        faces[0] = {0u, 1u, 2u};
        return mesh;
    }

    [[nodiscard]] Assets::AssetModelScenePayload MakeModelScenePayload(
        const bool includeTexcoords = true,
        const bool includeVertexColor = true)
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
            MakeTriangleMeshPayload(includeTexcoords, includeVertexColor),
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
    ASSERT_EQ(fx.Transfer.TextureUploads.size(), 2u);
    EXPECT_EQ(fx.Transfer.TextureUploads[0].SizeBytes, 4u);

    ASSERT_EQ(state->Record.GeneratedTextureAssets.size(), 1u);
    const Assets::AssetId generatedNormal = state->Record.GeneratedTextureAssets[0];
    EXPECT_TRUE(generatedNormal.IsValid());
    EXPECT_EQ(fx.Cache.GetState(generatedNormal), Graphics::GpuAssetState::GpuUploading);
    EXPECT_EQ(fx.Transfer.TextureUploads[1].SizeBytes, 64u * 64u * 4u);
    auto generatedPayload = fx.Service.Read<Assets::AssetTexture2DPayload>(generatedNormal);
    ASSERT_TRUE(generatedPayload.has_value()) << static_cast<int>(generatedPayload.error());
    ASSERT_EQ(generatedPayload->size(), 1u);
    EXPECT_EQ((*generatedPayload)[0].Metadata.SourceKind, Assets::AssetTextureSourceKind::Generated);
    EXPECT_EQ((*generatedPayload)[0].Metadata.ColorSpace, Assets::AssetTextureColorSpace::Linear);

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
    ASSERT_NE(view.VertexSource, nullptr);
    auto normals = view.VertexSource->Properties.Get<glm::vec3>("v:normal");
    ASSERT_TRUE(normals.IsValid());
    ASSERT_EQ(normals.Vector().size(), 3u);
    EXPECT_EQ(normals[0], glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(normals[1], glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(normals[2], glm::vec3(0.0f, 0.0f, -1.0f));
    auto texcoords = view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords.IsValid());
    ASSERT_EQ(texcoords.Vector().size(), 3u);
    EXPECT_EQ(texcoords[0], glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(texcoords[1], glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(texcoords[2], glm::vec2(0.0f, 1.0f));
    auto colors = view.VertexSource->Properties.Get<glm::vec4>("v:color");
    ASSERT_TRUE(colors.IsValid());
    ASSERT_EQ(colors.Vector().size(), 3u);
    EXPECT_EQ(colors[0], glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.Albedo, childTexture);
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.Normal, generatedNormal);
    EXPECT_TRUE(state->Record.Materials[0].HasMaterialSlot);
    EXPECT_FALSE(state->Record.Materials[0].TextureBindingsResolved)
        << "The texture is pending and this fixture has no fallback texture; "
           "the handoff still records the AssetId binding for later residency work.";

    EXPECT_EQ(diagnostics.ModelSceneMaterializeRequests, 1u);
    EXPECT_EQ(diagnostics.ModelSceneMaterializeSuccesses, 1u);
    EXPECT_EQ(diagnostics.PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(diagnostics.EmbeddedTextureAssetsCreated, 1u);
    EXPECT_EQ(diagnostics.EmbeddedTextureUploadRequests, 1u);
    EXPECT_EQ(diagnostics.GeneratedTextureAssetsCreated, 1u);
    EXPECT_EQ(diagnostics.GeneratedTextureUploadRequests, 1u);
    EXPECT_EQ(diagnostics.GeneratedTextureBakeFailures, 0u);
    EXPECT_EQ(diagnostics.MaterialInstancesCreated, 1u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingUploadDeferrals, 1u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingFailures, 0u);
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
        ASSERT_EQ(record->GeneratedTextureAssets.size(), 1u);
        EXPECT_EQ(fx.Cache.GetState(record->EmbeddedTextureAssets[0]),
                  Graphics::GpuAssetState::GpuUploading);
        EXPECT_EQ(fx.Cache.GetState(record->GeneratedTextureAssets[0]),
                  Graphics::GpuAssetState::GpuUploading);
        ASSERT_EQ(record->Primitives.size(), 1u);
        generated = record->Primitives[0].Entity;
        EXPECT_TRUE(fx.Scene.IsValid(generated));
        EXPECT_TRUE(fx.Scene.Raw().all_of<Graphics::Components::RenderSurface>(generated));

        const auto diagnostics = handoff.GetDiagnostics();
        EXPECT_EQ(diagnostics.ReadyEventsObserved, 3u);
        EXPECT_EQ(diagnostics.ModelSceneReadyEvents, 1u);
        EXPECT_EQ(diagnostics.NonModelSceneReadyEvents, 2u);
        EXPECT_EQ(diagnostics.ModelSceneMaterializeSuccesses, 1u);
        EXPECT_EQ(diagnostics.EmbeddedTextureUploadRequests, 1u);
        EXPECT_EQ(diagnostics.GeneratedTextureUploadRequests, 1u);
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
    options.GenerateMissingNormalTextures = false;
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

TEST(RuntimeAssetModelSceneHandoff, GeneratesMissingAlbedoTextureFromVertexColorProperty)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_generated_albedo.gltf");
    Assets::AssetModelScenePayload payload = MakeModelScenePayload();
    payload.EmbeddedImages.clear();
    payload.Materials[0].BaseColorTexture = {};

    auto model = LoadModel(fx.Service, modelFile.Path.string(), std::move(payload));
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};
    options.GenerateMissingNormalTextures = false;

    auto state = Runtime::MaterializeModelSceneAsset(
        fx.Service,
        fx.Cache,
        fx.Scene,
        fx.Materials,
        *model,
        options,
        &diagnostics);
    ASSERT_TRUE(state.has_value()) << static_cast<int>(state.error());

    ASSERT_EQ(state->Record.EmbeddedTextureAssets.size(), 0u);
    ASSERT_EQ(state->Record.GeneratedTextureAssets.size(), 1u);
    const Assets::AssetId generatedAlbedo = state->Record.GeneratedTextureAssets[0];
    EXPECT_TRUE(generatedAlbedo.IsValid());
    auto generatedPayload = fx.Service.Read<Assets::AssetTexture2DPayload>(generatedAlbedo);
    ASSERT_TRUE(generatedPayload.has_value()) << static_cast<int>(generatedPayload.error());
    ASSERT_EQ(generatedPayload->size(), 1u);
    EXPECT_EQ((*generatedPayload)[0].Metadata.SourceKind, Assets::AssetTextureSourceKind::Generated);
    EXPECT_EQ((*generatedPayload)[0].Metadata.ColorSpace, Assets::AssetTextureColorSpace::SRGB);

    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.Albedo, generatedAlbedo);
    EXPECT_FALSE(state->Record.Materials[0].TextureBindings.Normal.IsValid());
    EXPECT_EQ(diagnostics.GeneratedTextureAssetsCreated, 1u);
    EXPECT_EQ(diagnostics.GeneratedTextureUploadRequests, 1u);
    EXPECT_EQ(diagnostics.GeneratedTextureBakeFailures, 0u);
}

TEST(RuntimeAssetModelSceneHandoff, MissingTexcoordsReceiveFallbackAndGenerateNormalTexture)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_missing_texcoords.gltf");
    Assets::AssetModelScenePayload payload = MakeModelScenePayload(
        false,
        false);
    payload.EmbeddedImages.clear();
    payload.Materials[0].BaseColorTexture = {};
    payload.Materials[0].NormalTexture = {};

    auto model = LoadModel(fx.Service, modelFile.Path.string(), std::move(payload));
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};
    options.GenerateMissingAlbedoTextures = false;

    auto state = Runtime::MaterializeModelSceneAsset(
        fx.Service,
        fx.Cache,
        fx.Scene,
        fx.Materials,
        *model,
        options,
        &diagnostics);
    ASSERT_TRUE(state.has_value()) << static_cast<int>(state.error());

    ASSERT_EQ(state->Record.Primitives.size(), 1u);
    const auto entity = state->Record.Primitives[0].Entity;
    const auto view = ECS::Components::GeometrySources::BuildConstView(fx.Scene.Raw(), entity);
    ASSERT_TRUE(view.Valid());
    ASSERT_NE(view.VertexSource, nullptr);
    auto texcoords = view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords.IsValid());
    ASSERT_EQ(texcoords.Vector().size(), 3u);

    bool sawNonZeroTexcoord = false;
    for (const glm::vec2 texcoord : texcoords.Vector())
    {
        EXPECT_TRUE(std::isfinite(texcoord.x));
        EXPECT_TRUE(std::isfinite(texcoord.y));
        sawNonZeroTexcoord = sawNonZeroTexcoord ||
            std::abs(texcoord.x) > 1.0e-6f ||
            std::abs(texcoord.y) > 1.0e-6f;
    }
    EXPECT_TRUE(sawNonZeroTexcoord);

    ASSERT_EQ(state->Record.GeneratedTextureAssets.size(), 1u);
    const Assets::AssetId generatedNormal = state->Record.GeneratedTextureAssets[0];
    EXPECT_TRUE(generatedNormal.IsValid());
    EXPECT_EQ(fx.Cache.GetState(generatedNormal), Graphics::GpuAssetState::GpuUploading);
    ASSERT_EQ(fx.Transfer.TextureUploads.size(), 1u);
    EXPECT_EQ(fx.Transfer.TextureUploads[0].SizeBytes, 64u * 64u * 4u);
    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.Normal, generatedNormal);
    EXPECT_EQ(diagnostics.GeneratedTextureAssetsCreated, 1u);
    EXPECT_EQ(diagnostics.GeneratedTextureUploadRequests, 1u);
    EXPECT_EQ(diagnostics.GeneratedTextureBakeFailures, 0u);
    EXPECT_EQ(diagnostics.GeneratedNormalTextureBakeFailures, 0u);
}

TEST(RuntimeAssetModelSceneHandoff, PendingMaterialBindingsResolveAfterTextureUploadTick)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_pending_resolve.gltf");

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(fx.Device);
    Runtime::AssetModelSceneHandoff handoff(fx.Service, fx.Cache, fx.Scene, *renderer);

    auto model = LoadModel(fx.Service, modelFile.Path.string(), MakeModelScenePayload());
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    FlushAssetEvents(fx.Service);
    FlushAssetEvents(fx.Service);

    const Runtime::AssetModelSceneHandoffRecord* record = handoff.FindRecord(*model);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->Materials.size(), 1u);
    EXPECT_FALSE(record->Materials[0].TextureBindingsResolved);
    ASSERT_EQ(record->EmbeddedTextureAssets.size(), 1u);
    EXPECT_EQ(fx.Cache.GetState(record->EmbeddedTextureAssets[0]),
              Graphics::GpuAssetState::GpuUploading);
    ASSERT_EQ(record->GeneratedTextureAssets.size(), 1u);
    EXPECT_EQ(fx.Cache.GetState(record->GeneratedTextureAssets[0]),
              Graphics::GpuAssetState::GpuUploading);

    fx.Cache.Tick(0u, 2u);
    auto resolved = handoff.ResolvePendingMaterialTextureBindings();
    ASSERT_TRUE(resolved.has_value()) << static_cast<int>(resolved.error());
    EXPECT_EQ(*resolved, 1u);

    record = handoff.FindRecord(*model);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->Materials.size(), 1u);
    EXPECT_TRUE(record->Materials[0].TextureBindingsResolved);

    const auto diagnostics = handoff.GetDiagnostics();
    EXPECT_EQ(diagnostics.MaterialTextureBindingsResolved, 1u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingReresolveRequests, 3u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingReresolveSuccesses, 1u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingReresolveFailures, 0u);
    EXPECT_GE(diagnostics.MaterialTextureBindingUploadDeferrals, 1u);
}

TEST(RuntimeAssetModelSceneHandoff, ReloadedEmbeddedTextureInvalidatesAndReresolvesBinding)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_reload_resolve.gltf");

    const Assets::AssetEventBus::ListenerToken cacheToken = fx.Service.SubscribeAll(
        [&cache = fx.Cache](const Assets::AssetId id, const Assets::AssetEvent event)
        {
            switch (event)
            {
            case Assets::AssetEvent::Failed:
                cache.NotifyFailed(id);
                break;
            case Assets::AssetEvent::Reloaded:
                cache.NotifyReloaded(id);
                break;
            case Assets::AssetEvent::Destroyed:
                cache.NotifyDestroyed(id);
                break;
            case Assets::AssetEvent::Ready:
                break;
            }
        });
    Runtime::AssetModelTextureHandoff textureHandoff(fx.Service, fx.Cache);
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(fx.Device);
    Runtime::AssetModelSceneHandoff handoff(fx.Service, fx.Cache, fx.Scene, *renderer);

    auto model = LoadModel(fx.Service, modelFile.Path.string(), MakeModelScenePayload());
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    FlushAssetEvents(fx.Service);
    FlushAssetEvents(fx.Service);

    const Runtime::AssetModelSceneHandoffRecord* record = handoff.FindRecord(*model);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->EmbeddedTextureAssets.size(), 1u);
    const Assets::AssetId childTexture = record->EmbeddedTextureAssets[0];

    fx.Cache.Tick(0u, 2u);
    ASSERT_TRUE(handoff.ResolvePendingMaterialTextureBindings().has_value());
    record = handoff.FindRecord(*model);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->Materials.size(), 1u);
    ASSERT_TRUE(record->Materials[0].TextureBindingsResolved);

    ASSERT_TRUE(fx.Service.Reload<Assets::AssetTexture2DPayload>(
        childTexture,
        [](std::string_view, Assets::AssetId)
            -> Core::Expected<Assets::AssetTexture2DPayload>
        {
            Assets::AssetTexture2DPayload payload = MakeEmbeddedTexturePayload();
            payload.PixelBytes.assign(4u, std::byte{0x33});
            return payload;
        }).has_value());

    FlushAssetEvents(fx.Service);

    record = handoff.FindRecord(*model);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->Materials.size(), 1u);
    EXPECT_FALSE(record->Materials[0].TextureBindingsResolved);
    EXPECT_EQ(fx.Cache.GetState(childTexture), Graphics::GpuAssetState::GpuUploading);

    fx.Cache.Tick(1u, 2u);
    auto resolved = handoff.ResolvePendingMaterialTextureBindings();
    ASSERT_TRUE(resolved.has_value()) << static_cast<int>(resolved.error());
    EXPECT_EQ(*resolved, 1u);

    record = handoff.FindRecord(*model);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->Materials.size(), 1u);
    EXPECT_TRUE(record->Materials[0].TextureBindingsResolved);

    const auto diagnostics = handoff.GetDiagnostics();
    EXPECT_EQ(diagnostics.MaterialTextureBindingReloadInvalidations, 1u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingReresolveSuccesses, 2u);
    EXPECT_EQ(diagnostics.MaterialTextureBindingReresolveFailures, 0u);

    fx.Service.UnsubscribeAll(cacheToken);
}
