#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.TextureBakeModule;

#include "MockRHI.hpp"

namespace Assets = Extrinsic::Assets;
namespace ECS = Extrinsic::ECS;
namespace Graphics = Extrinsic::Graphics;
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
        edges.Properties
            .GetOrAdd<float>("e:weight", 0.0f)
            .Vector() = {0.25f, 0.5f, 0.75f};

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

    [[nodiscard]] Runtime::GeometryPresentationRecipe MakeBindings()
    {
        Runtime::GeometryPresentationSlotRecipe albedo{};
        albedo.Semantic = Runtime::GeometryPresentationSlotSemantic::Albedo;
        albedo.SourceKind = Runtime::GeometryPresentationSourceKind::UniformDefault;
        albedo.UniformDefault.Kind = Geometry::PropertyValueKind::Vec4;
        albedo.UniformDefault.Vector = glm::vec4{1.0f};
        Runtime::GeometryPresentationSlotRecipe normal{};
        normal.Semantic = Runtime::GeometryPresentationSlotSemantic::Normal;
        normal.SourceKind = Runtime::GeometryPresentationSourceKind::PropertyBake;
        normal.Property = Runtime::GeometryPropertyRef{
            .Domain = Runtime::GeometryElementDomain::MeshVertex,
            .Name = "v:normal",
            .ValueKind = Geometry::PropertyValueKind::Vec3,
        };
        normal.GeneratedPolicy =
            Runtime::GeometryGeneratedOutputPolicy::DeterministicChildAsset;

        Runtime::GeometryPresentationSlotRecipe roughness{};
        roughness.Semantic = Runtime::GeometryPresentationSlotSemantic::Roughness;
        roughness.SourceKind = Runtime::GeometryPresentationSourceKind::UniformDefault;
        roughness.UniformDefault.Kind =
            Geometry::PropertyValueKind::Float;
        roughness.UniformDefault.Scalar = 0.5;

        Runtime::GeometryPresentationSlotRecipe metallic{};
        metallic.Semantic = Runtime::GeometryPresentationSlotSemantic::Metallic;
        metallic.SourceKind = Runtime::GeometryPresentationSourceKind::UniformDefault;
        metallic.UniformDefault.Kind =
            Geometry::PropertyValueKind::Float;
        metallic.UniformDefault.Scalar = 0.0;

        return Runtime::GeometryPresentationRecipe{
            .Shape = Runtime::GeometryPresentationShape::Mesh,
            .Lanes = {
                Runtime::GeometryPresentationLaneRecipe{
                    .Lane = Runtime::GeometryRenderLane::Surface,
                    .PresentationKey = "mesh.surface",
                },
            },
            .Presentations = {
                Runtime::GeometryPresentationBindingRecipe{
                    .Key = "mesh.surface",
                    .Kind = Runtime::GeometryPresentationKind::SurfaceMaterial,
                    .Slots = {albedo, normal, roughness, metallic},
                },
            },
        };
    }

    [[nodiscard]] Runtime::GeometryPresentationRuntimeState MakeRuntimeState()
    {
        return Runtime::GeometryPresentationRuntimeState{
            .RecipeGeneration = 7u,
            .Slots = {
                Runtime::GeometryPresentationSlotStatus{
                    .PresentationKey = "mesh.surface",
                    .Semantic =
                        Runtime::GeometryPresentationSlotSemantic::Normal,
                    .Readiness =
                        Runtime::GeometryPresentationReadiness::Pending,
                    .Provenance =
                        Runtime::GeometryPresentationProvenance::PropertyBinding,
                },
            },
        };
    }

    [[nodiscard]] ECS::EntityHandle MakeMeshEntity(
        ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = scene.Create();
        SetMeshTopology(scene, entity);
        scene.Raw().emplace<Runtime::GeometryPresentationRecipe>(
            entity,
            MakeBindings());
        scene.Raw().emplace<Runtime::GeometryPresentationRuntimeState>(
            entity,
            MakeRuntimeState());
        return entity;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeNormalRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::GeometryElementDomain::MeshVertex;
        request.SourcePropertyName = "v:normal";
        request.ExpectedValueKind = Geometry::PropertyValueKind::Vec3;
        request.Encoder = Runtime::MeshAttributeTextureBakeEncoder::Normal;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Normal;
        request.GeneratedKey = "normal";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeHeatRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::GeometryElementDomain::MeshVertex;
        request.SourcePropertyName = "v:heat";
        request.ExpectedValueKind = Geometry::PropertyValueKind::Float;
        request.Encoder = Runtime::MeshAttributeTextureBakeEncoder::LinearScalar;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Albedo;
        request.GeneratedKey = "heat";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeAlbedoRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::GeometryElementDomain::MeshVertex;
        request.SourcePropertyName = "v:albedo";
        request.ExpectedValueKind = Geometry::PropertyValueKind::Vec4;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Albedo;
        request.GeneratedKey = "albedo";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeFaceAlbedoRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request = MakeAlbedoRequest(entity);
        request.SourceDomain = Runtime::GeometryElementDomain::MeshFace;
        request.SourcePropertyName = "f:debug_color";
        request.ExpectedValueKind = Geometry::PropertyValueKind::Vec4;
        request.GeneratedKey = "face-albedo";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeRoughnessRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request{};
        request.StableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        request.SourceDomain = Runtime::GeometryElementDomain::MeshVertex;
        request.SourcePropertyName = "v:roughness";
        request.ExpectedValueKind = Geometry::PropertyValueKind::Float;
        request.Width = 4u;
        request.Height = 4u;
        request.TargetPresentationKey = "mesh.surface";
        request.TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Roughness;
        request.GeneratedKey = "roughness";
        return request;
    }

    [[nodiscard]] Runtime::SelectedMeshTextureBakeRequest MakeMetallicRequest(
        const ECS::EntityHandle entity)
    {
        Runtime::SelectedMeshTextureBakeRequest request = MakeRoughnessRequest(entity);
        request.SourcePropertyName = "v:metallic";
        request.TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Metallic;
        request.GeneratedKey = "metallic";
        return request;
    }

    [[nodiscard]] const Runtime::GeometryPresentationSlotRecipe* FindSlot(
        const Runtime::GeometryPresentationRecipe& bindings,
        const Runtime::GeometryPresentationSlotSemantic semantic)
    {
        const auto* presentation =
            Runtime::FindGeometryPresentationBinding(bindings, "mesh.surface");
        if (presentation == nullptr)
            return nullptr;
        return Runtime::FindGeometryPresentationSlot(*presentation, semantic);
    }

    [[nodiscard]] const Runtime::GeometryPresentationSlotStatus* FindStatus(
        const ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity,
        const Runtime::GeometryPresentationSlotSemantic semantic)
    {
        const auto* state =
            scene.Raw().try_get<Runtime::GeometryPresentationRuntimeState>(entity);
        if (state == nullptr)
            return nullptr;
        return Runtime::FindGeometryPresentationSlotStatus(
            *state,
            "mesh.surface",
            semantic);
    }

    void ExpectPackedMetallicRoughnessPayload(
        const Assets::AssetTexture2DPayload& texture,
        const Runtime::GeometryPresentationSlotSemantic semantic)
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
            if (semantic == Runtime::GeometryPresentationSlotSemantic::Roughness)
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

    // `JobService` dispatches at submit onto the shared CPU pool and has no
    // inline fallback, so the async bake needs a live scheduler.
    //
    // Declare it *last* in a test: its destructor quiesces the pool, and that
    // has to happen while every object a worker task can still reach is alive.
    // A live scheduler also makes `AssetService::Load` decode on a worker
    // rather than inline, so the pipeline outliving its service is a real
    // use-after-free, not a theoretical one.
    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workers = 1)
        {
            if (Extrinsic::Core::Tasks::Scheduler::IsInitialized())
            {
                Extrinsic::Core::Tasks::Scheduler::Shutdown();
            }
            Extrinsic::Core::Tasks::Scheduler::Initialize(workers);
        }

        ~SchedulerScope()
        {
            Extrinsic::Core::Tasks::Scheduler::WaitForAll();
            Extrinsic::Core::Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    [[nodiscard]] bool IsTerminalJobState(const Runtime::JobState state)
    {
        switch (state)
        {
        case Runtime::JobState::Published:
        case Runtime::JobState::Dropped:
        case Runtime::JobState::Cancelled:
        case Runtime::JobState::Rejected:
        case Runtime::JobState::StaleDiscarded:
            return true;
        default:
            return false;
        }
    }

    // Per RUNTIME-194 Slice B0, drain until the job is terminal rather than
    // asserting on a fixed drain count.
    [[nodiscard]] bool DrainUntilTerminal(
        Runtime::JobService& jobs,
        Runtime::KernelEventBus& events,
        const Runtime::JobToken token,
        const std::chrono::milliseconds timeout = std::chrono::seconds{5})
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;)
        {
            Extrinsic::Core::Tasks::Scheduler::WaitForAll();
            (void)jobs.DrainCompletions(events);
            if (IsTerminalJobState(jobs.GetState(token)))
            {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
}

TEST(RuntimeTextureBakeModule, RepresentationDefaultsPreserveRawScalarData)
{
    const std::vector<Runtime::TextureBakeConsumerBinding> consumers{
        Runtime::TextureBakeConsumerBinding{
            .PresentationKey = "mesh.surface",
            .Semantic = Runtime::GeometryPresentationSlotSemantic::Albedo,
        },
        Runtime::TextureBakeConsumerBinding{
            .PresentationKey = "mesh.surface",
            .Semantic = Runtime::GeometryPresentationSlotSemantic::ScalarField,
        },
    };
    const Runtime::PropertyTextureBakeRepresentation representation =
        Runtime::ResolvePropertyTextureBakeRepresentation(
            Geometry::PropertyValueKind::Float,
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeEncoding::Auto,
            consumers);

    EXPECT_EQ(
        representation.Storage,
        Runtime::PropertyTextureBakeStorage::RawFloat);
    EXPECT_EQ(
        representation.Encoding,
        Runtime::PropertyTextureBakeEncoding::LinearScalar);
    EXPECT_TRUE(Runtime::IsPropertyTextureBakeConsumerCompatible(
        consumers[0],
        Geometry::PropertyValueKind::Float,
        representation.Storage,
        representation.Encoding));
    EXPECT_TRUE(Runtime::IsPropertyTextureBakeConsumerCompatible(
        consumers[1],
        Geometry::PropertyValueKind::Float,
        representation.Storage,
        representation.Encoding));
}

TEST(RuntimeTextureBakeModule, NormalAndLabelDefaultsChooseEncodedStorage)
{
    const std::vector<Runtime::TextureBakeConsumerBinding> normalConsumer{
        Runtime::TextureBakeConsumerBinding{
            .PresentationKey = "mesh.surface",
            .Semantic = Runtime::GeometryPresentationSlotSemantic::Normal,
        },
    };
    const auto normal = Runtime::ResolvePropertyTextureBakeRepresentation(
        Geometry::PropertyValueKind::Vec3,
        Runtime::PropertyTextureBakeStorage::Auto,
        Runtime::PropertyTextureBakeEncoding::Auto,
        normalConsumer);
    EXPECT_EQ(
        normal.Storage,
        Runtime::PropertyTextureBakeStorage::EncodedRgba);
    EXPECT_EQ(
        normal.Encoding,
        Runtime::PropertyTextureBakeEncoding::Normal);
    EXPECT_TRUE(Runtime::IsPropertyTextureBakeConsumerCompatible(
        normalConsumer.front(),
        Geometry::PropertyValueKind::Vec3,
        normal.Storage,
        normal.Encoding));

    const std::vector<Runtime::TextureBakeConsumerBinding> albedoConsumer{
        Runtime::TextureBakeConsumerBinding{
            .PresentationKey = "mesh.surface",
            .Semantic = Runtime::GeometryPresentationSlotSemantic::Albedo,
        },
    };
    const auto label = Runtime::ResolvePropertyTextureBakeRepresentation(
        Geometry::PropertyValueKind::UInt32,
        Runtime::PropertyTextureBakeStorage::Auto,
        Runtime::PropertyTextureBakeEncoding::Auto,
        albedoConsumer);
    EXPECT_EQ(
        label.Storage,
        Runtime::PropertyTextureBakeStorage::EncodedRgba);
    EXPECT_EQ(
        label.Encoding,
        Runtime::PropertyTextureBakeEncoding::LabelPalette);
}

TEST(RuntimeTextureBakeModule,
     RepresentationMatrixRejectsEncodersThatDoNotMatchStorageAndValueType)
{
    using Runtime::IsPropertyTextureBakeRepresentationCompatible;
    using Runtime::PropertyTextureBakeEncoding;
    using Runtime::PropertyTextureBakeStorage;

    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::LinearScalar));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::ScalarColormap));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::UInt32,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::LabelPalette));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::UInt32,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::LabelPalette));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec3,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec4,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec3,
        PropertyTextureBakeStorage::Auto,
        PropertyTextureBakeEncoding::Vector3));
}

