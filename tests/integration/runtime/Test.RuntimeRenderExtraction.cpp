#include <cstdint>
#include <array>
#include <cstddef>
#include <memory>
#include <span>

#include <entt/entity/entity.hpp>
#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.Light;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.EcsSystemBundle;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxEditorUi;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialDebugAdapters;
import Extrinsic.Runtime.VisualizationAdapters;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.AABB;
import Geometry.BVH;
import Geometry.ConvexHull;
import Geometry.Plane;
import Geometry.Properties;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    class ImmediateTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      const void*,
                                                      std::uint64_t size,
                                                      std::uint64_t = 0) override
        {
            return RHI::TransferToken{++m_NextToken + size};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      std::span<const std::byte> src,
                                                      std::uint64_t = 0) override
        {
            return RHI::TransferToken{++m_NextToken + static_cast<std::uint64_t>(src.size())};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                       const void*,
                                                       std::uint64_t size,
                                                       std::uint32_t = 0,
                                                       std::uint32_t = 0) override
        {
            return RHI::TransferToken{++m_NextToken + size};
        }

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                std::span<const std::byte> bytes) override
        {
            return RHI::TransferToken{++m_NextToken + static_cast<std::uint64_t>(bytes.size())};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken) const override { return true; }
        void CollectCompleted() override {}

    private:
        std::uint64_t m_NextToken = 0;
    };

    struct RendererFixture
    {
        Tests::MockDevice Device{};
        std::unique_ptr<Graphics::IRenderer> Renderer{Graphics::CreateRenderer()};
        Runtime::RenderExtractionCache Extraction{};

        RendererFixture()
        {
            Renderer->Initialize(Device);
        }

        ~RendererFixture()
        {
            Extraction.Shutdown(*Renderer);
            Renderer->Shutdown();
        }

        Runtime::RuntimeRenderExtractionStats Extract(ECS::Scene::Registry& scene,
                                                      Graphics::GpuAssetCache* gpuAssets = nullptr)
        {
            RHI::FrameHandle frame{};
            EXPECT_TRUE(Renderer->BeginFrame(frame));
            return Extraction.ExtractAndSubmit(scene, *Renderer, gpuAssets);
        }
    };

    Assets::AssetId MakeAssetId(const std::uint32_t index)
    {
        return Assets::AssetId{index, 1u};
    }

    RHI::BufferDesc TestBufferDesc()
    {
        return RHI::BufferDesc{
            .SizeBytes = 64,
            .Usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::TransferDst,
            .DebugName = "runtime-render-extraction-asset",
        };
    }

    std::array<std::byte, 64> ZeroBytes64{};

    [[nodiscard]] std::uint32_t StableId(entt::entity entity) noexcept
    {
        return Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    }

    [[nodiscard]] Geometry::PropertySet MakeScalarProperties()
    {
        Geometry::PropertySet properties;
        properties.Resize(3u);

        auto curvature = properties.Add<float>("curvature", 0.0f);
        curvature[0] = -0.5f;
        curvature[1] = 0.25f;
        curvature[2] = 1.5f;

        return properties;
    }

    [[nodiscard]] Geometry::PropertySet MakeColorProperties()
    {
        Geometry::PropertySet properties;
        properties.Resize(3u);

        auto colors = properties.Add<glm::vec4>("v:kmeans_color", glm::vec4{1.f});
        colors[0] = {1.f, 0.f, 0.f, 1.f};
        colors[1] = {0.f, 1.f, 0.f, 1.f};
        colors[2] = {0.f, 0.f, 1.f, 1.f};

        return properties;
    }

    [[nodiscard]] Geometry::PropertySet MakeVectorProperties()
    {
        Geometry::PropertySet properties;
        properties.Resize(3u);

        auto vectors = properties.Add<glm::vec3>("velocity", glm::vec3{0.f});
        vectors[0] = {1.f, 0.f, 0.f};
        vectors[1] = {0.f, 1.f, 0.f};
        vectors[2] = {0.f, 0.f, 1.f};

        return properties;
    }

    void ConfigureScalarVisualization(ECS::Scene::Registry& scene,
                                      entt::entity entity)
    {
        auto& registry = scene.Raw();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderPoints>(entity);

        auto& visualization = registry.emplace<Graphics::Components::VisualizationConfig>(entity);
        visualization.Source = Graphics::Components::VisualizationConfig::ColorSource::ScalarField;
        visualization.ScalarFieldName = "curvature";
        visualization.ScalarDomain = Graphics::Components::VisualizationConfig::Domain::Vertex;
        visualization.Scalar.Map = Graphics::Colormap::Type::Plasma;
    }

    void ConfigureColorBufferVisualization(ECS::Scene::Registry& scene,
                                           entt::entity entity)
    {
        auto& registry = scene.Raw();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderPoints>(entity);

        auto& visualization = registry.emplace<Graphics::Components::VisualizationConfig>(entity);
        visualization.Source = Graphics::Components::VisualizationConfig::ColorSource::PerVertexBuffer;
        visualization.ColorBufferName = "v:kmeans_color";
    }

    void ConfigureRenderablePoint(ECS::Scene::Registry& scene,
                                  entt::entity entity)
    {
        auto& registry = scene.Raw();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderPoints>(entity);
    }
}

TEST(RuntimeRenderExtraction, CreatesUpdatesAndClearsDirtyTransformSidecar)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    auto& world = registry.emplace<ECS::Components::Transform::WorldMatrix>(entity);
    world.Matrix = glm::mat4{1.f};
    world.Matrix[3] = glm::vec4{2.f, 3.f, 4.f, 1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::DirtyTags::DirtyTransform>(entity);

    auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);
    EXPECT_EQ(stats.SubmittedTransformCount, 1u);
    EXPECT_EQ(stats.SubmittedVisualizationCount, 1u);
    EXPECT_EQ(stats.DirtyTransformCount, 1u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);
    EXPECT_FALSE(registry.any_of<ECS::Components::DirtyTags::DirtyTransform>(entity));

    world.Matrix[3] = glm::vec4{5.f, 6.f, 7.f, 1.f};

    stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 0u);
    EXPECT_EQ(stats.DirtyTransformCount, 0u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
}

