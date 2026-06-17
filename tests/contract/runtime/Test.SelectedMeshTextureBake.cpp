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
                    .Slots = {albedo, normal},
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
