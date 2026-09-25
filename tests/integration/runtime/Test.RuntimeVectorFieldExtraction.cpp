// Vector-field extraction from Appearance layers: domain anchors, deleted
// rows, cache reuse and invalidation, lane independence and release.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    namespace GS = ECS::Components::GeometrySources;
    using D = Runtime::GeometryElementDomain;

    struct Fixture
    {
        Tests::MockDevice Device{};
        std::unique_ptr<Graphics::IRenderer> Renderer{Graphics::CreateRenderer()};
        Runtime::RenderExtractionCache Extraction{};
        ECS::Scene::Registry Scene{};

        Fixture() { Renderer->Initialize(Device); }
        ~Fixture()
        {
            Extraction.Shutdown(*Renderer);
            Renderer->Shutdown();
        }

        struct Frame
        {
            Runtime::RuntimeRenderExtractionStats Stats{};
            Graphics::RenderWorld World{};
        };

        Frame Extract()
        {
            RHI::FrameHandle frame{};
            EXPECT_TRUE(Renderer->BeginFrame(frame));
            Frame out{};
            out.Stats = Extraction.ExtractAndSubmit(Scene, *Renderer, nullptr);
            out.World = Renderer->ExtractRenderWorld({});
            return out;
        }

        // Last bytes the renderer wrote to the buffer behind `address`.
        template <typename T>
        [[nodiscard]] std::vector<T> Read(const std::uint64_t address) const
        {
            EXPECT_GE(address, 0x1'0000'0000ull);
            const std::uint64_t index = (address - 0x1'0000'0000ull) / 0x1000ull;
            for (auto it = Device.BufferWrites.rbegin(); it != Device.BufferWrites.rend(); ++it)
            {
                if (it->Handle.Index != index)
                    continue;
                std::vector<T> values(it->Data.size() / sizeof(T));
                std::memcpy(values.data(), it->Data.data(), values.size() * sizeof(T));
                return values;
            }
            ADD_FAILURE() << "no buffer write for address " << address;
            return {};
        }
    };

    [[nodiscard]] Runtime::GeometryVectorFieldLayerRecipe Layer(
        const D domain, std::string name)
    {
        return Runtime::GeometryVectorFieldLayerRecipe{
            .Vector = {.Domain = domain,
                       .Name = std::move(name),
                       .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Length = 0.25f,
        };
    }

    // Quad (a,b,c,d) plus triangle (c,b,e); optionally the triangle is
    // deleted without garbage collection so its rows stay in place.
    ECS::EntityHandle AddMesh(ECS::Scene::Registry& scene, const bool deleteTriangle,
                              const glm::mat4& world = glm::mat4{1.0f})
    {
        auto& raw = scene.Raw();
        const ECS::EntityHandle entity = scene.Create();
        raw.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = world;
        raw.emplace<Graphics::Components::RenderSurface>(entity);
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto a = mesh.AddVertex({0, 0, 0});
        const auto b = mesh.AddVertex({1, 0, 0});
        const auto c = mesh.AddVertex({1, 1, 0});
        const auto d = mesh.AddVertex({0, 1, 0});
        const auto e = mesh.AddVertex({2, 0, 0});
        EXPECT_TRUE(mesh.AddQuad(a, b, c, d));
        const auto triangle = mesh.AddTriangle(c, b, e);
        EXPECT_TRUE(triangle);
        if (deleteTriangle)
            mesh.DeleteFace(*triangle);
        GS::PopulateFromMesh(raw, entity, mesh);

        auto& vertices = raw.get<GS::Vertices>(entity).Properties;
        auto& edges = raw.get<GS::Edges>(entity).Properties;
        auto& faces = raw.get<GS::Faces>(entity).Properties;
        vertices.GetOrAdd<glm::vec3>("v:dir", glm::vec3{0, 0, 1});
        edges.GetOrAdd<glm::vec3>("e:dir", glm::vec3{0, 1, 0});
        faces.GetOrAdd<glm::vec3>("f:dir", glm::vec3{1, 0, 0});
        return entity;
    }

    Runtime::GeometryPresentationRecipe& Recipe(ECS::Scene::Registry& scene, const ECS::EntityHandle entity)
    {
        return scene.Raw().get_or_emplace<Runtime::GeometryPresentationRecipe>(entity);
    }

    [[nodiscard]] const Graphics::VectorFieldOverlayPacket* FindPacket(
        const Graphics::RenderWorld& world, const std::string_view suffix)
    {
        for (const auto& packet : world.Visualization.VectorFields)
        {
            if (packet.Name.ends_with(suffix))
                return &packet;
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<std::uint32_t> LiveRows(const Geometry::PropertySet& set,
                                                      const std::string_view deleted)
    {
        std::vector<std::uint32_t> rows{};
        const auto flags = set.Get<bool>(deleted);
        for (std::uint32_t row = 0u; row < set.Size(); ++row)
        {
            if (!flags || !flags.Vector()[row])
                rows.push_back(row);
        }
        return rows;
    }
}

TEST(RuntimeVectorFieldExtraction, AnchorsFollowDomainsSkipDeletedRowsAndCarryTransform)
{
    Fixture fixture;
    const glm::mat4 world = glm::translate(glm::mat4{1.0f}, glm::vec3{5, 0, 0});
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, /*deleteTriangle=*/true, world);
    Recipe(fixture.Scene, entity).VectorFields = {
        Layer(D::MeshVertex, "v:dir"), Layer(D::MeshEdge, "e:dir"), Layer(D::MeshFace, "f:dir")};

    const auto frame = fixture.Extract();
    EXPECT_EQ(frame.Stats.VectorFieldLayerCount, 3u);
    EXPECT_EQ(frame.Stats.VectorFieldPacketCount, 3u);
    EXPECT_EQ(frame.Stats.VectorFieldUnavailableCount, 0u);
    ASSERT_EQ(frame.World.Visualization.VectorFields.size(), 3u);
    EXPECT_EQ(frame.World.Visualization.PropertyBufferDiagnostics.StaleDirtyStampCount, 0u);
    EXPECT_FALSE(frame.World.Visualization.Diagnostics.HasErrors);

    auto& raw = fixture.Scene.Raw();
    const auto& vertices = raw.get<GS::Vertices>(entity).Properties;
    const auto& edges = raw.get<GS::Edges>(entity).Properties;
    const auto& faces = raw.get<GS::Faces>(entity).Properties;
    const auto positions = vertices.Get<glm::vec3>(GS::PropertyNames::kPosition).Vector();

    // Faces: the live quad's center is the mean of its four vertices; the
    // deleted triangle row is excluded from the live-row list.
    const auto* face = FindPacket(frame.World, ":f:dir");
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(face->Domain, Graphics::VisualizationAttributeDomain::Face);
    EXPECT_EQ(face->ElementCount, 2u);
    EXPECT_EQ(face->RowCount, 1u);
    EXPECT_EQ(face->ObjectToWorld, world);
    EXPECT_TRUE(Graphics::IsRenderableVectorFieldPacket(*face));
    const auto faceAnchors = fixture.Read<glm::vec3>(face->PositionBufferBDA);
    ASSERT_EQ(faceAnchors.size(), 2u);
    EXPECT_EQ(faceAnchors[0], (glm::vec3{0.5f, 0.5f, 0.0f}));
    EXPECT_EQ(fixture.Read<std::uint32_t>(face->RowBufferBDA), (std::vector<std::uint32_t>{0u}));
    EXPECT_EQ(fixture.Read<glm::vec3>(face->VectorBufferBDA).size(), 2u);

    // Edges: midpoints of the canonical endpoint rows; deleted edges are dead.
    const auto* edge = FindPacket(frame.World, ":e:dir");
    ASSERT_NE(edge, nullptr);
    const auto edgeAnchors = fixture.Read<glm::vec3>(edge->PositionBufferBDA);
    ASSERT_EQ(edgeAnchors.size(), edges.Size());
    const auto v0 = edges.Get<std::uint32_t>(GS::PropertyNames::kEdgeV0).Vector();
    const auto v1 = edges.Get<std::uint32_t>(GS::PropertyNames::kEdgeV1).Vector();
    const std::vector<std::uint32_t> liveEdges = LiveRows(edges, "e:deleted");
    ASSERT_LT(liveEdges.size(), edges.Size()) << "the deleted triangle must delete edges";
    for (const std::uint32_t row : liveEdges)
        EXPECT_EQ(edgeAnchors[row], 0.5f * (positions[v0[row]] + positions[v1[row]])) << row;
    EXPECT_EQ(fixture.Read<std::uint32_t>(edge->RowBufferBDA), liveEdges);

    // Vertices borrow canonical positions; deleted vertices are dead rows.
    const auto* vertex = FindPacket(frame.World, ":v:dir");
    ASSERT_NE(vertex, nullptr);
    EXPECT_EQ(fixture.Read<glm::vec3>(vertex->PositionBufferBDA), positions);
    const std::vector<std::uint32_t> liveVertices = LiveRows(vertices, "v:deleted");
    if (liveVertices.size() == vertices.Size())
    {
        EXPECT_TRUE(vertex->RowBufferSourceKey.empty());
        EXPECT_EQ(vertex->RowCount, vertex->ElementCount);
    }
    else
    {
        EXPECT_EQ(fixture.Read<std::uint32_t>(vertex->RowBufferBDA), liveVertices);
    }

    // Anchors are never written into the canonical geometry.
    EXPECT_FALSE(faces.Exists("f:centroid"));
    EXPECT_EQ(faces.Properties().size(), raw.get<GS::Faces>(entity).Properties.Properties().size());
}

TEST(RuntimeVectorFieldExtraction, SteadyFramesReuseCachesAndResidencyWithoutScans)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    Recipe(fixture.Scene, entity).VectorFields = {
        Layer(D::MeshVertex, "v:dir"), Layer(D::MeshFace, "f:dir")};

    const auto first = fixture.Extract();
    EXPECT_EQ(first.Stats.VectorFieldAnchorCacheBuilds, 2u);
    EXPECT_EQ(first.Stats.VectorFieldPayloadCacheBuilds, 2u);
    EXPECT_GT(first.Stats.VectorFieldCacheScannedElementCount, 0u);
    EXPECT_GT(first.World.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 0u);
    const std::size_t writesAfterFirst = fixture.Device.BufferWrites.size();

    for (int frame = 0; frame < 3; ++frame)
    {
        const auto steady = fixture.Extract();
        EXPECT_EQ(steady.Stats.VectorFieldPacketCount, 2u);
        EXPECT_EQ(steady.Stats.VectorFieldAnchorCacheBuilds, 0u);
        EXPECT_EQ(steady.Stats.VectorFieldPayloadCacheBuilds, 0u);
        EXPECT_EQ(steady.Stats.VectorFieldAnchorCacheReuses, 2u);
        EXPECT_EQ(steady.Stats.VectorFieldPayloadCacheReuses, 2u);
        EXPECT_EQ(steady.Stats.VectorFieldCacheScannedElementCount, 0u);
        const auto& residency = steady.World.Visualization.PropertyBufferDiagnostics;
        EXPECT_EQ(residency.UploadedBufferCount, 0u);
        EXPECT_EQ(residency.ReusedBufferCount, residency.InputBufferCount);
        EXPECT_EQ(residency.EvictedBufferCount, 0u);
    }
    // Only per-frame draw metadata may be written; no property payloads.
    for (std::size_t i = writesAfterFirst; i < fixture.Device.BufferWrites.size(); ++i)
    {
        for (const auto& packet : first.World.Visualization.VectorFields)
        {
            for (const std::uint64_t address :
                 {packet.PositionBufferBDA, packet.VectorBufferBDA})
            {
                EXPECT_NE(fixture.Device.BufferWrites[i].Handle.Index,
                          (address - 0x1'0000'0000ull) / 0x1000ull);
            }
        }
    }
}