// BUG-024 — a Sandbox Editor transform edit applied after the fixed-step ECS
// bundle reaches the render-world model only after the runtime pre-render
// flush. Asserts the actual snapshot translation (not just submission
// counts): stale before the flush, edited afterwards, with DirtyTransform
// stamped by the flush and drained by extraction.
TEST(RuntimeRenderExtraction, UiTransformEditModelReachesRenderWorldAfterPreRenderFlush)
{
    namespace ECSC = ECS::Components;

    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = ECS::Scene::CreateDefault(scene, "EditedTriangle");
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.get<ECSC::Transform::Component>(entity).Position = glm::vec3{1.f, 2.f, 3.f};
    registry.emplace_or_replace<ECSC::Transform::IsDirtyTag>(entity);

    // Scheduled fixed-step bundle settles the scene (engine Phase 2 shape).
    Core::FrameGraph graph;
    (void)Runtime::RegisterPromotedEcsSystemBundle(graph, scene);
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    auto stats = fixture.Extract(scene);
    ASSERT_EQ(stats.AllocatedInstanceCount, 1u);
    ASSERT_EQ(stats.DirtyTransformCount, 1u);

    const auto modelTranslation = [&]() -> glm::vec3 {
        const Graphics::RenderWorld world =
            fixture.Renderer->ExtractRenderWorld(Graphics::RenderFrameInput{});
        for (const auto& renderable : world.Renderables)
        {
            if (renderable.StableId == Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity))
                return glm::vec3{renderable.Model[3]};
        }
        ADD_FAILURE() << "renderable not found in render world";
        return glm::vec3{0.f};
    };
    EXPECT_EQ(modelTranslation(), (glm::vec3{1.f, 2.f, 3.f}));

    // Post-fixed-step editor command (the Inspector "Local position" edit).
    Runtime::SelectionController selection;
    const Runtime::SandboxEditorContext context{
        .Scene = &scene,
        .Selection = &selection,
    };
    const auto status = Runtime::ApplySandboxEditorTransformEdit(
        context,
        Runtime::SandboxEditorTransformEditCommand{
            .StableEntityId = Runtime::SelectionController::ToStableEntityId(entity),
            .SetPosition = true,
            .Position = glm::vec3{7.f, 8.f, 9.f},
        });
    ASSERT_EQ(status, Runtime::SandboxEditorCommandStatus::Applied);

    // Without the pre-render flush the snapshot stays at the stale
    // translation — the BUG-024 failure mode.
    stats = fixture.Extract(scene);
    EXPECT_EQ(stats.DirtyTransformCount, 0u);
    EXPECT_EQ(modelTranslation(), (glm::vec3{1.f, 2.f, 3.f}));

    // The runtime-owned flush (Engine::RunFrame order: after UI/gizmo
    // mutations, before extraction) propagates the edit into the snapshot.
    const Runtime::PreRenderTransformFlushStats flush =
        Runtime::FlushPreRenderTransformState(scene);
    EXPECT_EQ(flush.DirtyTransformStamped, 1u);

    stats = fixture.Extract(scene);
    EXPECT_EQ(stats.DirtyTransformCount, 1u);
    EXPECT_EQ(stats.SubmittedTransformCount, 1u);
    EXPECT_EQ(modelTranslation(), (glm::vec3{7.f, 8.f, 9.f}));
    EXPECT_FALSE(registry.any_of<ECSC::Transform::IsDirtyTag>(entity));
    EXPECT_FALSE(registry.any_of<ECSC::DirtyTags::DirtyTransform>(entity));
}

TEST(RuntimeRenderExtraction, ClearSceneStateDropsSidecarsAndPerEntityBindings)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    Geometry::PropertySet properties = MakeScalarProperties();
    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);
    const std::uint32_t stableId = StableId(entity);
    constexpr std::uint64_t kAdapterKey = 0x5CE11u;

    fixture.Extraction.RegisterVisualizationAdapter(
        kAdapterKey,
        std::make_unique<Runtime::PropertyScalarAdapter>(
            Geometry::ConstPropertySet{properties}));
    fixture.Extraction.SetVisualizationAdapterBinding(
        stableId,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kAdapterKey,
            .BufferBDA = 0xCAFE1000u,
        });
    fixture.Extraction.SetMeshPrimitiveViewSettings(
        stableId,
        Runtime::MeshPrimitiveViewSettings{.EnableEdgeView = true});

    const auto stats = fixture.Extract(scene);
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);
    EXPECT_EQ(fixture.Extraction.GetVisualizationAdapterCount(), 1u);
    ASSERT_TRUE(fixture.Extraction.GetVisualizationAdapterBinding(stableId).has_value());
    EXPECT_TRUE(fixture.Extraction.GetMeshPrimitiveViewSettings(stableId).AnyEnabled());

    fixture.Extraction.ClearSceneState(*fixture.Renderer);
    const Graphics::RenderWorld world =
        fixture.Renderer->ExtractRenderWorld(Graphics::RenderFrameInput{});

    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 0u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 0u);
    EXPECT_EQ(fixture.Extraction.GetVisualizationAdapterCount(), 1u);
    EXPECT_FALSE(fixture.Extraction.GetVisualizationAdapterBinding(stableId).has_value());
    EXPECT_FALSE(fixture.Extraction.GetMeshPrimitiveViewSettings(stableId).AnyEnabled());
    EXPECT_TRUE(world.Visualization.Scalars.empty());
    EXPECT_FALSE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, RetiresDestroyedRenderableSidecar)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderLines>(entity);

    auto stats = fixture.Extract(scene);
    ASSERT_EQ(stats.AllocatedInstanceCount, 1u);
    ASSERT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);

    scene.Destroy(entity);
    stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 0u);
    EXPECT_EQ(stats.SubmittedTransformCount, 0u);
    EXPECT_EQ(stats.FreedInstanceCount, 1u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 0u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 0u);
}

