module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:CatmullClark.Impl;

import :CatmullClark;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::CatmullClark
{
    // Perform a single level of Catmull-Clark subdivision.
    static bool SubdivideOnce(const Halfedge::Mesh& input, Halfedge::Mesh& output)
    {
        const std::size_t nV = input.VerticesSize();
        const std::size_t nE = input.EdgesSize();
        const std::size_t nF = input.FacesSize();

        if (nV == 0 || nF == 0)
            return false;

        output.Clear();

        // =====================================================================
        // Phase 1: Compute face points (one per face)
        // =====================================================================
        // F_i = centroid of face i's vertices
        std::vector<glm::vec3> facePoints(nF, glm::vec3(0.0f));
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (input.IsDeleted(fh))
                continue;

            glm::vec3 sum(0.0f);
            std::size_t count = 0;
            HalfedgeHandle hStart = input.Halfedge(fh);
            HalfedgeHandle h = hStart;
            std::size_t safety = 0;
            do
            {
                sum += input.Position(input.ToVertex(h));
                ++count;
                h = input.NextHalfedge(h);
                if (++safety > 1000) break;
            } while (h != hStart);

            if (count > 0)
                facePoints[fi] = sum / static_cast<float>(count);
        }

        // =====================================================================
        // Phase 2: Compute edge points (one per edge)
        // =====================================================================
        // Interior: E = (v0 + v1 + F_left + F_right) / 4
        // Boundary: E = (v0 + v1) / 2
        std::vector<glm::vec3> edgePoints(nE, glm::vec3(0.0f));
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (input.IsDeleted(eh))
                continue;

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            HalfedgeHandle h1 = input.OppositeHalfedge(h0);

            VertexHandle v0 = input.FromVertex(h0);
            VertexHandle v1 = input.ToVertex(h0);
            glm::vec3 p0 = input.Position(v0);
            glm::vec3 p1 = input.Position(v1);

            if (input.IsBoundary(eh))
            {
                // Boundary edge: simple midpoint
                edgePoints[ei] = 0.5f * (p0 + p1);
            }
            else
            {
                // Interior edge: average of endpoints and adjacent face points
                FaceHandle f0 = input.Face(h0);
                FaceHandle f1 = input.Face(h1);
                edgePoints[ei] = (p0 + p1 + facePoints[f0.Index] + facePoints[f1.Index]) / 4.0f;
            }
        }

        // =====================================================================
        // Phase 3: Compute new vertex points (one per original vertex)
        // =====================================================================
        // Interior: V' = Q/n + 2R/n + S(n-3)/n
        //   Q = average of adjacent face points
        //   R = average of adjacent edge midpoints
        //   S = original position, n = valence
        // Boundary: V' = (1/8)*prev + (3/4)*V + (1/8)*next
        std::vector<glm::vec3> vertexPoints(nV, glm::vec3(0.0f));
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (input.IsDeleted(vh) || input.IsIsolated(vh))
                continue;

            glm::vec3 S = input.Position(vh);

            if (input.IsBoundary(vh))
            {
                // Boundary vertex: (1/8)*prev + (3/4)*v + (1/8)*next
                glm::vec3 boundarySum(0.0f);
                std::size_t boundaryCount = 0;

                HalfedgeHandle hStart = input.Halfedge(vh);
                HalfedgeHandle h = hStart;
                std::size_t safety = 0;
                do
                {
                    if (input.IsBoundary(input.Edge(h)))
                    {
                        boundarySum += input.Position(input.ToVertex(h));
                        ++boundaryCount;
                    }
                    h = input.CWRotatedHalfedge(h);
                    if (++safety > 1000) break;
                } while (h != hStart);

                if (boundaryCount == 2)
                    vertexPoints[vi] = 0.75f * S + 0.125f * boundarySum;
                else
                    vertexPoints[vi] = S; // fallback
            }
            else
            {
                // Interior vertex
                std::size_t n = input.Valence(vh);
                float fn = static_cast<float>(n);

                // Q = average of adjacent face points
                glm::vec3 Q(0.0f);
                // R = average of adjacent edge midpoints
                glm::vec3 R(0.0f);

                HalfedgeHandle hStart = input.Halfedge(vh);
                HalfedgeHandle h = hStart;
                std::size_t count = 0;
                std::size_t safety = 0;
                do
                {
                    // Face point of the face incident to this halfedge
                    FaceHandle f = input.Face(h);
                    if (f.IsValid())
                        Q += facePoints[f.Index];

                    // Edge midpoint (average of the two endpoints)
                    glm::vec3 edgeMid = 0.5f * (input.Position(input.FromVertex(h)) +
                                                  input.Position(input.ToVertex(h)));
                    R += edgeMid;

                    ++count;
                    h = input.CWRotatedHalfedge(h);
                    if (++safety > 1000) break;
                } while (h != hStart);

                if (count > 0 && count == n)
                {
                    Q /= fn;
                    R /= fn;
                    vertexPoints[vi] = Q / fn + 2.0f * R / fn + S * (fn - 3.0f) / fn;
                }
                else
                {
                    vertexPoints[vi] = S; // fallback
                }
            }
        }

        // =====================================================================
        // Phase 4: Build the subdivided mesh
        // =====================================================================
        // New vertices: nV vertex points + nE edge points + nF face points
        // New faces: for each original face with k edges, k quads

        // Add vertex points
        std::vector<VertexHandle> vVertices(nV);
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (input.IsDeleted(vh) || input.IsIsolated(vh))
                continue;
            vVertices[vi] = output.AddVertex(vertexPoints[vi]);
        }

        // Add edge points
        std::vector<VertexHandle> eVertices(nE);
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (input.IsDeleted(eh))
                continue;
            eVertices[ei] = output.AddVertex(edgePoints[ei]);
        }

        // Add face points
        std::vector<VertexHandle> fVertices(nF);
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (input.IsDeleted(fh))
                continue;
            fVertices[fi] = output.AddVertex(facePoints[fi]);
        }

        // =====================================================================
        // Phase 5: Create quad faces
        // =====================================================================
        // For each original face, for each edge of that face, create a quad:
        //   (vertex_point[from], edge_point[edge], face_point[face], edge_point[prev_edge])
        //
        // Going around face halfedge cycle:
        //   h_prev → h_curr → h_next → ...
        //   For halfedge h_curr (from_v → to_v):
        //     Quad = (V[from_v], E[edge(h_curr)], F[face], E[edge(h_prev)])

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (input.IsDeleted(fh))
                continue;

            VertexHandle faceVert = fVertices[fi];

            HalfedgeHandle hStart = input.Halfedge(fh);
            HalfedgeHandle h = hStart;
            std::size_t safety = 0;
            do
            {
                HalfedgeHandle hPrev = input.PrevHalfedge(h);

                // Current halfedge goes from FromVertex(h) to ToVertex(h)
                VertexHandle fromV = input.FromVertex(h);
                EdgeHandle currEdge = input.Edge(h);
                EdgeHandle prevEdge = input.Edge(hPrev);

                VertexHandle vp = vVertices[fromV.Index];
                VertexHandle ep_curr = eVertices[currEdge.Index];
                VertexHandle ep_prev = eVertices[prevEdge.Index];

                // Quad: (vertex_point, edge_point_curr, face_point, edge_point_prev)
                // Winding order: CCW
                (void)output.AddQuad(vp, ep_curr, faceVert, ep_prev);

                h = input.NextHalfedge(h);
                if (++safety > 1000) break;
            } while (h != hStart);
        }

        return true;
    }

    std::optional<SubdivisionResult> Subdivide(
        const Halfedge::Mesh& input,
        Halfedge::Mesh& output,
        const SubdivisionParams& params)
    {
        if (input.IsEmpty() || params.Iterations == 0)
            return std::nullopt;

        SubdivisionResult result;

        // First iteration: input -> output
        if (!SubdivideOnce(input, output))
            return std::nullopt;

        result.IterationsPerformed = 1;

        // Subsequent iterations: ping-pong between two meshes
        Halfedge::Mesh temp;
        for (std::size_t i = 1; i < params.Iterations; ++i)
        {
            temp.Clear();
            if (!SubdivideOnce(output, temp))
                break;

            output = std::move(temp);
            result.IterationsPerformed = i + 1;
        }

        result.FinalVertexCount = output.VertexCount();
        result.FinalEdgeCount = output.EdgeCount();
        result.FinalFaceCount = output.FaceCount();

        // Check if all faces are quads
        result.AllQuads = true;
        for (std::size_t fi = 0; fi < output.FacesSize(); ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (output.IsDeleted(fh)) continue;
            if (output.Valence(fh) != 4)
            {
                result.AllQuads = false;
                break;
            }
        }

        return result;
    }

} // namespace Geometry::CatmullClark
