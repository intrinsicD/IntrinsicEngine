#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Properties;

using Extrinsic::ECS::Components::GeometrySources::ConstSourceView;
using Extrinsic::ECS::Components::GeometrySources::Domain;
using Extrinsic::ECS::Components::GeometrySources::Edges;
using Extrinsic::ECS::Components::GeometrySources::Faces;
using Extrinsic::ECS::Components::GeometrySources::Halfedges;
using Extrinsic::ECS::Components::GeometrySources::Nodes;
using Extrinsic::ECS::Components::GeometrySources::Vertices;
using Extrinsic::Runtime::MeshPackBuffer;
using Extrinsic::Runtime::MeshPackResult;
using Extrinsic::Runtime::MeshPackStatus;
using Extrinsic::Runtime::MeshVertex;
using Extrinsic::Runtime::PackMesh;

namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    struct MeshScratch
    {
        Vertices VertexSource{};
        Edges EdgeSource{};
        Halfedges HalfedgeSource{};
        Faces FaceSource{};

        [[nodiscard]] ConstSourceView View(Domain domain = Domain::Mesh) const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = domain;
            view.VertexSource = &VertexSource;
            view.EdgeSource = &EdgeSource;
            view.HalfedgeSource = &HalfedgeSource;
            view.FaceSource = &FaceSource;
            return view;
        }
    };

    void SetPositions(Vertices& v, const std::vector<glm::vec3>& positions)
    {
        v.Properties.Resize(positions.size());
        auto pos = v.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    void SetNormals(Vertices& v, const std::vector<glm::vec3>& normals)
    {
        auto normal = v.Properties.GetOrAdd<glm::vec3>(std::string{pn::kNormal}, glm::vec3(0.0f));
        normal.Vector() = normals;
    }

    void SetTexcoords(Vertices& v, const std::vector<glm::vec2>& texcoords)
    {
        auto texcoord = v.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
        texcoord.Vector() = texcoords;
    }

    void SetColors(Vertices& v, const std::vector<glm::vec4>& colors)
    {
        auto color = v.Properties.GetOrAdd<glm::vec4>("v:color", glm::vec4(1.0f));
        color.Vector() = colors;
    }

    void SetVec3Property(Vertices& v,
                         const std::string& name,
                         const std::vector<glm::vec3>& values)
    {
        auto prop = v.Properties.GetOrAdd<glm::vec3>(name, glm::vec3(0.0f));
        prop.Vector() = values;
    }

    void SetVec4Property(Vertices& v,
                         const std::string& name,
                         const std::vector<glm::vec4>& values)
    {
        auto prop = v.Properties.GetOrAdd<glm::vec4>(name, glm::vec4(1.0f));
        prop.Vector() = values;
    }

    void RemoveTexcoords(Vertices& v)
    {
        auto& registry = v.Properties.Registry();
        const auto id = registry.Find("v:texcoord");
        ASSERT_TRUE(id.has_value());
        EXPECT_TRUE(registry.Remove(*id));
    }

    void SetEdges(Edges& e,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1)
    {
        e.Properties.Resize(v0.size());
        auto p0 = e.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV0}, 0u);
        auto p1 = e.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV1}, 0u);
        p0.Vector() = v0;
        p1.Vector() = v1;
    }

    void SetHalfedges(Halfedges& h,
                      const std::vector<std::uint32_t>& toVertex,
                      const std::vector<std::uint32_t>& next,
                      const std::vector<std::uint32_t>& face)
    {
        h.Properties.Resize(toVertex.size());
        auto pt = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeToVertex}, kInvalidIndex);
        auto pn_ = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeNext}, kInvalidIndex);
        auto pf = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeFace}, kInvalidIndex);
        pt.Vector() = toVertex;
        pn_.Vector() = next;
        pf.Vector() = face;
    }

    void SetFaces(Faces& f, const std::vector<std::uint32_t>& faceHe)
    {
        f.Properties.Resize(faceHe.size());
        auto p = f.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kFaceHalfedge}, kInvalidIndex);
        p.Vector() = faceHe;
    }

    // Build a single-triangle mesh with one interior face (halfedges 0..2) and
    // three boundary halfedges (3..5). Only the interior face is reachable
    // through `f:halfedge`. Vertices: (0,0,0), (1,0,0), (0,1,0).
    MeshScratch BuildSingleTriangle()
    {
        MeshScratch m{};
        SetPositions(m.VertexSource, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        SetTexcoords(m.VertexSource, {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
        });
        SetEdges(m.EdgeSource, {0u, 1u, 2u}, {1u, 2u, 0u});

        // halfedge i targets vertex (i+1)%3; next walks 0->1->2->0.
        SetHalfedges(m.HalfedgeSource,
                     /*toVertex*/ {1u, 2u, 0u, 0u, 2u, 1u},
                     /*next*/     {1u, 2u, 0u, 5u, 3u, 4u},
                     /*face*/     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        SetFaces(m.FaceSource, {0u});
        return m;
    }

    // Build a single-quad mesh — one face with four halfedges. Vertices form
    // a unit square in the XY plane.
    MeshScratch BuildSingleQuad()
    {
        MeshScratch m{};
        SetPositions(m.VertexSource, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        SetTexcoords(m.VertexSource, {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f},
        });
        SetEdges(m.EdgeSource, {0u, 1u, 2u, 3u}, {1u, 2u, 3u, 0u});
        SetHalfedges(m.HalfedgeSource,
                     /*toVertex*/ {1u, 2u, 3u, 0u},
                     /*next*/     {1u, 2u, 3u, 0u},
                     /*face*/     {0u, 0u, 0u, 0u});
        SetFaces(m.FaceSource, {0u});
        return m;
    }
}

TEST(MeshGeometryPackerTest, SingleTriangleProducesThreeIndices)
{
    const MeshScratch mesh = BuildSingleTriangle();
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);

    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 3u);
    EXPECT_EQ(result.Upload->SurfaceIndices.size(), 3u);
    EXPECT_EQ(result.Upload->LineIndices.size(), 0u);
    EXPECT_EQ(result.Upload->PackedVertexBytes.size_bytes(), sizeof(MeshVertex) * 3u);
    EXPECT_EQ(result.Upload->PositionBytes.size_bytes(), sizeof(glm::vec3) * 3u);
    EXPECT_EQ(result.Upload->TexcoordBytes.size_bytes(), sizeof(glm::vec2) * 3u);
    EXPECT_EQ(result.Upload->NormalBytes.size_bytes(), sizeof(glm::vec3) * 3u);
    ASSERT_NE(result.Upload->DebugName, nullptr);
    EXPECT_STREQ(result.Upload->DebugName, "Runtime.Mesh");

    // Surface indices must match the halfedge target ring (1, 2, 0) per
    // `BuildSingleTriangle` halfedge wiring.
    EXPECT_EQ(result.Upload->SurfaceIndices[0], 1u);
    EXPECT_EQ(result.Upload->SurfaceIndices[1], 2u);
    EXPECT_EQ(result.Upload->SurfaceIndices[2], 0u);
}

