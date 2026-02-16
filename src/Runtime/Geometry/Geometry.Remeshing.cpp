module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Remeshing.Impl;

import :Remeshing;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::Remeshing
{
    // Compute squared edge length
    static double EdgeLengthSq(const Halfedge::Mesh& mesh, EdgeHandle e)
    {
        HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
        glm::vec3 a = mesh.Position(mesh.FromVertex(h));
        glm::vec3 b = mesh.Position(mesh.ToVertex(h));
        glm::vec3 d = b - a;
        return static_cast<double>(glm::dot(d, d));
    }

    // Compute mean edge length
    static double MeanEdgeLength(const Halfedge::Mesh& mesh)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;
            sum += std::sqrt(EdgeLengthSq(mesh, e));
            ++count;
        }
        return (count > 0) ? (sum / static_cast<double>(count)) : 0.0;
    }

    // Target valence: 6 for interior, 4 for boundary
    static int TargetValence(const Halfedge::Mesh& mesh, VertexHandle v)
    {
        return mesh.IsBoundary(v) ? 4 : 6;
    }

    // Compute valence deviation from target for four vertices affected by a flip
    static int ValenceDeviation(const Halfedge::Mesh& mesh,
                                VertexHandle a, VertexHandle b,
                                VertexHandle c, VertexHandle d)
    {
        auto dev = [&](VertexHandle v) -> int {
            int val = static_cast<int>(mesh.Valence(v));
            int target = TargetValence(mesh, v);
            return std::abs(val - target);
        };
        return dev(a) + dev(b) + dev(c) + dev(d);
    }

    // Compute face normal
    static glm::vec3 FaceNormal(const Halfedge::Mesh& mesh, FaceHandle f)
    {
        HalfedgeHandle h0 = mesh.Halfedge(f);
        HalfedgeHandle h1 = mesh.NextHalfedge(h0);
        HalfedgeHandle h2 = mesh.NextHalfedge(h1);

        glm::vec3 a = mesh.Position(mesh.ToVertex(h0));
        glm::vec3 b = mesh.Position(mesh.ToVertex(h1));
        glm::vec3 c = mesh.Position(mesh.ToVertex(h2));

        return glm::cross(b - a, c - a);
    }

    // Compute approximate vertex normal (area-weighted face normals)
    static glm::vec3 VertexNormal(const Halfedge::Mesh& mesh, VertexHandle v)
    {
        glm::vec3 n(0.0f);
        HalfedgeHandle hStart = mesh.Halfedge(v);
        HalfedgeHandle h = hStart;
        std::size_t safety = 0;
        do
        {
            FaceHandle f = mesh.Face(h);
            if (f.IsValid() && !mesh.IsDeleted(f))
            {
                n += FaceNormal(mesh, f); // Already area-weighted (cross product)
            }
            h = mesh.CWRotatedHalfedge(h);
            if (++safety > 100) break;
        } while (h != hStart);

        float len = glm::length(n);
        return (len > 1e-8f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // =========================================================================
    // Step 1: Split long edges
    // =========================================================================
    static std::size_t SplitLongEdges(Halfedge::Mesh& mesh, double maxLenSq, bool /*preserveBoundary*/)
    {
        std::size_t splitCount = 0;
        // Collect edges to split (don't iterate while modifying)
        std::vector<EdgeHandle> toSplit;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;
            if (EdgeLengthSq(mesh, e) > maxLenSq)
                toSplit.push_back(e);
        }

        for (auto e : toSplit)
        {
            if (mesh.IsDeleted(e)) continue;
            if (EdgeLengthSq(mesh, e) <= maxLenSq) continue;

            HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
            glm::vec3 mid = 0.5f * (mesh.Position(mesh.FromVertex(h)) +
                                     mesh.Position(mesh.ToVertex(h)));
            (void)mesh.Split(e, mid);
            ++splitCount;
        }

        return splitCount;
    }

    // =========================================================================
    // Step 2: Collapse short edges
    // =========================================================================
    static std::size_t CollapseShortEdges(Halfedge::Mesh& mesh, double minLenSq,
                                          double maxLenSq, bool preserveBoundary)
    {
        std::size_t collapseCount = 0;
        // Collect edges to collapse
        std::vector<EdgeHandle> toCollapse;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;
            if (EdgeLengthSq(mesh, e) < minLenSq)
                toCollapse.push_back(e);
        }

        for (auto e : toCollapse)
        {
            if (mesh.IsDeleted(e)) continue;
            if (EdgeLengthSq(mesh, e) >= minLenSq) continue;

            // Skip boundary edges if preserving boundary
            if (preserveBoundary && mesh.IsBoundary(e))
                continue;

            // Skip if either endpoint is a boundary vertex and we're preserving
            HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
            VertexHandle v0 = mesh.FromVertex(h);
            VertexHandle v1 = mesh.ToVertex(h);

            if (preserveBoundary && (mesh.IsBoundary(v0) || mesh.IsBoundary(v1)))
                continue;

            if (!mesh.IsCollapseOk(e))
                continue;

            // Check that the collapse won't create edges longer than maxLen.
            // The surviving vertex will be at the midpoint; check distances
            // to all neighbors of both endpoints.
            glm::vec3 mid = 0.5f * (mesh.Position(v0) + mesh.Position(v1));
            bool tooLong = false;

            auto checkNeighbors = [&](VertexHandle v)
            {
                HalfedgeHandle hStart = mesh.Halfedge(v);
                HalfedgeHandle hc = hStart;
                std::size_t safety = 0;
                do
                {
                    VertexHandle vn = mesh.ToVertex(hc);
                    if (vn != v0 && vn != v1)
                    {
                        glm::vec3 d = mesh.Position(vn) - mid;
                        if (static_cast<double>(glm::dot(d, d)) > maxLenSq)
                        {
                            tooLong = true;
                            return;
                        }
                    }
                    hc = mesh.CWRotatedHalfedge(hc);
                    if (++safety > 100) break;
                } while (hc != hStart);
            };

            checkNeighbors(v0);
            if (!tooLong) checkNeighbors(v1);
            if (tooLong) continue;

            (void)mesh.Collapse(e, mid);
            ++collapseCount;
        }

        return collapseCount;
    }

    // =========================================================================
    // Step 3: Equalize valence via edge flips
    // =========================================================================
    static std::size_t EqualizeValence(Halfedge::Mesh& mesh, bool preserveBoundary)
    {
        std::size_t flipCount = 0;

        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;
            if (mesh.IsBoundary(e)) continue;

            if (!mesh.IsFlipOk(e))
                continue;

            // The four vertices involved in a potential flip
            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);

            VertexHandle a = mesh.FromVertex(h0); // endpoint
            VertexHandle b = mesh.ToVertex(h0);   // endpoint
            VertexHandle c = mesh.ToVertex(mesh.NextHalfedge(h0)); // opposite in face 0
            VertexHandle d = mesh.ToVertex(mesh.NextHalfedge(h1)); // opposite in face 1

            if (preserveBoundary)
            {
                if (mesh.IsBoundary(a) || mesh.IsBoundary(b) ||
                    mesh.IsBoundary(c) || mesh.IsBoundary(d))
                    continue;
            }

            // Before flip: edge connects a-b
            int devBefore = ValenceDeviation(mesh, a, b, c, d);

            // After flip: edge would connect c-d
            // Valence changes: a loses 1, b loses 1, c gains 1, d gains 1
            auto devAfterFlip = [&]() -> int {
                auto devSimulated = [&](VertexHandle v, int adjust) -> int {
                    int val = static_cast<int>(mesh.Valence(v)) + adjust;
                    int target = TargetValence(mesh, v);
                    return std::abs(val - target);
                };
                return devSimulated(a, -1) + devSimulated(b, -1) +
                       devSimulated(c, +1) + devSimulated(d, +1);
            };

            int devAfter = devAfterFlip();

            // Only flip if it improves valence by at least 1
            if (devAfter < devBefore)
            {
                (void)mesh.Flip(e);
                ++flipCount;
            }
        }

        return flipCount;
    }

    // =========================================================================
    // Step 4: Tangential Laplacian smoothing
    // =========================================================================
    static void TangentialSmooth(Halfedge::Mesh& mesh, double lambda, bool preserveBoundary)
    {
        const std::size_t nV = mesh.VerticesSize();
        std::vector<glm::vec3> newPositions(nV);

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
            {
                newPositions[vi] = mesh.Position(vh);
                continue;
            }

            if (preserveBoundary && mesh.IsBoundary(vh))
            {
                newPositions[vi] = mesh.Position(vh);
                continue;
            }

            glm::vec3 p = mesh.Position(vh);

            // Compute uniform Laplacian displacement
            glm::vec3 centroid(0.0f);
            std::size_t count = 0;
            HalfedgeHandle hStart = mesh.Halfedge(vh);
            HalfedgeHandle h = hStart;
            std::size_t safety = 0;
            do
            {
                centroid += mesh.Position(mesh.ToVertex(h));
                ++count;
                h = mesh.CWRotatedHalfedge(h);
                if (++safety > 100) break;
            } while (h != hStart);

            if (count == 0)
            {
                newPositions[vi] = p;
                continue;
            }

            centroid /= static_cast<float>(count);
            glm::vec3 displacement = centroid - p;

            // Project displacement onto tangent plane
            glm::vec3 n = VertexNormal(mesh, vh);
            glm::vec3 tangentialDisp = displacement - glm::dot(displacement, n) * n;

            newPositions[vi] = p + static_cast<float>(lambda) * tangentialDisp;
        }

        // Apply new positions
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
            mesh.Position(vh) = newPositions[vi];
        }
    }

    // =========================================================================
    // Main remeshing function
    // =========================================================================

    std::optional<RemeshingResult> Remesh(
        Halfedge::Mesh& mesh,
        const RemeshingParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() < 2)
            return std::nullopt;

        // Determine target edge length
        double targetLen = params.TargetLength;
        if (targetLen <= 0.0)
            targetLen = MeanEdgeLength(mesh);

        if (targetLen <= 0.0)
            return std::nullopt;

        // Thresholds: split if > 4/3 * target, collapse if < 4/5 * target
        double maxLen = (4.0 / 3.0) * targetLen;
        double minLen = (4.0 / 5.0) * targetLen;
        double maxLenSq = maxLen * maxLen;
        double minLenSq = minLen * minLen;

        RemeshingResult result;

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            // Step 1: Split long edges
            result.SplitCount += SplitLongEdges(mesh, maxLenSq, params.PreserveBoundary);

            // Step 2: Collapse short edges
            result.CollapseCount += CollapseShortEdges(mesh, minLenSq, maxLenSq,
                                                       params.PreserveBoundary);

            // Step 3: Equalize valence
            result.FlipCount += EqualizeValence(mesh, params.PreserveBoundary);

            // Step 4: Tangential smoothing
            TangentialSmooth(mesh, params.SmoothingLambda, params.PreserveBoundary);

            result.IterationsPerformed = iter + 1;
        }

        result.FinalVertexCount = mesh.VertexCount();
        result.FinalEdgeCount = mesh.EdgeCount();
        result.FinalFaceCount = mesh.FaceCount();

        return result;
    }

} // namespace Geometry::Remeshing
