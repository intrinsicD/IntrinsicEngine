module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <queue>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:MeshRepair.Impl;

import :MeshRepair;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::MeshRepair
{
    // =========================================================================
    // Boundary Loop Detection
    // =========================================================================

    std::vector<BoundaryLoop> FindBoundaryLoops(const Halfedge::Mesh& mesh)
    {
        std::vector<BoundaryLoop> loops;

        const std::size_t nH = mesh.HalfedgesSize();
        std::vector<bool> visited(nH, false);

        for (std::size_t hi = 0; hi < nH; ++hi)
        {
            HalfedgeHandle h{static_cast<PropertyIndex>(hi)};
            if (visited[hi]) continue;
            if (mesh.IsDeleted(mesh.Edge(h))) continue;
            if (!mesh.IsBoundary(h)) continue;

            // Found an unvisited boundary halfedge — trace the loop
            BoundaryLoop loop;
            HalfedgeHandle hCurr = h;
            std::size_t safety = 0;
            do
            {
                visited[hCurr.Index] = true;
                loop.Halfedges.push_back(hCurr);
                loop.Vertices.push_back(mesh.FromVertex(hCurr));

                // Walk to next boundary halfedge in the loop
                hCurr = mesh.NextHalfedge(hCurr);
                if (++safety > nH) break; // safety limit
            } while (hCurr != h);

            if (!loop.Vertices.empty())
                loops.push_back(std::move(loop));
        }

        return loops;
    }

    // =========================================================================
    // Hole Filling — Advancing front ear-clipping
    // =========================================================================
    //
    // For a boundary loop of vertices [v0, v1, ..., vn-1], we iteratively
    // find the "best ear" (a vertex whose two boundary neighbors form a
    // valid triangle) and fill it. The best ear is the one with the
    // smallest interior angle, producing a triangulation that stays close
    // to flat.

    // Compute the angle at vertex vb in triangle (va, vb, vc)
    static float TriangleAngle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        glm::vec3 ba = a - b;
        glm::vec3 bc = c - b;
        float lenBA = glm::length(ba);
        float lenBC = glm::length(bc);
        if (lenBA < 1e-12f || lenBC < 1e-12f)
            return static_cast<float>(std::numbers::pi); // degenerate

        float cosAngle = glm::dot(ba, bc) / (lenBA * lenBC);
        return std::acos(std::clamp(cosAngle, -1.0f, 1.0f));
    }

    static bool FillSingleHole(Halfedge::Mesh& mesh, const BoundaryLoop& loop)
    {
        // Work with a mutable copy of the vertex list
        std::vector<VertexHandle> verts = loop.Vertices;

        // Iteratively clip ears
        while (verts.size() > 3)
        {
            const std::size_t n = verts.size();

            // Find the best ear (smallest angle at the ear vertex)
            float bestAngle = std::numeric_limits<float>::max();
            std::size_t bestIdx = 0;

            for (std::size_t i = 0; i < n; ++i)
            {
                std::size_t iPrev = (i + n - 1) % n;
                std::size_t iNext = (i + 1) % n;

                glm::vec3 pPrev = mesh.Position(verts[iPrev]);
                glm::vec3 pCurr = mesh.Position(verts[i]);
                glm::vec3 pNext = mesh.Position(verts[iNext]);

                float angle = TriangleAngle(pPrev, pCurr, pNext);
                if (angle < bestAngle)
                {
                    bestAngle = angle;
                    bestIdx = i;
                }
            }

            // Create triangle at the best ear
            std::size_t iPrev = (bestIdx + verts.size() - 1) % verts.size();
            std::size_t iNext = (bestIdx + 1) % verts.size();

            // Note: boundary halfedges go in the opposite direction to face
            // halfedges, so the winding for the fill triangle should connect
            // v[iNext] -> v[bestIdx] -> v[iPrev] for the fill face to be
            // compatible with the existing mesh orientation.
            auto faceOpt = mesh.AddTriangle(verts[iPrev], verts[bestIdx], verts[iNext]);
            if (!faceOpt.has_value())
            {
                // If adding failed, try the reverse winding
                faceOpt = mesh.AddTriangle(verts[iNext], verts[bestIdx], verts[iPrev]);
                if (!faceOpt.has_value())
                    return false; // Give up on this hole
            }

            // Remove the ear vertex from the loop
            verts.erase(verts.begin() + static_cast<std::ptrdiff_t>(bestIdx));
        }

        // Fill the last triangle
        if (verts.size() == 3)
        {
            auto faceOpt = mesh.AddTriangle(verts[0], verts[1], verts[2]);
            if (!faceOpt.has_value())
            {
                faceOpt = mesh.AddTriangle(verts[2], verts[1], verts[0]);
                if (!faceOpt.has_value())
                    return false;
            }
        }

        return true;
    }

    std::optional<HoleFillingResult> FillHoles(
        Halfedge::Mesh& mesh,
        const HoleFillingParams& params)
    {
        if (mesh.IsEmpty())
            return std::nullopt;

        HoleFillingResult result;

        auto loops = FindBoundaryLoops(mesh);
        result.HolesDetected = loops.size();

        if (loops.empty())
            return result; // No holes, but that's a valid result

        for (const auto& loop : loops)
        {
            if (loop.Vertices.size() > params.MaxLoopSize)
            {
                ++result.HolesSkipped;
                continue;
            }

            if (loop.Vertices.size() < 3)
            {
                ++result.HolesSkipped;
                continue;
            }

            std::size_t facesBefore = mesh.FaceCount();

            if (FillSingleHole(mesh, loop))
            {
                ++result.HolesFilled;
                result.TrianglesAdded += mesh.FaceCount() - facesBefore;
            }
            else
            {
                ++result.HolesSkipped;
            }
        }

        return result;
    }

    // =========================================================================
    // Degenerate Triangle Removal
    // =========================================================================

    std::optional<DegenerateRemovalResult> RemoveDegenerateFaces(
        Halfedge::Mesh& mesh,
        const DegenerateRemovalParams& params)
    {
        if (mesh.IsEmpty())
            return std::nullopt;

        DegenerateRemovalResult result;

        // Collect degenerate faces first, then delete (modifying while
        // iterating over connectivity is unsafe)
        std::vector<FaceHandle> toDelete;

        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;

            // Compute face area using the cross product of two edges
            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);

            VertexHandle v0 = mesh.FromVertex(h0);
            VertexHandle v1 = mesh.ToVertex(h0);
            VertexHandle v2 = mesh.ToVertex(h1);

            glm::vec3 p0 = mesh.Position(v0);
            glm::vec3 p1 = mesh.Position(v1);
            glm::vec3 p2 = mesh.Position(v2);

            glm::vec3 cross = glm::cross(p1 - p0, p2 - p0);
            float area = 0.5f * glm::length(cross);

            if (area < params.AreaThreshold)
            {
                ++result.DegenerateFacesFound;
                toDelete.push_back(fh);
            }
        }

        // Delete degenerate faces
        for (FaceHandle fh : toDelete)
        {
            if (!mesh.IsDeleted(fh))
            {
                mesh.DeleteFace(fh);
                ++result.FacesRemoved;
            }
        }

        if (result.FacesRemoved > 0)
            mesh.GarbageCollection();

        return result;
    }

    // =========================================================================
    // Consistent Face Orientation
    // =========================================================================
    //
    // BFS-based orientation propagation. For two adjacent faces sharing an
    // edge, consistent orientation means the shared edge's halfedges point
    // in opposite directions (one face traverses the edge va->vb, the other
    // vb->va). If both faces traverse in the same direction, one needs to
    // be flipped.
    //
    // "Flipping" a face means reversing its halfedge cycle: for a triangle
    // (v0, v1, v2) with halfedges h0->h1->h2, we reverse the next/prev
    // pointers so the winding becomes (v0, v2, v1).

    [[maybe_unused]] static void FlipFaceWinding(Halfedge::Mesh& mesh, FaceHandle f)
    {
        // Collect halfedges of this face
        std::vector<HalfedgeHandle> faceHE;
        faceHE.reserve(4);

        HalfedgeHandle hStart = mesh.Halfedge(f);
        HalfedgeHandle h = hStart;
        std::size_t safety = 0;
        do
        {
            faceHE.push_back(h);
            h = mesh.NextHalfedge(h);
            if (++safety > 1000) break;
        } while (h != hStart);

        const std::size_t n = faceHE.size();
        if (n < 3) return;

        // Reverse the ToVertex assignments and next/prev pointers.
        // For each halfedge in the face, swap its to-vertex with the
        // next halfedge's to-vertex (effectively reversing winding).

        // First, collect the current to-vertices in order
        std::vector<VertexHandle> toVerts(n);
        for (std::size_t i = 0; i < n; ++i)
            toVerts[i] = mesh.ToVertex(faceHE[i]);

        // Reverse: halfedge[i].to = toVerts[(n-1)-i] for reversal,
        // but we need to preserve the halfedge-edge relationship.
        // Instead of reassigning vertices (which would break the
        // halfedge-edge pairing), reverse the next/prev chain.
        //
        // The simplest correct approach: reverse next pointers.
        // Old chain: h[0] -> h[1] -> h[2] -> h[0]
        // New chain: h[0] -> h[n-1] -> h[n-2] -> ... -> h[1] -> h[0]
        // But this changes the vertex traversal order.
        //
        // Actually the correct approach for a halfedge mesh is to swap
        // the vertex assignments of each halfedge pair (h and its opposite).
        // But that changes the edge semantics globally. We can't easily
        // flip a single face in a halfedge mesh without re-stitching.
        //
        // The practical approach used by most mesh libraries:
        // Just reverse the next-pointer chain within the face.
        for (std::size_t i = 0; i < n; ++i)
        {
            std::size_t iPrev = (i + n - 1) % n;
            mesh.SetNextHalfedge(faceHE[i], faceHE[iPrev]);
        }
    }

    std::optional<OrientationResult> MakeConsistentOrientation(Halfedge::Mesh& mesh)
    {
        if (mesh.IsEmpty())
            return std::nullopt;

        OrientationResult result;

        const std::size_t nF = mesh.FacesSize();
        std::vector<bool> visited(nF, false);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle seed{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(seed) || visited[fi])
                continue;

            // BFS from this seed face
            ++result.ComponentCount;

            std::queue<FaceHandle> bfsQueue;
            bfsQueue.push(seed);
            visited[fi] = true;

            while (!bfsQueue.empty())
            {
                FaceHandle curr = bfsQueue.front();
                bfsQueue.pop();

                // Visit all neighbors through shared edges
                HalfedgeHandle hStart = mesh.Halfedge(curr);
                HalfedgeHandle h = hStart;
                std::size_t safety = 0;
                do
                {
                    HalfedgeHandle hOpp = mesh.OppositeHalfedge(h);
                    FaceHandle fAdj = mesh.Face(hOpp);

                    if (fAdj.IsValid() && !visited[fAdj.Index] && !mesh.IsDeleted(fAdj))
                    {
                        visited[fAdj.Index] = true;

                        // Check orientation consistency:
                        // For consistent orientation, the shared edge should be
                        // traversed in opposite directions by the two faces.
                        // h goes from A to B in face `curr`.
                        // hOpp goes from B to A.
                        // The adjacent face should traverse hOpp (B to A), which
                        // means hOpp should be part of fAdj's halfedge cycle.
                        // If hOpp is in fAdj's cycle, orientation is consistent.
                        //
                        // To check: h.ToVertex should equal hOpp.FromVertex
                        // and hOpp should have face = fAdj. This is guaranteed
                        // by construction. The inconsistency check is:
                        // If the adjacent face also traverses A->B (same direction),
                        // it needs flipping. We detect this by checking if a
                        // different halfedge of the adjacent face also goes A->B.
                        //
                        // Simpler check: for edge (A,B), face curr uses halfedge h (A->B).
                        // If fAdj uses hOpp (B->A), orientation is consistent.
                        // If fAdj uses a halfedge going A->B (which would be h itself),
                        // that's impossible since h belongs to curr. So in a valid
                        // halfedge mesh, orientation is always locally consistent
                        // by construction — adjacent faces always use opposite
                        // halfedges of a shared edge.
                        //
                        // Therefore, in a valid halfedge mesh, all faces within a
                        // connected component already have consistent orientation.
                        // Inconsistency only arises in non-manifold or corrupted meshes.

                        bfsQueue.push(fAdj);
                    }

                    h = mesh.NextHalfedge(h);
                    if (++safety > 1000) break;
                } while (h != hStart);
            }
        }

        result.WasConsistent = (result.FacesFlipped == 0);
        return result;
    }

    // =========================================================================
    // Combined Repair
    // =========================================================================

    std::optional<RepairResult> Repair(
        Halfedge::Mesh& mesh,
        const RepairParams& params)
    {
        if (mesh.IsEmpty())
            return std::nullopt;

        RepairResult result;

        // Step 1: Remove degenerate triangles
        if (params.RemoveDegenerates)
        {
            auto degResult = RemoveDegenerateFaces(mesh, params.DegenerateParams);
            if (degResult.has_value())
                result.DegenerateResult = *degResult;
        }

        // Step 2: Fix face orientation
        if (params.FixOrientation)
        {
            auto orientResult = MakeConsistentOrientation(mesh);
            if (orientResult.has_value())
                result.OrientResult = *orientResult;
        }

        // Step 3: Fill holes
        if (params.FillHoles)
        {
            auto holeResult = MeshRepair::FillHoles(mesh, params.HoleParams);
            if (holeResult.has_value())
                result.HoleResult = *holeResult;
        }

        return result;
    }

} // namespace Geometry::MeshRepair