TEST(RuntimeVectorFieldExtraction, MutationsInvalidateOnlyDependentCaches)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    Recipe(fixture.Scene, entity).VectorFields = {
        Layer(D::MeshVertex, "v:dir"), Layer(D::MeshFace, "f:dir")};
    const auto first = fixture.Extract();
    const auto* firstFace = FindPacket(first.World, ":f:dir");
    ASSERT_NE(firstFace, nullptr);
    const Graphics::VectorFieldOverlayPacket before = *firstFace;
    (void)fixture.Extract();

    // Changing vectors rebuilds only that payload and uploads a new buffer.
    auto& raw = fixture.Scene.Raw();
    raw.get<GS::Faces>(entity).Properties.Get<glm::vec3>("f:dir").Vector()[0] = {0, 2, 0};
    const auto vectorEdit = fixture.Extract();
    EXPECT_EQ(vectorEdit.Stats.VectorFieldPayloadCacheBuilds, 1u);
    EXPECT_EQ(vectorEdit.Stats.VectorFieldAnchorCacheBuilds, 0u);
    EXPECT_EQ(vectorEdit.World.Visualization.PropertyBufferDiagnostics.ReplacedBufferCount, 1u);
    const auto* face = FindPacket(vectorEdit.World, ":f:dir");
    ASSERT_NE(face, nullptr);
    EXPECT_NE(face->VectorBufferBDA, before.VectorBufferBDA);
    EXPECT_EQ(face->PositionBufferBDA, before.PositionBufferBDA);
    EXPECT_EQ(fixture.Read<glm::vec3>(face->VectorBufferBDA)[0], (glm::vec3{0, 2, 0}));

    // Moving a vertex rebuilds both anchor caches, not the payloads.
    raw.get<GS::Vertices>(entity).Properties.Get<glm::vec3>(GS::PropertyNames::kPosition)
        .Vector()[0] = {-1, -1, 0};
    const auto positionEdit = fixture.Extract();
    EXPECT_EQ(positionEdit.Stats.VectorFieldAnchorCacheBuilds, 2u);
    EXPECT_EQ(positionEdit.Stats.VectorFieldPayloadCacheBuilds, 0u);
    EXPECT_EQ(positionEdit.World.Visualization.PropertyBufferDiagnostics.StaleDirtyStampCount, 0u);
    face = FindPacket(positionEdit.World, ":f:dir");
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(fixture.Read<glm::vec3>(face->PositionBufferBDA)[0], (glm::vec3{0.25f, 0.25f, 0.0f}));

    // Replacing the whole source with identical counts is still detected.
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 3}), b = mesh.AddVertex({1, 0, 3}),
               c = mesh.AddVertex({1, 1, 3}), d = mesh.AddVertex({0, 1, 3}),
               e = mesh.AddVertex({2, 0, 3});
    ASSERT_TRUE(mesh.AddQuad(a, b, c, d));
    ASSERT_TRUE(mesh.AddTriangle(c, b, e));
    GS::PopulateFromMesh(raw, entity, mesh);
    raw.get<GS::Vertices>(entity).Properties.GetOrAdd<glm::vec3>("v:dir", glm::vec3{0, 0, 1});
    raw.get<GS::Faces>(entity).Properties.GetOrAdd<glm::vec3>("f:dir", glm::vec3{1, 0, 0});
    const auto replaced = fixture.Extract();
    EXPECT_EQ(replaced.Stats.VectorFieldAnchorCacheBuilds, 2u);
    EXPECT_EQ(replaced.Stats.VectorFieldPayloadCacheBuilds, 2u);
    EXPECT_EQ(replaced.World.Visualization.PropertyBufferDiagnostics.StaleDirtyStampCount, 0u);
    face = FindPacket(replaced.World, ":f:dir");
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(fixture.Read<glm::vec3>(face->PositionBufferBDA)[0], (glm::vec3{0.5f, 0.5f, 3.0f}));
}

