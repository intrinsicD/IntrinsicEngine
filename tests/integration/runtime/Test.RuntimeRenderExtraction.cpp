#include <cstdint>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <entt/entity/entity.hpp>
#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "EditorFeatureTestContext.hpp"

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
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
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.AABB;
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

    [[nodiscard]] std::uint64_t FindLatestGeometryColorBda(
        const Tests::MockDevice& device,
        const RHI::BufferHandle geometryBuffer,
        const std::uint32_t geometrySlot)
    {
        for (auto it = device.BufferWrites.rbegin(); it != device.BufferWrites.rend(); ++it)
        {
            if (it->Handle != geometryBuffer ||
                it->Data.size() % sizeof(RHI::GpuGeometryRecord) != 0u)
            {
                continue;
            }

            const std::uint64_t firstSlot =
                it->Offset / sizeof(RHI::GpuGeometryRecord);
            const std::uint64_t count =
                static_cast<std::uint64_t>(it->Data.size() / sizeof(RHI::GpuGeometryRecord));
            if (geometrySlot < firstSlot || geometrySlot >= firstSlot + count)
            {
                continue;
            }

            const auto records = std::span<const RHI::GpuGeometryRecord>{
                reinterpret_cast<const RHI::GpuGeometryRecord*>(it->Data.data()),
                static_cast<std::size_t>(count),
            };
            return records[static_cast<std::size_t>(geometrySlot - firstSlot)].ColorBufferBDA;
        }

        return 0u;
    }

    void AttachProceduralTriangle(ECS::Scene::Registry& scene,
                                  entt::entity entity)
    {
        scene.Raw().emplace_or_replace<ECS::Components::ProceduralGeometryRef>(entity);
    }

    void AttachPointCloudSources(ECS::Scene::Registry& scene,
                                 entt::entity entity)
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace PN = ECS::Components::GeometrySources::PropertyNames;

        auto& vertices = scene.Raw().emplace_or_replace<GS::Vertices>(entity);
        vertices.Properties.Resize(3u);
        auto positions = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        positions.Vector() = std::vector<glm::vec3>{
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        };
    }

    void AttachLineGraphSources(ECS::Scene::Registry& scene,
                                entt::entity entity)
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace PN = ECS::Components::GeometrySources::PropertyNames;

        auto& raw = scene.Raw();
        auto& nodes = raw.emplace_or_replace<GS::Nodes>(entity);
        nodes.Properties.Resize(3u);
        nodes.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f}).Vector() = {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        };

        auto& edges = raw.emplace_or_replace<GS::Edges>(entity);
        edges.Properties.Resize(2u);
        edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV0},
            0u).Vector() = {0u, 1u};
        edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV1},
            0u).Vector() = {1u, 2u};

        raw.emplace_or_replace<GS::HasGraphTopology>(entity);
    }

    void AttachTriangleMeshSources(ECS::Scene::Registry& scene,
                                   entt::entity entity)
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace PN = ECS::Components::GeometrySources::PropertyNames;

        auto& raw = scene.Raw();
        auto& vertices = raw.emplace_or_replace<GS::Vertices>(entity);
        vertices.Properties.Resize(3u);
        vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f}).Vector() = {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        };
        vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kNormal},
            glm::vec3{0.0f, 0.0f, 1.0f}).Vector() = {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        };
        vertices.Properties.GetOrAdd<glm::vec4>(
            "v:color",
            glm::vec4{1.0f}).Vector() = {
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, 1.0f},
        };

        raw.emplace_or_replace<GS::Edges>(entity).Properties.Resize(3u);

        auto& halfedges = raw.emplace_or_replace<GS::Halfedges>(entity);
        halfedges.Properties.Resize(3u);
        halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeToVertex},
            0u).Vector() = {0u, 1u, 2u};
        halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeNext},
            0u).Vector() = {1u, 2u, 0u};
        halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeFace},
            0u).Vector() = {0u, 0u, 0u};

        auto& faces = raw.emplace_or_replace<GS::Faces>(entity);
        faces.Properties.Resize(1u);
        faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kFaceHalfedge},
            0u).Vector() = {0u};
        raw.emplace_or_replace<GS::HasMeshTopology>(entity);
    }

    void AttachPointScalarProperty(ECS::Scene::Registry& scene,
                                   entt::entity entity,
                                   const std::string& name = "curvature")
    {
        auto& vertices = scene.Raw().get<
            ECS::Components::GeometrySources::Vertices>(entity);
        vertices.Properties.GetOrAdd<float>(name, 0.0f).Vector() =
            {-0.5f, 0.25f, 1.5f};
    }

    void AttachPointColorProperty(ECS::Scene::Registry& scene,
                                  entt::entity entity,
                                  const std::string& name = "v:kmeans_color")
    {
        auto& vertices = scene.Raw().get<
            ECS::Components::GeometrySources::Vertices>(entity);
        vertices.Properties.GetOrAdd<glm::vec4>(name, glm::vec4{1.0f}).Vector() = {
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, 1.0f},
        };
    }

    void AttachPointVectorProperty(ECS::Scene::Registry& scene,
                                   entt::entity entity,
                                   const std::string& name = "velocity")
    {
        auto& vertices = scene.Raw().get<
            ECS::Components::GeometrySources::Vertices>(entity);
        vertices.Properties.GetOrAdd<glm::vec3>(name, glm::vec3{0.0f}).Vector() = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
    }

    [[nodiscard]] Runtime::GeometryPropertyRef PointProperty(
        const std::string& name,
        const Geometry::PropertyValueKind valueKind)
    {
        return Runtime::GeometryPropertyRef{
            .Domain = Runtime::GeometryElementDomain::PointCloudPoint,
            .Name = name,
            .ValueKind = valueKind,
        };
    }

    void ConfigureScalarVisualization(ECS::Scene::Registry& scene,
                                      entt::entity entity)
    {
        auto& registry = scene.Raw();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderPoints>(entity);
        AttachPointCloudSources(scene, entity);

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
        AttachPointCloudSources(scene, entity);

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
        AttachPointCloudSources(scene, entity);
    }

    void ConfigureMeshColorVisualization(ECS::Scene::Registry& scene,
                                         entt::entity entity,
                                         const std::string& propertyName)
    {
        auto& registry = scene.Raw();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderSurface>(entity);
        AttachTriangleMeshSources(scene, entity);

        auto& visualization = registry.emplace<Graphics::Components::VisualizationConfig>(entity);
        visualization.Source = Graphics::Components::VisualizationConfig::ColorSource::PerVertexBuffer;
        visualization.ColorBufferName = propertyName;
    }

    void ConfigureGraphScalarVisualization(
        ECS::Scene::Registry& scene,
        entt::entity entity,
        const std::string& propertyName,
        Graphics::Components::VisualizationConfig::Domain domain)
    {
        auto& registry = scene.Raw();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderEdges>(entity);
        AttachLineGraphSources(scene, entity);

        auto& visualization = registry.emplace<Graphics::Components::VisualizationConfig>(entity);
        visualization.Source = Graphics::Components::VisualizationConfig::ColorSource::ScalarField;
        visualization.ScalarFieldName = propertyName;
        visualization.ScalarDomain = domain;
        visualization.Scalar.Map = Graphics::Colormap::Type::Plasma;
    }

    void ConfigureGraphColorVisualization(
        ECS::Scene::Registry& scene,
        entt::entity entity,
        const std::string& propertyName,
        Graphics::Components::VisualizationConfig::ColorSource source)
    {
        auto& registry = scene.Raw();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderEdges>(entity);
        AttachLineGraphSources(scene, entity);

        auto& visualization = registry.emplace<Graphics::Components::VisualizationConfig>(entity);
        visualization.Source = source;
        visualization.ColorBufferName = propertyName;
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
    AttachProceduralTriangle(scene, entity);
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

TEST(RuntimeRenderExtraction, ClearSceneStateDropsSidecarsAndPerEntityRecipes)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);
    AttachPointScalarProperty(scene, entity);
    const std::uint32_t stableId = StableId(entity);

    fixture.Extraction.SetVisualizationRecipe(
        stableId,
        Runtime::VisualizationRecipe{.Data = Runtime::ScalarVisualizationRecipe{
            .Source = PointProperty("curvature", Geometry::PropertyValueKind::Float),
            .OutputName = "curvature",
            .BufferBDA = 0xCAFE1000u,
        }});
    const auto stats = fixture.Extract(scene);
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);
    ASSERT_TRUE(fixture.Extraction.GetVisualizationRecipe(stableId).has_value());

    fixture.Extraction.ClearSceneState(*fixture.Renderer);
    const Graphics::RenderWorld world =
        fixture.Renderer->ExtractRenderWorld(Graphics::RenderFrameInput{});

    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 0u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 0u);
    EXPECT_FALSE(fixture.Extraction.GetVisualizationRecipe(stableId).has_value());
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
    registry.emplace<Graphics::Components::RenderEdges>(entity);

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