TEST(RuntimeTextureBakeModule, ServiceFailsClosedWithoutGpuComposition)
{
    Runtime::TextureBakeService service{};
    EXPECT_FALSE(service.Available());
    const Runtime::PropertyTextureBakeResult result =
        service.Bake(Runtime::PropertyTextureBakeRequest{});
    EXPECT_EQ(
        result.Status,
        Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
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

TEST(RuntimeSelectedMeshTextureBake, BuildsNearestEdgeScalarBakeRequest)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Runtime::SelectedMeshTextureBakeRequest request = MakeHeatRequest(entity);
    request.SourceDomain = Runtime::GeometryElementDomain::MeshEdge;
    request.SourcePropertyName = "e:weight";
    request.OutputName = "edge-weight";

    const Runtime::SelectedMeshTextureBakeBuildResult build =
        Runtime::BuildSelectedMeshTextureBakeRequest(scene, request);
    ASSERT_EQ(build.Status, Runtime::SelectedMeshTextureBakeStatus::Success);
    EXPECT_EQ(
        build.BakeRequest.SourceDomain,
        Runtime::MeshAttributeTextureBakeSourceDomain::Edge);
    EXPECT_EQ(build.ExpectedElementCount, 3u);
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
    badRoughness.ExpectedValueKind = Geometry::PropertyValueKind::Vec3;
    badRoughness.Encoder = Runtime::MeshAttributeTextureBakeEncoder::Normal;
    EXPECT_EQ(
        Runtime::BuildSelectedMeshTextureBakeRequest(scene, badRoughness).Status,
        Runtime::SelectedMeshTextureBakeStatus::IncompatibleTargetSlot);

    Runtime::SelectedMeshTextureBakeRequest displacement = MakeHeatRequest(entity);
    displacement.TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Displacement;
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
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* slot =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* status =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->Readiness,
              Runtime::GeometryPresentationReadiness::Ready);
    EXPECT_EQ(status->GeneratedTexture, result.GeneratedTexture);
}

