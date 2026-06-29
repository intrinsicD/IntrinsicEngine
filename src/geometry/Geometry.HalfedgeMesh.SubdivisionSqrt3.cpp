module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.HalfedgeMesh.SubdivisionSqrt3;

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;

namespace Geometry::SubdivisionSqrt3
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const glm::vec3& p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        [[nodiscard]] bool ValidateTriangleMesh(const HalfedgeMesh::Mesh& mesh)
        {
            if (mesh.IsEmpty() || mesh.FaceCount() == 0) return false;
            for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
            {
                const VertexHandle v{static_cast<PropertyIndex>(vi)};
                if (mesh.IsDeleted(v)) continue;
                if (!IsFinite(mesh.Position(v))) return false;
            }
            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                const FaceHandle f{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(f)) continue;
                if (mesh.Valence(f) != 3) return false;
                if (MeshUtils::FaceArea(mesh, f) <= 0.0) return false;
            }
            return true;
        }

        [[nodiscard]] double KobbeltBeta(const std::size_t valence)
        {
            if (valence == 0) return 0.0;
            const double theta = 2.0 * std::numbers::pi / static_cast<double>(valence);
            return (4.0 - 2.0 * std::cos(theta)) / 9.0;
        }

        [[nodiscard]] bool SubdivideOnce(const HalfedgeMesh::Mesh& input, HalfedgeMesh::Mesh& output)
        {
            if (!ValidateTriangleMesh(input)) return false;

            const std::size_t nV = input.VerticesSize();
            const std::size_t nF = input.FacesSize();
            output.Clear();
            output.Reserve(input.VertexCount() + input.FaceCount(),
                           input.EdgeCount() * 3u,
                           input.FaceCount() * 3u);

            std::vector<glm::vec3> evenPositions(nV, glm::vec3(0.0f));
            for (std::size_t vi = 0; vi < nV; ++vi)
            {
                const VertexHandle v{static_cast<PropertyIndex>(vi)};
                if (input.IsDeleted(v) || input.IsIsolated(v)) continue;

                const glm::vec3 p = input.Position(v);
                if (input.IsBoundary(v))
                {
                    glm::vec3 boundarySum(0.0f);
                    std::size_t boundaryCount = 0;
                    for (const HalfedgeHandle h : input.HalfedgesAroundVertex(v))
                    {
                        if (!input.IsBoundary(input.Edge(h))) continue;
                        boundarySum += input.Position(input.ToVertex(h));
                        ++boundaryCount;
                    }
                    evenPositions[vi] = boundaryCount == 2
                        ? (0.75f * p + 0.125f * boundarySum)
                        : p;
                    continue;
                }

                glm::vec3 neighborSum(0.0f);
                std::size_t valence = 0;
                for (const HalfedgeHandle h : input.HalfedgesAroundVertex(v))
                {
                    neighborSum += input.Position(input.ToVertex(h));
                    ++valence;
                }
                if (valence == 0)
                {
                    evenPositions[vi] = p;
                    continue;
                }
                const float beta = static_cast<float>(KobbeltBeta(valence));
                evenPositions[vi] = (1.0f - beta) * p
                    + beta * (neighborSum / static_cast<float>(valence));
            }

            std::vector<VertexHandle> evenVerts(nV);
            for (std::size_t vi = 0; vi < nV; ++vi)
            {
                const VertexHandle v{static_cast<PropertyIndex>(vi)};
                if (input.IsDeleted(v) || input.IsIsolated(v)) continue;
                evenVerts[vi] = output.AddVertex(evenPositions[vi]);
            }

            std::vector<VertexHandle> centroidVerts(nF);
            for (std::size_t fi = 0; fi < nF; ++fi)
            {
                const FaceHandle f{static_cast<PropertyIndex>(fi)};
                if (input.IsDeleted(f)) continue;

                const glm::vec3 c = glm::vec3(MeshUtils::FaceCentroid(input, f));
                centroidVerts[fi] = output.AddVertex(c);
            }

            for (std::size_t fi = 0; fi < nF; ++fi)
            {
                const FaceHandle f{static_cast<PropertyIndex>(fi)};
                if (input.IsDeleted(f)) continue;

                const HalfedgeHandle h0 = input.Halfedge(f);
                const HalfedgeHandle h1 = input.NextHalfedge(h0);
                const HalfedgeHandle h2 = input.NextHalfedge(h1);

                const VertexHandle v0 = input.ToVertex(h0);
                const VertexHandle v1 = input.ToVertex(h1);
                const VertexHandle v2 = input.ToVertex(h2);
                const VertexHandle c = centroidVerts[fi];

                (void)output.AddTriangle(evenVerts[v0.Index], evenVerts[v1.Index], c);
                (void)output.AddTriangle(evenVerts[v1.Index], evenVerts[v2.Index], c);
                (void)output.AddTriangle(evenVerts[v2.Index], evenVerts[v0.Index], c);
            }

            std::vector<std::pair<VertexHandle, VertexHandle>> originalInteriorEdges;
            originalInteriorEdges.reserve(input.EdgeCount());
            for (std::size_t ei = 0; ei < input.EdgesSize(); ++ei)
            {
                const EdgeHandle e{static_cast<PropertyIndex>(ei)};
                if (input.IsDeleted(e) || input.IsBoundary(e)) continue;
                const HalfedgeHandle h{static_cast<PropertyIndex>(2u * ei)};
                originalInteriorEdges.emplace_back(
                    evenVerts[input.FromVertex(h).Index],
                    evenVerts[input.ToVertex(h).Index]);
            }

            for (const auto& [a, b] : originalInteriorEdges)
            {
                if (const auto e = output.FindEdge(a, b))
                {
                    (void)output.Flip(*e);
                }
            }

            return true;
        }
    }

    std::optional<Sqrt3Result> Subdivide(
        const HalfedgeMesh::Mesh& input,
        HalfedgeMesh::Mesh& output,
        const Sqrt3Params& params)
    {
        if (input.IsEmpty() || params.Iterations == 0) return std::nullopt;

        std::size_t iterationsToRun = params.Iterations;
        if (params.MaxOutputFaces > 0)
        {
            const std::size_t inputFaces = input.FaceCount();
            if (inputFaces == 0 || inputFaces > params.MaxOutputFaces) return std::nullopt;

            std::size_t capped = 0;
            std::size_t faces = inputFaces;
            while (capped < params.Iterations)
            {
                if (faces > params.MaxOutputFaces / 3u) break;
                faces *= 3u;
                ++capped;
            }
            if (capped == 0) return std::nullopt;
            iterationsToRun = capped;
        }

        HalfedgeMesh::Mesh working;
        if (!SubdivideOnce(input, working)) return std::nullopt;

        Sqrt3Result result{};
        result.IterationsPerformed = 1;

        HalfedgeMesh::Mesh temp;
        for (std::size_t iter = 1; iter < iterationsToRun; ++iter)
        {
            temp.Clear();
            if (!SubdivideOnce(working, temp)) break;
            working = std::move(temp);
            result.IterationsPerformed = iter + 1;
        }

        output = std::move(working);
        result.FinalVertexCount = output.VertexCount();
        result.FinalFaceCount = output.FaceCount();
        return result;
    }
}