TEST(RuntimeRenderExtraction, ReusesLiveRenderableKeyScratchAcrossExtractions)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    constexpr std::uint32_t kRenderableCount = 64u;
    std::vector<entt::entity> entities;
    entities.reserve(kRenderableCount);

    for (std::uint32_t i = 0u; i < kRenderableCount; ++i)
    {
        const auto entity = scene.Create();
        registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        registry.emplace<Graphics::Components::RenderEdges>(entity);
        entities.push_back(entity);
    }

    auto stats = fixture.Extract(scene);
    ASSERT_EQ(stats.CandidateRenderableCount, kRenderableCount);
    EXPECT_EQ(stats.AllocatedInstanceCount, kRenderableCount);
    const std::size_t firstBucketCount =
        fixture.Extraction.GetLiveRenderableKeyScratchBucketCountForTest();
    ASSERT_GT(firstBucketCount, std::size_t{0});

    stats = fixture.Extract(scene);
    EXPECT_EQ(stats.CandidateRenderableCount, kRenderableCount);
    EXPECT_EQ(stats.AllocatedInstanceCount, 0u);
    EXPECT_EQ(fixture.Extraction.GetLiveRenderableKeyScratchBucketCountForTest(),
              firstBucketCount);

    for (const entt::entity entity : entities)
    {
        scene.Destroy(entity);
    }

    stats = fixture.Extract(scene);
    EXPECT_EQ(stats.CandidateRenderableCount, 0u);
    EXPECT_EQ(stats.FreedInstanceCount, kRenderableCount);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 0u);
    EXPECT_EQ(fixture.Extraction.GetLiveRenderableKeyScratchBucketCountForTest(),
              firstBucketCount);
}

TEST(RuntimeRenderExtraction, ExtractsVisualizationAndLightsWithoutRenderableOwnership)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto visualized = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(visualized).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderPoints>(visualized);
    AttachPointCloudSources(scene, visualized);
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

TEST(RuntimeRenderExtraction, VisualizationScalarRecipeReachesRenderWorld)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);
    AttachPointScalarProperty(scene, entity);

    fixture.Extraction.SetVisualizationRecipe(
        StableId(entity),
        Runtime::VisualizationRecipe{.Data = Runtime::ScalarVisualizationRecipe{
            .Source = PointProperty("curvature", Geometry::PropertyValueKind::Float),
            .OutputName = "curvature",
            .BufferBDA = 0xCAFE1000u,
            .AutoRange = true,
            .Colormap = Graphics::Colormap::Type::Plasma,
        }});

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationRecipeEncodeCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 1u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeInvalidBufferCount, 0u);
    EXPECT_EQ(stats.VisualizationRecipeScalarValueScanCount, 3u);

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

TEST(RuntimeRenderExtraction, VisualizationScalarConfigMissingSourceFailsClosed)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationRecipeScalarConfigsObserved, 1u);
    EXPECT_EQ(stats.VisualizationRecipeEncodeCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeMissingSourceCount, 1u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 0u);
    EXPECT_TRUE(world.Visualization.Scalars.empty());
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 0u);
    EXPECT_FALSE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, VisualizationScalarRecipeMissingBdaUploadsPropertyBuffer)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    ConfigureScalarVisualization(scene, entity);
    AttachPointScalarProperty(scene, entity);

    fixture.Extraction.SetVisualizationRecipe(
        StableId(entity),
        Runtime::VisualizationRecipe{.Data = Runtime::ScalarVisualizationRecipe{
            .Source = PointProperty("curvature", Geometry::PropertyValueKind::Float),
            .OutputName = "curvature",
            .BufferBDA = 0u,
            .BufferSourceKey =
                std::to_string(StableId(entity)) + ":scalar:curvature",
            .AutoRange = true,
            .Colormap = Graphics::Colormap::Type::Plasma,
        }});

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationRecipeEncodeCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeInvalidBufferCount, 0u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 1u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeScalarValueScanCount, 3u);
    ASSERT_EQ(world.Visualization.Scalars.size(), 1u);
    EXPECT_NE(world.Visualization.Scalars.front().ScalarBufferBDA, 0u);
    EXPECT_EQ(world.Visualization.Scalars.front().SourceBufferKey,
              std::to_string(StableId(entity)) + ":scalar:curvature");
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 1u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 1u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 1u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
}

