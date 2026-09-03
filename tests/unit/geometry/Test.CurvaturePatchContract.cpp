#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Features;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;
import Geometry.HalfedgeMesh.Features;
import Geometry.Properties;

namespace
{
    namespace Features = Geometry::HalfedgeMesh::Features;
    namespace FeatureDetector = Geometry::CurvatureSegmentation;
    using Geometry::EdgeHandle;
    using Geometry::FaceHandle;
    using Geometry::HalfedgeHandle;
    using Geometry::VertexHandle;
    using Geometry::HalfedgeMesh::Mesh;

    enum class BoundaryRole : std::uint8_t
    {
        None = 0u,
        HardFeature,
        SoftFeatureSupported,
        CurvatureClosure,
    };

    enum class EvidenceStatus : std::uint8_t
    {
        Success = 0u,
        EmptyMesh,
        UnsupportedSubmeshView,
        CurvatureCountMismatch,
        HardEvidenceCountMismatch,
        SoftEvidenceCountMismatch,
        InvalidHardEvidence,
        NonFiniteSoftEvidence,
        SoftEvidenceOutOfRange,
        InvalidTopology,
        NonTriangleFace,
        NonFiniteGeometry,
        DegenerateFace,
        NonFiniteCurvature,
        InvalidCurvatureOrder,
    };

    // Slice A freezes this borrowed, slot-aligned shape. The production
    // feature-patch entry point will consume the same two spans; it does not
    // need a feature-network interface or owning adapter.
    struct SuppliedFeatureEvidence
    {
        std::span<const std::uint8_t> HardEdgeMask{};
        std::span<const double> SoftEdgeConfidence{};
    };

    struct OracleFixture
    {
        std::string Id{};
        Mesh Surface{};
        std::vector<double> K1{};
        std::vector<double> K2{};
        std::vector<std::uint8_t> HardEdgeMask{};
        std::vector<double> SoftEdgeConfidence{};
        std::vector<std::uint32_t> ExpectedPatchByFace{};
        std::vector<BoundaryRole> ExpectedBoundaryRole{};
        std::vector<glm::dvec3> ExactFaceNormals{};
    };

    enum class LineAxis : std::uint8_t
    {
        X,
        Y,
    };

    struct LineEvidence
    {
        LineAxis Axis{LineAxis::X};
        double Coordinate{0.0};
        bool Hard{false};
        double SoftConfidence{0.0};
    };