TEST(RuntimeVectorFieldExtraction, FieldsShareAnchorsSanitizeVectorsAndSampleRows)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    auto& vertices = fixture.Scene.Raw().get<GS::Vertices>(entity).Properties;
    auto other = vertices.GetOrAdd<glm::vec3>("v:other", glm::vec3{1, 0, 0});
    other.Vector()[2] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    auto sampled = Layer(D::MeshVertex, "v:other");
    sampled.Stride = 2u;
    auto capped = Layer(D::MeshVertex, "v:dir");
    capped.MaxGlyphs = 2u;
    Recipe(fixture.Scene, entity).VectorFields = {capped, sampled};

    const auto frame = fixture.Extract();
    EXPECT_EQ(frame.Stats.VectorFieldAnchorCacheBuilds, 1u) << "one shared vertex anchor cache";
    EXPECT_EQ(frame.Stats.VectorFieldNonFiniteVectorCount, 1u);
    const auto* dir = FindPacket(frame.World, ":v:dir");
    const auto* otherPacket = FindPacket(frame.World, ":v:other");
    ASSERT_NE(dir, nullptr);
    ASSERT_NE(otherPacket, nullptr);
    EXPECT_EQ(dir->PositionBufferBDA, otherPacket->PositionBufferBDA);
    EXPECT_EQ(dir->PositionBufferSourceKey, otherPacket->PositionBufferSourceKey);
    // 5 live rows capped at 2 glyphs widens the stride to 3.
    EXPECT_EQ(dir->RowStride, 3u);
    EXPECT_EQ(Graphics::VectorFieldGlyphCount(*dir), 2u);
    EXPECT_EQ(otherPacket->RowStride, 2u);
    EXPECT_EQ(Graphics::VectorFieldGlyphCount(*otherPacket), 3u);
    // The non-finite vector uploads as zero (drawn as nothing); the source
    // property keeps its value.
    const auto uploaded = fixture.Read<glm::vec3>(otherPacket->VectorBufferBDA);
    ASSERT_EQ(uploaded.size(), 5u);
    EXPECT_EQ(uploaded[2], glm::vec3{0.0f});
    EXPECT_TRUE(std::isnan(vertices.Get<glm::vec3>("v:other").Vector()[2].x));
    EXPECT_FALSE(frame.World.Visualization.PropertyBufferDiagnostics.HasErrors);
}