TEST(MeshGeometryPackerTest, SingleTriangleVertexBytesMatchPositions)
{
    const MeshScratch mesh = BuildSingleTriangle();
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());

    const auto* verts = reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].Px, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Py, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].Px, 1.0f);
    EXPECT_FLOAT_EQ(verts[2].Py, 1.0f);
    EXPECT_FLOAT_EQ(verts[0].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Nx, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Ny, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Nz, 1.0f);

    ASSERT_EQ(result.Upload->PositionBytes.size_bytes(), sizeof(glm::vec3) * 3u);
    const auto* positions = reinterpret_cast<const glm::vec3*>(result.Upload->PositionBytes.data());
    EXPECT_FLOAT_EQ(positions[0].x, 0.0f);
    EXPECT_FLOAT_EQ(positions[0].y, 0.0f);
    EXPECT_FLOAT_EQ(positions[1].x, 1.0f);
    EXPECT_FLOAT_EQ(positions[2].y, 1.0f);
}

TEST(MeshGeometryPackerTest, MissingTexcoordsUseDefaultUvsEvenWhenNormalsExist)
{
    MeshScratch mesh = BuildSingleTriangle();
    RemoveTexcoords(mesh.VertexSource);
    SetNormals(mesh.VertexSource, {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    });
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);

    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    const auto* verts = reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Nx, 1.0f);
    EXPECT_FLOAT_EQ(verts[1].Ny, 1.0f);
    EXPECT_FLOAT_EQ(verts[2].Nz, 1.0f);
}

