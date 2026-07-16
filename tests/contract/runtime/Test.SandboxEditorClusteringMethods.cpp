// ARCH-006 runtime Sandbox editor ClusteringMethods contract partition.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/entity.hpp>
#include <gtest/gtest.h>
#include <glm/gtc/quaternion.hpp>
#include "ProgressivePoissonReference.hpp"

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Graph.Vertex.Normals;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.KMeans;
import Geometry.PointCloud.Normals;
import Geometry.Properties;
import Geometry.Smoothing;
import Geometry.UvAtlas;

#include "MockRHI.hpp"

namespace Runtime = Extrinsic::Runtime;
namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace Graphics = Extrinsic::Graphics;
namespace Plat = Extrinsic::Platform;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
namespace GN = Geometry::HalfedgeMesh::VertexNormals;
namespace GVN = Geometry::Graph::VertexNormals;
namespace PCN = Geometry::PointCloud::Normals;
namespace Smooth = Geometry::Smoothing;
namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
namespace Tests = Extrinsic::Tests;

namespace
{
constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] bool HasDiagnostic(
        const std::vector<Runtime::SandboxEditorDiagnostic>& diagnostics,
        const Runtime::SandboxEditorDiagnosticCode code)
    {
        for (const Runtime::SandboxEditorDiagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.Code == code)
                return true;
        }
        return false;
    }

[[nodiscard]] std::uint32_t SumCounts(
        const std::vector<std::uint32_t>& counts)
    {
        std::uint32_t sum = 0u;
        for (const std::uint32_t count : counts)
            sum += count;
        return sum;
    }

[[nodiscard]] ECS::EntityHandle MakeSelectable(
        ECS::Scene::Registry& registry,
        std::string name)
    {
        const ECS::EntityHandle entity = registry.Create();
        auto& raw = registry.Raw();
        raw.emplace<ECSC::MetaData>(entity, std::move(name));
        raw.emplace<ECSC::Transform::Component>(entity);
        raw.emplace<ECSC::Transform::WorldMatrix>(entity);
        raw.emplace<Sel::SelectableTag>(entity);
        return entity;
    }

void AddPointCloudSource(ECS::Scene::Registry& registry,
                             const ECS::EntityHandle entity,
                             const std::size_t pointCount)
    {
        auto& vertices = registry.Raw().emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(pointCount);
        registry.Raw().emplace<G::RenderPoints>(entity);
    }

void SetNodePositions(GS::Nodes& nodes,
                          const std::vector<glm::vec3>& positions)
    {
        nodes.Properties.Resize(positions.size());
        auto pos = nodes.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        pos.Vector() = positions;
    }

void SetPositions(GS::Vertices& vertices,
                      const std::vector<glm::vec3>& positions)
    {
        vertices.Properties.Resize(positions.size());
        auto pos = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        pos.Vector() = positions;
    }

void SetTexcoords(GS::Vertices& vertices,
                      const std::vector<glm::vec2>& texcoords)
    {
        auto uv = vertices.Properties.GetOrAdd<glm::vec2>(
            "v:texcoord",
            glm::vec2{0.0f});
        uv.Vector() = texcoords;
    }

    // A small deterministic, asymmetric point lattice — distinct extents per axis
    // give ICP a well-conditioned correspondence problem (UI-029).

[[nodiscard]] std::vector<glm::vec3> MakeRegistrationCloud()
    {
        std::vector<glm::vec3> points{};
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j)
                for (int k = 0; k < 3; ++k)
                    points.emplace_back(static_cast<float>(i) * 0.3f,
                                        static_cast<float>(j) * 0.4f,
                                        static_cast<float>(k) * 0.5f);
        return points;
    }

[[nodiscard]] ECS::EntityHandle MakePointCloudEntity(
        ECS::Scene::Registry& registry,
        std::string name,
        const std::vector<glm::vec3>& positions)
    {
        const ECS::EntityHandle entity =
            MakeSelectable(registry, std::move(name));
        auto& vertices = registry.Raw().emplace<GS::Vertices>(entity);
        SetPositions(vertices, positions);
        registry.Raw().emplace<G::RenderPoints>(entity);
        return entity;
    }

    // An open grid-plane triangle mesh; its outer ring is an open boundary, so
    // texcoord-bearing boundary vertices are UV-seam vertices (UI-028).

void ExpectKMeansVertexProperties(Geometry::PropertySet& properties,
                                      const std::size_t expectedCount,
                                      const bool pointCloudNames)
    {
        const std::string labelName =
            pointCloudNames ? "p:kmeans_label" : "v:kmeans_label";
        const std::string colorName =
            pointCloudNames ? "p:kmeans_color" : "v:kmeans_color";

        auto labels = properties.Get<std::uint32_t>(labelName);
        auto colors = properties.Get<glm::vec4>(colorName);
        ASSERT_TRUE(labels);
        ASSERT_TRUE(colors);
        ASSERT_EQ(labels.Vector().size(), expectedCount);
        ASSERT_EQ(colors.Vector().size(), expectedCount);
        for (std::size_t i = 0u; i < expectedCount; ++i)
        {
            EXPECT_LT(labels.Vector()[i], expectedCount);
            EXPECT_FLOAT_EQ(colors.Vector()[i].w, 1.0f);
        }

        if (pointCloudNames)
        {
            EXPECT_FALSE(properties.Get<float>("v:kmeans_label_f"));
        }
        else
        {
            auto labelFloats = properties.Get<float>("v:kmeans_label_f");
            ASSERT_TRUE(labelFloats);
            ASSERT_EQ(labelFloats.Vector().size(), expectedCount);
            for (std::size_t i = 0u; i < expectedCount; ++i)
            {
                EXPECT_FLOAT_EQ(labelFloats.Vector()[i],
                                static_cast<float>(labels.Vector()[i]));
            }
        }
    }