TEST(RuntimeRenderExtraction, ExtractsVisualizationAndLightsWithoutRenderableOwnership)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto visualized = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(visualized).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderPoints>(visualized);
    registry.emplace<Graphics::Components::VisualizationConfig>(visualized).Source =
        Graphics::Components::VisualizationConfig::ColorSource::UniformColor;

    const auto light = scene.Create();
    auto& lightWorld = registry.emplace<ECS::Components::Transform::WorldMatrix>(light);
    lightWorld.Matrix = glm::mat4{1.f};
    lightWorld.Matrix[3] = glm::vec4{1.f, 2.f, 3.f, 1.f};
    auto& point = registry.emplace<ECS::Components::Lights::PointLight>(light);
    point.Color = {0.25f, 0.5f, 1.f};
    point.Intensity = 3.f;

    const auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.SubmittedTransformCount, 1u);
    EXPECT_EQ(stats.SubmittedVisualizationCount, 1u);
    EXPECT_EQ(stats.SubmittedLightCount, 1u);
    EXPECT_EQ(stats.SkippedInvalidEntityCount, 0u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);
}

TEST(RuntimeRenderExtraction, VisualizationScalarAdapterBindingReachesRenderWorld)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    Geometry::PropertySet properties = MakeScalarProperties();
    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);

    constexpr std::uint64_t kAdapterKey = 0x51A1u;
    fixture.Extraction.RegisterVisualizationAdapter(
        kAdapterKey,
        std::make_unique<Runtime::PropertyScalarAdapter>(
            Geometry::ConstPropertySet{properties}));
    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(entity),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kAdapterKey,
            .BufferBDA = 0xCAFE1000u,
        });

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationAdapterScalarConfigsObserved, 1u);
    EXPECT_EQ(stats.VisualizationAdapterBindingsMissing, 0u);
    EXPECT_EQ(stats.VisualizationAdapterMissingAdapterCount, 0u);
    EXPECT_EQ(stats.VisualizationAdapterInvokedCount, 1u);
    EXPECT_EQ(stats.VisualizationAdapterPacketAppendCount, 1u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationAdapterInvalidBufferCount, 0u);

    ASSERT_EQ(world.Visualization.Scalars.size(), 1u);
    const Graphics::ScalarAttributePacket& packet = world.Visualization.Scalars.front();
    EXPECT_EQ(packet.Name, "curvature");
    EXPECT_EQ(packet.Domain, Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packet.ElementCount, 3u);
    EXPECT_EQ(packet.ScalarBufferBDA, 0xCAFE1000u);
    EXPECT_FLOAT_EQ(packet.RangeMin, -0.5f);
    EXPECT_FLOAT_EQ(packet.RangeMax, 1.5f);
    EXPECT_EQ(packet.Colormap, Graphics::Colormap::Type::Plasma);
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 1u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
    EXPECT_TRUE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, VisualizationScalarAdapterMissingBindingDoesNotSubmitPackets)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationAdapterScalarConfigsObserved, 1u);
    EXPECT_EQ(stats.VisualizationAdapterBindingsMissing, 1u);
    EXPECT_EQ(stats.VisualizationAdapterInvokedCount, 0u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 0u);
    EXPECT_TRUE(world.Visualization.Scalars.empty());
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 0u);
    EXPECT_FALSE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, VisualizationScalarAdapterInvalidBdaIsCounted)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    Geometry::PropertySet properties = MakeScalarProperties();
    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);

    constexpr std::uint64_t kAdapterKey = 0xBADBDAu;
    fixture.Extraction.RegisterVisualizationAdapter(
        kAdapterKey,
        std::make_unique<Runtime::PropertyScalarAdapter>(
            Geometry::ConstPropertySet{properties}));
    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(entity),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kAdapterKey,
            .BufferBDA = 0u,
        });

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationAdapterScalarConfigsObserved, 1u);
    EXPECT_EQ(stats.VisualizationAdapterInvokedCount, 1u);
    EXPECT_EQ(stats.VisualizationAdapterInvalidBufferCount, 1u);
    EXPECT_EQ(stats.VisualizationAdapterPacketAppendCount, 0u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 0u);
    EXPECT_TRUE(world.Visualization.Scalars.empty());
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 0u);
}

