module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <glm/fwd.hpp>

export module Geometry:MeshUtils;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::MeshUtils
{
    // Canonical property name for per-vertex texture coordinates across all
    // geometry modules (subdivision, remeshing, mesh conversion, etc.).
    inline constexpr const char* kVertexTexcoordPropertyName = "v:texcoord";

    struct TriangleFaceView
    {
        FaceHandle Face{};
        HalfedgeHandle H0{};
        HalfedgeHandle H1{};
        HalfedgeHandle H2{};
        VertexHandle V0{};
        VertexHandle V1{};
        VertexHandle V2{};
        glm::vec3 P0{};
        glm::vec3 P1{};
        glm::vec3 P2{};
    };

    /// Gather canonical triangle traversal data for a face.
    /// Returns false for invalid/deleted faces and non-triangular face loops.
    bool TryGetTriangleFaceView(const Halfedge::Mesh& mesh, FaceHandle f, TriangleFaceView& out);

    // --- Index-buffer mesh utilities ---

    int GenerateUVs(std::span<const glm::vec3> positions, std::span<glm::vec4> aux);

    void CalculateNormals(std::span<const glm::vec3> positions, std::span<const uint32_t> indices,
                          std::span<glm::vec3> normals);

    // --- Halfedge mesh math utilities ---
    // These were previously duplicated as static functions across Curvature, DEC,
    // Smoothing, Geodesic, Remeshing, AdaptiveRemeshing, and MeshQuality modules.

    /// Cotangent of the angle between two vectors: cot(theta) = dot(u,v) / |cross(u,v)|.
    /// Returns 0.0 for degenerate (near-zero cross product) inputs.
    double Cotan(glm::vec3 u, glm::vec3 v);

    /// Area of the triangle with vertices (a, b, c).
    double TriangleArea(glm::vec3 a, glm::vec3 b, glm::vec3 c);

    /// Angle at vertex a in triangle (a, b, c), computed via acos.
    double AngleAtVertex(glm::vec3 a, glm::vec3 b, glm::vec3 c);

    /// Squared edge length of a halfedge mesh edge.
    double EdgeLengthSq(const Halfedge::Mesh& mesh, EdgeHandle e);

    /// Mean edge length over all non-deleted edges.
    double MeanEdgeLength(const Halfedge::Mesh& mesh);

    /// Unnormalized face normal (cross product of two edge vectors).
    /// Magnitude equals twice the face area.
    glm::vec3 FaceNormal(const Halfedge::Mesh& mesh, FaceHandle f);

    /// Area-weighted vertex normal with safety iteration limit.
    /// Falls back to (0, 1, 0) for degenerate configurations.
    glm::vec3 VertexNormal(const Halfedge::Mesh& mesh, VertexHandle v);

    /// Target valence for isotropic remeshing: 6 for interior, 4 for boundary.
    int TargetValence(const Halfedge::Mesh& mesh, VertexHandle v);

    /// Shared edge-flip pass used by remeshing operators to reduce valence error.
    std::size_t EqualizeValenceByEdgeFlip(Halfedge::Mesh& mesh, bool preserveBoundary);

    /// Shared tangential Laplacian smoothing pass used by remeshing operators.
    void TangentialSmooth(Halfedge::Mesh& mesh, double lambda, bool preserveBoundary);

    /// Mixed Voronoi area per vertex (Meyer et al., 2003).
    /// Returns a vector indexed by vertex storage index.
    /// Used by Curvature, Smoothing (cotan), and DEC (Hodge star 0).
    std::vector<double> ComputeMixedVoronoiAreas(const Halfedge::Mesh& mesh);

    struct TriangleSoupBuildParams
    {
        // Merge coincident vertices before building topology. When UVs are supplied,
        // only UV-compatible duplicates are welded so texture seams remain representable.
        bool WeldVertices{false};

        // World-space weld tolerance. Values <= 0 fall back to exact-position matching.
        float WeldEpsilon{0.0f};
    };

    [[nodiscard]] std::optional<Halfedge::Mesh> BuildHalfedgeMeshFromIndexedTriangles(
        std::span<const glm::vec3> positions,
        std::span<const uint32_t> indices,
        const TriangleSoupBuildParams& params = {});

    [[nodiscard]] std::optional<Halfedge::Mesh> BuildHalfedgeMeshFromIndexedTriangles(
        std::span<const glm::vec3> positions,
        std::span<const uint32_t> indices,
        std::span<const glm::vec4> aux,
        const TriangleSoupBuildParams& params = {});

    void ExtractIndexedTriangles(
        const Halfedge::Mesh& mesh,
        std::vector<glm::vec3>& positions,
        std::vector<uint32_t>& indices,
        std::vector<glm::vec4>* aux = nullptr);
}
