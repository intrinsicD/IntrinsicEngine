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
import Extrinsic.Graphics.Material;
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
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.StreamingExecutor;
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
        const bool includeVertexColor = true,
        const bool includeNormals = true)
    {
        Geometry::MeshIO::MeshIOResult mesh{};
        mesh.SourcePath = "/models/triangle.gltf";
        mesh.BasePath = "/models";
        mesh.Vertices.Resize(3u);
        auto positions = mesh.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3{0.0f});
        positions[0] = glm::vec3{0.0f, 0.0f, 0.0f};
        positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};
        positions[2] = glm::vec3{0.0f, 1.0f, 0.0f};
        if (includeNormals)
        {
            auto normals = mesh.Vertices.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f});
            normals[0] = glm::vec3{1.0f, 0.0f, 0.0f};
            normals[1] = glm::vec3{0.0f, 1.0f, 0.0f};
            normals[2] = glm::vec3{0.0f, 0.0f, -1.0f};
        }
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
        const bool includeVertexColor = true,
        const bool includeNormals = true)
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
            MakeTriangleMeshPayload(includeTexcoords, includeVertexColor, includeNormals),
            "Geometry::MeshIO::MeshIOResult"));
        payload.Primitives.push_back(Assets::AssetModelPrimitivePayload{
            .Name = "Triangle",
            .GeometryKind = Assets::AssetPayloadKind::Mesh,
            .GeometryPayloadIndex = 0u,
            .MaterialIndex = 0u,
            .VertexCount = 3u,
            .IndexCount = 3u,
        });
        payload.RootNodeIndices.push_back(0u);
        payload.Nodes.push_back(Assets::AssetModelNodePayload{
            .Name = "TriangleRoot",
            .PrimitiveIndices = {0u},
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

    void PumpDerivedJobs(Runtime::DerivedJobRegistry& jobs, const std::uint32_t maxLaunches)
    {
        jobs.Pump(maxLaunches);
        if (Core::Tasks::Scheduler::IsInitialized())
        {
            Core::Tasks::Scheduler::WaitForAll();
        }
        jobs.DrainCompletions();
        jobs.ApplyMainThreadResults();
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

TEST(RuntimeAssetModelSceneHandoff, MaterialLessPrimitiveBindsNeutralLitDefaultMaterial)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_materialless.gltf");

    // A plain mesh import (e.g. OBJ/PLY/STL) with no authored material: the
    // primitive references no material index.
    Assets::AssetModelScenePayload payload{};
    payload.SourcePath = "/models/materialless.gltf";
    payload.GeometryPayloads.push_back(Assets::AssetGeometryPayload::Make(
        Assets::AssetPayloadKind::Mesh,
        MakeTriangleMeshPayload(
            /*includeTexcoords*/ true,
            /*includeVertexColor*/ false,
            /*includeNormals*/ true),
        "Geometry::MeshIO::MeshIOResult"));
    payload.Primitives.push_back(Assets::AssetModelPrimitivePayload{
        .Name = "Triangle",
        .GeometryKind = Assets::AssetPayloadKind::Mesh,
        .GeometryPayloadIndex = 0u,
        .MaterialIndex = Assets::kInvalidAssetModelIndex,
        .VertexCount = 3u,
        .IndexCount = 3u,
    });
    payload.RootNodeIndices.push_back(0u);
    payload.Nodes.push_back(Assets::AssetModelNodePayload{
        .Name = "MateriallessRoot",
        .PrimitiveIndices = {0u},
    });

    auto model = LoadModel(fx.Service, modelFile.Path.string(), std::move(payload));
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

    EXPECT_TRUE(state->Record.Materials.empty());
    ASSERT_EQ(state->Record.Primitives.size(), 1u);
    const auto& primitive = state->Record.Primitives[0];

    // The material-less primitive must NOT fall back to the unlit
    // DefaultDebugSurface (slot 0); it binds the handoff's neutral lit
    // StandardPBR default so the mesh shades using its vertex normals.
    EXPECT_TRUE(primitive.HasMaterialSlot);
    EXPECT_NE(primitive.MaterialSlot, Graphics::kDefaultMaterialSlotIndex);

    ASSERT_TRUE(state->HasDefaultLitMaterial);
    EXPECT_EQ(primitive.MaterialSlot, state->DefaultLitMaterialSlot);
    EXPECT_EQ(diagnostics.DefaultLitMaterialInstancesCreated, 1u);
    EXPECT_EQ(diagnostics.MaterialLessPrimitivesAssignedDefaultLit, 1u);

    // The default material is lit (no Unlit flag) with neutral base color.
    const Graphics::MaterialParams params =
        fx.Materials.GetParams(state->DefaultLitMaterialLease.GetHandle());
    EXPECT_FALSE(Graphics::HasFlag(params.Flags, Graphics::MaterialFlags::Unlit));
    EXPECT_GT(params.BaseColorFactor.r, 0.0f);
    EXPECT_GT(params.BaseColorFactor.g, 0.0f);
    EXPECT_GT(params.BaseColorFactor.b, 0.0f);
}

TEST(RuntimeAssetModelSceneHandoff, ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs)
{
    SceneHandoffFixture fx;
    Runtime::StreamingExecutor streaming{};
    Runtime::DerivedJobRegistry jobs{streaming};
    TmpFile modelFile("asset_model_scene_handoff_progressive_triangle.gltf");
    auto payload = MakeModelScenePayload(
        /*includeTexcoords*/ false,
        /*includeVertexColor*/ true,
        /*includeNormals*/ false);
    payload.Materials[0].BaseColorTexture = {};
    auto model = LoadModel(
        fx.Service,
        modelFile.Path.string(),
        std::move(payload));
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};
    options.ProgressiveRawGeometryFirst = true;
    options.ProgressiveJobs = &jobs;
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
    EXPECT_TRUE(state->Record.GeneratedTextureAssets.empty());
    EXPECT_EQ(diagnostics.ProgressiveRawPrimitiveEntitiesPublished, 1u);
    EXPECT_EQ(diagnostics.ProgressivePresentationBindingsCreated, 1u);
    EXPECT_EQ(diagnostics.ProgressiveUvAtlasJobsQueued, 1u);
    EXPECT_EQ(diagnostics.ProgressiveNormalJobsQueued, 0u);
    EXPECT_EQ(diagnostics.ProgressiveTextureBakeJobsQueued, 2u);

    const ECS::EntityHandle entity = state->Record.Primitives[0].Entity;
    ASSERT_TRUE(fx.Scene.IsValid(entity));
    ASSERT_TRUE(fx.Scene.Raw().all_of<Runtime::ProgressivePresentationBindings>(entity));
    const auto jobSnapshot = jobs.SnapshotEntity(Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_EQ(jobSnapshot.Entries.size(), 3u);
    EXPECT_EQ(jobSnapshot.Entries[0].Name, "generate mesh uv atlas");
    EXPECT_EQ(jobSnapshot.Entries[1].Name, "bake normal texture");
    EXPECT_EQ(jobSnapshot.Entries[2].Name, "bake albedo texture");
    EXPECT_EQ(jobSnapshot.Entries[1].Dependencies.size(), 1u);
    EXPECT_EQ(jobSnapshot.Entries[2].Dependencies.size(), 1u);

    const auto* initialVertices =
        fx.Scene.Raw().try_get<ECS::Components::GeometrySources::Vertices>(entity);
    ASSERT_NE(initialVertices, nullptr);
    EXPECT_TRUE(initialVertices->Properties.Exists("v:normal"));
    EXPECT_FALSE(initialVertices->Properties.Exists("v:texcoord"));

    PumpDerivedJobs(jobs, 2u);
    PumpDerivedJobs(jobs, 2u);

    const auto* vertices = fx.Scene.Raw().try_get<ECS::Components::GeometrySources::Vertices>(entity);
    ASSERT_NE(vertices, nullptr);
    EXPECT_TRUE(vertices->Properties.Exists("v:texcoord"));
    EXPECT_TRUE(vertices->Properties.Exists("v:normal"));
    EXPECT_EQ(jobs.SnapshotEntity(Runtime::StableEntityLookup::ToRenderId(entity)).Entries[1].Status,
              Runtime::DerivedJobStatus::Complete);

    auto& bindings =
        fx.Scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressivePresentationBinding* presentation =
        Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const Runtime::ProgressiveSlotBinding* normal =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_TRUE(normal->GeneratedTexture.IsValid());
    EXPECT_NE(normal->LastDiagnostic.find("without upload"), std::string::npos);
    const Runtime::ProgressiveSlotBinding* albedo =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Albedo);
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_TRUE(albedo->GeneratedTexture.IsValid());
    EXPECT_NE(albedo->LastDiagnostic.find("without upload"), std::string::npos);
}

TEST(RuntimeAssetModelSceneHandoff, ProgressiveRawGeometryFirstQueuesObjectSpaceNormalBakeWhenInputsReady)
{
    SceneHandoffFixture fx;
    Runtime::StreamingExecutor streaming{};
    Runtime::DerivedJobRegistry jobs{streaming};
    Runtime::RuntimeObjectSpaceNormalBakeQueue normalBakeQueue{};
    TmpFile modelFile("asset_model_scene_handoff_progressive_ready_normal_bake.gltf");
    auto payload = MakeModelScenePayload(
        /*includeTexcoords*/ true,
        /*includeVertexColor*/ false,
        /*includeNormals*/ true);
    payload.Materials[0].NormalTexture = {};
    auto model = LoadModel(
        fx.Service,
        modelFile.Path.string(),
        std::move(payload));
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};
    options.ProgressiveRawGeometryFirst = true;
    options.ProgressiveJobs = &jobs;
    options.ObjectSpaceNormalBakeQueue = &normalBakeQueue;
    options.ObjectSpaceNormalBakeGraphicsBackendOperational = true;
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
    EXPECT_TRUE(state->Record.GeneratedTextureAssets.empty());
    EXPECT_EQ(diagnostics.ProgressiveNormalJobsQueued, 0u);
    EXPECT_EQ(diagnostics.ProgressiveTextureBakeJobsQueued, 1u);
    EXPECT_EQ(normalBakeQueue.Diagnostics().QueuedRequests, 0u);

    const ECS::EntityHandle entity = state->Record.Primitives[0].Entity;
    ASSERT_TRUE(fx.Scene.IsValid(entity));
    const auto initialJobs =
        jobs.SnapshotEntity(Runtime::StableEntityLookup::ToRenderId(entity));
    bool sawCpuNormalBake = false;
    bool sawGpuSchedule = false;
    for (const auto& entry : initialJobs.Entries)
    {
        sawCpuNormalBake = sawCpuNormalBake || entry.Name == "bake normal texture";
        sawGpuSchedule =
            sawGpuSchedule || entry.Name == "schedule normal GPU bake request";
    }
    EXPECT_FALSE(sawCpuNormalBake);
    EXPECT_TRUE(sawGpuSchedule);

    PumpDerivedJobs(jobs, 2u);
    PumpDerivedJobs(jobs, 2u);
    const auto afterJobs =
        jobs.SnapshotEntity(Runtime::StableEntityLookup::ToRenderId(entity));
    for (const auto& entry : afterJobs.Entries)
    {
        if (entry.Name == "schedule normal GPU bake request")
        {
            EXPECT_EQ(entry.Status, Runtime::DerivedJobStatus::Complete)
                << entry.Diagnostic;
        }
    }
    EXPECT_EQ(normalBakeQueue.Diagnostics().QueuedRequests, 1u);
    EXPECT_EQ(normalBakeQueue.Diagnostics().NonOperationalNoOps, 0u);
    EXPECT_EQ(normalBakeQueue.PendingCount(), 1u);

    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_FALSE(state->Record.Materials[0].TextureBindings.Normal.IsValid());
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.NormalSpace,
              Graphics::MaterialNormalTextureSpace::TangentSpaceNormal);

    auto& bindings =
        fx.Scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressivePresentationBinding* presentation =
        Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const Runtime::ProgressiveSlotBinding* normal =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Readiness, Runtime::ProgressiveReadinessState::Pending);
    EXPECT_FALSE(normal->GeneratedTexture.IsValid());
    EXPECT_NE(normal->LastDiagnostic.find("queued object-space normal GPU bake request"),
              std::string::npos);
}

