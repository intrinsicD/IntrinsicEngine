#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Core.Error;
import Extrinsic.Runtime.AssetMeshNormals;
import Geometry.HalfedgeMesh.IO;
import Geometry.Properties;
import Geometry.UvAtlas;

namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    std::uint32_t g_FakeBackendCalls = 0u;

    [[nodiscard]] Geometry::UvAtlas::UvAtlasResult FailingBackend(
        const Geometry::UvAtlas::UvAtlasInput& input,
        const Geometry::UvAtlas::UvAtlasOptions&)
    {
        ++g_FakeBackendCalls;
        Geometry::UvAtlas::UvAtlasResult result{};
        result.Status = Geometry::UvAtlas::UvAtlasStatus::BackendFailed;
        result.Provenance = Geometry::UvAtlas::UvAtlasProvenance::None;
        result.Diagnostics.Status = result.Status;
        result.Diagnostics.Provenance = result.Provenance;
        result.Diagnostics.InputVertexCount = input.Positions.size();
        result.Diagnostics.InputFaceCount = input.Faces.size();
        result.Diagnostics.BackendName = "fake-failure";
        result.Diagnostics.BackendDetail = "forced failure";
        return result;
    }

    [[nodiscard]] Geometry::UvAtlas::UvAtlasBackend FakeFailingBackend() noexcept
    {
        return Geometry::UvAtlas::UvAtlasBackend{
            .Name = "fake-failure",
            .Generate = &FailingBackend,
        };
    }

    [[nodiscard]] Geometry::MeshIO::MeshIOResult MakeTrianglePayload(
        const bool includeTexcoords)
    {
        Geometry::MeshIO::MeshIOResult mesh{};
        mesh.Vertices.Resize(3u);
        auto positions = mesh.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3{0.0f});
        positions[0] = glm::vec3{0.0f, 0.0f, 0.0f};
        positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};
        positions[2] = glm::vec3{0.0f, 1.0f, 0.0f};

        auto normals = mesh.Vertices.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f});
        normals[0] = glm::vec3{1.0f, 0.0f, 0.0f};
        normals[1] = glm::vec3{0.0f, 1.0f, 0.0f};
        normals[2] = glm::vec3{0.0f, 0.0f, 1.0f};

        auto colors = mesh.Vertices.GetOrAdd<glm::vec4>("v:color", glm::vec4{0.0f});
        colors[0] = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
        colors[1] = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
        colors[2] = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

        if (includeTexcoords)
        {
            auto texcoords = mesh.Vertices.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f});
            texcoords[0] = glm::vec2{0.0f, 0.0f};
            texcoords[1] = glm::vec2{1.0f, 0.0f};
            texcoords[2] = glm::vec2{0.0f, 1.0f};
        }

        mesh.Faces.Resize(1u);
        auto faces = mesh.Faces.GetOrAdd<std::vector<std::uint32_t>>("f:vertices", {});
        faces[0] = {0u, 1u, 2u};
        return mesh;
    }

    [[nodiscard]] Geometry::MeshIO::MeshIOResult MakeCubePayload()
    {
        Geometry::MeshIO::MeshIOResult mesh{};
        mesh.Vertices.Resize(8u);
        auto positions = mesh.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3{0.0f});
        const std::vector<glm::vec3> sourcePositions{
            {-1.0f, -1.0f, -1.0f},
            { 1.0f, -1.0f, -1.0f},
            { 1.0f,  1.0f, -1.0f},
            {-1.0f,  1.0f, -1.0f},
            {-1.0f, -1.0f,  1.0f},
            { 1.0f, -1.0f,  1.0f},
            { 1.0f,  1.0f,  1.0f},
            {-1.0f,  1.0f,  1.0f},
        };
        for (std::size_t i = 0u; i < sourcePositions.size(); ++i)
        {
            positions[i] = sourcePositions[i];
        }

        auto normals = mesh.Vertices.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f});
        auto colors = mesh.Vertices.GetOrAdd<glm::vec4>("v:color", glm::vec4{0.0f});
        auto heat = mesh.Vertices.GetOrAdd<float>("v:heat", 0.0f);
        for (std::size_t i = 0u; i < sourcePositions.size(); ++i)
        {
            normals[i] = glm::normalize(sourcePositions[i]);
            colors[i] = glm::vec4{
                static_cast<float>(i + 1u),
                static_cast<float>(i + 2u),
                static_cast<float>(i + 3u),
                1.0f};
            heat[i] = static_cast<float>(i) + 0.5f;
        }

        mesh.Faces.Resize(12u);
        auto faces = mesh.Faces.GetOrAdd<std::vector<std::uint32_t>>("f:vertices", {});
        faces[0] = {0u, 2u, 1u};
        faces[1] = {0u, 3u, 2u};
        faces[2] = {4u, 5u, 6u};
        faces[3] = {4u, 6u, 7u};
        faces[4] = {0u, 1u, 5u};
        faces[5] = {0u, 5u, 4u};
        faces[6] = {1u, 2u, 6u};
        faces[7] = {1u, 6u, 5u};
        faces[8] = {2u, 3u, 7u};
        faces[9] = {2u, 7u, 6u};
        faces[10] = {3u, 0u, 4u};
        faces[11] = {3u, 4u, 7u};
        return mesh;
    }

    void ExpectFiniteTexcoords(const Geometry::HalfedgeMesh::Mesh& mesh)
    {
        auto texcoords = mesh.VertexProperties().Get<glm::vec2>("v:texcoord");
        ASSERT_TRUE(texcoords);
        ASSERT_EQ(texcoords.Vector().size(), mesh.VerticesSize());
        for (const glm::vec2 texcoord : texcoords.Vector())
        {
            EXPECT_TRUE(std::isfinite(texcoord.x));
            EXPECT_TRUE(std::isfinite(texcoord.y));
        }
    }
}

