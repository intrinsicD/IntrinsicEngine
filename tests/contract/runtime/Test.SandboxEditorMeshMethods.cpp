// ARCH-006 runtime Sandbox editor MeshMethods contract partition.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "ProgressivePoissonReference.hpp"
#include <entt/entity/entity.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"

#include "EditorFeatureTestContext.hpp"

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
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
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;
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
#include "SandboxEditorJobHarness.hpp"

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
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

void InstallSandboxDefaultRuntimePolicies(Runtime::Engine& engine)
    {
        auto* const pipeline =
            engine.Services().Find<Runtime::AssetWorkflowModule>();
        ASSERT_NE(pipeline, nullptr);
    }

[[nodiscard]] bool HasDiagnostic(
        const std::vector<Runtime::EditorDiagnostic>& diagnostics,
        const Runtime::EditorDiagnosticCode code)
    {
        for (const Runtime::EditorDiagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.Code == code)
                return true;
        }
        return false;
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

void SetNormals(
        GS::Vertices& vertices,
        const std::string_view propertyName = PN::kNormal)
    {
        vertices.Properties
            .GetOrAdd<glm::vec3>(
                std::string{propertyName},
                glm::vec3{0.0f, 0.0f, 1.0f})
            .Vector() = {
                glm::vec3{0.0f, 0.0f, 1.0f},
                glm::vec3{0.0f, 0.0f, 1.0f},
                glm::vec3{0.0f, 0.0f, 1.0f},
            };
    }

    // A small deterministic, asymmetric point lattice — distinct extents per axis
    // give ICP a well-conditioned correspondence problem (UI-029).

[[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeGridPlaneMesh(const int n)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<Geometry::VertexHandle> handles;
        handles.reserve(static_cast<std::size_t>((n + 1) * (n + 1)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
                handles.push_back(mesh.AddVertex(glm::vec3(
                    static_cast<float>(i), static_cast<float>(j), 0.0f)));
        const auto at = [&](const int i, const int j) {
            return handles[static_cast<std::size_t>(i * (n + 1) + j)];
        };
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                (void)mesh.AddTriangle(at(i, j), at(i + 1, j), at(i + 1, j + 1));
                (void)mesh.AddTriangle(at(i, j), at(i + 1, j + 1), at(i, j + 1));
            }
        return mesh;
    }

    // Per-vertex texcoords in the same (i, j) order MakeGridPlaneMesh adds
    // vertices, so they align 1:1 with the populated GeometrySources.

[[nodiscard]] std::vector<glm::vec2> GridPlaneTexcoords(const int n)
    {
        std::vector<glm::vec2> tex;
        tex.reserve(static_cast<std::size_t>((n + 1) * (n + 1)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
                tex.emplace_back(static_cast<float>(i) / static_cast<float>(n),
                                 static_cast<float>(j) / static_cast<float>(n));
        return tex;
    }

[[nodiscard]] const Runtime::EditorTextureBakeSourceRow*
    FindTextureBakeSource(
        const Runtime::EditorTextureBakeControlsModel& model,
        const std::string& name)
    {
        for (const Runtime::EditorTextureBakeSourceRow& row :
             model.Sources)
        {
            if (row.Name == name)
                return &row;
        }
        return nullptr;
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

void AddDenoiseTetraMeshSource(ECS::Scene::Registry& registry,
                                   const ECS::EntityHandle entity)
    {
        Geometry::HalfedgeMesh::Mesh mesh =
            Geometry::HalfedgeMesh::MakeMeshTetrahedron();
        mesh.Position(Geometry::VertexHandle{0u}) +=
            glm::vec3{0.35f, -0.15f, 0.20f};
        GS::PopulateFromMesh(registry.Raw(), entity, mesh);
        registry.Raw().emplace_or_replace<G::RenderSurface>(entity);
    }

void AddIcosahedronMeshSource(ECS::Scene::Registry& registry,
                                  const ECS::EntityHandle entity)
    {
        Geometry::HalfedgeMesh::Mesh mesh =
            Geometry::HalfedgeMesh::MakeMeshIcosahedron();
        GS::PopulateFromMesh(registry.Raw(), entity, mesh);
        registry.Raw().emplace_or_replace<G::RenderSurface>(entity);
    }

struct MeshCounts
    {
        std::size_t Vertices{0u};
        std::size_t Faces{0u};
    };

[[nodiscard]] MeshCounts SourceMeshCounts(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        const GS::ConstSourceView view =
            GS::BuildConstView(registry.Raw(), entity);
        return MeshCounts{
            .Vertices = view.VerticesAlive(),
            .Faces = view.FacesAlive(),
        };
    }

void ExpectMeshCountsEqual(const MeshCounts actual,
                               const MeshCounts expected)
    {
        EXPECT_EQ(actual.Vertices, expected.Vertices);
        EXPECT_EQ(actual.Faces, expected.Faces);
    }

[[nodiscard]] std::vector<glm::vec3> MeshVertexPositions(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        auto positions = registry.Raw()
                             .get<GS::Vertices>(entity)
                             .Properties.Get<glm::vec3>(PN::kPosition);
        if (!positions)
            return {};
        return positions.Vector();
    }

void ExpectPositionsExactlyEqual(
        const std::vector<glm::vec3>& lhs,
        const std::vector<glm::vec3>& rhs)
    {
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            EXPECT_FLOAT_EQ(lhs[i].x, rhs[i].x);
            EXPECT_FLOAT_EQ(lhs[i].y, rhs[i].y);
            EXPECT_FLOAT_EQ(lhs[i].z, rhs[i].z);
        }
    }

void ExpectTexcoordsExactlyEqual(
        const std::vector<glm::vec2>& lhs,
        const std::vector<glm::vec2>& rhs)
    {
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(lhs[i].x),
                      std::bit_cast<std::uint32_t>(rhs[i].x));
            EXPECT_EQ(std::bit_cast<std::uint32_t>(lhs[i].y),
                      std::bit_cast<std::uint32_t>(rhs[i].y));
        }
    }

void ExpectColorsExactlyEqual(
        const std::vector<glm::vec4>& lhs,
        const std::vector<glm::vec4>& rhs)
    {
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(lhs[i].x),
                      std::bit_cast<std::uint32_t>(rhs[i].x));
            EXPECT_EQ(std::bit_cast<std::uint32_t>(lhs[i].y),
                      std::bit_cast<std::uint32_t>(rhs[i].y));
            EXPECT_EQ(std::bit_cast<std::uint32_t>(lhs[i].z),
                      std::bit_cast<std::uint32_t>(rhs[i].z));
            EXPECT_EQ(std::bit_cast<std::uint32_t>(lhs[i].w),
                      std::bit_cast<std::uint32_t>(rhs[i].w));
        }
    }

[[nodiscard]] bool AnyPositionDiffers(
        const std::vector<glm::vec3>& lhs,
        const std::vector<glm::vec3>& rhs)
    {
        if (lhs.size() != rhs.size())
            return true;
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            if (lhs[i].x != rhs[i].x ||
                lhs[i].y != rhs[i].y ||
                lhs[i].z != rhs[i].z)
            {
                return true;
            }
        }
        return false;
    }

[[nodiscard]] Runtime::GeometryPresentationRecipe
    MakeGeometryPresentationRecipe()
    {
        Runtime::GeometryPresentationSlotRecipe albedo{};
        albedo.Semantic = Runtime::GeometryPresentationSlotSemantic::Albedo;
        albedo.SourceKind = Runtime::GeometryPresentationSourceKind::UniformDefault;
        albedo.UniformDefault = Runtime::GeometryPresentationDefaultValue{
            .Kind = Geometry::PropertyValueKind::Vec4,
            .Vector = glm::vec4{0.2f, 0.4f, 0.8f, 1.0f},
        };
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
                    .Slots = {albedo, normal},
                },
            },
        };
    }

    [[nodiscard]] Runtime::GeometryPresentationRuntimeState
    MakeGeometryPresentationRuntimeState()
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
                    .Diagnostic = "waiting for normal bake",
                },
            },
        };
    }

    void AttachGeometryPresentation(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        registry.Raw().emplace<Runtime::GeometryPresentationRecipe>(
            entity,
            MakeGeometryPresentationRecipe());
        registry.Raw().emplace<Runtime::GeometryPresentationRuntimeState>(
            entity,
            MakeGeometryPresentationRuntimeState());
    }

void AddPlanarCycleGraphSource(ECS::Scene::Registry& registry,
                                   const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        SetNodePositions(nodes,
                         {
                             {0.0f, 0.0f, 0.0f},
                             {1.0f, 0.0f, 0.0f},
                             {1.0f, 1.0f, 0.0f},
                             {0.0f, 1.0f, 0.0f},
                         });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u, 2u, 3u}, {1u, 2u, 3u, 0u});
        raw.emplace<GS::HasGraphTopology>(entity);
        raw.emplace<G::RenderEdges>(entity);
        raw.emplace<G::RenderPoints>(entity);
    }

[[nodiscard]] Intrinsic::Tests::EditorFeatureTestContext MakeContext(
        ECS::Scene::Registry& registry,
        Runtime::SelectionController& selection,
        const bool imguiAvailable = true,
        const std::optional<Runtime::PrimitiveSelectionResult>* lastPrimitive = nullptr,
        Extrinsic::RHI::IDevice* device = nullptr)
    {
        return Intrinsic::Tests::EditorFeatureTestContext{
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

    class WaitForConditionApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit WaitForConditionApplication(
            std::function<bool(Runtime::Engine&)> ready,
            std::uint32_t maxFrames = 512u)
            : m_Ready(std::move(ready))
            , m_MaxFrames(maxFrames)
        {
        }

        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++m_ObservedFrames;
            if ((m_Ready && m_Ready(engine)) || m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
                return;
            }
            std::this_thread::yield();
        }
        void Shutdown() override {}

    private:
        std::function<bool(Runtime::Engine&)> m_Ready{};
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

[[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    class DirectMeshPostProcessWorkerBarrier final
    {
    public:
        DirectMeshPostProcessWorkerBarrier() = default;
        ~DirectMeshPostProcessWorkerBarrier()
        {
            Release();
        }

        DirectMeshPostProcessWorkerBarrier(
            const DirectMeshPostProcessWorkerBarrier&) = delete;
        DirectMeshPostProcessWorkerBarrier& operator=(
            const DirectMeshPostProcessWorkerBarrier&) = delete;

        [[nodiscard]] Runtime::JobToken Submit(
            Runtime::JobService& jobs,
            const Runtime::WorldHandle world)
        {
            return jobs.Submit(Runtime::JobDesc{
                .DebugName = "Test.DirectMeshPostProcessWorkerBarrier",
                .Scope = world,
                .Priority = Extrinsic::Core::Dag::TaskPriority::Normal,
                .Kind = Runtime::RuntimeTaskKinds::Generic,
                .EstimatedCost = 1u,
                .Work =
                    [this](const Runtime::JobCancellation&)
                    {
                        std::unique_lock lock(m_Mutex);
                        m_Started = true;
                        m_Condition.notify_all();
                        m_Condition.wait(
                            lock,
                            [this]
                            {
                                return m_Released;
                            });
                        return Runtime::JobResultEnvelope::Make<bool>(true);
                    },
                .PublishCompletion =
                    [](
                        Runtime::KernelEventBus&,
                        const Runtime::JobResultEnvelope& envelope)
                    {
                        return envelope.TryGet<bool>() != nullptr;
                    },
            });
        }

        [[nodiscard]] bool WaitUntilStarted()
        {
            std::unique_lock lock(m_Mutex);
            return m_Condition.wait_for(
                lock,
                std::chrono::seconds(5),
                [this]
                {
                    return m_Started;
                });
        }

        void Release()
        {
            {
                std::lock_guard lock(m_Mutex);
                m_Released = true;
            }
            m_Condition.notify_all();
        }

    private:
        std::mutex m_Mutex{};
        std::condition_variable m_Condition{};
        bool m_Started{false};
        bool m_Released{false};
    };

    [[nodiscard]] std::unique_ptr<Intrinsic::Tests::RuntimeTestModule>
    MakeDirectMeshPostProcessExitApplication()
    {
        return std::make_unique<WaitForConditionApplication>(
            [](Runtime::Engine& runningEngine)
            {
                const Runtime::JobServiceStats stats =
                    RequiredEngineService<Runtime::JobService>(runningEngine)
                        .Stats();
                return stats.InFlightJobs == 0u &&
                    stats.PublishedCompletions + stats.StaleDiscardedJobs >= 2u;
            },
            1024u);
    }

    void InitializeDirectMeshPostProcessEngine(
        Intrinsic::Tests::RuntimeTestKernel& engine)
    {
        engine.EmplaceModule<Runtime::AsyncWorkModule>();
        engine.EmplaceModule<Runtime::SceneDocumentModule>();
        engine.EmplaceModule<Runtime::SceneInteractionModule>();
        engine.EmplaceModule<Runtime::AssetWorkflowModule>();
        engine.Initialize();
        InstallSandboxDefaultRuntimePolicies(engine);
    }

void ExpectFiniteUnitNormal(const glm::vec3 normal,
                                const float epsilon = 1.0e-4f)
    {
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
        EXPECT_NEAR(glm::length(normal), 1.0f, epsilon);
    }

struct TmpFile
    {
        std::filesystem::path Path;

        TmpFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << contents;
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

[[nodiscard]] std::optional<ECS::EntityHandle> FindFirstEntityWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        auto& raw = registry.Raw();
        std::optional<ECS::EntityHandle> found{};
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (!raw.all_of<Sel::SelectableTag>(entity))
                return;
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
                found = entity;
        });
        return found;
    }

}