    [[nodiscard]] bool Finite(const glm::vec3 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y)
            && std::isfinite(value.z);
    }

    [[nodiscard]] EvidenceStatus ValidateSuppliedEvidence(
        const Mesh& mesh,
        const std::span<const double> k1,
        const std::span<const double> k2,
        const SuppliedFeatureEvidence evidence)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0u)
            return EvidenceStatus::EmptyMesh;
        if (mesh.IsSubmeshView())
            return EvidenceStatus::UnsupportedSubmeshView;
        if (k1.size() != mesh.VerticesSize()
            || k2.size() != mesh.VerticesSize())
        {
            return EvidenceStatus::CurvatureCountMismatch;
        }
        if (evidence.HardEdgeMask.size() != mesh.EdgesSize())
            return EvidenceStatus::HardEvidenceCountMismatch;
        if (evidence.SoftEdgeConfidence.size() != mesh.EdgesSize())
            return EvidenceStatus::SoftEvidenceCountMismatch;

        for (std::size_t i = 0u; i < k1.size(); ++i)
        {
            if (!std::isfinite(k1[i]) || !std::isfinite(k2[i]))
                return EvidenceStatus::NonFiniteCurvature;
            if (k1[i] < k2[i])
                return EvidenceStatus::InvalidCurvatureOrder;
        }
        for (const VertexHandle vertex : mesh.LiveVertices())
        {
            if (mesh.IsIsolated(vertex) || !mesh.IsManifold(vertex))
                return EvidenceStatus::InvalidTopology;
        }
        for (const EdgeHandle edge : mesh.LiveEdges())
        {
            const HalfedgeHandle h0 = mesh.Halfedge(edge, 0u);
            const HalfedgeHandle h1 = mesh.Halfedge(edge, 1u);
            if (!h0.IsValid() || !h1.IsValid()
                || !mesh.IsValid(h0) || !mesh.IsValid(h1)
                || mesh.Edge(h0) != edge || mesh.Edge(h1) != edge)
            {
                return EvidenceStatus::InvalidTopology;
            }
            const VertexHandle from = mesh.FromVertex(h0);
            const VertexHandle to = mesh.ToVertex(h0);
            if (!from.IsValid() || !to.IsValid()
                || !mesh.IsValid(from) || !mesh.IsValid(to)
                || mesh.IsDeleted(from) || mesh.IsDeleted(to)
                || from == to)
            {
                return EvidenceStatus::InvalidTopology;
            }
            const FaceHandle f0 = mesh.Face(h0);
            const FaceHandle f1 = mesh.Face(h1);
            const auto liveFace = [&](const FaceHandle face)
            {
                return face.IsValid() && mesh.IsValid(face)
                    && !mesh.IsDeleted(face);
            };
            const auto cleanSentinel = [](const FaceHandle face)
            { return !face.IsValid(); };
            if ((!liveFace(f0) && !cleanSentinel(f0))
                || (!liveFace(f1) && !cleanSentinel(f1))
                || (!liveFace(f0) && !liveFace(f1)))
            {
                return EvidenceStatus::InvalidTopology;
            }
        }
        for (const VertexHandle vertex : mesh.LiveVertices())
        {
            if (!Finite(mesh.Position(vertex)))
                return EvidenceStatus::NonFiniteGeometry;
        }
        for (const FaceHandle face : mesh.LiveFaces())
        {
            if (mesh.Valence(face) != 3u)
                return EvidenceStatus::NonTriangleFace;
            std::array<glm::dvec3, 3u> points{};
            std::size_t count = 0u;
            for (const VertexHandle vertex : mesh.VerticesAroundFace(face))
            {
                if (count >= points.size())
                    return EvidenceStatus::NonTriangleFace;
                points[count++] = glm::dvec3(mesh.Position(vertex));
            }
            if (count != points.size())
                return EvidenceStatus::NonTriangleFace;
            const glm::dvec3 normal = glm::cross(
                points[1u] - points[0u], points[2u] - points[0u]);
            const double squaredLength = glm::dot(normal, normal);
            if (!std::isfinite(squaredLength) || !(squaredLength > 1.0e-24))
                return EvidenceStatus::DegenerateFace;
        }
        for (const std::uint8_t hard : evidence.HardEdgeMask)
        {
            if (hard > 1u)
                return EvidenceStatus::InvalidHardEvidence;
        }
        for (const double soft : evidence.SoftEdgeConfidence)
        {
            if (!std::isfinite(soft))
                return EvidenceStatus::NonFiniteSoftEvidence;
            if (soft < 0.0 || soft > 1.0)
                return EvidenceStatus::SoftEvidenceOutOfRange;
        }
        return EvidenceStatus::Success;
    }

    void FinalizeBoundaryOracle(OracleFixture& fixture)
    {
        fixture.ExpectedBoundaryRole.assign(
            fixture.Surface.EdgesSize(), BoundaryRole::None);
        for (const EdgeHandle edge : fixture.Surface.LiveEdges())
        {
            const FaceHandle a = fixture.Surface.Face(
                fixture.Surface.Halfedge(edge, 0u));
            const FaceHandle b = fixture.Surface.Face(
                fixture.Surface.Halfedge(edge, 1u));
            if (!a.IsValid() || !b.IsValid())
                continue;
            if (fixture.ExpectedPatchByFace[a.Index]
                == fixture.ExpectedPatchByFace[b.Index])
            {
                continue;
            }
            if (fixture.HardEdgeMask[edge.Index] != 0u)
            {
                fixture.ExpectedBoundaryRole[edge.Index] =
                    BoundaryRole::HardFeature;
            }
            else if (fixture.SoftEdgeConfidence[edge.Index] > 0.0)
            {
                fixture.ExpectedBoundaryRole[edge.Index] =
                    BoundaryRole::SoftFeatureSupported;
            }
            else
            {
                fixture.ExpectedBoundaryRole[edge.Index] =
                    BoundaryRole::CurvatureClosure;
            }
        }
    }

    template <typename HeightFn, typename CurvatureFn, typename PatchFn>
    [[nodiscard]] OracleFixture MakeGraphGrid(
        std::string id,
        const HeightFn& height,
        const CurvatureFn& curvature,
        const PatchFn& patch,
        const std::initializer_list<LineEvidence> lines = {},
        const bool flippedDiagonals = false,
        const std::uint32_t rows = 8u,
        const std::uint32_t columns = 16u)
    {
        OracleFixture fixture{};
        fixture.Id = std::move(id);
        const std::size_t vertexCount =
            static_cast<std::size_t>(rows + 1u) * (columns + 1u);
        const std::size_t faceCount =
            2u * static_cast<std::size_t>(rows) * columns;
        fixture.Surface.Reserve(vertexCount, 2u * faceCount, faceCount);
        fixture.K1.resize(vertexCount);
        fixture.K2.resize(vertexCount);

        std::vector<VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= rows; ++row)
        {
            const double y = -1.0 + 2.0 * static_cast<double>(row)
                / static_cast<double>(rows);
            for (std::uint32_t column = 0u; column <= columns; ++column)
            {
                const double x = -1.0 + 2.0 * static_cast<double>(column)
                    / static_cast<double>(columns);
                const VertexHandle vertex = fixture.Surface.AddVertex({
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(height(x, y)),
                });
                vertices.push_back(vertex);
                const glm::dvec2 principal = curvature(x, y);
                fixture.K1[vertex.Index] = std::max(principal.x, principal.y);
                fixture.K2[vertex.Index] = std::min(principal.x, principal.y);
            }
        }

        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) * (columns + 1u) + column];
        };
        const auto addFace = [&](const VertexHandle a,
                                 const VertexHandle b,
                                 const VertexHandle c)
        {
            const auto face = fixture.Surface.AddTriangle(a, b, c);
            EXPECT_TRUE(face.has_value()) << fixture.Id;
            if (!face.has_value())
                return;
            if (fixture.ExpectedPatchByFace.size() <= face->Index)
            {
                fixture.ExpectedPatchByFace.resize(
                    static_cast<std::size_t>(face->Index) + 1u, 0u);
            }
            const glm::dvec3 centroid =
                (glm::dvec3(fixture.Surface.Position(a))
                 + glm::dvec3(fixture.Surface.Position(b))
                 + glm::dvec3(fixture.Surface.Position(c))) / 3.0;
            fixture.ExpectedPatchByFace[face->Index] =
                patch(centroid.x, centroid.y);
        };

        for (std::uint32_t row = 0u; row < rows; ++row)
        {
            for (std::uint32_t column = 0u; column < columns; ++column)
            {
                const VertexHandle v00 = vertexAt(row, column);
                const VertexHandle v10 = vertexAt(row, column + 1u);
                const VertexHandle v01 = vertexAt(row + 1u, column);
                const VertexHandle v11 = vertexAt(row + 1u, column + 1u);
                const bool alternate =
                    ((row + column + (flippedDiagonals ? 1u : 0u)) & 1u)
                    != 0u;
                if (alternate)
                {
                    addFace(v00, v10, v01);
                    addFace(v10, v11, v01);
                }
                else
                {
                    addFace(v00, v10, v11);
                    addFace(v00, v11, v01);
                }
            }
        }

        fixture.HardEdgeMask.assign(fixture.Surface.EdgesSize(), 0u);
        fixture.SoftEdgeConfidence.assign(fixture.Surface.EdgesSize(), 0.0);
        for (const LineEvidence& line : lines)
        {
            if (line.Axis == LineAxis::X)
            {
                const auto column = static_cast<std::uint32_t>(std::llround(
                    0.5 * (line.Coordinate + 1.0) * columns));
                EXPECT_LE(column, columns);
                if (column > columns)
                    continue;
                for (std::uint32_t row = 0u; row < rows; ++row)
                {
                    const auto edge = fixture.Surface.FindEdge(
                        vertexAt(row, column), vertexAt(row + 1u, column));
                    EXPECT_TRUE(edge.has_value()) << fixture.Id;
                    if (!edge.has_value())
                        continue;
                    fixture.HardEdgeMask[edge->Index] = line.Hard ? 1u : 0u;
                    fixture.SoftEdgeConfidence[edge->Index] =
                        line.SoftConfidence;
                }
            }
            else
            {
                const auto row = static_cast<std::uint32_t>(std::llround(
                    0.5 * (line.Coordinate + 1.0) * rows));
                EXPECT_LE(row, rows);
                if (row > rows)
                    continue;
                for (std::uint32_t column = 0u; column < columns; ++column)
                {
                    const auto edge = fixture.Surface.FindEdge(
                        vertexAt(row, column), vertexAt(row, column + 1u));
                    EXPECT_TRUE(edge.has_value()) << fixture.Id;
                    if (!edge.has_value())
                        continue;
                    fixture.HardEdgeMask[edge->Index] = line.Hard ? 1u : 0u;
                    fixture.SoftEdgeConfidence[edge->Index] =
                        line.SoftConfidence;
                }
            }
        }
        FinalizeBoundaryOracle(fixture);
        return fixture;
    }

    [[nodiscard]] OracleFixture MakeFold(
        const double degrees,
        const bool flippedDiagonals)
    {
        constexpr std::uint32_t kRows = 8u;
        constexpr std::uint32_t kColumns = 16u;
        const double radians = degrees * std::numbers::pi_v<double> / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        OracleFixture fixture{};
        fixture.Id = "strict_fold_" + std::to_string(
            static_cast<int>(degrees))
            + (flippedDiagonals ? "_phase_b" : "_phase_a");
        const std::size_t vertexCount =
            static_cast<std::size_t>(kRows + 1u) * (kColumns + 1u);
        fixture.Surface.Reserve(
            vertexCount, 4u * kRows * kColumns, 2u * kRows * kColumns);
        fixture.K1.assign(vertexCount, 1.0);
        fixture.K2.assign(vertexCount, 0.0);
        std::vector<VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= kRows; ++row)
        {
            const double y = static_cast<double>(row) / kRows;
            for (std::uint32_t column = 0u; column <= kColumns; ++column)
            {
                const double u = 2.0 * static_cast<double>(column)
                    / kColumns - 1.0;
                const bool right = u > 0.0;
                vertices.push_back(fixture.Surface.AddVertex({
                    static_cast<float>(right ? u * cosine : u),
                    static_cast<float>(y),
                    static_cast<float>(right ? u * sine : 0.0),
                }));
            }
        }
        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) * (kColumns + 1u) + column];
        };
        const auto addFace = [&](const VertexHandle a,
                                 const VertexHandle b,
                                 const VertexHandle c,
                                 const bool right)
        {
            const auto face = fixture.Surface.AddTriangle(a, b, c);
            EXPECT_TRUE(face.has_value()) << fixture.Id;
            if (!face.has_value())
                return;
            if (fixture.ExpectedPatchByFace.size() <= face->Index)
            {
                fixture.ExpectedPatchByFace.resize(
                    static_cast<std::size_t>(face->Index) + 1u, 0u);
                fixture.ExactFaceNormals.resize(
                    static_cast<std::size_t>(face->Index) + 1u);
            }
            fixture.ExpectedPatchByFace[face->Index] =
                degrees > 45.0 && right ? 1u : 0u;
            fixture.ExactFaceNormals[face->Index] = right
                ? glm::dvec3{-sine, 0.0, cosine}
                : glm::dvec3{0.0, 0.0, 1.0};
        };
        const std::uint32_t creaseColumn = kColumns / 2u;
        for (std::uint32_t row = 0u; row < kRows; ++row)
        {
            for (std::uint32_t column = 0u; column < kColumns; ++column)
            {
                const VertexHandle v00 = vertexAt(row, column);
                const VertexHandle v10 = vertexAt(row, column + 1u);
                const VertexHandle v01 = vertexAt(row + 1u, column);
                const VertexHandle v11 = vertexAt(row + 1u, column + 1u);
                const bool right = column >= creaseColumn;
                const bool alternate =
                    ((row + column + (flippedDiagonals ? 1u : 0u)) & 1u)
                    != 0u;
                if (alternate)
                {
                    addFace(v00, v10, v01, right);
                    addFace(v10, v11, v01, right);
                }
                else
                {
                    addFace(v00, v10, v11, right);
                    addFace(v00, v11, v01, right);
                }
            }
        }
        fixture.HardEdgeMask.assign(fixture.Surface.EdgesSize(), 0u);
        fixture.SoftEdgeConfidence.assign(fixture.Surface.EdgesSize(), 0.0);
        if (degrees > 45.0)
        {
            for (std::uint32_t row = 0u; row < kRows; ++row)
            {
                const auto edge = fixture.Surface.FindEdge(
                    vertexAt(row, creaseColumn),
                    vertexAt(row + 1u, creaseColumn));
                EXPECT_TRUE(edge.has_value()) << fixture.Id;
                if (edge.has_value())
                    fixture.HardEdgeMask[edge->Index] = 1u;
            }
        }
        FinalizeBoundaryOracle(fixture);
        return fixture;
    }

    [[nodiscard]] OracleFixture MakeCylinder()
    {
        constexpr std::uint32_t kRows = 8u;
        constexpr std::uint32_t kColumns = 24u;
        OracleFixture fixture{};
        fixture.Id = "open_cylinder";
        const std::size_t vertexCount =
            static_cast<std::size_t>(kRows + 1u) * kColumns;
        fixture.Surface.Reserve(
            vertexCount, 4u * kRows * kColumns, 2u * kRows * kColumns);
        fixture.K1.assign(vertexCount, 1.0);
        fixture.K2.assign(vertexCount, 0.0);
        std::vector<VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= kRows; ++row)
        {
            const double y = 2.0 * static_cast<double>(row) / kRows;
            for (std::uint32_t column = 0u; column < kColumns; ++column)
            {
                const double angle = 2.0 * std::numbers::pi_v<double>
                    * static_cast<double>(column) / kColumns;
                vertices.push_back(fixture.Surface.AddVertex({
                    static_cast<float>(std::cos(angle)),
                    static_cast<float>(y),
                    static_cast<float>(-std::sin(angle)),
                }));
            }
        }
        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) * kColumns
                + (column % kColumns)];
        };
        for (std::uint32_t row = 0u; row < kRows; ++row)
        {
            for (std::uint32_t column = 0u; column < kColumns; ++column)
            {
                const VertexHandle v00 = vertexAt(row, column);
                const VertexHandle v10 = vertexAt(row, column + 1u);
                const VertexHandle v01 = vertexAt(row + 1u, column);
                const VertexHandle v11 = vertexAt(row + 1u, column + 1u);
                EXPECT_TRUE(fixture.Surface.AddTriangle(v00, v10, v11));
                EXPECT_TRUE(fixture.Surface.AddTriangle(v00, v11, v01));
            }
        }
        fixture.ExpectedPatchByFace.assign(fixture.Surface.FacesSize(), 0u);
        fixture.HardEdgeMask.assign(fixture.Surface.EdgesSize(), 0u);
        fixture.SoftEdgeConfidence.assign(fixture.Surface.EdgesSize(), 0.0);
        FinalizeBoundaryOracle(fixture);
        return fixture;
    }

    [[nodiscard]] OracleFixture MakeSphere()
    {
        OracleFixture fixture{};
        fixture.Id = "unit_sphere";
        fixture.Surface = Geometry::HalfedgeMesh::MakeMeshIcosahedron();
        fixture.K1.assign(fixture.Surface.VerticesSize(), 1.0);
        fixture.K2.assign(fixture.Surface.VerticesSize(), 1.0);
        fixture.ExpectedPatchByFace.assign(fixture.Surface.FacesSize(), 0u);
        fixture.HardEdgeMask.assign(fixture.Surface.EdgesSize(), 0u);
        fixture.SoftEdgeConfidence.assign(fixture.Surface.EdgesSize(), 0.0);
        FinalizeBoundaryOracle(fixture);
        return fixture;
    }

    [[nodiscard]] OracleFixture MakeDisconnected()
    {
        OracleFixture fixture{};
        fixture.Id = "disconnected_surfaces";
        for (std::uint32_t component = 0u; component < 2u; ++component)
        {
            const float x = 3.0f * static_cast<float>(component);
            const VertexHandle a = fixture.Surface.AddVertex({x, 0.0f, 0.0f});
            const VertexHandle b = fixture.Surface.AddVertex({x + 1.0f, 0.0f, 0.0f});
            const VertexHandle c = fixture.Surface.AddVertex({x, 1.0f, 0.0f});
            const auto face = fixture.Surface.AddTriangle(a, b, c);
            EXPECT_TRUE(face.has_value());
            if (face.has_value())
            {
                fixture.ExpectedPatchByFace.resize(face->Index + 1u);
                fixture.ExpectedPatchByFace[face->Index] = component;
            }
        }
        fixture.K1.assign(fixture.Surface.VerticesSize(), 0.0);
        fixture.K2.assign(fixture.Surface.VerticesSize(), 0.0);
        fixture.HardEdgeMask.assign(fixture.Surface.EdgesSize(), 0u);
        fixture.SoftEdgeConfidence.assign(fixture.Surface.EdgesSize(), 0.0);
        FinalizeBoundaryOracle(fixture);
        return fixture;
    }

    [[nodiscard]] std::vector<OracleFixture> MakeOracleCatalog()
    {
        const auto zeroHeight = [](double, double) { return 0.0; };
        const auto zeroCurvature = [](double, double)
        { return glm::dvec2{0.0}; };
        const auto onePatch = [](double, double) { return 0u; };
        const auto splitX = [](const double x, double)
        { return x >= 0.0 ? 1u : 0u; };
        constexpr double width = 0.20;
        const auto smoothHeight = [](const double x, double)
        { return 0.5 * (1.0 + std::tanh(x / width)); };
        const auto smoothCurvature = [](const double x, double)
        {
            const double tangent = std::tanh(x / width);
            const double sechSquared = 1.0 - tangent * tangent;
            const double first = 0.5 * sechSquared / width;
            const double second = -sechSquared * tangent / (width * width);
            const double k = second / std::pow(1.0 + first * first, 1.5);
            return glm::dvec2{std::max(k, 0.0), std::min(k, 0.0)};
        };
        const auto gaussianHeight = [](const double x, double)
        { return std::exp(-(x * x) / (width * width)); };
        const auto gaussianCurvature = [](const double x, double)
        {
            const double q = std::exp(-(x * x) / (width * width));
            const double first = -2.0 * x * q / (width * width);
            const double second = q * (4.0 * x * x / std::pow(width, 4.0)
                - 2.0 / (width * width));
            const double k = second / std::pow(1.0 + first * first, 1.5);
            return glm::dvec2{std::max(k, 0.0), std::min(k, 0.0)};
        };

        std::vector<OracleFixture> catalog;
        catalog.reserve(17u);
        catalog.push_back(MakeGraphGrid(
            "plane", zeroHeight, zeroCurvature, onePatch));
        catalog.push_back(MakeCylinder());
        catalog.push_back(MakeSphere());
        for (const double angle : {30.0, 45.0, 60.0})
        {
            catalog.push_back(MakeFold(angle, false));
            catalog.push_back(MakeFold(angle, true));
        }
        catalog.push_back(MakeGraphGrid(
            "smooth_signed_transition",
            smoothHeight,
            smoothCurvature,
            splitX,
            {{LineAxis::X, 0.0, false, 1.0}}));
        catalog.push_back(MakeGraphGrid(
            "ridge",
            gaussianHeight,
            gaussianCurvature,
            splitX,
            {{LineAxis::X, 0.0, false, 0.9}}));
        catalog.push_back(MakeGraphGrid(
            "valley",
            [&](const double x, const double y)
            { return -gaussianHeight(x, y); },
            [&](const double x, const double y)
            {
                const glm::dvec2 k = gaussianCurvature(x, y);
                return glm::dvec2{-k.y, -k.x};
            },
            splitX,
            {{LineAxis::X, 0.0, false, 0.9}}));
        catalog.push_back(MakeGraphGrid(
            "nearby_feature_pair",
            zeroHeight,
            [](const double x, double)
            {
                return glm::dvec2{
                    std::tanh((x + 0.25) / width)
                        - std::tanh((x - 0.25) / width),
                    0.0,
                };
            },
            [](const double x, double)
            {
                if (x < -0.25)
                    return 0u;
                return x < 0.25 ? 1u : 2u;
            },
            {{LineAxis::X, -0.25, false, 0.9},
             {LineAxis::X, 0.25, false, 0.9}}));
        catalog.push_back(MakeGraphGrid(
            "feature_junction",
            zeroHeight,
            [](const double x, const double y)
            {
                return glm::dvec2{
                    std::tanh(x / width), std::tanh(y / width)};
            },
            [](const double x, const double y)
            {
                return static_cast<std::uint32_t>(x >= 0.0)
                    + 2u * static_cast<std::uint32_t>(y >= 0.0);
            },
            {{LineAxis::X, 0.0, false, 1.0},
             {LineAxis::Y, 0.0, false, 1.0}}));
        catalog.push_back(MakeGraphGrid(
            "open_boundary_termination",
            zeroHeight,
            [](const double x, double)
            { return glm::dvec2{x >= 0.0 ? 1.0 : -1.0, 0.0}; },
            splitX,
            {{LineAxis::X, 0.0, false, 0.8}}));
        catalog.push_back(MakeDisconnected());
        catalog.push_back(MakeGraphGrid(
            "same_curvature_false_boundary",
            zeroHeight,
            zeroCurvature,
            onePatch,
            {{LineAxis::X, 0.0, false, 0.55}}));
        return catalog;
    }

    [[nodiscard]] bool PatchesAreConnected(const OracleFixture& fixture)
    {
        if (fixture.ExpectedPatchByFace.size() != fixture.Surface.FacesSize())
            return false;
        const std::uint32_t maxPatch = *std::max_element(
            fixture.ExpectedPatchByFace.begin(),
            fixture.ExpectedPatchByFace.end());
        std::vector<std::vector<std::uint32_t>> adjacency(
            fixture.Surface.FacesSize());
        for (const EdgeHandle edge : fixture.Surface.LiveEdges())
        {
            const FaceHandle a = fixture.Surface.Face(
                fixture.Surface.Halfedge(edge, 0u));
            const FaceHandle b = fixture.Surface.Face(
                fixture.Surface.Halfedge(edge, 1u));
            if (!a.IsValid() || !b.IsValid())
                continue;
            adjacency[a.Index].push_back(b.Index);
            adjacency[b.Index].push_back(a.Index);
        }
        for (std::uint32_t patch = 0u; patch <= maxPatch; ++patch)
        {
            const auto seedIt = std::find(
                fixture.ExpectedPatchByFace.begin(),
                fixture.ExpectedPatchByFace.end(),
                patch);
            if (seedIt == fixture.ExpectedPatchByFace.end())
                return false;
            const std::uint32_t seed = static_cast<std::uint32_t>(
                std::distance(fixture.ExpectedPatchByFace.begin(), seedIt));
            std::vector<std::uint8_t> visited(
                fixture.Surface.FacesSize(), 0u);
            std::vector<std::uint32_t> stack{seed};
            visited[seed] = 1u;
            while (!stack.empty())
            {
                const std::uint32_t face = stack.back();
                stack.pop_back();
                for (const std::uint32_t neighbor : adjacency[face])
                {
                    if (visited[neighbor] != 0u
                        || fixture.ExpectedPatchByFace[neighbor] != patch)
                    {
                        continue;
                    }
                    visited[neighbor] = 1u;
                    stack.push_back(neighbor);
                }
            }
            for (std::size_t face = 0u;
                 face < fixture.ExpectedPatchByFace.size();
                 ++face)
            {
                if (fixture.ExpectedPatchByFace[face] == patch
                    && visited[face] == 0u)
                {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool BoundaryTopologyIsValid(const OracleFixture& fixture)
    {
        std::vector<std::size_t> degree(fixture.Surface.VerticesSize(), 0u);
        for (const EdgeHandle edge : fixture.Surface.LiveEdges())
        {
            if (fixture.ExpectedBoundaryRole[edge.Index]
                == BoundaryRole::None)
            {
                continue;
            }
            if (fixture.Surface.IsBoundary(edge))
                return false;
            const HalfedgeHandle halfedge = fixture.Surface.Halfedge(edge, 0u);
            ++degree[fixture.Surface.FromVertex(halfedge).Index];
            ++degree[fixture.Surface.ToVertex(halfedge).Index];
        }
        for (std::size_t vertex = 0u; vertex < degree.size(); ++vertex)
        {
            if (degree[vertex] == 1u
                && !fixture.Surface.IsBoundary(VertexHandle{
                    static_cast<Geometry::PropertyIndex>(vertex)}))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<OracleFixture> MakeDetectorControlCatalog(
        const bool flippedDiagonals)
    {
        constexpr std::uint32_t rows = 24u;
        constexpr std::uint32_t columns = 48u;
        constexpr double smoothWidth = 0.08;
        constexpr double ridgeWidth = 0.20;
        const std::string phase = flippedDiagonals ? "_phase_b" : "_phase_a";
        const auto splitX = [](const double x, double)
        { return x >= 0.0 ? 1u : 0u; };
        const auto smoothHeight = [](const double x, double)
        { return 0.5 * (1.0 + std::tanh(x / smoothWidth)); };
        const auto smoothCurvature = [](const double x, double)
        {
            const double tangent = std::tanh(x / smoothWidth);
            const double sechSquared = 1.0 - tangent * tangent;
            const double first = 0.5 * sechSquared / smoothWidth;
            const double second =
                -sechSquared * tangent / (smoothWidth * smoothWidth);
            const double k = second / std::pow(1.0 + first * first, 1.5);
            return glm::dvec2{std::max(k, 0.0), std::min(k, 0.0)};
        };
        const auto ridgeHeight = [](const double x, double)
        { return std::exp(-(x * x) / (ridgeWidth * ridgeWidth)); };
        const auto ridgeCurvature = [](const double x, double)
        {
            const double q = std::exp(-(x * x) / (ridgeWidth * ridgeWidth));
            const double first = -2.0 * x * q / (ridgeWidth * ridgeWidth);
            const double second = q * (
                4.0 * x * x / std::pow(ridgeWidth, 4.0)
                - 2.0 / (ridgeWidth * ridgeWidth));
            const double k = second / std::pow(1.0 + first * first, 1.5);
            return glm::dvec2{std::max(k, 0.0), std::min(k, 0.0)};
        };

        std::vector<OracleFixture> fixtures;
        fixtures.reserve(6u);
        fixtures.push_back(MakeGraphGrid(
            "detector_smooth_transition" + phase,
            smoothHeight,
            smoothCurvature,
            splitX,
            {{LineAxis::X, 0.0, false, 1.0}},
            flippedDiagonals,
            rows,
            columns));
        fixtures.push_back(MakeGraphGrid(
            "detector_ridge" + phase,
            ridgeHeight,
            ridgeCurvature,
            splitX,
            {{LineAxis::X, 0.0, false, 0.9}},
            flippedDiagonals,
            rows,
            columns));
        fixtures.push_back(MakeGraphGrid(
            "detector_valley" + phase,
            [&](const double x, const double y)
            { return -ridgeHeight(x, y); },
            [&](const double x, const double y)
            {
                const glm::dvec2 k = ridgeCurvature(x, y);
                return glm::dvec2{-k.y, -k.x};
            },
            splitX,
            {{LineAxis::X, 0.0, false, 0.9}},
            flippedDiagonals,
            rows,
            columns));
        fixtures.push_back(MakeGraphGrid(
            "detector_nearby_pair" + phase,
            [](double, double) { return 0.0; },
            [](const double x, double)
            {
                if (x > -0.25 && x < 0.25)
                    return glm::dvec2{1.0, 0.0};
                if (x == -0.25 || x == 0.25)
                    return glm::dvec2{0.0};
                return glm::dvec2{0.0, -1.0};
            },
            [](const double x, double)
            {
                if (x < -0.25)
                    return 0u;
                return x < 0.25 ? 1u : 2u;
            },
            {{LineAxis::X, -0.25, false, 0.9},
             {LineAxis::X, 0.25, false, 0.9}},
            flippedDiagonals,
            rows,
            columns));
        fixtures.push_back(MakeGraphGrid(
            "detector_junction" + phase,
            [](double, double) { return 0.0; },
            [](const double x, const double y)
            {
                if (x == 0.0 || y == 0.0)
                    return glm::dvec2{0.0};
                if (x >= 0.0 && y >= 0.0)
                    return glm::dvec2{1.0, 1.0};
                if (x < 0.0 && y < 0.0)
                    return glm::dvec2{-1.0, -1.0};
                return glm::dvec2{1.0, -1.0};
            },
            [](const double x, const double y)
            {
                return static_cast<std::uint32_t>(x >= 0.0)
                    + 2u * static_cast<std::uint32_t>(y >= 0.0);
            },
            {{LineAxis::X, 0.0, false, 1.0},
             {LineAxis::Y, 0.0, false, 1.0}},
            flippedDiagonals,
            rows,
            columns));
        fixtures.push_back(MakeGraphGrid(
            "detector_open_termination" + phase,
            [](double, double) { return 0.0; },
            [](const double x, double)
            {
                if (x == 0.0)
                    return glm::dvec2{0.0};
                return glm::dvec2{x > 0.0 ? 1.0 : -1.0, 0.0};
            },
            splitX,
            {{LineAxis::X, 0.0, false, 0.8}},
            flippedDiagonals,
            rows,
            columns));
        return fixtures;
    }

    [[nodiscard]] FeatureDetector::FeatureEvidenceParams DetectorParamsFor(
        const OracleFixture& fixture)
    {
        FeatureDetector::FeatureEvidenceParams params{};
        if (fixture.Id.starts_with("detector_ridge")
            || fixture.Id.starts_with("detector_valley"))
        {
            // These tests isolate the smooth R/V path. On the deliberately
            // steep graph itself, the default 45-degree classifier correctly
            // owns the center line as hard evidence.
            params.HardDihedralThresholdDegrees = 180.0;
        }
        return params;
    }

    [[nodiscard]] std::vector<glm::dvec3> FaceNormals(
        const Mesh& mesh)
    {
        std::vector<glm::dvec3> normals(
            mesh.FacesSize(), glm::dvec3{0.0});
        for (const FaceHandle face : mesh.LiveFaces())
        {
            std::array<glm::dvec3, 3u> points{};
            std::size_t index = 0u;
            for (const VertexHandle vertex : mesh.VerticesAroundFace(face))
                points[index++] = glm::dvec3(mesh.Position(vertex));
            normals[face.Index] = glm::normalize(glm::cross(
                points[1u] - points[0u], points[2u] - points[0u]));
        }
        return normals;
    }

    [[nodiscard]] std::size_t ThreeBitCount(const std::uint8_t mask) noexcept
    {
        return static_cast<std::size_t>(mask & 1u)
            + static_cast<std::size_t>((mask >> 1u) & 1u)
            + static_cast<std::size_t>((mask >> 2u) & 1u);
    }

    [[nodiscard]] FeatureDetector::CurvaturePatchParams FixedPatchParams(
        const std::uint32_t componentCount = 1u)
    {
        FeatureDetector::CurvaturePatchParams params{};
        params.Mixture.SelectionMode =
            FeatureDetector::ComponentSelectionMode::FixedCount;
        params.Mixture.FixedComponentCount = componentCount;
        return params;
    }

    [[nodiscard]] bool EquivalentPartitions(
        const Mesh& mesh,
        const std::span<const std::uint32_t> expected,
        const std::span<const std::uint32_t> actual)
    {
        if (expected.size() != mesh.FacesSize()
            || actual.size() != mesh.FacesSize())
        {
            return false;
        }
        for (const FaceHandle a : mesh.LiveFaces())
        {
            if (actual[a.Index] == FeatureDetector::kInvalidPatchIndex)
                return false;
            for (const FaceHandle b : mesh.LiveFaces())
            {
                if ((expected[a.Index] == expected[b.Index])
                    != (actual[a.Index] == actual[b.Index]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] double FaceArea(
        const Mesh& mesh,
        const FaceHandle face)
    {
        std::array<glm::dvec3, 3u> positions{};
        std::size_t count = 0u;
        for (const VertexHandle vertex : mesh.VerticesAroundFace(face))
            positions[count++] = glm::dvec3(mesh.Position(vertex));
        return 0.5 * glm::length(glm::cross(
            positions[1u] - positions[0u],
            positions[2u] - positions[0u]));
    }

    [[nodiscard]] double AreaWeightedVariationOfInformation(
        const Mesh& mesh,
        const std::span<const std::uint32_t> a,
        const std::span<const std::uint32_t> b)
    {
        std::map<std::uint32_t, double> massA;
        std::map<std::uint32_t, double> massB;
        std::map<std::pair<std::uint32_t, std::uint32_t>, double> joint;
        double total = 0.0;
        for (const FaceHandle face : mesh.LiveFaces())
        {
            const double area = FaceArea(mesh, face);
            total += area;
            massA[a[face.Index]] += area;
            massB[b[face.Index]] += area;
            joint[{a[face.Index], b[face.Index]}] += area;
        }
        const auto entropy = [total](const auto& masses)
        {
            double value = 0.0;
            for (const auto& [label, mass] : masses)
            {
                static_cast<void>(label);
                const double probability = mass / total;
                value -= probability * std::log(probability);
            }
            return value;
        };
        double mutualInformation = 0.0;
        for (const auto& [labels, mass] : joint)
        {
            const double probability = mass / total;
            mutualInformation += probability * std::log(
                probability
                / ((massA[labels.first] / total)
                   * (massB[labels.second] / total)));
        }
        return std::max(
            entropy(massA) + entropy(massB)
                - 2.0 * mutualInformation,
            0.0);
    }

    [[nodiscard]] double BoundingDiagonal(const Mesh& mesh)
    {
        const double infinity = std::numeric_limits<double>::infinity();
        glm::dvec3 lower{infinity};
        glm::dvec3 upper{-infinity};
        for (const VertexHandle vertex : mesh.LiveVertices())
        {
            const glm::dvec3 position{mesh.Position(vertex)};
            lower = glm::min(lower, position);
            upper = glm::max(upper, position);
        }
        return glm::length(upper - lower);
    }

    [[nodiscard]] std::vector<glm::dvec3> BoundaryMidpoints(
        const Mesh& mesh,
        const std::span<const std::uint8_t> boundaryMask)
    {
        std::vector<glm::dvec3> points;
        for (const EdgeHandle edge : mesh.LiveEdges())
        {
            if (boundaryMask[edge.Index] == 0u)
                continue;
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            points.push_back(0.5 * (
                glm::dvec3(mesh.Position(mesh.FromVertex(halfedge)))
                + glm::dvec3(mesh.Position(mesh.ToVertex(halfedge)))));
        }
        return points;
    }

    [[nodiscard]] double SymmetricBoundaryDistance(
        const Mesh& meshA,
        const std::span<const std::uint8_t> boundaryA,
        const Mesh& meshB,
        const std::span<const std::uint8_t> boundaryB)
    {
        const std::vector<glm::dvec3> a =
            BoundaryMidpoints(meshA, boundaryA);
        const std::vector<glm::dvec3> b =
            BoundaryMidpoints(meshB, boundaryB);
        if (a.empty() || b.empty())
        {
            return a.empty() && b.empty()
                ? 0.0
                : std::numeric_limits<double>::infinity();
        }
        const auto directed = [](const auto& from, const auto& to)
        {
            double maximum = 0.0;
            for (const glm::dvec3 point : from)
            {
                double nearest = std::numeric_limits<double>::infinity();
                for (const glm::dvec3 candidate : to)
                {
                    nearest = std::min(
                        nearest, glm::length(point - candidate));
                }
                maximum = std::max(maximum, nearest);
            }
            return maximum;
        };
        return std::max(directed(a, b), directed(b, a));
    }

    [[nodiscard]] FeatureDetector::PatchBoundaryRole ExpectedPatchRole(
        const BoundaryRole role) noexcept
    {
        switch (role)
        {
        case BoundaryRole::None:
            return FeatureDetector::PatchBoundaryRole::None;
        case BoundaryRole::HardFeature:
            return FeatureDetector::PatchBoundaryRole::HardFeature;
        case BoundaryRole::SoftFeatureSupported:
            return FeatureDetector::PatchBoundaryRole::SoftFeatureSupported;
        case BoundaryRole::CurvatureClosure:
            return FeatureDetector::PatchBoundaryRole::CurvatureClosure;
        }
        return FeatureDetector::PatchBoundaryRole::None;
    }

    [[nodiscard]] std::vector<std::uint32_t>
    FacesAdjacentToExpectedBoundary(const OracleFixture& fixture)
    {
        std::set<std::uint32_t> adjacent;
        for (const EdgeHandle edge : fixture.Surface.LiveEdges())
        {
            const FaceHandle a = fixture.Surface.Face(
                fixture.Surface.Halfedge(edge, 0u));
            const FaceHandle b = fixture.Surface.Face(
                fixture.Surface.Halfedge(edge, 1u));
            if (!a.IsValid() || !b.IsValid()
                || fixture.ExpectedPatchByFace[a.Index]
                    == fixture.ExpectedPatchByFace[b.Index])
            {
                continue;
            }
            adjacent.insert(a.Index);
            adjacent.insert(b.Index);
        }
        return {adjacent.begin(), adjacent.end()};
    }

    [[nodiscard]] std::vector<std::uint32_t> PerturbSeedsOneDualStep(
        const Mesh& mesh,
        const std::span<const std::uint32_t> seeds)
    {
        std::set<std::uint32_t> perturbed;
        for (const std::uint32_t seedSlot : seeds)
        {
            const FaceHandle seed{seedSlot};
            std::optional<std::uint32_t> neighborSlot;
            for (const EdgeHandle edge : mesh.LiveEdges())
            {
                const FaceHandle a = mesh.Face(mesh.Halfedge(edge, 0u));
                const FaceHandle b = mesh.Face(mesh.Halfedge(edge, 1u));
                if (!a.IsValid() || !b.IsValid())
                    continue;
                if (a == seed)
                    neighborSlot = b.Index;
                else if (b == seed)
                    neighborSlot = a.Index;
                else
                    continue;
                break;
            }
            perturbed.insert(neighborSlot.value_or(seedSlot));
        }
        return {perturbed.begin(), perturbed.end()};
    }
}

TEST(CurvaturePatchContract,
     GeneratedOracleCatalogHasValidEvidenceAndPartitionTopology)
{
    std::vector<OracleFixture> fixtures = MakeOracleCatalog();
    ASSERT_EQ(fixtures.size(), 17u);
    for (const OracleFixture& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.Id);
        EXPECT_EQ(
            ValidateSuppliedEvidence(
                fixture.Surface,
                fixture.K1,
                fixture.K2,
                {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
            EvidenceStatus::Success);
        EXPECT_TRUE(PatchesAreConnected(fixture));
        EXPECT_TRUE(BoundaryTopologyIsValid(fixture));
        ASSERT_EQ(
            fixture.ExpectedBoundaryRole.size(),
            fixture.Surface.EdgesSize());
        for (const EdgeHandle edge : fixture.Surface.LiveEdges())
        {
            const BoundaryRole role = fixture.ExpectedBoundaryRole[edge.Index];
            if (role == BoundaryRole::HardFeature)
                EXPECT_EQ(fixture.HardEdgeMask[edge.Index], 1u);
            if (role == BoundaryRole::SoftFeatureSupported)
                EXPECT_GT(fixture.SoftEdgeConfidence[edge.Index], 0.0);
        }
    }
}

TEST(CurvaturePatchContract,
     PatchReferenceMatchesSuppliedFeatureOracleCatalog)
{
    std::vector<OracleFixture> fixtures = MakeOracleCatalog();
    for (OracleFixture& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.Id);
        const FeatureDetector::CurvaturePatchResult result =
            FeatureDetector::SegmentFeatureAlignedPatches(
                fixture.Surface,
                fixture.K1,
                fixture.K2,
                {fixture.HardEdgeMask, fixture.SoftEdgeConfidence},
                FixedPatchParams());
        ASSERT_TRUE(result.Succeeded())
            << FeatureDetector::ToString(result.Diagnostics.Status);
        EXPECT_TRUE(EquivalentPartitions(
            fixture.Surface,
            fixture.ExpectedPatchByFace,
            result.FaceRegions));
        ASSERT_EQ(
            result.EdgeBoundaryRoles.size(),
            fixture.ExpectedBoundaryRole.size());
        for (const EdgeHandle edge : fixture.Surface.LiveEdges())
        {
            EXPECT_EQ(
                result.EdgeBoundaryRoles[edge.Index],
                ExpectedPatchRole(
                    fixture.ExpectedBoundaryRole[edge.Index]))
                << "edge " << edge.Index;
        }
        EXPECT_EQ(result.Diagnostics.FinalNegativeMergeCount, 0u);
        EXPECT_EQ(
            result.Diagnostics.FinalRegionCount,
            *std::max_element(
                fixture.ExpectedPatchByFace.begin(),
                fixture.ExpectedPatchByFace.end()) + 1u);
        ASSERT_FALSE(result.AcceptedEnergyHistory.empty());
        for (std::size_t i = 1u;
             i < result.AcceptedEnergyHistory.size();
             ++i)
        {
            EXPECT_LT(
                result.AcceptedEnergyHistory[i],
                result.AcceptedEnergyHistory[i - 1u]);
        }
    }
}

TEST(CurvaturePatchContract,
     PatchGrowthIsDeterministicAndSeedFrontsHaveNoAuthority)
{
    OracleFixture plane = MakeGraphGrid(
        "patch_growth_plane",
        [](double, double) { return 0.0; },
        [](double, double) { return glm::dvec2{0.0}; },
        [](double, double) { return 0u; });
    std::vector<std::uint32_t> suppliedSeeds;
    for (const FaceHandle face : plane.Surface.LiveFaces())
    {
        if ((face.Index % 23u) == 0u)
            suppliedSeeds.push_back(face.Index);
    }
    ASSERT_GT(suppliedSeeds.size(), 2u);
    const FeatureDetector::PatchSeedOverrides overrides{
        suppliedSeeds, true};
    const FeatureDetector::CurvaturePatchResult first =
        FeatureDetector::SegmentFeatureAlignedPatches(
            plane.Surface,
            plane.K1,
            plane.K2,
            {plane.HardEdgeMask, plane.SoftEdgeConfidence},
            FixedPatchParams(),
            overrides);
    const FeatureDetector::CurvaturePatchResult second =
        FeatureDetector::SegmentFeatureAlignedPatches(
            plane.Surface,
            plane.K1,
            plane.K2,
            {plane.HardEdgeMask, plane.SoftEdgeConfidence},
            FixedPatchParams(),
            overrides);
    ASSERT_TRUE(first.Succeeded())
        << FeatureDetector::ToString(first.Diagnostics.Status);
    ASSERT_TRUE(second.Succeeded());
    EXPECT_EQ(first.SeedFaceSlots, suppliedSeeds);
    EXPECT_EQ(first.SeedFaceSlots, second.SeedFaceSlots);
    EXPECT_EQ(
        first.ProvisionalFaceRegions,
        second.ProvisionalFaceRegions);
    EXPECT_EQ(first.FaceGrowthCosts, second.FaceGrowthCosts);
    EXPECT_EQ(first.EdgeGrowthFlags, second.EdgeGrowthFlags);
    EXPECT_EQ(first.FaceRegions, second.FaceRegions);
    EXPECT_EQ(first.EdgeBoundaryRoles, second.EdgeBoundaryRoles);
    EXPECT_EQ(
        first.Diagnostics.ProvisionalRegionCount,
        suppliedSeeds.size());
    EXPECT_GT(first.Diagnostics.ProvisionalBoundaryEdgeCount, 0u);
    EXPECT_EQ(first.Diagnostics.FinalRegionCount, 1u);
    EXPECT_EQ(first.Diagnostics.FinalBoundaryEdgeCount, 0u);
    EXPECT_EQ(
        first.AcceptedMerges.size(), suppliedSeeds.size() - 1u);
    EXPECT_TRUE(std::ranges::all_of(
        first.FaceRegions,
        [](const std::uint32_t region) { return region == 0u; }));
}

TEST(CurvaturePatchContract,
     PatchHardBarriersRemainDistinctFromGrowthThroughPublication)
{
    OracleFixture fold = MakeFold(60.0, false);
    const FeatureDetector::CurvaturePatchResult result =
        FeatureDetector::SegmentFeatureAlignedPatches(
            fold.Surface,
            fold.K1,
            fold.K2,
            {fold.HardEdgeMask, fold.SoftEdgeConfidence},
            FixedPatchParams());
    ASSERT_TRUE(result.Succeeded())
        << FeatureDetector::ToString(result.Diagnostics.Status);
    EXPECT_EQ(result.Diagnostics.HardBlockedTransitionCount, 8u);
    EXPECT_EQ(result.Diagnostics.HardBoundaryEdgeCount, 8u);
    EXPECT_EQ(result.Diagnostics.FinalRegionCount, 2u);
    for (const EdgeHandle edge : fold.Surface.LiveEdges())
    {
        if (fold.HardEdgeMask[edge.Index] == 0u)
            continue;
        const FaceHandle a = fold.Surface.Face(
            fold.Surface.Halfedge(edge, 0u));
        const FaceHandle b = fold.Surface.Face(
            fold.Surface.Halfedge(edge, 1u));
        ASSERT_TRUE(a.IsValid());
        ASSERT_TRUE(b.IsValid());
        EXPECT_NE(
            result.ProvisionalFaceRegions[a.Index],
            result.ProvisionalFaceRegions[b.Index]);
        EXPECT_NE(result.FaceRegions[a.Index], result.FaceRegions[b.Index]);
        EXPECT_EQ(
            result.EdgeBoundaryRoles[edge.Index],
            FeatureDetector::PatchBoundaryRole::HardFeature);
        EXPECT_TRUE(std::isinf(
            result.EdgeGrowthTransitionCosts[edge.Index]));
        EXPECT_TRUE(std::isinf(
            result.EdgeBoundaryMergeDelta[edge.Index]));
    }
}

TEST(CurvaturePatchContract,
     PatchCurvatureClosureSurvivesWithoutFeatureEvidence)
{
    const auto transitionCurvature = [](const double x, double)
    {
        const double value = std::tanh(x / 0.05);
        return glm::dvec2{
            std::max(value, 0.0), std::min(value, 0.0)};
    };
    OracleFixture fixture = MakeGraphGrid(
        "curvature_closure",
        [](double, double) { return 0.0; },
        transitionCurvature,
        [](const double x, double) { return x >= 0.0 ? 1u : 0u; });
    const FeatureDetector::CurvaturePatchResult result =
        FeatureDetector::SegmentFeatureAlignedPatches(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence},
            FixedPatchParams(2u));
    ASSERT_TRUE(result.Succeeded())
        << FeatureDetector::ToString(result.Diagnostics.Status);
    EXPECT_TRUE(EquivalentPartitions(
        fixture.Surface,
        fixture.ExpectedPatchByFace,
        result.FaceRegions));
    EXPECT_EQ(result.Diagnostics.FinalRegionCount, 2u);
    EXPECT_EQ(result.Diagnostics.HardBoundaryEdgeCount, 0u);
    EXPECT_EQ(result.Diagnostics.SoftBoundaryEdgeCount, 0u);
    EXPECT_GT(result.Diagnostics.ClosureBoundaryEdgeCount, 0u);
    ASSERT_EQ(result.FinalAdjacencies.size(), 1u);
    EXPECT_FALSE(result.FinalAdjacencies.front().HardBlocked);
    EXPECT_GT(
        result.FinalAdjacencies.front().RegionalCostIncrease,
        0.0);
    EXPECT_GE(result.FinalAdjacencies.front().DeltaMerge, 0.0);
    for (const EdgeHandle edge : fixture.Surface.LiveEdges())
    {
        if (result.EdgeBoundaries[edge.Index] == 0u)
            continue;
        EXPECT_EQ(
            result.EdgeBoundaryRoles[edge.Index],
            FeatureDetector::PatchBoundaryRole::CurvatureClosure);
        EXPECT_GE(result.EdgeBoundaryMergeDelta[edge.Index], 0.0);
    }
}

TEST(CurvaturePatchContract,
     PatchCompletionReplaysWithDetectedFeatureEvidence)
{
    for (const bool flipped : {false, true})
    {
        std::vector<OracleFixture> fixtures =
            MakeDetectorControlCatalog(flipped);
        fixtures.resize(3u);
        for (OracleFixture& fixture : fixtures)
        {
            SCOPED_TRACE(fixture.Id);
            const FeatureDetector::FeatureEvidenceResult detected =
                FeatureDetector::DetectFeatureEvidence(
                    fixture.Surface,
                    fixture.K1,
                    fixture.K2,
                    DetectorParamsFor(fixture));
            ASSERT_TRUE(detected.Succeeded())
                << FeatureDetector::ToString(
                    detected.Diagnostics.Status);
            const std::vector<std::uint32_t> seeds =
                FacesAdjacentToExpectedBoundary(fixture);
            const FeatureDetector::CurvaturePatchResult result =
                FeatureDetector::SegmentFeatureAlignedPatches(
                    fixture.Surface,
                    fixture.K1,
                    fixture.K2,
                    detected.View(),
                    FixedPatchParams(),
                    {seeds, true});
            ASSERT_TRUE(result.Succeeded())
                << FeatureDetector::ToString(result.Diagnostics.Status);
            EXPECT_TRUE(EquivalentPartitions(
                fixture.Surface,
                fixture.ExpectedPatchByFace,
                result.FaceRegions));
            EXPECT_EQ(result.Diagnostics.HardBoundaryEdgeCount, 0u);
            EXPECT_GT(result.Diagnostics.SoftBoundaryEdgeCount, 0u);
            EXPECT_EQ(result.Diagnostics.ClosureBoundaryEdgeCount, 0u);
            for (const EdgeHandle edge : fixture.Surface.LiveEdges())
            {
                EXPECT_EQ(
                    result.EdgeBoundaryRoles[edge.Index],
                    ExpectedPatchRole(
                        fixture.ExpectedBoundaryRole[edge.Index]));
            }
        }
    }
}

TEST(CurvaturePatchContract,
     SuppliedOracleIsolatesPatchCompletionFromCorruptedDetectorEvidence)
{
    std::vector<OracleFixture> fixtures = MakeOracleCatalog();
    auto fixtureIt = std::find_if(
        fixtures.begin(),
        fixtures.end(),
        [](const OracleFixture& fixture)
        { return fixture.Id == "ridge"; });
    ASSERT_NE(fixtureIt, fixtures.end());
    OracleFixture& fixture = *fixtureIt;
    const std::vector<std::uint32_t> seeds =
        FacesAdjacentToExpectedBoundary(fixture);
    const FeatureDetector::PatchSeedOverrides overrides{seeds, true};
    const FeatureDetector::CurvaturePatchResult oracle =
        FeatureDetector::SegmentFeatureAlignedPatches(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence},
            FixedPatchParams(),
            overrides);
    ASSERT_TRUE(oracle.Succeeded());
    EXPECT_TRUE(EquivalentPartitions(
        fixture.Surface,
        fixture.ExpectedPatchByFace,
        oracle.FaceRegions));

    std::vector<double> corruptedSoft(fixture.Surface.EdgesSize(), 0.0);
    const FeatureDetector::CurvaturePatchResult corrupted =
        FeatureDetector::SegmentFeatureAlignedPatches(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, corruptedSoft},
            FixedPatchParams(),
            overrides);
    ASSERT_TRUE(corrupted.Succeeded());
    EXPECT_EQ(corrupted.Diagnostics.FinalRegionCount, 1u);
    EXPECT_NE(oracle.FaceRegions, corrupted.FaceRegions);
    EXPECT_EQ(corrupted.Diagnostics.FinalBoundaryEdgeCount, 0u);
}

TEST(CurvaturePatchContract,
     DetectedHardFoldBlocksWhileDetectedHomogeneousControlsStayWhole)
{
    std::vector<OracleFixture> controls;
    controls.push_back(MakeGraphGrid(
        "detected_plane",
        [](double, double) { return 0.0; },
        [](double, double) { return glm::dvec2{0.0}; },
        [](double, double) { return 0u; }));
    controls.push_back(MakeCylinder());
    for (OracleFixture& fixture : controls)
    {
        SCOPED_TRACE(fixture.Id);
        const FeatureDetector::FeatureEvidenceResult detected =
            FeatureDetector::DetectFeatureEvidence(
                fixture.Surface, fixture.K1, fixture.K2);
        ASSERT_TRUE(detected.Succeeded());
        const FeatureDetector::CurvaturePatchResult result =
            FeatureDetector::SegmentFeatureAlignedPatches(
                fixture.Surface,
                fixture.K1,
                fixture.K2,
                detected.View(),
                FixedPatchParams());
        ASSERT_TRUE(result.Succeeded())
            << FeatureDetector::ToString(result.Diagnostics.Status);
        EXPECT_EQ(result.Diagnostics.FinalRegionCount, 1u);
        EXPECT_EQ(result.Diagnostics.FinalBoundaryEdgeCount, 0u);
    }

    OracleFixture fold = MakeFold(60.0, true);
    const FeatureDetector::FeatureEvidenceResult detected =
        FeatureDetector::DetectFeatureEvidence(
            fold.Surface, fold.K1, fold.K2);
    ASSERT_TRUE(detected.Succeeded());
    ASSERT_EQ(detected.Diagnostics.HardFeatureEdgeCount, 8u);
    const FeatureDetector::CurvaturePatchResult result =
        FeatureDetector::SegmentFeatureAlignedPatches(
            fold.Surface,
            fold.K1,
            fold.K2,
            detected.View(),
            FixedPatchParams());
    ASSERT_TRUE(result.Succeeded())
        << FeatureDetector::ToString(result.Diagnostics.Status);
    EXPECT_EQ(result.Diagnostics.FinalRegionCount, 2u);
    EXPECT_EQ(result.Diagnostics.HardBoundaryEdgeCount, 8u);
}

TEST(CurvaturePatchContract,
     PatchReferenceFailsClosedForMalformedProductionInputs)
{
    const Mesh empty;
    EXPECT_EQ(
        FeatureDetector::SegmentFeatureAlignedPatches(
            empty, {}, {}, {}).Diagnostics.Status,
        FeatureDetector::CurvaturePatchStatus::EmptyMesh);

    OracleFixture fixture = MakeGraphGrid(
        "production_validation_control",
        [](double, double) { return 0.0; },
        [](double, double) { return glm::dvec2{0.0}; },
        [](double, double) { return 0u; });
    const auto run = [&](
                         const std::span<const double> k1,
                         const std::span<const double> k2,
                         const FeatureDetector::FeatureEvidenceView evidence,
                         const FeatureDetector::CurvaturePatchParams& params,
                         const FeatureDetector::PatchSeedOverrides seeds = {})
    {
        return FeatureDetector::SegmentFeatureAlignedPatches(
                   fixture.Surface, k1, k2, evidence, params, seeds)
            .Diagnostics.Status;
    };
    const FeatureDetector::FeatureEvidenceView validEvidence{
        fixture.HardEdgeMask, fixture.SoftEdgeConfidence};
    const FeatureDetector::CurvaturePatchParams validParams =
        FixedPatchParams();

    EXPECT_EQ(
        run({}, fixture.K2, validEvidence, validParams),
        FeatureDetector::CurvaturePatchStatus::CurvatureCountMismatch);
    EXPECT_EQ(
        run(
            fixture.K1,
            fixture.K2,
            {{}, fixture.SoftEdgeConfidence},
            validParams),
        FeatureDetector::CurvaturePatchStatus::HardEvidenceCountMismatch);
    EXPECT_EQ(
        run(
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, {}},
            validParams),
        FeatureDetector::CurvaturePatchStatus::SoftEvidenceCountMismatch);

    FeatureDetector::CurvaturePatchParams invalidParams = validParams;
    invalidParams.PatchComplexityCost = 0.0;
    EXPECT_EQ(
        run(fixture.K1, fixture.K2, validEvidence, invalidParams),
        FeatureDetector::CurvaturePatchStatus::InvalidParameters);

    const std::array<std::uint32_t, 1u> invalidSeed{
        static_cast<std::uint32_t>(fixture.Surface.FacesSize())};
    EXPECT_EQ(
        run(
            fixture.K1,
            fixture.K2,
            validEvidence,
            validParams,
            {invalidSeed, true}),
        FeatureDetector::CurvaturePatchStatus::InvalidSeedOverride);

    fixture.K1.front() = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        run(fixture.K1, fixture.K2, validEvidence, validParams),
        FeatureDetector::CurvaturePatchStatus::NonFiniteCurvature);
    fixture.K1.front() = 0.0;
    fixture.K2.front() = 1.0;
    EXPECT_EQ(
        run(fixture.K1, fixture.K2, validEvidence, validParams),
        FeatureDetector::CurvaturePatchStatus::InvalidCurvatureOrder);
}

TEST(CurvaturePatchContract,
     PatchPartitionPassesFrozenDensityDiagonalScaleNoiseAndOrientationControls)
{
    const auto makeClosure = [](const bool flipped)
    {
        return MakeGraphGrid(
            flipped
                ? "stability_closure_phase_b"
                : "stability_closure_phase_a",
            [](double, double) { return 0.0; },
            [](const double x, double)
            {
                const double value = std::tanh(x / 0.05);
                return glm::dvec2{
                    std::max(value, 0.0), std::min(value, 0.0)};
            },
            [](const double x, double) { return x >= 0.0 ? 1u : 0u; },
            {},
            flipped);
    };

    std::array<OracleFixture, 2u> fixtures{
        makeClosure(false), makeClosure(true)};
    std::array<FeatureDetector::CurvaturePatchResult, 2u> baselines;
    for (std::size_t phase = 0u; phase < fixtures.size(); ++phase)
    {
        OracleFixture& fixture = fixtures[phase];
        for (const double spacing : {1.5, 2.0, 3.0})
        {
            FeatureDetector::CurvaturePatchParams params =
                FixedPatchParams(2u);
            params.SeedSpacingMultiplier = spacing;
            const FeatureDetector::CurvaturePatchResult result =
                FeatureDetector::SegmentFeatureAlignedPatches(
                    fixture.Surface,
                    fixture.K1,
                    fixture.K2,
                    {fixture.HardEdgeMask, fixture.SoftEdgeConfidence},
                    params);
            ASSERT_TRUE(result.Succeeded())
                << fixture.Id << ' '
                << FeatureDetector::ToString(result.Diagnostics.Status);
            EXPECT_LE(
                AreaWeightedVariationOfInformation(
                    fixture.Surface,
                    fixture.ExpectedPatchByFace,
                    result.FaceRegions),
                0.01);
            if (spacing == 2.0)
                baselines[phase] = result;
        }
    }

    const double projectedDistance = SymmetricBoundaryDistance(
        fixtures[0u].Surface,
        baselines[0u].EdgeBoundaries,
        fixtures[1u].Surface,
        baselines[1u].EdgeBoundaries);
    EXPECT_LE(
        projectedDistance
            / std::max(
                BoundingDiagonal(fixtures[0u].Surface),
                BoundingDiagonal(fixtures[1u].Surface)),
        0.02);

    OracleFixture& base = fixtures[0u];
    OracleFixture scaled = base;
    for (const VertexHandle vertex : scaled.Surface.LiveVertices())
        scaled.Surface.Position(vertex) *= 3.0f;
    for (double& value : scaled.K1)
        value /= 3.0;
    for (double& value : scaled.K2)
        value /= 3.0;
    const FeatureDetector::CurvaturePatchResult scaledResult =
        FeatureDetector::SegmentFeatureAlignedPatches(
            scaled.Surface,
            scaled.K1,
            scaled.K2,
            {scaled.HardEdgeMask, scaled.SoftEdgeConfidence},
            FixedPatchParams(2u));
    ASSERT_TRUE(scaledResult.Succeeded());
    EXPECT_TRUE(EquivalentPartitions(
        scaled.Surface,
        scaled.ExpectedPatchByFace,
        scaledResult.FaceRegions));

    std::vector<double> noisyK1 = base.K1;
    std::vector<double> noisyK2 = base.K2;
    for (std::size_t vertex = 0u; vertex < noisyK1.size(); ++vertex)
    {
        const double noise = 1.0e-3 * std::sin(
            static_cast<double>(vertex) * 1.61803398875);
        noisyK1[vertex] += noise;
        noisyK2[vertex] += noise;
    }
    const FeatureDetector::CurvaturePatchResult noisy =
        FeatureDetector::SegmentFeatureAlignedPatches(
            base.Surface,
            noisyK1,
            noisyK2,
            {base.HardEdgeMask, base.SoftEdgeConfidence},
            FixedPatchParams(2u));
    ASSERT_TRUE(noisy.Succeeded());
    EXPECT_LE(
        AreaWeightedVariationOfInformation(
            base.Surface,
            base.ExpectedPatchByFace,
            noisy.FaceRegions),
        0.01);

    std::vector<double> reversedK1(base.K1.size(), 0.0);
    std::vector<double> reversedK2(base.K2.size(), 0.0);
    for (std::size_t vertex = 0u; vertex < base.K1.size(); ++vertex)
    {
        reversedK1[vertex] = -base.K2[vertex];
        reversedK2[vertex] = -base.K1[vertex];
    }
    const FeatureDetector::CurvaturePatchResult reversed =
        FeatureDetector::SegmentFeatureAlignedPatches(
            base.Surface,
            reversedK1,
            reversedK2,
            {base.HardEdgeMask, base.SoftEdgeConfidence},
            FixedPatchParams(2u));
    ASSERT_TRUE(reversed.Succeeded());
    EXPECT_TRUE(EquivalentPartitions(
        base.Surface,
        baselines[0u].FaceRegions,
        reversed.FaceRegions));
}

TEST(CurvaturePatchContract,
     LocalRagOneStepSeedPerturbationRefutesFrozenStabilityGate)
{
    OracleFixture fixture = MakeGraphGrid(
        "local_rag_seed_refutation",
        [](double, double) { return 0.0; },
        [](const double x, double)
        {
            const double value = std::tanh(x / 0.05);
            return glm::dvec2{
                std::max(value, 0.0), std::min(value, 0.0)};
        },
        [](const double x, double) { return x >= 0.0 ? 1u : 0u; });
    const FeatureDetector::CurvaturePatchResult baseline =
        FeatureDetector::SegmentFeatureAlignedPatches(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence},
            FixedPatchParams(2u));
    ASSERT_TRUE(baseline.Succeeded())
        << FeatureDetector::ToString(baseline.Diagnostics.Status);

    const std::vector<std::uint32_t> perturbedSeeds =
        PerturbSeedsOneDualStep(
            fixture.Surface, baseline.SeedFaceSlots);
    const FeatureDetector::CurvaturePatchResult perturbed =
        FeatureDetector::SegmentFeatureAlignedPatches(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence},
            FixedPatchParams(2u),
            {perturbedSeeds, true});
    ASSERT_TRUE(perturbed.Succeeded())
        << FeatureDetector::ToString(perturbed.Diagnostics.Status);

    const double variation = AreaWeightedVariationOfInformation(
        fixture.Surface,
        fixture.ExpectedPatchByFace,
        perturbed.FaceRegions);
    EXPECT_GT(variation, 0.01)
        << "A passing value would invalidate METHOD-039's recorded local "
           "solver refutation and require reevaluating its follow-up.";
    EXPECT_GT(perturbed.Diagnostics.FinalRegionCount, 2u);
    EXPECT_GT(perturbed.Diagnostics.ClosureBoundaryEdgeCount, 0u);
}

TEST(CurvaturePatchContract, SharedStrictFoldClassifierMatchesOracle)
{
    for (const double angle : {30.0, 45.0, 60.0})
    {
        for (const bool flipped : {false, true})
        {
            OracleFixture fixture = MakeFold(angle, flipped);
            SCOPED_TRACE(fixture.Id);
            Features::Params params{};
            params.BoundaryIsFeature = false;
            params.DihedralThresholdDegrees = 45.0;
            const Features::Classification classification = Features::Classify(
                fixture.Surface, fixture.ExactFaceNormals, params);
            ASSERT_EQ(
                classification.Status,
                Features::ClassificationStatus::Success);
            EXPECT_EQ(classification.EdgeFeatureMask, fixture.HardEdgeMask);
            EXPECT_EQ(
                classification.FeatureEdgeCount,
                angle > 45.0 ? 8u : 0u);
        }
    }
}

TEST(CurvaturePatchContract, SuppliedEvidenceFailsClosedBeforePatchWork)
{
    OracleFixture fixture = MakeGraphGrid(
        "validation_control",
        [](double, double) { return 0.0; },
        [](double, double) { return glm::dvec2{0.0}; },
        [](double, double) { return 0u; });
    const SuppliedFeatureEvidence valid{
        fixture.HardEdgeMask, fixture.SoftEdgeConfidence};
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface, fixture.K1, fixture.K2, valid),
        EvidenceStatus::Success);

    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            std::span<const double>{},
            fixture.K2,
            valid),
        EvidenceStatus::CurvatureCountMismatch);
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {std::span<const std::uint8_t>{},
             fixture.SoftEdgeConfidence}),
        EvidenceStatus::HardEvidenceCountMismatch);

    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask,
             std::span<const double>{}}),
        EvidenceStatus::SoftEvidenceCountMismatch);

    fixture.HardEdgeMask.front() = 2u;
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::InvalidHardEvidence);
    fixture.HardEdgeMask.front() = 0u;

    fixture.SoftEdgeConfidence.front() =
        std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::NonFiniteSoftEvidence);
    fixture.SoftEdgeConfidence.front() = 1.01;
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::SoftEvidenceOutOfRange);
    fixture.SoftEdgeConfidence.front() = 0.0;

    fixture.K1.front() =
        std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::NonFiniteCurvature);
    fixture.K1.front() = -1.0;
    fixture.K2.front() = 1.0;
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            fixture.Surface,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::InvalidCurvatureOrder);
    fixture.K1.front() = 0.0;
    fixture.K2.front() = 0.0;

    Mesh nonFiniteGeometry = fixture.Surface;
    nonFiniteGeometry.Position(VertexHandle{0u}).x =
        std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            nonFiniteGeometry,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::NonFiniteGeometry);

    Mesh invalidTopology = fixture.Surface;
    const EdgeHandle firstEdge = *invalidTopology.LiveEdges().begin();
    invalidTopology.SetFace(
        invalidTopology.Halfedge(firstEdge, 0u),
        FaceHandle{static_cast<Geometry::PropertyIndex>(
            invalidTopology.FacesSize() + 7u)});
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            invalidTopology,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::InvalidTopology);

    Mesh quad{};
    const std::array<VertexHandle, 4u> quadVertices{
        quad.AddVertex({0.0f, 0.0f, 0.0f}),
        quad.AddVertex({1.0f, 0.0f, 0.0f}),
        quad.AddVertex({1.0f, 1.0f, 0.0f}),
        quad.AddVertex({0.0f, 1.0f, 0.0f}),
    };
    ASSERT_TRUE(quad.AddFace(quadVertices).has_value());
    const std::vector<double> quadK1(quad.VerticesSize(), 0.0);
    const std::vector<double> quadK2(quad.VerticesSize(), 0.0);
    const std::vector<std::uint8_t> quadHard(quad.EdgesSize(), 0u);
    const std::vector<double> quadSoft(quad.EdgesSize(), 0.0);
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            quad, quadK1, quadK2, {quadHard, quadSoft}),
        EvidenceStatus::NonTriangleFace);

    Mesh degenerate{};
    const VertexHandle d0 = degenerate.AddVertex({0.0f, 0.0f, 0.0f});
    const VertexHandle d1 = degenerate.AddVertex({1.0f, 0.0f, 0.0f});
    const VertexHandle d2 = degenerate.AddVertex({2.0f, 0.0f, 0.0f});
    ASSERT_TRUE(degenerate.AddTriangle(d0, d1, d2).has_value());
    const std::vector<double> degenerateK1(
        degenerate.VerticesSize(), 0.0);
    const std::vector<double> degenerateK2(
        degenerate.VerticesSize(), 0.0);
    const std::vector<std::uint8_t> degenerateHard(
        degenerate.EdgesSize(), 0u);
    const std::vector<double> degenerateSoft(
        degenerate.EdgesSize(), 0.0);
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            degenerate,
            degenerateK1,
            degenerateK2,
            {degenerateHard, degenerateSoft}),
        EvidenceStatus::DegenerateFace);

    const Mesh empty{};
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            empty, {}, {}, {{}, {}}),
        EvidenceStatus::EmptyMesh);

    Mesh view = Mesh::CreateView(
        fixture.Surface,
        Geometry::ElementRange{0u, fixture.Surface.VerticesSize()},
        Geometry::ElementRange{0u, fixture.Surface.EdgesSize()},
        Geometry::ElementRange{0u, fixture.Surface.FacesSize()});
    EXPECT_EQ(
        ValidateSuppliedEvidence(
            view,
            fixture.K1,
            fixture.K2,
            {fixture.HardEdgeMask, fixture.SoftEdgeConfidence}),
        EvidenceStatus::UnsupportedSubmeshView);
}