TEST(MeshGeometryPackerTest, UsesVertexTexcoordsAsOnlyUvSource)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetNormals(mesh.VertexSource, {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    });
    SetTexcoords(mesh.VertexSource, {
        {0.25f, 0.75f},
        {0.50f, 0.25f},
        {1.00f, 0.00f},
    });
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());

    const auto* verts = reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].U, 0.25f);
    EXPECT_FLOAT_EQ(verts[0].V, 0.75f);
    EXPECT_FLOAT_EQ(verts[1].U, 0.50f);
    EXPECT_FLOAT_EQ(verts[1].V, 0.25f);
    EXPECT_FLOAT_EQ(verts[2].U, 1.00f);
    EXPECT_FLOAT_EQ(verts[2].V, 0.00f);
}

TEST(MeshGeometryPackerTest, PacksVertexNormalsWithoutUsingUvFields)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetNormals(mesh.VertexSource, {
        {2.0f, 0.0f, 0.0f},
        {0.0f, 3.0f, 0.0f},
        {0.0f, 0.0f, 4.0f},
    });
    SetTexcoords(mesh.VertexSource, {
        {0.25f, 0.75f},
        {0.50f, 0.25f},
        {1.00f, 0.00f},
    });
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());

    const auto* verts = reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].U, 0.25f);
    EXPECT_FLOAT_EQ(verts[0].V, 0.75f);
    EXPECT_FLOAT_EQ(verts[0].Nx, 1.0f);
    EXPECT_FLOAT_EQ(verts[0].Ny, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Nz, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].U, 0.50f);
    EXPECT_FLOAT_EQ(verts[1].V, 0.25f);
    EXPECT_FLOAT_EQ(verts[1].Nx, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].Ny, 1.0f);
    EXPECT_FLOAT_EQ(verts[1].Nz, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].U, 1.00f);
    EXPECT_FLOAT_EQ(verts[2].V, 0.00f);
    EXPECT_FLOAT_EQ(verts[2].Nx, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].Ny, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].Nz, 1.0f);
}

TEST(MeshGeometryPackerTest, CountMatchedVertexColorsProducePackedColorStream)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetColors(mesh.VertexSource, {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    });
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);

    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    ASSERT_EQ(result.Upload->PackedVertexColors.size(), 3u);
    EXPECT_EQ(result.Upload->PackedVertexColors[0], 0xFF0000FFu);
    EXPECT_EQ(result.Upload->PackedVertexColors[1], 0xFF00FF00u);
    EXPECT_EQ(result.Upload->PackedVertexColors[2], 0xFFFF0000u);
    const auto* colorStream = scratch.Channels.Find(Extrinsic::Runtime::VertexChannel::Color);
    ASSERT_NE(colorStream, nullptr);
    EXPECT_EQ(colorStream->Bytes.size(), sizeof(std::uint32_t) * 3u);
}

