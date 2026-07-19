#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeService;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;

#include "MockRHI.hpp"

namespace
{
    namespace Assets = Extrinsic::Assets;
    namespace ECS = Extrinsic::ECS;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;
    namespace Sources = ECS::Components::GeometrySources;
    namespace PropertyNames =
        ECS::Components::GeometrySources::PropertyNames;

    template <typename T, std::size_t N>
    [[nodiscard]] std::span<const std::byte> AsBytes(
        const std::array<T, N>& values) noexcept
    {
        return std::as_bytes(std::span<const T>{values});
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeIdentity
    MakeIdentity(const float xOffset = 0.0f)
    {
        const std::array<float, 9u> positions{
            xOffset + 0.0f, 0.0f, 0.0f,
            xOffset + 1.0f, 0.0f, 0.0f,
            xOffset + 0.0f, 1.0f, 0.0f,
        };
        const std::array<std::uint32_t, 3u> indices{0u, 1u, 2u};
        const std::array<float, 6u> texcoords{
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
        };
        const std::array<float, 9u> normals{
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
        };
        const auto built =
            Runtime::BuildRuntimeObjectSpaceNormalBakeIdentity(
                Runtime::RuntimeObjectSpaceNormalBakeIdentityInput{
                    .PackedPositionBytes = AsBytes(positions),
                    .SurfaceIndexBytes = AsBytes(indices),
                    .ResolvedTexcoordBytes = AsBytes(texcoords),
                    .ResolvedNormalBytes = AsBytes(normals),
                    .VertexCount = 3u,
                    .SurfaceIndexCount = 3u,
                    .Options =
                        Graphics::ObjectSpaceNormalTextureBakeOptions{
                            .Width = 64u,
                            .Height = 32u,
                            .PaddingTexels = 0u,
                        },
                });
        EXPECT_TRUE(built.Succeeded()) << built.Diagnostic;
        return *built.Identity;
    }

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeTarget MakeTarget(
        const ECS::EntityHandle entity,
        const std::uint64_t bindingEpoch = 3u)
    {
        return Runtime::RuntimeObjectSpaceNormalBakeTarget{
            .World = Runtime::WorldHandle{7u, 1u},
            .BindingEpoch = bindingEpoch,
            .Entity = entity,
            .StableEntityId =
                Runtime::StableEntityLookup::ToRenderId(entity),
            .Semantic = Runtime::ProgressiveSlotSemantic::Normal,
        };
    }

    struct DeterministicFixture
    {
        Extrinsic::Tests::MockDevice Device{};
        RHI::BufferManager Buffers{Device};
        RHI::TextureManager Textures{Device, Device.Bindless};
        RHI::SamplerManager Samplers{Device};
        Graphics::GpuAssetCache GpuAssets{
            Buffers,
            Textures,
            Samplers,
            Device.TransferQueue};
        Assets::AssetService Assets{};
        Runtime::RenderExtractionCache Extraction{};
        Runtime::ObjectSpaceNormalBakeService Service{};
        Runtime::JobService Jobs{};
        ECS::Scene::Registry Scene{};
        Runtime::GpuQueueParticipantHandle Participant{};
        bool IdleWaited{false};

        void Configure(
            Runtime::ObjectSpaceNormalBakeServiceTestHooks hooks =
                Runtime::ObjectSpaceNormalBakeServiceTestHooks{
                    .EnableDeterministicPlan = true,
                })
        {
            Runtime::SetObjectSpaceNormalBakeServiceTestHooks(
                Service,
                hooks);
            Service.SetDependencies(
                Runtime::ObjectSpaceNormalBakeServiceDependencies{
                    .Assets = &Assets,
                    .GpuAssets = &GpuAssets,
                    .RenderExtraction = &Extraction,
                    .Device = &Device,
                });
            Service.SetTargetScene(
                Runtime::WorldHandle{7u, 1u},
                3u,
                &Scene);
            Participant =
                Service.RegisterGpuQueueParticipant(Jobs);
            ASSERT_TRUE(Participant.IsValid());
        }

        void Shutdown()
        {
            if (Participant.IsValid())
            {
                Jobs.UnregisterGpuQueueParticipant(
                    Participant,
                    [this]
                    {
                        IdleWaited = true;
                        Device.WaitIdle();
                    });
                Participant = {};
            }
            Service.ClearDependencies();
        }

        ~DeterministicFixture()
        {
            Shutdown();
        }
    };

    [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeResult Schedule(
        DeterministicFixture& fixture,
        const ECS::EntityHandle entity,
        Runtime::RuntimeObjectSpaceNormalBakeIdentity identity =
            MakeIdentity())
    {
        return fixture.Service.Queue().Schedule(
            Runtime::RuntimeObjectSpaceNormalBakeRequest{
                .Identity = std::move(identity),
                .Target = MakeTarget(entity),
            },
            fixture.Device.IsOperational());
    }

    constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

    void SetPositions(
        Sources::Vertices& vertices,
        const std::vector<glm::vec3>& values)
    {
        vertices.Properties.Resize(values.size());
        auto property =
            vertices.Properties.GetOrAdd<glm::vec3>(
                std::string{PropertyNames::kPosition},
                glm::vec3{0.0f});
        property.Vector() = values;
    }

    void SetTexcoords(
        Sources::Vertices& vertices,
        const std::vector<glm::vec2>& values)
    {
        auto property =
            vertices.Properties.GetOrAdd<glm::vec2>(
                "v:texcoord",
                glm::vec2{0.0f});
        property.Vector() = values;
    }

    void SetNormals(
        Sources::Vertices& vertices,
        const std::vector<glm::vec3>& values)
    {
        auto property =
            vertices.Properties.GetOrAdd<glm::vec3>(
                "v:normal",
                glm::vec3{0.0f, 0.0f, 1.0f});
        property.Vector() = values;
    }

    void AttachTriangle(
        ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity,
        const float xOffset)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<Sources::Vertices>(entity);
        SetPositions(
            vertices,
            {
                {xOffset + 0.0f, 0.0f, 0.0f},
                {xOffset + 1.0f, 0.0f, 0.0f},
                {xOffset + 0.0f, 1.0f, 0.0f},
            });
        SetTexcoords(
            vertices,
            {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
                {0.0f, 1.0f},
            });
        SetNormals(
            vertices,
            {
                {0.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, 1.0f},
            });
        raw.emplace<Sources::Edges>(entity);
        auto& halfedges = raw.emplace<Sources::Halfedges>(entity);
        halfedges.Properties.Resize(6u);
        auto toVertex =
            halfedges.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kHalfedgeToVertex},
                kInvalidIndex);
        auto next =
            halfedges.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kHalfedgeNext},
                kInvalidIndex);
        auto face =
            halfedges.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kHalfedgeFace},
                kInvalidIndex);
        toVertex.Vector() = {1u, 2u, 0u, 0u, 2u, 1u};
        next.Vector() = {1u, 2u, 0u, 5u, 3u, 4u};
        face.Vector() = {
            0u,
            0u,
            0u,
            kInvalidIndex,
            kInvalidIndex,
            kInvalidIndex,
        };
        auto& faces = raw.emplace<Sources::Faces>(entity);
        faces.Properties.Resize(1u);
        auto faceHalfedge =
            faces.Properties.GetOrAdd<std::uint32_t>(
                std::string{PropertyNames::kFaceHalfedge},
                kInvalidIndex);
        faceHalfedge.Vector() = {0u};
    }

    [[nodiscard]] ECS::EntityHandle MakeRenderableTriangle(
        ECS::Scene::Registry& scene,
        const float xOffset)
    {
        const ECS::EntityHandle entity = scene.Create();
        scene.Raw()
            .emplace<ECS::Components::Transform::WorldMatrix>(
                entity)
            .Matrix = glm::mat4{1.0f};
        scene.Raw().emplace<
            Graphics::Components::RenderSurface>(entity);
        AttachTriangle(scene, entity, xOffset);
        return entity;
    }

    [[nodiscard]] Runtime::ProgressivePresentationBindings
    MakePendingNormalBindings()
    {
        Runtime::ProgressiveSlotBinding normal{};
        normal.Semantic = Runtime::ProgressiveSlotSemantic::Normal;
        normal.SourceKind =
            Runtime::ProgressiveSlotSourceKind::PropertyBake;
        normal.Property =
            Runtime::ProgressivePropertyBindingDescriptor{
                .Domain =
                    Runtime::ProgressiveGeometryDomain::MeshVertex,
                .PropertyName = "v:normal",
                .ExpectedValueKind =
                    Runtime::ProgressivePropertyValueKind::Vec3,
                .ExpectedElementCount = 3u,
            };
        normal.GeneratedPolicy =
            Runtime::ProgressiveGeneratedOutputPolicy::
                DeterministicChildAsset;
        normal.Provenance =
            Runtime::ProgressiveGeneratedOutputProvenance::
                PropertyBinding;
        normal.Readiness =
            Runtime::ProgressiveReadinessState::Pending;
        normal.LastDiagnostic = "original pending normal bake";

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
                    .Kind =
                        Runtime::ProgressivePresentationKind::
                            SurfaceMaterial,
                    .Slots = {std::move(normal)},
                },
            },
            .BindingGeneration = 7u,
        };
    }

    struct BindingFixture
    {
        static constexpr Runtime::WorldHandle World{9u, 1u};
        static constexpr std::uint64_t BindingEpoch = 5u;

        Extrinsic::Tests::MockDevice Device{};
        std::unique_ptr<Graphics::IRenderer> Renderer{
            Graphics::CreateRenderer()};
        std::unique_ptr<Graphics::GpuAssetCache> GpuAssets{};
        Runtime::RenderExtractionCache Extraction{};
        ECS::Scene::Registry Scene{};
        Runtime::RuntimeObjectSpaceNormalBakeQueue Queue{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        Runtime::RuntimeObjectSpaceNormalBakeIdentity Identity{};
        Runtime::RuntimeObjectSpaceNormalBakeStaleKey StaleKey{};
        std::uint64_t GeometryContentRevision{0u};
        Assets::AssetId GeneratedTexture{800u, 1u};
        Graphics::MaterialTextureAssetBindings OriginalMaterial{
            .Albedo = Assets::AssetId{801u, 1u},
            .Normal = Assets::AssetId{802u, 1u},
            .MetallicRoughness = Assets::AssetId{803u, 1u},
            .Emissive = Assets::AssetId{804u, 1u},
            .NormalSpace =
                Graphics::MaterialNormalTextureSpace::
                    TangentSpaceNormal,
        };
        bool HasProgressiveBinding{false};

        BindingFixture()
        {
            if (Renderer == nullptr)
                return;
            Renderer->Initialize(Device);
            GpuAssets =
                std::make_unique<Graphics::GpuAssetCache>(
                    Renderer->GetBufferManager(),
                    Renderer->GetTextureManager(),
                    Renderer->GetSamplerManager(),
                    Device.GetTransferQueue());
        }

        ~BindingFixture()
        {
            if (Renderer == nullptr)
                return;
            Extraction.Shutdown(*Renderer);
            GpuAssets.reset();
            Renderer->Shutdown();
        }

        [[nodiscard]] bool Initialize(
            const bool withProgressiveBinding)
        {
            if (Renderer == nullptr || GpuAssets == nullptr)
                return false;

            HasProgressiveBinding = withProgressiveBinding;
            Entity = MakeRenderableTriangle(Scene, 0.0f);
            StableEntityId =
                Runtime::StableEntityLookup::ToRenderId(Entity);
            if (withProgressiveBinding)
            {
                Scene.Raw()
                    .emplace<
                        Runtime::ProgressivePresentationBindings>(
                        Entity,
                        MakePendingNormalBindings());
            }

            const auto extracted =
                Extraction.ExtractAndSubmit(
                    Scene,
                    *Renderer,
                    GpuAssets.get());
            if (extracted.MeshGeometryFailedPack != 0u)
                return false;

            Extraction.SetMaterialTextureAssetBindings(
                StableEntityId,
                OriginalMaterial);

            Runtime::RuntimeObjectSpaceNormalBakeTarget target{
                .World = World,
                .BindingEpoch = BindingEpoch,
                .Entity = Entity,
                .StableEntityId = StableEntityId,
                .Semantic =
                    Runtime::ProgressiveSlotSemantic::Normal,
            };
            if (withProgressiveBinding)
            {
                target.PresentationKey = "mesh.surface";
                target.ExpectedProgressiveBindingGeneration = 7u;
            }

            const auto request =
                Runtime::BuildRuntimeObjectSpaceNormalBakeRequest(
                    Sources::BuildConstView(Scene.Raw(), Entity),
                    std::move(target),
                    Graphics::ObjectSpaceNormalTextureBakeOptions{
                        .Width = 64u,
                        .Height = 32u,
                        .PaddingTexels = 0u,
                    });
            if (!request.Succeeded() ||
                !request.Request->Identity.has_value())
            {
                return false;
            }
            Identity = *request.Request->Identity;

            const auto scheduled =
                Queue.Schedule(
                    *request.Request,
                    Device.IsOperational());
            if (!scheduled.Succeeded())
                return false;
            StaleKey = scheduled.Submission.StaleKey;

            const auto availability =
                Extraction.FindGpuRenderableAvailability(
                    StableEntityId);
            if (!availability.has_value() ||
                !availability->HasRenderable ||
                !availability->Surface.HasGeometry)
            {
                return false;
            }

            Graphics::GpuGeometryResidencyView residency{};
            if (!Renderer->GetGpuWorld()
                     .TryGetGeometryResidencyView(
                         availability->Surface.Geometry,
                         residency))
            {
                return false;
            }
            GeometryContentRevision = residency.ContentRevision;
            return GeometryContentRevision != 0u;
        }

        [[nodiscard]] std::optional<std::uint64_t>
        BeginGeneratedTexture(const bool ready)
        {
            if (GpuAssets == nullptr)
                return std::nullopt;
            auto pending =
                GpuAssets->BeginGpuProducedTexture(
                    Graphics::GpuProducedTextureRequest{
                        .Id = GeneratedTexture,
                        .Desc =
                            RHI::TextureDesc{
                                .Width = 64u,
                                .Height = 32u,
                                .MipLevels = 1u,
                                .Fmt = RHI::Format::RGBA16_FLOAT,
                                .Usage =
                                    RHI::TextureUsage::Sampled |
                                    RHI::TextureUsage::ColorTarget,
                                .DebugName =
                                    "runtime-normal-bake-binding-test",
                            },
                        .SamplerDesc =
                            RHI::SamplerDesc{
                                .DebugName =
                                    "runtime-normal-bake-binding-test",
                            },
                        .ReadyFrame = 0u,
                        .HasReadyFrame = ready,
                    });
            if (!pending.has_value())
                return std::nullopt;
            if (ready)
            {
                GpuAssets->Tick(
                    0u,
                    Device.GetFramesInFlight());
                if (GpuAssets->GetState(GeneratedTexture) !=
                    Graphics::GpuAssetState::Ready)
                {
                    return std::nullopt;
                }
            }
            return pending->Generation;
        }

        [[nodiscard]]
        Runtime::RuntimeObjectSpaceNormalBakeBindingContext
        Context() noexcept
        {
            return Runtime::RuntimeObjectSpaceNormalBakeBindingContext{
                .Queue = &Queue,
                .Extraction = &Extraction,
                .GpuAssets = GpuAssets.get(),
                .GpuWorld = &Renderer->GetGpuWorld(),
                .Scene = &Scene,
                .World = World,
                .BindingEpoch = BindingEpoch,
            };
        }

        [[nodiscard]] Runtime::RuntimeObjectSpaceNormalBakeCompletion
        Completion(
            const std::uint64_t cacheGeneration,
            const std::uint64_t geometryContentRevision = 0u) const
            noexcept
        {
            return Runtime::RuntimeObjectSpaceNormalBakeCompletion{
                .StaleKey = StaleKey,
                .Identity = &Identity,
                .GeneratedTextureAsset = GeneratedTexture,
                .CacheGeneration = cacheGeneration,
                .GeometryContentRevision =
                    geometryContentRevision == 0u
                        ? GeometryContentRevision
                        : geometryContentRevision,
                .AssetSelection =
                    Runtime::RuntimeObjectSpaceNormalBakeAssetSelection::
                        IdentityInserted,
            };
        }
    };

    void ExpectBindingStateUnchanged(
        const BindingFixture& fixture,
        const std::uint64_t expectedProgressiveGeneration = 7u)
    {
        EXPECT_TRUE(fixture.Queue.IsLatest(fixture.StaleKey));
        EXPECT_EQ(fixture.Queue.PendingCount(), 1u);

        const auto material =
            fixture.Extraction.GetMaterialTextureAssetBindings(
                fixture.StableEntityId);
        EXPECT_TRUE(material.has_value());
        if (material.has_value())
        {
            EXPECT_EQ(
                material->Albedo,
                fixture.OriginalMaterial.Albedo);
            EXPECT_EQ(
                material->Normal,
                fixture.OriginalMaterial.Normal);
            EXPECT_EQ(
                material->MetallicRoughness,
                fixture.OriginalMaterial.MetallicRoughness);
            EXPECT_EQ(
                material->Emissive,
                fixture.OriginalMaterial.Emissive);
            EXPECT_EQ(
                material->NormalSpace,
                fixture.OriginalMaterial.NormalSpace);
        }

        if (!fixture.HasProgressiveBinding ||
            !fixture.Scene.IsValid(fixture.Entity))
        {
            return;
        }
        const auto* bindings =
            fixture.Scene.Raw()
                .try_get<
                    Runtime::ProgressivePresentationBindings>(
                    fixture.Entity);
        EXPECT_NE(bindings, nullptr);
        if (bindings == nullptr)
            return;
        EXPECT_EQ(
            bindings->BindingGeneration,
            expectedProgressiveGeneration);
        const auto* presentation =
            Runtime::FindPresentationBinding(
                *bindings,
                "mesh.surface");
        EXPECT_NE(presentation, nullptr);
        if (presentation == nullptr)
            return;
        const auto* normal =
            Runtime::FindSlotBinding(
                *presentation,
                Runtime::ProgressiveSlotSemantic::Normal);
        EXPECT_NE(normal, nullptr);
        if (normal == nullptr)
            return;
        EXPECT_EQ(
            normal->SourceKind,
            Runtime::ProgressiveSlotSourceKind::PropertyBake);
        EXPECT_EQ(
            normal->Provenance,
            Runtime::ProgressiveGeneratedOutputProvenance::
                PropertyBinding);
        EXPECT_EQ(
            normal->Readiness,
            Runtime::ProgressiveReadinessState::Pending);
        EXPECT_FALSE(normal->GeneratedTexture.IsValid());
        EXPECT_EQ(
            normal->LastDiagnostic,
            "original pending normal bake");
    }
}