TEST(CurvaturePatchContract,
     FeatureDetectorConsumesSharedHardFactsAndRejectsConstantSoftSignals)
{
    std::vector<OracleFixture> fixtures = MakeOracleCatalog();
    ASSERT_GE(fixtures.size(), 9u);
    for (std::size_t fixtureIndex = 0u; fixtureIndex < 9u; ++fixtureIndex)
    {
        OracleFixture& fixture = fixtures[fixtureIndex];
        SCOPED_TRACE(fixture.Id);
        FeatureDetector::FeatureEvidenceParams params{};
        const FeatureDetector::FeatureEvidenceResult result =
            FeatureDetector::DetectFeatureEvidence(
                fixture.Surface, fixture.K1, fixture.K2, params);
        ASSERT_TRUE(result.Succeeded())
            << FeatureDetector::ToString(result.Diagnostics.Status);

        Features::Params sharedParams{};
        sharedParams.BoundaryIsFeature = false;
        sharedParams.DihedralThresholdDegrees = 45.0;
        const Features::Classification shared = Features::Classify(
            fixture.Surface, FaceNormals(fixture.Surface), sharedParams);
        ASSERT_EQ(shared.Status, Features::ClassificationStatus::Success);
        EXPECT_EQ(result.HardEdgeMask, shared.EdgeFeatureMask);
        EXPECT_EQ(
            result.Diagnostics.HardFeatureEdgeCount,
            shared.FeatureEdgeCount);
        EXPECT_TRUE(std::ranges::all_of(
            result.EdgeRawConfidence,
            [](const double value) { return value == 0.0; }));
        EXPECT_TRUE(std::ranges::all_of(
            result.SoftEdgeConfidence,
            [](const double value) { return value == 0.0; }));
        EXPECT_EQ(
            result.Diagnostics.BoundedSearchCount,
            2u * result.Diagnostics.InteriorCandidateEdgeCount);
        EXPECT_EQ(result.View().HardEdgeMask.size(), fixture.Surface.EdgesSize());
        EXPECT_EQ(
            result.View().SoftEdgeConfidence.size(),
            fixture.Surface.EdgesSize());

        if (fixture.Id.starts_with("strict_fold_30"))
            EXPECT_EQ(result.Diagnostics.HardFeatureEdgeCount, 0u);
        if (fixture.Id.starts_with("strict_fold_60"))
            EXPECT_EQ(result.Diagnostics.HardFeatureEdgeCount, 8u);
    }

    OracleFixture plane = std::move(fixtures.front());
    FeatureDetector::FeatureEvidenceParams boundaryParams{};
    boundaryParams.BoundaryIsHardFeature = true;
    const FeatureDetector::FeatureEvidenceResult boundaryResult =
        FeatureDetector::DetectFeatureEvidence(
            plane.Surface, plane.K1, plane.K2, boundaryParams);
    ASSERT_TRUE(boundaryResult.Succeeded());
    EXPECT_EQ(
        boundaryResult.Diagnostics.HardFeatureEdgeCount,
        boundaryResult.Diagnostics.SourceBoundaryEdgeCount);
    EXPECT_GT(boundaryResult.Diagnostics.HardFeatureEdgeCount, 0u);
}

