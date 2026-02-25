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
import :MeshUtils;

namespace Geometry::Remeshing
{
    using MeshUtils::EdgeLengthSq;
    using MeshUtils::MeanEdgeLength;
    using MeshUtils::EqualizeValenceByEdgeFlip;

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
            result.FlipCount += EqualizeValenceByEdgeFlip(mesh, params.PreserveBoundary);

            // Step 4: Tangential smoothing
            MeshUtils::TangentialSmooth(mesh, params.SmoothingLambda, params.PreserveBoundary);

            result.IterationsPerformed = iter + 1;
        }

        result.FinalVertexCount = mesh.VertexCount();
        result.FinalEdgeCount = mesh.EdgeCount();
        result.FinalFaceCount = mesh.FaceCount();

        return result;
    }

} // namespace Geometry::Remeshing