TEST(SandboxEditorUi, MeshDenoiseCommandPublishesPositionsAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const std::vector<glm::vec3> original =
        MeshVertexPositions(registry, mesh);
    ASSERT_EQ(original.size(), 4u);

    const Runtime::EditorMeshDenoiseResult result =
        Runtime::ApplyEditorMeshDenoiseCommand(
            context,
            Runtime::EditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.DenoiseStatus, Smooth::DenoiseStatus::Success);
    EXPECT_EQ(result.VertexSlotCount, original.size());
    EXPECT_EQ(result.WrittenCount, original.size());
    EXPECT_EQ(result.SkippedDeletedVertexCount, 0u);
    EXPECT_EQ(result.NormalIterations, 2u);
    EXPECT_EQ(result.VertexIterations, 3u);
    EXPECT_GT(result.ProcessedFaceCount, 0u);
    EXPECT_GT(result.MovedVertexCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());

    const std::vector<glm::vec3> denoised =
        MeshVertexPositions(registry, mesh);
    ASSERT_TRUE(AnyPositionDiffers(original, denoised));
    for (const glm::vec3 position : denoised)
    {
        EXPECT_TRUE(std::isfinite(position.x));
        EXPECT_TRUE(std::isfinite(position.y));
        EXPECT_TRUE(std::isfinite(position.z));
    }

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), denoised);

    std::vector<glm::vec3> intervening = denoised;
    intervening.front().x += 1.0f;
    SetPositions(registry.Raw().get<GS::Vertices>(mesh), intervening);
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        history.Snapshot();
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(registry, mesh),
        intervening);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_EQ(history.Snapshot().Revision, beforeRejectedUndo.Revision);

    context.LastMeshDenoiseResult = &result;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshDenoiseAvailable);
    ASSERT_TRUE(model.Processing.LastMeshDenoiseResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshDenoiseResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshDenoiseResult->WrittenCount, 4u);
}
TEST(SandboxEditorUi, MeshDenoiseRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorMeshDenoiseResult> completedResult{};
    context.MethodResultSinks.MeshDenoise =
        [&completedResult](Runtime::EditorMeshDenoiseResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const std::vector<glm::vec3> original =
        MeshVertexPositions(registry, mesh);

    const Runtime::EditorMeshDenoiseResult result =
        Runtime::ApplyEditorMeshDenoiseCommand(
            context,
            Runtime::EditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshDenoise.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races
    // between Queued/Running/AwaitingGate; assert only that it is active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_FALSE(completedResult.has_value());

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->DenoiseStatus, Smooth::DenoiseStatus::Success);
    EXPECT_EQ(completedResult->VertexSlotCount, original.size());
    EXPECT_GT(completedResult->MovedVertexCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(AnyPositionDiffers(original, MeshVertexPositions(registry, mesh)));
}
TEST(SandboxEditorUi, MeshDenoiseDerivedJobDiscardsStaleMeshBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshDenoise =
        [&completedSinkCalled](Runtime::EditorMeshDenoiseResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "StaleDenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshDenoiseResult result =
        Runtime::ApplyEditorMeshDenoiseCommand(
            context,
            Runtime::EditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });
    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

    SetPositions(registry.Raw().get<GS::Vertices>(mesh),
                 {
                     {10.0f, 0.0f, 0.0f},
                     {11.0f, 0.0f, 0.0f},
                     {12.0f, 0.0f, 0.0f},
                     {13.0f, 0.0f, 0.0f},
                 });

    ASSERT_TRUE(jobs.DrainUntilTerminal());

    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
}

namespace
{
    // Two dense 5x5 grid clusters plus three far isolated outliers (appended
    // last) — a deterministic UI-027 outlier-removal fixture mirroring the
    // GEOM-016 unit fixture.
    [[nodiscard]] std::vector<glm::vec3> MakeOutlierClusterPositions()
    {
        std::vector<glm::vec3> positions;
        const auto appendGrid = [&positions](const glm::vec3 origin)
        {
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 5; ++x)
                    positions.push_back(
                        origin + glm::vec3(static_cast<float>(x) * 0.05f,
                                           static_cast<float>(y) * 0.05f,
                                           0.0f));
        };
        appendGrid(glm::vec3{0.0f});
        appendGrid(glm::vec3{2.0f, 0.0f, 0.0f});
        positions.push_back(glm::vec3{10.0f, 10.0f, 10.0f});
        positions.push_back(glm::vec3{-8.0f, 5.0f, -3.0f});
        positions.push_back(glm::vec3{12.0f, -7.0f, 4.0f});
        return positions;
    }

    [[nodiscard]] std::size_t PointCloudPositionCount(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        auto pos = registry.Raw()
                       .get<GS::Vertices>(entity)
                       .Properties.Get<glm::vec3>(PN::kPosition);
        return pos ? pos.Vector().size() : 0u;
    }
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalStatisticalPublishesKeptPointsWithUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    ASSERT_EQ(originalCount, 53u);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "OutlierCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    ASSERT_EQ(PointCloudPositionCount(registry, cloud), originalCount);

    const Runtime::EditorPointCloudOutlierRemovalResult result =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.OriginalCount, originalCount);
    EXPECT_GE(result.RejectedCount, 2u);
    EXPECT_EQ(result.KeptCount + result.RejectedCount, originalCount);
    EXPECT_LT(result.KeptCount, originalCount);
    // The published point GeometrySources reflect exactly the kept points.
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_TRUE(history.IsDirty());

    // Undo restores the original point set; redo re-applies the removal.
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);

    context.LastPointCloudOutlierRemovalResult = &result;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(model.Processing.PointCloudOutlierRemovalAvailable);
    ASSERT_TRUE(
        model.Processing.LastPointCloudOutlierRemovalResult.has_value());
    EXPECT_TRUE(
        model.Processing.LastPointCloudOutlierRemovalResult->Succeeded());
}
TEST(SandboxEditorUi,
     PointCloudOutlierRemovalRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorPointCloudOutlierRemovalResult>
        completedResult{};
    context.MethodResultSinks.PointCloudOutlierRemoval =
        [&completedResult](
            Runtime::EditorPointCloudOutlierRemovalResult result)
        {
            completedResult = std::move(result);
        };

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "QueuedOutlierCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::EditorPointCloudOutlierRemovalResult queued =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });

    EXPECT_EQ(queued.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(queued.OriginalCount, originalCount);
    EXPECT_EQ(queued.KeptCount, originalCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    Runtime::EditorJobQueueSnapshot pending =
        jobs.Snapshot();
    ASSERT_EQ(pending.Entries.size(), 1u);
    EXPECT_EQ(pending.Entries[0].Name,
              "Sandbox.PointCloudOutlierRemoval.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races;
    // assert only that the job is still active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(pending.Entries[0].State));

    EXPECT_FALSE(completedResult.has_value());
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->OriginalCount, originalCount);
    EXPECT_GE(completedResult->RejectedCount, 2u);
    EXPECT_EQ(completedResult->KeptCount + completedResult->RejectedCount,
              originalCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud),
              completedResult->KeptCount);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    EXPECT_TRUE(history.IsDirty());

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud),
              completedResult->KeptCount);
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalDerivedJobDiscardsStaleSource)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.PointCloudOutlierRemoval =
        [&completedSinkCalled](
            Runtime::EditorPointCloudOutlierRemovalResult)
        {
            completedSinkCalled = true;
        };

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "StaleOutlierCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::EditorPointCloudOutlierRemovalResult queued =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_EQ(queued.Status, Runtime::EditorCommandStatus::Pending);

    std::vector<glm::vec3> stalePositions = positions;
    for (glm::vec3& position : stalePositions)
        position.x += 100.0f;
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), stalePositions);

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalDerivedJobDiscardsStaleProperty)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.PointCloudOutlierRemoval =
        [&completedSinkCalled](
            Runtime::EditorPointCloudOutlierRemovalResult)
        {
            completedSinkCalled = true;
        };

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "StaleOutlierPropertyCloud");
    AddPointCloudSource(registry, cloud, positions.size());
    auto& vertices = registry.Raw().get<GS::Vertices>(cloud);
    SetPositions(vertices, positions);
    auto confidence =
        vertices.Properties.GetOrAdd<float>("v:confidence", 0.5f);
    ASSERT_TRUE(confidence);

    const Runtime::EditorPointCloudOutlierRemovalResult queued =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_EQ(queued.Status, Runtime::EditorCommandStatus::Pending);

    confidence[0] = 0.75f;

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    const Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), positions.size());
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    EXPECT_FLOAT_EQ(
        registry.Raw()
            .get<GS::Vertices>(cloud)
            .Properties.Get<float>("v:confidence")[0],
        0.75f);
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalRadiusPublishesAndFailsClosed)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud = MakeSelectable(registry, "RadiusCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(cloud);

    const Runtime::EditorPointCloudOutlierRemovalResult radius =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Radius,
                .SearchRadius = 0.15f,
                .MinNeighbors = 3u,
            });
    ASSERT_TRUE(radius.Succeeded()) << radius.Message;
    EXPECT_GE(radius.RejectedCount, 2u);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), radius.KeptCount);

    // Fail-closed: non-positive radius is rejected before any mutation.
    const Runtime::EditorPointCloudOutlierRemovalResult badRadius =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Radius,
                .SearchRadius = 0.0f,
                .MinNeighbors = 3u,
            });
    EXPECT_EQ(badRadius.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);

    // Missing scene fails closed.
    const Runtime::EditorPointCloudOutlierRemovalResult missingScene =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            Intrinsic::Tests::EditorFeatureTestContext{},
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .KNeighbors = 8u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::EditorCommandStatus::MissingScene);

    // A mesh entity is the wrong domain for point-cloud outlier removal.
    const ECS::EntityHandle mesh = MakeSelectable(registry, "WrongDomainMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const Runtime::EditorPointCloudOutlierRemovalResult wrongDomain =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .KNeighbors = 8u,
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalPreservesSurvivingPointProperties)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();  // 53: 50 inliers + 3 outliers
    const ECS::EntityHandle cloud = MakeSelectable(registry, "LabeledCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    // A non-built-in per-point property: every inlier carries the sentinel 7.0,
    // the two trailing outliers carry 99.0. removal.Filtered would drop this
    // property entirely; the command must compact it onto the kept points.
    {
        auto labels = registry.Raw()
                          .get<GS::Vertices>(cloud)
                          .Properties.GetOrAdd<float>("v:label", 0.0f);
        ASSERT_EQ(labels.Vector().size(), originalCount);
        for (std::size_t i = 0; i < originalCount; ++i)
            labels.Vector()[i] = (i + 3u >= originalCount) ? 99.0f : 7.0f;
    }
    const std::vector<float> originalLabels =
        registry.Raw()
            .get<GS::Vertices>(cloud)
            .Properties.Get<float>("v:label")
            .Vector();
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::EditorPointCloudOutlierRemovalResult result =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    // The "v:label" property must survive removal (bug: previously dropped).
    auto survived = registry.Raw()
                        .get<GS::Vertices>(cloud)
                        .Properties.Get<float>("v:label");
    ASSERT_TRUE(survived) << "v:label property was dropped after outlier removal";
    EXPECT_EQ(survived.Vector().size(), result.KeptCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);
    // All kept points are the inliers, so every surviving label is the sentinel.
    for (const float label : survived.Vector())
        EXPECT_FLOAT_EQ(label, 7.0f);
    const std::vector<glm::vec3> keptPositions =
        MeshVertexPositions(registry, cloud);
    const std::vector<float> keptLabels = survived.Vector();

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(registry, cloud),
        positions);
    const auto restoredLabels =
        registry.Raw()
            .get<GS::Vertices>(cloud)
            .Properties.Get<float>("v:label");
    ASSERT_TRUE(restoredLabels);
    EXPECT_EQ(restoredLabels.Vector(), originalLabels);

    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(registry, cloud),
        keptPositions);
    auto redoneLabels =
        registry.Raw()
            .get<GS::Vertices>(cloud)
            .Properties.Get<float>("v:label");
    ASSERT_TRUE(redoneLabels);
    EXPECT_EQ(redoneLabels.Vector(), keptLabels);

    redoneLabels[0] = 42.0f;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        history.Snapshot();
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_FLOAT_EQ(
        registry.Raw()
            .get<GS::Vertices>(cloud)
            .Properties.Get<float>("v:label")[0],
        42.0f);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(registry, cloud),
        keptPositions);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_EQ(history.Snapshot().Revision, beforeRejectedUndo.Revision);
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalRespectsDeletedSlots)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    // 53 live points plus one trailing slot marked deleted (a far position that
    // would look like an outlier if it were wrongly treated as live).
    std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t liveCount = positions.size();  // 53
    positions.push_back(glm::vec3{50.0f, 50.0f, 50.0f});
    const std::size_t slotCount = positions.size();  // 54

    const ECS::EntityHandle cloud = MakeSelectable(registry, "DeletedSlotCloud");
    AddPointCloudSource(registry, cloud, slotCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    {
        auto& vertices = registry.Raw().get<GS::Vertices>(cloud);
        auto deleted =
            vertices.Properties.GetOrAdd<bool>("p:deleted", false);
        ASSERT_EQ(deleted.Vector().size(), slotCount);
        deleted.Vector()[slotCount - 1u] = true;  // last slot is dead
        vertices.NumDeleted = 1u;
    }
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::EditorPointCloudOutlierRemovalResult result =
        Runtime::ApplyEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::EditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::EditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    // The deleted slot is excluded: counts reflect the live point set only, and
    // the dead point is never resurrected into the published cloud.
    EXPECT_EQ(result.OriginalCount, liveCount);
    EXPECT_EQ(result.KeptCount + result.RejectedCount, liveCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);
    EXPECT_LE(result.KeptCount, liveCount);
    EXPECT_EQ(registry.Raw().get<GS::Vertices>(cloud).NumDeleted, 0u);

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    const auto& restored = registry.Raw().get<GS::Vertices>(cloud);
    EXPECT_EQ(restored.Properties.Size(), slotCount);
    EXPECT_EQ(restored.NumDeleted, 1u);
    const auto restoredDeleted =
        restored.Properties.Get<bool>("p:deleted");
    ASSERT_TRUE(restoredDeleted);
    ASSERT_EQ(restoredDeleted.Vector().size(), slotCount);
    EXPECT_TRUE(restoredDeleted[slotCount - 1u]);

    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);
    EXPECT_EQ(registry.Raw().get<GS::Vertices>(cloud).NumDeleted, 0u);
}
TEST(SandboxEditorUi, MeshDenoiseCommandFailsClosedForInvalidTargetsAndUnavailableKernel)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::EditorMeshDenoiseResult missingScene =
        Runtime::ApplyEditorMeshDenoiseCommand(
            Intrinsic::Tests::EditorFeatureTestContext{},
            Runtime::EditorMeshDenoiseCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::EditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "CloudWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });
    const Runtime::EditorMeshDenoiseResult wrongDomain =
        Runtime::ApplyEditorMeshDenoiseCommand(
            context,
            Runtime::EditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseFailMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::vector<glm::vec3> before =
        MeshVertexPositions(registry, mesh);
    const Runtime::EditorMeshDenoiseResult invalidParams =
        Runtime::ApplyEditorMeshDenoiseCommand(
            context,
            Runtime::EditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .NormalIterations = 0u,
            });
    EXPECT_EQ(invalidParams.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    context.MeshDenoiseKernelAvailable = false;
    const Runtime::EditorMeshDenoiseResult unavailable =
        Runtime::ApplyEditorMeshDenoiseCommand(
            context,
            Runtime::EditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailable.Error, Core::ErrorCode::InvalidState);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const Runtime::EditorDomainWindowModel unavailableModel =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_FALSE(unavailableModel.Processing.MeshDenoiseAvailable);
}
TEST(SandboxEditorUi, MeshCurvatureCommandPublishesCanonicalPropertiesAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "CurvatureMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    ASSERT_FALSE(properties.Exists(PN::kMeanCurvature));
    ASSERT_FALSE(properties.Exists(PN::kGaussianCurvature));
    ASSERT_FALSE(properties.Exists(PN::kPrincipalDir1));
    ASSERT_FALSE(properties.Exists(PN::kPrincipalDir2));

    const Runtime::EditorMeshCurvatureResult result =
        Runtime::ApplyEditorMeshCurvatureCommand(
            context,
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId = stableId,
                .Output = Runtime::EditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.VertexSlotCount, 4u);
    EXPECT_EQ(result.ScalarPropertyCount, 2u);
    EXPECT_EQ(result.ScalarWrittenCount, 8u);
    EXPECT_EQ(result.DirectionPropertyCount, 2u);
    EXPECT_EQ(result.DirectionWrittenCount, 8u);
    EXPECT_EQ(result.NonFiniteScalarCount, 0u);
    EXPECT_EQ(result.NonFiniteDirectionCount, 0u);
    EXPECT_TRUE(result.DirectionsRequested);
    EXPECT_TRUE(result.DirectionsAvailable);
    EXPECT_TRUE(result.DirectionsPublished);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(history.CanUndo());

    const auto mean = properties.Get<double>(PN::kMeanCurvature);
    const auto gaussian = properties.Get<double>(PN::kGaussianCurvature);
    const auto dir1 = properties.Get<glm::vec3>(PN::kPrincipalDir1);
    const auto dir2 = properties.Get<glm::vec3>(PN::kPrincipalDir2);
    ASSERT_TRUE(mean.IsValid());
    ASSERT_TRUE(gaussian.IsValid());
    ASSERT_TRUE(dir1.IsValid());
    ASSERT_TRUE(dir2.IsValid());
    ASSERT_EQ(mean.Vector().size(), 4u);
    ASSERT_EQ(gaussian.Vector().size(), 4u);
    ASSERT_EQ(dir1.Vector().size(), 4u);
    ASSERT_EQ(dir2.Vector().size(), 4u);
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        EXPECT_TRUE(std::isfinite(mean.Vector()[i]));
        EXPECT_TRUE(std::isfinite(gaussian.Vector()[i]));
        EXPECT_TRUE(std::isfinite(dir1.Vector()[i].x));
        EXPECT_TRUE(std::isfinite(dir1.Vector()[i].y));
        EXPECT_TRUE(std::isfinite(dir1.Vector()[i].z));
        EXPECT_TRUE(std::isfinite(dir2.Vector()[i].x));
        EXPECT_TRUE(std::isfinite(dir2.Vector()[i].y));
        EXPECT_TRUE(std::isfinite(dir2.Vector()[i].z));
    }

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_TRUE(properties.Get<double>(PN::kMeanCurvature).IsValid());
    EXPECT_TRUE(properties.Get<double>(PN::kGaussianCurvature).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir1).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir2).IsValid());

    auto liveMean = properties.Get<double>(PN::kMeanCurvature);
    ASSERT_TRUE(liveMean.IsValid());
    ASSERT_FALSE(liveMean.Vector().empty());
    const double publishedMean = liveMean.Vector().front();
    liveMean.Vector().front() = publishedMean + 1.0;
    const Runtime::EditorCommandHistorySnapshot beforePropertyRejection =
        history.Snapshot();
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_DOUBLE_EQ(
        properties.Get<double>(PN::kMeanCurvature).Vector().front(),
        publishedMean + 1.0);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_EQ(history.Snapshot().Revision, beforePropertyRejection.Revision);

    liveMean = properties.Get<double>(PN::kMeanCurvature);
    ASSERT_TRUE(liveMean.IsValid());
    liveMean.Vector().front() = publishedMean;
    std::vector<glm::vec3> interveningPositions =
        MeshVertexPositions(registry, mesh);
    interveningPositions.front().x += 1.0f;
    SetPositions(registry.Raw().get<GS::Vertices>(mesh), interveningPositions);
    const Runtime::EditorCommandHistorySnapshot beforePositionRejection =
        history.Snapshot();
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(registry, mesh),
        interveningPositions);
    EXPECT_TRUE(properties.Get<double>(PN::kMeanCurvature).IsValid());
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_EQ(history.Snapshot().Revision, beforePositionRejection.Revision);

    context.LastMeshCurvatureResult = &result;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshCurvatureAvailable);
    EXPECT_TRUE(model.Processing.MeshCurvatureDirectionsAvailable);
    ASSERT_TRUE(model.Processing.LastMeshCurvatureResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshCurvatureResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshCurvatureResult->ScalarWrittenCount, 8u);
}
TEST(SandboxEditorUi, MeshCurvatureRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorMeshCurvatureResult> completedResult{};
    context.MethodResultSinks.MeshCurvature =
        [&completedResult](Runtime::EditorMeshCurvatureResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedCurvature");
    AddDenoiseTetraMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    ASSERT_FALSE(properties.Exists(PN::kMeanCurvature));
    ASSERT_FALSE(properties.Exists(PN::kGaussianCurvature));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshCurvatureResult result =
        Runtime::ApplyEditorMeshCurvatureCommand(
            context,
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId = stableId,
                .Output = Runtime::EditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(result.VertexSlotCount, 4u);
    EXPECT_TRUE(result.DirectionsRequested);
    EXPECT_TRUE(result.DirectionsAvailable);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshCurvature.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races
    // between Queued/Running/AwaitingGate; assert only that it is active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->VertexSlotCount, 4u);
    EXPECT_EQ(completedResult->ScalarPropertyCount, 2u);
    EXPECT_EQ(completedResult->ScalarWrittenCount, 8u);
    EXPECT_EQ(completedResult->DirectionPropertyCount, 2u);
    EXPECT_EQ(completedResult->DirectionWrittenCount, 8u);
    EXPECT_TRUE(completedResult->DirectionsPublished);
    EXPECT_TRUE(properties.Get<double>(PN::kMeanCurvature).IsValid());
    EXPECT_TRUE(properties.Get<double>(PN::kGaussianCurvature).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir1).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir2).IsValid());
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(history.IsDirty());
    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
}
TEST(SandboxEditorUi, MeshCurvatureDerivedJobDiscardsStalePropertiesBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshCurvature =
        [&completedSinkCalled](Runtime::EditorMeshCurvatureResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "StaleCurvatureProperties");
    AddDenoiseTetraMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    auto mean = properties.GetOrAdd<double>(
        std::string{PN::kMeanCurvature},
        0.0);
    ASSERT_TRUE(mean.IsValid());
    ASSERT_EQ(mean.Vector().size(), 4u);
    for (double& value : mean.Vector())
        value = 1.0;

    const Runtime::EditorMeshCurvatureResult result =
        Runtime::ApplyEditorMeshCurvatureCommand(
            context,
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Output = Runtime::EditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });
    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

    auto currentMean = properties.Get<double>(PN::kMeanCurvature);
    ASSERT_TRUE(currentMean.IsValid());
    for (double& value : currentMean.Vector())
        value = 2.0;

    ASSERT_TRUE(jobs.DrainUntilTerminal());

    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    currentMean = properties.Get<double>(PN::kMeanCurvature);
    ASSERT_TRUE(currentMean.IsValid());
    for (const double value : currentMean.Vector())
        EXPECT_DOUBLE_EQ(value, 2.0);
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
}
TEST(SandboxEditorUi, MeshCurvatureCommandFallsBackToScalarOnlyWhenDirectionsUnavailable)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.MeshCurvatureDirectionsAvailable = false;

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CurvatureScalarOnlyMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const Runtime::EditorMeshCurvatureResult result =
        Runtime::ApplyEditorMeshCurvatureCommand(
            context,
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Output = Runtime::EditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(result.DirectionsRequested);
    EXPECT_FALSE(result.DirectionsAvailable);
    EXPECT_FALSE(result.DirectionsPublished);
    EXPECT_EQ(result.ScalarPropertyCount, 2u);
    EXPECT_EQ(result.ScalarWrittenCount, 8u);
    EXPECT_EQ(result.DirectionWrittenCount, 0u);
    EXPECT_NE(result.Message.find("not published"), std::string::npos);

    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    EXPECT_TRUE(properties.Get<double>(PN::kMeanCurvature).IsValid());
    EXPECT_TRUE(properties.Get<double>(PN::kGaussianCurvature).IsValid());
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshCurvatureAvailable);
    EXPECT_FALSE(model.Processing.MeshCurvatureDirectionsAvailable);
}
TEST(SandboxEditorUi, MeshCurvatureCommandFailsClosedForInvalidTargetsAndConflicts)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::EditorMeshCurvatureResult missingScene =
        Runtime::ApplyEditorMeshCurvatureCommand(
            Intrinsic::Tests::EditorFeatureTestContext{},
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::EditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "CurvatureWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    const Runtime::EditorMeshCurvatureResult wrongDomain =
        Runtime::ApplyEditorMeshCurvatureCommand(
            context,
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CurvatureUnavailableMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    context.MeshCurvatureKernelAvailable = false;
    const Runtime::EditorMeshCurvatureResult unavailable =
        Runtime::ApplyEditorMeshCurvatureCommand(
            context,
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailable.Error, Core::ErrorCode::InvalidState);
    EXPECT_FALSE(registry.Raw().get<GS::Vertices>(mesh).Properties.Exists(
        PN::kMeanCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    context.MeshCurvatureKernelAvailable = true;
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    (void)properties.Add<float>(std::string{PN::kMeanCurvature}, 0.0f);
    const Runtime::EditorMeshCurvatureResult conflict =
        Runtime::ApplyEditorMeshCurvatureCommand(
            context,
            Runtime::EditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(conflict.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(conflict.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
}
TEST(SandboxEditorUi, MeshRemeshCommandReplacesTopologyAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "UniformRemesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Vertices, 0u);
    ASSERT_GT(before.Faces, 0u);

    const Runtime::EditorMeshRemeshResult uniform =
        Runtime::ApplyEditorMeshRemeshCommand(
            context,
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId = stableId,
                .Mode = Runtime::EditorMeshRemeshMode::Uniform,
                .SizingLaw =
                    Runtime::EditorMeshRemeshSizingLaw::MeanCurvature,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .PreserveBoundary = false,
                .ProjectToSurface = true,
            });

    ASSERT_TRUE(uniform.Succeeded()) << uniform.Message;
    EXPECT_EQ(uniform.Mode, Runtime::EditorMeshRemeshMode::Uniform);
    EXPECT_TRUE(uniform.ProjectToSurface);
    EXPECT_EQ(uniform.IterationsRequested, 1u);
    EXPECT_EQ(uniform.IterationsPerformed, 1u);
    EXPECT_EQ(uniform.InputVertexCount, before.Vertices);
    EXPECT_EQ(uniform.InputFaceCount, before.Faces);
    EXPECT_GT(uniform.OutputVertexCount, before.Vertices);
    EXPECT_GT(uniform.OutputFaceCount, before.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(history.CanUndo());

    const MeshCounts afterUniform = SourceMeshCounts(registry, mesh);
    EXPECT_EQ(afterUniform.Vertices, uniform.OutputVertexCount);
    EXPECT_EQ(afterUniform.Faces, uniform.OutputFaceCount);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), afterUniform);

    auto nextHalfedge =
        registry.Raw()
            .get<GS::Halfedges>(mesh)
            .Properties.Get<std::uint32_t>(PN::kHalfedgeNext);
    ASSERT_TRUE(nextHalfedge.IsValid());
    ASSERT_FALSE(nextHalfedge.Vector().empty());
    const std::uint32_t publishedNext = nextHalfedge.Vector().front();
    const std::uint32_t interveningNext =
        publishedNext == 0u ? 1u : 0u;
    nextHalfedge.Vector().front() = interveningNext;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        history.Snapshot();
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(
        registry.Raw()
            .get<GS::Halfedges>(mesh)
            .Properties.Get<std::uint32_t>(PN::kHalfedgeNext)
            .Vector()
            .front(),
        interveningNext);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), afterUniform);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_EQ(history.Snapshot().Revision, beforeRejectedUndo.Revision);
    nextHalfedge =
        registry.Raw()
            .get<GS::Halfedges>(mesh)
            .Properties.Get<std::uint32_t>(PN::kHalfedgeNext);
    ASSERT_TRUE(nextHalfedge.IsValid());
    nextHalfedge.Vector().front() = publishedNext;

    context.LastMeshRemeshResult = &uniform;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshRemeshAvailable);
    ASSERT_TRUE(model.Processing.LastMeshRemeshResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshRemeshResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshRemeshResult->OutputFaceCount,
              uniform.OutputFaceCount);

    const ECS::EntityHandle adaptiveMesh =
        MakeSelectable(registry, "AdaptiveRemesh");
    AddIcosahedronMeshSource(registry, adaptiveMesh);
    const Runtime::EditorMeshRemeshResult adaptive =
        Runtime::ApplyEditorMeshRemeshCommand(
            context,
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(adaptiveMesh),
                .Mode = Runtime::EditorMeshRemeshMode::Adaptive,
                .SizingLaw =
                    Runtime::EditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .ApproximationError = 0.01,
                .PreserveBoundary = false,
                .ProjectToSurface = false,
            });
    ASSERT_TRUE(adaptive.Succeeded()) << adaptive.Message;
    EXPECT_EQ(adaptive.Mode, Runtime::EditorMeshRemeshMode::Adaptive);
    EXPECT_EQ(adaptive.SizingLaw,
              Runtime::EditorMeshRemeshSizingLaw::ErrorBoundedTaubin);
    EXPECT_EQ(adaptive.IterationsPerformed, 1u);
    EXPECT_GT(adaptive.OutputVertexCount, 0u);
    EXPECT_GT(adaptive.OutputFaceCount, 0u);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(adaptiveMesh));
}
TEST(SandboxEditorUi, MeshRemeshRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorMeshRemeshResult> completedResult{};
    context.MethodResultSinks.MeshRemesh =
        [&completedResult](Runtime::EditorMeshRemeshResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedRemesh");
    AddIcosahedronMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshRemeshResult result =
        Runtime::ApplyEditorMeshRemeshCommand(
            context,
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId = stableId,
                .Mode = Runtime::EditorMeshRemeshMode::Uniform,
                .SizingLaw =
                    Runtime::EditorMeshRemeshSizingLaw::MeanCurvature,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .PreserveBoundary = false,
                .ProjectToSurface = false,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshRemesh.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races
    // between Queued/Running/AwaitingGate; assert only that it is active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(completedResult.has_value());

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputFaceCount, before.Faces);
    EXPECT_GT(completedResult->OutputFaceCount, before.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(SourceMeshCounts(registry, mesh).Faces,
              completedResult->OutputFaceCount);
}
TEST(SandboxEditorUi, MeshSubdivideCommandReplacesTopologyForAllOperatorsAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle loopMesh = MakeSelectable(registry, "LoopSubdivide");
    AddDenoiseTetraMeshSource(registry, loopMesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, loopMesh));
    const MeshCounts beforeLoop = SourceMeshCounts(registry, loopMesh);
    const std::uint32_t loopStableId =
        Runtime::SelectionController::ToStableEntityId(loopMesh);

    const Runtime::EditorMeshSubdivideResult loop =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = loopStableId,
                .Operator = Runtime::EditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
                .PreserveLoopFeatureEdges = true,
            });

    ASSERT_TRUE(loop.Succeeded()) << loop.Message;
    EXPECT_EQ(loop.Operator,
              Runtime::EditorMeshSubdivideOperator::Loop);
    EXPECT_TRUE(loop.PreserveLoopFeatureEdges);
    EXPECT_EQ(loop.IterationsPerformed, 1u);
    EXPECT_EQ(loop.InputVertexCount, beforeLoop.Vertices);
    EXPECT_EQ(loop.InputFaceCount, beforeLoop.Faces);
    EXPECT_GT(loop.OutputVertexCount, beforeLoop.Vertices);
    EXPECT_GT(loop.OutputFaceCount, beforeLoop.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(loopMesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(loopMesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(loopMesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(loopMesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(loopMesh));
    EXPECT_TRUE(history.IsDirty());

    const MeshCounts afterLoop = SourceMeshCounts(registry, loopMesh);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, loopMesh), beforeLoop);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, loopMesh), afterLoop);

    context.LastMeshSubdivideResult = &loop;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshSubdivideAvailable);
    ASSERT_TRUE(model.Processing.LastMeshSubdivideResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshSubdivideResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshSubdivideResult->OutputFaceCount,
              loop.OutputFaceCount);

    const ECS::EntityHandle catmullMesh =
        MakeSelectable(registry, "CatmullClarkSubdivide");
    AddDenoiseTetraMeshSource(registry, catmullMesh);
    const Runtime::EditorMeshSubdivideResult catmull =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(catmullMesh),
                .Operator =
                    Runtime::EditorMeshSubdivideOperator::CatmullClark,
                .Iterations = 1u,
            });
    ASSERT_TRUE(catmull.Succeeded()) << catmull.Message;
    EXPECT_EQ(catmull.Operator,
              Runtime::EditorMeshSubdivideOperator::CatmullClark);
    EXPECT_GT(catmull.OutputVertexCount, catmull.InputVertexCount);
    EXPECT_GT(catmull.OutputFaceCount, catmull.InputFaceCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(catmullMesh));

    const ECS::EntityHandle sqrt3Mesh =
        MakeSelectable(registry, "Sqrt3Subdivide");
    AddDenoiseTetraMeshSource(registry, sqrt3Mesh);
    const Runtime::EditorMeshSubdivideResult sqrt3 =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(sqrt3Mesh),
                .Operator = Runtime::EditorMeshSubdivideOperator::Sqrt3,
                .Iterations = 1u,
            });
    ASSERT_TRUE(sqrt3.Succeeded()) << sqrt3.Message;
    EXPECT_EQ(sqrt3.Operator,
              Runtime::EditorMeshSubdivideOperator::Sqrt3);
    EXPECT_GT(sqrt3.OutputVertexCount, sqrt3.InputVertexCount);
    EXPECT_GT(sqrt3.OutputFaceCount, sqrt3.InputFaceCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(sqrt3Mesh));
}
TEST(SandboxEditorUi, MeshSubdivideRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorMeshSubdivideResult> completedResult{};
    context.MethodResultSinks.MeshSubdivide =
        [&completedResult](Runtime::EditorMeshSubdivideResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedSubdivide");
    AddDenoiseTetraMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshSubdivideResult result =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = stableId,
                .Operator = Runtime::EditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshSubdivide.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races
    // between Queued/Running/AwaitingGate; assert only that it is active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    EXPECT_FALSE(completedResult.has_value());
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputFaceCount, before.Faces);
    EXPECT_GT(completedResult->OutputFaceCount, before.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(SourceMeshCounts(registry, mesh).Faces,
              completedResult->OutputFaceCount);
    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
}
TEST(SandboxEditorUi, MeshSubdivideDerivedJobDiscardsStaleMeshBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshSubdivide =
        [&completedSinkCalled](Runtime::EditorMeshSubdivideResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "StaleSubdivide");
    AddDenoiseTetraMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshSubdivideResult result =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = stableId,
                .Operator = Runtime::EditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
            });
    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

    const std::vector<glm::vec3> stalePositions{
        glm::vec3{2.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 2.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 2.0f},
        glm::vec3{2.0f, 2.0f, 2.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(mesh), stalePositions);

    ASSERT_TRUE(jobs.DrainUntilTerminal());

    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh),
                                stalePositions);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
}
TEST(SandboxEditorUi, MeshSubdivideDerivedJobDiscardsStaleTopologyBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshSubdivide =
        [&completedSinkCalled](Runtime::EditorMeshSubdivideResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "StaleSubdivideTopology");
    AddDenoiseTetraMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const Runtime::EditorMeshSubdivideResult result =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Operator = Runtime::EditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
            });
    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

    auto nextHalfedge =
        registry.Raw()
            .get<GS::Halfedges>(mesh)
            .Properties.Get<std::uint32_t>(PN::kHalfedgeNext);
    ASSERT_TRUE(nextHalfedge.IsValid());
    ASSERT_FALSE(nextHalfedge.Vector().empty());
    const std::uint32_t staleNext =
        nextHalfedge.Vector().front() == 0u ? 1u : 0u;
    nextHalfedge.Vector().front() = staleNext;

    ASSERT_TRUE(jobs.DrainUntilTerminal());

    const Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_EQ(
        registry.Raw()
            .get<GS::Halfedges>(mesh)
            .Properties.Get<std::uint32_t>(PN::kHalfedgeNext)
            .Vector()
            .front(),
        staleNext);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
}
TEST(SandboxEditorUi, MeshSimplifyCommandReducesFaceCountAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "SimplifyMesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 12u);

    const Runtime::EditorMeshSimplifyResult simplified =
        Runtime::ApplyEditorMeshSimplifyCommand(
            context,
            Runtime::EditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::EditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });

    ASSERT_TRUE(simplified.Succeeded()) << simplified.Message;
    EXPECT_EQ(simplified.Metric,
              Runtime::EditorMeshSimplifyMetric::FA_QEM);
    EXPECT_EQ(simplified.TargetFaces, 12u);
    EXPECT_EQ(simplified.InputVertexCount, before.Vertices);
    EXPECT_EQ(simplified.InputFaceCount, before.Faces);
    EXPECT_LT(simplified.OutputFaceCount, before.Faces);
    EXPECT_GT(simplified.CollapseCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(history.CanUndo());

    const MeshCounts afterSimplify = SourceMeshCounts(registry, mesh);
    EXPECT_EQ(afterSimplify.Vertices, simplified.OutputVertexCount);
    EXPECT_EQ(afterSimplify.Faces, simplified.OutputFaceCount);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), afterSimplify);

    context.LastMeshSimplifyResult = &simplified;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshSimplifyAvailable);
    ASSERT_TRUE(model.Processing.LastMeshSimplifyResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshSimplifyResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshSimplifyResult->OutputFaceCount,
              simplified.OutputFaceCount);

    const ECS::EntityHandle classicalMesh =
        MakeSelectable(registry, "SimplifyClassical");
    AddIcosahedronMeshSource(registry, classicalMesh);
    const Runtime::EditorMeshSimplifyResult classical =
        Runtime::ApplyEditorMeshSimplifyCommand(
            context,
            Runtime::EditorMeshSimplifyCommand{
                .StableEntityId = Runtime::SelectionController::ToStableEntityId(
                    classicalMesh),
                .Metric =
                    Runtime::EditorMeshSimplifyMetric::ClassicalQEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });
    ASSERT_TRUE(classical.Succeeded()) << classical.Message;
    EXPECT_EQ(classical.Metric,
              Runtime::EditorMeshSimplifyMetric::ClassicalQEM);
    EXPECT_LT(classical.OutputFaceCount, before.Faces);
    EXPECT_EQ(classical.SharpFeatureVerticesPinned, 0u);
    EXPECT_EQ(classical.SeamVerticesPinned, 0u);
}
TEST(SandboxEditorUi, MeshSimplifyRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorMeshSimplifyResult> completedResult{};
    context.MethodResultSinks.MeshSimplify =
        [&completedResult](Runtime::EditorMeshSimplifyResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedSimplify");
    AddIcosahedronMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 12u);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshSimplifyResult result =
        Runtime::ApplyEditorMeshSimplifyCommand(
            context,
            Runtime::EditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::EditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshSimplify.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races
    // between Queued/Running/AwaitingGate; assert only that it is active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(completedResult.has_value());

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputFaceCount, before.Faces);
    EXPECT_LT(completedResult->OutputFaceCount, before.Faces);
    EXPECT_GT(completedResult->CollapseCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(SourceMeshCounts(registry, mesh).Faces,
              completedResult->OutputFaceCount);
}
TEST(SandboxEditorUi, MeshSimplifyCommandFailsClosedForInvalidTargetsAndUnavailableKernel)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "SimplifyGuard");
    AddIcosahedronMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshSimplifyResult invalid =
        Runtime::ApplyEditorMeshSimplifyCommand(
            context,
            Runtime::EditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .TargetFaces = 0u,
                .MaxError = 0.0,
            });
    EXPECT_EQ(invalid.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(invalid.Succeeded());

    const Runtime::EditorMeshSimplifyResult stale =
        Runtime::ApplyEditorMeshSimplifyCommand(
            context,
            Runtime::EditorMeshSimplifyCommand{
                .StableEntityId = stableId + 4242u,
                .TargetFaces = 8u,
            });
    EXPECT_EQ(stale.Status, Runtime::EditorCommandStatus::StaleEntity);

    context.MeshSimplifyKernelAvailable = false;
    const Runtime::EditorMeshSimplifyResult unavailable =
        Runtime::ApplyEditorMeshSimplifyCommand(
            context,
            Runtime::EditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .TargetFaces = 8u,
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
}
TEST(SandboxEditorUi, MeshSimplifyPreservesUvSeamsWhenTexcoordsPresent)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    constexpr int kGrid = 4;
    Geometry::HalfedgeMesh::Mesh grid = MakeGridPlaneMesh(kGrid);
    const ECS::EntityHandle mesh = MakeSelectable(registry, "TexturedGrid");
    GS::PopulateFromMesh(registry.Raw(), mesh, grid);
    registry.Raw().emplace<G::RenderSurface>(mesh);
    // The GeometrySources must carry the texcoords the command forwards into the
    // scratch halfedge mesh so FA_QEM can pin the boundary UV-seam vertices.
    SetTexcoords(registry.Raw().get<GS::Vertices>(mesh),
                 GridPlaneTexcoords(kGrid));

    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 4u);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshSimplifyResult result =
        Runtime::ApplyEditorMeshSimplifyCommand(
            context,
            Runtime::EditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::EditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 4u,
                .PreserveBoundary = false,  // seams pinned by PreserveUvSeams
                .PreserveSharpFeatures = true,
                .PreserveUvSeams = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    // Without forwarding v:texcoord the scratch mesh carries no texcoord and
    // SeamVerticesPinned would be 0; the fix forwards it so the boundary UV-seam
    // vertices are pinned.
    EXPECT_GT(result.SeamVerticesPinned, 0u);
    EXPECT_LT(result.OutputFaceCount, before.Faces);
}
TEST(SandboxEditorUi, MeshTopologyProcessingCommandsFailClosedForInvalidTargetsAndUnavailableKernels)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::EditorMeshRemeshResult missingRemeshScene =
        Runtime::ApplyEditorMeshRemeshCommand(
            Intrinsic::Tests::EditorFeatureTestContext{},
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingRemeshScene.Status,
              Runtime::EditorCommandStatus::MissingScene);
    EXPECT_EQ(missingRemeshScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::EditorMeshSubdivideResult missingSubdivideScene =
        Runtime::ApplyEditorMeshSubdivideCommand(
            Intrinsic::Tests::EditorFeatureTestContext{},
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingSubdivideScene.Status,
              Runtime::EditorCommandStatus::MissingScene);
    EXPECT_EQ(missingSubdivideScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "TopologyWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    const Runtime::EditorMeshRemeshResult wrongRemeshDomain =
        Runtime::ApplyEditorMeshRemeshCommand(
            context,
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId = cloudStableId,
            });
    EXPECT_EQ(wrongRemeshDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
    const Runtime::EditorMeshSubdivideResult wrongSubdivideDomain =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = cloudStableId,
            });
    EXPECT_EQ(wrongSubdivideDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TopologyFailMesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);

    const Runtime::EditorMeshRemeshResult invalidRemesh =
        Runtime::ApplyEditorMeshRemeshCommand(
            context,
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Iterations = 0u,
            });
    EXPECT_EQ(invalidRemesh.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    const Runtime::EditorMeshSubdivideResult invalidSubdivide =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = meshStableId,
                .Iterations = 0u,
            });
    EXPECT_EQ(invalidSubdivide.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshAdaptiveKernelAvailable = false;
    const Runtime::EditorMeshRemeshResult unavailableAdaptive =
        Runtime::ApplyEditorMeshRemeshCommand(
            context,
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Mode = Runtime::EditorMeshRemeshMode::Adaptive,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
            });
    EXPECT_EQ(unavailableAdaptive.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailableAdaptive.Error, Core::ErrorCode::InvalidState);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshAdaptiveKernelAvailable = true;
    context.MeshRemeshErrorBoundedSizingAvailable = false;
    const Runtime::EditorMeshRemeshResult unavailableSizing =
        Runtime::ApplyEditorMeshRemeshCommand(
            context,
            Runtime::EditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Mode = Runtime::EditorMeshRemeshMode::Adaptive,
                .SizingLaw =
                    Runtime::EditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .ApproximationError = 0.01,
            });
    EXPECT_EQ(unavailableSizing.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshErrorBoundedSizingAvailable = true;
    context.MeshSubdivideSqrt3KernelAvailable = false;
    const Runtime::EditorMeshSubdivideResult unavailableSqrt3 =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = meshStableId,
                .Operator = Runtime::EditorMeshSubdivideOperator::Sqrt3,
                .Iterations = 1u,
            });
    EXPECT_EQ(unavailableSqrt3.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailableSqrt3.Error, Core::ErrorCode::InvalidState);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    context.MeshRemeshUniformKernelAvailable = false;
    context.MeshRemeshAdaptiveKernelAvailable = false;
    context.MeshSubdivideLoopKernelAvailable = false;
    context.MeshSubdivideCatmullClarkKernelAvailable = false;
    const Runtime::EditorDomainWindowModel unavailableModel =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_FALSE(unavailableModel.Processing.MeshRemeshAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshRemeshUniformAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshRemeshAdaptiveAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideLoopAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideCatmullClarkAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideSqrt3Available);
}
TEST(SandboxEditorUi, MeshVertexNormalsCommandPublishesCanonicalNormalsForAllWeightings)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "NormalMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    constexpr std::array<GN::AveragingMode, 4> kWeightings{{
        GN::AveragingMode::UniformFace,
        GN::AveragingMode::AreaWeighted,
        GN::AveragingMode::AngleWeighted,
        GN::AveragingMode::MaxWeighted,
    }};

    Runtime::EditorMeshVertexNormalsResult lastResult{};
    for (const GN::AveragingMode weighting : kWeightings)
    {
        registry.Raw().remove<Dirty::GpuDirty,
                              Dirty::DirtyVertexPositions,
                              Dirty::DirtyVertexAttributes,
                              Dirty::DirtyVertexTexcoords,
                              Dirty::DirtyVertexNormals,
                              Dirty::DirtyVertexColors,
                              Dirty::DirtyFaceTopology,
                              Dirty::DirtyEdgeTopology>(mesh);

        lastResult = Runtime::ApplyEditorMeshVertexNormalsCommand(
            context,
            Runtime::EditorMeshVertexNormalsCommand{
                .StableEntityId = stableId,
                .Weighting = weighting,
            });

        ASSERT_TRUE(lastResult.Succeeded())
            << lastResult.Message;
        EXPECT_EQ(lastResult.NormalStatus, GN::RecomputeStatus::Success);
        EXPECT_EQ(lastResult.Weighting, weighting);
        EXPECT_EQ(lastResult.VertexSlotCount, 3u);
        EXPECT_EQ(lastResult.WrittenCount, 3u);
        EXPECT_EQ(lastResult.ProcessedFaceCount, 1u);
        EXPECT_EQ(lastResult.FallbackVertexCount, 0u);
        EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexTexcoords>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexColors>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));

        auto normals = registry.Raw()
                           .get<GS::Vertices>(mesh)
                           .Properties.Get<glm::vec3>(PN::kNormal);
        ASSERT_TRUE(normals);
        ASSERT_EQ(normals.Vector().size(), 3u);
        for (const glm::vec3 normal : normals.Vector())
        {
            EXPECT_NEAR(normal.x, 0.0f, 1.0e-5f);
            EXPECT_NEAR(normal.y, 0.0f, 1.0e-5f);
            EXPECT_NEAR(normal.z, 1.0f, 1.0e-5f);
        }
    }

    EXPECT_TRUE(history.IsDirty());
    context.LastMeshVertexNormalsResult = &lastResult;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    ASSERT_TRUE(model.Processing.MeshVertexNormalsAvailable);
    ASSERT_TRUE(model.Processing.LastMeshVertexNormalsResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshVertexNormalsResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshVertexNormalsResult->WrittenCount, 3u);
}
TEST(SandboxEditorUi, MeshVertexNormalsRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorMeshVertexNormalsResult>
        completedResult{};
    context.MethodResultSinks.MeshVertexNormals =
        [&completedResult](
            Runtime::EditorMeshVertexNormalsResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedNormalsMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    ASSERT_FALSE(properties.Exists(PN::kNormal));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::EditorMeshVertexNormalsResult result =
        Runtime::ApplyEditorMeshVertexNormalsCommand(
            context,
            Runtime::EditorMeshVertexNormalsCommand{
                .StableEntityId = stableId,
                .Weighting = GN::AveragingMode::AreaWeighted,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(result.VertexSlotCount, 3u);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(properties.Exists(PN::kNormal));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshVertexNormals.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races;
    // assert only that the job is still active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(properties.Exists(PN::kNormal));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->NormalStatus, GN::RecomputeStatus::Success);
    EXPECT_EQ(completedResult->VertexSlotCount, 3u);
    EXPECT_EQ(completedResult->WrittenCount, 3u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(history.IsDirty());

    auto normals = properties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(normals);
    ASSERT_EQ(normals.Vector().size(), 3u);
    for (const glm::vec3 normal : normals.Vector())
    {
        EXPECT_NEAR(normal.x, 0.0f, 1.0e-5f);
        EXPECT_NEAR(normal.y, 0.0f, 1.0e-5f);
        EXPECT_NEAR(normal.z, 1.0f, 1.0e-5f);
    }

    const std::vector<glm::vec3> publishedNormals = normals.Vector();
    registry.Raw().remove<Dirty::DirtyVertexNormals>(mesh);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists(PN::kNormal));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    registry.Raw().remove<Dirty::DirtyVertexNormals>(mesh);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    auto redoneNormals = properties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(redoneNormals);
    EXPECT_EQ(redoneNormals.Vector(), publishedNormals);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    redoneNormals.Vector()[0].x = 0.25f;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        history.Snapshot();
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_FLOAT_EQ(redoneNormals.Vector()[0].x, 0.25f);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_EQ(history.Snapshot().Revision, beforeRejectedUndo.Revision);

    redoneNormals.Vector() = publishedNormals;
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists(PN::kNormal));
}
TEST(SandboxEditorUi,
     GraphAndPointCloudVertexNormalsRequestsQueueDerivedJobsAndPublishOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorGraphVertexNormalsResult>
        completedGraph{};
    std::optional<Runtime::EditorPointCloudVertexNormalsResult>
        completedCloud{};
    context.MethodResultSinks.GraphVertexNormals =
        [&completedGraph](
            Runtime::EditorGraphVertexNormalsResult result)
        {
            completedGraph = std::move(result);
        };
    context.MethodResultSinks.PointCloudVertexNormals =
        [&completedCloud](
            Runtime::EditorPointCloudVertexNormalsResult result)
        {
            completedCloud = std::move(result);
        };

    const ECS::EntityHandle graph = MakeSelectable(registry, "QueuedGraphNormals");
    AddPlanarCycleGraphSource(registry, graph);
    auto& graphProperties = registry.Raw().get<GS::Nodes>(graph).Properties;
    ASSERT_FALSE(graphProperties.Exists(PN::kNormal));

    const Runtime::EditorGraphVertexNormalsResult graphResult =
        Runtime::ApplyEditorGraphVertexNormalsCommand(
            context,
            Runtime::EditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(graph),
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
                .OrientTowardFallback = true,
            });

    EXPECT_EQ(graphResult.Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(graphResult.VertexSlotCount, 4u);
    EXPECT_EQ(graphResult.EdgeSlotCount, 4u);
    EXPECT_FALSE(graphProperties.Exists(PN::kNormal));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.GraphVertexNormals.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races;
    // assert only that the job is still active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    EXPECT_FALSE(completedGraph.has_value());
    EXPECT_FALSE(graphProperties.Exists(PN::kNormal));
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    ASSERT_TRUE(completedGraph.has_value());
    EXPECT_TRUE(completedGraph->Succeeded()) << completedGraph->Message;
    EXPECT_EQ(completedGraph->WrittenCount, 4u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));
    auto graphNormals = graphProperties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(graphNormals);
    ASSERT_EQ(graphNormals.Vector().size(), 4u);
    for (const glm::vec3 normal : graphNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.9f);
    }

    const ECS::EntityHandle cloud = MakeSelectable(registry, "QueuedCloudNormals");
    AddPointCloudSource(registry, cloud, 9u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {-1.0f, -1.0f, 0.0f},
                     {0.0f, -1.0f, 0.0f},
                     {1.0f, -1.0f, 0.0f},
                     {-1.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {-1.0f, 1.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });
    auto& cloudProperties = registry.Raw().get<GS::Vertices>(cloud).Properties;
    ASSERT_FALSE(cloudProperties.Exists(PN::kNormal));

    const Runtime::EditorPointCloudVertexNormalsResult cloudResult =
        Runtime::ApplyEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::EditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .KNeighbors = 4u,
                .MinimumNeighbors = 2u,
                .UseRadiusSearch = false,
                .Orientation = PCN::OrientationMode::MinimumSpanningTree,
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
            });

    EXPECT_EQ(cloudResult.Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(cloudResult.PointSlotCount, 9u);
    EXPECT_FALSE(cloudProperties.Exists(PN::kNormal));

    queued = jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 2u);
    EXPECT_EQ(queued.Entries[1].Name, "Sandbox.PointCloudVertexNormals.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races;
    // assert only that the job is still active.
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[1].State));

    EXPECT_FALSE(completedCloud.has_value());
    EXPECT_FALSE(cloudProperties.Exists(PN::kNormal));
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    ASSERT_TRUE(completedCloud.has_value());
    EXPECT_TRUE(completedCloud->Succeeded()) << completedCloud->Message;
    EXPECT_EQ(completedCloud->PointSlotCount, 9u);
    EXPECT_EQ(completedCloud->WrittenCount, 9u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    auto cloudNormals = cloudProperties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(cloudNormals);
    ASSERT_EQ(cloudNormals.Vector().size(), 9u);
    for (const glm::vec3 normal : cloudNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.5f);
    }
    EXPECT_TRUE(history.IsDirty());

    registry.Raw().remove<Dirty::DirtyVertexNormals>(graph);
    registry.Raw().remove<Dirty::DirtyVertexNormals>(cloud);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(cloudProperties.Exists(PN::kNormal));
    EXPECT_TRUE(graphProperties.Exists(PN::kNormal));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(graphProperties.Exists(PN::kNormal));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));

    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_TRUE(graphProperties.Exists(PN::kNormal));
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_TRUE(cloudProperties.Exists(PN::kNormal));
}
TEST(SandboxEditorUi, VertexNormalsDerivedJobsDiscardStaleSourcesBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    {
        Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
        bool completedSinkCalled = false;
        context.MethodResultSinks.MeshVertexNormals =
            [&completedSinkCalled](
                Runtime::EditorMeshVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle mesh =
            MakeSelectable(registry, "StaleMeshNormals");
        AddTriangleMeshSource(registry, mesh);
        const Runtime::EditorMeshVertexNormalsResult result =
            Runtime::ApplyEditorMeshVertexNormalsCommand(
                context,
                Runtime::EditorMeshVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(mesh),
                });
        ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

        SetPositions(registry.Raw().get<GS::Vertices>(mesh),
                     {
                         {10.0f, 0.0f, 0.0f},
                         {11.0f, 0.0f, 0.0f},
                         {12.0f, 0.0f, 0.0f},
                     });

    ASSERT_TRUE(jobs.DrainUntilTerminal());
        Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
        ASSERT_EQ(done.Entries.size(), 1u);
        EXPECT_EQ(done.Entries[0].State,
                  Runtime::JobState::StaleDiscarded);
        EXPECT_FALSE(completedSinkCalled);
        EXPECT_FALSE(registry.Raw().get<GS::Vertices>(mesh)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    }

    {
        Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
        bool completedSinkCalled = false;
        context.MethodResultSinks.GraphVertexNormals =
            [&completedSinkCalled](
                Runtime::EditorGraphVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle graph =
            MakeSelectable(registry, "StaleGraphNormals");
        AddPlanarCycleGraphSource(registry, graph);
        const Runtime::EditorGraphVertexNormalsResult result =
            Runtime::ApplyEditorGraphVertexNormalsCommand(
                context,
                Runtime::EditorGraphVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(graph),
                });
        ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

        auto edgeV0 = registry.Raw()
                          .get<GS::Edges>(graph)
                          .Properties.Get<std::uint32_t>(PN::kEdgeV0);
        ASSERT_TRUE(edgeV0);
        edgeV0.Vector()[0] = 2u;

    ASSERT_TRUE(jobs.DrainUntilTerminal());
        Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
        ASSERT_EQ(done.Entries.size(), 1u);
        EXPECT_EQ(done.Entries[0].State,
                  Runtime::JobState::StaleDiscarded);
        EXPECT_FALSE(completedSinkCalled);
        EXPECT_FALSE(registry.Raw().get<GS::Nodes>(graph)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));
    }

    {
        Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
        bool completedSinkCalled = false;
        context.MethodResultSinks.PointCloudVertexNormals =
            [&completedSinkCalled](
                Runtime::EditorPointCloudVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle cloud =
            MakeSelectable(registry, "StaleCloudNormals");
        AddPointCloudSource(registry, cloud, 4u);
        SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                     {
                         {0.0f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f},
                         {0.0f, 1.0f, 0.0f},
                         {1.0f, 1.0f, 0.0f},
                     });
        auto& cloudProperties =
            registry.Raw().get<GS::Vertices>(cloud).Properties;
        auto existingNormals = cloudProperties.GetOrAdd<glm::vec3>(
            std::string{PN::kNormal},
            glm::vec3{0.0f, 0.0f, 1.0f});
        existingNormals.Vector() = {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        };
        const Runtime::EditorPointCloudVertexNormalsResult result =
            Runtime::ApplyEditorPointCloudVertexNormalsCommand(
                context,
                Runtime::EditorPointCloudVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(cloud),
                    .KNeighbors = 3u,
                    .MinimumNeighbors = 2u,
                });
        ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

        existingNormals.Vector()[0] = glm::vec3{1.0f, 0.0f, 0.0f};

    ASSERT_TRUE(jobs.DrainUntilTerminal());
        Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
        ASSERT_EQ(done.Entries.size(), 1u);
        EXPECT_EQ(done.Entries[0].State,
                  Runtime::JobState::StaleDiscarded);
        EXPECT_FALSE(completedSinkCalled);
        const auto retainedNormals =
            cloudProperties.Get<glm::vec3>(PN::kNormal);
        ASSERT_TRUE(retainedNormals);
        EXPECT_EQ(retainedNormals.Vector()[0],
                  (glm::vec3{1.0f, 0.0f, 0.0f}));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    }
}
TEST(SandboxEditorUi,
     DirectMeshPostProcessDiscardsCompletionAfterGeometryEdit)
{
    TmpFile meshFile(
        "runtime_mesh_postprocess_stale_geometry.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 1 0 0\n"
        "vn 1 0 0\n"
        "vn 1 0 0\n"
        "f 1/1/1 2/2/2 3/3/3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        MakeDirectMeshPostProcessExitApplication());
    InitializeDirectMeshPostProcessEngine(engine);

    Runtime::JobService& jobs =
        RequiredEngineService<Runtime::JobService>(engine);
    DirectMeshPostProcessWorkerBarrier workerBarrier{};
    const Runtime::JobToken blocker =
        workerBarrier.Submit(jobs, engine.ActiveWorld());
    ASSERT_TRUE(blocker.IsValid());
    ASSERT_TRUE(workerBarrier.WaitUntilStarted());

    auto imported =
        RequiredEngineService<
            Extrinsic::Runtime::AssetWorkflowModule>(engine)
            .ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(
            *engine.Worlds().Get(engine.ActiveWorld()),
            GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(*meshEntity);

    Intrinsic::Tests::EditorFeatureTestContext context =
        MakeContext(
            *engine.Worlds().Get(engine.ActiveWorld()),
            *engine.Services().Find<Runtime::SelectionController>());
    Runtime::EditorCommandHistory& history =
        RequiredEngineService<Runtime::EditorCommandHistory>(engine);
    context.CommandHistory = &history;
    const Runtime::EditorMeshSubdivideResult edited =
        Runtime::ApplyEditorMeshSubdivideCommand(
            context,
            Runtime::EditorMeshSubdivideCommand{
                .StableEntityId = stableId,
                .Operator =
                    Runtime::EditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
                .PreserveLoopFeatureEdges = true,
            });
    ASSERT_TRUE(edited.Succeeded()) << edited.Message;

    ECS::Scene::Registry& scene =
        *engine.Worlds().Get(engine.ActiveWorld());
    const MeshCounts editedCounts = SourceMeshCounts(scene, *meshEntity);
    const std::vector<glm::vec3> editedPositions =
        MeshVertexPositions(scene, *meshEntity);
    const Runtime::EditorCommandHistorySnapshot editedHistory =
        history.Snapshot();
    ASSERT_GT(editedCounts.Vertices, 3u);
    ASSERT_GT(editedCounts.Faces, 1u);

    workerBarrier.Release();
    engine.Run();

    const Runtime::JobServiceStats stats = jobs.Stats();
    EXPECT_EQ(stats.StaleDiscardedJobs, 1u);
    ExpectMeshCountsEqual(
        SourceMeshCounts(scene, *meshEntity),
        editedCounts);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(scene, *meshEntity),
        editedPositions);

    const Runtime::EditorCommandHistorySnapshot finalHistory =
        history.Snapshot();
    EXPECT_EQ(finalHistory.Revision, editedHistory.Revision);
    EXPECT_EQ(finalHistory.UndoCount, editedHistory.UndoCount);
    EXPECT_EQ(finalHistory.RedoCount, editedHistory.RedoCount);
    const std::span<const std::uint32_t> selectedStableIds =
        RequiredEngineService<Runtime::SelectionController>(engine)
            .SelectedStableIds();
    ASSERT_EQ(selectedStableIds.size(), 1u);
    EXPECT_EQ(selectedStableIds.front(), stableId);

    engine.Shutdown();
}

TEST(SandboxEditorUi,
     DirectMeshPostProcessPreservesNewerUvAndSourceProperties)
{
    TmpFile meshFile(
        "runtime_mesh_postprocess_stale_properties.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/2 3/3/3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        MakeDirectMeshPostProcessExitApplication());
    InitializeDirectMeshPostProcessEngine(engine);

    Runtime::JobService& jobs =
        RequiredEngineService<Runtime::JobService>(engine);
    DirectMeshPostProcessWorkerBarrier workerBarrier{};
    const Runtime::JobToken blocker =
        workerBarrier.Submit(jobs, engine.ActiveWorld());
    ASSERT_TRUE(blocker.IsValid());
    ASSERT_TRUE(workerBarrier.WaitUntilStarted());

    auto imported =
        RequiredEngineService<
            Extrinsic::Runtime::AssetWorkflowModule>(engine)
            .ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(
            *engine.Worlds().Get(engine.ActiveWorld()),
            GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());

    ECS::Scene::Registry& scene =
        *engine.Worlds().Get(engine.ActiveWorld());
    auto& vertexProperties =
        scene.Raw().get<GS::Vertices>(*meshEntity).Properties;
    auto texcoords = vertexProperties.GetOrAdd<glm::vec2>(
        "v:texcoord", glm::vec2{0.0f});
    ASSERT_TRUE(texcoords);
    const std::vector<glm::vec2> expectedTexcoords{
        {0.125f, 0.875f},
        {0.625f, 0.375f},
        {0.9375f, 0.0625f},
    };
    texcoords.Vector() = expectedTexcoords;

    auto paint = vertexProperties.GetOrAdd<glm::vec4>(
        "v:bug095-paint", glm::vec4{0.0f});
    ASSERT_TRUE(paint);
    const std::vector<glm::vec4> expectedPaint{
        {0.25f, 0.5f, 0.75f, 1.0f},
        {0.75f, 0.25f, 0.5f, 1.0f},
        {0.5f, 0.75f, 0.25f, 1.0f},
    };
    paint.Vector() = expectedPaint;

    const MeshCounts expectedCounts =
        SourceMeshCounts(scene, *meshEntity);
    const std::vector<glm::vec3> expectedPositions =
        MeshVertexPositions(scene, *meshEntity);

    workerBarrier.Release();
    engine.Run();

    EXPECT_EQ(jobs.Stats().StaleDiscardedJobs, 1u);
    ExpectMeshCountsEqual(
        SourceMeshCounts(scene, *meshEntity),
        expectedCounts);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(scene, *meshEntity),
        expectedPositions);

    const GS::ConstSourceView finalView =
        GS::BuildConstView(scene.Raw(), *meshEntity);
    ASSERT_NE(finalView.VertexSource, nullptr);
    const auto finalTexcoords =
        finalView.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    const auto finalPaint =
        finalView.VertexSource->Properties.Get<glm::vec4>(
            "v:bug095-paint");
    ASSERT_TRUE(finalTexcoords);
    ASSERT_TRUE(finalPaint);
    ExpectTexcoordsExactlyEqual(
        finalTexcoords.Vector(),
        expectedTexcoords);
    ExpectColorsExactlyEqual(finalPaint.Vector(), expectedPaint);

    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(
        scene,
        RequiredEngineService<Runtime::SelectionController>(engine));
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_FALSE(model.Processing.DirectMeshEnrichmentPending);
    EXPECT_EQ(
        model.Processing.DirectMeshEnrichmentStatus,
        Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(
        model.Processing.DirectMeshEnrichmentDiagnostic.empty());
    EXPECT_FALSE(model.Processing.Entries.empty());
    EXPECT_TRUE(model.Processing.MeshDenoiseAvailable);
    EXPECT_TRUE(model.Processing.MeshRemeshAvailable);
    EXPECT_TRUE(model.Processing.MeshSubdivideAvailable);
    EXPECT_TRUE(model.Processing.MeshSimplifyAvailable);
    EXPECT_TRUE(model.Processing.MeshVertexNormalsAvailable);

    engine.Shutdown();
}

TEST(SandboxEditorUi, DirectMeshPostProcessDiscardsCompletionAfterBindingChange)
{
    TmpFile meshFile(
        "runtime_mesh_postprocess_stale_binding.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/2 3/3/3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        MakeDirectMeshPostProcessExitApplication());
    InitializeDirectMeshPostProcessEngine(engine);

    Runtime::JobService& jobs =
        RequiredEngineService<Runtime::JobService>(engine);
    DirectMeshPostProcessWorkerBarrier workerBarrier{};
    const Runtime::JobToken blocker =
        workerBarrier.Submit(jobs, engine.ActiveWorld());
    ASSERT_TRUE(blocker.IsValid());
    ASSERT_TRUE(workerBarrier.WaitUntilStarted());

    auto imported =
        RequiredEngineService<Runtime::AssetWorkflowModule>(engine)
            .ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    ECS::Scene::Registry& scene =
        *engine.Worlds().Get(engine.ActiveWorld());
    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(scene, GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    const MeshCounts expectedCounts = SourceMeshCounts(scene, *meshEntity);
    const std::vector<glm::vec3> expectedPositions =
        MeshVertexPositions(scene, *meshEntity);

    const Runtime::VertexChannelBindingSet expectedBindings{
        .Color = Runtime::VertexChannelSourceBinding{
            .Enabled = true,
            .Property = Runtime::GeometryPropertyRef{
                .Domain = Runtime::GeometryElementDomain::MeshVertex,
                .Name = "v:binding-only",
                .ValueKind = Geometry::PropertyValueKind::Vec4,
            },
        },
        .BindingGeneration = 17u,
    };
    scene.Raw().emplace_or_replace<Runtime::VertexChannelBindingSet>(
        *meshEntity,
        expectedBindings);

    workerBarrier.Release();
    engine.Run();

    EXPECT_EQ(jobs.Stats().StaleDiscardedJobs, 1u);
    ExpectMeshCountsEqual(SourceMeshCounts(scene, *meshEntity), expectedCounts);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(scene, *meshEntity),
        expectedPositions);
    const auto* finalBindings =
        scene.Raw().try_get<Runtime::VertexChannelBindingSet>(*meshEntity);
    ASSERT_NE(finalBindings, nullptr);
    EXPECT_EQ(finalBindings->BindingGeneration,
              expectedBindings.BindingGeneration);
    EXPECT_EQ(finalBindings->Color.Enabled, expectedBindings.Color.Enabled);
    EXPECT_EQ(finalBindings->Color.Property,
              expectedBindings.Color.Property);

    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(
        scene,
        RequiredEngineService<Runtime::SelectionController>(engine));
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_FALSE(model.Processing.DirectMeshEnrichmentPending);
    EXPECT_EQ(model.Processing.DirectMeshEnrichmentStatus,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(model.Processing.DirectMeshEnrichmentDiagnostic.empty());

    engine.Shutdown();
}

TEST(SandboxEditorUi, DirectMeshPostProcessPendingStateGatesMutatingActions)
{
    TmpFile meshFile(
        "runtime_mesh_postprocess_pending_readiness.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/2 3/3/3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        MakeDirectMeshPostProcessExitApplication());
    InitializeDirectMeshPostProcessEngine(engine);

    Runtime::JobService& jobs =
        RequiredEngineService<Runtime::JobService>(engine);
    DirectMeshPostProcessWorkerBarrier workerBarrier{};
    const Runtime::JobToken blocker =
        workerBarrier.Submit(jobs, engine.ActiveWorld());
    ASSERT_TRUE(blocker.IsValid());
    ASSERT_TRUE(workerBarrier.WaitUntilStarted());

    auto imported =
        RequiredEngineService<
            Extrinsic::Runtime::AssetWorkflowModule>(engine)
            .ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(
            *engine.Worlds().Get(engine.ActiveWorld()),
            GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());

    ECS::Scene::Registry& scene =
        *engine.Worlds().Get(engine.ActiveWorld());
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(
        scene,
        RequiredEngineService<Runtime::SelectionController>(engine));
    const Runtime::EditorDomainWindowModel pendingModel =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(pendingModel.Processing.DirectMeshEnrichmentPending);
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(
        pendingModel.Processing.DirectMeshEnrichmentStatus));
    EXPECT_FALSE(
        pendingModel.Processing.DirectMeshEnrichmentDiagnostic.empty());
    EXPECT_TRUE(pendingModel.Processing.Entries.empty());
    EXPECT_TRUE(pendingModel.Processing.KMeansDomains.empty());
    EXPECT_FALSE(pendingModel.Processing.MeshDenoiseAvailable);
    EXPECT_FALSE(pendingModel.Processing.MeshCurvatureAvailable);
    EXPECT_FALSE(pendingModel.Processing.MeshRemeshAvailable);
    EXPECT_FALSE(pendingModel.Processing.MeshSubdivideAvailable);
    EXPECT_FALSE(pendingModel.Processing.MeshSimplifyAvailable);
    EXPECT_FALSE(pendingModel.Processing.MeshVertexNormalsAvailable);
    EXPECT_FALSE(pendingModel.Processing.MeshProgressivePoissonAvailable);

    Runtime::JobToken enrichmentJob{};
    for (const Runtime::JobSnapshot& job : jobs.SnapshotAll())
    {
        if (job.DebugName.starts_with("Runtime.DirectMeshPostProcess."))
        {
            ASSERT_FALSE(enrichmentJob.IsValid());
            enrichmentJob = job.Token;
        }
    }
    ASSERT_TRUE(enrichmentJob.IsValid());

    EXPECT_FALSE(
        scene.Raw()
            .get<GS::Vertices>(*meshEntity)
            .Properties.Exists("v:texcoord"));

    workerBarrier.Release();
    engine.Run();

    EXPECT_EQ(jobs.Stats().PublishedCompletions, 2u);
    EXPECT_EQ(jobs.Stats().StaleDiscardedJobs, 0u);
    EXPECT_EQ(jobs.DrainCompletions(engine.Events()), 0u);

    const GS::ConstSourceView enriched =
        GS::BuildConstView(scene.Raw(), *meshEntity);
    ASSERT_NE(enriched.VertexSource, nullptr);
    EXPECT_TRUE(
        enriched.VertexSource->Properties.Exists("v:normal"));
    EXPECT_TRUE(
        enriched.VertexSource->Properties.Exists("v:texcoord"));

    const Runtime::EditorDomainWindowModel readyModel =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_FALSE(readyModel.Processing.DirectMeshEnrichmentPending);
    EXPECT_EQ(
        readyModel.Processing.DirectMeshEnrichmentStatus,
        Runtime::JobState::Published);
    EXPECT_FALSE(
        readyModel.Processing.DirectMeshEnrichmentDiagnostic.empty());
    EXPECT_FALSE(readyModel.Processing.Entries.empty());
    EXPECT_FALSE(readyModel.Processing.KMeansDomains.empty());
    EXPECT_TRUE(readyModel.Processing.MeshDenoiseAvailable);
    EXPECT_TRUE(readyModel.Processing.MeshCurvatureAvailable);
    EXPECT_TRUE(readyModel.Processing.MeshRemeshAvailable);
    EXPECT_TRUE(readyModel.Processing.MeshSubdivideAvailable);
    EXPECT_TRUE(readyModel.Processing.MeshSimplifyAvailable);
    EXPECT_TRUE(readyModel.Processing.MeshVertexNormalsAvailable);
    EXPECT_TRUE(readyModel.Processing.MeshProgressivePoissonAvailable);

    engine.Shutdown();
}

TEST(SandboxEditorUi, DirectMeshPostProcessDiscardsCompletionAfterWorldSwitch)
{
    TmpFile meshFile(
        "runtime_mesh_postprocess_stale_world.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    DirectMeshPostProcessWorkerBarrier workerBarrier{};
    Runtime::WorldHandle replacementWorld{};
    bool releasedAfterSwitch = false;
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&](Runtime::Engine& runningEngine)
            {
                if (!releasedAfterSwitch &&
                    replacementWorld.IsValid() &&
                    runningEngine.ActiveWorld() == replacementWorld)
                {
                    releasedAfterSwitch = true;
                    workerBarrier.Release();
                }
                const Runtime::JobServiceStats stats =
                    RequiredEngineService<Runtime::JobService>(runningEngine)
                        .Stats();
                return releasedAfterSwitch &&
                    stats.InFlightJobs == 0u &&
                    stats.PublishedCompletions + stats.StaleDiscardedJobs >= 2u;
            },
            1024u));
    InitializeDirectMeshPostProcessEngine(engine);

    Runtime::JobService& jobs =
        RequiredEngineService<Runtime::JobService>(engine);
    const Runtime::WorldHandle originalWorld = engine.ActiveWorld();
    ECS::Scene::Registry* const originalScene =
        engine.Worlds().Get(originalWorld);
    ASSERT_NE(originalScene, nullptr);
    replacementWorld = engine.Worlds().CreateWorld("BUG-095 replacement");
    ASSERT_TRUE(replacementWorld.IsValid());

    const Runtime::JobToken blocker =
        workerBarrier.Submit(jobs, originalWorld);
    ASSERT_TRUE(blocker.IsValid());
    ASSERT_TRUE(workerBarrier.WaitUntilStarted());

    auto imported =
        RequiredEngineService<Runtime::AssetWorkflowModule>(engine)
            .ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(*originalScene, GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    const MeshCounts expectedCounts =
        SourceMeshCounts(*originalScene, *meshEntity);
    const std::vector<glm::vec3> expectedPositions =
        MeshVertexPositions(*originalScene, *meshEntity);
    ASSERT_FALSE(
        originalScene->Raw()
            .get<GS::Vertices>(*meshEntity)
            .Properties.Exists("v:texcoord"));

    ASSERT_TRUE(
        engine.Worlds().RequestSetActiveWorld(replacementWorld).has_value());
    engine.Run();

    ASSERT_TRUE(releasedAfterSwitch);
    EXPECT_EQ(engine.ActiveWorld(), replacementWorld);
    EXPECT_EQ(jobs.Stats().StaleDiscardedJobs, 1u);
    ASSERT_EQ(engine.Worlds().Get(originalWorld), originalScene);
    ExpectMeshCountsEqual(
        SourceMeshCounts(*originalScene, *meshEntity),
        expectedCounts);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(*originalScene, *meshEntity),
        expectedPositions);
    EXPECT_FALSE(
        originalScene->Raw()
            .get<GS::Vertices>(*meshEntity)
            .Properties.Exists("v:texcoord"));

    engine.Shutdown();
}

TEST(SandboxEditorUi, DirectMeshPostProcessWorldDestructionIsLifetimeSafe)
{
    TmpFile meshFile(
        "runtime_mesh_postprocess_destroyed_world.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    DirectMeshPostProcessWorkerBarrier workerBarrier{};
    Runtime::WorldHandle originalWorld{};
    Runtime::WorldHandle replacementWorld{};
    bool destroyRequested = false;
    bool releasedAfterDestroy = false;
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&](Runtime::Engine& runningEngine)
            {
                if (!destroyRequested &&
                    originalWorld.IsValid() &&
                    replacementWorld.IsValid() &&
                    runningEngine.ActiveWorld() == replacementWorld)
                {
                    const Core::Result requested =
                        runningEngine.Worlds().RequestDestroyWorld(
                            originalWorld);
                    EXPECT_TRUE(requested.has_value());
                    if (requested.has_value())
                    {
                        destroyRequested = true;
                    }
                }
                if (destroyRequested &&
                    !releasedAfterDestroy &&
                    !runningEngine.Worlds().Contains(originalWorld))
                {
                    releasedAfterDestroy = true;
                    workerBarrier.Release();
                }
                const Runtime::JobServiceStats stats =
                    RequiredEngineService<Runtime::JobService>(runningEngine)
                        .Stats();
                return releasedAfterDestroy &&
                    stats.InFlightJobs == 0u &&
                    stats.PendingUnpublishedFinalizers == 0u;
            },
            1024u));
    InitializeDirectMeshPostProcessEngine(engine);

    Runtime::JobService& jobs =
        RequiredEngineService<Runtime::JobService>(engine);
    originalWorld = engine.ActiveWorld();
    replacementWorld = engine.Worlds().CreateWorld("BUG-095 survivor");
    ASSERT_TRUE(replacementWorld.IsValid());

    const Runtime::JobToken blocker =
        workerBarrier.Submit(jobs, originalWorld);
    ASSERT_TRUE(blocker.IsValid());
    ASSERT_TRUE(workerBarrier.WaitUntilStarted());

    auto imported =
        RequiredEngineService<Runtime::AssetWorkflowModule>(engine)
            .ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());
    ASSERT_TRUE(
        engine.Worlds().RequestSetActiveWorld(replacementWorld).has_value());

    engine.Run();

    EXPECT_TRUE(destroyRequested);
    EXPECT_TRUE(releasedAfterDestroy);
    EXPECT_EQ(engine.ActiveWorld(), replacementWorld);
    EXPECT_FALSE(engine.Worlds().Contains(originalWorld));
    EXPECT_EQ(jobs.Stats().InFlightJobs, 0u);
    EXPECT_GE(jobs.Stats().CancelledJobs + jobs.Stats().StaleDiscardedJobs,
              1u);

    engine.Shutdown();
}