TEST(RuntimeSelectedMeshTextureBake, ObjectSpaceNormalQueueSchedulesSelectedMeshNormalBakeWithoutAssetService)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Runtime::RuntimeObjectSpaceNormalBakeQueue normalBakeQueue{};
    Extrinsic::Tests::MockDevice device{};

    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    request.Width = 64u;
    request.Height = 128u;
    request.SourceGeneration = 12u;
    request.DirtyStamp = 34u;
    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .BindingEpoch = 1u,
        .ObjectSpaceNormalBakeQueue = &normalBakeQueue,
        .ObjectSpaceNormalBakeDevice = &device,
    };

    const Runtime::SelectedMeshTextureBakeResult result =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);

    ASSERT_EQ(result.Status, Runtime::SelectedMeshTextureBakeStatus::Scheduled);
    EXPECT_EQ(result.ExecutionMode,
              Runtime::SelectedMeshTextureBakeExecutionMode::ObjectSpaceNormalBakeQueue);
    EXPECT_FALSE(result.GeneratedTexture.IsValid());
    EXPECT_FALSE(result.BoundGeneratedTexture);
    EXPECT_EQ(result.RecipeGeneration, 8u);
    EXPECT_EQ(normalBakeQueue.PendingCount(), 1u);
    EXPECT_GT(normalBakeQueue.PendingIdentityByteCount(), 0u);
    EXPECT_EQ(normalBakeQueue.Diagnostics().QueuedRequests, 1u);

    const auto& bindings =
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* slot =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->SourceKind, Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* status =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->Readiness,
              Runtime::GeometryPresentationReadiness::Pending);
    EXPECT_FALSE(status->GeneratedTexture.IsValid());

    auto pending = normalBakeQueue.TakePendingSubmissions();
    ASSERT_EQ(pending.size(), 1u);
    ASSERT_TRUE(pending.front().Identity.has_value());
    EXPECT_EQ(pending.front().Target.Entity, entity);
    EXPECT_EQ(pending.front().Target.StableEntityId, request.StableEntityId);
    EXPECT_EQ(pending.front().Target.BindingEpoch, 1u);
    EXPECT_EQ(pending.front().Target.PresentationKey, "mesh.surface");
    EXPECT_EQ(pending.front().Target.ExpectedRecipeGeneration, 8u);
    EXPECT_EQ(pending.front().Identity->Width, 64u);
    EXPECT_EQ(pending.front().Identity->Height, 128u);
    const auto completion = normalBakeQueue.Complete(
        pending.front().StaleKey,
        Assets::AssetId{71u, 1u},
        Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::IdentityInserted);
    EXPECT_EQ(completion.Status,
              Runtime::RuntimeObjectSpaceNormalBakeStatus::ReadyForBinding);
}