TEST(CurvaturePatchContract,
     FeatureDetectorImplementsFrozenPerSignalScaleEquations)
{
    Mesh mesh{};
    const VertexHandle v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const VertexHandle v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    const VertexHandle v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    const VertexHandle v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
    ASSERT_TRUE(mesh.AddTriangle(v0, v2, v3).has_value());
    const auto interior = mesh.FindEdge(v0, v2);
    ASSERT_TRUE(interior.has_value());

    std::vector<double> transitionK1(mesh.VerticesSize(), 0.0);
    std::vector<double> transitionK2(mesh.VerticesSize(), 0.0);
    transitionK1[v1.Index] = 30.0;
    transitionK2[v3.Index] = -30.0;
    const FeatureDetector::FeatureEvidenceResult transition =
        FeatureDetector::DetectFeatureEvidence(
            mesh, transitionK1, transitionK2);
    ASSERT_TRUE(transition.Succeeded());

    std::vector<double> ridgeK1(mesh.VerticesSize(), 0.0);
    std::vector<double> ridgeK2(mesh.VerticesSize(), 0.0);
    ridgeK1[v0.Index] = 30.0;
    ridgeK1[v2.Index] = 30.0;
    const FeatureDetector::FeatureEvidenceResult ridge =
        FeatureDetector::DetectFeatureEvidence(mesh, ridgeK1, ridgeK2);
    ASSERT_TRUE(ridge.Succeeded());

    std::vector<double> valleyK1(mesh.VerticesSize(), 0.0);
    std::vector<double> valleyK2(mesh.VerticesSize(), 0.0);
    valleyK2[v0.Index] = -30.0;
    valleyK2[v2.Index] = -30.0;
    const FeatureDetector::FeatureEvidenceResult valley =
        FeatureDetector::DetectFeatureEvidence(mesh, valleyK1, valleyK2);
    ASSERT_TRUE(valley.Succeeded());

    for (std::size_t scale = 0u;
         scale < FeatureDetector::kFeatureEvidenceScaleCount;
         ++scale)
    {
        const std::size_t index =
            interior->Index * FeatureDetector::kFeatureEvidenceScaleCount
            + scale;
        EXPECT_DOUBLE_EQ(transition.TransitionResponse[index], 1.0);
        EXPECT_DOUBLE_EQ(transition.RidgeResponse[index], 0.0);
        EXPECT_DOUBLE_EQ(transition.ValleyResponse[index], 0.0);

        const double expectedExtremality = 1.0 - std::exp(
            -10.0 * ridge.ScaleRadiiWorld[scale] / 0.10);
        EXPECT_NEAR(ridge.RidgeResponse[index], expectedExtremality, 1.0e-12);
        EXPECT_DOUBLE_EQ(ridge.TransitionResponse[index], 0.0);
        EXPECT_DOUBLE_EQ(ridge.ValleyResponse[index], 0.0);
        EXPECT_NEAR(valley.ValleyResponse[index], expectedExtremality, 1.0e-12);
        EXPECT_DOUBLE_EQ(valley.TransitionResponse[index], 0.0);
        EXPECT_DOUBLE_EQ(valley.RidgeResponse[index], 0.0);
    }

    EXPECT_DOUBLE_EQ(transition.SoftEdgeConfidence[interior->Index], 1.0);
    EXPECT_EQ(
        ridge.EdgeDominantSignal[interior->Index],
        FeatureDetector::SoftFeatureSignal::Ridge);
    EXPECT_EQ(
        valley.EdgeDominantSignal[interior->Index],
        FeatureDetector::SoftFeatureSignal::Valley);
    EXPECT_EQ(
        ridge.EdgeDecision[interior->Index],
        FeatureDetector::FeatureEdgeDecision::RetainedStrong);
    EXPECT_EQ(ridge.EdgePersistentScaleMask[interior->Index], 0x7u);
    EXPECT_EQ(ridge.Diagnostics.EndpointVertexCount, 2u);
    EXPECT_EQ(ridge.Diagnostics.JunctionVertexCount, 0u);
}