TEST(RuntimeRenderExtraction, VisualizationPropertyBufferKeysAreStableIdScoped)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto first = scene.Create();
    const auto second = scene.Create();
    ConfigureScalarVisualization(scene, first);
    ConfigureScalarVisualization(scene, second);
    AttachPointScalarProperty(scene, first);
    AttachPointScalarProperty(scene, second);

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationRecipeEncodeCount, 2u);
    EXPECT_EQ(stats.VisualizationRecipeInvalidBufferCount, 0u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 2u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 2u);
    ASSERT_EQ(world.Visualization.Scalars.size(), 2u);
    EXPECT_NE(world.Visualization.Scalars[0].SourceBufferKey,
              world.Visualization.Scalars[1].SourceBufferKey);
    EXPECT_NE(world.Visualization.Scalars[0].SourceBufferKey, "curvature");
    EXPECT_NE(world.Visualization.Scalars[1].SourceBufferKey, "curvature");
    EXPECT_NE(world.Visualization.Scalars[0].ScalarBufferBDA, 0u);
    EXPECT_NE(world.Visualization.Scalars[1].ScalarBufferBDA, 0u);
    EXPECT_NE(world.Visualization.Scalars[0].ScalarBufferBDA,
              world.Visualization.Scalars[1].ScalarBufferBDA);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 2u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 2u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
}

TEST(RuntimeRenderExtraction, MeshColorVisualizationPropertyBufferUploadsFromGeometrySources)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    ConfigureMeshColorVisualization(scene, entity, "v:color");

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 1u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeMissingSourceCount, 0u);
    EXPECT_EQ(stats.VisualizationRecipeUnsupportedSourceTypeCount, 0u);

    ASSERT_EQ(world.Visualization.Colors.size(), 1u);
    const Graphics::ColorAttributePacket& packet = world.Visualization.Colors.front();
    EXPECT_EQ(packet.Name, "v:color");
    EXPECT_EQ(packet.SourceBufferKey,
              std::to_string(StableId(entity)) + ":color:v:color");
    EXPECT_EQ(packet.Domain, Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(packet.ElementCount, 3u);
    EXPECT_NE(packet.ColorBufferBDA, 0u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 1u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 1u);
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 1u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 1u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
    EXPECT_TRUE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, PointCloudVisualizationPropertyBuffersUploadFromGeometrySources)
{
    namespace GS = ECS::Components::GeometrySources;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto scalarEntity = scene.Create();
    ConfigureScalarVisualization(scene, scalarEntity);
    auto& scalarVertices =
        scene.Raw().get<GS::Vertices>(scalarEntity);
    scalarVertices.Properties.GetOrAdd<float>("curvature", 0.0f).Vector() =
        {-0.5f, 0.25f, 1.5f};

    const auto colorEntity = scene.Create();
    ConfigureColorBufferVisualization(scene, colorEntity);
    auto& colorVertices =
        scene.Raw().get<GS::Vertices>(colorEntity);
    colorVertices.Properties.GetOrAdd<glm::vec4>(
        "v:kmeans_color",
        glm::vec4{1.0f}).Vector() = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.PointCloudGeometryUploads, 2u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 2u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeMissingSourceCount, 0u);
    EXPECT_EQ(stats.VisualizationRecipeUnsupportedSourceTypeCount, 0u);

    ASSERT_EQ(world.Visualization.Scalars.size(), 1u);
    const Graphics::ScalarAttributePacket& scalarPacket =
        world.Visualization.Scalars.front();
    EXPECT_EQ(scalarPacket.Name, "curvature");
    EXPECT_EQ(scalarPacket.SourceBufferKey,
              std::to_string(StableId(scalarEntity)) + ":scalar:curvature");
    EXPECT_EQ(scalarPacket.Domain, Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(scalarPacket.ElementCount, 3u);
    EXPECT_NE(scalarPacket.ScalarBufferBDA, 0u);

    ASSERT_EQ(world.Visualization.Colors.size(), 1u);
    const Graphics::ColorAttributePacket& colorPacket =
        world.Visualization.Colors.front();
    EXPECT_EQ(colorPacket.Name, "v:kmeans_color");
    EXPECT_EQ(colorPacket.SourceBufferKey,
              std::to_string(StableId(colorEntity)) + ":color:v:kmeans_color");
    EXPECT_EQ(colorPacket.Domain, Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(colorPacket.ElementCount, 3u);
    EXPECT_NE(colorPacket.ColorBufferBDA, 0u);

    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 2u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 2u);
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 2u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 2u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
    EXPECT_TRUE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, MeshScalarPropertyBufferReachesPreparedEntityConfigWithAutoRange)
{
    namespace GS = ECS::Components::GeometrySources;
    using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
    using Domain = Graphics::Components::VisualizationConfig::Domain;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    auto& registry = scene.Raw();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
        glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    AttachTriangleMeshSources(scene, entity);

    auto& vertexProperties = registry.get<GS::Vertices>(entity).Properties;
    vertexProperties.GetOrAdd<float>("v:temperature", 0.0f).Vector() =
        {-4.0f, 0.5f, 8.0f};

    auto& visualization =
        registry.emplace<Graphics::Components::VisualizationConfig>(entity);
    visualization.Source = ColorSource::ScalarField;
    visualization.ScalarFieldName = "v:temperature";
    visualization.ScalarDomain = Domain::Vertex;
    visualization.Scalar.AutoRange = true;
    visualization.Scalar.Map = Graphics::Colormap::Type::Plasma;
    visualization.Scalar.Isolines.Num = 6u;
    visualization.Scalar.Isolines.Width = 2.5f;
    visualization.Scalar.Isolines.Color = {0.25f, 0.75f, 1.0f, 1.0f};

    const auto stats = fixture.Extract(scene);
    Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);
    ASSERT_EQ(stats.VisualizationScalarPacketCount, 1u);
    ASSERT_EQ(world.Visualization.Scalars.size(), 1u);
    EXPECT_FLOAT_EQ(world.Visualization.Scalars.front().RangeMin, -4.0f);
    EXPECT_FLOAT_EQ(world.Visualization.Scalars.front().RangeMax, 8.0f);

    fixture.Renderer->PrepareFrame(world);

    const auto sidecar =
        fixture.Extraction.FindRenderableSidecarForTest(StableId(entity));
    ASSERT_TRUE(sidecar.has_value());
    ASSERT_TRUE(sidecar->Instance.IsValid());

    const RHI::GpuEntityConfig config =
        fixture.Renderer->GetGpuWorld().GetEntityConfigForTest(sidecar->Instance);
    EXPECT_EQ(config.ColorSourceMode, 2u);
    EXPECT_NE(config.ScalarBDA, 0u);
    EXPECT_NE(config.ColormapID, 0u);
    EXPECT_EQ(config.ElementCount, 3u);
    EXPECT_FLOAT_EQ(config.ScalarRangeMin, -4.0f);
    EXPECT_FLOAT_EQ(config.ScalarRangeMax, 8.0f);
    EXPECT_FLOAT_EQ(config.IsolineCount, 6.0f);
    EXPECT_FLOAT_EQ(config.IsolineWidth, 2.5f);
    EXPECT_FLOAT_EQ(config.IsolineColor.x, 0.25f);
    EXPECT_FLOAT_EQ(config.IsolineColor.y, 0.75f);
    EXPECT_FLOAT_EQ(config.IsolineColor.z, 1.0f);
    EXPECT_FLOAT_EQ(config.IsolineColor.w, 1.0f);
}