TEST(ObjectSpaceNormalBakeService,
     PendingExactTextureLeavesQueueMaterialAndProgressiveStateUntouched)
{
    BindingFixture fixture;
    ASSERT_TRUE(fixture.Initialize(
        /*withProgressiveBinding=*/true));
    const auto cacheGeneration =
        fixture.BeginGeneratedTexture(/*ready=*/false);
    ASSERT_TRUE(cacheGeneration.has_value());
    ASSERT_EQ(
        fixture.GpuAssets->GetState(fixture.GeneratedTexture),
        Graphics::GpuAssetState::GpuUploading);

    const auto bound =
        Runtime::TryBindReadyObjectSpaceNormalBake(
            fixture.Context(),
            fixture.Completion(*cacheGeneration));

    EXPECT_EQ(
        bound.Status,
        Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
            WaitingForGpuTexture);
    EXPECT_FALSE(bound.Succeeded());
    ExpectBindingStateUnchanged(fixture);
}

TEST(ObjectSpaceNormalBakeService,
     ExactReadyProgressiveCompletionCommitsMaterialAndSlotTogether)
{
    BindingFixture fixture;
    ASSERT_TRUE(fixture.Initialize(
        /*withProgressiveBinding=*/true));
    const auto cacheGeneration =
        fixture.BeginGeneratedTexture(/*ready=*/true);
    ASSERT_TRUE(cacheGeneration.has_value());

    const auto bound =
        Runtime::TryBindReadyObjectSpaceNormalBake(
            fixture.Context(),
            fixture.Completion(*cacheGeneration));

    ASSERT_TRUE(bound.Succeeded()) << bound.Diagnostic;
    EXPECT_EQ(
        bound.Status,
        Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::Bound);
    EXPECT_EQ(
        bound.BoundNormalTexture,
        fixture.GeneratedTexture);
    EXPECT_FALSE(fixture.Queue.IsLatest(fixture.StaleKey));
    EXPECT_EQ(fixture.Queue.PendingCount(), 0u);

    const auto material =
        fixture.Extraction.GetMaterialTextureAssetBindings(
            fixture.StableEntityId);
    ASSERT_TRUE(material.has_value());
    EXPECT_EQ(
        material->Albedo,
        fixture.OriginalMaterial.Albedo);
    EXPECT_EQ(material->Normal, fixture.GeneratedTexture);
    EXPECT_EQ(
        material->MetallicRoughness,
        fixture.OriginalMaterial.MetallicRoughness);
    EXPECT_EQ(
        material->Emissive,
        fixture.OriginalMaterial.Emissive);
    EXPECT_EQ(
        material->NormalSpace,
        Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal);

    const auto* progressive =
        fixture.Scene.Raw()
            .try_get<Runtime::ProgressivePresentationBindings>(
                fixture.Entity);
    ASSERT_NE(progressive, nullptr);
    EXPECT_EQ(progressive->BindingGeneration, 8u);
    const auto* presentation =
        Runtime::FindPresentationBinding(
            *progressive,
            "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const auto* normal =
        Runtime::FindSlotBinding(
            *presentation,
            Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(
        normal->SourceKind,
        Runtime::ProgressiveSlotSourceKind::
            GeneratedTextureAsset);
    EXPECT_EQ(
        normal->GeneratedTexture,
        fixture.GeneratedTexture);
    EXPECT_EQ(
        normal->Provenance,
        Runtime::ProgressiveGeneratedOutputProvenance::
            GeneratedTextureAsset);
    EXPECT_EQ(
        normal->Readiness,
        Runtime::ProgressiveReadinessState::Ready);
}

TEST(ObjectSpaceNormalBakeService,
     ReplacedSceneAndDestroyedEntityRejectWithoutConsumingCompletion)
{
    {
        BindingFixture fixture;
        ASSERT_TRUE(fixture.Initialize(
            /*withProgressiveBinding=*/true));
        const auto cacheGeneration =
            fixture.BeginGeneratedTexture(/*ready=*/true);
        ASSERT_TRUE(cacheGeneration.has_value());
        auto staleContext = fixture.Context();
        ++staleContext.BindingEpoch;

        const auto bound =
            Runtime::TryBindReadyObjectSpaceNormalBake(
                staleContext,
                fixture.Completion(*cacheGeneration));

        EXPECT_EQ(
            bound.Status,
            Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
                StaleScene);
        EXPECT_FALSE(bound.Succeeded());
        ExpectBindingStateUnchanged(fixture);
    }

    {
        BindingFixture fixture;
        ASSERT_TRUE(fixture.Initialize(
            /*withProgressiveBinding=*/false));
        const auto cacheGeneration =
            fixture.BeginGeneratedTexture(/*ready=*/true);
        ASSERT_TRUE(cacheGeneration.has_value());
        fixture.Scene.Destroy(fixture.Entity);

        const auto bound =
            Runtime::TryBindReadyObjectSpaceNormalBake(
                fixture.Context(),
                fixture.Completion(*cacheGeneration));

        EXPECT_EQ(
            bound.Status,
            Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
                InvalidStableEntity);
        EXPECT_FALSE(bound.Succeeded());
        ExpectBindingStateUnchanged(fixture);
    }
}

TEST(ObjectSpaceNormalBakeService,
     ChangedProgressiveGenerationRejectsWithoutPartialMaterialOrSlotMutation)
{
    BindingFixture fixture;
    ASSERT_TRUE(fixture.Initialize(
        /*withProgressiveBinding=*/true));
    const auto cacheGeneration =
        fixture.BeginGeneratedTexture(/*ready=*/true);
    ASSERT_TRUE(cacheGeneration.has_value());
    auto* progressive =
        fixture.Scene.Raw()
            .try_get<Runtime::ProgressivePresentationBindings>(
                fixture.Entity);
    ASSERT_NE(progressive, nullptr);
    progressive->BindingGeneration = 8u;

    const auto bound =
        Runtime::TryBindReadyObjectSpaceNormalBake(
            fixture.Context(),
            fixture.Completion(*cacheGeneration));

    EXPECT_EQ(
        bound.Status,
        Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
            StaleProgressiveState);
    EXPECT_FALSE(bound.Succeeded());
    ExpectBindingStateUnchanged(
        fixture,
        /*expectedProgressiveGeneration=*/8u);
}

TEST(ObjectSpaceNormalBakeService,
     ExactCacheAndGeometryGenerationsRejectStaleProvenanceTransactionally)
{
    BindingFixture fixture;
    ASSERT_TRUE(fixture.Initialize(
        /*withProgressiveBinding=*/true));
    const auto cacheGeneration =
        fixture.BeginGeneratedTexture(/*ready=*/true);
    ASSERT_TRUE(cacheGeneration.has_value());

    const auto staleCache =
        Runtime::TryBindReadyObjectSpaceNormalBake(
            fixture.Context(),
            fixture.Completion(*cacheGeneration + 1u));
    EXPECT_EQ(
        staleCache.Status,
        Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
            StaleCompletion);
    EXPECT_FALSE(staleCache.Succeeded());
    ExpectBindingStateUnchanged(fixture);

    const auto staleGeometry =
        Runtime::TryBindReadyObjectSpaceNormalBake(
            fixture.Context(),
            fixture.Completion(
                *cacheGeneration,
                fixture.GeometryContentRevision + 1u));
    EXPECT_EQ(
        staleGeometry.Status,
        Runtime::RuntimeObjectSpaceNormalBakeBindingStatus::
            StaleGeometry);
    EXPECT_FALSE(staleGeometry.Succeeded());
    ExpectBindingStateUnchanged(fixture);
}

TEST(ObjectSpaceNormalBakeService,
     AllocatesRegistryMetadataOnlyDuringMainThreadPrepare)
{
    DeterministicFixture fixture;
    fixture.Configure();
    const ECS::EntityHandle entity = fixture.Scene.Create();

    const auto scheduled = Schedule(fixture, entity);
    ASSERT_TRUE(scheduled.Succeeded());
    EXPECT_FALSE(
        scheduled.Submission.GeneratedTextureAsset.IsValid());
    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 0u);

    fixture.Service.PrepareScheduledRequests();

    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 1u);
    EXPECT_EQ(
        Runtime::GetObjectSpaceNormalBakeServiceTestDiagnostics(
            fixture.Service)
            .AllocatedAssets,
        1u);
}