TEST(RuntimeRenderExtraction, VisualizationNonScalarAdapterBindingsReachRenderWorld)
{
    using BindingKind =
        Runtime::RenderExtractionCache::VisualizationAdapterBindingKind;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    Geometry::PropertySet colorProperties = MakeColorProperties();
    Geometry::PropertySet vectorProperties = MakeVectorProperties();
    Geometry::PropertySet scalarProperties = MakeScalarProperties();

    const auto colorEntity = scene.Create();
    ConfigureColorBufferVisualization(scene, colorEntity);

    const auto vectorEntity = scene.Create();
    ConfigureRenderablePoint(scene, vectorEntity);

    const auto isolineEntity = scene.Create();
    ConfigureScalarVisualization(scene, isolineEntity);
    auto& isolineConfig = scene.Raw().get<Graphics::Components::VisualizationConfig>(isolineEntity);
    isolineConfig.Scalar.AutoRange = false;
    isolineConfig.Scalar.RangeMin = -1.0f;
    isolineConfig.Scalar.RangeMax = 2.0f;
    isolineConfig.Scalar.Isolines.Num = 3u;
    isolineConfig.Scalar.Isolines.Width = 2.5f;
    isolineConfig.Scalar.Isolines.Color = {0.2f, 0.8f, 1.0f, 1.0f};

    const auto htexEntity = scene.Create();
    ConfigureRenderablePoint(scene, htexEntity);

    constexpr std::uint64_t kColorKey = 0xC010u;
    constexpr std::uint64_t kVectorKey = 0xBEEF'7001u;
    constexpr std::uint64_t kIsolineKey = 0x1501u;
    constexpr std::uint64_t kHtexKey = 0xA71A5u;
    fixture.Extraction.RegisterVisualizationAdapter(
        kColorKey,
        std::make_unique<Runtime::KMeansLabelAdapter>(
            Geometry::ConstPropertySet{colorProperties}));
    fixture.Extraction.RegisterVisualizationAdapter(
        kVectorKey,
        std::make_unique<Runtime::VectorFieldAdapter>(
            Geometry::ConstPropertySet{vectorProperties}));
    fixture.Extraction.RegisterVisualizationAdapter(
        kIsolineKey,
        std::make_unique<Runtime::IsolineAdapter>(
            Geometry::ConstPropertySet{scalarProperties}));
    fixture.Extraction.RegisterVisualizationAdapter(
        kHtexKey,
        std::make_unique<Runtime::HtexMetadataAdapter>());

    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(colorEntity),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kColorKey,
            .BufferBDA = 0xC010'0000u,
            .Kind = BindingKind::Color,
        });
    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(vectorEntity),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kVectorKey,
            .Kind = BindingKind::VectorField,
            .Options = Runtime::VisualizationAdapterOptions{
                .SourceName = "velocity",
                .OutputName = "velocity_glyphs",
                .Domain = Graphics::VisualizationAttributeDomain::Vertex,
                .PositionBufferBDA = 0xAABB'1000u,
                .VectorBufferBDA = 0xAABB'2000u,
                .VectorScale = 2.0f,
                .VectorColor = {1.0f, 0.25f, 0.0f, 1.0f},
                .DepthTested = false,
            },
        });
    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(isolineEntity),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kIsolineKey,
            .Kind = BindingKind::Isoline,
        });
    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(htexEntity),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kHtexKey,
            .BufferBDA = 0xFEED'CAFEu,
            .Kind = BindingKind::HtexMetadata,
            .Options = Runtime::VisualizationAdapterOptions{
                .SourceName = "curvature",
                .OutputName = "curvature_uv_bake",
                .EmitHtexPreview = true,
                .EmitFragmentBake = true,
                .SourceAttributeName = "curvature",
                .FragmentBakeMapping =
                    Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
                .MeshHasTexcoords = true,
                .PatchCount = 8u,
                .FaceCount = 24u,
                .AtlasWidth = 512u,
                .AtlasHeight = 256u,
            },
        });

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationAdapterScalarConfigsObserved, 1u);
    EXPECT_EQ(stats.VisualizationAdapterBindingsMissing, 0u);
    EXPECT_EQ(stats.VisualizationAdapterMissingAdapterCount, 0u);
    EXPECT_EQ(stats.VisualizationAdapterInvokedCount, 4u);
    EXPECT_EQ(stats.VisualizationAdapterPacketAppendCount, 5u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationVectorFieldPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationIsolinePacketCount, 1u);
    EXPECT_EQ(stats.VisualizationHtexAtlasPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationFragmentBakeAtlasPacketCount, 1u);

    ASSERT_EQ(world.Visualization.Colors.size(), 1u);
    EXPECT_EQ(world.Visualization.Colors.front().Name, "v:kmeans_color");
    EXPECT_EQ(world.Visualization.Colors.front().Domain,
              Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(world.Visualization.Colors.front().ColorBufferBDA, 0xC010'0000u);

    ASSERT_EQ(world.Visualization.VectorFields.size(), 1u);
    EXPECT_EQ(world.Visualization.VectorFields.front().Name, "velocity_glyphs");
    EXPECT_EQ(world.Visualization.VectorFields.front().PositionBufferBDA,
              0xAABB'1000u);
    EXPECT_EQ(world.Visualization.VectorFields.front().VectorBufferBDA,
              0xAABB'2000u);
    EXPECT_FALSE(world.Visualization.VectorFields.front().DepthTested);

    ASSERT_EQ(world.Visualization.Isolines.size(), 1u);
    EXPECT_EQ(world.Visualization.Isolines.front().SourceScalarName,
              "curvature");
    EXPECT_EQ(world.Visualization.Isolines.front().IsoValueCount, 3u);
    EXPECT_FLOAT_EQ(world.Visualization.Isolines.front().RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(world.Visualization.Isolines.front().RangeMax, 2.0f);
    EXPECT_FLOAT_EQ(world.Visualization.Isolines.front().LineWidth, 2.5f);

    ASSERT_EQ(world.Visualization.HtexAtlases.size(), 1u);
    EXPECT_EQ(world.Visualization.HtexAtlases.front().Name,
              "curvature_uv_bake");
    EXPECT_EQ(world.Visualization.HtexAtlases.front().PatchCount, 8u);

    ASSERT_EQ(world.Visualization.FragmentBakeAtlases.size(), 1u);
    EXPECT_EQ(world.Visualization.FragmentBakeAtlases.front().Name,
              "curvature_uv_bake");
    EXPECT_EQ(world.Visualization.FragmentBakeAtlases.front().TexcoordBufferBDA,
              0xFEED'CAFEu);

    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 5u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 5u);
    EXPECT_EQ(world.Visualization.Diagnostics.TextureResidencyDeferredCount, 2u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
    EXPECT_EQ(world.Visualization.OverlaySummary.VectorFieldCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.VectorGlyphCount, 3u);
    EXPECT_EQ(world.Visualization.OverlaySummary.IsolineLayerCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.IsolineValueCount, 3u);
    EXPECT_EQ(world.Visualization.OverlaySummary.HtexAtlasDescriptorCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.UvBakeAtlasDescriptorCount, 1u);
    EXPECT_TRUE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, VisualizationNonScalarAdapterFailuresAreCounted)
{
    using BindingKind =
        Runtime::RenderExtractionCache::VisualizationAdapterBindingKind;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto missingBinding = scene.Create();
    ConfigureColorBufferVisualization(scene, missingBinding);

    const auto missingAdapter = scene.Create();
    ConfigureRenderablePoint(scene, missingAdapter);
    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(missingAdapter),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0xDEADu,
            .Kind = BindingKind::VectorField,
            .Options = Runtime::VisualizationAdapterOptions{
                .SourceName = "velocity",
                .PositionBufferBDA = 0x1000u,
                .VectorBufferBDA = 0x2000u,
            },
        });

    const auto missingTexcoords = scene.Create();
    ConfigureRenderablePoint(scene, missingTexcoords);
    constexpr std::uint64_t kHtexKey = 0xBADC0DEu;
    fixture.Extraction.RegisterVisualizationAdapter(
        kHtexKey,
        std::make_unique<Runtime::HtexMetadataAdapter>());
    fixture.Extraction.SetVisualizationAdapterBinding(
        StableId(missingTexcoords),
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = kHtexKey,
            .Kind = BindingKind::HtexMetadata,
            .Options = Runtime::VisualizationAdapterOptions{
                .SourceName = "curvature",
                .EmitFragmentBake = true,
                .SourceAttributeName = "curvature",
                .FragmentBakeMapping =
                    Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
                .MeshHasTexcoords = false,
                .FaceCount = 24u,
                .AtlasWidth = 512u,
                .AtlasHeight = 256u,
            },
        });

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationAdapterBindingsMissing, 1u);
    EXPECT_EQ(stats.VisualizationAdapterMissingAdapterCount, 1u);
    EXPECT_EQ(stats.VisualizationAdapterInvokedCount, 1u);
    EXPECT_EQ(stats.VisualizationAdapterMissingSourceCount, 1u);
    EXPECT_EQ(stats.VisualizationAdapterPacketAppendCount, 0u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 0u);
    EXPECT_EQ(stats.VisualizationVectorFieldPacketCount, 0u);
    EXPECT_EQ(stats.VisualizationIsolinePacketCount, 0u);
    EXPECT_EQ(stats.VisualizationHtexAtlasPacketCount, 0u);
    EXPECT_EQ(stats.VisualizationFragmentBakeAtlasPacketCount, 0u);

    EXPECT_TRUE(world.Visualization.Colors.empty());
    EXPECT_TRUE(world.Visualization.VectorFields.empty());
    EXPECT_TRUE(world.Visualization.Isolines.empty());
    EXPECT_TRUE(world.Visualization.HtexAtlases.empty());
    EXPECT_TRUE(world.Visualization.FragmentBakeAtlases.empty());
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 0u);
    EXPECT_FALSE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, ObservesAssetSourceAsUnavailableWithoutGpuAssetCache)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::AssetInstance::Source>(entity).AssetId = 2u;

    const auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);
    EXPECT_EQ(stats.SourceAssetCacheUnavailableCount, 1u);
    EXPECT_EQ(stats.SourceAssetViewUnavailableCount, 0u);
    EXPECT_EQ(stats.SourceAssetRebindRequiredCount, 0u);
    EXPECT_EQ(stats.SourceAssetUpToDateCount, 0u);
}

