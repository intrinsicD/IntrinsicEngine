module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:ConvexHullBuilder.Impl;

import :ConvexHullBuilder;
import :Primitives;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::ConvexHullBuilder
{
    // =========================================================================
    // Internal data structures
    // =========================================================================

    struct HullFace
    {
        // Indices into the input point array
        std::array<uint32_t, 3> Vertices;

        // Outward-facing plane for this face
        Plane FacePlane;

        // Conflict list: indices of input points above this face
        std::vector<uint32_t> ConflictPoints;

        // Linked-list indices for active face management
        bool Deleted{false};
    };

    struct HorizonEdge
    {
        uint32_t V0;         // Start vertex (index into input points)
        uint32_t V1;         // End vertex (index into input points)
        std::size_t FaceIdx; // The non-visible face adjacent to this edge
    };

    // =========================================================================
    // Helper functions
    // =========================================================================

    static Plane MakePlane(glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        glm::vec3 normal = glm::cross(b - a, c - a);
        float len = glm::length(normal);
        if (len < 1e-12f)
            return Plane{{0.0f, 0.0f, 0.0f}, 0.0f};
        normal /= len;
        float dist = glm::dot(normal, a);
        return Plane{normal, dist};
    }

    static double SignedDistance(const Plane& plane, glm::vec3 point)
    {
        return static_cast<double>(glm::dot(plane.Normal, point)) -
               static_cast<double>(plane.Distance);
    }

    // Find the point index farthest from a line segment (A, B) among candidates
    static uint32_t FarthestFromLine(std::span<const glm::vec3> points,
                                     const std::vector<uint32_t>& candidates,
                                     glm::vec3 a, glm::vec3 b)
    {
        glm::dvec3 ab = glm::dvec3(b) - glm::dvec3(a);
        double abLen2 = glm::dot(ab, ab);
        double bestDist2 = -1.0;
        uint32_t bestIdx = candidates[0];

        for (uint32_t idx : candidates)
        {
            glm::dvec3 ap = glm::dvec3(points[idx]) - glm::dvec3(a);
            double t = (abLen2 > 1e-30) ? glm::dot(ap, ab) / abLen2 : 0.0;
            glm::dvec3 proj = glm::dvec3(a) + t * ab;
            glm::dvec3 diff = glm::dvec3(points[idx]) - proj;
            double dist2 = glm::dot(diff, diff);
            if (dist2 > bestDist2)
            {
                bestDist2 = dist2;
                bestIdx = idx;
            }
        }
        return bestIdx;
    }

    // Find the point index farthest from a plane among candidates
    static uint32_t FarthestFromPlane(std::span<const glm::vec3> points,
                                      const std::vector<uint32_t>& candidates,
                                      const Plane& plane)
    {
        double bestDist = -std::numeric_limits<double>::max();
        uint32_t bestIdx = candidates[0];

        for (uint32_t idx : candidates)
        {
            double dist = std::abs(SignedDistance(plane, points[idx]));
            if (dist > bestDist)
            {
                bestDist = dist;
                bestIdx = idx;
            }
        }
        return bestIdx;
    }

    // =========================================================================
    // Quickhull: find initial tetrahedron
    // =========================================================================

    struct InitialSimplex
    {
        std::array<uint32_t, 4> Indices;
        bool Valid{false};
    };

    static InitialSimplex FindInitialSimplex(std::span<const glm::vec3> points,
                                             double epsilon)
    {
        const auto n = static_cast<uint32_t>(points.size());
        InitialSimplex result{};

        // Step 1: Find extreme points on each axis
        uint32_t minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
        for (uint32_t i = 1; i < n; ++i)
        {
            if (points[i].x < points[minX].x) minX = i;
            if (points[i].x > points[maxX].x) maxX = i;
            if (points[i].y < points[minY].y) minY = i;
            if (points[i].y > points[maxY].y) maxY = i;
            if (points[i].z < points[minZ].z) minZ = i;
            if (points[i].z > points[maxZ].z) maxZ = i;
        }

        // Step 2: Find the most distant pair among extremes
        std::array<uint32_t, 6> extremes = {minX, maxX, minY, maxY, minZ, maxZ};
        double bestDist2 = -1.0;
        uint32_t p0 = 0, p1 = 1;

        for (std::size_t i = 0; i < extremes.size(); ++i)
        {
            for (std::size_t j = i + 1; j < extremes.size(); ++j)
            {
                glm::dvec3 diff = glm::dvec3(points[extremes[i]]) -
                                  glm::dvec3(points[extremes[j]]);
                double d2 = glm::dot(diff, diff);
                if (d2 > bestDist2)
                {
                    bestDist2 = d2;
                    p0 = extremes[i];
                    p1 = extremes[j];
                }
            }
        }

        // Degenerate: all points coincident
        if (bestDist2 < epsilon * epsilon)
            return result;

        // Step 3: Find the point most distant from the line p0-p1
        std::vector<uint32_t> allIndices(n);
        std::iota(allIndices.begin(), allIndices.end(), 0u);
        uint32_t p2 = FarthestFromLine(points, allIndices, points[p0], points[p1]);

        // Check that p2 is not collinear with p0-p1
        {
            glm::dvec3 ab = glm::dvec3(points[p1]) - glm::dvec3(points[p0]);
            glm::dvec3 ap = glm::dvec3(points[p2]) - glm::dvec3(points[p0]);
            glm::dvec3 cross = glm::cross(ab, ap);
            if (glm::dot(cross, cross) < epsilon * epsilon * glm::dot(ab, ab))
                return result; // All points are collinear
        }

        // Step 4: Find the point most distant from the plane of (p0, p1, p2)
        Plane triPlane = MakePlane(points[p0], points[p1], points[p2]);
        uint32_t p3 = FarthestFromPlane(points, allIndices, triPlane);

        // Check that p3 is not coplanar
        double p3Dist = std::abs(SignedDistance(triPlane, points[p3]));
        if (p3Dist < epsilon)
            return result; // All points are coplanar

        // Step 5: Orient the tetrahedron so all faces have outward normals.
        // If p3 is on the positive side of the triangle (p0, p1, p2), we need
        // to flip the winding of the base triangle so p3 is "inside".
        if (SignedDistance(triPlane, points[p3]) > 0.0)
        {
            // p3 is above (p0, p1, p2) — swap p0 and p1 to flip the base
            std::swap(p0, p1);
        }

        result.Indices = {p0, p1, p2, p3};
        result.Valid = true;
        return result;
    }

    // =========================================================================
    // Quickhull: main algorithm
    // =========================================================================

    std::optional<ConvexHullResult> Build(
        std::span<const glm::vec3> points,
        const ConvexHullParams& params)
    {
        const auto n = static_cast<uint32_t>(points.size());
        if (n < 4)
            return std::nullopt;

        const double eps = params.DistanceEpsilon;

        // --- Step 1: Find initial tetrahedron ---
        auto simplex = FindInitialSimplex(points, eps);
        if (!simplex.Valid)
            return std::nullopt;

        auto [i0, i1, i2, i3] = simplex.Indices;

        // Build 4 initial faces with outward normals.
        // Winding convention: vertices listed CCW when viewed from outside.
        // The tetrahedron (i0, i1, i2, i3) has i3 on the negative side of
        // triangle (i0, i1, i2). The four faces are:
        //   Face 0: (i0, i1, i2) — base, normal away from i3
        //   Face 1: (i0, i3, i1) — side
        //   Face 2: (i1, i3, i2) — side
        //   Face 3: (i0, i2, i3) — side

        std::vector<HullFace> faces;
        faces.reserve(256);

        auto addFace = [&](uint32_t a, uint32_t b, uint32_t c) -> std::size_t
        {
            HullFace f;
            f.Vertices = {a, b, c};
            f.FacePlane = MakePlane(points[a], points[b], points[c]);
            f.Deleted = false;
            faces.push_back(std::move(f));
            return faces.size() - 1;
        };

        std::size_t f0 = addFace(i0, i1, i2);
        std::size_t f1 = addFace(i0, i3, i1);
        std::size_t f2 = addFace(i1, i3, i2);
        std::size_t f3 = addFace(i0, i2, i3);

        // Verify outward orientation: centroid of tetrahedron should be on the
        // negative side of all faces.
        glm::vec3 centroid = (points[i0] + points[i1] + points[i2] + points[i3]) * 0.25f;
        for (std::size_t fi : {f0, f1, f2, f3})
        {
            if (SignedDistance(faces[fi].FacePlane, centroid) > 0.0)
            {
                // Flip the face
                std::swap(faces[fi].Vertices[0], faces[fi].Vertices[1]);
                faces[fi].FacePlane = MakePlane(
                    points[faces[fi].Vertices[0]],
                    points[faces[fi].Vertices[1]],
                    points[faces[fi].Vertices[2]]);
            }
        }

        // --- Step 2: Assign initial conflict lists ---
        std::unordered_set<uint32_t> simplexSet = {i0, i1, i2, i3};

        for (uint32_t pi = 0; pi < n; ++pi)
        {
            if (simplexSet.contains(pi))
                continue;

            double bestDist = -std::numeric_limits<double>::max();
            std::size_t bestFace = std::numeric_limits<std::size_t>::max();

            for (std::size_t fi = 0; fi < faces.size(); ++fi)
            {
                double dist = SignedDistance(faces[fi].FacePlane, points[pi]);
                if (dist > eps && dist > bestDist)
                {
                    bestDist = dist;
                    bestFace = fi;
                }
            }

            if (bestFace != std::numeric_limits<std::size_t>::max())
                faces[bestFace].ConflictPoints.push_back(pi);
        }

        // --- Step 3: Iterative hull expansion ---
        // Build an edge-to-face adjacency map.
        // Key: (min(v0,v1), max(v0,v1)) packed as uint64_t
        auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t
        {
            uint32_t lo = (a < b) ? a : b;
            uint32_t hi = (a < b) ? b : a;
            return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
        };

        // Map from edge key to the two face indices sharing that edge.
        // For a convex hull, each edge is shared by exactly 2 faces.
        std::unordered_map<uint64_t, std::array<std::size_t, 2>> edgeToFaces;
        edgeToFaces.reserve(faces.size() * 3);

        auto registerFaceEdges = [&](std::size_t fi)
        {
            const auto& verts = faces[fi].Vertices;
            for (int e = 0; e < 3; ++e)
            {
                uint32_t a = verts[e];
                uint32_t b = verts[(e + 1) % 3];
                uint64_t key = edgeKey(a, b);
                auto it = edgeToFaces.find(key);
                if (it == edgeToFaces.end())
                {
                    edgeToFaces[key] = {fi, std::numeric_limits<std::size_t>::max()};
                }
                else
                {
                    it->second[1] = fi;
                }
            }
        };

        auto unregisterFaceEdges = [&](std::size_t fi)
        {
            const auto& verts = faces[fi].Vertices;
            for (int e = 0; e < 3; ++e)
            {
                uint32_t a = verts[e];
                uint32_t b = verts[(e + 1) % 3];
                uint64_t key = edgeKey(a, b);
                auto it = edgeToFaces.find(key);
                if (it != edgeToFaces.end())
                {
                    if (it->second[0] == fi)
                        it->second[0] = it->second[1];
                    it->second[1] = std::numeric_limits<std::size_t>::max();
                    // If both slots are empty, remove the entry
                    if (it->second[0] == std::numeric_limits<std::size_t>::max())
                        edgeToFaces.erase(it);
                }
            }
        };

        for (std::size_t fi = 0; fi < faces.size(); ++fi)
            registerFaceEdges(fi);

        // Safety limit: each iteration processes one eye point. In the worst
        // case every input point becomes a hull vertex, so n iterations suffice.
        const std::size_t maxIterations = static_cast<std::size_t>(n) * 2;

        for (std::size_t iteration = 0; iteration < maxIterations; ++iteration)
        {
            // Find a face with a non-empty conflict list
            std::size_t seedFace = std::numeric_limits<std::size_t>::max();
            double bestEyeDist = -std::numeric_limits<double>::max();
            uint32_t eyePoint = 0;

            for (std::size_t fi = 0; fi < faces.size(); ++fi)
            {
                if (faces[fi].Deleted || faces[fi].ConflictPoints.empty())
                    continue;

                // Find the farthest point in this face's conflict list
                for (uint32_t pi : faces[fi].ConflictPoints)
                {
                    double dist = SignedDistance(faces[fi].FacePlane, points[pi]);
                    if (dist > bestEyeDist)
                    {
                        bestEyeDist = dist;
                        eyePoint = pi;
                        seedFace = fi;
                    }
                }
            }

            if (seedFace == std::numeric_limits<std::size_t>::max())
                break; // No more conflict points — hull is complete

            // --- BFS to find all visible faces from the eye point ---
            std::vector<std::size_t> visibleFaces;
            std::vector<bool> visited(faces.size(), false);

            {
                std::vector<std::size_t> stack;
                stack.push_back(seedFace);
                visited[seedFace] = true;

                while (!stack.empty())
                {
                    std::size_t fi = stack.back();
                    stack.pop_back();
                    visibleFaces.push_back(fi);

                    // Check neighbors through each edge
                    const auto& verts = faces[fi].Vertices;
                    for (int e = 0; e < 3; ++e)
                    {
                        uint32_t a = verts[e];
                        uint32_t b = verts[(e + 1) % 3];
                        uint64_t key = edgeKey(a, b);
                        auto it = edgeToFaces.find(key);
                        if (it == edgeToFaces.end())
                            continue;

                        for (std::size_t neighbor : it->second)
                        {
                            if (neighbor == std::numeric_limits<std::size_t>::max())
                                continue;
                            if (neighbor == fi || visited[neighbor])
                                continue;
                            if (faces[neighbor].Deleted)
                                continue;

                            double dist = SignedDistance(faces[neighbor].FacePlane,
                                                        points[eyePoint]);
                            if (dist > eps)
                            {
                                visited[neighbor] = true;
                                stack.push_back(neighbor);
                            }
                        }
                    }
                }
            }

            // --- Extract horizon edges ---
            // A horizon edge is an edge of a visible face whose neighbor
            // across that edge is NOT visible.
            std::vector<HorizonEdge> horizon;

            for (std::size_t fi : visibleFaces)
            {
                const auto& verts = faces[fi].Vertices;
                for (int e = 0; e < 3; ++e)
                {
                    uint32_t a = verts[e];
                    uint32_t b = verts[(e + 1) % 3];
                    uint64_t key = edgeKey(a, b);
                    auto it = edgeToFaces.find(key);
                    if (it == edgeToFaces.end())
                        continue;

                    // Find the neighbor face across this edge
                    std::size_t neighbor = std::numeric_limits<std::size_t>::max();
                    for (std::size_t adj : it->second)
                    {
                        if (adj != std::numeric_limits<std::size_t>::max() && adj != fi)
                        {
                            neighbor = adj;
                            break;
                        }
                    }

                    if (neighbor == std::numeric_limits<std::size_t>::max() ||
                        !visited[neighbor] || faces[neighbor].Deleted)
                    {
                        // This neighbor is not visible — this is a horizon edge.
                        // The edge direction must be reversed relative to the
                        // visible face so it's CCW from outside when connected
                        // to the eye point.
                        horizon.push_back(HorizonEdge{b, a, neighbor});
                    }
                }
            }

            if (horizon.empty())
                break; // Safety: no horizon means something went wrong

            // --- Order horizon edges into a loop ---
            // Build an adjacency from edge start vertex to edge index
            std::unordered_map<uint32_t, std::size_t> startToEdge;
            for (std::size_t i = 0; i < horizon.size(); ++i)
                startToEdge[horizon[i].V0] = i;

            std::vector<HorizonEdge> orderedHorizon;
            orderedHorizon.reserve(horizon.size());

            {
                orderedHorizon.push_back(horizon[0]);
                std::size_t safety = horizon.size() + 1;

                while (orderedHorizon.size() < horizon.size() && safety-- > 0)
                {
                    uint32_t nextStart = orderedHorizon.back().V1;
                    auto it = startToEdge.find(nextStart);
                    if (it == startToEdge.end())
                        break;
                    orderedHorizon.push_back(horizon[it->second]);
                }
            }

            if (orderedHorizon.size() != horizon.size())
            {
                // Horizon loop is broken — this shouldn't happen on valid
                // geometry. Bail out rather than produce garbage.
                break;
            }

            // --- Collect orphaned conflict points from visible faces ---
            std::vector<uint32_t> orphanedPoints;
            for (std::size_t fi : visibleFaces)
            {
                for (uint32_t pi : faces[fi].ConflictPoints)
                {
                    if (pi != eyePoint)
                        orphanedPoints.push_back(pi);
                }
            }

            // --- Delete visible faces ---
            for (std::size_t fi : visibleFaces)
            {
                unregisterFaceEdges(fi);
                faces[fi].Deleted = true;
                faces[fi].ConflictPoints.clear();
            }

            // --- Create new faces connecting eye point to horizon edges ---
            std::vector<std::size_t> newFaceIndices;
            newFaceIndices.reserve(orderedHorizon.size());

            for (const auto& he : orderedHorizon)
            {
                std::size_t fi = addFace(he.V0, he.V1, eyePoint);
                newFaceIndices.push_back(fi);

                // Verify outward orientation
                if (SignedDistance(faces[fi].FacePlane, centroid) > eps)
                {
                    std::swap(faces[fi].Vertices[0], faces[fi].Vertices[1]);
                    faces[fi].FacePlane = MakePlane(
                        points[faces[fi].Vertices[0]],
                        points[faces[fi].Vertices[1]],
                        points[faces[fi].Vertices[2]]);
                }

                registerFaceEdges(fi);
            }

            // --- Redistribute orphaned points to new faces ---
            for (uint32_t pi : orphanedPoints)
            {
                double bestDist = -std::numeric_limits<double>::max();
                std::size_t bestFace = std::numeric_limits<std::size_t>::max();

                for (std::size_t fi : newFaceIndices)
                {
                    double dist = SignedDistance(faces[fi].FacePlane, points[pi]);
                    if (dist > eps && dist > bestDist)
                    {
                        bestDist = dist;
                        bestFace = fi;
                    }
                }

                if (bestFace != std::numeric_limits<std::size_t>::max())
                    faces[bestFace].ConflictPoints.push_back(pi);
                // else: point is now interior — discard
            }
        }

        // --- Step 4: Extract result ---
        ConvexHullResult result;
        result.InputPointCount = n;

        // Collect unique vertices from non-deleted faces
        std::unordered_map<uint32_t, uint32_t> vertexRemap; // old index → new index

        for (const auto& face : faces)
        {
            if (face.Deleted)
                continue;
            for (uint32_t vi : face.Vertices)
            {
                if (!vertexRemap.contains(vi))
                {
                    auto newIdx = static_cast<uint32_t>(result.Hull.Vertices.size());
                    vertexRemap[vi] = newIdx;
                    result.Hull.Vertices.push_back(points[vi]);
                }
            }
        }

        result.HullVertexCount = result.Hull.Vertices.size();

        // Count faces
        std::size_t faceCount = 0;
        for (const auto& face : faces)
        {
            if (!face.Deleted)
                ++faceCount;
        }
        result.HullFaceCount = faceCount;

        // Euler's formula for convex polyhedra: V - E + F = 2
        // E = V + F - 2
        if (result.HullVertexCount >= 3 && result.HullFaceCount >= 4)
            result.HullEdgeCount = result.HullVertexCount + result.HullFaceCount - 2;
        else
            result.HullEdgeCount = 0;

        result.InteriorPointCount = n - result.HullVertexCount;

        // Compute H-Rep (face planes)
        if (params.ComputePlanes)
        {
            result.Hull.Planes.reserve(result.HullFaceCount);
            for (const auto& face : faces)
            {
                if (face.Deleted)
                    continue;
                result.Hull.Planes.push_back(face.FacePlane);
            }
        }

        // Build Halfedge::Mesh representation
        if (params.BuildMesh)
        {
            result.Mesh.Reserve(result.HullVertexCount,
                                result.HullEdgeCount,
                                result.HullFaceCount);

            // Add vertices
            std::vector<VertexHandle> vertexHandles(result.HullVertexCount);
            for (std::size_t i = 0; i < result.HullVertexCount; ++i)
                vertexHandles[i] = result.Mesh.AddVertex(result.Hull.Vertices[i]);

            // Add faces
            for (const auto& face : faces)
            {
                if (face.Deleted)
                    continue;

                VertexHandle v0 = vertexHandles[vertexRemap[face.Vertices[0]]];
                VertexHandle v1 = vertexHandles[vertexRemap[face.Vertices[1]]];
                VertexHandle v2 = vertexHandles[vertexRemap[face.Vertices[2]]];

                (void)result.Mesh.AddTriangle(v0, v1, v2);
            }
        }

        return result;
    }

    std::optional<ConvexHullResult> BuildFromMesh(
        const Halfedge::Mesh& mesh,
        const ConvexHullParams& params)
    {
        if (mesh.IsEmpty())
            return std::nullopt;

        // Extract non-deleted vertex positions
        std::vector<glm::vec3> positions;
        positions.reserve(mesh.VertexCount());

        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            VertexHandle v{static_cast<PropertyIndex>(i)};
            if (!mesh.IsDeleted(v))
                positions.push_back(mesh.Position(v));
        }

        if (positions.size() < 4)
            return std::nullopt;

        return Build(positions, params);
    }

} // namespace Geometry::ConvexHullBuilder