TEST(MeshGeometryPackerTest, ExplicitVertexChannelBindingsOverrideNormalsAndColors)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetNormals(mesh.VertexSource, {
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
    });
    SetVec3Property(mesh.VertexSource, "v:custom_normal", {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    });
    SetVec4Property(mesh.VertexSource, "v:paint", {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    });
    Extrinsic::Runtime::VertexChannelBindingSet bindings{};
    bindings.Normal = Extrinsic::Runtime::VertexChannelSourceBinding{
        .Enabled = true,
        .SourceType = Extrinsic::Runtime::AttributeSourceType::Vec3,
        .SourceProperty = "v:custom_normal",
    };
    bindings.Color = Extrinsic::Runtime::VertexChannelSourceBinding{
        .Enabled = true,
        .SourceType = Extrinsic::Runtime::AttributeSourceType::Vec4,
        .SourceProperty = "v:paint",
    };

    MeshPackBuffer scratch;
    const MeshPackResult result = PackMesh(mesh.View(), &bindings, scratch);

    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    const auto* verts =
        reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].Nx, 1.0f);
    EXPECT_FLOAT_EQ(verts[1].Ny, 1.0f);
    EXPECT_FLOAT_EQ(verts[2].Nz, 1.0f);
    ASSERT_EQ(result.Upload->NormalBytes.size_bytes(), sizeof(glm::vec3) * 3u);
    const auto* normalBytes =
        reinterpret_cast<const glm::vec3*>(result.Upload->NormalBytes.data());
    EXPECT_FLOAT_EQ(normalBytes[0].x, 1.0f);
    EXPECT_FLOAT_EQ(normalBytes[1].y, 1.0f);
    ASSERT_EQ(result.Upload->PackedVertexColors.size(), 3u);
    EXPECT_EQ(result.Upload->PackedVertexColors[0], 0xFF0000FFu);
    EXPECT_EQ(result.Upload->PackedVertexColors[1], 0xFF00FF00u);
    EXPECT_EQ(result.Upload->PackedVertexColors[2], 0xFFFF0000u);
}

TEST(MeshGeometryPackerTest, MissingVertexColorsLeavePackedColorStreamEmpty)
{
    const MeshScratch mesh = BuildSingleTriangle();
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);

    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_TRUE(result.Upload->PackedVertexColors.empty());
    EXPECT_TRUE(scratch.PackedColors.empty());
}

TEST(MeshGeometryPackerTest, InvalidVertexNormalFallsBackWithoutChangingUv)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetNormals(mesh.VertexSource, {
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    });
    SetTexcoords(mesh.VertexSource, {
        {0.25f, 0.75f},
        {0.50f, 0.25f},
        {1.00f, 0.00f},
    });
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());

    const auto* verts = reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].U, 0.25f);
    EXPECT_FLOAT_EQ(verts[0].V, 0.75f);
    EXPECT_FLOAT_EQ(verts[0].Nx, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Ny, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].Nz, 1.0f);
    EXPECT_FLOAT_EQ(verts[1].Nx, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].Ny, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].Nz, 1.0f);
    EXPECT_FLOAT_EQ(verts[2].Nx, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].Ny, 1.0f);
    EXPECT_FLOAT_EQ(verts[2].Nz, 0.0f);
}

TEST(MeshGeometryPackerTest, MismatchedTexcoordCountUsesDefaultUvs)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetTexcoords(mesh.VertexSource, {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
    });
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);

    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    const auto* verts = reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].V, 0.0f);
}

TEST(MeshGeometryPackerTest, NonFiniteTexcoordUsesDefaultUvForInvalidVertex)
{
    MeshScratch mesh = BuildSingleTriangle();
    auto texcoord = mesh.VertexSource.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoord);
    texcoord.Vector()[1].x = std::numeric_limits<float>::quiet_NaN();
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);

    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    const auto* verts = reinterpret_cast<const MeshVertex*>(result.Upload->PackedVertexBytes.data());
    EXPECT_FLOAT_EQ(verts[0].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[0].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[1].V, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].U, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].V, 1.0f);
}