TEST(RuntimeVectorFieldExtraction, FieldsIgnoreLaneVisibilityAndReleaseWhenUnused)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    Recipe(fixture.Scene, entity).VectorFields = {
        Layer(D::MeshVertex, "v:dir"), Layer(D::MeshFace, "f:dir")};
    (void)fixture.Extract();

    // Hiding every base lane keeps the vector fields drawn.
    fixture.Scene.Raw().remove<Graphics::Components::RenderSurface>(entity);
    const auto hidden = fixture.Extract();
    EXPECT_EQ(hidden.World.Visualization.VectorFields.size(), 2u);
    EXPECT_EQ(hidden.Stats.VectorFieldAnchorCacheBuilds, 0u);

    // A disabled field stops drawing; its caches and buffers are released.
    Recipe(fixture.Scene, entity).VectorFields[1].Enabled = false;
    const auto disabled = fixture.Extract();
    EXPECT_EQ(disabled.World.Visualization.VectorFields.size(), 1u);
    EXPECT_EQ(disabled.Stats.VectorFieldCacheReleases, 2u);
    EXPECT_GE(disabled.World.Visualization.PropertyBufferDiagnostics.EvictedBufferCount, 2u);

    // Re-enabling rebuilds with fresh, increasing stamps.
    Recipe(fixture.Scene, entity).VectorFields[1].Enabled = true;
    const auto reenabled = fixture.Extract();
    EXPECT_EQ(reenabled.World.Visualization.VectorFields.size(), 2u);
    EXPECT_EQ(reenabled.World.Visualization.PropertyBufferDiagnostics.StaleDirtyStampCount, 0u);
    EXPECT_TRUE(Graphics::IsRenderableVectorFieldPacket(*FindPacket(reenabled.World, ":f:dir")));

    // Destroying the entity releases every remaining cache and buffer.
    fixture.Scene.Destroy(entity);
    const auto destroyed = fixture.Extract();
    EXPECT_TRUE(destroyed.World.Visualization.VectorFields.empty());
    EXPECT_EQ(destroyed.Stats.VectorFieldCacheReleases, 4u);
    EXPECT_EQ(destroyed.World.Visualization.PropertyBufferDiagnostics.InputBufferCount, 0u);
    EXPECT_GE(destroyed.World.Visualization.PropertyBufferDiagnostics.EvictedBufferCount, 3u);
}