TEST(RuntimeAssetModelSceneHandoff, ProgressiveRawGeometryFirstQueuesObjectSpaceNormalBakeAfterUvEnrichment)
{
    SceneHandoffFixture fx;
    Runtime::StreamingExecutor streaming{};
    Runtime::DerivedJobRegistry jobs{streaming};
    Runtime::RuntimeObjectSpaceNormalBakeQueue normalBakeQueue{};
    TmpFile modelFile("asset_model_scene_handoff_progressive_enriched_normal_bake.gltf");
    auto payload = MakeModelScenePayload(
        /*includeTexcoords*/ false,
        /*includeVertexColor*/ false,
        /*includeNormals*/ true);
    payload.Materials[0].NormalTexture = {};
    auto model = LoadModel(
        fx.Service,
        modelFile.Path.string(),
        std::move(payload));
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};
    options.ProgressiveRawGeometryFirst = true;
    options.ProgressiveJobs = &jobs;
    options.ObjectSpaceNormalBakeQueue = &normalBakeQueue;
    options.ObjectSpaceNormalBakeGraphicsBackendOperational = true;
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
    EXPECT_EQ(diagnostics.ProgressiveUvAtlasJobsQueued, 1u);
    EXPECT_EQ(diagnostics.ProgressiveNormalJobsQueued, 0u);
    EXPECT_EQ(diagnostics.ProgressiveTextureBakeJobsQueued, 1u);
    EXPECT_EQ(normalBakeQueue.Diagnostics().QueuedRequests, 0u);

    const ECS::EntityHandle entity = state->Record.Primitives[0].Entity;
    ASSERT_TRUE(fx.Scene.IsValid(entity));
    const auto initialJobs =
        jobs.SnapshotEntity(Runtime::StableEntityLookup::ToRenderId(entity));
    bool sawUvAtlas = false;
    bool sawGpuSchedule = false;
    std::size_t scheduleDependencies = 0u;
    for (const auto& entry : initialJobs.Entries)
    {
        sawUvAtlas = sawUvAtlas || entry.Name == "generate mesh uv atlas";
        if (entry.Name == "schedule normal GPU bake request")
        {
            sawGpuSchedule = true;
            scheduleDependencies = entry.Dependencies.size();
        }
    }
    EXPECT_TRUE(sawUvAtlas);
    EXPECT_TRUE(sawGpuSchedule);
    EXPECT_EQ(scheduleDependencies, 1u);

    const auto* initialVertices =
        fx.Scene.Raw().try_get<ECS::Components::GeometrySources::Vertices>(entity);
    ASSERT_NE(initialVertices, nullptr);
    EXPECT_FALSE(initialVertices->Properties.Exists("v:texcoord"));

    PumpDerivedJobs(jobs, 2u);
    PumpDerivedJobs(jobs, 2u);

    const auto* vertices =
        fx.Scene.Raw().try_get<ECS::Components::GeometrySources::Vertices>(entity);
    ASSERT_NE(vertices, nullptr);
    EXPECT_TRUE(vertices->Properties.Exists("v:texcoord"));
    EXPECT_EQ(normalBakeQueue.Diagnostics().QueuedRequests, 1u);
    EXPECT_EQ(normalBakeQueue.CachedContentKeyCount(), 1u);
    EXPECT_EQ(normalBakeQueue.PendingCount(), 1u);

    auto& bindings =
        fx.Scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressivePresentationBinding* presentation =
        Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const Runtime::ProgressiveSlotBinding* normal =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Readiness, Runtime::ProgressiveReadinessState::Pending);
    EXPECT_FALSE(normal->GeneratedTexture.IsValid());
    EXPECT_NE(normal->LastDiagnostic.find("queued object-space normal GPU bake request"),
              std::string::npos);
}