TEST(SandboxEditorUi, DirectMeshPostProcessRejectsRecycledEntityTarget)
{
    TmpFile meshFile(
        "runtime_mesh_postprocess_recycled_entity.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        MakeDirectMeshPostProcessExitApplication());
    InitializeDirectMeshPostProcessEngine(engine);

    Runtime::JobService& jobs =
        RequiredEngineService<Runtime::JobService>(engine);
    DirectMeshPostProcessWorkerBarrier workerBarrier{};
    const Runtime::JobToken blocker =
        workerBarrier.Submit(jobs, engine.ActiveWorld());
    ASSERT_TRUE(blocker.IsValid());
    ASSERT_TRUE(workerBarrier.WaitUntilStarted());

    auto imported =
        RequiredEngineService<Runtime::AssetWorkflowModule>(engine)
            .ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
                .Path = meshFile.Path.string(),
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
            });
    ASSERT_TRUE(imported.has_value())
        << static_cast<int>(imported.error());

    ECS::Scene::Registry& scene =
        *engine.Worlds().Get(engine.ActiveWorld());
    const std::optional<ECS::EntityHandle> original =
        FindFirstEntityWithDomain(scene, GS::Domain::Mesh);
    ASSERT_TRUE(original.has_value());
    scene.Destroy(*original);
    const ECS::EntityHandle recycled = scene.Create();
    ASSERT_EQ(entt::to_entity(recycled), entt::to_entity(*original));
    ASSERT_NE(recycled, *original);
    AddIcosahedronMeshSource(scene, recycled);
    const MeshCounts expectedCounts = SourceMeshCounts(scene, recycled);
    const std::vector<glm::vec3> expectedPositions =
        MeshVertexPositions(scene, recycled);

    workerBarrier.Release();
    engine.Run();

    EXPECT_EQ(jobs.Stats().StaleDiscardedJobs, 1u);
    EXPECT_FALSE(scene.IsValid(*original));
    EXPECT_TRUE(scene.IsValid(recycled));
    ExpectMeshCountsEqual(SourceMeshCounts(scene, recycled), expectedCounts);
    ExpectPositionsExactlyEqual(
        MeshVertexPositions(scene, recycled),
        expectedPositions);

    engine.Shutdown();
}