TEST(RuntimeVectorFieldExtraction, SteadyFramesDoNotRevisitLiveRowsOfSharedDeletedDomains)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, /*deleteTriangle=*/true);
    fixture.Scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<glm::vec3>("e:other", glm::vec3{1, 0, 0});
    fixture.Scene.Raw().get<GS::Faces>(entity).Properties.GetOrAdd<glm::vec3>("f:other", glm::vec3{0, 1, 0});
    Recipe(fixture.Scene, entity).VectorFields = {
        Layer(D::MeshEdge, "e:dir"), Layer(D::MeshEdge, "e:other"),
        Layer(D::MeshFace, "f:dir"), Layer(D::MeshFace, "f:other")};

    const auto first = fixture.Extract();
    ASSERT_EQ(first.World.Visualization.VectorFields.size(), 4u);
    for (const auto& packet : first.World.Visualization.VectorFields)
    {
        // Deleted rows make every packet use a live-row list.
        EXPECT_FALSE(packet.RowBufferSourceKey.empty()) << packet.Name;
        EXPECT_LT(packet.RowCount, packet.ElementCount) << packet.Name;
    }
    EXPECT_EQ(first.Stats.VectorFieldAnchorCacheBuilds, 2u) << "edge and face caches are shared";
    EXPECT_EQ(first.Stats.VectorFieldRowIndexCheckCount, 0u)
        << "cache rows are validated when built, not when appended";

    for (int frame = 0; frame < 3; ++frame)
    {
        const auto steady = fixture.Extract();
        EXPECT_EQ(steady.World.Visualization.VectorFields.size(), 4u);
        EXPECT_EQ(steady.Stats.VectorFieldCacheScannedElementCount, 0u);
        EXPECT_EQ(steady.Stats.VectorFieldRowIndexCheckCount, 0u);
        EXPECT_EQ(steady.World.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 0u);
    }
}