TEST(ObjectSpaceNormalBakeService,
     ExactPendingIdentitySharesOneAssetAndOneGpuRecord)
{
    DeterministicFixture fixture;
    fixture.Configure();
    const ECS::EntityHandle first = fixture.Scene.Create();
    const ECS::EntityHandle second = fixture.Scene.Create();

    ASSERT_TRUE(Schedule(fixture, first).Succeeded());
    ASSERT_TRUE(Schedule(fixture, second).Succeeded());
    fixture.Service.PrepareScheduledRequests();

    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 1u);
    EXPECT_EQ(
        Runtime::GetObjectSpaceNormalBakeServiceTestDiagnostics(
            fixture.Service)
            .PendingIdentityReuses,
        1u);

    Extrinsic::Tests::MockCommandContext commands;
    fixture.Jobs.RecordGpuQueueFrameCommands(commands);

    EXPECT_EQ(commands.DrawIndexedCalls, 1);
    EXPECT_EQ(
        Runtime::GetObjectSpaceNormalBakeServiceTestDiagnostics(
            fixture.Service)
            .RecordedSubmissions,
        1u);
}

TEST(ObjectSpaceNormalBakeService,
     DifferentExactIdentitiesAllocateDistinctStrongHandles)
{
    DeterministicFixture fixture;
    fixture.Configure();
    const ECS::EntityHandle first = fixture.Scene.Create();
    const ECS::EntityHandle second = fixture.Scene.Create();

    ASSERT_TRUE(Schedule(fixture, first, MakeIdentity(0.0f)).Succeeded());
    ASSERT_TRUE(Schedule(fixture, second, MakeIdentity(4.0f)).Succeeded());
    fixture.Service.PrepareScheduledRequests();

    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 2u);
}