void SetEdges(GS::Edges& edges,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1)
    {
        edges.Properties.Resize(v0.size());
        auto p0 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV0},
            0u);
        auto p1 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV1},
            0u);
        p0.Vector() = v0;
        p1.Vector() = v1;
    }

void SetHalfedges(GS::Halfedges& halfedges,
                      const std::vector<std::uint32_t>& toVertex,
                      const std::vector<std::uint32_t>& next,
                      const std::vector<std::uint32_t>& face)
    {
        halfedges.Properties.Resize(toVertex.size());
        auto to = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeToVertex},
            kInvalidIndex);
        auto nx = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeNext},
            kInvalidIndex);
        auto fa = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeFace},
            kInvalidIndex);
        to.Vector() = toVertex;
        nx.Vector() = next;
        fa.Vector() = face;
    }

void SetFaces(GS::Faces& faces,
                  const std::vector<std::uint32_t>& faceHalfedge)
    {
        faces.Properties.Resize(faceHalfedge.size());
        auto halfedge = faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kFaceHalfedge},
            kInvalidIndex);
        halfedge.Vector() = faceHalfedge;
    }

void AddTriangleMeshSource(ECS::Scene::Registry& registry,
                               const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices,
                     {
                         {0.0f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f},
                         {0.0f, 1.0f, 0.0f},
                     });
        SetTexcoords(vertices,
                     {
                         {0.0f, 0.0f},
                         {1.0f, 0.0f},
                         {0.0f, 1.0f},
                     });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u, 2u}, {1u, 2u, 0u});
        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        SetHalfedges(halfedges,
                     {1u, 2u, 0u, 0u, 2u, 1u},
                     {1u, 2u, 0u, 5u, 3u, 4u},
                     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        auto& faces = raw.emplace<GS::Faces>(entity);
        SetFaces(faces, {0u});
    }

void AddIcosahedronMeshSource(ECS::Scene::Registry& registry,
                                  const ECS::EntityHandle entity)
    {
        Geometry::HalfedgeMesh::Mesh mesh =
            Geometry::HalfedgeMesh::MakeMeshIcosahedron();
        GS::PopulateFromMesh(registry.Raw(), entity, mesh);
        registry.Raw().emplace_or_replace<G::RenderSurface>(entity);
    }

void AddGraphSource(ECS::Scene::Registry& registry,
                        const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        SetNodePositions(nodes,
                         {
                             {0.0f, 0.0f, 0.0f},
                             {1.0f, 0.0f, 0.0f},
                             {2.0f, 0.0f, 0.0f},
                         });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u}, {1u, 2u});
        raw.emplace<GS::HasGraphTopology>(entity);
        raw.emplace<G::RenderEdges>(entity);
        raw.emplace<G::RenderPoints>(entity);
    }

[[nodiscard]] Runtime::SandboxEditorContext MakeContext(
        ECS::Scene::Registry& registry,
        Runtime::SelectionController& selection,
        const bool imguiAvailable = true,
        const std::optional<Runtime::PrimitiveSelectionResult>* lastPrimitive = nullptr,
        Extrinsic::RHI::IDevice* device = nullptr)
    {
        return Runtime::SandboxEditorContext{
            .Scene = &registry,
            .Selection = &selection,
            .LastRefinedPrimitive = lastPrimitive,
            .Device = device,
            .ImGuiAdapterAvailable = imguiAvailable,
            .AssetImportCommandsAvailable = false,
            .CameraRenderCommandsAvailable = false,
            .VisualizationCommandsAvailable = false,
        };
    }

