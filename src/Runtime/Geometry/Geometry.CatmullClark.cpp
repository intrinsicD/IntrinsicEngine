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
    namespace
    {
        constexpr const char* kVertexTexcoordPropertyName = "v:texcoord";
    }

    // Perform a single level of Catmull-Clark subdivision.
    static bool SubdivideOnce(const Halfedge::Mesh& input, Halfedge::Mesh& output)
    {
        const std::size_t nV = input.VerticesSize();
        const std::size_t nE = input.EdgesSize();
        const std::size_t nF = input.FacesSize();

        if (nV == 0 || nF == 0)
            return false;

        const auto inputTexcoord = input.VertexProperties().Get<glm::vec2>(kVertexTexcoordPropertyName);
        const bool hasTexcoord = static_cast<bool>(inputTexcoord);

        output.Clear();

        // =====================================================================
        // Phase 1: Compute face points (one per face)
        // =====================================================================
        // F_i = centroid of face i's vertices
        std::vector<glm::vec3> facePoints(nF, glm::vec3(0.0f));
        std::vector<glm::vec2> faceTexcoords;
        if (hasTexcoord)
        {
            faceTexcoords.assign(nF, glm::vec2(0.0f));
        }
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (input.IsDeleted(fh))
                continue;

            glm::vec3 sum(0.0f);
            glm::vec2 uvSum(0.0f);
            std::size_t count = 0;
            HalfedgeHandle hStart = input.Halfedge(fh);
            HalfedgeHandle h = hStart;
            std::size_t safety = 0;
            do
            {
                const VertexHandle v = input.ToVertex(h);
                sum += input.Position(v);
                if (hasTexcoord)
                {
                    uvSum += inputTexcoord[v.Index];
                }
                ++count;
                h = input.NextHalfedge(h);
                if (++safety > std::max<std::size_t>(input.HalfedgesSize(), 1000u)) break;
            } while (h != hStart);

            if (count > 0)
            {
                facePoints[fi] = sum / static_cast<float>(count);
                if (hasTexcoord)
                {
                    faceTexcoords[fi] = uvSum / static_cast<float>(count);
                }
            }
        }

        // =====================================================================
        // Phase 2: Compute edge points (one per edge)
        // =====================================================================
        std::vector<glm::vec3> edgePoints(nE, glm::vec3(0.0f));
        std::vector<glm::vec2> edgeTexcoords;
        if (hasTexcoord)
        {
            edgeTexcoords.assign(nE, glm::vec2(0.0f));
        }
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
            const glm::vec2 uv0 = hasTexcoord ? inputTexcoord[v0.Index] : glm::vec2(0.0f);
            const glm::vec2 uv1 = hasTexcoord ? inputTexcoord[v1.Index] : glm::vec2(0.0f);

            if (input.IsBoundary(eh))
            {
                edgePoints[ei] = 0.5f * (p0 + p1);
                if (hasTexcoord)
                {
                    edgeTexcoords[ei] = 0.5f * (uv0 + uv1);
                }
            }
            else
            {
                FaceHandle f0 = input.Face(h0);
                FaceHandle f1 = input.Face(h1);
                edgePoints[ei] = (p0 + p1 + facePoints[f0.Index] + facePoints[f1.Index]) / 4.0f;
                if (hasTexcoord)
                {
                    edgeTexcoords[ei] = (uv0 + uv1 + faceTexcoords[f0.Index] + faceTexcoords[f1.Index]) / 4.0f;
                }
            }
        }

        // =====================================================================
        // Phase 3: Compute new vertex points (one per original vertex)
        // =====================================================================
        std::vector<glm::vec3> vertexPoints(nV, glm::vec3(0.0f));
        std::vector<glm::vec2> vertexTexcoords;
        if (hasTexcoord)
        {
            vertexTexcoords.assign(nV, glm::vec2(0.0f));
        }
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (input.IsDeleted(vh) || input.IsIsolated(vh))
                continue;

            glm::vec3 S = input.Position(vh);
            const glm::vec2 Suv = hasTexcoord ? inputTexcoord[vi] : glm::vec2(0.0f);

            if (input.IsBoundary(vh))
            {
                glm::vec3 boundarySum(0.0f);
                glm::vec2 boundaryUvSum(0.0f);
                std::size_t boundaryCount = 0;

                HalfedgeHandle hStart = input.Halfedge(vh);
                HalfedgeHandle h = hStart;
                std::size_t safety = 0;
                do
                {
                    if (input.IsBoundary(input.Edge(h)))
                    {
                        const VertexHandle neighbor = input.ToVertex(h);
                        boundarySum += input.Position(neighbor);
                        if (hasTexcoord)
                        {
                            boundaryUvSum += inputTexcoord[neighbor.Index];
                        }
                        ++boundaryCount;
                    }
                    h = input.CWRotatedHalfedge(h);
                    if (++safety > std::max<std::size_t>(input.HalfedgesSize(), 1000u)) break;
                } while (h != hStart);

                if (boundaryCount == 2)
                {
                    vertexPoints[vi] = 0.75f * S + 0.125f * boundarySum;
                    if (hasTexcoord)
                    {
                        vertexTexcoords[vi] = 0.75f * Suv + 0.125f * boundaryUvSum;
                    }
                }
                else
                {
                    vertexPoints[vi] = S;
                    if (hasTexcoord)
                    {
                        vertexTexcoords[vi] = Suv;
                    }
                }
            }
            else
            {
                std::size_t n = input.Valence(vh);
                float fn = static_cast<float>(n);

                glm::vec3 Q(0.0f);
                glm::vec3 R(0.0f);
                glm::vec2 Quv(0.0f);
                glm::vec2 Ruv(0.0f);

                HalfedgeHandle hStart = input.Halfedge(vh);
                HalfedgeHandle h = hStart;
                std::size_t count = 0;
                std::size_t safety = 0;
                do
                {
                    FaceHandle f = input.Face(h);
                    if (f.IsValid())
                    {
                        Q += facePoints[f.Index];
                        if (hasTexcoord)
                        {
                            Quv += faceTexcoords[f.Index];
                        }
                    }

                    const VertexHandle from = input.FromVertex(h);
                    const VertexHandle to = input.ToVertex(h);
                    glm::vec3 edgeMid = 0.5f * (input.Position(from) + input.Position(to));
                    R += edgeMid;
                    if (hasTexcoord)
                    {
                        const glm::vec2 edgeUvMid = 0.5f * (inputTexcoord[from.Index] + inputTexcoord[to.Index]);
                        Ruv += edgeUvMid;
                    }

                    ++count;
                    h = input.CWRotatedHalfedge(h);
                    if (++safety > std::max<std::size_t>(input.HalfedgesSize(), 1000u)) break;
                } while (h != hStart);

                if (count > 0 && count == n)
                {
                    Q /= fn;
                    R /= fn;
                    vertexPoints[vi] = Q / fn + 2.0f * R / fn + S * (fn - 3.0f) / fn;
                    if (hasTexcoord)
                    {
                        Quv /= fn;
                        Ruv /= fn;
                        vertexTexcoords[vi] = Quv / fn + 2.0f * Ruv / fn + Suv * (fn - 3.0f) / fn;
                    }
                }
                else
                {
                    vertexPoints[vi] = S;
                    if (hasTexcoord)
                    {
                        vertexTexcoords[vi] = Suv;
                    }
                }
            }
        }

        // =====================================================================
        // Phase 4: Build the subdivided mesh
        // =====================================================================
        VertexProperty<glm::vec2> outputTexcoord;
        if (hasTexcoord)
        {
            outputTexcoord = VertexProperty<glm::vec2>(
                output.VertexProperties().GetOrAdd<glm::vec2>(kVertexTexcoordPropertyName, glm::vec2(0.0f)));
        }

        // Add vertex points
        std::vector<VertexHandle> vVertices(nV);
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (input.IsDeleted(vh) || input.IsIsolated(vh))
                continue;
            vVertices[vi] = output.AddVertex(vertexPoints[vi]);
            if (hasTexcoord)
            {
                outputTexcoord[vVertices[vi]] = vertexTexcoords[vi];
            }
        }

        // Add edge points
        std::vector<VertexHandle> eVertices(nE);
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (input.IsDeleted(eh))
                continue;
            eVertices[ei] = output.AddVertex(edgePoints[ei]);
            if (hasTexcoord)
            {
                outputTexcoord[eVertices[ei]] = edgeTexcoords[ei];
            }
        }

        // Add face points
        std::vector<VertexHandle> fVertices(nF);
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (input.IsDeleted(fh))
                continue;
            fVertices[fi] = output.AddVertex(facePoints[fi]);
            if (hasTexcoord)
            {
                outputTexcoord[fVertices[fi]] = faceTexcoords[fi];
            }
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
                if (++safety > std::max<std::size_t>(input.HalfedgesSize(), 1000u)) break;
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
