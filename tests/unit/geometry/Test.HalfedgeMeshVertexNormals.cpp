#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.Properties;

namespace
{
    using Geometry::HalfedgeMesh::Mesh;
    namespace VertexNormals = Geometry::HalfedgeMesh::VertexNormals;

    [[nodiscard]] glm::vec3 Normalize(const glm::vec3 value)
    {
        const float len = glm::length(value);
        return len > 0.0f ? value / len : value;
    }

    void ExpectVecNear(const glm::vec3 actual, const glm::vec3 expected, const float epsilon = 1.0e-5f)
    {
        EXPECT_NEAR(actual.x, expected.x, epsilon);
        EXPECT_NEAR(actual.y, expected.y, epsilon);
        EXPECT_NEAR(actual.z, expected.z, epsilon);
    }

    [[nodiscard]] Mesh MakeAsymmetricTwoTriangleFan()
    {
        Mesh mesh;
        const auto v0 = mesh.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
        const auto v3 = mesh.AddVertex(glm::vec3{0.0f, 1.0f, 4.0f});

        EXPECT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
        EXPECT_TRUE(mesh.AddTriangle(v0, v2, v3).has_value());
        return mesh;
    }

    [[nodiscard]] Mesh MakeCollinearTriangle()
    {
        Mesh mesh;
        const auto v0 = mesh.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex(glm::vec3{2.0f, 0.0f, 0.0f});
        EXPECT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
        return mesh;
    }
}

TEST(HalfedgeMeshVertexNormals, DebugNamesAreStable)
{
    EXPECT_EQ(VertexNormals::DebugName(VertexNormals::AveragingMode::UniformFace), "UniformFace");
    EXPECT_EQ(VertexNormals::DebugName(VertexNormals::AveragingMode::AreaWeighted), "AreaWeighted");
    EXPECT_EQ(VertexNormals::DebugName(VertexNormals::AveragingMode::AngleWeighted), "AngleWeighted");
    EXPECT_EQ(VertexNormals::DebugName(VertexNormals::AveragingMode::MaxWeighted), "MaxWeighted");
    EXPECT_EQ(VertexNormals::DebugName(VertexNormals::RecomputeStatus::Success), "Success");
}

TEST(HalfedgeMeshVertexNormals, WeightingModesProduceExpectedCornerNormal)
{
    auto mesh = MakeAsymmetricTwoTriangleFan();
    const Geometry::VertexHandle center{0u};

    VertexNormals::Params params;
    params.OutputProperty = "v:test_normal";

    params.Weighting = VertexNormals::AveragingMode::UniformFace;
    auto uniform = VertexNormals::Recompute(mesh, params);
    ASSERT_EQ(uniform.Status, VertexNormals::RecomputeStatus::Success);
    ASSERT_TRUE(uniform.Normals.IsValid());
    EXPECT_EQ(uniform.ProcessedFaceCount, 2u);
    EXPECT_EQ(uniform.WrittenCount, 4u);
    EXPECT_EQ(uniform.FallbackVertexCount, 0u);
    ExpectVecNear(uniform.Normals[center], Normalize(glm::vec3{1.0f, 0.0f, 1.0f}));

    params.Weighting = VertexNormals::AveragingMode::AreaWeighted;
    const auto area = VertexNormals::Recompute(mesh, params);
    ASSERT_EQ(area.Status, VertexNormals::RecomputeStatus::Success);
    ExpectVecNear(area.Normals[center], Normalize(glm::vec3{4.0f, 0.0f, 1.0f}));

    params.Weighting = VertexNormals::AveragingMode::AngleWeighted;
    const auto angle = VertexNormals::Recompute(mesh, params);
    ASSERT_EQ(angle.Status, VertexNormals::RecomputeStatus::Success);
    const float xAngle = static_cast<float>(std::acos(1.0 / std::sqrt(17.0)));
    const float zAngle = static_cast<float>(std::numbers::pi / 2.0);
    ExpectVecNear(angle.Normals[center], Normalize(glm::vec3{xAngle, 0.0f, zAngle}));

    params.Weighting = VertexNormals::AveragingMode::MaxWeighted;
    const auto maxWeighted = VertexNormals::Recompute(mesh, params);
    ASSERT_EQ(maxWeighted.Status, VertexNormals::RecomputeStatus::Success);
    ExpectVecNear(maxWeighted.Normals[center], Normalize(glm::vec3{4.0f / 17.0f, 0.0f, 1.0f}));
}

