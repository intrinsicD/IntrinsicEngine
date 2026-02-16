module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Subdivision.Impl;

import :Subdivision;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::Subdivision
{
    // Warren's beta formula for Loop subdivision even-vertex weights.
    // For valence n:
    //   β = 1/n * (5/8 - (3/8 + 1/4 * cos(2π/n))²)
    // Simplified: β = 3/(8*n) for n > 3, β = 3/16 for n = 3.
    static double LoopBeta(std::size_t valence)
    {
        if (valence == 3)
            return 3.0 / 16.0;
        return 3.0 / (8.0 * static_cast<double>(valence));
    }

    // Perform a single level of Loop subdivision.
    // Takes input mesh, produces output mesh.
    static bool SubdivideOnce(const Halfedge::Mesh& input, Halfedge::Mesh& output)
    {
        const std::size_t nV = input.VerticesSize();
        const std::size_t nE = input.EdgesSize();
        const std::size_t nF = input.FacesSize();

        if (nV == 0 || nF == 0)
            return false;

        // Verify all faces are triangles
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (input.IsDeleted(fh)) continue;
            if (input.Valence(fh) != 3)
                return false;
        }

        output.Clear();

        // Phase 1: Compute new positions for even vertices (existing vertices)
        std::vector<glm::vec3> evenPositions(nV, glm::vec3(0.0f));
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (input.IsDeleted(vh) || input.IsIsolated(vh))
                continue;

            glm::vec3 p = input.Position(vh);
            std::size_t valence = input.Valence(vh);

            if (input.IsBoundary(vh))
            {
                // Boundary vertex rule: 1/8 * prev + 3/4 * v + 1/8 * next
                // Find the two boundary neighbors
                glm::vec3 boundarySum(0.0f);
                std::size_t boundaryCount = 0;

                HalfedgeHandle hStart = input.Halfedge(vh);
                HalfedgeHandle h = hStart;
                do
                {
                    if (input.IsBoundary(input.Edge(h)))
                    {
                        boundarySum += input.Position(input.ToVertex(h));
                        ++boundaryCount;
                    }
                    h = input.CWRotatedHalfedge(h);
                } while (h != hStart);

                if (boundaryCount == 2)
                {
                    evenPositions[vi] = 0.75f * p + 0.125f * boundarySum;
                }
                else
                {
                    evenPositions[vi] = p; // Fallback: keep position
                }
            }
            else
            {
                // Interior vertex rule: (1 - n*β)*v + β*Σ neighbors
                double beta = LoopBeta(valence);
                float fb = static_cast<float>(beta);
                float fw = 1.0f - static_cast<float>(valence) * fb;

                glm::vec3 neighborSum(0.0f);
                HalfedgeHandle hStart = input.Halfedge(vh);
                HalfedgeHandle h = hStart;
                do
                {
                    neighborSum += input.Position(input.ToVertex(h));
                    h = input.CWRotatedHalfedge(h);
                } while (h != hStart);

                evenPositions[vi] = fw * p + fb * neighborSum;
            }
        }

        // Phase 2: Compute positions for odd vertices (new edge midpoints)
        std::vector<glm::vec3> oddPositions(nE, glm::vec3(0.0f));
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
                oddPositions[ei] = 0.5f * (p0 + p1);
            }
            else
            {
                // Interior edge: 3/8*(v0+v1) + 1/8*(v2+v3)
                // v2 is opposite vertex in face of h0, v3 in face of h1
                VertexHandle v2 = input.ToVertex(input.NextHalfedge(h0));
                VertexHandle v3 = input.ToVertex(input.NextHalfedge(h1));
                glm::vec3 p2 = input.Position(v2);
                glm::vec3 p3 = input.Position(v3);

                oddPositions[ei] = 0.375f * (p0 + p1) + 0.125f * (p2 + p3);
            }
        }

        // Phase 3: Build the subdivided mesh
        // Add even vertices (one per original vertex)
        std::vector<VertexHandle> evenVerts(nV);
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (input.IsDeleted(vh) || input.IsIsolated(vh))
                continue;
            evenVerts[vi] = output.AddVertex(evenPositions[vi]);
        }

        // Add odd vertices (one per original edge)
        std::vector<VertexHandle> oddVerts(nE);
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (input.IsDeleted(eh))
                continue;
            oddVerts[ei] = output.AddVertex(oddPositions[ei]);
        }

        // Phase 4: Create 4 sub-triangles per original face
        // Face halfedge cycle: h0(vc→va) → h1(va→vb) → h2(vb→vc)
        // Edge midpoints: m_ca = mid(vc,va), m_ab = mid(va,vb), m_bc = mid(vb,vc)
        // Sub-triangles (preserving CCW winding):
        //   (va, m_ab, m_ca), (vb, m_bc, m_ab), (vc, m_ca, m_bc), (m_ca, m_ab, m_bc)
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (input.IsDeleted(fh))
                continue;

            // Get halfedges of this triangle
            HalfedgeHandle h0 = input.Halfedge(fh);
            HalfedgeHandle h1 = input.NextHalfedge(h0);
            HalfedgeHandle h2 = input.NextHalfedge(h1);

            // Corner vertices
            VertexHandle va = input.ToVertex(h0);
            VertexHandle vb = input.ToVertex(h1);
            VertexHandle vc = input.ToVertex(h2);

            VertexHandle a = evenVerts[va.Index];
            VertexHandle b = evenVerts[vb.Index];
            VertexHandle c = evenVerts[vc.Index];

            // Edge midpoint vertices
            // h0 goes from vc to va, h1 from va to vb, h2 from vb to vc
            EdgeHandle e0 = input.Edge(h0); // edge spanning (vc, va)
            EdgeHandle e1 = input.Edge(h1); // edge spanning (va, vb)
            EdgeHandle e2 = input.Edge(h2); // edge spanning (vb, vc)

            VertexHandle m_ca = oddVerts[e0.Index]; // midpoint of edge (vc, va)
            VertexHandle m_ab = oddVerts[e1.Index]; // midpoint of edge (va, vb)
            VertexHandle m_bc = oddVerts[e2.Index]; // midpoint of edge (vb, vc)

            // Four sub-triangles preserving parent face winding (vc → va → vb)
            (void)output.AddTriangle(a, m_ab, m_ca);     // corner at va
            (void)output.AddTriangle(b, m_bc, m_ab);     // corner at vb
            (void)output.AddTriangle(c, m_ca, m_bc);     // corner at vc
            (void)output.AddTriangle(m_ca, m_ab, m_bc);  // center
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

        // First iteration: input → output
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
        result.FinalFaceCount = output.FaceCount();

        return result;
    }

} // namespace Geometry::Subdivision
