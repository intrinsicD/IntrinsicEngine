module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <glm/fwd.hpp>

export module Geometry.HalfedgeMesh.Utils;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

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

    struct BoundaryLoopData
    {
        std::vector<HalfedgeHandle> Halfedges{};
        std::vector<VertexHandle> Vertices{};
    };

    /// Gather canonical triangle traversal data for a face.
    /// Returns false for invalid/deleted faces and non-triangular face loops.
    bool TryGetTriangleFaceView(const HalfedgeMesh::Mesh& mesh, FaceHandle f, TriangleFaceView& out);

    /// Enumerate all boundary loops in canonical halfedge order.
    /// Deleted edges are skipped; empty/closed meshes return an empty vector.
    [[nodiscard]] std::vector<BoundaryLoopData> CollectBoundaryLoops(const HalfedgeMesh::Mesh& mesh);

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
    double EdgeLengthSq(const HalfedgeMesh::Mesh& mesh, EdgeHandle e);

    /// Mean edge length over all non-deleted edges.
    double MeanEdgeLength(const HalfedgeMesh::Mesh& mesh);

    /// Canonical cotan edge weight used by DEC and cotan Laplacian operators:
    ///   w_ij = (cot α_ij + cot β_ij) / 2
    /// Boundary edges contribute only their single incident angle.
    double EdgeCotanWeight(const HalfedgeMesh::Mesh& mesh, EdgeHandle e);

    /// Policy clamp bound for the per-halfedge cotangent: |cot| is limited to
    /// this magnitude so near-degenerate (sliver) triangles cannot inject
    /// unbounded weights into FEM/DEC operators.
    inline constexpr double kHalfedgeCotanClamp = 1.0e4;

    /// Standalone clamped per-halfedge cotangent, published as `h:clamped_cotan`.
    /// For each interior halfedge h it computes the cotangent of the angle
    /// opposite h's edge (the apex angle of h's triangle) using the Heron/metric
    /// form cot = (a² + b² − c²) / (4·Area), with the magnitude clamped to
    /// `maxMagnitude`. Boundary halfedges and degenerate (zero-area / non-finite)
    /// triangles fail closed to 0. The per-edge cotan weight is recovered as the
    /// average of the two halfedge cotans: EdgeCotanWeight(e) =
    /// (cot(h0) + cot(h1)) / 2.
    [[nodiscard]] HalfedgeProperty<double> ClampedHalfedgeCotan(
        HalfedgeMesh::Mesh& mesh, double maxMagnitude = kHalfedgeCotanClamp);

    /// Unnormalized face normal (cross product of two edge vectors).
    /// Magnitude equals twice the face area.
    glm::vec3 FaceNormal(const HalfedgeMesh::Mesh& mesh, FaceHandle f);

    /// Oriented vector area of a (possibly polygonal) face via Newell's method:
    ///   A = 1/2 Σ_i (v_i × v_{i+1}).
    /// The magnitude is the face area (for planar faces); the direction is the
    /// face normal. Returns the zero vector for a deleted/invalid/degenerate
    /// face or one with a non-finite corner position.
    glm::dvec3 FaceAreaVector(const HalfedgeMesh::Mesh& mesh, FaceHandle f);

    /// Scalar surface area of a (possibly polygonal) face, summed over the
    /// triangle fan so non-planar polygons report their true area (the Newell
    /// |FaceAreaVector| underreports folded faces). Equals |FaceAreaVector| for
    /// a planar face. Returns 0.0 for a deleted/invalid/degenerate face or one
    /// with a non-finite corner position.
    double FaceArea(const HalfedgeMesh::Mesh& mesh, FaceHandle f);

    /// Centroid of a face's own corner positions (average of the face vertices).
    /// Distinct from ComputeOneRingCentroid, which averages 1-ring neighbours.
    /// Returns (0,0,0) for a deleted/invalid/empty face.
    glm::dvec3 FaceCentroid(const HalfedgeMesh::Mesh& mesh, FaceHandle f);

    /// Lumped ("barycentric") vertex areas: each vertex receives FaceArea/degree
    /// from every incident face (= Σ incident FaceArea / 3 for a triangle mesh).
    /// Cheaper alternative to the mixed-Voronoi area. Indexed by vertex storage
    /// index; deleted vertices receive 0.
    std::vector<double> ComputeBarycentricVertexAreas(const HalfedgeMesh::Mesh& mesh);

    /// Area-weighted vertex normal with safety iteration limit.
    /// Falls back to (0, 1, 0) for degenerate configurations.
    glm::vec3 VertexNormal(const HalfedgeMesh::Mesh& mesh, VertexHandle v);

    /// Target valence for isotropic remeshing: 6 for interior, 4 for boundary.
    int TargetValence(const HalfedgeMesh::Mesh& mesh, VertexHandle v);

    /// Shared edge-flip pass used by remeshing operators to reduce valence error.
    std::size_t EqualizeValenceByEdgeFlip(HalfedgeMesh::Mesh& mesh, bool preserveBoundary);

    /// Shared tangential Laplacian smoothing pass used by remeshing operators.
    void TangentialSmooth(HalfedgeMesh::Mesh& mesh, double lambda, bool preserveBoundary);

    /// Compute area-weighted vertex normals and store as "v:normal" property.
    /// Returns the number of vertices with valid normals.
    std::size_t PublishVertexNormals(HalfedgeMesh::Mesh& mesh);

    /// Compute face normals and store as "f:normal" property.
    /// Returns the number of faces with valid normals.
    std::size_t PublishFaceNormals(HalfedgeMesh::Mesh& mesh);

    /// Mixed Voronoi area per vertex (Meyer et al., 2003).
    /// Returns a vector indexed by vertex storage index.
    /// Used by Curvature, Smoothing (cotan), and DEC (Hodge star 0).
    std::vector<double> ComputeMixedVoronoiAreas(const HalfedgeMesh::Mesh& mesh);

    /// Cotan-weighted Laplacian displacement per vertex:
    ///   L[i] = Σ_j (cot α_ij + cot β_ij) / 2 · (x_j - x_i)
    /// Used by Curvature (mean curvature normal) and Smoothing (cotan Laplacian).
    /// When clampNonNegative is true, individual edge weights are clamped to
    /// max(0, cotSum) — standard practice for explicit smoothing to avoid
    /// instability from obtuse-triangle negative weights (Botsch et al., §4.2).
    std::vector<glm::dvec3> ComputeCotanLaplacian(
        const HalfedgeMesh::Mesh& mesh,
        bool clampNonNegative = false);

    /// Sum incident corner angles for each vertex using all non-deleted
    /// triangular faces. Returned vector is indexed by vertex storage index.
    /// Used by curvature operators for angle-defect based Gaussian curvature.
    std::vector<double> ComputeVertexAngleSums(const HalfedgeMesh::Mesh& mesh);

    /// Discrete Gaussian angle defect at a vertex from a precomputed angle sum.
    /// Interior: 2π - angleSum, boundary: π - angleSum.
    double ComputeVertexAngleDefect(const HalfedgeMesh::Mesh& mesh,
                                    VertexHandle v,
                                    double angleSumAtVertex);

    /// Uniform 1-ring centroid for a mesh vertex (double precision).
    /// Returns the average position of all halfedge neighbors, or the vertex
    /// position itself if it has no neighbors.
    glm::dvec3 ComputeOneRingCentroid(const HalfedgeMesh::Mesh& mesh, VertexHandle v);

    struct TriangleSoupBuildParams
    {
        // Merge coincident vertices before building topology. When UVs are supplied,
        // only UV-compatible duplicates are welded so texture seams remain representable.
        bool WeldVertices{false};

        // World-space weld tolerance. Values <= 0 fall back to exact-position matching.
        float WeldEpsilon{0.0f};
    };

    [[nodiscard]] std::optional<HalfedgeMesh::Mesh> BuildHalfedgeMeshFromIndexedTriangles(
        std::span<const glm::vec3> positions,
        std::span<const uint32_t> indices,
        const TriangleSoupBuildParams& params = {});

    [[nodiscard]] std::optional<HalfedgeMesh::Mesh> BuildHalfedgeMeshFromIndexedTriangles(
        std::span<const glm::vec3> positions,
        std::span<const uint32_t> indices,
        std::span<const glm::vec4> aux,
        const TriangleSoupBuildParams& params = {});

    void ExtractIndexedTriangles(
        const HalfedgeMesh::Mesh& mesh,
        std::vector<glm::vec3>& positions,
        std::vector<uint32_t>& indices,
        std::vector<glm::vec4>* aux = nullptr,
        std::vector<uint32_t>* triangleFaceIds = nullptr);

    // --- Edge Loop / Edge Ring selection ---

    /// Strategy for edge loop/ring traversal behavior on mixed-topology meshes.
    enum class EdgeTraversalStrategy : uint8_t
    {
        /// Permissive (default): walks through any vertex valence / face type
        /// (triangles and quads). At odd-valence vertices, uses geometric
        /// angle-based tie-breaking to pick the straightest continuation edge.
        /// Produces zig-zag paths on triangle meshes.
        Permissive = 0,

        /// StrictQuad: stops at non-valence-4 vertices (loop) or non-quad
        /// faces (ring). Produces clean selections only on pure-quad regions.
        StrictQuad = 1,
    };

    /// Collect an edge loop starting from the given edge.
    /// An edge loop is a continuous path of edges through vertices. At each
    /// vertex the walk picks the "straight" continuation by CW-rotating by
    /// half the vertex valence in the halfedge fan (Blender convention).
    /// For even-valence vertices the continuation is unambiguous (valence/2).
    /// For odd-valence vertices, the two candidates (floor and ceil of
    /// valence/2 rotations) are evaluated geometrically: the candidate whose
    /// outgoing direction best continues the incoming direction (smallest
    /// angle deviation from straight-through) wins. This ensures deterministic,
    /// geometry-aware results on irregular topology.
    /// In StrictQuad mode, the walk stops at any vertex with valence != 4.
    /// Returns the ordered edge indices (includes the starting edge).
    /// No duplicates on closed manifolds.
    [[nodiscard]] std::vector<uint32_t> CollectEdgeLoop(
        const HalfedgeMesh::Mesh& mesh, EdgeHandle startEdge,
        EdgeTraversalStrategy strategy = EdgeTraversalStrategy::Permissive);

    /// Collect an edge ring starting from the given edge.
    /// An edge ring selects parallel edges across a face strip. In each face
    /// the "opposite" edge is found via Next^(valence/2), then the walk crosses
    /// to the adjacent face. For quads this gives clean parallel selections;
    /// for triangles it walks via Next^1(h) (valence/2 = 1, zig-zag pattern).
    /// In StrictQuad mode, the walk stops at any non-quad face (valence != 4).
    /// Stops at mesh boundaries, unsupported face types, or when the ring closes.
    /// Returns the ordered edge indices (includes the starting edge).
    /// No duplicates on closed manifolds.
    [[nodiscard]] std::vector<uint32_t> CollectEdgeRing(
        const HalfedgeMesh::Mesh& mesh, EdgeHandle startEdge,
        EdgeTraversalStrategy strategy = EdgeTraversalStrategy::Permissive);
}
