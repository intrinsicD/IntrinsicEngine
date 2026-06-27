module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.HalfedgeMesh.AdaptiveRemeshing;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.Curvature;
import Geometry.HalfedgeMesh.Utils;
import Geometry.MeshClosestFace;

namespace Geometry::AdaptiveRemeshing
{
    using MeshUtils::EdgeLengthSq;
    using MeshUtils::MeanEdgeLength;
    using MeshUtils::EqualizeValenceByEdgeFlip;

    bool ReferenceProjector::Build(const HalfedgeMesh::Mesh& mesh,
                                   const ReferenceProjectionParams& params)
    {
        Enabled = false;
        m_MaxDistanceSq = 0.0f;

        if (!m_Index.Build(mesh)) return false;

        const double maxDist = params.MaxReferenceProjectionDistance;
        m_MaxDistanceSq = (std::isfinite(maxDist) && maxDist > 0.0)
            ? static_cast<float>(maxDist * maxDist)
            : 0.0f;
        Enabled = true;
        static_cast<void>(params.ReferenceProjectionK);
        return true;
    }

    ReferenceProjectionResult ReferenceProjector::Project(const glm::vec3 point) const
    {
        ReferenceProjectionResult projected{};
        projected.Point = point;
        if (!Enabled) return projected;

        const MeshClosestFaceResult nearest = m_Index.Query(point);
        if (!nearest.Found) return projected;
        if (m_MaxDistanceSq > 0.0f && nearest.SquaredDistance > m_MaxDistanceSq)
        {
            return projected;
        }

        projected.Found = true;
        projected.Point = nearest.Point;
        projected.Face = nearest.Face;
        projected.Distance = std::sqrt(std::max(nearest.SquaredDistance, 0.0f));
        return projected;
    }

    glm::vec3 ReferenceProjector::ProjectPoint(const glm::vec3 point) const
    {
        const ReferenceProjectionResult projected = Project(point);
        return projected.Found ? projected.Point : point;
    }

    // Per-edge local target = average of endpoint sizing fields
    static double LocalTarget(
        const std::vector<double>& sizing,
        const HalfedgeMesh::Mesh& mesh,
        EdgeHandle e)
    {
        HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
        const std::size_t v0 = mesh.FromVertex(h).Index;
        const std::size_t v1 = mesh.ToVertex(h).Index;
        return 0.5 * (sizing[v0] + sizing[v1]);
    }