TEST(RuntimeSelectedMeshTextureBake, ObjectSpaceNormalQueueNoOpsOnNonOperationalBackend)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Runtime::RuntimeObjectSpaceNormalBakeQueue normalBakeQueue{};

    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    request.Width = 64u;
    request.Height = 64u;
    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .BindingEpoch = 1u,
        .ObjectSpaceNormalBakeQueue = &normalBakeQueue,
        .ObjectSpaceNormalBakeDevice = nullptr,
    };

    const Runtime::SelectedMeshTextureBakeResult result =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);

    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SelectedMeshTextureBakeStatus::NonOperationalBackend);
    EXPECT_EQ(result.ExecutionMode,
              Runtime::SelectedMeshTextureBakeExecutionMode::ObjectSpaceNormalBakeQueue);
    EXPECT_NE(result.Diagnostic.find("no CPU fallback"), std::string::npos);
    EXPECT_EQ(result.RecipeGeneration, 0u);
    EXPECT_EQ(normalBakeQueue.PendingCount(), 0u);
    EXPECT_EQ(normalBakeQueue.PendingIdentityByteCount(), 0u);
    EXPECT_EQ(normalBakeQueue.Diagnostics().NonOperationalNoOps, 1u);

    const auto& bindings =
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    EXPECT_EQ(
        scene.Raw()
            .get<Runtime::GeometryPresentationRuntimeState>(entity)
            .RecipeGeneration,
        7u);
    const Runtime::GeometryPresentationSlotRecipe* slot =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->SourceKind, Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* status =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->Readiness,
              Runtime::GeometryPresentationReadiness::Pending);
    EXPECT_FALSE(status->GeneratedTexture.IsValid());
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
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* albedo =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Albedo);
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* albedoStatus =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Albedo);
    ASSERT_NE(albedoStatus, nullptr);
    EXPECT_EQ(albedoStatus->Readiness,
              Runtime::GeometryPresentationReadiness::Ready);
    EXPECT_EQ(albedoStatus->GeneratedTexture, result.GeneratedTexture);
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
        Runtime::GeometryPresentationSlotSemantic::Roughness);

    const auto& bindings =
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* roughness =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Roughness);
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(roughness->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* roughnessStatus =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Roughness);
    ASSERT_NE(roughnessStatus, nullptr);
    EXPECT_EQ(roughnessStatus->Readiness,
              Runtime::GeometryPresentationReadiness::Ready);
    EXPECT_EQ(roughnessStatus->GeneratedTexture, result.GeneratedTexture);
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
        Runtime::GeometryPresentationSlotSemantic::Metallic);

    const auto& bindings =
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* metallic =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Metallic);
    ASSERT_NE(metallic, nullptr);
    EXPECT_EQ(metallic->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* metallicStatus =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Metallic);
    ASSERT_NE(metallicStatus, nullptr);
    EXPECT_EQ(metallicStatus->Readiness,
              Runtime::GeometryPresentationReadiness::Ready);
    EXPECT_EQ(metallicStatus->GeneratedTexture, result.GeneratedTexture);
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
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* normalSlot =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Normal);
    const Runtime::GeometryPresentationSlotRecipe* albedoSlot =
        FindSlot(bindings, Runtime::GeometryPresentationSlotSemantic::Albedo);
    ASSERT_NE(normalSlot, nullptr);
    ASSERT_NE(albedoSlot, nullptr);
    EXPECT_EQ(normalSlot->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    EXPECT_EQ(albedoSlot->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* normalStatus =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Normal);
    const Runtime::GeometryPresentationSlotStatus* albedoStatus =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Albedo);
    ASSERT_NE(normalStatus, nullptr);
    ASSERT_NE(albedoStatus, nullptr);
    EXPECT_EQ(normalStatus->Readiness,
              Runtime::GeometryPresentationReadiness::Ready);
    EXPECT_EQ(albedoStatus->Readiness,
              Runtime::GeometryPresentationReadiness::Ready);
    EXPECT_EQ(normalStatus->GeneratedTexture, normal.GeneratedTexture);
    EXPECT_EQ(albedoStatus->GeneratedTexture, heat.GeneratedTexture);
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

