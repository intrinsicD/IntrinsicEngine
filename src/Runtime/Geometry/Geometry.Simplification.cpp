module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Simplification.Impl;

import :Simplification;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::Simplification
{
    // =========================================================================
    // Symmetric 4x4 matrix for quadric error representation
    // =========================================================================
    //
    // The quadric Q represents the error function:
    //   E(v) = v^T Q v
    // where v is the homogeneous position [x, y, z, 1].
    //
    // Since Q is symmetric, we store only the upper triangle (10 elements).

    struct Quadric
    {
        double A[10]{};
        //  0: a00  1: a01  2: a02  3: a03
        //          4: a11  5: a12  6: a13
        //                  7: a22  8: a23
        //                          9: a33

        Quadric() = default;

        // Construct from plane equation ax + by + cz + d = 0
        explicit Quadric(double a, double b, double c, double d)
        {
            A[0] = a * a;  A[1] = a * b;  A[2] = a * c;  A[3] = a * d;
                           A[4] = b * b;  A[5] = b * c;  A[6] = b * d;
                                          A[7] = c * c;  A[8] = c * d;
                                                         A[9] = d * d;
        }

        Quadric& operator+=(const Quadric& rhs)
        {
            for (int i = 0; i < 10; ++i) A[i] += rhs.A[i];
            return *this;
        }

        Quadric operator+(const Quadric& rhs) const
        {
            Quadric result = *this;
            result += rhs;
            return result;
        }

        Quadric& operator*=(double s)
        {
            for (int i = 0; i < 10; ++i) A[i] *= s;
            return *this;
        }

        // Evaluate the quadric error for position (x, y, z) [homogeneous w=1]
        [[nodiscard]] double Evaluate(double x, double y, double z) const
        {
            // v^T Q v where v = [x, y, z, 1]
            return A[0]*x*x + 2.0*A[1]*x*y + 2.0*A[2]*x*z + 2.0*A[3]*x
                            +     A[4]*y*y + 2.0*A[5]*y*z + 2.0*A[6]*y
                                           +     A[7]*z*z + 2.0*A[8]*z
                                                           +     A[9];
        }

        [[nodiscard]] double Evaluate(glm::vec3 v) const
        {
            return Evaluate(static_cast<double>(v.x),
                           static_cast<double>(v.y),
                           static_cast<double>(v.z));
        }

        // Try to find the optimal position that minimizes v^T Q v.
        // This requires solving Q_3x3 * v = -[a03, a13, a23]^T
        // Returns nullopt if the 3x3 upper-left block is singular.
        [[nodiscard]] std::optional<glm::vec3> OptimalPosition() const
        {
            // Solve the 3x3 linear system using Cramer's rule
            double a00 = A[0], a01 = A[1], a02 = A[2], a03 = A[3];
            double              a11 = A[4], a12 = A[5], a13 = A[6];
            double                          a22 = A[7], a23 = A[8];

            double det = a00 * (a11 * a22 - a12 * a12)
                       - a01 * (a01 * a22 - a12 * a02)
                       + a02 * (a01 * a12 - a11 * a02);

            if (std::abs(det) < 1e-15) return std::nullopt;

            double invDet = 1.0 / det;

            double x = -invDet * (
                a03 * (a11 * a22 - a12 * a12) +
                a13 * (a02 * a12 - a01 * a22) +
                a23 * (a01 * a12 - a02 * a11));

            double y = -invDet * (
                a03 * (a12 * a02 - a01 * a22) +
                a13 * (a00 * a22 - a02 * a02) +
                a23 * (a02 * a01 - a00 * a12));

            double z = -invDet * (
                a03 * (a01 * a12 - a11 * a02) +
                a13 * (a01 * a02 - a00 * a12) +
                a23 * (a00 * a11 - a01 * a01));

            return glm::vec3(static_cast<float>(x),
                           static_cast<float>(y),
                           static_cast<float>(z));
        }
    };

    // =========================================================================
    // Binary min-heap for edge collapse priority queue
    // =========================================================================

    struct CollapseCandidate
    {
        Geometry::EdgeHandle Edge;
        double Cost{0.0};
        glm::vec3 OptimalPos{0.0f};
        std::size_t Version{0}; // To detect stale entries
    };

    struct EdgeHeap
    {
        std::vector<CollapseCandidate> Entries;

        void Push(CollapseCandidate c)
        {
            Entries.push_back(c);
            SiftUp(Entries.size() - 1);
        }

        [[nodiscard]] bool Empty() const { return Entries.empty(); }

        CollapseCandidate Pop()
        {
            assert(!Entries.empty());
            CollapseCandidate top = Entries[0];
            Entries[0] = Entries.back();
            Entries.pop_back();
            if (!Entries.empty()) SiftDown(0);
            return top;
        }

    private:
        void SiftUp(std::size_t i)
        {
            while (i > 0)
            {
                std::size_t parent = (i - 1) / 2;
                if (Entries[i].Cost < Entries[parent].Cost)
                {
                    std::swap(Entries[i], Entries[parent]);
                    i = parent;
                }
                else break;
            }
        }

        void SiftDown(std::size_t i)
        {
            std::size_t n = Entries.size();
            while (true)
            {
                std::size_t left = 2 * i + 1;
                std::size_t right = 2 * i + 2;
                std::size_t smallest = i;

                if (left < n && Entries[left].Cost < Entries[smallest].Cost)
                    smallest = left;
                if (right < n && Entries[right].Cost < Entries[smallest].Cost)
                    smallest = right;

                if (smallest == i) break;
                std::swap(Entries[i], Entries[smallest]);
                i = smallest;
            }
        }
    };

    // =========================================================================
    // Simplify implementation
    // =========================================================================

    std::optional<SimplificationResult> Simplify(
        Halfedge::Mesh& mesh,
        const SimplificationParams& params)
    {
        using namespace Geometry;

        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();

        if (mesh.FaceCount() < 4) return std::nullopt;

        // Target face count: use the explicit target, or default to 1 if only error-based
        std::size_t targetFaces = params.TargetFaces > 0
            ? params.TargetFaces
            : 1;

        // Step 1: Compute per-vertex quadrics
        std::vector<Quadric> vertexQuadrics(nV);

        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;

            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            // Compute face plane: n · x + d = 0
            glm::vec3 normal = glm::cross(pb - pa, pc - pa);
            float len = glm::length(normal);
            if (len < 1e-10f) continue;
            normal /= len;

            double a = static_cast<double>(normal.x);
            double b = static_cast<double>(normal.y);
            double c = static_cast<double>(normal.z);
            double d = -static_cast<double>(glm::dot(normal, pa));

            Quadric Kf(a, b, c, d);

            vertexQuadrics[va.Index] += Kf;
            vertexQuadrics[vb.Index] += Kf;
            vertexQuadrics[vc.Index] += Kf;
        }

        // Optional: add boundary constraint planes
        if (!params.PreserveBoundary)
        {
            for (std::size_t ei = 0; ei < nE; ++ei)
            {
                EdgeHandle eh{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(eh) || !mesh.IsBoundary(eh)) continue;

                HalfedgeHandle h0 = mesh.Halfedge(eh, 0);
                HalfedgeHandle h1 = mesh.Halfedge(eh, 1);

                // Find the boundary halfedge
                HalfedgeHandle hBnd = mesh.IsBoundary(h0) ? h0 : h1;
                HalfedgeHandle hInt = mesh.OppositeHalfedge(hBnd);

                VertexHandle vi = mesh.FromVertex(hInt);
                VertexHandle vj = mesh.ToVertex(hInt);

                // Get face normal of the interior face
                FaceHandle f = mesh.Face(hInt);
                if (!f.IsValid()) continue;

                HalfedgeHandle fh0 = mesh.Halfedge(f);
                HalfedgeHandle fh1 = mesh.NextHalfedge(fh0);
                HalfedgeHandle fh2 = mesh.NextHalfedge(fh1);

                glm::vec3 fa = mesh.Position(mesh.ToVertex(fh0));
                glm::vec3 fb = mesh.Position(mesh.ToVertex(fh1));
                glm::vec3 fc = mesh.Position(mesh.ToVertex(fh2));

                glm::vec3 faceNormal = glm::normalize(glm::cross(fb - fa, fc - fa));

                // Boundary plane: perpendicular to face normal and along the edge
                glm::vec3 edgeDir = glm::normalize(mesh.Position(vj) - mesh.Position(vi));
                glm::vec3 bndNormal = glm::normalize(glm::cross(edgeDir, faceNormal));

                double a = static_cast<double>(bndNormal.x);
                double b = static_cast<double>(bndNormal.y);
                double c = static_cast<double>(bndNormal.z);
                double d = -static_cast<double>(glm::dot(bndNormal, mesh.Position(vi)));

                Quadric Kb(a, b, c, d);
                Kb *= params.BoundaryWeight;

                vertexQuadrics[vi.Index] += Kb;
                vertexQuadrics[vj.Index] += Kb;
            }
        }

        // Step 2: Version tracking for lazy deletion in the heap
        std::vector<std::size_t> edgeVersion(nE, 0);

        // Step 3: Build the priority queue
        auto computeCollapse = [&](EdgeHandle eh) -> CollapseCandidate
        {
            HalfedgeHandle h0 = mesh.Halfedge(eh, 0);
            VertexHandle vi = mesh.FromVertex(h0);
            VertexHandle vj = mesh.ToVertex(h0);

            Quadric Q = vertexQuadrics[vi.Index] + vertexQuadrics[vj.Index];

            glm::vec3 optPos;
            double cost;

            // Try optimal placement
            if (auto opt = Q.OptimalPosition())
            {
                optPos = *opt;
                cost = Q.Evaluate(optPos);
            }
            else
            {
                // Fallback: choose among endpoints and midpoint
                glm::vec3 pi = mesh.Position(vi);
                glm::vec3 pj = mesh.Position(vj);
                glm::vec3 pm = (pi + pj) * 0.5f;

                double ci = Q.Evaluate(pi);
                double cj = Q.Evaluate(pj);
                double cm = Q.Evaluate(pm);

                if (ci <= cj && ci <= cm)      { optPos = pi; cost = ci; }
                else if (cj <= ci && cj <= cm) { optPos = pj; cost = cj; }
                else                           { optPos = pm; cost = cm; }
            }

            // Ensure non-negative cost (numerical precision)
            if (cost < 0.0) cost = 0.0;

            return {eh, cost, optPos, edgeVersion[eh.Index]};
        };

        EdgeHeap heap;
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh)) continue;
            if (params.PreserveBoundary && mesh.IsBoundary(eh)) continue;

            heap.Push(computeCollapse(eh));
        }

        // Step 4: Iteratively collapse edges
        SimplificationResult result;
        result.FinalFaceCount = mesh.FaceCount();

        while (!heap.Empty() && result.FinalFaceCount > targetFaces)
        {
            CollapseCandidate top = heap.Pop();

            // Skip stale entries
            if (mesh.IsDeleted(top.Edge)) continue;
            if (top.Version != edgeVersion[top.Edge.Index]) continue;

            // Check error threshold
            if (top.Cost > params.MaxError) break;

            // Check link condition
            if (!mesh.IsCollapseOk(top.Edge)) continue;

            // Perform collapse
            EdgeHandle collapsedEdge = top.Edge;
            HalfedgeHandle h0 = mesh.Halfedge(collapsedEdge, 0);
            VertexHandle vi = mesh.FromVertex(h0);
            VertexHandle vj = mesh.ToVertex(h0);

            // Compute merged quadric
            Quadric Qmerged = vertexQuadrics[vi.Index] + vertexQuadrics[vj.Index];

            auto surviving = mesh.Collapse(collapsedEdge, top.OptimalPos);
            if (!surviving) continue;

            // Update quadric for surviving vertex
            vertexQuadrics[surviving->Index] = Qmerged;

            ++result.CollapseCount;
            result.FinalFaceCount = mesh.FaceCount();
            result.MaxCollapseError = std::max(result.MaxCollapseError, top.Cost);

            // Re-enqueue affected edges (1-ring of surviving vertex)
            if (!mesh.IsIsolated(*surviving))
            {
                HalfedgeHandle hStart = mesh.Halfedge(*surviving);
                HalfedgeHandle h = hStart;
                const std::size_t maxRingIter = mesh.HalfedgesSize();
                std::size_t ringIter = 0;
                do
                {
                    EdgeHandle eAdj = mesh.Edge(h);
                    if (!mesh.IsDeleted(eAdj))
                    {
                        if (!params.PreserveBoundary || !mesh.IsBoundary(eAdj))
                        {
                            ++edgeVersion[eAdj.Index];
                            heap.Push(computeCollapse(eAdj));
                        }
                    }
                    h = mesh.CWRotatedHalfedge(h);
                    if (++ringIter > maxRingIter) break; // safety: broken connectivity
                } while (h != hStart);
            }
        }

        return result;
    }

} // namespace Geometry::Simplification