// BUG-059 — mirrors the editor Appearance flow: the mesh window's Scalar /
// Isolines preset buttons write a fresh VisualizationLaneOverrides::Surface
// config (Target=Surface), and the UI curvature command authors
// "v:mean_curvature" as a double property. The prior regression only covered
// the base-component float path.
TEST(RuntimeRenderExtraction, MeshSurfaceLaneOverrideDoubleCurvatureReachesPreparedEntityConfig)
{
    namespace GS = ECS::Components::GeometrySources;
    using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
    using Domain = Graphics::Components::VisualizationConfig::Domain;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    auto& registry = scene.Raw();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
        glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    AttachTriangleMeshSources(scene, entity);

    auto& vertexProperties = registry.get<GS::Vertices>(entity).Properties;
    vertexProperties.GetOrAdd<double>("v:mean_curvature", 0.0).Vector() =
        {-0.02, 0.004, 0.05};

    // Exactly what ApplyEditorVisualizationPropertyCommand builds for
    // the Scalar preset via ToVisualizationConfig: a default config with only
    // the command fields assigned (colormap stays the ScalarFieldConfig
    // default), stored on the surface lane.
    Graphics::Components::VisualizationConfig laneConfig{};
    laneConfig.Source = ColorSource::ScalarField;
    laneConfig.ScalarFieldName = "v:mean_curvature";
    laneConfig.ScalarDomain = Domain::Vertex;
    laneConfig.Scalar.AutoRange = true;
    auto& overrides =
        registry.emplace<Graphics::Components::VisualizationLaneOverrides>(entity);
    overrides.Surface = laneConfig;

    const auto stats = fixture.Extract(scene);
    Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);
    ASSERT_EQ(stats.VisualizationScalarPacketCount, 1u);
    ASSERT_EQ(world.Visualization.Scalars.size(), 1u);
    EXPECT_FLOAT_EQ(world.Visualization.Scalars.front().RangeMin,
                    static_cast<float>(-0.02));
    EXPECT_FLOAT_EQ(world.Visualization.Scalars.front().RangeMax,
                    static_cast<float>(0.05));

    fixture.Renderer->PrepareFrame(world);

    const auto sidecar =
        fixture.Extraction.FindRenderableSidecarForTest(StableId(entity));
    ASSERT_TRUE(sidecar.has_value());
    ASSERT_TRUE(sidecar->Instance.IsValid());

    const RHI::GpuEntityConfig config =
        fixture.Renderer->GetGpuWorld().GetEntityConfigForTest(sidecar->Instance);
    EXPECT_EQ(config.ColorSourceMode, 2u);
    EXPECT_NE(config.ScalarBDA, 0u);
    EXPECT_NE(config.ColormapID, 0u);
    EXPECT_EQ(config.ElementCount, 3u);
    EXPECT_FLOAT_EQ(config.ScalarRangeMin, static_cast<float>(-0.02));
    EXPECT_FLOAT_EQ(config.ScalarRangeMax, static_cast<float>(0.05));
}

// BUG-059 — same flow with the Isolines preset (IsolineCount=12, default
// width/color from ScalarFieldConfig).
TEST(RuntimeRenderExtraction, MeshSurfaceLaneOverrideIsolinePresetReachesPreparedEntityConfig)
{
    namespace GS = ECS::Components::GeometrySources;
    using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
    using Domain = Graphics::Components::VisualizationConfig::Domain;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    auto& registry = scene.Raw();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
        glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    AttachTriangleMeshSources(scene, entity);

    auto& vertexProperties = registry.get<GS::Vertices>(entity).Properties;
    vertexProperties.GetOrAdd<double>("v:mean_curvature", 0.0).Vector() =
        {-0.02, 0.004, 0.05};

    Graphics::Components::VisualizationConfig laneConfig{};
    laneConfig.Source = ColorSource::ScalarField;
    laneConfig.ScalarFieldName = "v:mean_curvature";
    laneConfig.ScalarDomain = Domain::Vertex;
    laneConfig.Scalar.AutoRange = true;
    laneConfig.Scalar.Isolines.Num = 12u;
    // UI-032 — explicit highlight isovalues (raw scalar units); one
    // non-finite entry must be skipped fail-closed.
    laneConfig.Scalar.Isolines.Values[0] = 0.0f;
    laneConfig.Scalar.Isolines.Values[1] =
        std::numeric_limits<float>::quiet_NaN();
    laneConfig.Scalar.Isolines.Values[2] = 0.03f;
    laneConfig.Scalar.Isolines.ValueCount = 3u;
    auto& overrides =
        registry.emplace<Graphics::Components::VisualizationLaneOverrides>(entity);
    overrides.Surface = laneConfig;

    const auto stats = fixture.Extract(scene);
    Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});
    ASSERT_EQ(stats.VisualizationScalarPacketCount, 1u);

    fixture.Renderer->PrepareFrame(world);

    const auto sidecar =
        fixture.Extraction.FindRenderableSidecarForTest(StableId(entity));
    ASSERT_TRUE(sidecar.has_value());
    ASSERT_TRUE(sidecar->Instance.IsValid());

    const RHI::GpuEntityConfig config =
        fixture.Renderer->GetGpuWorld().GetEntityConfigForTest(sidecar->Instance);
    EXPECT_EQ(config.ColorSourceMode, 2u);
    EXPECT_NE(config.ScalarBDA, 0u);
    EXPECT_NE(config.ColormapID, 0u);
    EXPECT_EQ(config.ElementCount, 3u);
    EXPECT_FLOAT_EQ(config.ScalarRangeMin, static_cast<float>(-0.02));
    EXPECT_FLOAT_EQ(config.ScalarRangeMax, static_cast<float>(0.05));
    EXPECT_FLOAT_EQ(config.IsolineCount, 12.0f);
    EXPECT_GT(config.IsolineWidth, 0.0f);
    EXPECT_EQ(config.IsoValueCount, 2u);
    EXPECT_FLOAT_EQ(config.IsoValuesA.x, 0.0f);
    EXPECT_FLOAT_EQ(config.IsoValuesA.y, 0.03f);
}