TEST(ObjectSpaceNormalBakeService,
     DigestCollisionProbesAndStillKeepsExactIdentitiesDistinct)
{
    DeterministicFixture fixture;
    fixture.Configure(
        Runtime::ObjectSpaceNormalBakeServiceTestHooks{
            .EnableDeterministicPlan = true,
            .IdentityDigestOverride = 0x1234u,
        });
    const ECS::EntityHandle first = fixture.Scene.Create();
    const ECS::EntityHandle second = fixture.Scene.Create();

    ASSERT_TRUE(Schedule(fixture, first, MakeIdentity(0.0f)).Succeeded());
    ASSERT_TRUE(Schedule(fixture, second, MakeIdentity(8.0f)).Succeeded());
    fixture.Service.PrepareScheduledRequests();

    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 2u);
    EXPECT_EQ(
        Runtime::GetObjectSpaceNormalBakeServiceTestDiagnostics(
            fixture.Service)
            .AllocatedAssets,
        2u);
}

TEST(ObjectSpaceNormalBakeService,
     RecordFailureDestroysMetadataAndDoesNotSilentlyRetry)
{
    DeterministicFixture fixture;
    fixture.Configure(
        Runtime::ObjectSpaceNormalBakeServiceTestHooks{
            .EnableDeterministicPlan = true,
            .InvalidateFirstRecord = true,
        });
    const ECS::EntityHandle entity = fixture.Scene.Create();
    ASSERT_TRUE(Schedule(fixture, entity).Succeeded());
    fixture.Service.PrepareScheduledRequests();
    ASSERT_EQ(fixture.Assets.LiveAssetCount(), 1u);

    Extrinsic::Tests::MockCommandContext commands;
    fixture.Jobs.RecordGpuQueueFrameCommands(commands);

    EXPECT_EQ(
        Runtime::GetObjectSpaceNormalBakeServiceTestDiagnostics(
            fixture.Service)
            .RecordFailures,
        1u);
    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 0u);
    EXPECT_EQ(fixture.Service.Queue().PendingCount(), 0u);
}