TEST(CurvaturePatchContract,
     FeatureDetectorRecoversPersistentLinesEndpointsAndJunctions)
{
    for (const bool flipped : {false, true})
    {
        std::vector<OracleFixture> fixtures =
            MakeDetectorControlCatalog(flipped);
        ASSERT_EQ(fixtures.size(), 6u);
        for (OracleFixture& fixture : fixtures)
        {
            SCOPED_TRACE(fixture.Id);
            const FeatureDetector::FeatureEvidenceParams params =
                DetectorParamsFor(fixture);
            const FeatureDetector::FeatureEvidenceResult result =
                FeatureDetector::DetectFeatureEvidence(
                    fixture.Surface, fixture.K1, fixture.K2, params);
            ASSERT_TRUE(result.Succeeded())
                << FeatureDetector::ToString(result.Diagnostics.Status);
            EXPECT_EQ(result.Diagnostics.HardFeatureEdgeCount, 0u);
            EXPECT_EQ(
                result.Diagnostics.BoundedSearchCount,
                2u * result.Diagnostics.InteriorCandidateEdgeCount);
            EXPECT_GT(result.Diagnostics.SettledFaceVisitCount, 0u);
            EXPECT_GT(result.Diagnostics.MaximumNeighborhoodFaceCount, 0u);

            std::size_t expectedCount = 0u;
            std::size_t detectedCount = 0u;
            for (const EdgeHandle edge : fixture.Surface.LiveEdges())
            {
                const bool expected =
                    fixture.SoftEdgeConfidence[edge.Index] > 0.0;
                const bool detected =
                    result.SoftEdgeConfidence[edge.Index] > 0.0;
                const HalfedgeHandle diagnosticHalfedge =
                    fixture.Surface.Halfedge(edge, 0u);
                EXPECT_EQ(detected, expected)
                    << "edge " << edge.Index << " from "
                    << fixture.Surface.Position(
                        fixture.Surface.FromVertex(diagnosticHalfedge)).x
                    << ',' << fixture.Surface.Position(
                        fixture.Surface.FromVertex(diagnosticHalfedge)).y
                    << " to "
                    << fixture.Surface.Position(
                        fixture.Surface.ToVertex(diagnosticHalfedge)).x
                    << ',' << fixture.Surface.Position(
                        fixture.Surface.ToVertex(diagnosticHalfedge)).y
                    << " q=" << result.EdgeRawConfidence[edge.Index];
                expectedCount += expected ? 1u : 0u;
                detectedCount += detected ? 1u : 0u;
                if (!detected)
                    continue;
                EXPECT_GE(
                    ThreeBitCount(result.EdgePersistentScaleMask[edge.Index]),
                    2u);
                EXPECT_EQ(result.EdgeNonMaximumSurvivor[edge.Index], 1u);
                EXPECT_TRUE(
                    result.EdgeDecision[edge.Index]
                        == FeatureDetector::FeatureEdgeDecision::RetainedStrong
                    || result.EdgeDecision[edge.Index]
                        == FeatureDetector::FeatureEdgeDecision::RetainedWeak);
            }
            EXPECT_EQ(detectedCount, expectedCount);
            EXPECT_EQ(
                result.Diagnostics.RetainedSoftEdgeCount,
                expectedCount);

            if (fixture.Id.starts_with("detector_nearby_pair"))
            {
                EXPECT_EQ(result.Diagnostics.EndpointVertexCount, 4u);
                EXPECT_EQ(result.Diagnostics.JunctionVertexCount, 0u);
            }
            else if (fixture.Id.starts_with("detector_junction"))
            {
                EXPECT_EQ(result.Diagnostics.EndpointVertexCount, 4u);
                EXPECT_EQ(result.Diagnostics.JunctionVertexCount, 1u);
            }
            else
            {
                EXPECT_EQ(result.Diagnostics.EndpointVertexCount, 2u);
                EXPECT_EQ(result.Diagnostics.JunctionVertexCount, 0u);
            }
        }
    }
}

