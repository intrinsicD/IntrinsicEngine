// ARCH-006 runtime Sandbox editor MeshMethods contract partition.
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

void InstallSandboxDefaultRuntimePolicies(Runtime::Engine& engine)
    {
        (void)Runtime::RegisterSandboxDefaultRuntimePolicies(engine);
    }

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

[[nodiscard]] const Runtime::SandboxEditorTextureBakeSourceRow*
    FindTextureBakeSource(
        const Runtime::SandboxEditorTextureBakeControlsModel& model,
        const std::string& name)
    {
        for (const Runtime::SandboxEditorTextureBakeSourceRow& row :
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

[[nodiscard]] Runtime::ProgressivePresentationBindings
    MakeProgressiveMeshPresentationBindings()
    {
        Runtime::ProgressiveSlotBinding albedo{};
        albedo.Semantic = Runtime::ProgressiveSlotSemantic::Albedo;
        albedo.SourceKind = Runtime::ProgressiveSlotSourceKind::UniformDefault;
        albedo.UniformDefault = Runtime::ProgressiveDefaultValue{
            .Kind = Runtime::ProgressivePropertyValueKind::Vec4,
            .Vector = glm::vec4{0.2f, 0.4f, 0.8f, 1.0f},
        };
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
        normal.LastDiagnostic = "waiting for normal bake";

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

[[nodiscard]] std::string ReadRepositoryTextFile(
        const std::filesystem::path& relativePath)
    {
        const std::filesystem::path path =
            std::filesystem::path{ENGINE_ROOT_DIR} / relativePath;
        std::ifstream file{path};
        if (!file)
            return {};
        return std::string{std::istreambuf_iterator<char>{file},
                           std::istreambuf_iterator<char>{}};
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

class WaitForConditionApplication final : public Runtime::IApplication
    {
    public:
        explicit WaitForConditionApplication(
            std::function<bool(Runtime::Engine&)> ready,
            std::uint32_t maxFrames = 512u)
            : m_Ready(std::move(ready))
            , m_MaxFrames(maxFrames)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_ObservedFrames;
            if ((m_Ready && m_Ready(engine)) || m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::function<bool(Runtime::Engine&)> m_Ready{};
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

[[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

[[nodiscard]] bool MeshHasVertexProperty(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::string_view propertyName)
    {
        if (!engine.GetScene().IsValid(entity))
        {
            return false;
        }

        auto& raw = engine.GetScene().Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        return view.Valid() &&
            view.ActiveDomain == GS::Domain::Mesh &&
            view.VertexSource != nullptr &&
            view.VertexSource->Properties.Exists(propertyName);
    }

[[nodiscard]] bool DirectMeshPostProcessReady(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity)
    {
        return MeshHasVertexProperty(engine, entity, "v:texcoord") &&
            MeshHasVertexProperty(engine, entity, "v:normal") &&
            engine.GetObjectSpaceNormalBakeQueueDiagnosticsForTest()
                .NonOperationalNoOps > 0u;
    }

void ExpectMeshVertexNormalsNear(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::span<const glm::vec3> expected)
    {
        ASSERT_TRUE(engine.GetScene().IsValid(entity));
        auto& raw = engine.GetScene().Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        ASSERT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        ASSERT_NE(view.VertexSource, nullptr);

        const auto normals = view.VertexSource->Properties.Get<glm::vec3>(PN::kNormal);
        ASSERT_TRUE(normals);
        ASSERT_EQ(normals.Vector().size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_NEAR(normals.Vector()[i].x, expected[i].x, 1.0e-5f) << i;
            EXPECT_NEAR(normals.Vector()[i].y, expected[i].y, 1.0e-5f) << i;
            EXPECT_NEAR(normals.Vector()[i].z, expected[i].z, 1.0e-5f) << i;
        }
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

[[nodiscard]] std::string_view SourceRange(
        const std::string& source,
        const std::string_view beginMarker,
        const std::string_view endMarker)
    {
        const std::size_t begin = source.find(beginMarker);
        if (begin == std::string::npos)
            return {};
        const std::size_t end = source.find(endMarker, begin + beginMarker.size());
        if (end == std::string::npos || end <= begin)
            return {};
        return std::string_view{source.data() + begin, end - begin};
    }
}

TEST(SandboxEditorUi, MeshDenoiseCommandPublishesPositionsAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const std::vector<glm::vec3> original =
        MeshVertexPositions(registry, mesh);
    ASSERT_EQ(original.size(), 4u);

    const Runtime::SandboxEditorMeshDenoiseResult result =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
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

    context.LastMeshDenoiseResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshDenoiseResult> completedResult{};
    context.MethodResultSinks.MeshDenoise =
        [&completedResult](Runtime::SandboxEditorMeshDenoiseResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const std::vector<glm::vec3> original =
        MeshVertexPositions(registry, mesh);

    const Runtime::SandboxEditorMeshDenoiseResult result =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshDenoise.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_FALSE(completedResult.has_value());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshDenoise =
        [&completedSinkCalled](Runtime::SandboxEditorMeshDenoiseResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "StaleDenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshDenoiseResult result =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(registry.Raw().get<GS::Vertices>(mesh),
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
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

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult result =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
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
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorPointCloudOutlierRemovalResult>
        completedResult{};
    context.MethodResultSinks.PointCloudOutlierRemoval =
        [&completedResult](
            Runtime::SandboxEditorPointCloudOutlierRemovalResult result)
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

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult queued =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });

    EXPECT_EQ(queued.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(queued.OriginalCount, originalCount);
    EXPECT_EQ(queued.KeptCount, originalCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    Runtime::DerivedJobQueueSnapshot pending = jobs.SnapshotAll();
    ASSERT_EQ(pending.Entries.size(), 1u);
    EXPECT_EQ(pending.Entries[0].Name,
              "Sandbox.PointCloudOutlierRemoval.CPU");
    EXPECT_EQ(pending.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.PointCloudOutlierRemoval =
        [&completedSinkCalled](
            Runtime::SandboxEditorPointCloudOutlierRemovalResult)
        {
            completedSinkCalled = true;
        };

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "StaleOutlierCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult queued =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_EQ(queued.Status, Runtime::SandboxEditorCommandStatus::Pending);

    std::vector<glm::vec3> stalePositions = positions;
    for (glm::vec3& position : stalePositions)
        position.x += 100.0f;
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), stalePositions);

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
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalRadiusPublishesAndFailsClosed)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud = MakeSelectable(registry, "RadiusCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(cloud);

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult radius =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Radius,
                .SearchRadius = 0.15f,
                .MinNeighbors = 3u,
            });
    ASSERT_TRUE(radius.Succeeded()) << radius.Message;
    EXPECT_GE(radius.RejectedCount, 2u);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), radius.KeptCount);

    // Fail-closed: non-positive radius is rejected before any mutation.
    const Runtime::SandboxEditorPointCloudOutlierRemovalResult badRadius =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Radius,
                .SearchRadius = 0.0f,
                .MinNeighbors = 3u,
            });
    EXPECT_EQ(badRadius.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    // Missing scene fails closed.
    const Runtime::SandboxEditorPointCloudOutlierRemovalResult missingScene =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .KNeighbors = 8u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);

    // A mesh entity is the wrong domain for point-cloud outlier removal.
    const ECS::EntityHandle mesh = MakeSelectable(registry, "WrongDomainMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const Runtime::SandboxEditorPointCloudOutlierRemovalResult wrongDomain =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .KNeighbors = 8u,
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalPreservesSurvivingPointProperties)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
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
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult result =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
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
}
TEST(SandboxEditorUi, PointCloudOutlierRemovalRespectsDeletedSlots)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
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

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult result =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
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
}
TEST(SandboxEditorUi, MeshDenoiseCommandFailsClosedForInvalidTargetsAndUnavailableKernel)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshDenoiseResult missingScene =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "CloudWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });
    const Runtime::SandboxEditorMeshDenoiseResult wrongDomain =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseFailMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::vector<glm::vec3> before =
        MeshVertexPositions(registry, mesh);
    const Runtime::SandboxEditorMeshDenoiseResult invalidParams =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .NormalIterations = 0u,
            });
    EXPECT_EQ(invalidParams.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    context.MeshDenoiseKernelAvailable = false;
    const Runtime::SandboxEditorMeshDenoiseResult unavailable =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailable.Error, Core::ErrorCode::InvalidState);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const Runtime::SandboxEditorDomainWindowModel unavailableModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_FALSE(unavailableModel.Processing.MeshDenoiseAvailable);
}
TEST(SandboxEditorUi, MeshCurvatureCommandPublishesCanonicalPropertiesAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
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

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId = stableId,
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
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

    context.LastMeshCurvatureResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshCurvatureResult> completedResult{};
    context.MethodResultSinks.MeshCurvature =
        [&completedResult](Runtime::SandboxEditorMeshCurvatureResult result)
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

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId = stableId,
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.VertexSlotCount, 4u);
    EXPECT_TRUE(result.DirectionsRequested);
    EXPECT_TRUE(result.DirectionsAvailable);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshCurvature.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshCurvature =
        [&completedSinkCalled](Runtime::SandboxEditorMeshCurvatureResult)
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

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    auto currentMean = properties.Get<double>(PN::kMeanCurvature);
    ASSERT_TRUE(currentMean.IsValid());
    for (double& value : currentMean.Vector())
        value = 2.0;

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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.MeshCurvatureDirectionsAvailable = false;

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CurvatureScalarOnlyMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
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

    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshCurvatureAvailable);
    EXPECT_FALSE(model.Processing.MeshCurvatureDirectionsAvailable);
}
TEST(SandboxEditorUi, MeshCurvatureCommandFailsClosedForInvalidTargetsAndConflicts)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshCurvatureResult missingScene =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "CurvatureWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    const Runtime::SandboxEditorMeshCurvatureResult wrongDomain =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CurvatureUnavailableMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    context.MeshCurvatureKernelAvailable = false;
    const Runtime::SandboxEditorMeshCurvatureResult unavailable =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailable.Error, Core::ErrorCode::InvalidState);
    EXPECT_FALSE(registry.Raw().get<GS::Vertices>(mesh).Properties.Exists(
        PN::kMeanCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    context.MeshCurvatureKernelAvailable = true;
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    (void)properties.Add<float>(std::string{PN::kMeanCurvature}, 0.0f);
    const Runtime::SandboxEditorMeshCurvatureResult conflict =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(conflict.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(conflict.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
}
TEST(SandboxEditorUi, MeshRemeshCommandReplacesTopologyAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "UniformRemesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Vertices, 0u);
    ASSERT_GT(before.Faces, 0u);

    const Runtime::SandboxEditorMeshRemeshResult uniform =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = stableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Uniform,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::MeanCurvature,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .PreserveBoundary = false,
                .ProjectToSurface = true,
            });

    ASSERT_TRUE(uniform.Succeeded()) << uniform.Message;
    EXPECT_EQ(uniform.Mode, Runtime::SandboxEditorMeshRemeshMode::Uniform);
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

    context.LastMeshRemeshResult = &uniform;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshRemeshAvailable);
    ASSERT_TRUE(model.Processing.LastMeshRemeshResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshRemeshResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshRemeshResult->OutputFaceCount,
              uniform.OutputFaceCount);

    const ECS::EntityHandle adaptiveMesh =
        MakeSelectable(registry, "AdaptiveRemesh");
    AddIcosahedronMeshSource(registry, adaptiveMesh);
    const Runtime::SandboxEditorMeshRemeshResult adaptive =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(adaptiveMesh),
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Adaptive,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .ApproximationError = 0.01,
                .PreserveBoundary = false,
                .ProjectToSurface = false,
            });
    ASSERT_TRUE(adaptive.Succeeded()) << adaptive.Message;
    EXPECT_EQ(adaptive.Mode, Runtime::SandboxEditorMeshRemeshMode::Adaptive);
    EXPECT_EQ(adaptive.SizingLaw,
              Runtime::SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshRemeshResult> completedResult{};
    context.MethodResultSinks.MeshRemesh =
        [&completedResult](Runtime::SandboxEditorMeshRemeshResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedRemesh");
    AddIcosahedronMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshRemeshResult result =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = stableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Uniform,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::MeanCurvature,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .PreserveBoundary = false,
                .ProjectToSurface = false,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshRemesh.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(completedResult.has_value());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle loopMesh = MakeSelectable(registry, "LoopSubdivide");
    AddDenoiseTetraMeshSource(registry, loopMesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, loopMesh));
    const MeshCounts beforeLoop = SourceMeshCounts(registry, loopMesh);
    const std::uint32_t loopStableId =
        Runtime::SelectionController::ToStableEntityId(loopMesh);

    const Runtime::SandboxEditorMeshSubdivideResult loop =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = loopStableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
                .PreserveLoopFeatureEdges = true,
            });

    ASSERT_TRUE(loop.Succeeded()) << loop.Message;
    EXPECT_EQ(loop.Operator,
              Runtime::SandboxEditorMeshSubdivideOperator::Loop);
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
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshSubdivideAvailable);
    ASSERT_TRUE(model.Processing.LastMeshSubdivideResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshSubdivideResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshSubdivideResult->OutputFaceCount,
              loop.OutputFaceCount);

    const ECS::EntityHandle catmullMesh =
        MakeSelectable(registry, "CatmullClarkSubdivide");
    AddDenoiseTetraMeshSource(registry, catmullMesh);
    const Runtime::SandboxEditorMeshSubdivideResult catmull =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(catmullMesh),
                .Operator =
                    Runtime::SandboxEditorMeshSubdivideOperator::CatmullClark,
                .Iterations = 1u,
            });
    ASSERT_TRUE(catmull.Succeeded()) << catmull.Message;
    EXPECT_EQ(catmull.Operator,
              Runtime::SandboxEditorMeshSubdivideOperator::CatmullClark);
    EXPECT_GT(catmull.OutputVertexCount, catmull.InputVertexCount);
    EXPECT_GT(catmull.OutputFaceCount, catmull.InputFaceCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(catmullMesh));

    const ECS::EntityHandle sqrt3Mesh =
        MakeSelectable(registry, "Sqrt3Subdivide");
    AddDenoiseTetraMeshSource(registry, sqrt3Mesh);
    const Runtime::SandboxEditorMeshSubdivideResult sqrt3 =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(sqrt3Mesh),
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Sqrt3,
                .Iterations = 1u,
            });
    ASSERT_TRUE(sqrt3.Succeeded()) << sqrt3.Message;
    EXPECT_EQ(sqrt3.Operator,
              Runtime::SandboxEditorMeshSubdivideOperator::Sqrt3);
    EXPECT_GT(sqrt3.OutputVertexCount, sqrt3.InputVertexCount);
    EXPECT_GT(sqrt3.OutputFaceCount, sqrt3.InputFaceCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(sqrt3Mesh));
}
TEST(SandboxEditorUi, MeshSubdivideRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshSubdivideResult> completedResult{};
    context.MethodResultSinks.MeshSubdivide =
        [&completedResult](Runtime::SandboxEditorMeshSubdivideResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedSubdivide");
    AddDenoiseTetraMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSubdivideResult result =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = stableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshSubdivide.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshSubdivide =
        [&completedSinkCalled](Runtime::SandboxEditorMeshSubdivideResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "StaleSubdivide");
    AddDenoiseTetraMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSubdivideResult result =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = stableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    const std::vector<glm::vec3> stalePositions{
        glm::vec3{2.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 2.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 2.0f},
        glm::vec3{2.0f, 2.0f, 2.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(mesh), stalePositions);

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
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh),
                                stalePositions);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
}
TEST(SandboxEditorUi, MeshSimplifyCommandReducesFaceCountAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "SimplifyMesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 12u);

    const Runtime::SandboxEditorMeshSimplifyResult simplified =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });

    ASSERT_TRUE(simplified.Succeeded()) << simplified.Message;
    EXPECT_EQ(simplified.Metric,
              Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM);
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
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshSimplifyAvailable);
    ASSERT_TRUE(model.Processing.LastMeshSimplifyResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshSimplifyResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshSimplifyResult->OutputFaceCount,
              simplified.OutputFaceCount);

    const ECS::EntityHandle classicalMesh =
        MakeSelectable(registry, "SimplifyClassical");
    AddIcosahedronMeshSource(registry, classicalMesh);
    const Runtime::SandboxEditorMeshSimplifyResult classical =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = Runtime::SelectionController::ToStableEntityId(
                    classicalMesh),
                .Metric =
                    Runtime::SandboxEditorMeshSimplifyMetric::ClassicalQEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });
    ASSERT_TRUE(classical.Succeeded()) << classical.Message;
    EXPECT_EQ(classical.Metric,
              Runtime::SandboxEditorMeshSimplifyMetric::ClassicalQEM);
    EXPECT_LT(classical.OutputFaceCount, before.Faces);
    EXPECT_EQ(classical.SharpFeatureVerticesPinned, 0u);
    EXPECT_EQ(classical.SeamVerticesPinned, 0u);
}
TEST(SandboxEditorUi, MeshSimplifyRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshSimplifyResult> completedResult{};
    context.MethodResultSinks.MeshSimplify =
        [&completedResult](Runtime::SandboxEditorMeshSimplifyResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedSimplify");
    AddIcosahedronMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 12u);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSimplifyResult result =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshSimplify.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(completedResult.has_value());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "SimplifyGuard");
    AddIcosahedronMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSimplifyResult invalid =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .TargetFaces = 0u,
                .MaxError = 0.0,
            });
    EXPECT_EQ(invalid.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(invalid.Succeeded());

    const Runtime::SandboxEditorMeshSimplifyResult stale =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId + 4242u,
                .TargetFaces = 8u,
            });
    EXPECT_EQ(stale.Status, Runtime::SandboxEditorCommandStatus::StaleEntity);

    context.MeshSimplifyKernelAvailable = false;
    const Runtime::SandboxEditorMeshSimplifyResult unavailable =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .TargetFaces = 8u,
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
}
TEST(SandboxEditorUi, MeshSimplifyPreservesUvSeamsWhenTexcoordsPresent)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

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

    const Runtime::SandboxEditorMeshSimplifyResult result =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM,
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshRemeshResult missingRemeshScene =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingRemeshScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingRemeshScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::SandboxEditorMeshSubdivideResult missingSubdivideScene =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingSubdivideScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingSubdivideScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "TopologyWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    const Runtime::SandboxEditorMeshRemeshResult wrongRemeshDomain =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = cloudStableId,
            });
    EXPECT_EQ(wrongRemeshDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    const Runtime::SandboxEditorMeshSubdivideResult wrongSubdivideDomain =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = cloudStableId,
            });
    EXPECT_EQ(wrongSubdivideDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TopologyFailMesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);

    const Runtime::SandboxEditorMeshRemeshResult invalidRemesh =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Iterations = 0u,
            });
    EXPECT_EQ(invalidRemesh.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    const Runtime::SandboxEditorMeshSubdivideResult invalidSubdivide =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = meshStableId,
                .Iterations = 0u,
            });
    EXPECT_EQ(invalidSubdivide.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshAdaptiveKernelAvailable = false;
    const Runtime::SandboxEditorMeshRemeshResult unavailableAdaptive =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Adaptive,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
            });
    EXPECT_EQ(unavailableAdaptive.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailableAdaptive.Error, Core::ErrorCode::InvalidState);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshAdaptiveKernelAvailable = true;
    context.MeshRemeshErrorBoundedSizingAvailable = false;
    const Runtime::SandboxEditorMeshRemeshResult unavailableSizing =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Adaptive,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .ApproximationError = 0.01,
            });
    EXPECT_EQ(unavailableSizing.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshErrorBoundedSizingAvailable = true;
    context.MeshSubdivideSqrt3KernelAvailable = false;
    const Runtime::SandboxEditorMeshSubdivideResult unavailableSqrt3 =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = meshStableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Sqrt3,
                .Iterations = 1u,
            });
    EXPECT_EQ(unavailableSqrt3.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailableSqrt3.Error, Core::ErrorCode::InvalidState);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    context.MeshRemeshUniformKernelAvailable = false;
    context.MeshRemeshAdaptiveKernelAvailable = false;
    context.MeshSubdivideLoopKernelAvailable = false;
    context.MeshSubdivideCatmullClarkKernelAvailable = false;
    const Runtime::SandboxEditorDomainWindowModel unavailableModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
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

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    constexpr std::array<GN::AveragingMode, 4> kWeightings{{
        GN::AveragingMode::UniformFace,
        GN::AveragingMode::AreaWeighted,
        GN::AveragingMode::AngleWeighted,
        GN::AveragingMode::MaxWeighted,
    }};

    Runtime::SandboxEditorMeshVertexNormalsResult lastResult{};
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

        lastResult = Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
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
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshVertexNormalsResult>
        completedResult{};
    context.MethodResultSinks.MeshVertexNormals =
        [&completedResult](
            Runtime::SandboxEditorMeshVertexNormalsResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedNormalsMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    ASSERT_FALSE(properties.Exists(PN::kNormal));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshVertexNormalsResult result =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId = stableId,
                .Weighting = GN::AveragingMode::AreaWeighted,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.VertexSlotCount, 3u);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(properties.Exists(PN::kNormal));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshVertexNormals.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(properties.Exists(PN::kNormal));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
}
TEST(SandboxEditorUi,
     GraphAndPointCloudVertexNormalsRequestsQueueDerivedJobsAndPublishOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorGraphVertexNormalsResult>
        completedGraph{};
    std::optional<Runtime::SandboxEditorPointCloudVertexNormalsResult>
        completedCloud{};
    context.MethodResultSinks.GraphVertexNormals =
        [&completedGraph](
            Runtime::SandboxEditorGraphVertexNormalsResult result)
        {
            completedGraph = std::move(result);
        };
    context.MethodResultSinks.PointCloudVertexNormals =
        [&completedCloud](
            Runtime::SandboxEditorPointCloudVertexNormalsResult result)
        {
            completedCloud = std::move(result);
        };

    const ECS::EntityHandle graph = MakeSelectable(registry, "QueuedGraphNormals");
    AddPlanarCycleGraphSource(registry, graph);
    auto& graphProperties = registry.Raw().get<GS::Nodes>(graph).Properties;
    ASSERT_FALSE(graphProperties.Exists(PN::kNormal));

    const Runtime::SandboxEditorGraphVertexNormalsResult graphResult =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(graph),
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
                .OrientTowardFallback = true,
            });

    EXPECT_EQ(graphResult.Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(graphResult.VertexSlotCount, 4u);
    EXPECT_EQ(graphResult.EdgeSlotCount, 4u);
    EXPECT_FALSE(graphProperties.Exists(PN::kNormal));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.GraphVertexNormals.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedGraph.has_value());
    EXPECT_FALSE(graphProperties.Exists(PN::kNormal));
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
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

    const Runtime::SandboxEditorPointCloudVertexNormalsResult cloudResult =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .KNeighbors = 4u,
                .MinimumNeighbors = 2u,
                .UseRadiusSearch = false,
                .Orientation = PCN::OrientationMode::MinimumSpanningTree,
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
            });

    EXPECT_EQ(cloudResult.Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(cloudResult.PointSlotCount, 9u);
    EXPECT_FALSE(cloudProperties.Exists(PN::kNormal));

    queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 2u);
    EXPECT_EQ(queued.Entries[1].Name, "Sandbox.PointCloudVertexNormals.CPU");
    EXPECT_EQ(queued.Entries[1].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedCloud.has_value());
    EXPECT_FALSE(cloudProperties.Exists(PN::kNormal));
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
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
}
TEST(SandboxEditorUi, VertexNormalsDerivedJobsDiscardStaleSourcesBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    {
        Runtime::SandboxEditorContext context = MakeContext(registry, selection);
        Runtime::StreamingExecutor executor{};
        Runtime::DerivedJobRegistry jobs{executor};
        AttachDerivedJobCommands(context, jobs);
        bool completedSinkCalled = false;
        context.MethodResultSinks.MeshVertexNormals =
            [&completedSinkCalled](
                Runtime::SandboxEditorMeshVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle mesh =
            MakeSelectable(registry, "StaleMeshNormals");
        AddTriangleMeshSource(registry, mesh);
        const Runtime::SandboxEditorMeshVertexNormalsResult result =
            Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
                context,
                Runtime::SandboxEditorMeshVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(mesh),
                });
        ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

        SetPositions(registry.Raw().get<GS::Vertices>(mesh),
                     {
                         {10.0f, 0.0f, 0.0f},
                         {11.0f, 0.0f, 0.0f},
                         {12.0f, 0.0f, 0.0f},
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
        EXPECT_FALSE(registry.Raw().get<GS::Vertices>(mesh)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    }

    {
        Runtime::SandboxEditorContext context = MakeContext(registry, selection);
        Runtime::StreamingExecutor executor{};
        Runtime::DerivedJobRegistry jobs{executor};
        AttachDerivedJobCommands(context, jobs);
        bool completedSinkCalled = false;
        context.MethodResultSinks.GraphVertexNormals =
            [&completedSinkCalled](
                Runtime::SandboxEditorGraphVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle graph =
            MakeSelectable(registry, "StaleGraphNormals");
        AddPlanarCycleGraphSource(registry, graph);
        const Runtime::SandboxEditorGraphVertexNormalsResult result =
            Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
                context,
                Runtime::SandboxEditorGraphVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(graph),
                });
        ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

        SetNodePositions(registry.Raw().get<GS::Nodes>(graph),
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
        EXPECT_FALSE(registry.Raw().get<GS::Nodes>(graph)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));
    }

    {
        Runtime::SandboxEditorContext context = MakeContext(registry, selection);
        Runtime::StreamingExecutor executor{};
        Runtime::DerivedJobRegistry jobs{executor};
        AttachDerivedJobCommands(context, jobs);
        bool completedSinkCalled = false;
        context.MethodResultSinks.PointCloudVertexNormals =
            [&completedSinkCalled](
                Runtime::SandboxEditorPointCloudVertexNormalsResult)
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
        const Runtime::SandboxEditorPointCloudVertexNormalsResult result =
            Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
                context,
                Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(cloud),
                    .KNeighbors = 3u,
                    .MinimumNeighbors = 2u,
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
        EXPECT_FALSE(registry.Raw().get<GS::Vertices>(cloud)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    }
}
TEST(SandboxEditorUi, MeshVertexNormalsCommandSurvivesPendingDirectMeshPostProcess)
{
    TmpFile meshFile(
        "runtime_mesh_normals_postprocess_race.obj",
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

    std::optional<ECS::EntityHandle> meshEntity{};
    std::optional<std::uint32_t> stableId{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            },
            128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    auto imported = engine.GetAssetImportPipeline().ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());

    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    stableId = Runtime::SelectionController::ToStableEntityId(*meshEntity);

    ExpectMeshVertexNormalsNear(
        engine,
        *meshEntity,
        std::array{
            glm::vec3{1.0f, 0.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
        });

    Runtime::SandboxEditorContext context =
        MakeContext(engine.GetScene(), engine.GetSelectionController());
    const Runtime::SandboxEditorMeshVertexNormalsResult recomputed =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId = *stableId,
                .Weighting = GN::AveragingMode::AreaWeighted,
            });
    ASSERT_TRUE(recomputed.Succeeded()) << recomputed.Message;
    ExpectMeshVertexNormalsNear(
        engine,
        *meshEntity,
        std::array{
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
        });

    engine.Run();

    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        engine.GetMaterialTextureAssetBindingsForTest(*stableId);
    if (bindings.has_value())
    {
        EXPECT_FALSE(bindings->Normal.IsValid());
    }
    const auto& diagnostics =
        engine.GetObjectSpaceNormalBakeQueueDiagnosticsForTest();
    EXPECT_EQ(diagnostics.NonOperationalNoOps, 1u);
    EXPECT_EQ(engine.GetPendingObjectSpaceNormalBakeCountForTest(), 0u);
    ExpectMeshVertexNormalsNear(
        engine,
        *meshEntity,
        std::array{
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
        });

    engine.Shutdown();
}
TEST(SandboxEditorUi, MeshVertexNormalsCommandFailsClosedForInvalidTargets)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshVertexNormalsCommand validShape{
        .StableEntityId = 1u,
    };

    const Runtime::SandboxEditorMeshVertexNormalsResult missingScene =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            Runtime::SandboxEditorContext{},
            validShape);
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::SandboxEditorMeshVertexNormalsResult stale =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
            });
    EXPECT_EQ(stale.Status, Runtime::SandboxEditorCommandStatus::StaleEntity);
    EXPECT_EQ(stale.Error, Core::ErrorCode::ResourceNotFound);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    const Runtime::SandboxEditorMeshVertexNormalsResult wrongDomain =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TypeConflictMesh");
    AddTriangleMeshSource(registry, mesh);
    const auto conflictingNormals = registry.Raw()
                                        .get<GS::Vertices>(mesh)
                                        .Properties.GetOrAdd<float>(
                                            std::string{PN::kNormal},
                                            0.0f);
    ASSERT_TRUE(conflictingNormals);
    const Runtime::SandboxEditorMeshVertexNormalsResult conflict =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(conflict.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(conflict.NormalStatus, GN::RecomputeStatus::PropertyTypeConflict);
    EXPECT_EQ(conflict.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    context.LastMeshVertexNormalsResult = &conflict;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed));
}
TEST(SandboxEditorUi, GraphAndPointCloudVertexNormalsCommandsPublishCanonicalNormals)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle graph = MakeSelectable(registry, "NormalGraph");
    AddPlanarCycleGraphSource(registry, graph);
    const std::uint32_t graphStableId =
        Runtime::SelectionController::ToStableEntityId(graph);

    const Runtime::SandboxEditorGraphVertexNormalsResult graphResult =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
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
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
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

    const Runtime::SandboxEditorPointCloudVertexNormalsResult cloudResult =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
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
    const Runtime::SandboxEditorDomainWindowModel cloudModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorGraphVertexNormalsResult missingGraphScene =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingGraphScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
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
    const Runtime::SandboxEditorGraphVertexNormalsResult graphWrongDomain =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        cloudWrongDomain),
            });
    EXPECT_EQ(graphWrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloudWrongDomain));

    const Runtime::SandboxEditorPointCloudVertexNormalsResult invalidPointParams =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        cloudWrongDomain),
                .KNeighbors = 0u,
            });
    EXPECT_EQ(invalidPointParams.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
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
    const Runtime::SandboxEditorGraphVertexNormalsResult graphConflictResult =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        graphConflict),
            });
    EXPECT_EQ(graphConflictResult.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
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
    const Runtime::SandboxEditorPointCloudVertexNormalsResult pointWrongDomain =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        meshWrongDomain),
            });
    EXPECT_EQ(pointWrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
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
    const Runtime::SandboxEditorPointCloudVertexNormalsResult
        pointConflictResult =
            Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
                context,
                Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(
                            cloudConflict),
                    .KNeighbors = 3u,
                    .MinimumNeighbors = 2u,
                });
    EXPECT_EQ(pointConflictResult.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
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

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const Runtime::SandboxEditorPanelFrame before =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(before.Inspector.TextureBake.HasSelectedEntity);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.UvRegenerationAvailable);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.HasTexcoords);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.TexcoordCountMatchesVertices);
    EXPECT_FALSE(before.Inspector.TextureBake.Uv.TexcoordsFinite);
    EXPECT_FALSE(before.Inspector.TextureBake.CanBake);

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });

    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Applied);
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

    const Runtime::SandboxEditorPanelFrame after =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(after.Inspector.TextureBake.Uv.TexcoordsFinite);
    EXPECT_TRUE(after.Inspector.TextureBake.Uv.CheckerPreviewAvailable);
}
TEST(SandboxEditorUi, UvRegenerationRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorUvRegenerationCommandResult>
        completedResult{};
    context.MethodResultSinks.UvRegeneration =
        [&completedResult](
            Runtime::SandboxEditorUvRegenerationCommandResult result)
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
    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Diagnostic.find("queued"), std::string::npos);
    EXPECT_FALSE(texcoordsFinite());
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.UvRegeneration.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(texcoordsFinite());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);

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

    const Runtime::SandboxEditorUvRegenerationCommand command{
        .StableEntityId =
            Runtime::SelectionController::ToStableEntityId(mesh),
        .Resolution = 64u,
        .Padding = 2u,
    };

    const Runtime::SandboxEditorUvRegenerationCommandResult first =
        Runtime::ApplySandboxEditorUvRegenerationCommand(context, command);
    ASSERT_EQ(first.Status, Runtime::SandboxEditorCommandStatus::Pending);

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    context.DerivedJobs = &queued;

    const Runtime::SandboxEditorUvRegenerationCommandResult duplicate =
        Runtime::ApplySandboxEditorUvRegenerationCommand(context, command);
    EXPECT_EQ(duplicate.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(duplicate.Diagnostic.find("already has an active"),
              std::string::npos);
    EXPECT_NE(duplicate.Diagnostic.find("job 0:1"), std::string::npos);
    EXPECT_EQ(jobs.SnapshotAll().Entries.size(), 1u);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot complete = jobs.SnapshotAll();
    ASSERT_EQ(complete.Entries.size(), 1u);
    EXPECT_EQ(complete.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    context.DerivedJobs = &complete;

    const Runtime::SandboxEditorUvRegenerationCommandResult rerun =
        Runtime::ApplySandboxEditorUvRegenerationCommand(context, command);
    EXPECT_EQ(rerun.Status, Runtime::SandboxEditorCommandStatus::Pending);
    Runtime::DerivedJobQueueSnapshot afterRerun = jobs.SnapshotAll();
    ASSERT_EQ(afterRerun.Entries.size(), 2u);
    EXPECT_EQ(afterRerun.Entries[1].Status, Runtime::DerivedJobStatus::Queued);
}
TEST(SandboxEditorUi, UvRegenerationDerivedJobDiscardsStaleSource)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.UvRegeneration =
        [&completedSinkCalled](
            Runtime::SandboxEditorUvRegenerationCommandResult)
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
    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(vertices,
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.25f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
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
TEST(SandboxEditorUi, UvRegenerationPanelModelTracksDerivedJobStateThroughCache)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CachedUvJobMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.HasEntity);
    EXPECT_FALSE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());

    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    context.DerivedJobs = &queued;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::DerivedJobStatus::Queued);
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Key.OutputName,
              "uv_regeneration");

    jobs.Pump(1u);
    jobs.DrainCompletions();
    Runtime::DerivedJobQueueSnapshot applying = jobs.SnapshotAll();
    context.DerivedJobs = &applying;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::DerivedJobStatus::Applying);

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot complete = jobs.SnapshotAll();
    context.DerivedJobs = &complete;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::DerivedJobStatus::Complete);
    EXPECT_TRUE(frame.Inspector.TextureBake.Uv.TexcoordsFinite);

    const Runtime::SandboxEditorSelectedModelCacheStats stats = cache.Stats();
    EXPECT_GE(stats.SelectedAnalysisCacheMisses, 4u);
}
TEST(SandboxEditorUi, TextureBakeControlsReportUvSourcesAndRouteCommand)
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
    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshPresentationBindings());

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);
    context.CommandHistory = &history;
    context.AssetService = &assets;

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const Runtime::SandboxEditorTextureBakeControlsModel& bake =
        frame.Inspector.TextureBake;
    ASSERT_TRUE(bake.HasSelectedEntity);
    EXPECT_TRUE(bake.IsMesh);
    EXPECT_TRUE(bake.Uv.HasTexcoords);
    EXPECT_TRUE(bake.Uv.TexcoordCountMatchesVertices);
    EXPECT_TRUE(bake.Uv.TexcoordsFinite);
    EXPECT_TRUE(bake.HasRuntimeBakeCommand);
    EXPECT_TRUE(bake.CanBake);
    EXPECT_TRUE(bake.Uv.UvRegenerationAvailable);
    EXPECT_TRUE(bake.Uv.UvRegenerationDisabledReason.empty());

    const Runtime::SandboxEditorTextureBakeSourceRow* paint =
        FindTextureBakeSource(bake, "v:paint");
    ASSERT_NE(paint, nullptr);
    EXPECT_TRUE(paint->Bakeable);
    EXPECT_EQ(paint->BakeDomain, Runtime::ProgressiveGeometryDomain::MeshVertex);
    EXPECT_EQ(paint->ExpectedValueKind, Runtime::ProgressivePropertyValueKind::Vec4);

    const Runtime::SandboxEditorTextureBakeSourceRow* position =
        FindTextureBakeSource(bake, std::string{PN::kPosition});
    ASSERT_NE(position, nullptr);
    EXPECT_FALSE(position->Bakeable);
    EXPECT_EQ(position->Category,
              Runtime::SandboxEditorTextureBakeSourceCategory::Connectivity);
    EXPECT_FALSE(position->DisabledReason.empty());

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorTextureBakeCommandResult result =
        Runtime::ApplySandboxEditorTextureBakeCommand(
            context,
            Runtime::SandboxEditorTextureBakeCommand{
                .StableEntityId = stableId,
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::ProgressiveSlotSemantic::Albedo,
                .SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex,
                .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec4,
                .PropertyName = "v:paint",
                .Encoder = Runtime::MeshAttributeTextureBakeEncoder::RgbaColor,
                .Width = 4u,
                .Height = 4u,
                .GeneratedKey = "paint",
                .BindGeneratedTexture = true,
            });

    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_EQ(result.BakeStatus,
              Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(result.GeneratedTexture.IsValid());
    EXPECT_TRUE(result.BoundGeneratedTexture);
    EXPECT_TRUE(history.IsDirty());

    const auto texture =
        assets.Read<Assets::AssetTexture2DPayload>(result.GeneratedTexture);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    EXPECT_EQ((*texture)[0].Metadata.Width, 4u);

    const auto& bindings =
        registry.Raw().get<Runtime::ProgressivePresentationBindings>(mesh);
    const auto* presentation =
        Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const auto* albedo =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Albedo);
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(albedo->GeneratedTexture, result.GeneratedTexture);
}
TEST(SandboxEditorUi, AttachedEngineContextWiresTextureBakeServiceAndDevice)
{
    const std::string editorSource =
        ReadRepositoryTextFile("src/runtime/Runtime.SandboxEditorFacades.cpp");
    ASSERT_FALSE(editorSource.empty());

    const std::string_view contextBuilder = SourceRange(
        editorSource,
        "[[nodiscard]] SandboxEditorContext BuildContextFromEngine(Engine& engine)",
        "[[nodiscard]] bool AttachmentEpochIsActive(");
    ASSERT_FALSE(contextBuilder.empty());
    EXPECT_NE(contextBuilder.find(".AssetService = &engine.GetAssetService(),"),
              std::string_view::npos);
    EXPECT_NE(contextBuilder.find(".Device = &engine.GetDevice(),"),
              std::string_view::npos);
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
    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshPresentationBindings());

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);
    context.AssetService = &assets;

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const Runtime::SandboxEditorTextureBakeControlsModel& bake =
        frame.Inspector.TextureBake;
    ASSERT_TRUE(bake.HasSelectedEntity);
    EXPECT_TRUE(bake.IsMesh);
    EXPECT_TRUE(bake.HasRuntimeBakeCommand);
    EXPECT_TRUE(bake.Uv.HasTexcoords);
    EXPECT_TRUE(bake.Uv.TexcoordsFinite);
    EXPECT_FALSE(bake.CanBake);
    EXPECT_EQ(bake.DisabledReason,
              "texture baking requires an operational GPU backend");

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorTextureBakeCommandResult result =
        Runtime::ApplySandboxEditorTextureBakeCommand(
            context,
            Runtime::SandboxEditorTextureBakeCommand{
                .StableEntityId = stableId,
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::ProgressiveSlotSemantic::Albedo,
                .SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex,
                .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec4,
                .PropertyName = "v:paint",
                .Encoder = Runtime::MeshAttributeTextureBakeEncoder::RgbaColor,
                .Width = 4u,
                .Height = 4u,
                .GeneratedKey = "paint",
                .BindGeneratedTexture = true,
            });

    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);
    EXPECT_EQ(result.BakeStatus,
              Runtime::SelectedMeshTextureBakeStatus::CommandFailed);
    EXPECT_NE(result.Diagnostic.find("operational GPU"), std::string::npos);
}

// UI-032 — preset buttons switch the scalar source but must preserve styling
// (colormap, isoline width/color, highlight isovalues) already configured on
// the target lane, and the styling fields must round-trip through the config
// command onto the component.