    static void ComputeSizingField(
        HalfedgeMesh::Mesh& mesh,
        double baseLength,
        const AdaptiveRemeshingParams& params,
        double minLen,
        double maxLen,
        bool preserveBoundary,
        std::vector<double>& sizing)
    {
        sizing.resize(mesh.VerticesSize(), baseLength);

        const double alpha = params.CurvatureAdaptation;
        const bool useErrorBoundedTaubin =
            params.Sizing == SizingLaw::ErrorBoundedTaubin
            && std::isfinite(params.ApproximationError)
            && params.ApproximationError > 0.0;

        if (!useErrorBoundedTaubin && alpha < 1e-12)
        {
            std::fill(sizing.begin(), sizing.end(), std::clamp(baseLength, minLen, maxLen));
            return;
        }

        const auto curvField = Curvature::ComputeCurvature(mesh);
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
            {
                sizing[vi] = baseLength;
                continue;
            }
            if (preserveBoundary && mesh.IsBoundary(vh))
            {
                sizing[vi] = std::clamp(baseLength, minLen, maxLen);
                continue;
            }

            double target = baseLength;
            if (useErrorBoundedTaubin)
            {
                const double kMax = std::abs(curvField.MaxPrincipalCurvatureProperty[vh]);
                const double kMin = std::abs(curvField.MinPrincipalCurvatureProperty[vh]);
                const double k = std::max(kMax, kMin);
                if (std::isfinite(k) && k > 1.0e-12)
                {
                    const double e = params.ApproximationError;
                    const double radius = 1.0 / k;
                    const double radicand = 6.0 * e * radius - 3.0 * e * e;
                    target = radicand > 0.0 ? std::sqrt(radicand) : maxLen;
                }
                else
                {
                    target = maxLen;
                }
            }
            else
            {
                const double absH = std::abs(curvField.MeanCurvatureProperty[vh]);
                target = baseLength / (1.0 + alpha * absH);
            }
            sizing[vi] = std::clamp(target, minLen, maxLen);
        }
    }

    static std::size_t SplitLongEdges(
        HalfedgeMesh::Mesh& mesh,
        std::vector<double>& sizing,
        double minLen,
        double maxLen,
        std::size_t& remainingOps,
        std::size_t maxVertices,
        std::size_t maxEdges,
        const ReferenceProjector* projector)
    {
        std::size_t splitCount = 0;
        const bool capOps = remainingOps > 0;

        std::vector<EdgeHandle> toSplit;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;

            double localT = LocalTarget(sizing, mesh, e);
            double maxLenSq = (4.0 / 3.0) * localT;
            maxLenSq *= maxLenSq;
            if (EdgeLengthSq(mesh, e) > maxLenSq) toSplit.push_back(e);
        }

        for (const EdgeHandle e : toSplit)
        {
            if (mesh.IsDeleted(e)) continue;
            if (capOps && remainingOps == 0) break;
            if (maxVertices > 0 && mesh.VertexCount() >= maxVertices) break;
            if (maxEdges > 0 && mesh.EdgeCount() >= maxEdges) break;

            double localT = LocalTarget(sizing, mesh, e);
            double threshold = (4.0 / 3.0) * localT;
            if (EdgeLengthSq(mesh, e) <= threshold * threshold) continue;

            HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
            const std::size_t v0 = mesh.FromVertex(h).Index;
            const std::size_t v1 = mesh.ToVertex(h).Index;

            glm::vec3 mid = 0.5f * (mesh.Position(mesh.FromVertex(h)) + mesh.Position(mesh.ToVertex(h)));
            if (projector != nullptr && projector->Enabled && projector->ProjectSplitVertices)
            {
                mid = projector->ProjectPoint(mid);
            }

            const std::size_t oldSize = sizing.size();
            (void)mesh.Split(e, mid);
            ++splitCount;
            if (capOps && remainingOps > 0) --remainingOps;

            if (sizing.size() < mesh.VerticesSize())
            {
                const double avgSizing = std::clamp(0.5 * (sizing[v0] + sizing[v1]), minLen, maxLen);
                const std::size_t newSize = mesh.VerticesSize();
                sizing.resize(newSize, avgSizing);
                for (std::size_t i = oldSize; i < newSize; ++i) sizing[i] = avgSizing;
            }
        }

        if (sizing.size() < mesh.VerticesSize())
        {
            const double fallback = (minLen + maxLen) * 0.5;
            sizing.resize(mesh.VerticesSize(), fallback);
        }

        return splitCount;
    }

    static std::size_t CollapseShortEdges(
        HalfedgeMesh::Mesh& mesh,
        const std::vector<double>& sizing,
        bool preserveBoundary,
        std::size_t& remainingOps)
    {
        std::size_t collapseCount = 0;
        const bool capOps = remainingOps > 0;

        std::vector<EdgeHandle> toCollapse;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;

            const double localT = LocalTarget(sizing, mesh, e);
            const double minThreshold = (4.0 / 5.0) * localT;
            if (EdgeLengthSq(mesh, e) < minThreshold * minThreshold) toCollapse.push_back(e);
        }

        for (const EdgeHandle e : toCollapse)
        {
            if (mesh.IsDeleted(e)) continue;
            if (capOps && remainingOps == 0) break;

            const double localT = LocalTarget(sizing, mesh, e);
            const double minThreshold = (4.0 / 5.0) * localT;
            if (EdgeLengthSq(mesh, e) >= minThreshold * minThreshold) continue;
            if (preserveBoundary && mesh.IsBoundary(e)) continue;

            HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
            const VertexHandle v0 = mesh.FromVertex(h);
            const VertexHandle v1 = mesh.ToVertex(h);

            if (preserveBoundary && (mesh.IsBoundary(v0) || mesh.IsBoundary(v1))) continue;
            if (!mesh.IsCollapseOk(e)) continue;

            glm::vec3 mid = 0.5f * (mesh.Position(v0) + mesh.Position(v1));
            bool tooLong = false;

            const auto checkNeighbors = [&](VertexHandle v)
            {
                for (const HalfedgeHandle hc : mesh.HalfedgesAroundVertex(v))
                {
                    VertexHandle vn = mesh.ToVertex(hc);
                    if (vn != v0 && vn != v1)
                    {
                        const glm::vec3 d = mesh.Position(vn) - mid;
                        const double distSq = static_cast<double>(glm::dot(d, d));
                        const double maxT = std::max(sizing[v0.Index], sizing[v1.Index]);
                        const double upper = (4.0 / 3.0) * maxT;
                        if (distSq > upper * upper)
                        {
                            tooLong = true;
                            return;
                        }
                    }
                }
            };

            checkNeighbors(v0);
            if (!tooLong) checkNeighbors(v1);
            if (tooLong) continue;

            (void)mesh.Collapse(e, mid);
            ++collapseCount;
            if (capOps && remainingOps > 0) --remainingOps;
        }

        return collapseCount;
    }

    static void ProjectVerticesToReference(HalfedgeMesh::Mesh& mesh, const ReferenceProjector& projector, bool preserveBoundary)
    {
        if (!projector.Enabled || !projector.ProjectAfterSmoothing) return;

        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            VertexHandle v{static_cast<PropertyIndex>(vi)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v) || mesh.IsIsolated(v)) continue;
            if (preserveBoundary && mesh.IsBoundary(v)) continue;
            mesh.Position(v) = projector.ProjectPoint(mesh.Position(v));
        }
    }

    std::optional<AdaptiveRemeshingResult> AdaptiveRemesh(
        HalfedgeMesh::Mesh& mesh,
        const AdaptiveRemeshingParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() < 2) return std::nullopt;

        const double baseLength = MeanEdgeLength(mesh);
        if (baseLength <= 0.0) return std::nullopt;

        double minLen = params.MinEdgeLength;
        double maxLen = params.MaxEdgeLength;
        if (minLen <= 0.0) minLen = baseLength * 0.1;
        if (maxLen <= 0.0) maxLen = baseLength * 5.0;
        if (minLen > maxLen) std::swap(minLen, maxLen);

        AdaptiveRemeshingResult result{};
        std::vector<double> sizing;

        ReferenceProjector projector{};
        projector.ProjectSplitVertices = params.ProjectSplitVertices;
        projector.ProjectAfterSmoothing = params.ProjectAfterSmoothing;
        if (params.EnableReferenceProjection)
        {
            ReferenceProjectionParams projectionParams{};
            projectionParams.ReferenceProjectionK = params.ReferenceProjectionK;
            projectionParams.MaxReferenceProjectionDistance = params.MaxReferenceProjectionDistance;
            static_cast<void>(projector.Build(mesh, projectionParams));
        }

        const std::size_t initialVertices = mesh.VertexCount();
        const std::size_t initialEdges = mesh.EdgeCount();
        const double growthFactor = params.MaxTopologyGrowthFactor;
        const bool capGrowth = std::isfinite(growthFactor) && growthFactor > 0.0;
        const std::size_t maxVertices = capGrowth
            ? std::max<std::size_t>(initialVertices,
                static_cast<std::size_t>(static_cast<double>(initialVertices) * growthFactor))
            : 0;
        const std::size_t maxEdges = capGrowth
            ? std::max<std::size_t>(initialEdges,
                static_cast<std::size_t>(static_cast<double>(initialEdges) * growthFactor))
            : 0;

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            ComputeSizingField(mesh, baseLength, params, minLen, maxLen,
                params.PreserveBoundary, sizing);

            std::size_t remainingOps = params.MaxOpsPerIteration;

            const std::size_t splitThisIter = SplitLongEdges(mesh, sizing, minLen, maxLen,
                remainingOps, maxVertices, maxEdges, &projector);
            result.SplitCount += splitThisIter;

            const std::size_t collapseThisIter = CollapseShortEdges(mesh, sizing, params.PreserveBoundary, remainingOps);
            result.CollapseCount += collapseThisIter;

            std::size_t flipThisIter = 0;
            if (params.MaxOpsPerIteration == 0 || remainingOps > 0)
            {
                flipThisIter = EqualizeValenceByEdgeFlip(mesh, params.PreserveBoundary);
            }
            result.FlipCount += flipThisIter;

            MeshUtils::TangentialSmooth(mesh, params.Lambda, params.PreserveBoundary);
            ProjectVerticesToReference(mesh, projector, params.PreserveBoundary);

            if (mesh.HasGarbage()) mesh.GarbageCollection();

            result.IterationsPerformed = iter + 1;
            if (splitThisIter == 0 && collapseThisIter == 0 && flipThisIter == 0) break;
        }

        result.FinalVertexCount = mesh.VertexCount();
        result.FinalEdgeCount = mesh.EdgeCount();
        result.FinalFaceCount = mesh.FaceCount();
        return result;
    }
}