TEST(RuntimeRenderExtraction, ObservesReadyAssetGenerationAsRebindRequired)
{
    RendererFixture fixture;
    ImmediateTransferQueue transfer;
    Graphics::GpuAssetCache gpuAssets(fixture.Renderer->GetBufferManager(),
                                      fixture.Renderer->GetTextureManager(),
                                      fixture.Renderer->GetSamplerManager(),
                                      transfer);
    const auto asset = MakeAssetId(3u);
    auto upload = gpuAssets.RequestUpload(Graphics::GpuBufferRequest{
        .Id = asset,
        .Bytes = std::span{ZeroBytes64},
        .Desc = TestBufferDesc(),
    });
    ASSERT_TRUE(upload.has_value()) << static_cast<int>(upload.error());
    gpuAssets.Tick(0u, fixture.Device.GetFramesInFlight());

    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();
    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::AssetInstance::Source>(entity).AssetId = asset.Index;

    const auto stats = fixture.Extract(scene, &gpuAssets);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);
    EXPECT_EQ(stats.SourceAssetCacheUnavailableCount, 0u);
    EXPECT_EQ(stats.SourceAssetViewUnavailableCount, 0u);
    EXPECT_EQ(stats.SourceAssetRebindRequiredCount, 1u);
    EXPECT_EQ(stats.SourceAssetUpToDateCount, 0u);
}

TEST(RuntimeRenderExtraction, HelperDoesNotMarkRebindGenerationAsSeen)
{
    RendererFixture fixture;
    ImmediateTransferQueue transfer;
    Graphics::GpuAssetCache gpuAssets(fixture.Renderer->GetBufferManager(),
                                      fixture.Renderer->GetTextureManager(),
                                      fixture.Renderer->GetSamplerManager(),
                                      transfer);
    const auto asset = MakeAssetId(4u);
    auto upload = gpuAssets.RequestUpload(Graphics::GpuBufferRequest{
        .Id = asset,
        .Bytes = std::span{ZeroBytes64},
        .Desc = TestBufferDesc(),
    });
    ASSERT_TRUE(upload.has_value()) << static_cast<int>(upload.error());
    gpuAssets.Tick(0u, fixture.Device.GetFramesInFlight());
    const auto view = gpuAssets.GetView(asset);
    ASSERT_TRUE(view.has_value());

    Graphics::Components::GpuSceneSlot slot{};
    const auto first = Runtime::ObserveRenderableAssetGeneration(slot, asset, &gpuAssets);

    EXPECT_EQ(first.Status, Runtime::RuntimeRenderableAssetObservationStatus::RebindRequired);
    EXPECT_EQ(first.ObservedGeneration, view->Generation);
    EXPECT_EQ(slot.SourceAsset, asset);
    EXPECT_EQ(slot.LastSeenAssetGeneration, 0u);

    slot.UpdateLastSeenAssetGeneration(view->Generation);
    const auto second = Runtime::ObserveRenderableAssetGeneration(slot, asset, &gpuAssets);

    EXPECT_EQ(second.Status, Runtime::RuntimeRenderableAssetObservationStatus::UpToDate);
    EXPECT_EQ(second.ObservedGeneration, view->Generation);
    EXPECT_EQ(slot.LastSeenAssetGeneration, view->Generation);
}

TEST(RuntimeRenderExtraction, AcknowledgeRebindAdvancesLastSeenGeneration)
{
    RendererFixture fixture;
    ImmediateTransferQueue transfer;
    Graphics::GpuAssetCache gpuAssets(fixture.Renderer->GetBufferManager(),
                                      fixture.Renderer->GetTextureManager(),
                                      fixture.Renderer->GetSamplerManager(),
                                      transfer);
    const auto asset = MakeAssetId(5u);
    auto upload = gpuAssets.RequestUpload(Graphics::GpuBufferRequest{
        .Id = asset,
        .Bytes = std::span{ZeroBytes64},
        .Desc = TestBufferDesc(),
    });
    ASSERT_TRUE(upload.has_value()) << static_cast<int>(upload.error());
    gpuAssets.Tick(0u, fixture.Device.GetFramesInFlight());
    const auto view = gpuAssets.GetView(asset);
    ASSERT_TRUE(view.has_value());

    Graphics::Components::GpuSceneSlot slot{};
    const auto pending = Runtime::ObserveRenderableAssetGeneration(slot, asset, &gpuAssets);
    ASSERT_EQ(pending.Status, Runtime::RuntimeRenderableAssetObservationStatus::RebindRequired);
    ASSERT_EQ(slot.LastSeenAssetGeneration, 0u);

    const auto ack = Runtime::AcknowledgeRenderableAssetRebind(slot, pending);
    EXPECT_EQ(ack, Runtime::RuntimeRenderableAssetAcknowledgmentResult::Acknowledged);
    EXPECT_EQ(slot.LastSeenAssetGeneration, view->Generation);

    const auto settled = Runtime::ObserveRenderableAssetGeneration(slot, asset, &gpuAssets);
    EXPECT_EQ(settled.Status, Runtime::RuntimeRenderableAssetObservationStatus::UpToDate);
    EXPECT_EQ(slot.LastSeenAssetGeneration, view->Generation);

    const auto reAck = Runtime::AcknowledgeRenderableAssetRebind(slot, settled);
    EXPECT_EQ(reAck, Runtime::RuntimeRenderableAssetAcknowledgmentResult::Acknowledged);
    EXPECT_EQ(slot.LastSeenAssetGeneration, view->Generation);
}

