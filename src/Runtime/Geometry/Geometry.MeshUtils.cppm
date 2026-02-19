module;
#include <cstddef>
#include <span>
#include <glm/fwd.hpp>

export module Geometry:MeshUtils;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::MeshUtils
{
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
}