TEST(RuntimeVectorFieldExtraction, ScalarGradientCommandReachesFaceArrowBuffers)
{
    Fixture fixture;
    const auto entity = fixture.Scene.Create();
    fixture.Scene.Raw().emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1};
    fixture.Scene.Raw().emplace<Graphics::Components::RenderSurface>(entity);
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(fixture.Scene.Raw(), entity, mesh);
    auto& props = fixture.Scene.Raw().get<GS::Vertices>(entity).Properties;
    props.GetOrAdd<double>("field", 0.).Vector() = {0,2,3};
    Runtime::EditorProcessingContext processing;
    processing.Scene = &fixture.Scene;
    Runtime::ScalarGradientConfig config;
    config.Scalar.Name = "field";
    const auto id = Runtime::SelectionController::ToStableEntityId(entity);
    ASSERT_TRUE(Runtime::ApplyEditorScalarGradientCommand(
        Runtime::BindEditorProcessingCommands(processing), id, config).Succeeded());
    Runtime::EditorVisualizationEditingContext visualization;
    visualization.Scene = &fixture.Scene;
    EXPECT_EQ(Runtime::ApplyEditorGeometryVectorFieldCommand(visualization,
        {.StableEntityId = id, .Layer = {.Vector = config.Output}}), Runtime::EditorCommandStatus::Applied);
    const auto frame = fixture.Extract();
    const auto* packet = FindPacket(frame.World, ":f:scalar_gradient");
    ASSERT_NE(packet, nullptr);
    ASSERT_TRUE(Graphics::IsRenderableVectorFieldPacket(*packet));
    EXPECT_EQ(packet->Domain, Graphics::VisualizationAttributeDomain::Face);
    EXPECT_EQ(packet->RowCount, 1u);
    const auto anchors = fixture.Read<glm::vec3>(packet->PositionBufferBDA);
    const auto vectors = fixture.Read<glm::vec3>(packet->VectorBufferBDA);
    ASSERT_EQ(anchors.size(), 1u);
    ASSERT_EQ(vectors.size(), 1u);
    EXPECT_EQ(anchors[0], glm::vec3(1.f/3, 1.f/3, 0));
    EXPECT_EQ(vectors[0], glm::vec3(2,3,0));
}