void AttachDerivedJobCommands(
        Runtime::SandboxEditorContext& context,
        Runtime::DerivedJobRegistry& jobs)
    {
        context.DerivedJobCommands.Submit =
            [&jobs](Runtime::DerivedJobDesc desc)
            {
                return jobs.Submit(std::move(desc));
            };
    }
}
TEST(SandboxEditorUi, KMeansCommandPublishesMeshGraphAndPointCloudProperties)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    const Runtime::SandboxEditorKMeansResult meshResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Domain = Domain::MeshVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 7u,
            });
    ASSERT_TRUE(meshResult.Succeeded());
    EXPECT_EQ(meshResult.Domain, Domain::MeshVertices);
    EXPECT_EQ(meshResult.LabelCount, 3u);
    EXPECT_EQ(meshResult.ClusterCount, 2u);
    EXPECT_EQ(meshResult.RequestedBackend,
              Runtime::SandboxEditorKMeansBackend::CpuReference);
    EXPECT_EQ(meshResult.ActualBackend,
              Runtime::SandboxEditorKMeansBackend::CpuReference);
    EXPECT_EQ(meshResult.RequestedBackendId, "cpu_reference");
    EXPECT_EQ(meshResult.BackendId, "cpu_reference");
    EXPECT_FALSE(meshResult.FellBackToCpu);
    EXPECT_TRUE(meshResult.BackendFallbackReason.empty());
    EXPECT_NE(meshResult.Message.find("requested cpu_reference"),
              std::string::npos);
    EXPECT_EQ(meshResult.Error, Core::ErrorCode::Success);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(mesh).Properties,
        3u,
        false);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    const Runtime::SandboxEditorKMeansResult graphResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(graph),
                .Domain = Domain::GraphVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 11u,
                .UseHierarchicalInitialization = false,
            });
    ASSERT_TRUE(graphResult.Succeeded());
    EXPECT_EQ(graphResult.Domain, Domain::GraphVertices);
    EXPECT_EQ(graphResult.LabelCount, 3u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Nodes>(graph).Properties,
        3u,
        false);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(graph));

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });
    const Runtime::SandboxEditorKMeansResult cloudResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
            });
    ASSERT_TRUE(cloudResult.Succeeded());
    EXPECT_EQ(cloudResult.Domain, Domain::PointCloudPoints);
    EXPECT_EQ(cloudResult.LabelCount, 4u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(cloud).Properties,
        4u,
        true);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    context.LastKMeansResult = &cloudResult;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(model.Processing.LastKMeansResult.has_value());
    EXPECT_TRUE(model.Processing.LastKMeansResult->Succeeded());
    EXPECT_EQ(model.Processing.LastKMeansResult->LabelCount, 4u);
}
TEST(SandboxEditorUi, KMeansVulkanRequestFallsBackToCpuReference)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Tests::MockDevice device;
    device.Operational = false;
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::VulkanCompute,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorKMeansBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorKMeansBackend::CpuReference);
    EXPECT_EQ(result.RequestedBackendId, "gpu_vulkan_compute");
    EXPECT_EQ(result.BackendId, "cpu_reference");
    EXPECT_TRUE(result.FellBackToCpu);
    EXPECT_NE(result.BackendFallbackReason.find("not operational"),
              std::string::npos);
    EXPECT_NE(result.Message.find("requested gpu_vulkan_compute"),
              std::string::npos);
    EXPECT_NE(result.Message.find("actual cpu_reference"), std::string::npos);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(cloud).Properties,
        4u,
        true);
}
TEST(SandboxEditorUi, KMeansVulkanRequestQueuesGpuJobWhenSurfaceAccepts)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    std::optional<Runtime::RuntimeKMeansGpuJobRequest> submitted{};
    context.KMeansGpuCommands.Submit =
        [&submitted](Runtime::RuntimeKMeansGpuJobRequest request)
        {
            request.Sequence = 77u;
            submitted = request;
            return Runtime::RuntimeKMeansGpuJobSubmission{
                .Status = Runtime::RuntimeKMeansGpuJobStatus::Accepted,
                .Sequence = request.Sequence,
            };
        };
    context.KMeansGpuCommands.ConsumeCompleted =
        []() -> std::optional<Runtime::RuntimeKMeansGpuJobResult>
        {
            return std::nullopt;
        };

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::VulkanCompute,
            });

    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(submitted->StableEntityId,
              Runtime::SelectionController::ToStableEntityId(cloud));
    EXPECT_EQ(submitted->DomainTag,
              static_cast<std::uint32_t>(Domain::PointCloudPoints));
    EXPECT_EQ(submitted->Points.size(), 4u);
    EXPECT_EQ(submitted->Params.Compute, Geometry::KMeans::Backend::GPU);
    EXPECT_EQ(submitted->Params.ClusterCount, 2u);
    EXPECT_EQ(submitted->Params.MaxIterations, 8u);

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorKMeansBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorKMeansBackend::VulkanCompute);
    EXPECT_EQ(result.RequestedBackendId, "gpu_vulkan_compute");
    EXPECT_EQ(result.BackendId, "gpu_vulkan_compute");
    EXPECT_FALSE(result.FellBackToCpu);
    EXPECT_TRUE(result.BackendFallbackReason.empty());
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));

    context.LastKMeansResult = &result;
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(model.Processing.LastKMeansResult.has_value());
    EXPECT_EQ(model.Processing.LastKMeansResult->Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_FALSE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed));
}
TEST(SandboxEditorUi, KMeansCpuRequestQueuesDerivedJobAndPublishesOnApply)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    std::optional<Runtime::SandboxEditorKMeansResult> completedResult{};
    context.MethodResultSinks.KMeans =
        [&completedResult](Runtime::SandboxEditorKMeansResult result)
        {
            completedResult = std::move(result);
        };
    context.DerivedJobs = nullptr;

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::CpuReference,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.KMeans.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));

    Runtime::DerivedJobQueueSnapshot ready = jobs.SnapshotAll();
    ASSERT_EQ(ready.Entries.size(), 1u);
    EXPECT_EQ(ready.Entries[0].Status, Runtime::DerivedJobStatus::Applying);

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->LabelCount, 4u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(cloud).Properties,
        4u,
        true);
}
TEST(SandboxEditorUi, KMeansCpuDuplicateSubmitUsesExistingActiveJob)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansCommand command{
        .StableEntityId =
            Runtime::SelectionController::ToStableEntityId(cloud),
        .Domain = Domain::PointCloudPoints,
        .ClusterCount = 2u,
        .MaxIterations = 8u,
        .Seed = 13u,
        .Backend = Runtime::SandboxEditorKMeansBackend::CpuReference,
    };

    const Runtime::SandboxEditorKMeansResult first =
        Runtime::ApplySandboxEditorKMeansCommand(context, command);
    ASSERT_EQ(first.Status, Runtime::SandboxEditorCommandStatus::Pending);

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    context.DerivedJobs = &queued;

    const Runtime::SandboxEditorKMeansResult duplicate =
        Runtime::ApplySandboxEditorKMeansCommand(context, command);
    EXPECT_EQ(duplicate.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(duplicate.Message.find("already has an active"),
              std::string::npos);
    EXPECT_NE(duplicate.Message.find("job 0:1"), std::string::npos);
    EXPECT_EQ(jobs.SnapshotAll().Entries.size(), 1u);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot complete = jobs.SnapshotAll();
    ASSERT_EQ(complete.Entries.size(), 1u);
    EXPECT_EQ(complete.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    context.DerivedJobs = &complete;

    const Runtime::SandboxEditorKMeansResult rerun =
        Runtime::ApplySandboxEditorKMeansCommand(context, command);
    EXPECT_EQ(rerun.Status, Runtime::SandboxEditorCommandStatus::Pending);
    Runtime::DerivedJobQueueSnapshot afterRerun = jobs.SnapshotAll();
    ASSERT_EQ(afterRerun.Entries.size(), 2u);
    EXPECT_EQ(afterRerun.Entries[1].Status, Runtime::DerivedJobStatus::Queued);
}
TEST(SandboxEditorUi, KMeansCpuDerivedJobDiscardsStaleTargetBeforeApply)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    bool completedSinkCalled = false;
    context.MethodResultSinks.KMeans =
        [&completedSinkCalled](Runtime::SandboxEditorKMeansResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::CpuReference,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {10.0f, 0.0f, 0.0f},
                     {11.0f, 0.0f, 0.0f},
                     {12.0f, 0.0f, 0.0f},
                     {13.0f, 0.0f, 0.0f},
                 });

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));
}
TEST(SandboxEditorUi, ProgressivePoissonCommandPublishesPointPropertiesAndVisualization)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 8u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 3u,
        .GridWidth = 2u,
        .MaxLevels = 4u,
        .HashLoadFactor = 0.5f,
        .RadiusAlpha = -1.0f,
        .RandomizeGridOrigin = false,
        .GridOriginSeed = 17u,
        .ShuffleWithinLevels = true,
        .ShuffleSeed = 23u,
        .PrefixCount = 3u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Phase,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.InputCount, positions.size());
    EXPECT_GT(result.AcceptedCount, 0u);
    EXPECT_EQ(result.PrefixCount, std::min(3u, result.AcceptedCount));
    EXPECT_EQ(result.Channel,
              Runtime::SandboxEditorProgressivePoissonChannel::Phase);
    EXPECT_STREQ(
        Runtime::DebugNameForSandboxEditorProgressivePoissonChannel(
            result.Channel),
        "Phase");
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_EQ(result.RequestedBackendId, PPR::kBackendId);
    EXPECT_FALSE(result.FellBackToCpu);
    EXPECT_TRUE(result.BackendFallbackReason.empty());
    EXPECT_EQ(result.LevelAcceptedCounts.size(), result.LevelCount);
    EXPECT_EQ(SumCounts(result.LevelAcceptedCounts), result.AcceptedCount);
    EXPECT_NE(result.Message.find(PPR::kBackendId), std::string::npos);

    Geometry::PropertySet& properties =
        registry.Raw().get<GS::Vertices>(cloud).Properties;
    const auto levels = properties.Get<float>("p:poisson_level");
    const auto phases = properties.Get<float>("p:poisson_phase");
    const auto splats = properties.Get<float>("p:poisson_splat_radius");
    const auto visible = properties.Get<float>("p:poisson_prefix_visible");
    ASSERT_TRUE(levels);
    ASSERT_TRUE(phases);
    ASSERT_TRUE(splats);
    ASSERT_TRUE(visible);
    ASSERT_EQ(levels.Vector().size(), positions.size());
    ASSERT_EQ(phases.Vector().size(), positions.size());
    ASSERT_EQ(splats.Vector().size(), positions.size());
    ASSERT_EQ(visible.Vector().size(), positions.size());

    std::size_t acceptedFromProperties = 0u;
    std::size_t visibleFromProperties = 0u;
    for (std::size_t i = 0u; i < positions.size(); ++i)
    {
        if (levels.Vector()[i] >= 0.0f)
        {
            ++acceptedFromProperties;
            EXPECT_GE(phases.Vector()[i], 0.0f);
            EXPECT_LT(phases.Vector()[i], 8.0f);
            EXPECT_GT(splats.Vector()[i], 0.0f);
        }
        if (visible.Vector()[i] > 0.5f)
            ++visibleFromProperties;
    }
    EXPECT_EQ(acceptedFromProperties, result.AcceptedCount);
    EXPECT_EQ(visibleFromProperties, result.PrefixCount);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<G::RenderPoints>(cloud));

    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(cloud));
    const G::VisualizationConfig& vis =
        registry.Raw().get<G::VisualizationConfig>(cloud);
    EXPECT_EQ(vis.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(vis.ScalarDomain, G::VisualizationConfig::Domain::Vertex);
    EXPECT_EQ(vis.ScalarFieldName, "p:poisson_phase");

    context.LastProgressivePoissonResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(model.Processing.PointCloudProgressivePoissonAvailable);
    ASSERT_TRUE(model.Processing.LastProgressivePoissonResult.has_value());
    EXPECT_TRUE(model.Processing.LastProgressivePoissonResult->Succeeded());
    EXPECT_EQ(model.Processing.LastProgressivePoissonResult->AcceptedCount,
              result.AcceptedCount);
}
TEST(SandboxEditorUi, ProgressivePoissonVulkanRequestFallsBackToCpuReference)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Tests::MockDevice device;
    device.Operational = false;
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    Runtime::SandboxEditorProgressivePoissonConfig config{};
    config.Dimension = 2u;
    config.GridWidth = 3u;
    config.MaxLevels = 5u;
    config.HashLoadFactor = 0.75f;
    config.RadiusAlpha = 0.4f;
    config.RandomizeGridOrigin = false;
    config.ShuffleWithinLevels = false;
    config.PrefixCount = 3u;
    config.Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level;
    config.Backend =
        Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute;

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_EQ(result.RequestedBackendId, "gpu_vulkan_compute");
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_TRUE(result.FellBackToCpu);
    EXPECT_NE(result.BackendFallbackReason.find("not operational"),
              std::string::npos);
    EXPECT_NE(result.Message.find("requested gpu_vulkan_compute"),
              std::string::npos);
    EXPECT_NE(result.Message.find("actual cpu_reference"),
              std::string::npos);

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.PrefixCount, std::min(3u, direct.Diag.AcceptedCount));
}
TEST(SandboxEditorUi, ProgressivePoissonCommandMatchesDirectMethodConfig)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = true,
        .GridOriginSeed = 19u,
        .ShuffleWithinLevels = false,
        .ShuffleSeed = 29u,
        .PrefixCount = 0u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);

    EXPECT_EQ(result.InputCount, direct.Diag.InputCount);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.PrefixCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.LevelCount, direct.Diag.LevelCounts.size());
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_FLOAT_EQ(result.BaseRadius, direct.BaseRadius);
    EXPECT_FLOAT_EQ(result.UsedAlpha, direct.Diag.UsedAlpha);

    Geometry::PropertySet& properties =
        registry.Raw().get<GS::Vertices>(cloud).Properties;
    const auto levels = properties.Get<float>("p:poisson_level");
    ASSERT_TRUE(levels);
    ASSERT_EQ(levels.Vector().size(), positions.size());

    std::vector<float> expectedLevels(positions.size(), -1.0f);
    for (std::size_t level = 0u; level + 1u < direct.LevelOffsets.size(); ++level)
    {
        for (std::uint32_t rank = direct.LevelOffsets[level];
             rank < direct.LevelOffsets[level + 1u];
             ++rank)
        {
            expectedLevels[direct.Order[rank]] = static_cast<float>(level);
        }
    }
    EXPECT_EQ(levels.Vector(), expectedLevels);
}
TEST(SandboxEditorUi, ProgressivePoissonCpuRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    std::optional<Runtime::SandboxEditorProgressivePoissonResult>
        completedResult{};
    context.MethodResultSinks.ProgressivePoisson =
        [&completedResult](Runtime::SandboxEditorProgressivePoissonResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = true,
        .GridOriginSeed = 19u,
        .ShuffleWithinLevels = false,
        .ShuffleSeed = 29u,
        .PrefixCount = 3u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Phase,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<float>("p:poisson_phase"));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.ProgressivePoisson.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<float>("p:poisson_phase"));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputCount, positions.size());

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(completedResult->AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(completedResult->LevelAcceptedCounts, direct.Diag.LevelCounts);

    const auto phases =
        registry.Raw()
            .get<GS::Vertices>(cloud)
            .Properties.Get<float>("p:poisson_phase");
    ASSERT_TRUE(phases);
    EXPECT_EQ(phases.Vector().size(), positions.size());
}
TEST(SandboxEditorUi, ProgressivePoissonCpuDerivedJobDiscardsStalePointCloudBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    bool completedSinkCalled = false;
    context.MethodResultSinks.ProgressivePoisson =
        [&completedSinkCalled](Runtime::SandboxEditorProgressivePoissonResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.25f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = Runtime::SandboxEditorProgressivePoissonConfig{
                    .Dimension = 2u,
                    .GridWidth = 3u,
                    .MaxLevels = 5u,
                    .HashLoadFactor = 0.75f,
                    .RadiusAlpha = 0.4f,
                    .RandomizeGridOrigin = false,
                    .ShuffleWithinLevels = false,
                    .PrefixCount = 0u,
                },
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {10.0f, 0.0f, 0.0f},
                     {11.0f, 0.0f, 0.0f},
                     {12.0f, 0.0f, 0.0f},
                     {13.0f, 0.0f, 0.0f},
                 });

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<float>("p:poisson_level"));
}
TEST(SandboxEditorUi, ProgressivePoissonMeshCpuRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    std::optional<Runtime::SandboxEditorProgressivePoissonResult>
        completedResult{};
    context.MethodResultSinks.ProgressivePoisson =
        [&completedResult](Runtime::SandboxEditorProgressivePoissonResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "PoissonMesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderSurface>(mesh);

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = false,
        .GridOriginSeed = 31u,
        .ShuffleWithinLevels = true,
        .ShuffleSeed = 37u,
        .PrefixCount = 7u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level,
        .MeshSurfaceSampleCount = 24u,
        .MeshSurfaceSampleSeed = 41u,
        .MeshSurfaceMinTriangleArea = 1.0e-14,
        .MeshSurfaceInterpolateNormals = true,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Config = config,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_TRUE(result.MeshSurfaceSamplingUsed);
    EXPECT_TRUE(registry.Raw().all_of<G::RenderSurface>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<GS::Faces>(mesh));

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_TRUE(registry.Raw().all_of<G::RenderSurface>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<GS::Faces>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_TRUE(completedResult->MeshSurfaceSamplingUsed);
    EXPECT_EQ(completedResult->MeshSurfaceSampleCount, 24u);
    EXPECT_EQ(completedResult->MeshSurfaceAcceptedTriangleCount, 1u);

    const GS::ConstSourceView view = GS::BuildConstView(registry.Raw(), mesh);
    const GS::SourceAvailability availability =
        GS::BuildSourceAvailability(view);
    EXPECT_EQ(availability.ProvenanceDomain, GS::Domain::PointCloud);
    EXPECT_FALSE(registry.Raw().all_of<G::RenderSurface>(mesh));
    ASSERT_NE(view.VertexSource, nullptr);
    EXPECT_EQ(view.FaceSource, nullptr);

    const auto positions =
        view.VertexSource->Properties.Get<glm::vec3>(PN::kPosition);
    const auto levels =
        view.VertexSource->Properties.Get<float>("p:poisson_level");
    ASSERT_TRUE(positions);
    ASSERT_TRUE(levels);
    EXPECT_EQ(positions.Vector().size(), 24u);
    EXPECT_EQ(levels.Vector().size(), 24u);
}
TEST(SandboxEditorUi, ProgressivePoissonConfigCommandRoutesThroughConfigFacade)
{
    Runtime::RuntimeEngineConfigControlState controlState{};
    controlState.ActiveConfig = Core::Config::EngineConfig{};

    Runtime::SandboxEditorContext configContext{};
    configContext.EngineConfigControlState = &controlState;
    configContext.EngineConfigCommandsAvailable = true;

    int previewCalls = 0;
    int applyCalls = 0;
    configContext.PreviewEngineConfigDocument =
        [&](const std::string& document, const std::string& sourceId)
    {
        ++previewCalls;
        return Core::Config::PreviewEngineConfig(
            document,
            controlState.ActiveConfig,
            Core::Config::EngineConfigParseOptions{.SourceId = sourceId});
    };
    configContext.ApplyEngineConfigHotSubset =
        [&](const Core::Config::EngineConfigLoadResult& loadResult)
    {
        ++applyCalls;
        Runtime::RuntimeEngineConfigApplyResult apply{
            .Source = Runtime::RuntimeConfigControlSource::Editor,
            .LoadResult = loadResult,
        };
        if (!Core::Config::IsConfigUsable(loadResult))
        {
            apply.Status =
                Runtime::RuntimeEngineConfigApplyStatus::Rejected;
            return apply;
        }
        controlState.ActiveConfig = loadResult.Preview.Config;
        apply.Status = Runtime::RuntimeEngineConfigApplyStatus::Applied;
        apply.EngineConfigApplied = true;
        apply.SandboxProgressivePoissonChanged = true;
        return apply;
    };

    Core::Config::ProgressivePoissonPlaygroundConfig config{};
    config.Dimension = 2u;
    config.GridWidth = 3u;
    config.MaxLevels = 5u;
    config.HashLoadFactor = 0.75;
    config.RadiusAlpha = 0.4;
    config.RandomizeGridOrigin = true;
    config.GridOriginSeed = 19u;
    config.ShuffleWithinLevels = false;
    config.ShuffleSeed = 29u;
    config.PrefixCount = 3u;
    config.Channel = Core::Config::ProgressivePoissonPlaygroundChannel::Phase;
    config.Backend = Core::Config::ProgressivePoissonPlaygroundBackend::VulkanCompute;
    config.AutoRunOnEdit = true;
    config.DebounceSeconds = 0.2;

    const Runtime::SandboxEditorProgressivePoissonConfigResult configResult =
        Runtime::ApplySandboxEditorProgressivePoissonConfigCommand(
            configContext,
            Runtime::SandboxEditorProgressivePoissonConfigCommand{
                .Config = Runtime::MakeSandboxEditorProgressivePoissonConfig(
                    config),
                .SourceId = "test-progressive-poisson-config",
            });

    ASSERT_TRUE(configResult.Succeeded()) << configResult.Message;
    EXPECT_EQ(previewCalls, 1);
    EXPECT_EQ(applyCalls, 1);
    EXPECT_EQ(controlState.ActiveConfig.Sandbox.ProgressivePoisson.GridWidth,
              3u);
    EXPECT_EQ(controlState.ActiveConfig.Sandbox.ProgressivePoisson.Channel,
              Core::Config::ProgressivePoissonPlaygroundChannel::Phase);
    EXPECT_EQ(controlState.ActiveConfig.Sandbox.ProgressivePoisson.Backend,
              Core::Config::ProgressivePoissonPlaygroundBackend::VulkanCompute);
    EXPECT_TRUE(
        controlState.ActiveConfig.Sandbox.ProgressivePoisson.AutoRunOnEdit);
    EXPECT_DOUBLE_EQ(
        controlState.ActiveConfig.Sandbox.ProgressivePoisson.DebounceSeconds,
        0.2);

    const std::optional<Runtime::SandboxEditorProgressivePoissonConfig>
        activeConfig =
            Runtime::GetSandboxEditorProgressivePoissonConfig(configContext);
    ASSERT_TRUE(activeConfig.has_value());
    EXPECT_TRUE(activeConfig->AutoRunOnEdit);
    EXPECT_DOUBLE_EQ(activeConfig->DebounceSeconds, 0.2);

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext commandContext =
        MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorProgressivePoissonConfig runtimeConfig =
        Runtime::MakeSandboxEditorProgressivePoissonConfig(
            controlState.ActiveConfig.Sandbox.ProgressivePoisson);
    EXPECT_EQ(runtimeConfig.Backend,
              Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute);
    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            commandContext,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = runtimeConfig,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    PPR::Config directConfig{};
    directConfig.Dimension = runtimeConfig.Dimension;
    directConfig.GridWidth = runtimeConfig.GridWidth;
    directConfig.MaxLevels = runtimeConfig.MaxLevels;
    directConfig.HashLoadFactor = runtimeConfig.HashLoadFactor;
    directConfig.RadiusAlpha = runtimeConfig.RadiusAlpha;
    directConfig.RandomizeGridOrigin = runtimeConfig.RandomizeGridOrigin;
    directConfig.GridOriginSeed = runtimeConfig.GridOriginSeed;
    directConfig.ShuffleWithinLevels = runtimeConfig.ShuffleWithinLevels;
    directConfig.ShuffleSeed = runtimeConfig.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.PrefixCount, std::min(3u, direct.Diag.AcceptedCount));
    EXPECT_EQ(result.LevelCount, direct.Diag.LevelCounts.size());
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_TRUE(result.FellBackToCpu);
    EXPECT_NE(result.BackendFallbackReason.find("no RHI device"),
              std::string::npos);
    EXPECT_FLOAT_EQ(result.BaseRadius, direct.BaseRadius);

    Geometry::PropertySet& properties =
        registry.Raw().get<GS::Vertices>(cloud).Properties;
    const auto phases = properties.Get<float>("p:poisson_phase");
    const auto visible = properties.Get<float>("p:poisson_prefix_visible");
    ASSERT_TRUE(phases);
    ASSERT_TRUE(visible);
    ASSERT_EQ(phases.Vector().size(), positions.size());
    ASSERT_EQ(visible.Vector().size(), positions.size());
    std::size_t visibleCount = 0u;
    for (float value : visible.Vector())
    {
        if (value > 0.5f)
        {
            ++visibleCount;
        }
    }
    EXPECT_EQ(visibleCount, result.PrefixCount);
}
TEST(SandboxEditorUi, ProgressivePoissonCommandSamplesMeshSurfaceToPointCloud)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "PoissonMesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderSurface>(mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = false,
        .GridOriginSeed = 31u,
        .ShuffleWithinLevels = true,
        .ShuffleSeed = 37u,
        .PrefixCount = 7u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level,
        .MeshSurfaceSampleCount = 32u,
        .MeshSurfaceSampleSeed = 41u,
        .MeshSurfaceMinTriangleArea = 1.0e-14,
        .MeshSurfaceInterpolateNormals = true,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Config = config,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(result.MeshSurfaceSamplingUsed);
    EXPECT_EQ(result.MeshSurfaceSampleCount, 32u);
    EXPECT_EQ(result.MeshSurfaceTotalFaceCount, 1u);
    EXPECT_EQ(result.MeshSurfaceAcceptedTriangleCount, 1u);
    EXPECT_EQ(result.MeshSurfaceRejectedFaceCount, 0u);
    EXPECT_GT(result.MeshSurfaceArea, 0.0);
    EXPECT_EQ(result.InputCount, 32u);
    EXPECT_GT(result.AcceptedCount, 0u);
    EXPECT_EQ(result.PrefixCount, std::min(7u, result.AcceptedCount));

    const GS::ConstSourceView view =
        GS::BuildConstView(registry.Raw(), mesh);
    const GS::SourceAvailability availability =
        GS::BuildSourceAvailability(view);
    EXPECT_EQ(availability.ProvenanceDomain, GS::Domain::PointCloud);
    ASSERT_NE(view.VertexSource, nullptr);
    EXPECT_EQ(view.HalfedgeSource, nullptr);
    EXPECT_EQ(view.FaceSource, nullptr);

    const Geometry::ConstPropertySet properties{
        view.VertexSource->Properties};
    const auto positions = properties.Get<glm::vec3>(PN::kPosition);
    const auto normals = properties.Get<glm::vec3>(PN::kNormal);
    const auto levels = properties.Get<float>("p:poisson_level");
    const auto phases = properties.Get<float>("p:poisson_phase");
    const auto splats = properties.Get<float>("p:poisson_splat_radius");
    const auto visible = properties.Get<float>("p:poisson_prefix_visible");
    ASSERT_TRUE(positions);
    ASSERT_TRUE(normals);
    ASSERT_TRUE(levels);
    ASSERT_TRUE(phases);
    ASSERT_TRUE(splats);
    ASSERT_TRUE(visible);
    ASSERT_EQ(positions.Vector().size(), 32u);
    EXPECT_EQ(normals.Vector().size(), 32u);
    EXPECT_EQ(levels.Vector().size(), 32u);
    EXPECT_EQ(phases.Vector().size(), 32u);
    EXPECT_EQ(splats.Vector().size(), 32u);
    EXPECT_EQ(visible.Vector().size(), 32u);

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct =
        PPR::Compute(positions.Vector(), directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.LevelCount, direct.Diag.LevelCounts.size());
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_FLOAT_EQ(result.BaseRadius, direct.BaseRadius);

    EXPECT_FALSE(registry.Raw().all_of<G::RenderSurface>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<G::RenderPoints>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
    const G::VisualizationConfig& vis =
        registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(vis.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(vis.ScalarDomain, G::VisualizationConfig::Domain::Vertex);
    EXPECT_EQ(vis.ScalarFieldName, "p:poisson_level");
}
TEST(SandboxEditorUi, KMeansCommandFailsClosedForInvalidTargetsAndInputs)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorKMeansCommand validShape{
        .StableEntityId = 1u,
        .Domain = Domain::PointCloudPoints,
        .ClusterCount = 2u,
        .MaxIterations = 8u,
    };

    const Runtime::SandboxEditorKMeansResult missingScene =
        Runtime::ApplySandboxEditorKMeansCommand(
            Runtime::SandboxEditorContext{},
            validShape);
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::SandboxEditorKMeansResult invalidParameters =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = 1u,
                .Domain = Domain::MeshEdges,
                .ClusterCount = 0u,
                .MaxIterations = 0u,
            });
    EXPECT_EQ(invalidParameters.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(invalidParameters.Error, Core::ErrorCode::InvalidArgument);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     invalidParameters.Status),
                 "InvalidProcessingParameters");

    const Runtime::SandboxEditorKMeansResult stale =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(stale.Status, Runtime::SandboxEditorCommandStatus::StaleEntity);
    EXPECT_EQ(stale.Error, Core::ErrorCode::ResourceNotFound);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    const Runtime::SandboxEditorKMeansResult wrongDomain =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::MeshVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    const Runtime::SandboxEditorKMeansResult missingPositions =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(missingPositions.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                 });
    const Runtime::SandboxEditorKMeansResult nonFinite =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(nonFinite.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    context.LastKMeansResult = &nonFinite;
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed));
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDiagnosticCode(
                     Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed),
                 "GeometryProcessingFailed");
}
TEST(SandboxEditorUi, RegistrationCommandAlignsSourceOntoTargetAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> target = MakeRegistrationCloud();
    const glm::vec3 offset{0.05f, -0.03f, 0.02f};
    std::vector<glm::vec3> sourcePoints{};
    sourcePoints.reserve(target.size());
    for (const glm::vec3& p : target)
        sourcePoints.push_back(p + offset);

    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "ICP Source", sourcePoints);
    const ECS::EntityHandle targetEntity =
        MakePointCloudEntity(registry, "ICP Target", target);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(targetEntity);
    const glm::vec3 originalPosition =
        registry.Raw().get<ECSC::Transform::Component>(source).Position;

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(result.HasResult);
    EXPECT_EQ(result.SourcePointCount, sourcePoints.size());
    EXPECT_EQ(result.TargetPointCount, target.size());
    EXPECT_GT(result.IterationsPerformed, 0u);
    EXPECT_EQ(result.TrajectoryLength, result.IterationsPerformed);
    EXPECT_EQ(result.AppliedStep, result.TrajectoryLength);
    EXPECT_LT(result.FinalRMSE, 1.0e-3);

    // ICP recovers the transform mapping source (= target + offset) back onto
    // target: a pure translation by -offset with identity rotation.
    const ECSC::Transform::Component& aligned =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(aligned.Position.x, -offset.x, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.y, -offset.y, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.z, -offset.z, 1.0e-2f);
    EXPECT_NEAR(std::abs(aligned.Rotation.w), 1.0f, 1.0e-2f);

    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    const glm::vec3 undone =
        registry.Raw().get<ECSC::Transform::Component>(source).Position;
    EXPECT_NEAR(undone.x, originalPosition.x, 1.0e-5f);
    EXPECT_NEAR(undone.y, originalPosition.y, 1.0e-5f);
    EXPECT_NEAR(undone.z, originalPosition.z, 1.0e-5f);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_NEAR(
        registry.Raw().get<ECSC::Transform::Component>(source).Position.x,
        -offset.x, 1.0e-2f);
}
TEST(SandboxEditorUi, RegistrationRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorRegistrationResult> completedResult{};
    context.MethodResultSinks.Registration =
        [&completedResult](Runtime::SandboxEditorRegistrationResult result)
        {
            completedResult = std::move(result);
        };

    const std::vector<glm::vec3> target = MakeRegistrationCloud();
    const glm::vec3 offset{0.05f, -0.03f, 0.02f};
    std::vector<glm::vec3> sourcePoints{};
    sourcePoints.reserve(target.size());
    for (const glm::vec3& p : target)
        sourcePoints.push_back(p + offset);

    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Queued ICP Source", sourcePoints);
    const ECS::EntityHandle targetEntity =
        MakePointCloudEntity(registry, "Queued ICP Target", target);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(targetEntity);
    const ECSC::Transform::Component before =
        registry.Raw().get<ECSC::Transform::Component>(source);

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.SourcePointCount, sourcePoints.size());
    EXPECT_EQ(result.TargetPointCount, target.size());
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    const ECSC::Transform::Component& pending =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(pending.Position.x, before.Position.x, 1.0e-6f);
    EXPECT_NEAR(pending.Position.y, before.Position.y, 1.0e-6f);
    EXPECT_NEAR(pending.Position.z, before.Position.z, 1.0e-6f);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(source));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.RegistrationICP.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_NEAR(
        registry.Raw().get<ECSC::Transform::Component>(source).Position.x,
        before.Position.x,
        1.0e-6f);

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_TRUE(completedResult->HasResult);
    EXPECT_GT(completedResult->IterationsPerformed, 0u);
    EXPECT_EQ(completedResult->AppliedStep,
              completedResult->TrajectoryLength);
    EXPECT_LT(completedResult->FinalRMSE, 1.0e-3);

    const ECSC::Transform::Component& aligned =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(aligned.Position.x, -offset.x, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.y, -offset.y, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.z, -offset.z, 1.0e-2f);
    EXPECT_TRUE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(source));
    EXPECT_TRUE(history.IsDirty());
    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    const ECSC::Transform::Component& undone =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(undone.Position.x, before.Position.x, 1.0e-6f);
    EXPECT_NEAR(undone.Position.y, before.Position.y, 1.0e-6f);
    EXPECT_NEAR(undone.Position.z, before.Position.z, 1.0e-6f);
}
TEST(SandboxEditorUi, RegistrationDerivedJobDiscardsStaleSourceBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.Registration =
        [&completedSinkCalled](Runtime::SandboxEditorRegistrationResult)
        {
            completedSinkCalled = true;
        };

    const std::vector<glm::vec3> target = MakeRegistrationCloud();
    const glm::vec3 offset{0.05f, -0.03f, 0.02f};
    std::vector<glm::vec3> sourcePoints{};
    sourcePoints.reserve(target.size());
    for (const glm::vec3& p : target)
        sourcePoints.push_back(p + offset);

    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Stale ICP Source", sourcePoints);
    const ECS::EntityHandle targetEntity =
        MakePointCloudEntity(registry, "Stale ICP Target", target);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(targetEntity);

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    std::vector<glm::vec3> staleSourcePoints = sourcePoints;
    for (glm::vec3& point : staleSourcePoints)
        point += glm::vec3{10.0f, 0.0f, 0.0f};
    SetPositions(registry.Raw().get<GS::Vertices>(source), staleSourcePoints);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    const ECSC::Transform::Component& transform =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(transform.Position.x, 0.0f, 1.0e-6f);
    EXPECT_NEAR(transform.Position.y, 0.0f, 1.0e-6f);
    EXPECT_NEAR(transform.Position.z, 0.0f, 1.0e-6f);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(source));
}
TEST(SandboxEditorUi, RegistrationCommandFailsClosedForInvalidSelectionAndParameters)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const std::vector<glm::vec3> cloud = MakeRegistrationCloud();
    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Src", cloud);
    const ECS::EntityHandle target =
        MakePointCloudEntity(registry, "Tgt", cloud);
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddIcosahedronMeshSource(registry, mesh);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(target);
    const std::uint32_t meshId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = sourceId,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .MaxIterations = 0u,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId + 9999u,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::StaleEntity);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = meshId,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
}
TEST(SandboxEditorUi, RegistrationCommandAlignsAcrossEntityTransforms)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    // Identical local clouds, but the target is translated in the scene. ICP run
    // on raw local arrays would return identity and leave the source at the
    // origin; running in world space must drive the source onto the target.
    const std::vector<glm::vec3> cloud = MakeRegistrationCloud();
    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Src", cloud);
    const ECS::EntityHandle target =
        MakePointCloudEntity(registry, "Tgt", cloud);
    const glm::vec3 targetWorldOffset{1.0f, 0.5f, -0.5f};
    registry.Raw().get<ECSC::Transform::Component>(target).Position =
        targetWorldOffset;

    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(target);

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_LT(result.FinalRMSE, 1.0e-3);
    const ECSC::Transform::Component& aligned =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(aligned.Position.x, targetWorldOffset.x, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.y, targetWorldOffset.y, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.z, targetWorldOffset.z, 1.0e-2f);
    EXPECT_NEAR(std::abs(aligned.Rotation.w), 1.0f, 1.0e-2f);
}