TEST(CurvaturePatchContract,
     FeatureDetectorSupportIsInvariantUnderGlobalOrientationReversal)
{
    std::vector<OracleFixture> fixtures = MakeDetectorControlCatalog(false);
    ASSERT_GE(fixtures.size(), 3u);
    for (std::size_t fixtureIndex = 0u; fixtureIndex < 3u; ++fixtureIndex)
    {
        OracleFixture& fixture = fixtures[fixtureIndex];
        SCOPED_TRACE(fixture.Id);
        const FeatureDetector::FeatureEvidenceParams params =
            DetectorParamsFor(fixture);
        const FeatureDetector::FeatureEvidenceResult forward =
            FeatureDetector::DetectFeatureEvidence(
                fixture.Surface, fixture.K1, fixture.K2, params);
        ASSERT_TRUE(forward.Succeeded());

        std::vector<double> reversedK1(fixture.K1.size(), 0.0);
        std::vector<double> reversedK2(fixture.K2.size(), 0.0);
        for (std::size_t vertex = 0u; vertex < fixture.K1.size(); ++vertex)
        {
            reversedK1[vertex] = -fixture.K2[vertex];
            reversedK2[vertex] = -fixture.K1[vertex];
        }
        const FeatureDetector::FeatureEvidenceResult reversed =
            FeatureDetector::DetectFeatureEvidence(
                fixture.Surface, reversedK1, reversedK2, params);
        ASSERT_TRUE(reversed.Succeeded());
        EXPECT_EQ(forward.HardEdgeMask, reversed.HardEdgeMask);
        EXPECT_EQ(
            forward.EdgePersistentScaleMask,
            reversed.EdgePersistentScaleMask);
        EXPECT_EQ(
            forward.EdgeNonMaximumSurvivor,
            reversed.EdgeNonMaximumSurvivor);
        EXPECT_EQ(forward.EdgeDecision, reversed.EdgeDecision);
        ASSERT_EQ(
            forward.SoftEdgeConfidence.size(),
            reversed.SoftEdgeConfidence.size());
        for (std::size_t edge = 0u;
             edge < forward.SoftEdgeConfidence.size();
             ++edge)
        {
            EXPECT_NEAR(
                forward.SoftEdgeConfidence[edge],
                reversed.SoftEdgeConfidence[edge],
                1.0e-12);
            EXPECT_NEAR(
                forward.EdgeRawConfidence[edge],
                reversed.EdgeRawConfidence[edge],
                1.0e-12);
        }
        ASSERT_EQ(
            forward.TransitionResponse.size(),
            reversed.TransitionResponse.size());
        for (std::size_t index = 0u;
             index < forward.TransitionResponse.size();
             ++index)
        {
            EXPECT_NEAR(
                forward.TransitionResponse[index],
                reversed.TransitionResponse[index],
                1.0e-12);
            EXPECT_NEAR(
                forward.RidgeResponse[index],
                reversed.ValleyResponse[index],
                1.0e-12);
            EXPECT_NEAR(
                forward.ValleyResponse[index],
                reversed.RidgeResponse[index],
                1.0e-12);
        }
    }
}