TEST(HalfedgeMeshVertexNormals, DegenerateFaceWritesFallbackNormals)
{
    auto mesh = MakeCollinearTriangle();

    VertexNormals::Params params;
    params.OutputProperty = "v:test_normal";
    params.FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f};

    const auto result = VertexNormals::Recompute(mesh, params);

    ASSERT_EQ(result.Status, VertexNormals::RecomputeStatus::Success);
    ASSERT_TRUE(result.Normals.IsValid());
    EXPECT_EQ(result.ProcessedFaceCount, 0u);
    EXPECT_EQ(result.DegenerateFaceCount, 1u);
    EXPECT_EQ(result.WrittenCount, 3u);
    EXPECT_EQ(result.FallbackVertexCount, 3u);
    EXPECT_EQ(result.ValidNormalVertexCount, 0u);

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        ExpectVecNear(result.Normals[Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)}],
                      glm::vec3{0.0f, 0.0f, 1.0f});
    }
}

TEST(HalfedgeMeshVertexNormals, InvalidFallbackIsRepairedDeterministically)
{
    auto mesh = MakeCollinearTriangle();

    VertexNormals::Params params;
    params.OutputProperty = "v:test_normal";
    params.FallbackNormal = glm::vec3{0.0f};

    const auto result = VertexNormals::Recompute(mesh, params);

    ASSERT_EQ(result.Status, VertexNormals::RecomputeStatus::Success);
    EXPECT_TRUE(result.FallbackNormalWasRepaired);
    EXPECT_EQ(result.FallbackVertexCount, 3u);
    ExpectVecNear(result.Normals[Geometry::VertexHandle{0u}], glm::vec3{0.0f, 1.0f, 0.0f});
}

TEST(HalfedgeMeshVertexNormals, NonFiniteFaceDoesNotProduceNaNNormals)
{
    auto mesh = MakeAsymmetricTwoTriangleFan();
    mesh.Position(Geometry::VertexHandle{1u}).x = std::numeric_limits<float>::infinity();

    VertexNormals::Params params;
    params.OutputProperty = "v:test_normal";
    params.FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f};

    const auto result = VertexNormals::Recompute(mesh, params);

    ASSERT_EQ(result.Status, VertexNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.NonFiniteFaceCount, 1u);
    EXPECT_EQ(result.ProcessedFaceCount, 1u);
    EXPECT_EQ(result.WrittenCount, 4u);

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const glm::vec3 normal = result.Normals[Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)}];
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
    }
}

TEST(HalfedgeMeshVertexNormals, EmptyMeshReturnsEmptyProperty)
{
    Mesh mesh;

    VertexNormals::Params params;
    params.OutputProperty = "v:test_normal";

    const auto result = VertexNormals::Recompute(mesh, params);

    EXPECT_EQ(result.Status, VertexNormals::RecomputeStatus::EmptyMesh);
    ASSERT_TRUE(result.Normals.IsValid());
    EXPECT_EQ(result.VertexSlotCount, 0u);
    EXPECT_EQ(result.Normals.Vector().size(), 0u);
    EXPECT_EQ(result.WrittenCount, 0u);
}

TEST(HalfedgeMeshVertexNormals, InvalidOutputPropertyFailsClosed)
{
    auto mesh = MakeAsymmetricTwoTriangleFan();

    VertexNormals::Params params;
    params.OutputProperty = "";

    const auto result = VertexNormals::Recompute(mesh, params);

    EXPECT_EQ(result.Status, VertexNormals::RecomputeStatus::InvalidOutputProperty);
    EXPECT_FALSE(result.Normals.IsValid());
}

TEST(HalfedgeMeshVertexNormals, ExistingPropertyWithWrongTypeFailsClosed)
{
    auto mesh = MakeAsymmetricTwoTriangleFan();
    auto wrongType = mesh.VertexProperties().GetOrAdd<float>("v:test_normal", 0.0f);
    ASSERT_TRUE(wrongType.IsValid());

    VertexNormals::Params params;
    params.OutputProperty = "v:test_normal";

    const auto result = VertexNormals::Recompute(mesh, params);

    EXPECT_EQ(result.Status, VertexNormals::RecomputeStatus::PropertyTypeConflict);
    EXPECT_FALSE(result.Normals.IsValid());
    EXPECT_TRUE(mesh.VertexProperties().Get<float>("v:test_normal").IsValid());
}