TEST(RuntimeRenderExtraction, AcknowledgeRebindRejectsObservationsWithoutGeneration)
{
    Graphics::Components::GpuSceneSlot slot{};
    const auto asset = MakeAssetId(6u);
    slot.SetSourceAsset(asset, 0u);

    Runtime::RuntimeRenderableAssetGenerationObservation cacheUnavailable{};
    cacheUnavailable.Status = Runtime::RuntimeRenderableAssetObservationStatus::CacheUnavailable;
    cacheUnavailable.SourceAsset = asset;
    cacheUnavailable.ObservedGeneration = 0u;

    const auto skipped = Runtime::AcknowledgeRenderableAssetRebind(slot, cacheUnavailable);
    EXPECT_EQ(skipped, Runtime::RuntimeRenderableAssetAcknowledgmentResult::SkippedNoObservedGeneration);
    EXPECT_EQ(slot.LastSeenAssetGeneration, 0u);
}

TEST(RuntimeRenderExtraction, AcknowledgeRebindRejectsAssetMismatch)
{
    Graphics::Components::GpuSceneSlot slot{};
    const auto bound = MakeAssetId(7u);
    slot.SetSourceAsset(bound, 4u);

    Runtime::RuntimeRenderableAssetGenerationObservation stale{};
    stale.Status = Runtime::RuntimeRenderableAssetObservationStatus::RebindRequired;
    stale.SourceAsset = MakeAssetId(8u);
    stale.ObservedGeneration = 9u;

    const auto skipped = Runtime::AcknowledgeRenderableAssetRebind(slot, stale);
    EXPECT_EQ(skipped, Runtime::RuntimeRenderableAssetAcknowledgmentResult::SkippedAssetMismatch);
    EXPECT_EQ(slot.LastSeenAssetGeneration, 4u);
    EXPECT_EQ(slot.SourceAsset, bound);
}

TEST(RuntimeRenderExtraction, AcknowledgeRebindRejectsSlotWithoutSourceAsset)
{
    Graphics::Components::GpuSceneSlot slot{};

    Runtime::RuntimeRenderableAssetGenerationObservation observation{};
    observation.Status = Runtime::RuntimeRenderableAssetObservationStatus::RebindRequired;
    observation.SourceAsset = MakeAssetId(9u);
    observation.ObservedGeneration = 3u;

    const auto skipped = Runtime::AcknowledgeRenderableAssetRebind(slot, observation);
    EXPECT_EQ(skipped, Runtime::RuntimeRenderableAssetAcknowledgmentResult::SkippedNoSourceAsset);
    EXPECT_FALSE(slot.HasSourceAsset());
    EXPECT_EQ(slot.LastSeenAssetGeneration, 0u);
}

TEST(RuntimeRenderExtraction, ExtractAndSubmitDoesNotAutoAcknowledgeRebinds)
{
    RendererFixture fixture;
    ImmediateTransferQueue transfer;
    Graphics::GpuAssetCache gpuAssets(fixture.Renderer->GetBufferManager(),
                                      fixture.Renderer->GetTextureManager(),
                                      fixture.Renderer->GetSamplerManager(),
                                      transfer);
    const auto asset = MakeAssetId(10u);
    auto upload = gpuAssets.RequestUpload(Graphics::GpuBufferRequest{
        .Id = asset,
        .Bytes = std::span{ZeroBytes64},
        .Desc = TestBufferDesc(),
    });
    ASSERT_TRUE(upload.has_value()) << static_cast<int>(upload.error());
    gpuAssets.Tick(0u, fixture.Device.GetFramesInFlight());

    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();
    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::AssetInstance::Source>(entity).AssetId = asset.Index;

    const auto first = fixture.Extract(scene, &gpuAssets);
    EXPECT_EQ(first.SourceAssetRebindRequiredCount, 1u);
    EXPECT_EQ(first.SourceAssetRebindAcknowledgedCount, 0u);

    const auto second = fixture.Extract(scene, &gpuAssets);
    EXPECT_EQ(second.SourceAssetRebindRequiredCount, 1u);
    EXPECT_EQ(second.SourceAssetUpToDateCount, 0u);
    EXPECT_EQ(second.SourceAssetRebindAcknowledgedCount, 0u);
}

// ============================================================================
// RUNTIME-082 Slice D — spatial-debug adapter pump integration.
//
// These tests exercise the runtime extraction wiring end-to-end against the
// Null renderer: entities carry a `SpatialDebugBinding` component, adapters
// are registered into the cache (which owns them via `unique_ptr`), and
// `ExtractAndSubmit` looks up + invokes the active adapter, folds per-frame
// stats onto `RuntimeRenderExtractionStats`, and attaches the resulting
// snapshot spans to `RuntimeRenderSnapshotBatch`.
// ============================================================================