TEST(MeshGeometryPackerTest, SingleTriangleLocalSphereCentersAtAabbMidpoint)
{
    const MeshScratch mesh = BuildSingleTriangle();
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());

    const glm::vec4 sphere = result.Upload->LocalBounds.LocalSphere;
    EXPECT_FLOAT_EQ(sphere.x, 0.5f);
    EXPECT_FLOAT_EQ(sphere.y, 0.5f);
    EXPECT_FLOAT_EQ(sphere.z, 0.0f);
    // AABB diagonal length 0.5 * sqrt(1 + 1 + 0) ≈ 0.7071
    EXPECT_NEAR(sphere.w, 0.7071067f, 1e-5f);
}

TEST(MeshGeometryPackerTest, QuadFanTriangulatesIntoTwoTriangles)
{
    const MeshScratch mesh = BuildSingleQuad();
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 4u);
    ASSERT_EQ(result.Upload->SurfaceIndices.size(), 6u);

    // Ring is (1, 2, 3, 0); fan from ring[0]=1: (1,2,3) + (1,3,0).
    EXPECT_EQ(result.Upload->SurfaceIndices[0], 1u);
    EXPECT_EQ(result.Upload->SurfaceIndices[1], 2u);
    EXPECT_EQ(result.Upload->SurfaceIndices[2], 3u);
    EXPECT_EQ(result.Upload->SurfaceIndices[3], 1u);
    EXPECT_EQ(result.Upload->SurfaceIndices[4], 3u);
    EXPECT_EQ(result.Upload->SurfaceIndices[5], 0u);
}

TEST(MeshGeometryPackerTest, PackIsDeterministicAcrossInvocations)
{
    const MeshScratch mesh = BuildSingleQuad();
    MeshPackBuffer scratchA;
    MeshPackBuffer scratchB;

    const MeshPackResult a = PackMesh(mesh.View(), scratchA);
    const MeshPackResult b = PackMesh(mesh.View(), scratchB);

    ASSERT_EQ(a.Status, MeshPackStatus::Success);
    ASSERT_EQ(b.Status, MeshPackStatus::Success);
    ASSERT_TRUE(a.Upload.has_value());
    ASSERT_TRUE(b.Upload.has_value());

    ASSERT_EQ(a.Upload->PackedVertexBytes.size_bytes(), b.Upload->PackedVertexBytes.size_bytes());
    EXPECT_EQ(std::memcmp(a.Upload->PackedVertexBytes.data(),
                           b.Upload->PackedVertexBytes.data(),
                           a.Upload->PackedVertexBytes.size_bytes()),
              0);
    EXPECT_EQ(a.Upload->SurfaceIndices.size(), b.Upload->SurfaceIndices.size());
    for (std::size_t i = 0; i < a.Upload->SurfaceIndices.size(); ++i)
    {
        EXPECT_EQ(a.Upload->SurfaceIndices[i], b.Upload->SurfaceIndices[i]);
    }
}

TEST(MeshGeometryPackerTest, ScratchBufferIsReusedWithoutAccumulation)
{
    MeshPackBuffer scratch;
    const MeshScratch triMesh = BuildSingleTriangle();
    const MeshScratch quadMesh = BuildSingleQuad();

    const MeshPackResult triPack = PackMesh(triMesh.View(), scratch);
    ASSERT_EQ(triPack.Status, MeshPackStatus::Success);
    const std::size_t triVertexBytes = scratch.VertexBytes.size();
    const std::size_t triSurfaceIndices = scratch.SurfaceIndices.size();
    EXPECT_EQ(triVertexBytes, sizeof(MeshVertex) * 3u);
    EXPECT_EQ(triSurfaceIndices, 3u);

    const MeshPackResult quadPack = PackMesh(quadMesh.View(), scratch);
    ASSERT_EQ(quadPack.Status, MeshPackStatus::Success);
    // After packing the quad the scratch holds exactly the quad payload — no
    // residue from the previous triangle pack.
    EXPECT_EQ(scratch.VertexBytes.size(), sizeof(MeshVertex) * 4u);
    EXPECT_EQ(scratch.SurfaceIndices.size(), 6u);

    const MeshPackResult triPack2 = PackMesh(triMesh.View(), scratch);
    ASSERT_EQ(triPack2.Status, MeshPackStatus::Success);
    EXPECT_EQ(scratch.VertexBytes.size(), sizeof(MeshVertex) * 3u);
    EXPECT_EQ(scratch.SurfaceIndices.size(), 3u);
}

