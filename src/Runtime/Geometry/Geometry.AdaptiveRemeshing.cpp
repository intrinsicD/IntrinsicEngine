module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:AdaptiveRemeshing.Impl;

import :AdaptiveRemeshing;
import :Properties;
import :HalfedgeMesh;
import :Curvature;

namespace Geometry::AdaptiveRemeshing
{
    // =========================================================================
    // Internal helpers (independent copies — isotropic remeshing helpers are
    // static/unexported and need per-vertex local thresholds here)
    // =========================================================================

    static double EdgeLengthSq(const Halfedge::Mesh& mesh, EdgeHandle e)
    {
        HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
        glm::vec3 a = mesh.Position(mesh.FromVertex(h));
        glm::vec3 b = mesh.Position(mesh.ToVertex(h));
        glm::vec3 d = b - a;
        return static_cast<double>(glm::dot(d, d));
    }

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

    static int TargetValence(const Halfedge::Mesh& mesh, VertexHandle v)
    {
        return mesh.IsBoundary(v) ? 4 : 6;
    }

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
                n += FaceNormal(mesh, f);
            }
            h = mesh.CWRotatedHalfedge(h);
            if (++safety > 100) break;
        } while (h != hStart);

        float len = glm::length(n);
        return (len > 1e-8f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Per-edge local target = average of endpoint sizing fields
    static double LocalTarget(
        const std::vector<double>& sizing,
        const Halfedge::Mesh& mesh,
        EdgeHandle e)
    {
        HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
        std::size_t v0 = mesh.FromVertex(h).Index;
        std::size_t v1 = mesh.ToVertex(h).Index;
        return 0.5 * (sizing[v0] + sizing[v1]);
    }

    // =========================================================================
    // Compute per-vertex sizing field from curvature
    // =========================================================================
    static void ComputeSizingField(
        const Halfedge::Mesh& mesh,
        double baseLength,
        double alpha,
        double minLen,
        double maxLen,
        std::vector<double>& sizing)
    {
        sizing.resize(mesh.VerticesSize(), baseLength);

        if (alpha < 1e-12)
        {
            // Zero adaptation → uniform sizing
            std::fill(sizing.begin(), sizing.end(),
                      std::clamp(baseLength, minLen, maxLen));
            return;
        }

        auto curvField = Curvature::ComputeCurvature(mesh);

        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
            {
                sizing[vi] = baseLength;
                continue;
            }

            double absH = std::abs(curvField.Vertices[vi].MeanCurvature);
            double target = baseLength / (1.0 + alpha * absH);
            sizing[vi] = std::clamp(target, minLen, maxLen);
        }
    }

    // =========================================================================
    // Step 1: Split long edges (adaptive thresholds)
    // =========================================================================
    static std::size_t SplitLongEdges(
        Halfedge::Mesh& mesh,
        std::vector<double>& sizing,
        double minLen,
        double maxLen)
    {
        std::size_t splitCount = 0;

        // Collect edges to split (don't iterate while modifying)
        std::vector<EdgeHandle> toSplit;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;

            double localT = LocalTarget(sizing, mesh, e);
            double maxLenSq = (4.0 / 3.0) * localT;
            maxLenSq *= maxLenSq;

            if (EdgeLengthSq(mesh, e) > maxLenSq)
                toSplit.push_back(e);
        }

        for (auto e : toSplit)
        {
            if (mesh.IsDeleted(e)) continue;

            double localT = LocalTarget(sizing, mesh, e);
            double threshold = (4.0 / 3.0) * localT;
            if (EdgeLengthSq(mesh, e) <= threshold * threshold) continue;

            HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
            std::size_t v0 = mesh.FromVertex(h).Index;
            std::size_t v1 = mesh.ToVertex(h).Index;

            glm::vec3 mid = 0.5f * (mesh.Position(mesh.FromVertex(h)) +
                                     mesh.Position(mesh.ToVertex(h)));
            (void)mesh.Split(e, mid);
            ++splitCount;

            // New vertex gets average sizing of the two endpoints.
            // After split, new vertices are appended at the end.
            if (sizing.size() < mesh.VerticesSize())
            {
                sizing.resize(mesh.VerticesSize(), 0.0);
                // The newly created vertex is at index mesh.VerticesSize()-1
                // (or potentially several new vertices from the split).
                // Assign average sizing to all new vertices.
                double avgSizing = 0.5 * (sizing[v0] + sizing[v1]);
                avgSizing = std::clamp(avgSizing, minLen, maxLen);
                for (std::size_t i = sizing.size() - (mesh.VerticesSize() - sizing.size() + (mesh.VerticesSize() - sizing.size()));
                     i < mesh.VerticesSize(); ++i)
                {
                    if (sizing[i] < 1e-15)
                        sizing[i] = avgSizing;
                }
            }
        }

        // Ensure sizing is properly sized after all splits
        if (sizing.size() < mesh.VerticesSize())
        {
            double defaultSize = (minLen + maxLen) * 0.5;
            sizing.resize(mesh.VerticesSize(), defaultSize);
        }

        return splitCount;
    }

    // =========================================================================
    // Step 2: Collapse short edges (adaptive thresholds)
    // =========================================================================
    static std::size_t CollapseShortEdges(
        Halfedge::Mesh& mesh,
        const std::vector<double>& sizing,
        bool preserveBoundary)
    {
        std::size_t collapseCount = 0;

        std::vector<EdgeHandle> toCollapse;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;

            double localT = LocalTarget(sizing, mesh, e);
            double minThreshold = (4.0 / 5.0) * localT;
            if (EdgeLengthSq(mesh, e) < minThreshold * minThreshold)
                toCollapse.push_back(e);
        }

        for (auto e : toCollapse)
        {
            if (mesh.IsDeleted(e)) continue;

            double localT = LocalTarget(sizing, mesh, e);
            double minThreshold = (4.0 / 5.0) * localT;
            if (EdgeLengthSq(mesh, e) >= minThreshold * minThreshold) continue;

            if (preserveBoundary && mesh.IsBoundary(e))
                continue;

            HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
            VertexHandle v0 = mesh.FromVertex(h);
            VertexHandle v1 = mesh.ToVertex(h);

            if (preserveBoundary && (mesh.IsBoundary(v0) || mesh.IsBoundary(v1)))
                continue;

            if (!mesh.IsCollapseOk(e))
                continue;

            // Check that collapse won't create edges longer than the local max
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
                        double distSq = static_cast<double>(glm::dot(d, d));
                        // Use max of endpoint targets as the upper bound
                        double maxT = std::max(sizing[v0.Index], sizing[v1.Index]);
                        double upperBound = (4.0 / 3.0) * maxT;
                        if (distSq > upperBound * upperBound)
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

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);

            VertexHandle a = mesh.FromVertex(h0);
            VertexHandle b = mesh.ToVertex(h0);
            VertexHandle c = mesh.ToVertex(mesh.NextHalfedge(h0));
            VertexHandle d = mesh.ToVertex(mesh.NextHalfedge(h1));

            if (preserveBoundary)
            {
                if (mesh.IsBoundary(a) || mesh.IsBoundary(b) ||
                    mesh.IsBoundary(c) || mesh.IsBoundary(d))
                    continue;
            }

            auto dev = [&](VertexHandle v, int adjust) -> int {
                int val = static_cast<int>(mesh.Valence(v)) + adjust;
                int target = TargetValence(mesh, v);
                return std::abs(val - target);
            };

            int devBefore = dev(a, 0) + dev(b, 0) + dev(c, 0) + dev(d, 0);
            int devAfter  = dev(a, -1) + dev(b, -1) + dev(c, +1) + dev(d, +1);

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

            // Uniform Laplacian
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

            // Project onto tangent plane
            glm::vec3 n = VertexNormal(mesh, vh);
            glm::vec3 tangentialDisp = displacement - glm::dot(displacement, n) * n;

            newPositions[vi] = p + static_cast<float>(lambda) * tangentialDisp;
        }

        // Apply
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
            mesh.Position(vh) = newPositions[vi];
        }
    }

    // =========================================================================
    // Main adaptive remeshing function
    // =========================================================================

    std::optional<AdaptiveRemeshingResult> AdaptiveRemesh(
        Halfedge::Mesh& mesh,
        const AdaptiveRemeshingParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() < 2)
            return std::nullopt;

        double baseLength = MeanEdgeLength(mesh);
        if (baseLength <= 0.0)
            return std::nullopt;

        double minLen = params.MinEdgeLength;
        double maxLen = params.MaxEdgeLength;

        if (minLen <= 0.0)
            minLen = baseLength * 0.1;
        if (maxLen <= 0.0)
            maxLen = baseLength * 5.0;

        // Ensure min <= max
        if (minLen > maxLen)
            std::swap(minLen, maxLen);

        AdaptiveRemeshingResult result;
        std::vector<double> sizing;

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            // Step 0: Compute sizing field from curvature
            ComputeSizingField(mesh, baseLength, params.CurvatureAdaptation,
                               minLen, maxLen, sizing);

            // Step 1: Split long edges
            result.SplitCount += SplitLongEdges(mesh, sizing, minLen, maxLen);

            // Step 2: Collapse short edges
            result.CollapseCount += CollapseShortEdges(mesh, sizing, params.PreserveBoundary);

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

} // namespace Geometry::AdaptiveRemeshing
