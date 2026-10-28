// Vector-field extraction from Appearance layers: domain anchors, deleted
// rows, cache reuse and invalidation, lane independence and release.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.VertexChannelBindings;
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

// RUNTIME-315: glyph anchors sit on the displayed (bound) positions, and
// return to the canonical positions when the binding goes away.
TEST(RuntimeVectorFieldExtraction, AnchorsFollowABoundPositionSource)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, /*deleteTriangle=*/false);
    Recipe(fixture.Scene, entity).VectorFields = {Layer(D::MeshVertex, "v:dir"), Layer(D::MeshFace, "f:dir")};
    auto& raw = fixture.Scene.Raw();
    auto& vertices = raw.get<GS::Vertices>(entity).Properties;
    const auto canonical = vertices.Get<glm::vec3>(GS::PropertyNames::kPosition).Vector();
    auto lifted = canonical;
    for (glm::vec3& p : lifted)
        p.z += 2.0f;
    vertices.GetOrAdd<glm::vec3>("v:lifted", glm::vec3{0.0f}).Vector() = lifted;
    raw.emplace<Runtime::VertexChannelBindingSet>(entity,
        Runtime::VertexChannelBindingSet{
            .Surface = {.Position = {.Enabled = true,
                            .Property = {D::MeshVertex, "v:lifted", Geometry::PropertyValueKind::Vec3}}}});

    auto frame = fixture.Extract();
    const auto* vertex = FindPacket(frame.World, ":v:dir");
    const auto* face = FindPacket(frame.World, ":f:dir");
    ASSERT_NE(vertex, nullptr);
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(fixture.Read<glm::vec3>(vertex->PositionBufferBDA), lifted);
    EXPECT_EQ(fixture.Read<glm::vec3>(face->PositionBufferBDA)[0], (glm::vec3{0.5f, 0.5f, 2.0f}));
    EXPECT_EQ(vertices.Get<glm::vec3>(GS::PropertyNames::kPosition).Vector(), canonical);

    raw.remove<Runtime::VertexChannelBindingSet>(entity);
    frame = fixture.Extract();
    EXPECT_EQ(frame.Stats.VectorFieldAnchorCacheBuilds, 2u);
    vertex = FindPacket(frame.World, ":v:dir");
    face = FindPacket(frame.World, ":f:dir");
    ASSERT_NE(vertex, nullptr);
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(fixture.Read<glm::vec3>(vertex->PositionBufferBDA), canonical);
    EXPECT_EQ(fixture.Read<glm::vec3>(face->PositionBufferBDA)[0], (glm::vec3{0.5f, 0.5f, 0.0f}));
}

// UI-081: a valid glyph rank on the domain makes its layers draw the first MaxGlyphs rows of the
// rank-sorted order; an invalid rank falls back to the stride.
TEST(RuntimeVectorFieldExtraction, AValidGlyphOrderDrawsARankSortedPrefix)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    auto& vertices = fixture.Scene.Raw().get<GS::Vertices>(entity).Properties;
    vertices.GetOrAdd<float>("v:glyph_rank").Vector() = {3.0f, 0.0f, 4.0f, 1.0f, 2.0f};
    auto layer = Layer(D::MeshVertex, "v:dir");
    layer.MaxGlyphs = 2u;
    Recipe(fixture.Scene, entity).VectorFields = {layer};

    const auto ordered = fixture.Extract();
    EXPECT_EQ(ordered.Stats.VectorFieldOrderedLayerCount, 1u);
    const auto* packet = FindPacket(ordered.World, ":v:dir");
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->RowCount, 2u);
    EXPECT_EQ(packet->RowStride, 1u);
    EXPECT_EQ(Graphics::VectorFieldGlyphCount(*packet), 2u);
    EXPECT_EQ(fixture.Read<std::uint32_t>(packet->RowBufferBDA), (std::vector<std::uint32_t>{1u, 3u, 4u, 0u, 2u}));

    vertices.Get<float>("v:glyph_rank").Vector()[0] = 0.0f; // duplicate rank: stale order
    const auto strided = fixture.Extract();
    EXPECT_EQ(strided.Stats.VectorFieldOrderedLayerCount, 0u);
    packet = FindPacket(strided.World, ":v:dir");
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->RowStride, 3u) << "5 rows capped at 2 glyphs widen the stride again";

    // An older layer's stride keeps its arrow count on the order: ceil(5 / 2) = 3 rows.
    vertices.Get<float>("v:glyph_rank").Vector()[0] = 3.0f;
    layer.Stride = 2u;
    layer.MaxGlyphs = 0u;
    Recipe(fixture.Scene, entity).VectorFields = {layer};
    const auto legacy = fixture.Extract();
    EXPECT_EQ(legacy.Stats.VectorFieldOrderedLayerCount, 1u);
    packet = FindPacket(legacy.World, ":v:dir");
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->RowCount, 3u);
    EXPECT_EQ(packet->RowStride, 1u);

    // An anchor without a finite position (rank 0's row) leaves the order and draws nothing.
    vertices.Get<glm::vec3>(GS::PropertyNames::kPosition).Vector()[1] =
        glm::vec3{std::numeric_limits<float>::quiet_NaN()};
    layer.Stride = 1u;
    layer.MaxGlyphs = 2u;
    Recipe(fixture.Scene, entity).VectorFields = {layer};
    const auto nonFinite = fixture.Extract();
    EXPECT_EQ(nonFinite.Stats.VectorFieldOrderedLayerCount, 1u);
    packet = FindPacket(nonFinite.World, ":v:dir");
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->RowCount, 2u);
    EXPECT_EQ(fixture.Read<std::uint32_t>(packet->RowBufferBDA), (std::vector<std::uint32_t>{3u, 4u, 0u, 2u}));
}