TEST(RuntimeSelectedMeshTextureBake, AsyncJobAppliesGeneratedTextureOnMainThread)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};
    Runtime::EditorCommandHistory history{};
    Runtime::JobService jobs{};
    Runtime::KernelEventBus events{};
    // Declared last so it is destroyed first: with a live scheduler
    // `AssetService::Load` decodes on a worker instead of inline, so the pool
    // must be quiesced while everything a worker can touch is still alive.
    SchedulerScope scheduler{};

    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    request.PreferAsyncJob = true;
    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
        .CommandHistory = &history,
        .Jobs = &jobs,
    };

    const Runtime::SelectedMeshTextureBakeResult scheduled =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);

    ASSERT_EQ(scheduled.Status, Runtime::SelectedMeshTextureBakeStatus::Scheduled);
    ASSERT_TRUE(scheduled.Job.IsValid());
    EXPECT_FALSE(scheduled.BoundGeneratedTexture);
    EXPECT_TRUE(history.IsDirty());

    const auto& pendingBindings =
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* pendingSlot =
        FindSlot(pendingBindings, Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(pendingSlot, nullptr);
    EXPECT_EQ(pendingSlot->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* pendingStatus =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(pendingStatus, nullptr);
    EXPECT_EQ(pendingStatus->Readiness,
              Runtime::GeometryPresentationReadiness::Pending);

    ASSERT_TRUE(DrainUntilTerminal(jobs, events, scheduled.Job));
    EXPECT_EQ(jobs.GetState(scheduled.Job), Runtime::JobState::Published);

    const auto& readyBindings =
        scene.Raw().get<Runtime::GeometryPresentationRecipe>(entity);
    const Runtime::GeometryPresentationSlotRecipe* readySlot =
        FindSlot(readyBindings, Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(readySlot, nullptr);
    EXPECT_EQ(readySlot->SourceKind,
              Runtime::GeometryPresentationSourceKind::PropertyBake);
    const Runtime::GeometryPresentationSlotStatus* readyStatus =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(readyStatus, nullptr);
    EXPECT_EQ(readyStatus->Readiness,
              Runtime::GeometryPresentationReadiness::Ready);
    EXPECT_TRUE(readyStatus->GeneratedTexture.IsValid());
}

TEST(RuntimeSelectedMeshTextureBake, AsyncJobStaleBindingApplyIsDiscarded)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle entity = MakeMeshEntity(scene);
    Assets::AssetService assets{};
    Runtime::JobService jobs{};
    Runtime::KernelEventBus events{};
    // Destroyed first — see the note in the sibling apply test.
    SchedulerScope scheduler{};

    Runtime::SelectedMeshTextureBakeRequest request = MakeNormalRequest(entity);
    request.PreferAsyncJob = true;
    Runtime::SelectedMeshTextureBakeContext context{
        .Scene = &scene,
        .AssetService = &assets,
        .Jobs = &jobs,
    };

    const Runtime::SelectedMeshTextureBakeResult scheduled =
        Runtime::ApplySelectedMeshTextureBakeCommand(context, request);
    ASSERT_EQ(scheduled.Status, Runtime::SelectedMeshTextureBakeStatus::Scheduled);

    auto& runtimeState =
        scene.Raw().get<Runtime::GeometryPresentationRuntimeState>(entity);
    ++runtimeState.RecipeGeneration;

    ASSERT_TRUE(DrainUntilTerminal(jobs, events, scheduled.Job));
    EXPECT_EQ(jobs.GetState(scheduled.Job), Runtime::JobState::StaleDiscarded);
    EXPECT_EQ(jobs.Stats().StaleDiscardedJobs, 1u);
    EXPECT_EQ(jobs.Stats().PublishedCompletions, 0u);
    const Runtime::GeometryPresentationSlotStatus* status =
        FindStatus(
            scene,
            entity,
            Runtime::GeometryPresentationSlotSemantic::Normal);
    ASSERT_NE(status, nullptr);
    EXPECT_FALSE(status->GeneratedTexture.IsValid());
}