TEST(RuntimeAssetModelSceneHandoff, ProgressiveRawGeometryFirstDoesNotCpuFallbackWhenNormalBakeBackendIsNonOperational)
{
    SceneHandoffFixture fx;
    Runtime::StreamingExecutor streaming{};
    Runtime::DerivedJobRegistry jobs{streaming};
    Runtime::RuntimeObjectSpaceNormalBakeQueue normalBakeQueue{};
    TmpFile modelFile("asset_model_scene_handoff_progressive_nonoperational_normal_bake.gltf");
    auto payload = MakeModelScenePayload(
        /*includeTexcoords*/ true,
        /*includeVertexColor*/ false,
        /*includeNormals*/ true);
    payload.Materials[0].NormalTexture = {};
    auto model = LoadModel(
        fx.Service,
        modelFile.Path.string(),
        std::move(payload));
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};
    options.ProgressiveRawGeometryFirst = true;
    options.ProgressiveJobs = &jobs;
    options.ObjectSpaceNormalBakeQueue = &normalBakeQueue;
    options.ObjectSpaceNormalBakeGraphicsBackendOperational = false;
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
    EXPECT_TRUE(state->Record.GeneratedTextureAssets.empty());
    EXPECT_EQ(diagnostics.ProgressiveTextureBakeJobsQueued, 1u);
    EXPECT_EQ(normalBakeQueue.Diagnostics().QueuedRequests, 0u);

    const ECS::EntityHandle entity = state->Record.Primitives[0].Entity;
    ASSERT_TRUE(fx.Scene.IsValid(entity));
    const auto initialJobs =
        jobs.SnapshotEntity(Runtime::StableEntityLookup::ToRenderId(entity));
    bool sawCpuNormalBake = false;
    bool sawGpuSchedule = false;
    for (const auto& entry : initialJobs.Entries)
    {
        sawCpuNormalBake = sawCpuNormalBake || entry.Name == "bake normal texture";
        sawGpuSchedule =
            sawGpuSchedule || entry.Name == "schedule normal GPU bake request";
    }
    EXPECT_FALSE(sawCpuNormalBake);
    EXPECT_TRUE(sawGpuSchedule);

    PumpDerivedJobs(jobs, 2u);
    PumpDerivedJobs(jobs, 2u);
    const auto afterJobs =
        jobs.SnapshotEntity(Runtime::StableEntityLookup::ToRenderId(entity));
    for (const auto& entry : afterJobs.Entries)
    {
        if (entry.Name == "schedule normal GPU bake request")
        {
            EXPECT_EQ(entry.Status, Runtime::DerivedJobStatus::Complete)
                << entry.Diagnostic;
        }
    }
    EXPECT_EQ(normalBakeQueue.Diagnostics().QueuedRequests, 0u);
    EXPECT_EQ(normalBakeQueue.Diagnostics().NonOperationalNoOps, 1u);
    EXPECT_EQ(normalBakeQueue.PendingCount(), 0u);

    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_FALSE(state->Record.Materials[0].TextureBindings.Normal.IsValid());
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.NormalSpace,
              Graphics::MaterialNormalTextureSpace::TangentSpaceNormal);

    auto& bindings =
        fx.Scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressivePresentationBinding* presentation =
        Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const Runtime::ProgressiveSlotBinding* normal =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Readiness, Runtime::ProgressiveReadinessState::Pending);
    EXPECT_FALSE(normal->GeneratedTexture.IsValid());
    EXPECT_NE(normal->LastDiagnostic.find("no CPU fallback"), std::string::npos);
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

