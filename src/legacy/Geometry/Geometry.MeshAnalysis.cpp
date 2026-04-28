module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

module Geometry.MeshAnalysis;

import Core.Logging;
import Geometry.HalfedgeMesh;
import Geometry.MeshUtils;
import Geometry.Properties;

namespace Geometry::MeshAnalysis
{
    namespace
    {
        constexpr double kRadToDeg = 180.0 / std::numbers::pi;

        [[nodiscard]] bool IsFiniteVec3(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        template <class HandleT, class MaskPropertyT, class BoolPropertyT>
        void ResetDomain(MaskPropertyT& mask, BoolPropertyT& problem, std::size_t count)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                const HandleT h{static_cast<PropertyIndex>(i)};
                mask[h] = 0u;
                problem[h] = false;
            }
        }

        template <class HandleT, class BoolPropertyT>
        void CollectProblemHandles(const BoolPropertyT& problem, std::size_t count, std::vector<HandleT>& out)
        {
            out.clear();
            out.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const HandleT h{static_cast<PropertyIndex>(i)};
                if (problem[h])
                {
                    out.push_back(h);
                }
            }
        }

        template <class HandleT, class MaskPropertyT, class BoolPropertyT>
        void MarkIssue(HandleT h, std::uint32_t bit, MaskPropertyT& mask, BoolPropertyT& problem)
        {
            mask[h] |= bit;
            problem[h] = true;
        }


        void MarkEdgeNeighborhood(
            const Halfedge::Mesh& mesh,
            EdgeHandle e,
            VertexProperty<bool>& vertexProblem,
            EdgeProperty<bool>& edgeProblem,
            HalfedgeProperty<bool>& halfedgeProblem,
            FaceProperty<bool>& faceProblem)
        {
            if (!mesh.IsValid(e) || mesh.IsDeleted(e))
            {
                return;
            }

            edgeProblem[e] = true;

            const HalfedgeHandle h0 = mesh.Halfedge(e, 0);
            const HalfedgeHandle h1 = mesh.Halfedge(e, 1);

            if (h0.IsValid() && mesh.IsValid(h0) && !mesh.IsDeleted(h0))
            {
                halfedgeProblem[h0] = true;
                const VertexHandle v0 = mesh.FromVertex(h0);
                const VertexHandle v1 = mesh.ToVertex(h0);
                if (mesh.IsValid(v0) && !mesh.IsDeleted(v0)) vertexProblem[v0] = true;
                if (mesh.IsValid(v1) && !mesh.IsDeleted(v1)) vertexProblem[v1] = true;

                const FaceHandle f0 = mesh.Face(h0);
                if (f0.IsValid() && !mesh.IsDeleted(f0))
                {
                    faceProblem[f0] = true;
                }
            }

            if (h1.IsValid() && mesh.IsValid(h1) && !mesh.IsDeleted(h1))
            {
                halfedgeProblem[h1] = true;
                const VertexHandle v0 = mesh.FromVertex(h1);
                const VertexHandle v1 = mesh.ToVertex(h1);
                if (mesh.IsValid(v0) && !mesh.IsDeleted(v0)) vertexProblem[v0] = true;
                if (mesh.IsValid(v1) && !mesh.IsDeleted(v1)) vertexProblem[v1] = true;

                const FaceHandle f1 = mesh.Face(h1);
                if (f1.IsValid() && !mesh.IsDeleted(f1))
                {
                    faceProblem[f1] = true;
                }
            }
        }

        void MarkFaceNeighborhood(
            const Halfedge::Mesh& mesh,
            FaceHandle f,
            VertexProperty<bool>& vertexProblem,
            EdgeProperty<bool>& edgeProblem,
            HalfedgeProperty<bool>& halfedgeProblem,
            HalfedgeProperty<std::uint32_t>& halfedgeMask,
            FaceProperty<bool>& faceProblem)
        {
            if (!mesh.IsValid(f) || mesh.IsDeleted(f))
            {
                return;
            }

            faceProblem[f] = true;

            for (const HalfedgeHandle h : mesh.HalfedgesAroundFace(f))
            {
                if (!mesh.IsValid(h) || mesh.IsDeleted(h))
                {
                    continue;
                }

                halfedgeMask[h] |= kHalfedgeIssueFaceProblem;
                halfedgeProblem[h] = true;

                const EdgeHandle e = mesh.Edge(h);
                if (mesh.IsValid(e) && !mesh.IsDeleted(e))
                {
                    edgeProblem[e] = true;
                }

                const VertexHandle v = mesh.ToVertex(h);
                if (mesh.IsValid(v) && !mesh.IsDeleted(v))
                {
                    vertexProblem[v] = true;
                }
            }
        }
    } // namespace

    std::optional<AnalysisResult> Analyze(Halfedge::Mesh& mesh, const AnalysisParams& params)
    {
        if (mesh.IsEmpty())
        {
            return std::nullopt;
        }

        AnalysisResult result;

        result.ProblemVertex = VertexProperty<bool>(mesh.VertexProperties().GetOrAdd<bool>(kVertexProblemPropertyName, false));
        result.ProblemEdge = EdgeProperty<bool>(mesh.EdgeProperties().GetOrAdd<bool>(kEdgeProblemPropertyName, false));
        result.ProblemHalfedge = HalfedgeProperty<bool>(mesh.HalfedgeProperties().GetOrAdd<bool>(kHalfedgeProblemPropertyName, false));
        result.ProblemFace = FaceProperty<bool>(mesh.FaceProperties().GetOrAdd<bool>(kFaceProblemPropertyName, false));

        result.VertexIssueMask = VertexProperty<std::uint32_t>(mesh.VertexProperties().GetOrAdd<std::uint32_t>(kVertexIssueMaskPropertyName, 0u));
        result.EdgeIssueMask = EdgeProperty<std::uint32_t>(mesh.EdgeProperties().GetOrAdd<std::uint32_t>(kEdgeIssueMaskPropertyName, 0u));
        result.HalfedgeIssueMask = HalfedgeProperty<std::uint32_t>(mesh.HalfedgeProperties().GetOrAdd<std::uint32_t>(kHalfedgeIssueMaskPropertyName, 0u));
        result.FaceIssueMask = FaceProperty<std::uint32_t>(mesh.FaceProperties().GetOrAdd<std::uint32_t>(kFaceIssueMaskPropertyName, 0u));

        if (!result.ProblemVertex.IsValid() || !result.ProblemEdge.IsValid() || !result.ProblemHalfedge.IsValid() || !result.ProblemFace.IsValid() ||
            !result.VertexIssueMask.IsValid() || !result.EdgeIssueMask.IsValid() || !result.HalfedgeIssueMask.IsValid() || !result.FaceIssueMask.IsValid())
        {
            Core::Log::Warn("MeshAnalysis::Analyze: failed to create one or more analysis marker properties due to a type collision.");
            return std::nullopt;
        }

        ResetDomain<VertexHandle>(result.VertexIssueMask, result.ProblemVertex, mesh.VerticesSize());
        ResetDomain<EdgeHandle>(result.EdgeIssueMask, result.ProblemEdge, mesh.EdgesSize());
        ResetDomain<HalfedgeHandle>(result.HalfedgeIssueMask, result.ProblemHalfedge, mesh.HalfedgesSize());
        ResetDomain<FaceHandle>(result.FaceIssueMask, result.ProblemFace, mesh.FacesSize());

        const double zeroLengthEpsilonSq = params.ZeroLengthEdgeEpsilon * params.ZeroLengthEdgeEpsilon;

        // ------------------------------------------------------------------
        // Vertex analysis: isolation, boundary, manifoldness, non-finite input.
        // ------------------------------------------------------------------
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            const VertexHandle v{static_cast<PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
            {
                continue;
            }

            const glm::vec3 p = mesh.Position(v);
            bool problem = false;

            if (!IsFiniteVec3(p))
            {
                MarkIssue(v, kVertexIssueNonFinitePosition, result.VertexIssueMask, result.ProblemVertex);
                ++result.NonFiniteVertexCount;
                problem = true;
            }

            if (mesh.IsIsolated(v))
            {
                MarkIssue(v, kVertexIssueIsolated, result.VertexIssueMask, result.ProblemVertex);
                ++result.IsolatedVertexCount;
                problem = true;
            }

            if (mesh.IsBoundary(v))
            {
                MarkIssue(v, kVertexIssueBoundary, result.VertexIssueMask, result.ProblemVertex);
                ++result.BoundaryVertexCount;
                problem = true;
            }

            if (!mesh.IsManifold(v))
            {
                MarkIssue(v, kVertexIssueNonManifold, result.VertexIssueMask, result.ProblemVertex);
                ++result.NonManifoldVertexCount;
                problem = true;
            }

            if (!problem)
            {
                result.ProblemVertex[v] = false;
            }
        }

        // ------------------------------------------------------------------
        // Edge analysis: boundary, zero-length, non-finite geometry.
        // ------------------------------------------------------------------
        for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
        {
            const EdgeHandle e{static_cast<PropertyIndex>(i)};
            if (!mesh.IsValid(e) || mesh.IsDeleted(e))
            {
                continue;
            }

            const HalfedgeHandle h0 = mesh.Halfedge(e, 0);
            const HalfedgeHandle h1 = mesh.Halfedge(e, 1);
            const VertexHandle a = mesh.FromVertex(h0);
            const VertexHandle b = mesh.ToVertex(h0);

            bool problem = false;

            const bool hasFiniteEndpoints =
                a.IsValid() && b.IsValid() &&
                mesh.IsValid(a) && mesh.IsValid(b) &&
                !mesh.IsDeleted(a) && !mesh.IsDeleted(b) &&
                IsFiniteVec3(mesh.Position(a)) && IsFiniteVec3(mesh.Position(b));

            if (!hasFiniteEndpoints)
            {
                MarkIssue(e, kEdgeIssueNonFiniteGeometry, result.EdgeIssueMask, result.ProblemEdge);
                ++result.NonFiniteEdgeCount;
                problem = true;
            }
            else
            {
                const glm::vec3 delta = mesh.Position(b) - mesh.Position(a);
                const auto lengthSq = static_cast<double>(glm::dot(delta, delta));
                if (lengthSq <= zeroLengthEpsilonSq)
                {
                    MarkIssue(e, kEdgeIssueZeroLength, result.EdgeIssueMask, result.ProblemEdge);
                    ++result.ZeroLengthEdgeCount;
                    problem = true;
                }
            }

            if (mesh.IsBoundary(e))
            {
                MarkIssue(e, kEdgeIssueBoundary, result.EdgeIssueMask, result.ProblemEdge);
                ++result.BoundaryEdgeCount;
                ++result.BoundaryHalfedgeCount;
                problem = true;

                if (h0.IsValid() && mesh.IsValid(h0) && !mesh.IsDeleted(h0))
                {
                    MarkIssue(h0, kHalfedgeIssueBoundary, result.HalfedgeIssueMask, result.ProblemHalfedge);
                }
                if (h1.IsValid() && mesh.IsValid(h1) && !mesh.IsDeleted(h1))
                {
                    MarkIssue(h1, kHalfedgeIssueBoundary, result.HalfedgeIssueMask, result.ProblemHalfedge);
                }
            }

            if (h0.IsValid() && mesh.IsValid(h0) && !mesh.IsDeleted(h0))
            {
                if (!IsFiniteVec3(mesh.Position(mesh.FromVertex(h0))) || !IsFiniteVec3(mesh.Position(mesh.ToVertex(h0))))
                {
                    MarkIssue(h0, kHalfedgeIssueNonFiniteGeometry, result.HalfedgeIssueMask, result.ProblemHalfedge);
                    problem = true;
                }
            }

            if (h1.IsValid() && mesh.IsValid(h1) && !mesh.IsDeleted(h1))
            {
                if (!IsFiniteVec3(mesh.Position(mesh.FromVertex(h1))) || !IsFiniteVec3(mesh.Position(mesh.ToVertex(h1))))
                {
                    MarkIssue(h1, kHalfedgeIssueNonFiniteGeometry, result.HalfedgeIssueMask, result.ProblemHalfedge);
                    problem = true;
                }
            }

            if (problem)
            {
                MarkEdgeNeighborhood(mesh, e, result.ProblemVertex, result.ProblemEdge, result.ProblemHalfedge, result.ProblemFace);
            }
        }

        // ------------------------------------------------------------------
        // Face analysis: topology, area, skinny triangles, finite geometry.
        // ------------------------------------------------------------------
        for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
        {
            const FaceHandle f{static_cast<PropertyIndex>(i)};
            if (!mesh.IsValid(f) || mesh.IsDeleted(f))
            {
                continue;
            }

            const auto faceBoundary = mesh.IsBoundary(f);
            if (faceBoundary)
            {
                MarkIssue(f, kFaceIssueBoundary, result.FaceIssueMask, result.ProblemFace);
                ++result.BoundaryFaceCount;
                MarkFaceNeighborhood(mesh, f, result.ProblemVertex, result.ProblemEdge, result.ProblemHalfedge, result.HalfedgeIssueMask, result.ProblemFace);
            }

            MeshUtils::TriangleFaceView tri{};
            const bool hasTriangle = MeshUtils::TryGetTriangleFaceView(mesh, f, tri);
            if (!hasTriangle)
            {
                MarkIssue(f, kFaceIssueNonTriangle, result.FaceIssueMask, result.ProblemFace);
                ++result.NonTriangleFaceCount;
                MarkFaceNeighborhood(mesh, f, result.ProblemVertex, result.ProblemEdge, result.ProblemHalfedge, result.HalfedgeIssueMask, result.ProblemFace);
                continue;
            }

            const bool hasFiniteGeometry =
                IsFiniteVec3(tri.P0) && IsFiniteVec3(tri.P1) && IsFiniteVec3(tri.P2);
            if (!hasFiniteGeometry)
            {
                MarkIssue(f, kFaceIssueNonFiniteGeometry, result.FaceIssueMask, result.ProblemFace);
                ++result.NonFiniteFaceCount;
                MarkFaceNeighborhood(mesh, f, result.ProblemVertex, result.ProblemEdge, result.ProblemHalfedge, result.HalfedgeIssueMask, result.ProblemFace);
            }
            else
            {
                const double area = MeshUtils::TriangleArea(tri.P0, tri.P1, tri.P2);
                if (area <= params.DegenerateFaceAreaEpsilon)
                {
                    MarkIssue(f, kFaceIssueDegenerateArea, result.FaceIssueMask, result.ProblemFace);
                    ++result.DegenerateFaceCount;
                    MarkFaceNeighborhood(mesh, f, result.ProblemVertex, result.ProblemEdge, result.ProblemHalfedge, result.HalfedgeIssueMask, result.ProblemFace);
                }
                else
                {
                    const double angle0 = MeshUtils::AngleAtVertex(tri.P0, tri.P1, tri.P2) * kRadToDeg;
                    const double angle1 = MeshUtils::AngleAtVertex(tri.P1, tri.P2, tri.P0) * kRadToDeg;
                    const double angle2 = MeshUtils::AngleAtVertex(tri.P2, tri.P0, tri.P1) * kRadToDeg;
                    const double longestEdge = std::max({
                        static_cast<double>(glm::distance(tri.P0, tri.P1)),
                        static_cast<double>(glm::distance(tri.P1, tri.P2)),
                        static_cast<double>(glm::distance(tri.P2, tri.P0))});
                    const double semiPerimeter = 0.5 * (
                        static_cast<double>(glm::distance(tri.P0, tri.P1)) +
                        static_cast<double>(glm::distance(tri.P1, tri.P2)) +
                        static_cast<double>(glm::distance(tri.P2, tri.P0)));

                    double aspectRatio = 0.0;
                    if (semiPerimeter > params.DegenerateFaceAreaEpsilon)
                    {
                        const double inradius = area / semiPerimeter;
                        if (inradius > params.DegenerateFaceAreaEpsilon)
                        {
                            aspectRatio = longestEdge / (2.0 * std::numbers::sqrt3_v<double> * inradius);
                        }
                    }

                    const bool skinnyByAngles =
                        angle0 < params.SmallAngleThresholdDegrees || angle1 < params.SmallAngleThresholdDegrees || angle2 < params.SmallAngleThresholdDegrees ||
                        angle0 > params.LargeAngleThresholdDegrees || angle1 > params.LargeAngleThresholdDegrees || angle2 > params.LargeAngleThresholdDegrees;
                    const bool skinnyByAspect = aspectRatio > params.MaxAspectRatio;
                    if (skinnyByAngles || skinnyByAspect)
                    {
                        MarkIssue(f, kFaceIssueSkinny, result.FaceIssueMask, result.ProblemFace);
                        ++result.SkinnyFaceCount;
                        MarkFaceNeighborhood(mesh, f, result.ProblemVertex, result.ProblemEdge, result.ProblemHalfedge, result.HalfedgeIssueMask, result.ProblemFace);
                    }
                }
            }
        }

        CollectProblemHandles<VertexHandle>(result.ProblemVertex, mesh.VerticesSize(), result.ProblemVertices);
        CollectProblemHandles<EdgeHandle>(result.ProblemEdge, mesh.EdgesSize(), result.ProblemEdges);
        CollectProblemHandles<HalfedgeHandle>(result.ProblemHalfedge, mesh.HalfedgesSize(), result.ProblemHalfedges);
        CollectProblemHandles<FaceHandle>(result.ProblemFace, mesh.FacesSize(), result.ProblemFaces);

        return result;
    }

} // namespace Geometry::MeshAnalysis