// BUG-059 — multi-frame variant of the editor flow: the mesh renders with
// material shading first, the curvature job applies the property and the
// Appearance preset arrives later, and the visualization must then survive
// subsequent steady-state frames (no packet/config decay).
TEST(RuntimeRenderExtraction, MeshScalarVisualizationSurvivesLatePropertyAndSteadyStateFrames)
{
    namespace GS = ECS::Components::GeometrySources;
    using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
    using Domain = Graphics::Components::VisualizationConfig::Domain;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    auto& registry = scene.Raw();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
        glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    AttachTriangleMeshSources(scene, entity);

    // Frame 1 — plain material rendering, no curvature property yet.
    (void)fixture.Extract(scene);
    {
        Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});
        fixture.Renderer->PrepareFrame(world);
    }

    // Curvature job apply + Appearance "Scalar" preset land between frames.
    auto& vertexProperties = registry.get<GS::Vertices>(entity).Properties;
    vertexProperties.GetOrAdd<double>("v:mean_curvature", 0.0).Vector() =
        {-0.02, 0.004, 0.05};
    registry.emplace_or_replace<ECS::Components::DirtyTags::DirtyVertexAttributes>(entity);

    Graphics::Components::VisualizationConfig laneConfig{};
    laneConfig.Source = ColorSource::ScalarField;
    laneConfig.ScalarFieldName = "v:mean_curvature";
    laneConfig.ScalarDomain = Domain::Vertex;
    laneConfig.Scalar.AutoRange = true;
    laneConfig.Scalar.Isolines.Num = 12u;
    registry.emplace<Graphics::Components::VisualizationLaneOverrides>(entity)
        .Surface = laneConfig;

    const auto sidecar =
        fixture.Extraction.FindRenderableSidecarForTest(StableId(entity));
    ASSERT_TRUE(sidecar.has_value());

    // Frames 2..4 — the prepared config must resolve scalar data on the first
    // frame after the preset and keep resolving on steady-state frames.
    for (int frame = 2; frame <= 4; ++frame)
    {
        (void)fixture.Extract(scene);
        Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});
        fixture.Renderer->PrepareFrame(world);

        const RHI::GpuEntityConfig config =
            fixture.Renderer->GetGpuWorld().GetEntityConfigForTest(sidecar->Instance);
        EXPECT_EQ(config.ColorSourceMode, 2u) << "frame " << frame;
        EXPECT_NE(config.ScalarBDA, 0u) << "frame " << frame;
        EXPECT_NE(config.ColormapID, 0u) << "frame " << frame;
        EXPECT_EQ(config.ElementCount, 3u) << "frame " << frame;
        EXPECT_FLOAT_EQ(config.ScalarRangeMin, static_cast<float>(-0.02))
            << "frame " << frame;
        EXPECT_FLOAT_EQ(config.ScalarRangeMax, static_cast<float>(0.05))
            << "frame " << frame;
        EXPECT_FLOAT_EQ(config.IsolineCount, 12.0f) << "frame " << frame;
    }
}

// BUG-059 — a degenerate manual range on the component keeps the documented
// fail-closed packet behavior (adapters reject it, no scalar buffer), but the
// prepared config must not forward a range whose shader normalization pins
// every fragment to t=0; sync expands it around the requested value.
TEST(RuntimeRenderExtraction, MeshScalarDegenerateManualRangeIsSanitizedInPreparedConfig)
{
    namespace GS = ECS::Components::GeometrySources;
    using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
    using Domain = Graphics::Components::VisualizationConfig::Domain;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto entity = scene.Create();
    auto& registry = scene.Raw();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
        glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    AttachTriangleMeshSources(scene, entity);

    auto& vertexProperties = registry.get<GS::Vertices>(entity).Properties;
    vertexProperties.GetOrAdd<double>("v:mean_curvature", 0.0).Vector() =
        {-0.02, 0.004, 0.05};

    Graphics::Components::VisualizationConfig laneConfig{};
    laneConfig.Source = ColorSource::ScalarField;
    laneConfig.ScalarFieldName = "v:mean_curvature";
    laneConfig.ScalarDomain = Domain::Vertex;
    laneConfig.Scalar.AutoRange = false;
    laneConfig.Scalar.RangeMin = 0.5f;
    laneConfig.Scalar.RangeMax = 0.5f;
    registry.emplace<Graphics::Components::VisualizationLaneOverrides>(entity)
        .Surface = laneConfig;

    const auto stats = fixture.Extract(scene);
    Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});
    EXPECT_EQ(stats.VisualizationRecipeInvalidRangeCount, 1u);
    EXPECT_EQ(world.Visualization.Scalars.size(), 0u);

    fixture.Renderer->PrepareFrame(world);

    const auto sidecar =
        fixture.Extraction.FindRenderableSidecarForTest(StableId(entity));
    ASSERT_TRUE(sidecar.has_value());

    const RHI::GpuEntityConfig config =
        fixture.Renderer->GetGpuWorld().GetEntityConfigForTest(sidecar->Instance);
    EXPECT_EQ(config.ColorSourceMode, 2u);
    EXPECT_LT(config.ScalarRangeMin, config.ScalarRangeMax);
    EXPECT_FLOAT_EQ(config.ScalarRangeMin, 0.0f);
    EXPECT_FLOAT_EQ(config.ScalarRangeMax, 1.0f);
}