namespace
{
    struct StyleUpload
    {
        bool Found{false};
        bool Renderable{false};
        std::uint32_t Fallbacks{0u};
        Graphics::VectorFieldColorMode Mode{Graphics::VectorFieldColorMode::Uniform};
        float RangeMin{0.0f};
        float RangeMax{0.0f};
        std::uint64_t LengthBDA{0u};
        std::uint64_t ColorBDA{0u};
        std::vector<float> Lengths{};
        std::vector<float> Scalars{};
        std::vector<glm::vec4> Rgba{};
    };

    // Extracts one frame with `layers` and reads back the style buffers of the layer drawing `vector`.
    [[nodiscard]] StyleUpload ExtractStyle(Fixture& fixture,
        const ECS::EntityHandle entity,
        std::vector<Runtime::GeometryVectorFieldLayerRecipe> layers,
        const std::string_view vector = ":v:dir")
    {
        Recipe(fixture.Scene, entity).VectorFields = std::move(layers);
        const auto frame = fixture.Extract();
        StyleUpload out{.Fallbacks = frame.Stats.VectorFieldStyleFallbackCount};
        const auto* packet = FindPacket(frame.World, vector);
        if (packet == nullptr)
            return out;
        out.Found = true;
        out.Renderable = Graphics::IsRenderableVectorFieldPacket(*packet);
        out.Mode = packet->ColorMode;
        out.RangeMin = packet->Style.ColorRangeMin;
        out.RangeMax = packet->Style.ColorRangeMax;
        out.LengthBDA = packet->LengthBufferBDA;
        out.ColorBDA = packet->ColorBufferBDA;
        if (packet->LengthBufferBDA >= 0x1'0000'0000ull)
            out.Lengths = fixture.Read<float>(packet->LengthBufferBDA);
        if (packet->ColorBufferBDA >= 0x1'0000'0000ull)
        {
            if (packet->ColorMode == Graphics::VectorFieldColorMode::Rgba)
                out.Rgba = fixture.Read<glm::vec4>(packet->ColorBufferBDA);
            else
                out.Scalars = fixture.Read<float>(packet->ColorBufferBDA);
        }
        return out;
    }

    [[nodiscard]] Runtime::GeometryVectorFieldLayerRecipe Styled(const std::string& length, const std::string& color)
    {
        auto layer = Layer(D::MeshVertex, "v:dir");
        layer.LengthProperty = length;
        layer.ColorProperty = color;
        return layer;
    }
}