TEST(SandboxEditorUi, MeshVertexNormalsCommandFailsClosedForInvalidTargets)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::EditorMeshVertexNormalsCommand validShape{
        .StableEntityId = 1u,
    };

    const Runtime::EditorMeshVertexNormalsResult missingScene =
        Runtime::ApplyEditorMeshVertexNormalsCommand(
            Intrinsic::Tests::EditorFeatureTestContext{},
            validShape);
    EXPECT_EQ(missingScene.Status,
              Runtime::EditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::EditorMeshVertexNormalsResult stale =
        Runtime::ApplyEditorMeshVertexNormalsCommand(
            context,
            Runtime::EditorMeshVertexNormalsCommand{
                .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
            });
    EXPECT_EQ(stale.Status, Runtime::EditorCommandStatus::StaleEntity);
    EXPECT_EQ(stale.Error, Core::ErrorCode::ResourceNotFound);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    const Runtime::EditorMeshVertexNormalsResult wrongDomain =
        Runtime::ApplyEditorMeshVertexNormalsCommand(
            context,
            Runtime::EditorMeshVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TypeConflictMesh");
    AddTriangleMeshSource(registry, mesh);
    const auto conflictingNormals = registry.Raw()
                                        .get<GS::Vertices>(mesh)
                                        .Properties.GetOrAdd<float>(
                                            std::string{PN::kNormal},
                                            0.0f);
    ASSERT_TRUE(conflictingNormals);
    const Runtime::EditorMeshVertexNormalsResult conflict =
        Runtime::ApplyEditorMeshVertexNormalsCommand(
            context,
            Runtime::EditorMeshVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(conflict.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(conflict.NormalStatus, GN::RecomputeStatus::PropertyTypeConflict);
    EXPECT_EQ(conflict.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    context.LastMeshVertexNormalsResult = &conflict;
    const Runtime::EditorDomainWindowModel model =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::EditorDiagnosticCode::GeometryProcessingFailed));
}
TEST(SandboxEditorUi, GraphAndPointCloudVertexNormalsCommandsPublishCanonicalNormals)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle graph = MakeSelectable(registry, "NormalGraph");
    AddPlanarCycleGraphSource(registry, graph);
    const std::uint32_t graphStableId =
        Runtime::SelectionController::ToStableEntityId(graph);

    const Runtime::EditorGraphVertexNormalsResult graphResult =
        Runtime::ApplyEditorGraphVertexNormalsCommand(
            context,
            Runtime::EditorGraphVertexNormalsCommand{
                .StableEntityId = graphStableId,
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
                .OrientTowardFallback = true,
            });

    ASSERT_TRUE(graphResult.Succeeded()) << graphResult.Message;
    EXPECT_EQ(graphResult.NormalStatus, GVN::RecomputeStatus::Success);
    EXPECT_EQ(graphResult.VertexSlotCount, 4u);
    EXPECT_EQ(graphResult.EdgeSlotCount, 4u);
    EXPECT_EQ(graphResult.WrittenCount, 4u);
    EXPECT_EQ(graphResult.ValidNormalVertexCount, 4u);
    EXPECT_EQ(graphResult.InvalidEdgeCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(graph));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(graph));

    auto graphNormals = registry.Raw()
                            .get<GS::Nodes>(graph)
                            .Properties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(graphNormals);
    ASSERT_EQ(graphNormals.Vector().size(), 4u);
    for (const glm::vec3 normal : graphNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.9f);
    }

    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    context.LastGraphVertexNormalsResult = &graphResult;
    const Runtime::EditorDomainWindowModel graphModel =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::Graph);
    ASSERT_TRUE(graphModel.Processing.GraphVertexNormalsAvailable);
    ASSERT_TRUE(graphModel.Processing.LastGraphVertexNormalsResult.has_value());
    EXPECT_TRUE(
        graphModel.Processing.LastGraphVertexNormalsResult->Succeeded());
    EXPECT_EQ(
        graphModel.Processing.LastGraphVertexNormalsResult->WrittenCount,
        4u);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "NormalCloud");
    AddPointCloudSource(registry, cloud, 9u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {-1.0f, -1.0f, 0.0f},
                     {0.0f, -1.0f, 0.0f},
                     {1.0f, -1.0f, 0.0f},
                     {-1.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {-1.0f, 1.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });
    auto deletedPoints = registry.Raw()
                             .get<GS::Vertices>(cloud)
                             .Properties.GetOrAdd<bool>("p:deleted", false);
    ASSERT_EQ(deletedPoints.Vector().size(), 9u);
    deletedPoints.Vector()[8] = true;
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);

    const Runtime::EditorPointCloudVertexNormalsResult cloudResult =
        Runtime::ApplyEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::EditorPointCloudVertexNormalsCommand{
                .StableEntityId = cloudStableId,
                .KNeighbors = 4u,
                .MinimumNeighbors = 2u,
                .UseRadiusSearch = false,
                .Orientation = PCN::OrientationMode::MinimumSpanningTree,
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
            });

    ASSERT_TRUE(cloudResult.Succeeded()) << cloudResult.Message;
    EXPECT_EQ(cloudResult.NormalStatus, PCN::RecomputeStatus::Success);
    EXPECT_EQ(cloudResult.Backend, PCN::NeighborhoodBackend::KDTree);
    EXPECT_EQ(cloudResult.PointSlotCount, 9u);
    EXPECT_EQ(cloudResult.WrittenCount, 8u);
    EXPECT_EQ(cloudResult.SkippedDeletedPointCount, 1u);
    EXPECT_EQ(cloudResult.KNeighbors, 4u);
    EXPECT_EQ(cloudResult.MinimumNeighbors, 2u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));

    auto cloudNormals = registry.Raw()
                            .get<GS::Vertices>(cloud)
                            .Properties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(cloudNormals);
    ASSERT_EQ(cloudNormals.Vector().size(), 9u);
    for (const glm::vec3 normal : cloudNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.5f);
    }

    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    context.LastGraphVertexNormalsResult = nullptr;
    context.LastPointCloudVertexNormalsResult = &cloudResult;
    const Runtime::EditorDomainWindowModel cloudModel =
        Runtime::BuildEditorDomainWindowModel(
            context,
            Runtime::EditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(cloudModel.Processing.PointCloudVertexNormalsAvailable);
    ASSERT_TRUE(
        cloudModel.Processing.LastPointCloudVertexNormalsResult.has_value());
    EXPECT_TRUE(
        cloudModel.Processing.LastPointCloudVertexNormalsResult->Succeeded());
    EXPECT_EQ(
        cloudModel.Processing.LastPointCloudVertexNormalsResult->WrittenCount,
        8u);
    EXPECT_EQ(cloudModel.Processing.LastPointCloudVertexNormalsResult
                  ->SkippedDeletedPointCount,
              1u);
    EXPECT_TRUE(history.IsDirty());
}
TEST(SandboxEditorUi, GraphAndPointCloudVertexNormalsCommandsFailClosedForInvalidTargets)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::EditorGraphVertexNormalsResult missingGraphScene =
        Runtime::ApplyEditorGraphVertexNormalsCommand(
            Intrinsic::Tests::EditorFeatureTestContext{},
            Runtime::EditorGraphVertexNormalsCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingGraphScene.Status,
              Runtime::EditorCommandStatus::MissingScene);
    EXPECT_EQ(missingGraphScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloudWrongDomain =
        MakeSelectable(registry, "CloudWrongDomain");
    AddPointCloudSource(registry, cloudWrongDomain, 3u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloudWrongDomain),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });
    const Runtime::EditorGraphVertexNormalsResult graphWrongDomain =
        Runtime::ApplyEditorGraphVertexNormalsCommand(
            context,
            Runtime::EditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        cloudWrongDomain),
            });
    EXPECT_EQ(graphWrongDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloudWrongDomain));

    const Runtime::EditorPointCloudVertexNormalsResult invalidPointParams =
        Runtime::ApplyEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::EditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        cloudWrongDomain),
                .KNeighbors = 0u,
            });
    EXPECT_EQ(invalidPointParams.Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(registry.Raw().get<GS::Vertices>(cloudWrongDomain)
                     .Properties.Exists(PN::kNormal));

    const ECS::EntityHandle graphConflict =
        MakeSelectable(registry, "GraphConflict");
    AddPlanarCycleGraphSource(registry, graphConflict);
    auto graphConflictNormals = registry.Raw()
                                    .get<GS::Nodes>(graphConflict)
                                    .Properties.GetOrAdd<float>(
                                        std::string{PN::kNormal},
                                        0.0f);
    ASSERT_TRUE(graphConflictNormals);
    const Runtime::EditorGraphVertexNormalsResult graphConflictResult =
        Runtime::ApplyEditorGraphVertexNormalsCommand(
            context,
            Runtime::EditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        graphConflict),
            });
    EXPECT_EQ(graphConflictResult.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(graphConflictResult.NormalStatus,
              GVN::RecomputeStatus::PropertyTypeConflict);
    EXPECT_EQ(graphConflictResult.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(graphConflict));
    EXPECT_TRUE(registry.Raw()
                    .get<GS::Nodes>(graphConflict)
                    .Properties.Get<float>(PN::kNormal));

    const ECS::EntityHandle meshWrongDomain =
        MakeSelectable(registry, "MeshWrongDomain");
    AddTriangleMeshSource(registry, meshWrongDomain);
    const Runtime::EditorPointCloudVertexNormalsResult pointWrongDomain =
        Runtime::ApplyEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::EditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        meshWrongDomain),
            });
    EXPECT_EQ(pointWrongDomain.Status,
              Runtime::EditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(meshWrongDomain));

    const ECS::EntityHandle cloudConflict =
        MakeSelectable(registry, "CloudConflict");
    AddPointCloudSource(registry, cloudConflict, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloudConflict),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });
    auto pointConflictNormals = registry.Raw()
                                    .get<GS::Vertices>(cloudConflict)
                                    .Properties.GetOrAdd<float>(
                                        std::string{PN::kNormal},
                                        0.0f);
    ASSERT_TRUE(pointConflictNormals);
    const Runtime::EditorPointCloudVertexNormalsResult
        pointConflictResult =
            Runtime::ApplyEditorPointCloudVertexNormalsCommand(
                context,
                Runtime::EditorPointCloudVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(
                            cloudConflict),
                    .KNeighbors = 3u,
                    .MinimumNeighbors = 2u,
                });
    EXPECT_EQ(pointConflictResult.Status,
              Runtime::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(pointConflictResult.NormalStatus,
              PCN::RecomputeStatus::PropertyTypeConflict);
    EXPECT_EQ(pointConflictResult.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloudConflict));
    EXPECT_TRUE(registry.Raw()
                    .get<GS::Vertices>(cloudConflict)
                    .Properties.Get<float>(PN::kNormal));
}
TEST(SandboxEditorUi, UvRegenerationCommandRepairsSelectedMeshTexcoords)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "UvRepairMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };
    vertices.Properties.GetOrAdd<glm::vec4>("v:paint", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    auto& faces = registry.Raw().get<GS::Faces>(mesh);
    faces.Properties.GetOrAdd<std::uint32_t>("f:material", 0u).Vector() = {7u};
    const MeshCounts originalCounts = SourceMeshCounts(registry, mesh);
    const std::vector<glm::vec2> originalTexcoords = texcoords.Vector();
    const std::vector<glm::vec4> originalPaint =
        vertices.Properties.Get<glm::vec4>("v:paint").Vector();
    const std::vector<std::uint32_t> originalMaterial =
        faces.Properties.Get<std::uint32_t>("f:material").Vector();

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const Runtime::EditorWorkspaceSnapshot before =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(before.Inspector.TextureBake.HasSelectedEntity);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.UvRegenerationAvailable);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.HasTexcoords);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.TexcoordCountMatchesVertices);
    EXPECT_FALSE(before.Inspector.TextureBake.Uv.TexcoordsFinite);
    EXPECT_FALSE(before.Inspector.TextureBake.CanBake);

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::EditorUvRegenerationCommandResult result =
        Runtime::ApplyEditorUvRegenerationCommand(
            context,
            Runtime::EditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });

    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Applied);
    EXPECT_EQ(result.UvStatus, Geometry::UvAtlas::UvAtlasStatus::Success);
    EXPECT_EQ(result.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
    EXPECT_GT(result.AtlasWidth, 0u);
    EXPECT_GT(result.AtlasHeight, 0u);
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    const GS::ConstSourceView repaired = GS::BuildConstView(registry.Raw(), mesh);
    ASSERT_EQ(repaired.ActiveDomain, GS::Domain::Mesh);
    ASSERT_NE(repaired.VertexSource, nullptr);
    const auto repairedTexcoords =
        repaired.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(repairedTexcoords);
    ASSERT_EQ(repairedTexcoords.Vector().size(), repaired.VerticesAlive());
    for (const glm::vec2 uv : repairedTexcoords.Vector())
    {
        EXPECT_TRUE(std::isfinite(uv.x));
        EXPECT_TRUE(std::isfinite(uv.y));
    }
    const auto repairedPaint =
        repaired.VertexSource->Properties.Get<glm::vec4>("v:paint");
    ASSERT_TRUE(repairedPaint);
    EXPECT_EQ(repairedPaint.Vector().size(), repairedTexcoords.Vector().size());

    ASSERT_NE(repaired.FaceSource, nullptr);
    const auto repairedMaterial =
        repaired.FaceSource->Properties.Get<std::uint32_t>("f:material");
    ASSERT_TRUE(repairedMaterial);
    ASSERT_FALSE(repairedMaterial.Vector().empty());
    EXPECT_EQ(repairedMaterial[0], 7u);
    const MeshCounts repairedCounts = SourceMeshCounts(registry, mesh);
    const std::vector<glm::vec2> generatedTexcoords =
        repairedTexcoords.Vector();
    const std::vector<glm::vec4> generatedPaint =
        repairedPaint.Vector();
    const std::vector<std::uint32_t> generatedMaterial =
        repairedMaterial.Vector();

    const Runtime::EditorWorkspaceSnapshot after =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    EXPECT_TRUE(after.Inspector.TextureBake.Uv.TexcoordsFinite);
    EXPECT_TRUE(after.Inspector.TextureBake.Uv.CheckerPreviewAvailable);

    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), originalCounts);
    const GS::ConstSourceView restored =
        GS::BuildConstView(registry.Raw(), mesh);
    ASSERT_NE(restored.VertexSource, nullptr);
    ASSERT_NE(restored.FaceSource, nullptr);
    const auto restoredTexcoords =
        restored.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    const auto restoredPaint =
        restored.VertexSource->Properties.Get<glm::vec4>("v:paint");
    const auto restoredMaterial =
        restored.FaceSource->Properties.Get<std::uint32_t>("f:material");
    ASSERT_TRUE(restoredTexcoords);
    ASSERT_TRUE(restoredPaint);
    ASSERT_TRUE(restoredMaterial);
    ExpectTexcoordsExactlyEqual(
        restoredTexcoords.Vector(),
        originalTexcoords);
    ExpectColorsExactlyEqual(restoredPaint.Vector(), originalPaint);
    EXPECT_EQ(restoredMaterial.Vector(), originalMaterial);

    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), repairedCounts);
    const GS::ConstSourceView regenerated =
        GS::BuildConstView(registry.Raw(), mesh);
    ASSERT_NE(regenerated.VertexSource, nullptr);
    ASSERT_NE(regenerated.FaceSource, nullptr);
    const auto regeneratedTexcoords =
        regenerated.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    auto regeneratedPaint =
        registry.Raw()
            .get<GS::Vertices>(mesh)
            .Properties.Get<glm::vec4>("v:paint");
    const auto regeneratedMaterial =
        regenerated.FaceSource->Properties.Get<std::uint32_t>("f:material");
    ASSERT_TRUE(regeneratedTexcoords);
    ASSERT_TRUE(regeneratedPaint);
    ASSERT_TRUE(regeneratedMaterial);
    ExpectTexcoordsExactlyEqual(
        regeneratedTexcoords.Vector(),
        generatedTexcoords);
    ExpectColorsExactlyEqual(regeneratedPaint.Vector(), generatedPaint);
    EXPECT_EQ(regeneratedMaterial.Vector(), generatedMaterial);

    const float interveningPaint = regeneratedPaint[0].x + 0.25f;
    regeneratedPaint[0].x = interveningPaint;
    const Runtime::EditorCommandHistorySnapshot beforeRejectedUndo =
        history.Snapshot();
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_FLOAT_EQ(
        registry.Raw()
            .get<GS::Vertices>(mesh)
            .Properties.Get<glm::vec4>("v:paint")[0]
            .x,
        interveningPaint);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), repairedCounts);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_EQ(history.Snapshot().Revision, beforeRejectedUndo.Revision);
}
TEST(SandboxEditorUi, UvRegenerationRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    std::optional<Runtime::EditorUvRegenerationCommandResult>
        completedResult{};
    context.MethodResultSinks.UvRegeneration =
        [&completedResult](
            Runtime::EditorUvRegenerationCommandResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "QueuedUvRepairMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };

    const auto texcoordsFinite =
        [&registry, mesh]()
        {
            const GS::ConstSourceView view =
                GS::BuildConstView(registry.Raw(), mesh);
            if (view.VertexSource == nullptr)
                return false;
            const auto uv =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (!uv)
                return false;
            for (const glm::vec2 value : uv.Vector())
            {
                if (!std::isfinite(value.x) || !std::isfinite(value.y))
                    return false;
            }
            return true;
        };

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::EditorUvRegenerationCommandResult result =
        Runtime::ApplyEditorUvRegenerationCommand(
            context,
            Runtime::EditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_NE(result.Diagnostic.find("queued"), std::string::npos);
    EXPECT_FALSE(texcoordsFinite());
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.UvRegeneration.CPU");
    // `JobService` dispatches at submit, so the pre-drain state races;
    // assert only that the job is still active.
    EXPECT_TRUE(
        Runtime::IsActiveEditorJobState(queued.Entries[0].State));

    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(texcoordsFinite());

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State, Runtime::JobState::Published);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Diagnostic;
    EXPECT_EQ(completedResult->UvStatus,
              Geometry::UvAtlas::UvAtlasStatus::Success);
    EXPECT_EQ(completedResult->Provenance,
              Geometry::UvAtlas::UvAtlasProvenance::Generated);
    EXPECT_TRUE(texcoordsFinite());
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());

    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(texcoordsFinite());
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_TRUE(texcoordsFinite());
}
TEST(SandboxEditorUi, UvRegenerationDuplicateSubmitUsesExistingActiveJob)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "QueuedUvDuplicateMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };

    const Runtime::EditorUvRegenerationCommand command{
        .StableEntityId =
            Runtime::SelectionController::ToStableEntityId(mesh),
        .Resolution = 64u,
        .Padding = 2u,
    };

    const Runtime::EditorUvRegenerationCommandResult first =
        Runtime::ApplyEditorUvRegenerationCommand(context, command);
    ASSERT_EQ(first.Status, Runtime::EditorCommandStatus::Pending);

    Runtime::EditorJobQueueSnapshot queued =
        jobs.Snapshot();
    ASSERT_EQ(queued.Entries.size(), 1u);

    const Runtime::EditorUvRegenerationCommandResult duplicate =
        Runtime::ApplyEditorUvRegenerationCommand(context, command);
    EXPECT_EQ(duplicate.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_NE(duplicate.Diagnostic.find("already has an active"),
              std::string::npos);
    EXPECT_NE(duplicate.Diagnostic.find("job 0:1"), std::string::npos);
    EXPECT_EQ(jobs.Snapshot().Entries.size(), 1u);

    ASSERT_TRUE(jobs.DrainUntilTerminal());

    Runtime::EditorJobQueueSnapshot complete =
        jobs.Snapshot();
    ASSERT_EQ(complete.Entries.size(), 1u);
    EXPECT_EQ(complete.Entries[0].State, Runtime::JobState::Published);

    const Runtime::EditorUvRegenerationCommandResult rerun =
        Runtime::ApplyEditorUvRegenerationCommand(context, command);
    EXPECT_EQ(rerun.Status, Runtime::EditorCommandStatus::Pending);
    Runtime::EditorJobQueueSnapshot afterRerun =
        jobs.Snapshot();
    ASSERT_EQ(afterRerun.Entries.size(), 2u);
    EXPECT_TRUE(
        Runtime::IsActiveEditorJobState(afterRerun.Entries[1].State));
}
TEST(SandboxEditorUi, UvRegenerationDerivedJobDiscardsStaleSource)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.UvRegeneration =
        [&completedSinkCalled](
            Runtime::EditorUvRegenerationCommandResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "StaleUvRepairMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::EditorUvRegenerationCommandResult result =
        Runtime::ApplyEditorUvRegenerationCommand(
            context,
            Runtime::EditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });
    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

    SetPositions(vertices,
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.25f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });

    ASSERT_TRUE(jobs.DrainUntilTerminal());

    Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    const GS::ConstSourceView stale = GS::BuildConstView(registry.Raw(), mesh);
    ASSERT_NE(stale.VertexSource, nullptr);
    const auto staleTexcoords =
        stale.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(staleTexcoords);
    ASSERT_GT(staleTexcoords.Vector().size(), 1u);
    EXPECT_FALSE(std::isfinite(staleTexcoords[1].x));
}
TEST(SandboxEditorUi, UvRegenerationDerivedJobDiscardsStaleAuthoredProperty)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);
    bool completedSinkCalled = false;
    context.MethodResultSinks.UvRegeneration =
        [&completedSinkCalled](
            Runtime::EditorUvRegenerationCommandResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "StaleUvPropertyMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };
    auto paint =
        vertices.Properties.GetOrAdd<glm::vec4>(
            "v:paint",
            glm::vec4{1.0f});
    ASSERT_TRUE(paint);

    const Runtime::EditorUvRegenerationCommandResult result =
        Runtime::ApplyEditorUvRegenerationCommand(
            context,
            Runtime::EditorUvRegenerationCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Resolution = 64u,
                .Padding = 2u,
            });
    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

    paint[0] = glm::vec4{0.25f, 0.5f, 0.75f, 1.0f};

    ASSERT_TRUE(jobs.DrainUntilTerminal());
    const Runtime::EditorJobQueueSnapshot done =
        jobs.Snapshot();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].State,
              Runtime::JobState::StaleDiscarded);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    const GS::ConstSourceView stale = GS::BuildConstView(registry.Raw(), mesh);
    ASSERT_NE(stale.VertexSource, nullptr);
    const auto staleTexcoords =
        stale.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    const auto stalePaint =
        stale.VertexSource->Properties.Get<glm::vec4>("v:paint");
    ASSERT_TRUE(staleTexcoords);
    ASSERT_TRUE(stalePaint);
    EXPECT_FALSE(std::isfinite(staleTexcoords[1].x));
    EXPECT_FLOAT_EQ(stalePaint[0].x, 0.25f);
    EXPECT_FLOAT_EQ(stalePaint[0].y, 0.5f);
    EXPECT_FLOAT_EQ(stalePaint[0].z, 0.75f);
    EXPECT_FLOAT_EQ(stalePaint[0].w, 1.0f);
}
TEST(SandboxEditorUi, UvRegenerationPanelModelTracksDerivedJobStateThroughCache)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorSelectedModelCache cache{};
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    Extrinsic::Tests::EditorJobHarness jobs{};
    jobs.Attach(context);

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CachedUvJobMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.Inspector.HasEntity);
    EXPECT_FALSE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());

    const Runtime::EditorUvRegenerationCommandResult result =
        Runtime::ApplyEditorUvRegenerationCommand(
            context,
            Runtime::EditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });
    ASSERT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);

    frame = Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_TRUE(Runtime::IsActiveEditorJobState(
        frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status));
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Key.OutputName,
              "uv_regeneration");

    Core::Tasks::Scheduler::WaitForAll();
    frame = Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::JobState::AwaitingGate);

    EXPECT_EQ(jobs.Jobs().DrainCompletions(jobs.Events(), 1u), 1u);
    frame = Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::JobState::Published);
    EXPECT_TRUE(frame.Inspector.TextureBake.Uv.TexcoordsFinite);

    const Runtime::EditorSelectedModelCacheStats stats = cache.Stats();
    EXPECT_GE(stats.SelectedAnalysisCacheMisses, 4u);
}
TEST(SandboxEditorUi, TextureBakeControlsReportUvSourcesAndRequireRuntimeModule)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Assets::AssetService assets;
    Tests::MockDevice device;
    device.Operational = true;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TextureBakeMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec4>("v:paint", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    AttachGeometryPresentation(registry, mesh);

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Intrinsic::Tests::EditorFeatureTestContext context =
        MakeContext(registry, selection, true, nullptr, &device);
    context.CommandHistory = &history;
    context.AssetService = &assets;
    Runtime::TextureBakeService textureBake{};
    context.TextureBake = &textureBake;

    const Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    const Runtime::EditorTextureBakeControlsModel& bake =
        frame.Inspector.TextureBake;
    ASSERT_TRUE(bake.HasSelectedEntity);
    EXPECT_TRUE(bake.IsMesh);
    EXPECT_TRUE(bake.Uv.HasTexcoords);
    EXPECT_TRUE(bake.Uv.TexcoordCountMatchesVertices);
    EXPECT_TRUE(bake.Uv.TexcoordsFinite);
    EXPECT_FALSE(bake.HasRuntimeBakeCommand);
    EXPECT_FALSE(bake.CanBake);
    EXPECT_TRUE(bake.Uv.UvRegenerationAvailable);
    EXPECT_TRUE(bake.Uv.UvRegenerationDisabledReason.empty());

    const Runtime::EditorTextureBakeSourceRow* paint =
        FindTextureBakeSource(bake, "v:paint");
    ASSERT_NE(paint, nullptr);
    EXPECT_TRUE(paint->Bakeable);
    EXPECT_EQ(paint->BakeDomain, Runtime::GeometryElementDomain::MeshVertex);
    EXPECT_EQ(paint->ExpectedValueKind, Geometry::PropertyValueKind::Vec4);

    const Runtime::EditorTextureBakeSourceRow* position =
        FindTextureBakeSource(bake, std::string{PN::kPosition});
    ASSERT_NE(position, nullptr);
    EXPECT_FALSE(position->Bakeable);
    EXPECT_EQ(position->Category,
              Runtime::EditorTextureBakeSourceCategory::Connectivity);
    EXPECT_FALSE(position->DisabledReason.empty());

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::EditorTextureBakeCommandResult result =
        Runtime::ApplyEditorTextureBakeCommand(
            context,
            Runtime::EditorTextureBakeCommand{
                .StableEntityId = stableId,
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Albedo,
                .SourceDomain = Runtime::GeometryElementDomain::MeshVertex,
                .ExpectedValueKind = Geometry::PropertyValueKind::Vec4,
                .PropertyName = "v:paint",
                .Encoder = Runtime::PropertyTextureBakeEncoding::RgbaColor,
                .Width = 4u,
                .Height = 4u,
                .GeneratedKey = "paint",
                .BindGeneratedTexture = true,
            });

    ASSERT_EQ(
        result.Status,
        Runtime::EditorCommandStatus::InvalidVisualizationProperty);
    EXPECT_EQ(result.BakeStatus,
              Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
    EXPECT_FALSE(result.GeneratedTexture.IsValid());
    EXPECT_FALSE(result.BoundGeneratedTexture);
    EXPECT_FALSE(history.IsDirty());
}
TEST(SandboxEditorUi, AttachedEngineContextWiresTextureBakeModule)
{
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>([](Runtime::Engine&) { return true; }));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    engine.EmplaceModule<Runtime::TextureBakeModule>();
    engine.Initialize();

    Runtime::AssetWorkflowModule& pipeline =
        RequiredEngineService<Runtime::AssetWorkflowModule>(engine);
    Runtime::TextureBakeService& textureBake =
        RequiredEngineService<Runtime::TextureBakeService>(engine);
    EXPECT_EQ(
        pipeline.GetTextureBakeServiceForTest(),
        &textureBake);
    EXPECT_FALSE(textureBake.Available())
        << "The wired service must remain fail-closed on the Null device.";

    Runtime::EditorWorkspaceSession session{};
    session.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(session.PrepareFrame());
    bool visited = false;
    ASSERT_TRUE(
        session.VisitPreparedFrame(
            [&](const Runtime::EditorWorkspacePreparedFrame prepared)
            {
                visited = true;
                EXPECT_TRUE(Runtime::IsEditorTextureBakeServiceAttached(
                    prepared.VisualizationCommands));
            }));
    EXPECT_TRUE(visited);

    session.Detach();
    engine.Shutdown();
}
TEST(SandboxEditorUi, TextureBakeOperationDoesNotBypassUnavailableRuntimeModule)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::TextureBakeService textureBake{};

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "QueuedNormalBakeMesh");
    AddTriangleMeshSource(registry, mesh);
    SetNormals(registry.Raw().get<GS::Vertices>(mesh));
    AttachGeometryPresentation(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Intrinsic::Tests::EditorFeatureTestContext context =
        MakeContext(registry, selection);
    context.TextureBake = &textureBake;

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::EditorTextureBakeCommandResult result =
        Runtime::ApplyEditorTextureBakeCommand(
            context,
            Runtime::EditorTextureBakeCommand{
                .StableEntityId = stableId,
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Normal,
                .SourceDomain =
                    Runtime::GeometryElementDomain::MeshVertex,
                .ExpectedValueKind =
                    Geometry::PropertyValueKind::Vec3,
                .PropertyName = "v:normal",
                .Encoder = Runtime::PropertyTextureBakeEncoding::Normal,
                .Width = 64u,
                .Height = 64u,
                .GeneratedKey = "normal",
                .BindGeneratedTexture = true,
            });

    ASSERT_EQ(
        result.Status,
        Runtime::EditorCommandStatus::InvalidVisualizationProperty);
    EXPECT_EQ(
        result.BakeStatus,
        Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
    EXPECT_FALSE(result.Scheduled);
    EXPECT_FALSE(result.GeneratedTexture.IsValid());
    EXPECT_FALSE(result.BoundGeneratedTexture);

    EXPECT_EQ(
        registry.Raw()
            .get<Runtime::GeometryPresentationRuntimeState>(mesh)
            .RecipeGeneration,
        7u);
}
TEST(SandboxEditorUi, UnavailableTextureBakeModuleHasNoCpuFallback)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Tests::MockDevice genericDevice;
    Runtime::TextureBakeService textureBake{};

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "UnavailableNormalBakeMesh");
    AddTriangleMeshSource(registry, mesh);
    SetNormals(registry.Raw().get<GS::Vertices>(mesh));
    AttachGeometryPresentation(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Intrinsic::Tests::EditorFeatureTestContext context =
        MakeContext(registry, selection, true, nullptr, &genericDevice);
    context.TextureBake = &textureBake;

    const Runtime::EditorTextureBakeCommandResult result =
        Runtime::ApplyEditorTextureBakeCommand(
            context,
            Runtime::EditorTextureBakeCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Normal,
                .SourceDomain =
                    Runtime::GeometryElementDomain::MeshVertex,
                .ExpectedValueKind =
                    Geometry::PropertyValueKind::Vec3,
                .PropertyName = "v:normal",
                .Encoder = Runtime::PropertyTextureBakeEncoding::Normal,
                .Width = 64u,
                .Height = 64u,
                .GeneratedKey = "normal",
                .BindGeneratedTexture = true,
            });

    EXPECT_EQ(
        result.Status,
        Runtime::EditorCommandStatus::InvalidVisualizationProperty);
    EXPECT_EQ(
        result.BakeStatus,
        Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
    EXPECT_FALSE(result.Scheduled);
    EXPECT_FALSE(result.BoundGeneratedTexture);
    EXPECT_NE(result.Diagnostic.find("operational GPU"), std::string::npos);

    EXPECT_EQ(
        registry.Raw()
            .get<Runtime::GeometryPresentationRuntimeState>(mesh)
            .RecipeGeneration,
        7u);
}
TEST(SandboxEditorUi, TextureBakeModuleAvailabilityPrecedesAssetCreation)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Tests::MockDevice device;
    Runtime::TextureBakeService textureBake{};

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "MissingAssetServiceBakeMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    (void)vertices.Properties.GetOrAdd<glm::vec4>(
        "v:paint",
        glm::vec4{1.0f});
    AttachGeometryPresentation(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Intrinsic::Tests::EditorFeatureTestContext context =
        MakeContext(registry, selection, true, nullptr, &device);
    context.TextureBake = &textureBake;

    const Runtime::EditorTextureBakeCommandResult result =
        Runtime::ApplyEditorTextureBakeCommand(
            context,
            Runtime::EditorTextureBakeCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Albedo,
                .SourceDomain =
                    Runtime::GeometryElementDomain::MeshVertex,
                .ExpectedValueKind =
                    Geometry::PropertyValueKind::Vec4,
                .PropertyName = "v:paint",
                .Encoder =
                    Runtime::PropertyTextureBakeEncoding::RgbaColor,
                .Width = 4u,
                .Height = 4u,
                .GeneratedKey = "paint",
                .BindGeneratedTexture = true,
            });

    EXPECT_EQ(
        result.Status,
        Runtime::EditorCommandStatus::InvalidVisualizationProperty);
    EXPECT_EQ(
        result.BakeStatus,
        Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
}
TEST(SandboxEditorUi, TextureBakeRequiresOperationalGpuBackend)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Assets::AssetService assets;
    Tests::MockDevice device;
    device.Operational = false;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TextureBakeMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec4>("v:paint", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    AttachGeometryPresentation(registry, mesh);

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Intrinsic::Tests::EditorFeatureTestContext context =
        MakeContext(registry, selection, true, nullptr, &device);
    context.AssetService = &assets;
    Runtime::TextureBakeService textureBake{};
    context.TextureBake = &textureBake;

    const Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    const Runtime::EditorTextureBakeControlsModel& bake =
        frame.Inspector.TextureBake;
    ASSERT_TRUE(bake.HasSelectedEntity);
    EXPECT_TRUE(bake.IsMesh);
    EXPECT_FALSE(bake.HasRuntimeBakeCommand);
    EXPECT_TRUE(bake.Uv.HasTexcoords);
    EXPECT_TRUE(bake.Uv.TexcoordsFinite);
    EXPECT_FALSE(bake.CanBake);
    EXPECT_EQ(bake.DisabledReason,
              "texture baking requires an operational GPU backend");

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::EditorTextureBakeCommandResult result =
        Runtime::ApplyEditorTextureBakeCommand(
            context,
            Runtime::EditorTextureBakeCommand{
                .StableEntityId = stableId,
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::GeometryPresentationSlotSemantic::Albedo,
                .SourceDomain = Runtime::GeometryElementDomain::MeshVertex,
                .ExpectedValueKind = Geometry::PropertyValueKind::Vec4,
                .PropertyName = "v:paint",
                .Encoder = Runtime::PropertyTextureBakeEncoding::RgbaColor,
                .Width = 4u,
                .Height = 4u,
                .GeneratedKey = "paint",
                .BindGeneratedTexture = true,
            });

    EXPECT_EQ(result.Status,
              Runtime::EditorCommandStatus::InvalidVisualizationProperty);
    EXPECT_EQ(result.BakeStatus,
              Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
    EXPECT_NE(result.Diagnostic.find("operational GPU"), std::string::npos);
}

// UI-032 — preset buttons switch the scalar source but must preserve styling
// (colormap, isoline width/color, highlight isovalues) already configured on
// the target lane, and the styling fields must round-trip through the config
// command onto the component.