TEST(CurvaturePatchContract, FeatureDetectorFailsClosedBeforeBoundedSearches)
{
    OracleFixture fixture = MakeGraphGrid(
        "detector_validation_control",
        [](double, double) { return 0.0; },
        [](double, double) { return glm::dvec2{0.0}; },
        [](double, double) { return 0u; });
    const auto expectFailure = [](
        const FeatureDetector::FeatureEvidenceResult& result,
        const FeatureDetector::FeatureEvidenceStatus status)
    {
        EXPECT_EQ(result.Diagnostics.Status, status);
        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Diagnostics.BoundedSearchCount, 0u);
    };

    FeatureDetector::FeatureEvidenceParams invalidParams{};
    invalidParams.BaseRadiusRatio = 0.0;
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            fixture.Surface, fixture.K1, fixture.K2, invalidParams),
        FeatureDetector::FeatureEvidenceStatus::InvalidParameters);
    invalidParams = {};
    invalidParams.HardDihedralThresholdDegrees =
        std::numeric_limits<double>::quiet_NaN();
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            fixture.Surface, fixture.K1, fixture.K2, invalidParams),
        FeatureDetector::FeatureEvidenceStatus::InvalidParameters);

    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            fixture.Surface, {}, fixture.K2),
        FeatureDetector::FeatureEvidenceStatus::CurvatureCountMismatch);
    std::vector<double> invalidK1 = fixture.K1;
    invalidK1.front() = std::numeric_limits<double>::quiet_NaN();
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            fixture.Surface, invalidK1, fixture.K2),
        FeatureDetector::FeatureEvidenceStatus::NonFiniteCurvature);
    invalidK1.front() = -1.0;
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            fixture.Surface, invalidK1, fixture.K2),
        FeatureDetector::FeatureEvidenceStatus::InvalidCurvatureOrder);

    Mesh nonFiniteGeometry = fixture.Surface;
    nonFiniteGeometry.Position(VertexHandle{0u}).x =
        std::numeric_limits<float>::infinity();
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            nonFiniteGeometry, fixture.K1, fixture.K2),
        FeatureDetector::FeatureEvidenceStatus::NonFinitePosition);

    Mesh invalidTopology = fixture.Surface;
    const EdgeHandle firstEdge = *invalidTopology.LiveEdges().begin();
    invalidTopology.SetFace(
        invalidTopology.Halfedge(firstEdge, 0u),
        FaceHandle{static_cast<Geometry::PropertyIndex>(
            invalidTopology.FacesSize() + 5u)});
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            invalidTopology, fixture.K1, fixture.K2),
        FeatureDetector::FeatureEvidenceStatus::InvalidTopology);

    Mesh quad{};
    const std::array<VertexHandle, 4u> quadVertices{
        quad.AddVertex({0.0f, 0.0f, 0.0f}),
        quad.AddVertex({1.0f, 0.0f, 0.0f}),
        quad.AddVertex({1.0f, 1.0f, 0.0f}),
        quad.AddVertex({0.0f, 1.0f, 0.0f}),
    };
    ASSERT_TRUE(quad.AddFace(quadVertices).has_value());
    const std::vector<double> quadCurvature(quad.VerticesSize(), 0.0);
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            quad, quadCurvature, quadCurvature),
        FeatureDetector::FeatureEvidenceStatus::NonTriangleFace);

    Mesh degenerate{};
    const VertexHandle d0 = degenerate.AddVertex({0.0f, 0.0f, 0.0f});
    const VertexHandle d1 = degenerate.AddVertex({1.0f, 0.0f, 0.0f});
    const VertexHandle d2 = degenerate.AddVertex({2.0f, 0.0f, 0.0f});
    ASSERT_TRUE(degenerate.AddTriangle(d0, d1, d2).has_value());
    const std::vector<double> degenerateCurvature(
        degenerate.VerticesSize(), 0.0);
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            degenerate, degenerateCurvature, degenerateCurvature),
        FeatureDetector::FeatureEvidenceStatus::DegenerateFace);

    const Mesh empty{};
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(empty, {}, {}),
        FeatureDetector::FeatureEvidenceStatus::EmptyMesh);
    Mesh view = Mesh::CreateView(
        fixture.Surface,
        Geometry::ElementRange{0u, fixture.Surface.VerticesSize()},
        Geometry::ElementRange{0u, fixture.Surface.EdgesSize()},
        Geometry::ElementRange{0u, fixture.Surface.FacesSize()});
    expectFailure(
        FeatureDetector::DetectFeatureEvidence(
            view, fixture.K1, fixture.K2),
        FeatureDetector::FeatureEvidenceStatus::UnsupportedSubmeshView);
}