TEST(RuntimeAssetModelSceneHandoff, MissingTexcoordsReceiveGeneratedAtlasBeforeGeneratedMaterialTextures)
{
    SceneHandoffFixture fx;
    TmpFile modelFile("asset_model_scene_handoff_missing_texcoords.gltf");
    Assets::AssetModelScenePayload payload = MakeModelScenePayload(
        false,
        true);
    payload.EmbeddedImages.clear();
    payload.Materials[0].BaseColorTexture = {};
    payload.Materials[0].NormalTexture = {};

    auto model = LoadModel(fx.Service, modelFile.Path.string(), std::move(payload));
    ASSERT_TRUE(model.has_value()) << static_cast<int>(model.error());

    Runtime::AssetModelSceneHandoffDiagnostics diagnostics{};
    Runtime::AssetModelSceneHandoffOptions options{};

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

    EXPECT_EQ(state->Record.GeneratedTextureAssets.size(), 2u);
    EXPECT_EQ(fx.Transfer.TextureUploads.size(), 2u);
    ASSERT_EQ(state->Record.Materials.size(), 1u);
    EXPECT_TRUE(state->Record.Materials[0].TextureBindings.Albedo.IsValid());
    EXPECT_TRUE(state->Record.Materials[0].TextureBindings.Normal.IsValid());
    EXPECT_EQ(state->Record.Materials[0].TextureBindings.NormalSpace,
              Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal);
    EXPECT_EQ(diagnostics.GeneratedTextureAssetsCreated, 2u);
    EXPECT_EQ(diagnostics.GeneratedTextureUploadRequests, 2u);
    EXPECT_EQ(diagnostics.GeneratedTextureBakeFailures, 0u);
    EXPECT_EQ(diagnostics.GeneratedNormalTextureBakeFailures, 0u);
    EXPECT_EQ(diagnostics.GeneratedAlbedoTextureBakeFailures, 0u);
    EXPECT_EQ(diagnostics.AuthoredUvPrimitives, 0u);
    EXPECT_EQ(diagnostics.GeneratedUvAtlasPrimitives, 1u);
    EXPECT_EQ(diagnostics.UvAtlasFailures, 0u);
    EXPECT_GE(diagnostics.LastUvAtlasChartCount, 1u);
    EXPECT_GT(diagnostics.LastUvAtlasWidth, 0u);
    EXPECT_GT(diagnostics.LastUvAtlasHeight, 0u);
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
