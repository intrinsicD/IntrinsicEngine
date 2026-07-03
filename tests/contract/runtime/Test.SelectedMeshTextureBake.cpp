#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StreamingExecutor;

namespace Assets = Extrinsic::Assets;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    void FlushAssetEvents(Assets::AssetService& service)
    {
        for (std::uint32_t i = 0u; i < 4u; ++i)
            service.Tick();
    }

    void SetMeshTopology(ECS::Scene::Registry& scene, const ECS::EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(3u);
        vertices.Properties
            .GetOrAdd<glm::vec3>(std::string{GS::PropertyNames::kPosition}, glm::vec3{0.0f})
            .Vector() = {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        };
        vertices.Properties
            .GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f})
            .Vector() = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
        };
        vertices.Properties
            .GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f})
            .Vector() = {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        };
        vertices.Properties
            .GetOrAdd<float>("v:heat", 0.0f)
            .Vector() = {0.0f, 1.0f, 0.5f};
        vertices.Properties
            .GetOrAdd<glm::vec4>("v:albedo", glm::vec4{1.0f})
            .Vector() = {
            {0.25f, 0.5f, 1.0f, 1.0f},
            {0.25f, 0.5f, 1.0f, 1.0f},
            {0.25f, 0.5f, 1.0f, 1.0f},
        };
        vertices.Properties
            .GetOrAdd<float>("v:roughness", 0.5f)
            .Vector() = {0.2f, 0.4f, 0.6f};
        vertices.Properties
            .GetOrAdd<float>("v:metallic", 0.0f)
            .Vector() = {0.0f, 0.5f, 1.0f};

        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(3u);
        edges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kEdgeV0}, kInvalidIndex)
            .Vector() = {0u, 1u, 2u};
        edges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kEdgeV1}, kInvalidIndex)
            .Vector() = {1u, 2u, 0u};

        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        halfedges.Properties.Resize(6u);
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeToVertex}, kInvalidIndex)
            .Vector() = {1u, 2u, 0u, 0u, 2u, 1u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeNext}, kInvalidIndex)
            .Vector() = {1u, 2u, 0u, 5u, 3u, 4u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeFace}, kInvalidIndex)
            .Vector() = {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex};

        auto& faces = raw.emplace<GS::Faces>(entity);
        faces.Properties.Resize(1u);
        faces.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kFaceHalfedge}, kInvalidIndex)
            .Vector() = {0u};
        faces.Properties
            .GetOrAdd<glm::vec4>("f:debug_color", glm::vec4{1.0f})
            .Vector() = {{0.25f, 0.5f, 1.0f, 1.0f}};
    }

    [[nodiscard]] Runtime::ProgressivePresentationBindings MakeBindings()
    {
        Runtime::ProgressiveSlotBinding albedo{};
        albedo.Semantic = Runtime::ProgressiveSlotSemantic::Albedo;
        albedo.SourceKind = Runtime::ProgressiveSlotSourceKind::UniformDefault;
        albedo.UniformDefault.Kind = Runtime::ProgressivePropertyValueKind::Vec4;
        albedo.UniformDefault.Vector = glm::vec4{1.0f};
        albedo.Readiness = Runtime::ProgressiveReadinessState::DefaultValue;
        albedo.Provenance =
            Runtime::ProgressiveGeneratedOutputProvenance::UniformDefault;

        Runtime::ProgressiveSlotBinding normal{};
        normal.Semantic = Runtime::ProgressiveSlotSemantic::Normal;
        normal.SourceKind = Runtime::ProgressiveSlotSourceKind::PropertyBake;
        normal.Property = Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .PropertyName = "v:normal",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3,
            .ExpectedElementCount = 3u,
        };
        normal.Readiness = Runtime::ProgressiveReadinessState::Pending;
        normal.GeneratedPolicy =
            Runtime::ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
        normal.Provenance =
            Runtime::ProgressiveGeneratedOutputProvenance::PropertyBinding;

        Runtime::ProgressiveSlotBinding roughness{};
        roughness.Semantic = Runtime::ProgressiveSlotSemantic::Roughness;
        roughness.SourceKind = Runtime::ProgressiveSlotSourceKind::UniformDefault;
        roughness.UniformDefault.Kind =
            Runtime::ProgressivePropertyValueKind::ScalarFloat;
        roughness.UniformDefault.Scalar = 0.5;
        roughness.Readiness = Runtime::ProgressiveReadinessState::DefaultValue;
        roughness.Provenance =
            Runtime::ProgressiveGeneratedOutputProvenance::UniformDefault;

        Runtime::ProgressiveSlotBinding metallic{};
        metallic.Semantic = Runtime::ProgressiveSlotSemantic::Metallic;
        metallic.SourceKind = Runtime::ProgressiveSlotSourceKind::UniformDefault;
        metallic.UniformDefault.Kind =
            Runtime::ProgressivePropertyValueKind::ScalarFloat;
        metallic.UniformDefault.Scalar = 0.0;
        metallic.Readiness = Runtime::ProgressiveReadinessState::DefaultValue;
        metallic.Provenance =
            Runtime::ProgressiveGeneratedOutputProvenance::UniformDefault;

        return Runtime::ProgressivePresentationBindings{
            .Shape = Runtime::ProgressiveEntityShape::MeshLeaf,
            .Lanes = {
                Runtime::ProgressiveRenderLaneBinding{
                    .Lane = Runtime::ProgressiveRenderLane::Surface,
                    .PresentationKey = "mesh.surface",
                },
            },
            .Presentations = {
                Runtime::ProgressivePresentationBinding{
                    .Key = "mesh.surface",
                    .Kind = Runtime::ProgressivePresentationKind::SurfaceMaterial,
                    .Slots = {albedo, normal, roughness, metallic},
                },
            },
            .BindingGeneration = 7u,
        };
    }

    [[nodiscard]] ECS::EntityHandle MakeMeshEntity(
        ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = scene.Create();
        SetMeshTopology(scene, entity);
        scene.Raw().emplace<Runtime::ProgressivePresentationBindings>(
            entity,
            MakeBindings());
        return entity;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeNormalRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex;
        request.SourcePropertyName = "v:normal";
        request.ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3;
        request.Encoder = Runtime::MeshAttributeTextureBakeEncoder::Normal;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::ProgressiveSlotSemantic::Normal;
        request.GeneratedKey = "normal";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeHeatRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex;
        request.SourcePropertyName = "v:heat";
        request.ExpectedValueKind = Runtime::ProgressivePropertyValueKind::ScalarFloat;
        request.Encoder = Runtime::MeshAttributeTextureBakeEncoder::LinearScalar;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::ProgressiveSlotSemantic::Albedo;
        request.GeneratedKey = "heat";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeAlbedoRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex;
        request.SourcePropertyName = "v:albedo";
        request.ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec4;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::ProgressiveSlotSemantic::Albedo;
        request.GeneratedKey = "albedo";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeFaceAlbedoRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request = MakeAlbedoRequest(entity);
        request.SourceDomain = Runtime::ProgressiveGeometryDomain::MeshFace;
        request.SourcePropertyName = "f:debug_color";
        request.ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec4;
        request.GeneratedKey = "face-albedo";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeRoughnessRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex;
        request.SourcePropertyName = "v:roughness";
        request.ExpectedValueKind = Runtime::ProgressivePropertyValueKind::ScalarFloat;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::ProgressiveSlotSemantic::Roughness;
        request.GeneratedKey = "roughness";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeMetallicRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request = MakeRoughnessRequest(entity);
        request.SourcePropertyName = "v:metallic";
        request.TargetSemantic = Runtime::ProgressiveSlotSemantic::Metallic;
        request.GeneratedKey = "metallic";
        return request;
    }

    [[nodiscard]] const Runtime::ProgressiveSlotBinding* FindSlot(
        const Runtime::ProgressivePresentationBindings& bindings,
        const Runtime::ProgressiveSlotSemantic semantic)
    {
        const auto* presentation =
            Runtime::FindPresentationBinding(bindings, "mesh.surface");
        if (presentation == nullptr)
            return nullptr;
        return Runtime::FindSlotBinding(*presentation, semantic);
    }

    void ExpectPackedMetallicRoughnessPayload(
        const Assets::AssetTexture2DPayload& texture,
        const Runtime::ProgressiveSlotSemantic semantic)
    {
        ASSERT_EQ(texture.Metadata.PixelFormat,
                  Assets::AssetTexturePixelFormat::Rgba8Unorm);
        ASSERT_EQ(texture.Metadata.ColorSpace,
                  Assets::AssetTextureColorSpace::Linear);
        ASSERT_EQ(texture.Metadata.Components, 4u);

        const std::size_t pixelCount =
            static_cast<std::size_t>(texture.Metadata.Width) *
            static_cast<std::size_t>(texture.Metadata.Height);
        ASSERT_EQ(texture.PixelBytes.size(), pixelCount * 4u);

        bool sawAuthoredValue = false;
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::size_t offset = pixel * 4u;
            EXPECT_EQ(texture.PixelBytes[offset + 0u], std::byte{0xFF});
            EXPECT_EQ(texture.PixelBytes[offset + 3u], std::byte{0xFF});
            if (semantic == Runtime::ProgressiveSlotSemantic::Roughness)
            {
                EXPECT_EQ(texture.PixelBytes[offset + 2u], std::byte{0x00});
                sawAuthoredValue =
                    sawAuthoredValue ||
                    texture.PixelBytes[offset + 1u] != std::byte{0xFF};
            }
            else
            {
                EXPECT_EQ(texture.PixelBytes[offset + 1u], std::byte{0xFF});
                sawAuthoredValue =
                    sawAuthoredValue ||
                    texture.PixelBytes[offset + 2u] != std::byte{0x00};
            }
        }
        EXPECT_TRUE(sawAuthoredValue);
    }
}