TEST(MeshGeometryPackerTest, WrongDomainIsRejected)
{
    Nodes nodes{};
    ConstSourceView view{};
    view.NodeSource = &nodes;
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(view, scratch);

    EXPECT_EQ(result.Status, MeshPackStatus::WrongDomain);
    EXPECT_FALSE(result.Upload.has_value());
    EXPECT_TRUE(scratch.SurfaceIndices.empty());
    EXPECT_TRUE(scratch.VertexBytes.empty());
}

TEST(MeshGeometryPackerTest, MissingVertexSourceIsRejected)
{
    MeshPackBuffer scratch;
    ConstSourceView view{};
    view.HasMeshTopologyMarker = true;
    view.VertexSource = nullptr;

    const MeshPackResult result = PackMesh(view, scratch);

    EXPECT_EQ(result.Status, MeshPackStatus::MissingPositions);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshGeometryPackerTest, MissingPositionPropertyIsRejected)
{
    MeshScratch mesh = BuildSingleTriangle();
    // Wipe the v:position property, leaving an empty Vertices PropertySet.
    mesh.VertexSource.Properties = Geometry::PropertySet{};
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::MissingPositions);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshGeometryPackerTest, MissingHalfedgeTopologyIsRejected)
{
    MeshScratch mesh = BuildSingleTriangle();
    mesh.HalfedgeSource.Properties = Geometry::PropertySet{}; // strip h:to_vertex / h:next / h:face
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::MissingHalfedgeTopology);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshGeometryPackerTest, MeshMarkerWithoutHalfedgesReportsMissingHalfedgeTopology)
{
    Vertices vertices{};
    SetPositions(vertices, {{0.0f, 0.0f, 0.0f}});
    ConstSourceView view{};
    view.VertexSource = &vertices;
    view.HasMeshTopologyMarker = true;
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(view, scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::MissingHalfedgeTopology);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshGeometryPackerTest, MissingFaceTopologyIsRejected)
{
    MeshScratch mesh = BuildSingleTriangle();
    mesh.FaceSource.Properties = Geometry::PropertySet{};
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::MissingFaceTopology);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshGeometryPackerTest, EmptyVerticesAreRejected)
{
    MeshScratch mesh{};
    SetPositions(mesh.VertexSource, {}); // zero positions
    SetHalfedges(mesh.HalfedgeSource, {0u}, {0u}, {0u});
    SetFaces(mesh.FaceSource, {0u});
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::EmptyMesh);
}

TEST(MeshGeometryPackerTest, EmptyFacesAreRejected)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetFaces(mesh.FaceSource, {}); // strip faces but keep topology arrays
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::EmptyMesh);
}

TEST(MeshGeometryPackerTest, OutOfRangeFaceHalfedgeIndexIsRejected)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetFaces(mesh.FaceSource, {99u});
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::InvalidTopology);
}

TEST(MeshGeometryPackerTest, OutOfRangeHalfedgeTargetIsRejected)
{
    MeshScratch mesh = BuildSingleTriangle();
    auto toVertex = mesh.HalfedgeSource.Properties.Get<std::uint32_t>(pn::kHalfedgeToVertex);
    ASSERT_TRUE(toVertex);
    toVertex.Vector()[0] = 42u; // out of range — only 3 vertices
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::InvalidTopology);
}

