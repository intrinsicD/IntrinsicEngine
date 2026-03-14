module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:AdaptiveRemeshing.Impl;

import :AdaptiveRemeshing;
import :Properties;
import :HalfedgeMesh;
import :Curvature;
import :MeshUtils;
import :KDTree;
import :AABB;

namespace Geometry::AdaptiveRemeshing
{
    using MeshUtils::EdgeLengthSq;
    using MeshUtils::MeanEdgeLength;
    using MeshUtils::EqualizeValenceByEdgeFlip;

    namespace
    {
        struct ReferenceProjector
        {
            bool Enabled{false};
            bool ProjectSplitVertices{true};
            bool ProjectAfterSmoothing{true};
            std::uint32_t K{16};
            float MaxDistanceSq{0.0f};
            std::vector<std::array<glm::vec3, 3>> Triangles{};
            KDTree Tree{};

            [[nodiscard]] static glm::vec3 ClosestPointOnTriangle(
                const glm::vec3& p,
                const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c)
            {
                const glm::vec3 ab = b - a;
                const glm::vec3 ac = c - a;
                const glm::vec3 ap = p - a;
                const float d1 = glm::dot(ab, ap);
                const float d2 = glm::dot(ac, ap);
                if (d1 <= 0.0f && d2 <= 0.0f) return a;

                const glm::vec3 bp = p - b;
                const float d3 = glm::dot(ab, bp);
                const float d4 = glm::dot(ac, bp);
                if (d3 >= 0.0f && d4 <= d3) return b;

                const float vc = d1 * d4 - d3 * d2;
                if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
                {
                    const float v = d1 / (d1 - d3);
                    return a + v * ab;
                }

                const glm::vec3 cp = p - c;
                const float d5 = glm::dot(ab, cp);
                const float d6 = glm::dot(ac, cp);
                if (d6 >= 0.0f && d5 <= d6) return c;

                const float vb = d5 * d2 - d1 * d6;
                if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
                {
                    const float w = d2 / (d2 - d6);
                    return a + w * ac;
                }

                const float va = d3 * d6 - d5 * d4;
                if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
                {
                    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                    return b + w * (c - b);
                }

                const float denom = 1.0f / (va + vb + vc);
                const float v = vb * denom;
                const float w = vc * denom;
                return a + ab * v + ac * w;
            }

            [[nodiscard]] glm::vec3 Project(const glm::vec3& point) const
            {
                if (!Enabled || Triangles.empty()) return point;

                std::vector<KDTree::ElementIndex> candidates;
                const auto knn = Tree.QueryKnn(point, std::max<std::uint32_t>(K, 1u), candidates);
                if (!knn.has_value() || candidates.empty()) return point;

                float bestDistSq = std::numeric_limits<float>::max();
                glm::vec3 best = point;

                for (const KDTree::ElementIndex idx : candidates)
                {
                    if (idx >= Triangles.size()) continue;
                    const auto& tri = Triangles[idx];
                    const glm::vec3 q = ClosestPointOnTriangle(point, tri[0], tri[1], tri[2]);
                    const glm::vec3 d = q - point;
                    const float distSq = glm::dot(d, d);
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        best = q;
                    }
                }

                if (MaxDistanceSq > 0.0f && bestDistSq > MaxDistanceSq) return point;
                return best;
            }

            [[nodiscard]] bool Build(const Halfedge::Mesh& mesh, const AdaptiveRemeshingParams& params)
            {
                Enabled = false;
                Triangles.clear();
                ProjectSplitVertices = params.ProjectSplitVertices;
                ProjectAfterSmoothing = params.ProjectAfterSmoothing;

                if (!params.EnableReferenceProjection) return false;

                std::vector<AABB> triAabbs;
                triAabbs.reserve(mesh.FaceCount());
                Triangles.reserve(mesh.FaceCount());

                for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
                {
                    FaceHandle f{static_cast<PropertyIndex>(fi)};
                    if (!mesh.IsValid(f) || mesh.IsDeleted(f)) continue;

                    const HalfedgeHandle h0 = mesh.Halfedge(f);
                    const HalfedgeHandle h1 = mesh.NextHalfedge(h0);
                    const HalfedgeHandle h2 = mesh.NextHalfedge(h1);
                    const HalfedgeHandle h3 = mesh.NextHalfedge(h2);
                    if (h3 != h0) continue;

                    const glm::vec3 p0 = mesh.Position(mesh.ToVertex(h0));
                    const glm::vec3 p1 = mesh.Position(mesh.ToVertex(h1));
                    const glm::vec3 p2 = mesh.Position(mesh.ToVertex(h2));

                    Triangles.push_back({p0, p1, p2});
                    triAabbs.push_back(AABB{
                        .Min = glm::min(p0, glm::min(p1, p2)),
                        .Max = glm::max(p0, glm::max(p1, p2))});
                }

                if (Triangles.empty()) return false;

                KDTreeBuildParams buildParams{};
                buildParams.LeafSize = 16;
                buildParams.MaxDepth = 32;
                buildParams.MinSplitExtent = 1.0e-6f;
                if (!Tree.Build(std::move(triAabbs), buildParams).has_value()) return false;

                K = static_cast<std::uint32_t>(std::max<std::size_t>(params.ReferenceProjectionK, 1u));
                const double maxDist = params.MaxReferenceProjectionDistance;
                MaxDistanceSq = (std::isfinite(maxDist) && maxDist > 0.0)
                    ? static_cast<float>(maxDist * maxDist)
                    : 0.0f;

                Enabled = true;
                return true;
            }
        };
    }

    // Per-edge local target = average of endpoint sizing fields
    static double LocalTarget(
        const std::vector<double>& sizing,
        const Halfedge::Mesh& mesh,
        EdgeHandle e)
    {
        HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
        const std::size_t v0 = mesh.FromVertex(h).Index;
        const std::size_t v1 = mesh.ToVertex(h).Index;
        return 0.5 * (sizing[v0] + sizing[v1]);
    }

    static void ComputeSizingField(
        Halfedge::Mesh& mesh,
        double baseLength,
        double alpha,
        double minLen,
        double maxLen,
        bool preserveBoundary,
        std::vector<double>& sizing)
    {
        sizing.resize(mesh.VerticesSize(), baseLength);

        if (alpha < 1e-12)
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

            const double absH = std::abs(curvField.MeanCurvatureProperty[vh]);
            const double target = baseLength / (1.0 + alpha * absH);
            sizing[vi] = std::clamp(target, minLen, maxLen);
        }
    }

    static std::size_t SplitLongEdges(
        Halfedge::Mesh& mesh,
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
                mid = projector->Project(mid);
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
        Halfedge::Mesh& mesh,
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

    static void ProjectVerticesToReference(Halfedge::Mesh& mesh, const ReferenceProjector& projector, bool preserveBoundary)
    {
        if (!projector.Enabled || !projector.ProjectAfterSmoothing) return;

        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            VertexHandle v{static_cast<PropertyIndex>(vi)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v) || mesh.IsIsolated(v)) continue;
            if (preserveBoundary && mesh.IsBoundary(v)) continue;
            mesh.Position(v) = projector.Project(mesh.Position(v));
        }
    }

    std::optional<AdaptiveRemeshingResult> AdaptiveRemesh(
        Halfedge::Mesh& mesh,
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
        static_cast<void>(projector.Build(mesh, params));

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
            ComputeSizingField(mesh, baseLength, params.CurvatureAdaptation, minLen, maxLen,
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