namespace
{
    [[nodiscard]] Geometry::BVH MakeFourBoxBvh()
    {
        std::vector<Geometry::AABB> boxes{
            Geometry::AABB{{-4.0f, -1.0f, -1.0f}, {-3.0f, 1.0f, 1.0f}},
            Geometry::AABB{{-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f, 1.0f}},
            Geometry::AABB{{ 1.0f, -1.0f, -1.0f}, { 2.0f, 1.0f, 1.0f}},
            Geometry::AABB{{ 3.0f, -1.0f, -1.0f}, { 4.0f, 1.0f, 1.0f}},
        };
        Geometry::BVH bvh;
        Geometry::BVHBuildParams params{};
        params.LeafSize = 1;
        EXPECT_TRUE(bvh.Build(boxes, params).has_value());
        return bvh;
    }

    [[nodiscard]] Geometry::ConvexHull MakeUnitCubeHull()
    {
        Geometry::ConvexHull hull{};
        hull.Vertices = {
            glm::vec3{0.f, 0.f, 0.f},
            glm::vec3{1.f, 0.f, 0.f},
            glm::vec3{0.f, 1.f, 0.f},
            glm::vec3{1.f, 1.f, 0.f},
            glm::vec3{0.f, 0.f, 1.f},
            glm::vec3{1.f, 0.f, 1.f},
            glm::vec3{0.f, 1.f, 1.f},
            glm::vec3{1.f, 1.f, 1.f},
        };
        hull.Planes = {
            Geometry::Plane{glm::vec3{-1.f, 0.f, 0.f}, 0.f},
            Geometry::Plane{glm::vec3{ 1.f, 0.f, 0.f}, -1.f},
            Geometry::Plane{glm::vec3{0.f, -1.f, 0.f}, 0.f},
            Geometry::Plane{glm::vec3{0.f,  1.f, 0.f}, -1.f},
            Geometry::Plane{glm::vec3{0.f, 0.f, -1.f}, 0.f},
            Geometry::Plane{glm::vec3{0.f, 0.f,  1.f}, -1.f},
        };
        return hull;
    }
}

TEST(RuntimeRenderExtraction, SpatialDebugBindingResolvesAndPumpsBvhAdapter)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const Geometry::BVH bvh = MakeFourBoxBvh();
    constexpr std::uint64_t kKey = 0xB7u;
    fixture.Extraction.RegisterSpatialDebugAdapter(
        kKey, std::make_unique<Runtime::BvhAdapter>(bvh));
    ASSERT_EQ(fixture.Extraction.GetSpatialDebugAdapterCount(), 1u);

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.Kind        = ECS::Components::SpatialDebugGeometryKind::Bvh;
    binding.RegistryKey = kKey;

    const auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.SpatialDebugBindingsObserved, 1u);
    EXPECT_EQ(stats.SpatialDebugAdaptersInvoked, 1u);
    EXPECT_EQ(stats.SpatialDebugMissingAdapterCount, 0u);
    EXPECT_EQ(stats.SpatialDebugBoundsCount,
              static_cast<std::uint32_t>(bvh.Nodes().size()));
    EXPECT_EQ(stats.SpatialDebugHierarchyNodeCount, stats.SpatialDebugBoundsCount);

    std::uint32_t expectedLeafCount   = 0u;
    std::uint32_t expectedInnerCount  = 0u;
    for (const auto& node : bvh.Nodes())
    {
        if (node.IsLeaf) ++expectedLeafCount;
        else             ++expectedInnerCount;
    }
    EXPECT_EQ(stats.SpatialDebugLeafNodeAccumulator,  expectedLeafCount);
    EXPECT_EQ(stats.SpatialDebugInnerNodeAccumulator, expectedInnerCount);
    EXPECT_EQ(stats.SpatialDebugSplitPlaneCount,       expectedInnerCount);
    EXPECT_EQ(stats.SpatialDebugConvexHullVertexCount, 0u);
    EXPECT_EQ(stats.SpatialDebugConvexHullEdgeCount,   0u);
    EXPECT_EQ(stats.SpatialDebugPointMarkerCount,      0u);
}

TEST(RuntimeRenderExtraction, SpatialDebugBindingMissingAdapterIsCounted)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.Kind        = ECS::Components::SpatialDebugGeometryKind::Bvh;
    binding.RegistryKey = 42u;

    const auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.SpatialDebugBindingsObserved, 1u);
    EXPECT_EQ(stats.SpatialDebugAdaptersInvoked, 0u);
    EXPECT_EQ(stats.SpatialDebugMissingAdapterCount, 1u);
    EXPECT_EQ(stats.SpatialDebugBoundsCount, 0u);
    EXPECT_EQ(stats.SpatialDebugHierarchyNodeCount, 0u);
    EXPECT_EQ(stats.SpatialDebugSplitPlaneCount, 0u);
}

TEST(RuntimeRenderExtraction, SpatialDebugAdapterUnregistrationDrainsPump)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const Geometry::BVH bvh = MakeFourBoxBvh();
    constexpr std::uint64_t kKey = 7u;
    fixture.Extraction.RegisterSpatialDebugAdapter(
        kKey, std::make_unique<Runtime::BvhAdapter>(bvh));

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.RegistryKey = kKey;

    const auto firstStats = fixture.Extract(scene);
    ASSERT_GT(firstStats.SpatialDebugHierarchyNodeCount, 0u);

    EXPECT_TRUE(fixture.Extraction.UnregisterSpatialDebugAdapter(kKey));
    EXPECT_FALSE(fixture.Extraction.UnregisterSpatialDebugAdapter(kKey));
    EXPECT_EQ(fixture.Extraction.GetSpatialDebugAdapterCount(), 0u);

    const auto secondStats = fixture.Extract(scene);

    EXPECT_EQ(secondStats.SpatialDebugBindingsObserved, 1u);
    EXPECT_EQ(secondStats.SpatialDebugAdaptersInvoked, 0u);
    EXPECT_EQ(secondStats.SpatialDebugMissingAdapterCount, 1u);
    EXPECT_EQ(secondStats.SpatialDebugBoundsCount, 0u);
    EXPECT_EQ(secondStats.SpatialDebugHierarchyNodeCount, 0u);
}