TEST(MeshGeometryPackerTest, BrokenNextHalfedgeChainFailsClosed)
{
    MeshScratch mesh = BuildSingleTriangle();
    auto next = mesh.HalfedgeSource.Properties.Get<std::uint32_t>(pn::kHalfedgeNext);
    ASSERT_TRUE(next);
    next.Vector()[0] = kInvalidIndex; // breaks the closed ring before it returns to start
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::InvalidTopology);
}

TEST(MeshGeometryPackerTest, NonClosingNextChainFailsClosed)
{
    // Build a halfedge chain whose `next` pointers form an infinite self-loop
    // on a single halfedge without ever returning to the face's first
    // halfedge. The step guard must reject this rather than spinning.
    MeshScratch mesh = BuildSingleTriangle();
    auto next = mesh.HalfedgeSource.Properties.Get<std::uint32_t>(pn::kHalfedgeNext);
    ASSERT_TRUE(next);
    // 0 -> 1 -> 1 -> 1 -> ... never reaches halfedge 2 which would close
    // the original ring starting at halfedge 0.
    next.Vector()[1] = 1u;
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::InvalidTopology);
}

TEST(MeshGeometryPackerTest, NonFinitePositionIsRejected)
{
    MeshScratch mesh = BuildSingleTriangle();
    auto pos = mesh.VertexSource.Properties.Get<glm::vec3>(pn::kPosition);
    ASSERT_TRUE(pos);
    pos.Vector()[1].x = std::numeric_limits<float>::quiet_NaN();
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::NonFinitePosition);
    EXPECT_FALSE(result.Upload.has_value());
    EXPECT_TRUE(scratch.SurfaceIndices.empty());
    EXPECT_TRUE(scratch.VertexBytes.empty());
}

TEST(MeshGeometryPackerTest, AllInvalidFaceHalfedgesYieldDegenerateAllFaces)
{
    MeshScratch mesh = BuildSingleTriangle();
    SetFaces(mesh.FaceSource, {kInvalidIndex, kInvalidIndex});
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::DegenerateAllFaces);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshGeometryPackerTest, MissingHalfedgeFacePropertyIsRejected)
{
    // `PopulateFromMesh` always writes `h:face`; missing it is treated as
    // missing halfedge topology so callers can distinguish a wrong-shape
    // source from an internally inconsistent mesh.
    MeshScratch mesh = BuildSingleTriangle();
    auto& registry = mesh.HalfedgeSource.Properties.Registry();
    const auto id = registry.Find(pn::kHalfedgeFace);
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(registry.Remove(*id));
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::MissingHalfedgeTopology);
}