// UI-083: per-arrow lengths and colors upload one checked value per element. A length that is not
// a valid float becomes +inf (the shader draws no arrow); an invalid color becomes +inf or a
// negative RGBA (the shader keeps the uniform color).
TEST(RuntimeVectorFieldExtraction, StylePropertiesUploadCheckedPerElementValues)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    auto& vertices = fixture.Scene.Raw().get<GS::Vertices>(entity).Properties;
    constexpr float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    vertices.GetOrAdd<float>("v:len", 0.0f).Vector() = {1.0f, 2.0f, nan, -1.0f, 0.0f};
    vertices.GetOrAdd<double>("v:lend", 0.0).Vector() = {1.0e39, 3.0, 0.0, 0.0, 0.0};
    vertices.GetOrAdd<std::uint32_t>("v:lenu", 0u).Vector() = {16777217u, 7u, 0u, 0u, 0u};
    vertices.GetOrAdd<std::int32_t>("v:leni", 0).Vector() = {-2, 5, 0, 0, 0};
    vertices.GetOrAdd<std::uint64_t>("v:lenq", 0u).Vector() = {4u, 0u, 0u, 0u, 0u};
    vertices.GetOrAdd<bool>("v:show", false).Vector() = {true, false, true, false, false};

    const auto lengths = [&](const std::string& name)
    {
        const StyleUpload upload = ExtractStyle(fixture, entity, {Styled(name, "")});
        EXPECT_TRUE(upload.Found) << name;
        EXPECT_EQ(upload.Fallbacks, 0u) << name;
        EXPECT_EQ(upload.Mode, Graphics::VectorFieldColorMode::Uniform) << name;
        return upload.Lengths;
    };
    EXPECT_EQ(lengths("v:len"), (std::vector<float>{1.0f, 2.0f, inf, -1.0f, 0.0f}));
    EXPECT_EQ(lengths("v:lend"), (std::vector<float>{inf, 3.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(lengths("v:lenu"), (std::vector<float>{inf, 7.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(lengths("v:leni"), (std::vector<float>{-2.0f, 5.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(lengths("v:lenq"), (std::vector<float>{4.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(lengths("v:show"), (std::vector<float>{1.0f, 0.0f, 1.0f, 0.0f, 0.0f}));

    vertices.GetOrAdd<std::int32_t>("v:heat", 0).Vector() = {0, 10, 5, 5, 5};
    (void)vertices.GetOrAdd<float>("v:flat", 3.0f);
    vertices.GetOrAdd<float>("v:mixed", 0.0f).Vector() = {nan, 1.0f, 2.0f, inf, 4.0f};
    auto rgb = vertices.GetOrAdd<glm::vec3>("v:rgb", glm::vec3{0.5f});
    rgb.Vector()[0] = {2.0f, -1.0f, 0.5f};
    rgb.Vector()[1] = {nan, 0.0f, 0.0f};
    (void)vertices.GetOrAdd<glm::vec4>("v:rgba", glm::vec4{0.0f, 1.0f, 1.0f, 0.5f});

    auto heat = Styled("", "v:heat");
    heat.ColorMap = Graphics::Colormap::Type::Jet;
    StyleUpload upload = ExtractStyle(fixture, entity, {heat});
    EXPECT_EQ(upload.Mode, Graphics::VectorFieldColorMode::Colormap);
    EXPECT_EQ(upload.LengthBDA, 0u);
    EXPECT_EQ(upload.Scalars, (std::vector<float>{0.0f, 10.0f, 5.0f, 5.0f, 5.0f}));
    EXPECT_FLOAT_EQ(upload.RangeMin, 0.0f);
    EXPECT_FLOAT_EQ(upload.RangeMax, 10.0f);
    heat.ColorAutoRange = false;
    heat.ColorRangeMin = 10.0f;
    heat.ColorRangeMax = 20.0f;
    upload = ExtractStyle(fixture, entity, {heat});
    EXPECT_FLOAT_EQ(upload.RangeMin, 10.0f);
    EXPECT_FLOAT_EQ(upload.RangeMax, 20.0f);

    upload = ExtractStyle(fixture, entity, {Styled("", "v:flat")});
    EXPECT_FLOAT_EQ(upload.RangeMin, 2.5f);
    EXPECT_FLOAT_EQ(upload.RangeMax, 3.5f);
    upload = ExtractStyle(fixture, entity, {Styled("", "v:mixed")});
    EXPECT_EQ(upload.Scalars, (std::vector<float>{inf, 1.0f, 2.0f, inf, 4.0f}));
    EXPECT_FLOAT_EQ(upload.RangeMin, 1.0f) << "invalid values stay out of the auto range";
    EXPECT_FLOAT_EQ(upload.RangeMax, 4.0f);

    upload = ExtractStyle(fixture, entity, {Styled("", "v:rgb")});
    EXPECT_EQ(upload.Mode, Graphics::VectorFieldColorMode::Rgba);
    ASSERT_EQ(upload.Rgba.size(), 5u);
    EXPECT_EQ(upload.Rgba[0], (glm::vec4{1.0f, 0.0f, 0.5f, 1.0f}));
    EXPECT_EQ(upload.Rgba[1], glm::vec4{-1.0f});
    EXPECT_EQ(upload.Rgba[2], (glm::vec4{0.5f, 0.5f, 0.5f, 1.0f}));
    upload = ExtractStyle(fixture, entity, {Styled("", "v:rgba")});
    ASSERT_EQ(upload.Rgba.size(), 5u);
    EXPECT_EQ(upload.Rgba[0], (glm::vec4{0.0f, 1.0f, 1.0f, 0.5f}));

    // Under a glyph order the rows are reordered; the length buffer stays indexed by element.
    vertices.GetOrAdd<float>("v:glyph_rank").Vector() = {3.0f, 0.0f, 4.0f, 1.0f, 2.0f};
    auto ordered = Styled("v:len", "");
    ordered.MaxGlyphs = 2u;
    upload = ExtractStyle(fixture, entity, {ordered});
    EXPECT_EQ(upload.Lengths, (std::vector<float>{1.0f, 2.0f, inf, -1.0f, 0.0f}));
}

// UI-083: a style property that no longer resolves draws the uniform value and is counted; a
// steady frame rebuilds nothing and an edit rebuilds only that property's cache.
TEST(RuntimeVectorFieldExtraction, StylePropertiesFallBackAndRebuildOnlyTheirOwnCache)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    auto& vertices = fixture.Scene.Raw().get<GS::Vertices>(entity).Properties;
    vertices.GetOrAdd<float>("v:len", 0.0f).Vector() = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    const auto layer = Styled("v:len", "v:len");

    StyleUpload upload = ExtractStyle(fixture, entity, {layer});
    EXPECT_EQ(upload.Lengths, (std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f}));
    EXPECT_NE(upload.LengthBDA, 0u);
    EXPECT_EQ(upload.ColorBDA, upload.LengthBDA) << "one scalar for length and color uploads once";

    for (int frame = 0; frame < 2; ++frame)
    {
        const auto steady = fixture.Extract();
        EXPECT_EQ(steady.Stats.VectorFieldPayloadCacheBuilds, 0u);
        EXPECT_EQ(steady.Stats.VectorFieldPayloadCacheReuses, 2u) << "the vectors and one style cache";
        EXPECT_EQ(steady.Stats.VectorFieldCacheScannedElementCount, 0u);
        EXPECT_EQ(steady.World.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 0u);
    }

    vertices.Get<float>("v:len").Vector()[0] = 9.0f;
    const auto edit = fixture.Extract();
    EXPECT_EQ(edit.Stats.VectorFieldPayloadCacheBuilds, 1u);
    EXPECT_EQ(edit.Stats.VectorFieldAnchorCacheBuilds, 0u);
    const auto* packet = FindPacket(edit.World, ":v:dir");
    ASSERT_NE(packet, nullptr);
    ASSERT_NE(packet->LengthBufferBDA, 0u);
    EXPECT_EQ(fixture.Read<float>(packet->LengthBufferBDA), (std::vector<float>{9.0f, 2.0f, 3.0f, 4.0f, 5.0f}));

    {
        auto len = vertices.Get<float>("v:len");
        vertices.Remove(len);
    }
    upload = ExtractStyle(fixture, entity, {layer});
    EXPECT_EQ(upload.Fallbacks, 2u);
    EXPECT_EQ(upload.LengthBDA, 0u);
    EXPECT_EQ(upload.Mode, Graphics::VectorFieldColorMode::Uniform);
    EXPECT_EQ(upload.ColorBDA, 0u);

    vertices.GetOrAdd<float>("v:len", 0.0f).Vector() = {5.0f, 4.0f, 3.0f, 2.0f, 1.0f};
    upload = ExtractStyle(fixture, entity, {layer});
    EXPECT_EQ(upload.Fallbacks, 0u);
    EXPECT_EQ(upload.Lengths, (std::vector<float>{5.0f, 4.0f, 3.0f, 2.0f, 1.0f}));

    // Replaced by a vec3: no longer a length, now an RGB color.
    {
        auto len = vertices.Get<float>("v:len");
        vertices.Remove(len);
    }
    (void)vertices.GetOrAdd<glm::vec3>("v:len", glm::vec3{0.25f});
    upload = ExtractStyle(fixture, entity, {layer});
    EXPECT_EQ(upload.Fallbacks, 1u);
    EXPECT_EQ(upload.LengthBDA, 0u);
    EXPECT_EQ(upload.Mode, Graphics::VectorFieldColorMode::Rgba);
    ASSERT_EQ(upload.Rgba.size(), 5u);
    EXPECT_EQ(upload.Rgba[4], (glm::vec4{0.25f, 0.25f, 0.25f, 1.0f}));
}

// UI-083: style caches are keyed by property, not by revision token, survive cache growth and keep
// a vec3 used as vector and as color in both representations.
TEST(RuntimeVectorFieldExtraction, StyleCachesKeepSameTokenAndSharedPropertiesApart)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    auto& vertices = fixture.Scene.Raw().get<GS::Vertices>(entity).Properties;
    (void)vertices.GetOrAdd<float>("v:t1", 1.0f);
    (void)vertices.GetOrAdd<float>("v:t2", 2.0f);
    ASSERT_EQ(vertices.FindPropertyRevision("v:t1"), vertices.FindPropertyRevision("v:t2"));
    EXPECT_EQ(ExtractStyle(fixture, entity, {Styled("v:t1", "")}).Lengths, std::vector<float>(5u, 1.0f));
    EXPECT_EQ(ExtractStyle(fixture, entity, {Styled("v:t2", "")}).Lengths, std::vector<float>(5u, 2.0f));

    const StyleUpload self = ExtractStyle(fixture, entity, {Styled("", "v:dir")});
    EXPECT_EQ(self.Rgba, std::vector<glm::vec4>(5u, glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}));
    {
        const auto frame = fixture.Extract();
        const auto* packet = FindPacket(frame.World, ":v:dir");
        ASSERT_NE(packet, nullptr);
        EXPECT_EQ(fixture.Read<glm::vec3>(packet->VectorBufferBDA), std::vector<glm::vec3>(5u, glm::vec3{0, 0, 1}));
    }

    // Five layers with five style properties grow the cache while earlier entries are lent.
    std::vector<Runtime::GeometryVectorFieldLayerRecipe> layers{};
    for (int i = 0; i < 5; ++i)
    {
        const std::string suffix = std::to_string(i);
        (void)vertices.GetOrAdd<glm::vec3>("v:d" + suffix, glm::vec3{1.0f, 0.0f, 0.0f});
        (void)vertices.GetOrAdd<float>("v:l" + suffix, static_cast<float>(i + 1));
        auto layer = Layer(D::MeshVertex, "v:d" + suffix);
        layer.LengthProperty = "v:l" + suffix;
        layer.ColorProperty = "v:l" + suffix;
        layers.push_back(layer);
    }
    Recipe(fixture.Scene, entity).VectorFields = layers;
    const auto frame = fixture.Extract();
    for (int i = 0; i < 5; ++i)
    {
        const auto* packet = FindPacket(frame.World, ":v:d" + std::to_string(i));
        ASSERT_NE(packet, nullptr) << i;
        EXPECT_EQ(fixture.Read<float>(packet->LengthBufferBDA), std::vector<float>(5u, static_cast<float>(i + 1))) << i;
        EXPECT_EQ(packet->ColorBufferBDA, packet->LengthBufferBDA) << i;
    }
}

// UI-083, geometry.property-coherence: a scalar style property with a GPU preview front is drawn
// from the front; the CPU values return once the front ends.
TEST(RuntimeVectorFieldExtraction, GpuPreviewFrontsDriveScalarStyleBuffers)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    auto& vertices = fixture.Scene.Raw().get<GS::Vertices>(entity).Properties;
    vertices.GetOrAdd<float>("v:len", 0.0f).Vector() = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    struct Front
    {
        std::uint64_t Address{0u};
        std::uint64_t Stamp{0u};
        std::array<float, 2> Range{};
        std::string Name{"v:len"};
    };
    std::optional<Front> front = Front{.Address = 0xABCD0000ull, .Stamp = 1u, .Range = {-8.0f, 12.0f}};
    fixture.Extraction.SetGpuPropertyObserver(
        [&front, entity](auto, const auto observed, const Runtime::GeometryPropertyRef& property)
            -> std::optional<Runtime::RenderExtractionCache::GpuPropertyFront>
        {
            if (!front || observed != entity || property.Name != front->Name)
                return std::nullopt;
            return Runtime::RenderExtractionCache::GpuPropertyFront{.Buffer = {9, 1},
                .Address = front->Address,
                .Bytes = 20,
                .Count = 5u,
                .Stamp = front->Stamp,
                .ScalarRange = front->Range};
        });
    const auto layer = Styled("v:len", "v:len");

    StyleUpload upload = ExtractStyle(fixture, entity, {layer});
    EXPECT_EQ(upload.LengthBDA, 0xABCD0000ull);
    EXPECT_EQ(upload.ColorBDA, 0xABCD0000ull);
    EXPECT_FLOAT_EQ(upload.RangeMin, -8.0f);
    EXPECT_FLOAT_EQ(upload.RangeMax, 12.0f);

    // A new front with every CPU revision unchanged is followed.
    const auto revision = vertices.FindPropertyRevision("v:len");
    front = Front{.Address = 0xABCE0000ull, .Stamp = 2u, .Range = {-1.0f, 1.0f}};
    upload = ExtractStyle(fixture, entity, {layer});
    EXPECT_EQ(vertices.FindPropertyRevision("v:len"), revision);
    EXPECT_EQ(upload.LengthBDA, 0xABCE0000ull);
    EXPECT_FLOAT_EQ(upload.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(upload.RangeMax, 1.0f);

    front.reset();
    upload = ExtractStyle(fixture, entity, {layer});
    EXPECT_EQ(upload.Lengths, (std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f}));
    EXPECT_FLOAT_EQ(upload.RangeMin, 1.0f);
    EXPECT_FLOAT_EQ(upload.RangeMax, 5.0f);

    // A name only the front carries draws from it and falls back when the front ends.
    front = Front{.Address = 0xABCF0000ull, .Stamp = 3u, .Range = {0.0f, 2.0f}, .Name = "v:gpu_only"};
    upload = ExtractStyle(fixture, entity, {Styled("v:gpu_only", "")});
    EXPECT_EQ(upload.Fallbacks, 0u);
    EXPECT_EQ(upload.LengthBDA, 0xABCF0000ull);
    front.reset();
    upload = ExtractStyle(fixture, entity, {Styled("v:gpu_only", "")});
    EXPECT_EQ(upload.Fallbacks, 1u);
    EXPECT_EQ(upload.LengthBDA, 0u);
}

// RUNTIME-333: with no GPU preview, extraction binds a property as a layer's per-arrow length (a
// scalar) or color exactly when the editor model offers it, from the one rule both use; any other
// name leaves the layer drawing its uniform length or color. Vertex and edge domains, with deleted
// rows kept in the property sets. A scene-bound name the rule refuses stays and is reported.
TEST(RuntimeVectorFieldExtraction, CpuStyleBindingMatchesOfferedProperties)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, /*deleteTriangle=*/true);
    auto& raw = fixture.Scene.Raw();
    auto& vertices = raw.get<GS::Vertices>(entity).Properties;
    auto& edges = raw.get<GS::Edges>(entity).Properties;
    (void)vertices.GetOrAdd<bool>("v:b", true);
    (void)vertices.GetOrAdd<std::int32_t>("v:i", 1);
    (void)vertices.GetOrAdd<std::int64_t>("v:i64", 1);
    (void)vertices.GetOrAdd<std::uint32_t>("v:u", 1u);
    (void)vertices.GetOrAdd<std::uint64_t>("v:q", 1u);
    (void)vertices.GetOrAdd<float>("v:f", 1.0f);
    (void)vertices.GetOrAdd<double>("v:d", 1.0);
    (void)vertices.GetOrAdd<glm::vec2>("v:uv", glm::vec2{1.0f});
    (void)vertices.GetOrAdd<glm::vec4>("v:rgba", glm::vec4{1.0f});
    (void)vertices.GetOrAdd<bool>("v:deleted", false); // a topology property, never a style
    vertices.GetOrAdd<float>("v:short", 1.0f).Vector().resize(vertices.Size() - 1u);
    (void)edges.GetOrAdd<float>("e:f", 1.0f);

    const Runtime::GeometryEntityAvailability availability = Runtime::BuildGeometryAvailability(raw, entity);
    const Runtime::EditorVectorFieldModel model = Runtime::BuildEditorVectorFieldModel(availability, nullptr);
    std::size_t offeredCount = 0u;
    std::size_t topologyCount = 0u;
    for (const auto& [domain, vector, packetKey] :
        {std::tuple{D::MeshVertex, "v:dir", ":v:dir"}, std::tuple{D::MeshEdge, "e:dir", ":e:dir"}})
    {
        const auto options = std::ranges::find(model.Domains, domain, &Runtime::EditorVectorFieldDomainOption::Domain);
        ASSERT_NE(options, model.Domains.end());
        const Geometry::PropertySet& properties = domain == D::MeshVertex ? vertices : edges;
        for (const std::string& name : properties.Properties())
        {
            const auto option =
                std::ranges::find(options->StyleProperties, name, &Runtime::EditorVectorFieldStyleOption::Name);
            const bool offered = option != options->StyleProperties.end();
            const bool scalar = offered && option->Scalar;
            offeredCount += offered ? 1u : 0u;
            topologyCount += Runtime::IsTopologyProperty(domain, name) ? 1u : 0u;

            auto layer = Layer(domain, vector);
            layer.LengthProperty = name;
            const StyleUpload length = ExtractStyle(fixture, entity, {layer}, packetKey);
            ASSERT_TRUE(length.Found && length.Renderable) << "length " << name;
            EXPECT_EQ(length.LengthBDA != 0u, scalar) << "length " << name;
            EXPECT_EQ(length.Fallbacks, scalar ? 0u : 1u) << "length " << name;
            EXPECT_EQ(length.Mode, Graphics::VectorFieldColorMode::Uniform) << "length " << name;

            layer = Layer(domain, vector);
            layer.ColorProperty = name;
            const StyleUpload color = ExtractStyle(fixture, entity, {layer}, packetKey);
            ASSERT_TRUE(color.Found && color.Renderable) << "color " << name;
            EXPECT_EQ(color.Mode != Graphics::VectorFieldColorMode::Uniform, offered) << "color " << name;
            EXPECT_EQ(color.ColorBDA != 0u, offered) << "color " << name;
            EXPECT_EQ(color.Fallbacks, offered ? 0u : 1u) << "color " << name;
            EXPECT_EQ(color.LengthBDA, 0u) << "color " << name;
        }
    }
    EXPECT_GE(offeredCount, 10u) << "the six vertex scalars, v:rgba, v:dir, the positions and e:f";
    EXPECT_GE(topologyCount, 2u) << "each domain must hold a topology property to test the rule";

    // A scene-bound name the rule refuses keeps its name; the layer row says it draws uniform.
    auto bound = Layer(D::MeshVertex, "v:dir");
    bound.LengthProperty = "v:deleted";
    Recipe(fixture.Scene, entity).VectorFields = {bound};
    const Runtime::EditorVectorFieldModel withLayer = Runtime::BuildEditorVectorFieldModel(
        Runtime::BuildGeometryAvailability(raw, entity), &Recipe(fixture.Scene, entity));
    ASSERT_EQ(withLayer.Layers.size(), 1u);
    EXPECT_EQ(withLayer.Layers.front().Layer.LengthProperty, "v:deleted");
    EXPECT_NE(
        withLayer.Layers.front().Status.find("Per-arrow length 'v:deleted' not found or not usable: uniform length"), std::string::npos)
        << withLayer.Layers.front().Status;
}

// RUNTIME-331: every pass-through style row reaches the packet from the authored layer, at a value
// that is neither its default nor 0 (a bound, else the middle of its range).
TEST(RuntimeVectorFieldExtraction, EveryPassThroughRowReachesThePacket)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    auto layer = Layer(D::MeshVertex, "v:dir");
    const Runtime::GeometryVectorFieldLayerRecipe defaults{};
#define VF_STYLE_ROW(member, fallback, agentName, sceneKey, sceneRequired, label, help, lo, hi)                        \
    layer.member = (hi) != (fallback) && (hi) != 0.0f ? (hi)                                                           \
        : (lo) != (fallback) && (lo) != 0.0f          ? (lo)                                                           \
                                                      : ((lo) + (hi)) / 2.0f;                                                   \
    ASSERT_NE(layer.member, defaults.member) << #member;                                                               \
    ASSERT_NE(layer.member, 0.0f) << #member;
#include "common/vector_field_style_rows.glsl"
#undef VF_STYLE_ROW
    Recipe(fixture.Scene, entity).VectorFields = {layer};

    const auto frame = fixture.Extract();
    const auto* packet = FindPacket(frame.World, ":v:dir");
    ASSERT_NE(packet, nullptr);
    EXPECT_TRUE(Graphics::IsRenderableVectorFieldPacket(*packet));
#define VF_STYLE_ROW(member, ...) EXPECT_EQ(packet->Style.member, layer.member) << #member;
#include "common/vector_field_style_rows.glsl"
#undef VF_STYLE_ROW
}

// RUNTIME-328: every uniform style value the layer draws with reaches the packet unchanged.
TEST(RuntimeVectorFieldExtraction, EveryStyleFieldReachesThePacket)
{
    Fixture fixture;
    const ECS::EntityHandle entity = AddMesh(fixture.Scene, false);
    fixture.Scene.Raw().get<GS::Vertices>(entity).Properties.GetOrAdd<float>("v:heat", 0.0f).Vector() =
        {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};

    // Differs from a default layer in every drawn field.
    auto layer = Layer(D::MeshVertex, "v:dir");
    layer.LengthMode = Runtime::GeometryVectorFieldLengthMode::Raw;
    layer.Length = 0.75f;
    layer.LineWidthPx = 5.5f;
    layer.Color = {0.1f, 0.2f, 0.3f, 0.4f};
    layer.DepthTested = false;
    layer.ColorProperty = "v:heat";
    layer.ColorMap = Graphics::Colormap::Type::Coolwarm;
    layer.ColorAutoRange = false;
    layer.ColorRangeMin = -3.0f;
    layer.ColorRangeMax = 9.0f;
    const Runtime::GeometryVectorFieldLayerRecipe defaults{};
    ASSERT_NE(layer.LengthMode, defaults.LengthMode);
    ASSERT_NE(layer.Length, defaults.Length);
    ASSERT_NE(layer.LineWidthPx, defaults.LineWidthPx);
    ASSERT_NE(layer.Color, defaults.Color);
    ASSERT_NE(layer.DepthTested, defaults.DepthTested);
    ASSERT_NE(layer.ColorMap, defaults.ColorMap);
    ASSERT_NE(layer.ColorRangeMin, defaults.ColorRangeMin);
    ASSERT_NE(layer.ColorRangeMax, defaults.ColorRangeMax);
    Recipe(fixture.Scene, entity).VectorFields = {layer};

    const auto frame = fixture.Extract();
    const auto* packet = FindPacket(frame.World, ":v:dir");
    ASSERT_NE(packet, nullptr);
    EXPECT_TRUE(Graphics::IsRenderableVectorFieldPacket(*packet));
    EXPECT_EQ(packet->Style.Scale, 0.75f);
    EXPECT_FALSE(packet->Style.NormalizeLength);
    EXPECT_EQ(packet->Style.LineWidthPx, 5.5f);
    EXPECT_EQ(packet->Style.Color, (glm::vec4{0.1f, 0.2f, 0.3f, 0.4f}));
    EXPECT_FALSE(packet->Style.DepthTested);
    EXPECT_EQ(packet->ColorMode, Graphics::VectorFieldColorMode::Colormap);
    EXPECT_EQ(packet->Style.ColorMap, Graphics::Colormap::Type::Coolwarm);
    EXPECT_EQ(packet->Style.ColorRangeMin, -3.0f);
    EXPECT_EQ(packet->Style.ColorRangeMax, 9.0f);
}