TEST(RuntimeRenderExtraction, SpatialDebugReRegisterReplacesAdapterAndPreservesPump)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const Geometry::BVH bvh = MakeFourBoxBvh();
    const Geometry::ConvexHull hull = MakeUnitCubeHull();
    constexpr std::uint64_t kKey = 99u;

    fixture.Extraction.RegisterSpatialDebugAdapter(
        kKey, std::make_unique<Runtime::BvhAdapter>(bvh));

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.Kind        = ECS::Components::SpatialDebugGeometryKind::Bvh;
    binding.RegistryKey = kKey;

    const auto bvhStats = fixture.Extract(scene);
    ASSERT_GT(bvhStats.SpatialDebugHierarchyNodeCount, 0u);
    EXPECT_EQ(bvhStats.SpatialDebugConvexHullVertexCount, 0u);

    // Replace the BVH adapter with a ConvexHull adapter at the same key.
    // The cache must drop the prior unique_ptr, install the new one, and
    // refresh the registry slot without leaving a dangling raw pointer.
    fixture.Extraction.RegisterSpatialDebugAdapter(
        kKey, std::make_unique<Runtime::ConvexHullAdapter>(hull));
    binding.Kind = ECS::Components::SpatialDebugGeometryKind::ConvexHull;
    EXPECT_EQ(fixture.Extraction.GetSpatialDebugAdapterCount(), 1u);

    const auto hullStats = fixture.Extract(scene);

    EXPECT_EQ(hullStats.SpatialDebugAdaptersInvoked, 1u);
    EXPECT_EQ(hullStats.SpatialDebugHierarchyNodeCount, 0u);
    EXPECT_EQ(hullStats.SpatialDebugConvexHullVertexCount, 8u);
    EXPECT_EQ(hullStats.SpatialDebugConvexHullEdgeCount, 12u);
}

TEST(RuntimeRenderExtraction, SpatialDebugBindingProducesVisibleDebugPrimitivesInRenderWorld)
{
    // Regression for the original Slice D landing: the runtime pump filled
    // RuntimeRenderSnapshotBatch::SpatialDebug* spans, but the renderer
    // dropped them — counters were nonzero while no debug geometry reached
    // RenderWorld::DebugPrimitives. This test asserts the full pipe: a
    // SpatialDebugBinding + registered BVH adapter must end up producing a
    // non-zero line count in the extracted RenderWorld.
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const Geometry::BVH bvh = MakeFourBoxBvh();
    constexpr std::uint64_t kKey = 0xBADCAB1Eu;
    fixture.Extraction.RegisterSpatialDebugAdapter(
        kKey, std::make_unique<Runtime::BvhAdapter>(bvh));

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.RegistryKey = kKey;

    const auto stats = fixture.Extract(scene);
    ASSERT_GT(stats.SpatialDebugHierarchyNodeCount, 0u);
    ASSERT_GT(stats.SpatialDebugSplitPlaneCount, 0u);

    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_TRUE(world.DebugPrimitives.HasTransientDebug);
    EXPECT_GT(world.DebugPrimitives.LineCount, 0u);
    EXPECT_EQ(world.DebugPrimitives.Lines.size(), world.DebugPrimitives.LineCount);

    // BVH wireframe + split-plane wireframe both emit cuboid line packets;
    // 12 lines per AABB box and 12 lines per split-plane bounding box. We
    // only sanity-check that the merged count meets the minimum for a
    // single visualized node so the assertion is robust against future
    // visualizer-options or renderer-side budgeting changes.
    EXPECT_GE(world.DebugPrimitives.LineCount, 12u);
}

TEST(RuntimeRenderExtraction, SpatialDebugMissingAdapterProducesNoDebugPrimitives)
{
    // Control case for the routing regression above: when a binding has no
    // adapter, the pump submits empty SpatialDebug* spans and the renderer
    // must not synthesise any debug geometry.
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.RegistryKey = 0xFFFFFFFFu;

    const auto stats = fixture.Extract(scene);
    ASSERT_EQ(stats.SpatialDebugMissingAdapterCount, 1u);

    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(world.DebugPrimitives.LineCount, 0u);
    EXPECT_EQ(world.DebugPrimitives.PointCount, 0u);
    EXPECT_EQ(world.DebugPrimitives.TriangleCount, 0u);
}

TEST(RuntimeRenderExtraction, SpatialDebugConvexHullBindingProducesEdgeLines)
{
    // Convex-hull pump must reach RenderWorld too. A unit cube hull yields
    // 12 edges → at least 12 debug lines after the renderer-side build.
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const Geometry::ConvexHull hull = MakeUnitCubeHull();
    constexpr std::uint64_t kKey = 0xC0DEu;
    fixture.Extraction.RegisterSpatialDebugAdapter(
        kKey, std::make_unique<Runtime::ConvexHullAdapter>(hull));

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.Kind        = ECS::Components::SpatialDebugGeometryKind::ConvexHull;
    binding.RegistryKey = kKey;

    const auto stats = fixture.Extract(scene);
    ASSERT_EQ(stats.SpatialDebugConvexHullVertexCount, 8u);
    ASSERT_EQ(stats.SpatialDebugConvexHullEdgeCount, 12u);

    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_GE(world.DebugPrimitives.LineCount, 12u);
    EXPECT_TRUE(world.DebugPrimitives.HasTransientDebug);
}

TEST(RuntimeRenderExtraction, SpatialDebugBindingHonorsLeafOnlyAndDepthCap)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const Geometry::BVH bvh = MakeFourBoxBvh();
    constexpr std::uint64_t kKey = 1234u;
    fixture.Extraction.RegisterSpatialDebugAdapter(
        kKey, std::make_unique<Runtime::BvhAdapter>(bvh));

    const auto entity = scene.Create();
    auto& binding = registry.emplace<ECS::Components::SpatialDebugBinding>(entity);
    binding.RegistryKey = kKey;
    binding.LeafOnly    = true;

    const auto leafOnlyStats = fixture.Extract(scene);
    EXPECT_EQ(leafOnlyStats.SpatialDebugSplitPlaneCount, 0u);
    EXPECT_EQ(leafOnlyStats.SpatialDebugInnerNodeAccumulator, 0u);
    EXPECT_GT(leafOnlyStats.SpatialDebugLeafNodeAccumulator, 0u);

    binding.LeafOnly = false;
    binding.MaxDepth = 0u;

    const auto depthCapStats = fixture.Extract(scene);
    EXPECT_EQ(depthCapStats.SpatialDebugHierarchyNodeCount, 1u);
    EXPECT_EQ(depthCapStats.SpatialDebugDepthCapTruncationAccumulator, 1u);
}