TEST(RuntimeRenderExtraction, GraphVisualizationPropertyBuffersUploadFromNodeAndEdgeDomains)
{
    namespace GS = ECS::Components::GeometrySources;
    using Domain = Graphics::Components::VisualizationConfig::Domain;
    using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;

    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto nodeScalarEntity = scene.Create();
    ConfigureGraphScalarVisualization(
        scene, nodeScalarEntity, "node_score", Domain::Vertex);
    auto& nodeScalarProperties =
        scene.Raw().get<GS::Nodes>(nodeScalarEntity).Properties;
    nodeScalarProperties.GetOrAdd<float>("node_score", 0.0f).Vector() =
        {0.1f, 0.5f, 0.9f};

    const auto edgeScalarEntity = scene.Create();
    ConfigureGraphScalarVisualization(
        scene, edgeScalarEntity, "edge_score", Domain::Edge);
    auto& edgeScalarProperties =
        scene.Raw().get<GS::Edges>(edgeScalarEntity).Properties;
    edgeScalarProperties.GetOrAdd<float>("edge_score", 0.0f).Vector() =
        {0.2f, 0.8f};

    const auto nodeColorEntity = scene.Create();
    ConfigureGraphColorVisualization(
        scene, nodeColorEntity, "node_color", ColorSource::PerVertexBuffer);
    auto& nodeColorProperties =
        scene.Raw().get<GS::Nodes>(nodeColorEntity).Properties;
    nodeColorProperties.GetOrAdd<glm::vec4>(
        "node_color",
        glm::vec4{1.0f}).Vector() = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };

    const auto edgeColorEntity = scene.Create();
    ConfigureGraphColorVisualization(
        scene, edgeColorEntity, "edge_color", ColorSource::PerEdgeBuffer);
    auto& edgeColorProperties =
        scene.Raw().get<GS::Edges>(edgeColorEntity).Properties;
    edgeColorProperties.GetOrAdd<glm::vec4>(
        "edge_color",
        glm::vec4{1.0f}).Vector() = {
        {1.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f, 1.0f},
    };

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.GraphGeometryUploads, 4u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 4u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 2u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 2u);
    EXPECT_EQ(stats.VisualizationRecipeMissingSourceCount, 0u);
    EXPECT_EQ(stats.VisualizationRecipeUnsupportedSourceTypeCount, 0u);

    const auto findScalar =
        [&](const std::string& name) -> const Graphics::ScalarAttributePacket*
    {
        for (const Graphics::ScalarAttributePacket& packet :
             world.Visualization.Scalars)
        {
            if (packet.Name == name)
            {
                return &packet;
            }
        }
        return nullptr;
    };
    const auto findColor =
        [&](const std::string& name) -> const Graphics::ColorAttributePacket*
    {
        for (const Graphics::ColorAttributePacket& packet :
             world.Visualization.Colors)
        {
            if (packet.Name == name)
            {
                return &packet;
            }
        }
        return nullptr;
    };

    ASSERT_EQ(world.Visualization.Scalars.size(), 2u);
    const Graphics::ScalarAttributePacket* nodeScalar =
        findScalar("node_score");
    ASSERT_NE(nodeScalar, nullptr);
    EXPECT_EQ(nodeScalar->SourceBufferKey,
              std::to_string(StableId(nodeScalarEntity)) + ":scalar:node_score");
    EXPECT_EQ(nodeScalar->Domain, Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(nodeScalar->ElementCount, 3u);
    EXPECT_NE(nodeScalar->ScalarBufferBDA, 0u);

    const Graphics::ScalarAttributePacket* edgeScalar =
        findScalar("edge_score");
    ASSERT_NE(edgeScalar, nullptr);
    EXPECT_EQ(edgeScalar->SourceBufferKey,
              std::to_string(StableId(edgeScalarEntity)) + ":scalar:edge_score");
    EXPECT_EQ(edgeScalar->Domain, Graphics::VisualizationAttributeDomain::Edge);
    EXPECT_EQ(edgeScalar->ElementCount, 2u);
    EXPECT_NE(edgeScalar->ScalarBufferBDA, 0u);

    ASSERT_EQ(world.Visualization.Colors.size(), 2u);
    const Graphics::ColorAttributePacket* nodeColor =
        findColor("node_color");
    ASSERT_NE(nodeColor, nullptr);
    EXPECT_EQ(nodeColor->SourceBufferKey,
              std::to_string(StableId(nodeColorEntity)) + ":color:node_color");
    EXPECT_EQ(nodeColor->Domain, Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(nodeColor->ElementCount, 3u);
    EXPECT_NE(nodeColor->ColorBufferBDA, 0u);

    const Graphics::ColorAttributePacket* edgeColor =
        findColor("edge_color");
    ASSERT_NE(edgeColor, nullptr);
    EXPECT_EQ(edgeColor->SourceBufferKey,
              std::to_string(StableId(edgeColorEntity)) + ":color:edge_color");
    EXPECT_EQ(edgeColor->Domain, Graphics::VisualizationAttributeDomain::Edge);
    EXPECT_EQ(edgeColor->ElementCount, 2u);
    EXPECT_NE(edgeColor->ColorBufferBDA, 0u);

    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 4u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 4u);
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 4u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 4u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
    EXPECT_TRUE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, GeometryPresentationPropertyBuffersProjectToVisualizationRecipes)
{
    namespace GS = ECS::Components::GeometrySources;
    using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
    using Domain = Graphics::Components::VisualizationConfig::Domain;

    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix =
        glm::mat4{1.0f};
    registry.emplace<Graphics::Components::RenderEdges>(entity);
    registry.emplace<Graphics::Components::RenderPoints>(entity);
    AttachLineGraphSources(scene, entity);

    registry.get<GS::Nodes>(entity)
        .Properties.GetOrAdd<float>("node_heat", 0.0f).Vector() =
        {0.1f, 0.5f, 0.9f};
    registry.get<GS::Edges>(entity)
        .Properties.GetOrAdd<glm::vec4>(
            "edge_color", glm::vec4{1.0f}).Vector() = {
        {1.0f, 0.25f, 0.0f, 1.0f},
        {0.0f, 0.5f, 1.0f, 1.0f},
    };

    auto& config =
        registry.emplace<Graphics::Components::VisualizationConfig>(entity);
    config.Source = ColorSource::ScalarField;
    config.ScalarFieldName = "node_heat";
    config.ScalarDomain = Domain::Vertex;
    config.Scalar.AutoRange = false;
    config.Scalar.RangeMin = 0.0f;
    config.Scalar.RangeMax = 1.0f;
    config.Scalar.Map = Graphics::Colormap::Type::Inferno;

    registry.emplace<Runtime::GeometryPresentationRecipe>(
        entity,
        Runtime::GeometryPresentationRecipe{
            .Shape = Runtime::GeometryPresentationShape::Graph,
            .Lanes = {
                Runtime::GeometryPresentationLaneRecipe{
                    .Lane = Runtime::GeometryRenderLane::Points,
                    .PresentationKey = "graph.points",
                },
                Runtime::GeometryPresentationLaneRecipe{
                    .Lane = Runtime::GeometryRenderLane::Edges,
                    .PresentationKey = "graph.edges",
                },
            },
            .Presentations = {
                Runtime::GeometryPresentationBindingRecipe{
                    .Key = "graph.points",
                    .Kind = Runtime::GeometryPresentationKind::PointPresentation,
                    .Slots = {Runtime::GeometryPresentationSlotRecipe{
                        .Semantic = Runtime::GeometryPresentationSlotSemantic::PointScalarField,
                        .SourceKind = Runtime::GeometryPresentationSourceKind::PropertyBuffer,
                        .Property = Runtime::GeometryPropertyRef{
                            .Domain = Runtime::GeometryElementDomain::GraphNode,
                            .Name = "node_heat",
                            .ValueKind = Geometry::PropertyValueKind::Float,
                        },
                    }},
                },
                Runtime::GeometryPresentationBindingRecipe{
                    .Key = "graph.edges",
                    .Kind = Runtime::GeometryPresentationKind::LinePresentation,
                    .Slots = {Runtime::GeometryPresentationSlotRecipe{
                        .Semantic = Runtime::GeometryPresentationSlotSemantic::LineColor,
                        .SourceKind = Runtime::GeometryPresentationSourceKind::PropertyBuffer,
                        .Property = Runtime::GeometryPropertyRef{
                            .Domain = Runtime::GeometryElementDomain::GraphEdge,
                            .Name = "edge_color",
                            .ValueKind = Geometry::PropertyValueKind::Vec4,
                        },
                    }},
                },
            },
        });

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.GeometryPresentationEntityCount, 1u);
    EXPECT_EQ(stats.GeometryPresentationLaneCount, 2u);
    EXPECT_EQ(stats.GeometryPresentationSlotCount, 2u);
    EXPECT_EQ(stats.GeometryPresentationPropertyBufferReadyCount, 2u);
    EXPECT_EQ(stats.VisualizationRecipeEncodeCount, 2u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 2u);
    EXPECT_EQ(stats.VisualizationScalarPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 1u);

    ASSERT_EQ(world.Visualization.Scalars.size(), 1u)
        << "presentation must suppress the duplicate config-derived scalar";
    const Graphics::ScalarAttributePacket& scalar =
        world.Visualization.Scalars.front();
    EXPECT_EQ(scalar.Name, "node_heat");
    EXPECT_EQ(scalar.Domain, Graphics::VisualizationAttributeDomain::Vertex);
    EXPECT_EQ(scalar.ElementCount, 3u);
    EXPECT_EQ(scalar.SourceBufferKey,
              std::to_string(StableId(entity)) +
                  ":presentation.Points:node_heat");
    EXPECT_FLOAT_EQ(scalar.RangeMin, 0.0f);
    EXPECT_FLOAT_EQ(scalar.RangeMax, 1.0f);
    EXPECT_EQ(scalar.Colormap, Graphics::Colormap::Type::Inferno);

    ASSERT_EQ(world.Visualization.Colors.size(), 1u);
    const Graphics::ColorAttributePacket& color =
        world.Visualization.Colors.front();
    EXPECT_EQ(color.Name, "edge_color");
    EXPECT_EQ(color.Domain, Graphics::VisualizationAttributeDomain::Edge);
    EXPECT_EQ(color.ElementCount, 2u);
    EXPECT_EQ(color.SourceBufferKey,
              std::to_string(StableId(entity)) +
                  ":presentation.Edges:edge_color");

    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 2u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 2u);
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 2u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 2u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
}