TEST(RuntimeSelectedMeshTextureBake, BuildsVertexNormalBakeRequest)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);

    const Runtime::SelectedMeshTextureBakeBuildResult build =
        Runtime::BuildSelectedMeshTextureBakeRequest(
            scene,
            MakeNormalRequest(entity));

    ASSERT_EQ(build.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    EXPECT_EQ(build.BakeRequest.SourceDomain,
              Runtime::MeshAttributeTextureBakeSourceDomain::Vertex);
    EXPECT_EQ(build.BakeRequest.SourcePropertyName, "v:normal");
    EXPECT_EQ(build.BakeRequest.ValueKind,
              Runtime::MeshAttributeTextureBakeValueKind::Vector3);
    EXPECT_EQ(build.BakeRequest.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::Normal);
    EXPECT_EQ(build.ExpectedElementCount, 3u);
    EXPECT_NE(build.GeneratedAssetPath.find("selected-mesh-"), std::string::npos);
    EXPECT_NE(build.GeneratedAssetPath.find("normal"), std::string::npos);
}

TEST(RuntimeSelectedMeshTextureBake, AutoEncodersMatchTargetMaterialSlots)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);

    Runtime::SelectedMeshTextureBakeRequest normal = MakeNormalRequest(entity);
    normal.Encoder = Runtime::MeshAttributeTextureBakeEncoder::Auto;
    const Runtime::SelectedMeshTextureBakeBuildResult normalBuild =
        Runtime::BuildSelectedMeshTextureBakeRequest(scene, normal);
    ASSERT_EQ(normalBuild.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    EXPECT_EQ(normalBuild.BakeRequest.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::Normal);

    const Runtime::SelectedMeshTextureBakeBuildResult albedoBuild =
        Runtime::BuildSelectedMeshTextureBakeRequest(
            scene,
            MakeAlbedoRequest(entity));
    ASSERT_EQ(albedoBuild.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    EXPECT_EQ(albedoBuild.BakeRequest.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::RgbaColor);

    const Runtime::SelectedMeshTextureBakeBuildResult roughnessBuild =
        Runtime::BuildSelectedMeshTextureBakeRequest(
            scene,
            MakeRoughnessRequest(entity));
    ASSERT_EQ(roughnessBuild.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    EXPECT_EQ(roughnessBuild.BakeRequest.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::LinearScalar);

    const Runtime::SelectedMeshTextureBakeBuildResult metallicBuild =
        Runtime::BuildSelectedMeshTextureBakeRequest(
            scene,
            MakeMetallicRequest(entity));
    ASSERT_EQ(metallicBuild.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    EXPECT_EQ(metallicBuild.BakeRequest.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::LinearScalar);
}

TEST(RuntimeSelectedMeshTextureBake, IncompatibleMaterialSlotEncodersFailClosed)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);

    Runtime::SelectedMeshTextureBakeRequest badNormal = MakeNormalRequest(entity);
    badNormal.Encoder = Runtime::MeshAttributeTextureBakeEncoder::RgbaColor;
    EXPECT_EQ(
        Runtime::BuildSelectedMeshTextureBakeRequest(scene, badNormal).Status,
        Runtime::SelectedMeshTextureBakeStatus::IncompatibleTargetSlot);

    Runtime::SelectedMeshTextureBakeRequest badRoughness = MakeRoughnessRequest(entity);
    badRoughness.SourcePropertyName = "v:normal";
    badRoughness.ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3;
    badRoughness.Encoder = Runtime::MeshAttributeTextureBakeEncoder::Normal;
    EXPECT_EQ(
        Runtime::BuildSelectedMeshTextureBakeRequest(scene, badRoughness).Status,
        Runtime::SelectedMeshTextureBakeStatus::IncompatibleTargetSlot);

    Runtime::SelectedMeshTextureBakeRequest displacement = MakeHeatRequest(entity);
    displacement.TargetSemantic = Runtime::ProgressiveSlotSemantic::Displacement;
    EXPECT_EQ(
        Runtime::BuildSelectedMeshTextureBakeRequest(scene, displacement).Status,
        Runtime::SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic);
}

TEST(RuntimeSelectedMeshTextureBake, SynchronousCommandBakesBindsAndUsesHistory)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};
    Runtime::EditorCommandHistory history{};

    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
        .CommandHistory = &history,
    };

    const Runtime::SelectedMeshTextureBakeResult result =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);

    ASSERT_EQ(result.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(result.GeneratedTexture.IsValid());
    EXPECT_TRUE(result.BoundGeneratedTexture);
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(history.UndoCount(), 1u);

    const auto texture =
        assets.Read<Assets::AssetTexture2DPayload>(result.GeneratedTexture);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    EXPECT_EQ((*texture)[0].Metadata.Width, 4u);
    EXPECT_EQ((*texture)[0].Metadata.SourceKind,
              Assets::AssetTextureSourceKind::Generated);

    const auto& bindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressiveSlotBinding* slot =
        FindSlot(bindings, Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(slot->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_EQ(slot->GeneratedTexture, result.GeneratedTexture);
}

TEST(RuntimeSelectedMeshTextureBake, SynchronousCommandBakesFaceColorToAlbedoSlot)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};

    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
    };

    const Runtime::SelectedMeshTextureBakeResult result =
        Runtime::ApplySelectedMeshTextureBakeCommand(
            context,
            MakeFaceAlbedoRequest(entity));

    ASSERT_EQ(result.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(result.GeneratedTexture.IsValid());
    EXPECT_TRUE(result.BoundGeneratedTexture);
    EXPECT_EQ(result.BakeDiagnostics.SourceDomain,
              Runtime::MeshAttributeTextureBakeSourceDomain::Face);
    EXPECT_EQ(result.BakeDiagnostics.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::RgbaColor);

    const auto texture =
        assets.Read<Assets::AssetTexture2DPayload>(result.GeneratedTexture);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    EXPECT_EQ((*texture)[0].Metadata.PixelFormat,
              Assets::AssetTexturePixelFormat::Rgba8Unorm);
    EXPECT_EQ((*texture)[0].Metadata.ColorSpace,
              Assets::AssetTextureColorSpace::SRGB);

    const auto& bindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressiveSlotBinding* albedo =
        FindSlot(bindings, Runtime::ProgressiveSlotSemantic::Albedo);
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(albedo->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_EQ(albedo->GeneratedTexture, result.GeneratedTexture);
}

TEST(RuntimeSelectedMeshTextureBake, SynchronousCommandBakesScalarToRoughnessSlot)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};

    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
    };

    const Runtime::SelectedMeshTextureBakeResult result =
        Runtime::ApplySelectedMeshTextureBakeCommand(
            context,
            MakeRoughnessRequest(entity));

    ASSERT_EQ(result.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(result.GeneratedTexture.IsValid());
    EXPECT_TRUE(result.BoundGeneratedTexture);
    EXPECT_EQ(result.BakeDiagnostics.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::LinearScalar);

    const auto texture =
        assets.Read<Assets::AssetTexture2DPayload>(result.GeneratedTexture);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    ExpectPackedMetallicRoughnessPayload(
        (*texture)[0],
        Runtime::ProgressiveSlotSemantic::Roughness);

    const auto& bindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressiveSlotBinding* roughness =
        FindSlot(bindings, Runtime::ProgressiveSlotSemantic::Roughness);
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(roughness->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(roughness->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_EQ(roughness->GeneratedTexture, result.GeneratedTexture);
}

TEST(RuntimeSelectedMeshTextureBake, SynchronousCommandBakesScalarToMetallicSlot)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};

    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
    };

    const Runtime::SelectedMeshTextureBakeResult result =
        Runtime::ApplySelectedMeshTextureBakeCommand(
            context,
            MakeMetallicRequest(entity));

    ASSERT_EQ(result.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(result.GeneratedTexture.IsValid());
    EXPECT_TRUE(result.BoundGeneratedTexture);
    EXPECT_EQ(result.BakeDiagnostics.Encoder,
              Runtime::MeshAttributeTextureBakeEncoder::LinearScalar);

    const auto texture =
        assets.Read<Assets::AssetTexture2DPayload>(result.GeneratedTexture);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    ExpectPackedMetallicRoughnessPayload(
        (*texture)[0],
        Runtime::ProgressiveSlotSemantic::Metallic);

    const auto& bindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressiveSlotBinding* metallic =
        FindSlot(bindings, Runtime::ProgressiveSlotSemantic::Metallic);
    ASSERT_NE(metallic, nullptr);
    EXPECT_EQ(metallic->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(metallic->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_EQ(metallic->GeneratedTexture, result.GeneratedTexture);
}

TEST(RuntimeSelectedMeshTextureBake, DistinctPropertiesCreateDistinctGeneratedTexturesAndBindings)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};

    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
    };

    const Runtime::SelectedMeshTextureBakeResult normal =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, MakeNormalRequest(entity));
    ASSERT_EQ(normal.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(normal.GeneratedTexture.IsValid());

    const Runtime::SelectedMeshTextureBakeResult heat =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, MakeHeatRequest(entity));
    ASSERT_EQ(heat.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(heat.GeneratedTexture.IsValid());

    EXPECT_NE(normal.GeneratedTexture, heat.GeneratedTexture);
    EXPECT_NE(normal.GeneratedAssetPath, heat.GeneratedAssetPath);
    EXPECT_NE(normal.GeneratedAssetPath.find("normal"), std::string::npos);
    EXPECT_NE(heat.GeneratedAssetPath.find("heat"), std::string::npos);

    const auto normalTexture =
        assets.Read<Assets::AssetTexture2DPayload>(normal.GeneratedTexture);
    const auto heatTexture =
        assets.Read<Assets::AssetTexture2DPayload>(heat.GeneratedTexture);
    ASSERT_TRUE(normalTexture.has_value());
    ASSERT_TRUE(heatTexture.has_value());
    ASSERT_EQ(normalTexture->size(), 1u);
    ASSERT_EQ(heatTexture->size(), 1u);
    EXPECT_EQ((*normalTexture)[0].Metadata.SourceKind,
              Assets::AssetTextureSourceKind::Generated);
    EXPECT_EQ((*heatTexture)[0].Metadata.SourceKind,
              Assets::AssetTextureSourceKind::Generated);

    const auto& bindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressiveSlotBinding* normalSlot =
        FindSlot(bindings, Runtime::ProgressiveSlotSemantic::Normal);
    const Runtime::ProgressiveSlotBinding* albedoSlot =
        FindSlot(bindings, Runtime::ProgressiveSlotSemantic::Albedo);
    ASSERT_NE(normalSlot, nullptr);
    ASSERT_NE(albedoSlot, nullptr);
    EXPECT_EQ(normalSlot->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(albedoSlot->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(normalSlot->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_EQ(albedoSlot->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_EQ(normalSlot->GeneratedTexture, normal.GeneratedTexture);
    EXPECT_EQ(albedoSlot->GeneratedTexture, heat.GeneratedTexture);
}

TEST(RuntimeSelectedMeshTextureBake, ExplicitExistingGeneratedTextureReloadsPayload)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};

    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
    };
    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    request.BindGeneratedTexture = false;

    const Runtime::SelectedMeshTextureBakeResult first =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);
    ASSERT_EQ(first.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(first.GeneratedTexture.IsValid());
    FlushAssetEvents(assets);

    auto& vertices = scene.Raw().get<GS::Vertices>(entity);
    vertices.Properties.Get<glm::vec3>("v:normal").Vector() = {
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    request.ExistingGeneratedTexture = first.GeneratedTexture;

    const Runtime::SelectedMeshTextureBakeResult second =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);
    ASSERT_EQ(second.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    EXPECT_EQ(second.GeneratedTexture, first.GeneratedTexture);

    const auto texture =
        assets.Read<Assets::AssetTexture2DPayload>(second.GeneratedTexture);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    ASSERT_GE((*texture)[0].PixelBytes.size(), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>((*texture)[0].PixelBytes[0]), 255u);
}

TEST(RuntimeSelectedMeshTextureBake, InvalidRequestsFailBeforeScheduling)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle mesh = MakeMeshEntity(scene);

    Runtime::SelectedMeshTextureBakeRequest missing = MakeNormalRequest(mesh);
    missing.SourcePropertyName = "v:missing";
    EXPECT_EQ(Runtime::BuildSelectedMeshTextureBakeRequest(scene, missing).Status,
              Runtime::SelectedMeshTextureBakeStatus::MissingProperty);

    Runtime::SelectedMeshTextureBakeRequest invalidResolution =
        MakeNormalRequest(mesh);
    invalidResolution.Width = 0u;
    EXPECT_EQ(
        Runtime::BuildSelectedMeshTextureBakeRequest(scene, invalidResolution)
            .Status,
        Runtime::SelectedMeshTextureBakeStatus::InvalidResolution);

    const ECS::EntityHandle cloud = scene.Create();
    auto& points = scene.Raw().emplace<GS::Vertices>(cloud);
    points.Properties.Resize(1u);
    points.Properties
        .GetOrAdd<glm::vec3>(std::string{GS::PropertyNames::kPosition}, glm::vec3{0.0f})
        .Vector() = {{0.0f, 0.0f, 0.0f}};

    Runtime::SelectedMeshTextureBakeRequest nonMesh = MakeNormalRequest(mesh);
    nonMesh.StableEntityId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    EXPECT_EQ(Runtime::BuildSelectedMeshTextureBakeRequest(scene, nonMesh).Status,
              Runtime::SelectedMeshTextureBakeStatus::NonMeshSelection);
}

TEST(RuntimeSelectedMeshTextureBake, DerivedJobAppliesGeneratedTextureOnMainThread)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};
    Runtime::EditorCommandHistory history{};
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};

    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    request.PreferDerivedJob = true;
    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
        .CommandHistory = &history,
        .DerivedJobs = &jobs,
    };

    const Runtime::SelectedMeshTextureBakeResult scheduled =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);

    ASSERT_EQ(scheduled.Status, Runtime::SelectedMeshTextureBakeStatus::Scheduled);
    ASSERT_TRUE(scheduled.Job.IsValid());
    EXPECT_TRUE(scheduled.BoundGeneratedTexture);
    EXPECT_TRUE(history.IsDirty());

    const auto& pendingBindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressiveSlotBinding* pendingSlot =
        FindSlot(pendingBindings, Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(pendingSlot, nullptr);
    EXPECT_EQ(pendingSlot->Readiness, Runtime::ProgressiveReadinessState::Pending);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    jobs.ApplyMainThreadResults();

    const auto snapshot = jobs.Snapshot(scheduled.Job);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->Status, Runtime::DerivedJobStatus::Complete);

    const auto& readyBindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    const Runtime::ProgressiveSlotBinding* readySlot =
        FindSlot(readyBindings, Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(readySlot, nullptr);
    EXPECT_EQ(readySlot->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(readySlot->Readiness, Runtime::ProgressiveReadinessState::Ready);
    EXPECT_TRUE(readySlot->GeneratedTexture.IsValid());
}

TEST(RuntimeSelectedMeshTextureBake, DerivedJobStaleBindingApplyIsDiscarded)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};

    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    request.PreferDerivedJob = true;
    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
        .DerivedJobs = &jobs,
    };

    const Runtime::SelectedMeshTextureBakeResult scheduled =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);
    ASSERT_EQ(scheduled.Status, Runtime::SelectedMeshTextureBakeStatus::Scheduled);

    auto& bindings =
        scene.Raw().get<Runtime::ProgressivePresentationBindings>(entity);
    ++bindings.BindingGeneration;

    jobs.Pump(1u);
    jobs.DrainCompletions();
    jobs.ApplyMainThreadResults();

    const auto snapshot = jobs.Snapshot(scheduled.Job);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->Status, Runtime::DerivedJobStatus::StaleDiscarded);
    const Runtime::ProgressiveSlotBinding* slot =
        FindSlot(bindings, Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->GeneratedTexture.IsValid());
}