TEST(ObjectSpaceNormalBakeService,
     PostRecordReadyFailureRetainsResourcesUntilSafeFrame)
{
    DeterministicFixture fixture;
    fixture.Configure(
        Runtime::ObjectSpaceNormalBakeServiceTestHooks{
            .EnableDeterministicPlan = true,
            .RejectFirstReadyPublication = true,
        });
    const ECS::EntityHandle entity = fixture.Scene.Create();
    ASSERT_TRUE(Schedule(fixture, entity).Succeeded());
    fixture.Service.PrepareScheduledRequests();

    Extrinsic::Tests::MockCommandContext commands;
    fixture.Jobs.RecordGpuQueueFrameCommands(commands);
    ASSERT_EQ(commands.DrawIndexedCalls, 1);
    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 1u);
    EXPECT_EQ(
        Runtime::GetObjectSpaceNormalBakeServiceTestDiagnostics(
            fixture.Service)
            .ReadyFrameFailures,
        1u);

    fixture.Device.GlobalFrameNumber =
        Runtime::ObjectSpaceNormalBakeReadyFrame(
            0u,
            fixture.Device.GetFramesInFlight());
    (void)fixture.Jobs.DrainGpuQueueCompletedTransfers();

    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 0u);
}

TEST(ObjectSpaceNormalBakeService,
     SceneDetachDropsQueuedTargetWithoutCancellingShutdownContract)
{
    DeterministicFixture fixture;
    fixture.Configure();
    const ECS::EntityHandle entity = fixture.Scene.Create();
    ASSERT_TRUE(Schedule(fixture, entity).Succeeded());
    fixture.Service.PrepareScheduledRequests();
    ASSERT_EQ(fixture.Assets.LiveAssetCount(), 1u);

    fixture.Service.DetachTargets(
        Runtime::WorldHandle{7u, 1u},
        3u);

    EXPECT_EQ(fixture.Service.Queue().PendingCount(), 0u);
    EXPECT_EQ(fixture.Assets.LiveAssetCount(), 0u);
}