TEST(MeshGeometryPackerTest, DeletedFaceSlotIsSkippedNotTriangulated)
{
    // Simulate `HalfedgeMesh::DeleteFace`: the face's interior halfedges had
    // their `h:face` invalidated, but `f:halfedge` still points to the
    // formerly-walkable ring. The packer must rely on `h:face` ownership to
    // skip this slot rather than fan-triangulating it.
    MeshScratch mesh = BuildSingleTriangle();
    mesh.FaceSource.NumDeleted = 1u;
    auto faceProp = mesh.HalfedgeSource.Properties.Get<std::uint32_t>(pn::kHalfedgeFace);
    ASSERT_TRUE(faceProp);
    faceProp.Vector()[0] = kInvalidIndex;
    faceProp.Vector()[1] = kInvalidIndex;
    faceProp.Vector()[2] = kInvalidIndex;
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::DegenerateAllFaces);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshGeometryPackerTest, AliveFaceSurvivesAlongsideDeletedFaceSlot)
{
    // Two face slots: slot 0 alive (halfedges 0..2), slot 1 deleted (its
    // ring of halfedges 3..5 has been kept walkable by stale `h:next` but
    // its halfedges' `h:face` was cleared). Only slot 0 must be packed.
    MeshScratch mesh{};
    SetPositions(mesh.VertexSource, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {3.0f, 0.0f, 0.0f},
        {2.0f, 1.0f, 0.0f},
    });
    SetTexcoords(mesh.VertexSource, {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
        {2.0f, 0.0f},
        {3.0f, 0.0f},
        {2.0f, 1.0f},
    });
    SetEdges(mesh.EdgeSource, {0u, 1u, 2u, 3u, 4u, 5u}, {1u, 2u, 0u, 4u, 5u, 3u});
    // halfedges 0..2 belong to live face 0; 3..5 form a still-walkable ring
    // but their `h:face` has been cleared by a (simulated) DeleteFace.
    SetHalfedges(mesh.HalfedgeSource,
                 /*toVertex*/ {1u, 2u, 0u, 4u, 5u, 3u},
                 /*next*/     {1u, 2u, 0u, 4u, 5u, 3u},
                 /*face*/     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
    // Both face slots still have a `f:halfedge` pointing at their original
    // ring (deletion does not clear it).
    SetFaces(mesh.FaceSource, {0u, 3u});
    mesh.FaceSource.NumDeleted = 1u;

    MeshPackBuffer scratch;
    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    ASSERT_EQ(result.Status, MeshPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 6u);
    ASSERT_EQ(result.Upload->SurfaceIndices.size(), 3u);
    // Only the live face's ring (halfedge targets 1, 2, 0) is emitted.
    EXPECT_EQ(result.Upload->SurfaceIndices[0], 1u);
    EXPECT_EQ(result.Upload->SurfaceIndices[1], 2u);
    EXPECT_EQ(result.Upload->SurfaceIndices[2], 0u);
}

TEST(MeshGeometryPackerTest, RingHalfedgeClaimingDifferentFaceIsRejected)
{
    // The first halfedge claims face 0 (passes the ownership check), but a
    // mid-ring halfedge claims face 5 — the mesh's topology is corrupt
    // (mixed-owner ring) and must fail closed rather than silently emit a
    // misattributed triangle.
    MeshScratch mesh = BuildSingleTriangle();
    auto faceProp = mesh.HalfedgeSource.Properties.Get<std::uint32_t>(pn::kHalfedgeFace);
    ASSERT_TRUE(faceProp);
    faceProp.Vector()[1] = 5u; // out of range w.r.t. face count, but the
                               // ownership check fires first.
    MeshPackBuffer scratch;

    const MeshPackResult result = PackMesh(mesh.View(), scratch);
    EXPECT_EQ(result.Status, MeshPackStatus::InvalidTopology);
}

TEST(MeshGeometryPackerTest, DebugNameForStatusReturnsStableStrings)
{
    EXPECT_STREQ(Extrinsic::Runtime::DebugNameForMeshPackStatus(MeshPackStatus::Success),
                 "Mesh.Success");
    EXPECT_STREQ(Extrinsic::Runtime::DebugNameForMeshPackStatus(MeshPackStatus::WrongDomain),
                 "Mesh.WrongDomain");
    EXPECT_STREQ(Extrinsic::Runtime::DebugNameForMeshPackStatus(MeshPackStatus::InvalidTopology),
                 "Mesh.InvalidTopology");
    EXPECT_STREQ(Extrinsic::Runtime::DebugNameForMeshPackStatus(MeshPackStatus::NonFinitePosition),
                 "Mesh.NonFinitePosition");
    EXPECT_STREQ(Extrinsic::Runtime::DebugNameForMeshPackStatus(MeshPackStatus::MissingTexcoords),
                 "Mesh.MissingTexcoords");
    EXPECT_STREQ(Extrinsic::Runtime::DebugNameForMeshPackStatus(MeshPackStatus::NonFiniteTexcoord),
                 "Mesh.NonFiniteTexcoord");
}