TEST(RuntimeRenderExtraction, MeshVertexColorDirtyChannelPartiallyUploadsStructuralColorStream)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    AttachTriangleMeshSources(scene, entity);

    auto stats = fixture.Extract(scene);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryReuploads, 0u);
    fixture.Renderer->GetGpuWorld().SyncFrame();

    const auto firstSidecar =
        fixture.Extraction.FindRenderableSidecarForTest(StableId(entity));
    ASSERT_TRUE(firstSidecar.has_value());
    ASSERT_TRUE(firstSidecar->HasMeshResidency);
    const Graphics::GpuGeometryHandle firstHandle = firstSidecar->MeshGeometry;
    const std::uint32_t firstSlot = firstSidecar->GeometrySlot;

    const RHI::BufferHandle geometryBuffer =
        fixture.Renderer->GetGpuWorld().GetGeometryRecordBuffer();
    const std::uint64_t firstColorBda =
        FindLatestGeometryColorBda(fixture.Device, geometryBuffer, firstSidecar->GeometrySlot);
    EXPECT_NE(firstColorBda, 0u);

    auto& vertices =
        registry.get<ECS::Components::GeometrySources::Vertices>(entity);
    auto colors = vertices.Properties.Get<glm::vec4>("v:color");
    ASSERT_TRUE(colors.IsValid());
    colors.Vector() = {
        {0.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f},
    };
    ECS::Components::DirtyTags::MarkVertexColorsDirty(registry, entity);
    fixture.Device.BufferWrites.clear();

    stats = fixture.Extract(scene);
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryReuploads, 1u);
    EXPECT_EQ(stats.MeshGeometryPartialUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryReleases, 0u);
    EXPECT_FALSE(registry.any_of<ECS::Components::DirtyTags::DirtyVertexColors>(entity));
    const RHI::BufferHandle managedVertexBuffer =
        fixture.Renderer->GetGpuWorld().GetManagedVertexBuffer();
    std::size_t managedVertexWriteCount = 0u;
    std::uint64_t managedVertexWriteBytes = 0u;
    for (const auto& write : fixture.Device.BufferWrites)
    {
        if (write.Handle != managedVertexBuffer)
        {
            continue;
        }
        ++managedVertexWriteCount;
        managedVertexWriteBytes += static_cast<std::uint64_t>(write.Data.size());
    }
    EXPECT_EQ(managedVertexWriteCount, 1u);
    EXPECT_EQ(managedVertexWriteBytes, 3u * sizeof(std::uint32_t));
    fixture.Renderer->GetGpuWorld().SyncFrame();

    const auto secondSidecar =
        fixture.Extraction.FindRenderableSidecarForTest(StableId(entity));
    ASSERT_TRUE(secondSidecar.has_value());
    ASSERT_TRUE(secondSidecar->HasMeshResidency);
    EXPECT_EQ(secondSidecar->MeshGeometry, firstHandle);
    EXPECT_EQ(secondSidecar->GeometrySlot, firstSlot);
    EXPECT_EQ(secondSidecar->Geometry, secondSidecar->MeshGeometry);
    const std::uint64_t secondColorBda =
        FindLatestGeometryColorBda(fixture.Device, geometryBuffer, secondSidecar->GeometrySlot);
    EXPECT_EQ(secondColorBda, 0u)
        << "partial color updates keep the geometry record BDA stable";
    EXPECT_NE(firstColorBda, 0u);
}