TEST(RuntimeAssetMeshNormals, ValidAuthoredUvsArePreservedWithoutInvokingBackend)
{
    g_FakeBackendCalls = 0u;
    const Geometry::UvAtlas::UvAtlasBackend backend = FakeFailingBackend();
    auto payload = MakeTrianglePayload(true);

    auto result = Runtime::BuildRuntimeHalfedgeMeshMaterialization(
        payload,
        Runtime::RuntimeMeshMaterializationOptions{
            .UvResolution = Runtime::RuntimeMeshUvResolutionOptions{
                .Backend = &backend,
            },
        });

    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
    EXPECT_EQ(g_FakeBackendCalls, 0u);
    EXPECT_EQ(result->Diagnostics.TexcoordProvenance,
              Runtime::RuntimeMeshResolvedUvProvenance::AuthoredPreserved);
    EXPECT_TRUE(result->Diagnostics.AuthoredTexcoordsValid);
    EXPECT_TRUE(result->Diagnostics.ResolvedTexcoordsValid);

    auto texcoords = result->Mesh.VertexProperties().Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    ASSERT_EQ(texcoords.Vector().size(), 3u);
    EXPECT_EQ(texcoords[0], glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(texcoords[1], glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(texcoords[2], glm::vec2(0.0f, 1.0f));
}

TEST(RuntimeAssetMeshNormals, MissingUvsFailClosedWhenRequiredBackendFails)
{
    g_FakeBackendCalls = 0u;
    const Geometry::UvAtlas::UvAtlasBackend backend = FakeFailingBackend();
    auto payload = MakeTrianglePayload(false);

    auto result = Runtime::BuildRuntimeHalfedgeMeshMaterialization(
        payload,
        Runtime::RuntimeMeshMaterializationOptions{
            .UvResolution = Runtime::RuntimeMeshUvResolutionOptions{
                .Backend = &backend,
            },
        });

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::AssetInvalidData);
    EXPECT_EQ(g_FakeBackendCalls, 1u);
}

TEST(RuntimeAssetMeshNormals, OptionalBackendFailureReturnsDiagnosticsWithoutFallbackUvs)
{
    g_FakeBackendCalls = 0u;
    const Geometry::UvAtlas::UvAtlasBackend backend = FakeFailingBackend();
    auto payload = MakeTrianglePayload(false);

    auto result = Runtime::BuildRuntimeHalfedgeMeshMaterialization(
        payload,
        Runtime::RuntimeMeshMaterializationOptions{
            .UvResolution = Runtime::RuntimeMeshUvResolutionOptions{
                .FailurePolicy = Runtime::RuntimeMeshUvFailurePolicy::Optional,
                .Backend = &backend,
            },
        });

    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
    EXPECT_EQ(g_FakeBackendCalls, 1u);
    EXPECT_EQ(result->Diagnostics.UvAtlasStatus,
              Geometry::UvAtlas::UvAtlasStatus::BackendFailed);
    EXPECT_EQ(result->Diagnostics.TexcoordProvenance,
              Runtime::RuntimeMeshResolvedUvProvenance::None);
    EXPECT_FALSE(result->Diagnostics.ResolvedTexcoordsValid);
    EXPECT_FALSE(result->Mesh.VertexProperties().Get<glm::vec2>("v:texcoord"));
}

TEST(RuntimeAssetMeshNormals, InvalidAuthoredUvsGenerateAtlas)
{
    auto payload = MakeTrianglePayload(true);
    auto texcoords = payload.Vertices.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{std::numeric_limits<float>::quiet_NaN(), 0.0f};

    auto result = Runtime::BuildRuntimeHalfedgeMeshMaterialization(payload);

    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
    EXPECT_TRUE(result->Diagnostics.AuthoredTexcoordsRejected);
    EXPECT_EQ(result->Diagnostics.TexcoordProvenance,
              Runtime::RuntimeMeshResolvedUvProvenance::GeneratedAtlas);
    EXPECT_TRUE(result->Diagnostics.ResolvedTexcoordsValid);
    EXPECT_GE(result->Diagnostics.ChartCount, 1u);
    ExpectFiniteTexcoords(result->Mesh);
}

TEST(RuntimeAssetMeshNormals, SeamSplitOutputPreservesVertexPropertiesAndSourceXrefs)
{
    auto payload = MakeCubePayload();

    auto result = Runtime::BuildRuntimeHalfedgeMeshMaterialization(payload);

    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
    EXPECT_EQ(result->Diagnostics.TexcoordProvenance,
              Runtime::RuntimeMeshResolvedUvProvenance::GeneratedAtlas);
    EXPECT_TRUE(result->Diagnostics.ResolvedTexcoordsValid);
    EXPECT_GT(result->Diagnostics.SeamSplitVertexCount, 0u);
    ExpectFiniteTexcoords(result->Mesh);

    auto sourceVertex = result->Mesh.VertexProperties().Get<std::uint32_t>("v:source_vertex");
    auto normals = result->Mesh.VertexProperties().Get<glm::vec3>("v:normal");
    auto colors = result->Mesh.VertexProperties().Get<glm::vec4>("v:color");
    auto heat = result->Mesh.VertexProperties().Get<float>("v:heat");
    ASSERT_TRUE(sourceVertex);
    ASSERT_TRUE(normals);
    ASSERT_TRUE(colors);
    ASSERT_TRUE(heat);
    ASSERT_EQ(sourceVertex.Vector().size(), result->Mesh.VerticesSize());
    ASSERT_EQ(normals.Vector().size(), result->Mesh.VerticesSize());
    ASSERT_EQ(colors.Vector().size(), result->Mesh.VerticesSize());
    ASSERT_EQ(heat.Vector().size(), result->Mesh.VerticesSize());

    const auto sourceNormals = payload.Vertices.Get<glm::vec3>("v:normal");
    const auto sourceColors = payload.Vertices.Get<glm::vec4>("v:color");
    const auto sourceHeat = payload.Vertices.Get<float>("v:heat");
    ASSERT_TRUE(sourceNormals);
    ASSERT_TRUE(sourceColors);
    ASSERT_TRUE(sourceHeat);
    for (std::size_t i = 0u; i < result->Mesh.VerticesSize(); ++i)
    {
        const std::uint32_t source = sourceVertex[i];
        ASSERT_LT(source, sourceNormals.Vector().size());
        EXPECT_EQ(normals[i], sourceNormals[source]);
        EXPECT_EQ(colors[i], sourceColors[source]);
        EXPECT_FLOAT_EQ(heat[i], sourceHeat[source]);
    }
}