TEST(ObjectSpaceNormalBakeService,
     ProductionProviderUsesLiveSharedIndexSliceAndMergesNormalBinding)
{
    Extrinsic::Tests::MockDevice device;
    std::unique_ptr<Graphics::IRenderer> renderer =
        Graphics::CreateRenderer();
    renderer->Initialize(device);
    auto cache =
        std::make_unique<Graphics::GpuAssetCache>(
            renderer->GetBufferManager(),
            renderer->GetTextureManager(),
            renderer->GetSamplerManager(),
            device.GetTransferQueue());
    Assets::AssetService assets;
    Runtime::RenderExtractionCache extraction;
    ECS::Scene::Registry scene;
    const ECS::EntityHandle decoy =
        MakeRenderableTriangle(scene, -4.0f);
    (void)decoy;

    const auto decoyExtracted =
        extraction.ExtractAndSubmit(
            scene,
            *renderer,
            cache.get());
    ASSERT_EQ(decoyExtracted.MeshGeometryFailedPack, 0u);
    const ECS::EntityHandle target =
        MakeRenderableTriangle(scene, 0.0f);
    const auto extracted =
        extraction.ExtractAndSubmit(
            scene,
            *renderer,
            cache.get());
    ASSERT_EQ(extracted.MeshGeometryFailedPack, 0u);

    const std::uint32_t stableId =
        Runtime::StableEntityLookup::ToRenderId(target);
    const auto availability =
        extraction.FindGpuRenderableAvailability(stableId);
    ASSERT_TRUE(availability.has_value());
    ASSERT_TRUE(availability->Surface.HasGeometry);
    Graphics::GpuGeometryResidencyView residency{};
    ASSERT_TRUE(
        renderer->GetGpuWorld().TryGetGeometryResidencyView(
            availability->Surface.Geometry,
            residency));
    ASSERT_GT(residency.Record.SurfaceFirstIndex, 0u);

    const Assets::AssetId albedo{40u, 1u};
    const Assets::AssetId metallicRoughness{41u, 1u};
    const Assets::AssetId emissive{42u, 1u};
    extraction.SetMaterialTextureAssetBindings(
        stableId,
        Graphics::MaterialTextureAssetBindings{
            .Albedo = albedo,
            .MetallicRoughness = metallicRoughness,
            .Emissive = emissive,
        });

    Runtime::ObjectSpaceNormalBakeService service;
    Runtime::JobService jobs;
    service.SetDependencies(
        Runtime::ObjectSpaceNormalBakeServiceDependencies{
            .Assets = &assets,
            .GpuAssets = cache.get(),
            .Renderer = renderer.get(),
            .RenderExtraction = &extraction,
            .Device = &device,
        });
    service.SetTargetScene(
        Runtime::WorldHandle{9u, 1u},
        5u,
        &scene);
    const Runtime::GpuQueueParticipantHandle participant =
        service.RegisterGpuQueueParticipant(jobs);
    ASSERT_TRUE(participant.IsValid());

    const auto request =
        Runtime::BuildRuntimeObjectSpaceNormalBakeRequest(
            Sources::BuildConstView(scene.Raw(), target),
            Runtime::RuntimeObjectSpaceNormalBakeTarget{
                .World = Runtime::WorldHandle{9u, 1u},
                .BindingEpoch = 5u,
                .Entity = target,
                .StableEntityId = stableId,
                .Semantic =
                    Runtime::ProgressiveSlotSemantic::Normal,
            },
            Graphics::ObjectSpaceNormalTextureBakeOptions{
                .Width = 64u,
                .Height = 64u,
                .PaddingTexels = 0u,
            });
    ASSERT_TRUE(request.Succeeded()) << request.Diagnostic;
    ASSERT_TRUE(
        service.Queue()
            .Schedule(
                *request.Request,
                device.IsOperational())
            .Succeeded());
    service.PrepareScheduledRequests();

    Extrinsic::Tests::MockCommandContext commands;
    jobs.RecordGpuQueueFrameCommands(commands);

    ASSERT_EQ(commands.DrawIndexedCalls, 1);
    EXPECT_EQ(
        commands.LastDrawIndexed.FirstIndex,
        residency.Record.SurfaceFirstIndex);
    EXPECT_EQ(commands.LastDrawIndexed.VertexOffset, 0);
    EXPECT_FALSE(commands.BufferBarrierCalls.empty());

    const std::uint64_t readyFrame =
        Runtime::ObjectSpaceNormalBakeReadyFrame(
            device.GetGlobalFrameNumber(),
            device.GetFramesInFlight());
    cache->Tick(readyFrame, device.GetFramesInFlight());
    (void)jobs.DrainGpuQueueCompletedTransfers();

    const auto bindings =
        extraction.GetMaterialTextureAssetBindings(stableId);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_TRUE(bindings->Normal.IsValid());
    EXPECT_EQ(bindings->Albedo, albedo);
    EXPECT_EQ(
        bindings->MetallicRoughness,
        metallicRoughness);
    EXPECT_EQ(bindings->Emissive, emissive);
    EXPECT_EQ(
        bindings->NormalSpace,
        Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal);
    EXPECT_EQ(
        cache->GetState(bindings->Normal),
        Graphics::GpuAssetState::Ready);
    const auto readyView = cache->GetView(bindings->Normal);
    ASSERT_TRUE(readyView.has_value());
    EXPECT_NE(readyView->Generation, 0u);

    bool waitedForIdle = false;
    jobs.UnregisterGpuQueueParticipant(
        participant,
        [&]
        {
            waitedForIdle = true;
            device.WaitIdle();
        });
    EXPECT_TRUE(waitedForIdle);
    service.ClearDependencies();
    EXPECT_EQ(assets.LiveAssetCount(), 0u);
    extraction.Shutdown(*renderer);
    cache.reset();
    renderer->Shutdown();
}