TEST(RuntimeRenderExtraction, MeshColorVisualizationPropertyBufferFailsClosed)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto missing = scene.Create();
    ConfigureMeshColorVisualization(scene, missing, "missing:color");

    const auto wrongType = scene.Create();
    ConfigureMeshColorVisualization(scene, wrongType, "v:bad_color");
    auto& vertices =
        scene.Raw().get<ECS::Components::GeometrySources::Vertices>(wrongType);
    vertices.Properties.GetOrAdd<float>("v:bad_color", 0.0f).Vector() =
        {0.0f, 1.0f, 2.0f};

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.MeshGeometryUploads, 2u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 0u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 0u);
    EXPECT_EQ(stats.VisualizationRecipeMissingSourceCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeUnsupportedSourceTypeCount, 1u);
    EXPECT_TRUE(world.Visualization.Colors.empty());
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 0u);
    EXPECT_EQ(world.Visualization.Diagnostics.InputPacketCount, 0u);
    EXPECT_FALSE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, VisualizationNonScalarRecipesReachRenderWorld)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto colorEntity = scene.Create();
    ConfigureColorBufferVisualization(scene, colorEntity);
    AttachPointColorProperty(scene, colorEntity);

    const auto vectorEntity = scene.Create();
    ConfigureRenderablePoint(scene, vectorEntity);
    AttachPointVectorProperty(scene, vectorEntity);

    const auto isolineEntity = scene.Create();
    ConfigureScalarVisualization(scene, isolineEntity);
    AttachPointScalarProperty(scene, isolineEntity);

    const auto htexEntity = scene.Create();
    ConfigureRenderablePoint(scene, htexEntity);

    const auto fragmentEntity = scene.Create();
    ConfigureRenderablePoint(scene, fragmentEntity);
    AttachPointScalarProperty(scene, fragmentEntity);

    fixture.Extraction.SetVisualizationRecipe(
        StableId(colorEntity),
        Runtime::VisualizationRecipe{.Data = Runtime::ColorVisualizationRecipe{
            .Source = PointProperty(
                "v:kmeans_color", Geometry::PropertyValueKind::Vec4),
            .OutputName = "v:kmeans_color",
            .BufferBDA = 0xC010'0000u,
        }});
    fixture.Extraction.SetVisualizationRecipe(
        StableId(vectorEntity),
        Runtime::VisualizationRecipe{.Data = Runtime::VectorFieldVisualizationRecipe{
            .Source = PointProperty("velocity", Geometry::PropertyValueKind::Vec3),
            .PositionSource = PointProperty(
                std::string{ECS::Components::GeometrySources::PropertyNames::kPosition},
                Geometry::PropertyValueKind::Vec3),
            .OutputName = "velocity_glyphs",
            .PositionBufferBDA = 0xAABB'1000u,
            .VectorBufferBDA = 0xAABB'2000u,
            .Scale = 2.0f,
            .Color = {1.0f, 0.25f, 0.0f, 1.0f},
            .DepthTested = false,
        }});
    fixture.Extraction.SetVisualizationRecipe(
        StableId(isolineEntity),
        Runtime::VisualizationRecipe{.Data = Runtime::IsolineVisualizationRecipe{
            .Source = PointProperty("curvature", Geometry::PropertyValueKind::Float),
            .OutputName = "curvature",
            .BufferSourceKey =
                std::to_string(StableId(isolineEntity)) + ":isoline:curvature",
            .AutoRange = false,
            .RangeMin = -1.0f,
            .RangeMax = 2.0f,
            .IsoValueCount = 3u,
            .LineWidth = 2.5f,
            .Color = {0.2f, 0.8f, 1.0f, 1.0f},
        }});
    fixture.Extraction.SetVisualizationRecipe(
        StableId(htexEntity),
        Runtime::VisualizationRecipe{.Data = Runtime::HtexPreviewVisualizationRecipe{
            .Name = "curvature_uv_bake",
            .PatchCount = 8u,
            .AtlasWidth = 512u,
            .AtlasHeight = 256u,
        }});
    fixture.Extraction.SetVisualizationRecipe(
        StableId(fragmentEntity),
        Runtime::VisualizationRecipe{.Data = Runtime::FragmentBakeVisualizationRecipe{
            .Name = "curvature_uv_bake",
            .Source = PointProperty("curvature", Geometry::PropertyValueKind::Float),
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = true,
            .FaceCount = 24u,
            .AtlasWidth = 512u,
            .AtlasHeight = 256u,
            .TexcoordBufferBDA = 0xFEED'CAFEu,
        }});

    const auto entityCountBeforeExtraction =
        scene.Raw().storage<entt::entity>().size();

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(scene.Raw().storage<entt::entity>().size(),
              entityCountBeforeExtraction);
    EXPECT_EQ(stats.VisualizationRecipeEncodeCount, 5u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 5u);
    EXPECT_EQ(stats.VisualizationColorPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationVectorFieldPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationIsolinePacketCount, 1u);
    EXPECT_EQ(stats.VisualizationHtexAtlasPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationFragmentBakeAtlasPacketCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipeScalarValueScanCount, 6u);

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
    EXPECT_EQ(world.Visualization.Isolines.front().ScalarBufferSourceKey,
              std::to_string(StableId(isolineEntity)) + ":isoline:curvature");
    EXPECT_NE(world.Visualization.Isolines.front().ScalarBufferBDA, 0u);
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
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.InputBufferCount, 1u);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 1u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
    EXPECT_EQ(world.Visualization.OverlaySummary.VectorFieldCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.VectorGlyphCount, 3u);
    EXPECT_EQ(world.Visualization.OverlaySummary.IsolineLayerCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.IsolineValueCount, 3u);
    EXPECT_EQ(world.Visualization.OverlaySummary.HtexAtlasDescriptorCount, 1u);
    EXPECT_EQ(world.Visualization.OverlaySummary.UvBakeAtlasDescriptorCount, 1u);
    EXPECT_TRUE(world.Visualization.HasVisualizationPackets);
}

TEST(RuntimeRenderExtraction, VisualizationNonScalarRecipeFailuresAreCounted)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;

    const auto missingColorSource = scene.Create();
    ConfigureColorBufferVisualization(scene, missingColorSource);

    const auto missingVectorSource = scene.Create();
    ConfigureRenderablePoint(scene, missingVectorSource);
    fixture.Extraction.SetVisualizationRecipe(
        StableId(missingVectorSource),
        Runtime::VisualizationRecipe{.Data = Runtime::VectorFieldVisualizationRecipe{
            .Source = PointProperty("velocity", Geometry::PropertyValueKind::Vec3),
            .PositionSource = PointProperty(
                std::string{ECS::Components::GeometrySources::PropertyNames::kPosition},
                Geometry::PropertyValueKind::Vec3),
            .OutputName = "velocity",
            .PositionBufferBDA = 0x1000u,
            .VectorBufferBDA = 0x2000u,
        }});

    const auto missingTexcoords = scene.Create();
    ConfigureRenderablePoint(scene, missingTexcoords);
    AttachPointScalarProperty(scene, missingTexcoords);
    fixture.Extraction.SetVisualizationRecipe(
        StableId(missingTexcoords),
        Runtime::VisualizationRecipe{.Data = Runtime::FragmentBakeVisualizationRecipe{
            .Name = "curvature_uv_bake",
            .Source = PointProperty("curvature", Geometry::PropertyValueKind::Float),
            .Mapping = Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords,
            .MeshHasTexcoords = false,
            .FaceCount = 24u,
            .AtlasWidth = 512u,
            .AtlasHeight = 256u,
        }});

    const auto stats = fixture.Extract(scene);
    const Graphics::RenderWorld world = fixture.Renderer->ExtractRenderWorld({});

    EXPECT_EQ(stats.VisualizationRecipeEncodeCount, 3u);
    EXPECT_EQ(stats.VisualizationRecipeMissingSourceCount, 2u);
    EXPECT_EQ(stats.VisualizationRecipeMissingTexcoordCount, 1u);
    EXPECT_EQ(stats.VisualizationRecipePacketAppendCount, 0u);
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