TEST(CurvaturePatchContract,
     FeatureDetectorIsDeterministicAndComputePathOwnsItsResult)
{
    OracleFixture fixture = MakeDetectorControlCatalog(false).front();
    const FeatureDetector::FeatureEvidenceResult first =
        FeatureDetector::DetectFeatureEvidence(
            fixture.Surface, fixture.K1, fixture.K2);
    const FeatureDetector::FeatureEvidenceResult second =
        FeatureDetector::DetectFeatureEvidence(
            fixture.Surface, fixture.K1, fixture.K2);
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    EXPECT_EQ(first.HardEdgeMask, second.HardEdgeMask);
    EXPECT_EQ(first.SoftEdgeConfidence, second.SoftEdgeConfidence);
    EXPECT_EQ(first.TransitionResponse, second.TransitionResponse);
    EXPECT_EQ(first.RidgeResponse, second.RidgeResponse);
    EXPECT_EQ(first.ValleyResponse, second.ValleyResponse);
    EXPECT_EQ(first.EdgeDecision, second.EdgeDecision);
    EXPECT_EQ(first.HysteresisPredecessor, second.HysteresisPredecessor);

    Mesh sphere = Geometry::HalfedgeMesh::MakeMeshIcosahedron();
    const FeatureDetector::FeatureEvidenceResult computed =
        FeatureDetector::ComputeFeatureEvidence(sphere);
    ASSERT_TRUE(computed.Succeeded())
        << FeatureDetector::ToString(computed.Diagnostics.Status);
    EXPECT_EQ(computed.HardEdgeMask.size(), sphere.EdgesSize());
    EXPECT_EQ(computed.SoftEdgeConfidence.size(), sphere.EdgesSize());
    EXPECT_GE(
        computed.Diagnostics.Timings.CurvatureEstimationMilliseconds,
        0.0);
}
