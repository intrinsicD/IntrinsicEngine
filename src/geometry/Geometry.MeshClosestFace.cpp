module;

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

module Geometry.MeshClosestFace;

import Geometry.AABB;
import Geometry.BVH;
import Geometry.Triangle;
import Geometry.HalfedgeMesh;

namespace Geometry
{
    struct MeshClosestFaceIndex::Impl
    {
        BVH Bvh;
        std::vector<FaceHandle> FaceForElement;     // element index -> face
        std::vector<glm::vec3> TriVertices;         // 3 verts per triangle, fan-triangulated
        std::vector<std::uint32_t> TriBegin;        // element index -> first triangle * 3 offset
        std::vector<std::uint32_t> TriCount;        // element index -> triangle count
        std::vector<std::uint32_t> TriPrimitive;    // triangle index -> stable primitive id
        bool Built{false};
    };

    namespace
    {
        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) &&
                   std::isfinite(p.y) &&
                   std::isfinite(p.z);
        }

        [[nodiscard]] float PointAabbSquaredDistance(const AABB& aabb, const glm::vec3& p)
        {
            const glm::vec3 cp = ClosestPoint(aabb, p);
            const glm::vec3 d = p - cp;
            return glm::dot(d, d);
        }

        [[nodiscard]] bool IsUsableTriangle(const Triangle& tri) noexcept
        {
            if (!IsFinite(tri.A) || !IsFinite(tri.B) || !IsFinite(tri.C))
                return false;

            const glm::vec3 normal = glm::cross(tri.B - tri.A, tri.C - tri.A);
            return std::isfinite(normal.x) &&
                   std::isfinite(normal.y) &&
                   std::isfinite(normal.z) &&
                   glm::dot(normal, normal) > 1.0e-12f;
        }

        [[nodiscard]] bool HitLess(const MeshClosestFaceHit& lhs,
                                   const MeshClosestFaceHit& rhs) noexcept
        {
            if (lhs.SquaredDistance != rhs.SquaredDistance)
                return lhs.SquaredDistance < rhs.SquaredDistance;
            if (lhs.Face.Index != rhs.Face.Index)
                return lhs.Face.Index < rhs.Face.Index;
            return lhs.PrimitiveIndex < rhs.PrimitiveIndex;
        }

        void InsertSortedHit(std::vector<MeshClosestFaceHit>& hits,
                             MeshClosestFaceHit hit)
        {
            auto it = std::lower_bound(
                hits.begin(),
                hits.end(),
                hit,
                [](const MeshClosestFaceHit& lhs,
                   const MeshClosestFaceHit& rhs)
                {
                    return HitLess(lhs, rhs);
                });
            hits.insert(it, hit);
        }

        template <typename ImplT>
        [[nodiscard]] std::optional<MeshClosestFaceHit> EvaluateElement(
            const ImplT& impl,
            const BVH::ElementIndex elem,
            const glm::vec3& point,
            std::size_t& distanceEvaluations)
        {
            std::optional<MeshClosestFaceHit> best{};
            const std::uint32_t triBegin = impl.TriBegin[elem];
            const std::uint32_t triCount = impl.TriCount[elem];
            for (std::uint32_t t = 0; t < triCount; ++t)
            {
                const std::size_t triIndex =
                    static_cast<std::size_t>(triBegin) +
                    static_cast<std::size_t>(t);
                const std::size_t base = triIndex * 3u;
                const Triangle tri{
                    impl.TriVertices[base],
                    impl.TriVertices[base + 1u],
                    impl.TriVertices[base + 2u]};
                ++distanceEvaluations;

                const float sd = static_cast<float>(SquaredDistance(tri, point));
                const glm::vec3 closest = ClosestPoint(tri, point);
                const glm::vec3 normal = tri.GetNormal();
                if (!std::isfinite(sd) || !IsFinite(closest) || !IsFinite(normal))
                    continue;

                MeshClosestFaceHit hit{
                    .Face = impl.FaceForElement[elem],
                    .Point = closest,
                    .Normal = normal,
                    .SquaredDistance = sd,
                    .PrimitiveIndex = impl.TriPrimitive[triIndex],
                };
                if (!best.has_value() || HitLess(hit, *best))
                    best = hit;
            }
            return best;
        }

        template <typename ImplT>
        [[nodiscard]] bool AddFaceToIndex(
            const HalfedgeMesh::Mesh& mesh,
            const FaceHandle f,
            ImplT& impl,
            std::vector<AABB>& aabbs,
            std::uint32_t& primitiveIndex)
        {
            if (!f.IsValid() || !mesh.IsValid(f) || mesh.IsDeleted(f))
                return false;

            std::vector<glm::vec3> corners;
            for (const VertexHandle v : mesh.VerticesAroundFace(f))
            {
                if (!v.IsValid() || !mesh.IsValid(v) || mesh.IsDeleted(v))
                    continue;
                const glm::vec3 p = mesh.Position(v);
                if (!IsFinite(p))
                    continue;
                corners.push_back(p);
            }
            if (corners.size() < 3u)
                return false;

            const std::uint32_t triBegin =
                static_cast<std::uint32_t>(impl.TriVertices.size() / 3u);
            std::uint32_t triCount = 0u;
            AABB box{};
            for (std::size_t i = 1u; i + 1u < corners.size(); ++i)
            {
                const Triangle tri{corners[0], corners[i], corners[i + 1u]};
                if (!IsUsableTriangle(tri))
                    continue;

                impl.TriVertices.push_back(tri.A);
                impl.TriVertices.push_back(tri.B);
                impl.TriVertices.push_back(tri.C);
                impl.TriPrimitive.push_back(primitiveIndex++);
                ++triCount;

                box.Min = glm::min(box.Min, tri.A);
                box.Min = glm::min(box.Min, tri.B);
                box.Min = glm::min(box.Min, tri.C);
                box.Max = glm::max(box.Max, tri.A);
                box.Max = glm::max(box.Max, tri.B);
                box.Max = glm::max(box.Max, tri.C);
            }
            if (triCount == 0u || !box.IsValid())
                return false;

            impl.FaceForElement.push_back(f);
            impl.TriBegin.push_back(triBegin);
            impl.TriCount.push_back(triCount);
            aabbs.push_back(box);
            return true;
        }
    }

    const char* DebugName(const MeshClosestFaceStatus status) noexcept
    {
        switch (status)
        {
        case MeshClosestFaceStatus::Success:
            return "Success";
        case MeshClosestFaceStatus::UnbuiltIndex:
            return "UnbuiltIndex";
        case MeshClosestFaceStatus::EmptyIndex:
            return "EmptyIndex";
        case MeshClosestFaceStatus::InvalidQueryPoint:
            return "InvalidQueryPoint";
        case MeshClosestFaceStatus::InvalidParameter:
            return "InvalidParameter";
        }
        return "Unknown";
    }

    MeshClosestFaceIndex::MeshClosestFaceIndex() : m_Impl(std::make_unique<Impl>()) {}
    MeshClosestFaceIndex::~MeshClosestFaceIndex() = default;
    MeshClosestFaceIndex::MeshClosestFaceIndex(MeshClosestFaceIndex&&) noexcept = default;
    MeshClosestFaceIndex& MeshClosestFaceIndex::operator=(MeshClosestFaceIndex&&) noexcept = default;

    bool MeshClosestFaceIndex::IsBuilt() const noexcept { return m_Impl && m_Impl->Built; }
    std::size_t MeshClosestFaceIndex::FaceCount() const noexcept { return m_Impl ? m_Impl->FaceForElement.size() : 0u; }

    bool MeshClosestFaceIndex::Build(const HalfedgeMesh::Mesh& mesh)
    {
        if (!m_Impl)
            m_Impl = std::make_unique<Impl>();
        Impl& impl = *m_Impl;
        impl.Bvh.Clear();
        impl.FaceForElement.clear();
        impl.TriVertices.clear();
        impl.TriBegin.clear();
        impl.TriCount.clear();
        impl.TriPrimitive.clear();
        impl.Built = false;

        std::vector<AABB> aabbs;
        std::uint32_t primitiveIndex = 0u;
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            const FaceHandle f{static_cast<PropertyIndex>(fi)};
            (void)AddFaceToIndex(mesh, f, impl, aabbs, primitiveIndex);
        }

        if (aabbs.empty())
            return false;

        const auto built = impl.Bvh.Build(std::move(aabbs));
        if (!built.has_value())
        {
            impl.FaceForElement.clear();
            impl.TriVertices.clear();
            impl.TriBegin.clear();
            impl.TriCount.clear();
            impl.TriPrimitive.clear();
            return false;
        }

        impl.Built = true;
        return true;
    }

    bool MeshClosestFaceIndex::Build(
        const HalfedgeMesh::Mesh& mesh,
        const std::span<const FaceHandle> faces)
    {
        if (!m_Impl)
            m_Impl = std::make_unique<Impl>();
        Impl& impl = *m_Impl;
        impl.Bvh.Clear();
        impl.FaceForElement.clear();
        impl.TriVertices.clear();
        impl.TriBegin.clear();
        impl.TriCount.clear();
        impl.TriPrimitive.clear();
        impl.Built = false;

        std::vector<AABB> aabbs;
        std::uint32_t primitiveIndex = 0u;
        for (const FaceHandle f : faces)
        {
            (void)AddFaceToIndex(mesh, f, impl, aabbs, primitiveIndex);
        }

        if (aabbs.empty())
        {
            return false;
        }

        const auto built = impl.Bvh.Build(std::move(aabbs));
        if (!built.has_value())
        {
            impl.FaceForElement.clear();
            impl.TriVertices.clear();
            impl.TriBegin.clear();
            impl.TriCount.clear();
            impl.TriPrimitive.clear();
            return false;
        }
        impl.Built = true;
        return true;
    }

    MeshClosestFaceResult MeshClosestFaceIndex::Query(const glm::vec3& point) const
    {
        MeshClosestFaceResult result{};
        MeshClosestFaceKNearestResult nearest = QueryKNearest(point, 1u);
        result.Status = nearest.Status;
        result.Diagnostics = nearest.Diagnostics;
        if (nearest.Hits.empty())
        {
            return result;
        }
        const MeshClosestFaceHit& hit = nearest.Hits.front();
        result.Found = true;
        result.Face = hit.Face;
        result.Point = hit.Point;
        result.Normal = hit.Normal;
        result.SquaredDistance = hit.SquaredDistance;
        result.PrimitiveIndex = hit.PrimitiveIndex;
        return result;
    }

    MeshClosestFaceKNearestResult MeshClosestFaceIndex::QueryKNearest(
        const glm::vec3& point,
        const std::size_t k) const
    {
        MeshClosestFaceKNearestResult result{};
        if (k == 0u)
        {
            result.Status = MeshClosestFaceStatus::InvalidParameter;
            return result;
        }

        if (!m_Impl)
        {
            result.Status = MeshClosestFaceStatus::UnbuiltIndex;
            return result;
        }
        const Impl& impl = *m_Impl;
        if (!impl.Built)
        {
            result.Status = MeshClosestFaceStatus::UnbuiltIndex;
            return result;
        }
        if (!IsFinite(point))
        {
            result.Status = MeshClosestFaceStatus::InvalidQueryPoint;
            return result;
        }
        const std::vector<BVH::Node>& nodes = impl.Bvh.Nodes();
        if (nodes.empty())
        {
            result.Status = MeshClosestFaceStatus::EmptyIndex;
            return result;
        }

        const std::vector<BVH::ElementIndex>& elementIndices = impl.Bvh.ElementIndices();

        std::vector<BVH::NodeIndex> stack;
        stack.reserve(64);
        stack.push_back(0u);

        while (!stack.empty())
        {
            const BVH::NodeIndex ni = stack.back();
            stack.pop_back();
            const BVH::Node& node = nodes[ni];
            ++result.Diagnostics.VisitedNodes;

            const bool hasFullSet = result.Hits.size() >= k;
            const float worstKept =
                hasFullSet
                    ? result.Hits.back().SquaredDistance
                    : std::numeric_limits<float>::max();
            if (hasFullSet &&
                PointAabbSquaredDistance(node.Aabb, point) > worstKept)
            {
                continue; // prune
            }

            if (node.IsLeaf)
            {
                const std::size_t end = static_cast<std::size_t>(node.FirstElement) + node.NumElements;
                for (std::size_t i = node.FirstElement; i < end; ++i)
                {
                    const BVH::ElementIndex elem = elementIndices[i];
                    std::optional<MeshClosestFaceHit> hit = EvaluateElement(
                        impl,
                        elem,
                        point,
                        result.Diagnostics.DistanceEvaluations);
                    if (!hit.has_value())
                        continue;

                    const bool shouldKeep =
                        result.Hits.size() < k ||
                        HitLess(*hit, result.Hits.back());
                    if (!shouldKeep)
                        continue;

                    InsertSortedHit(result.Hits, *hit);
                    if (result.Hits.size() > k)
                    {
                        result.Hits.pop_back();
                    }
                }
                continue;
            }

            // Visit the nearer child first for better pruning.
            const BVH::NodeIndex left = node.Left;
            const BVH::NodeIndex right = node.Right;
            const float dl = PointAabbSquaredDistance(nodes[left].Aabb, point);
            const float dr = PointAabbSquaredDistance(nodes[right].Aabb, point);
            if (dl < dr)
            {
                stack.push_back(right); // farther pushed first (popped last)
                stack.push_back(left);
            }
            else
            {
                stack.push_back(left);
                stack.push_back(right);
            }
        }

        result.Status = MeshClosestFaceStatus::Success;
        result.Diagnostics.ReturnedCount = result.Hits.size();
        result.Diagnostics.MaxDistanceSquared =
            result.Hits.empty() ? 0.0f : result.Hits.back().SquaredDistance;
        return result;
    }

    MeshClosestFaceRadiusResult MeshClosestFaceIndex::QueryRadius(
        const glm::vec3& point,
        const float radius) const
    {
        MeshClosestFaceRadiusResult result{};
        if (!std::isfinite(radius) || radius < 0.0f)
        {
            result.Status = MeshClosestFaceStatus::InvalidParameter;
            return result;
        }

        if (!m_Impl)
        {
            result.Status = MeshClosestFaceStatus::UnbuiltIndex;
            return result;
        }
        const Impl& impl = *m_Impl;
        if (!impl.Built)
        {
            result.Status = MeshClosestFaceStatus::UnbuiltIndex;
            return result;
        }
        if (!IsFinite(point))
        {
            result.Status = MeshClosestFaceStatus::InvalidQueryPoint;
            return result;
        }
        const std::vector<BVH::Node>& nodes = impl.Bvh.Nodes();
        if (nodes.empty())
        {
            result.Status = MeshClosestFaceStatus::EmptyIndex;
            return result;
        }

        const float radiusSq = radius * radius;
        const std::vector<BVH::ElementIndex>& elementIndices = impl.Bvh.ElementIndices();
        std::vector<BVH::NodeIndex> stack;
        stack.reserve(64);
        stack.push_back(0u);

        while (!stack.empty())
        {
            const BVH::NodeIndex ni = stack.back();
            stack.pop_back();
            const BVH::Node& node = nodes[ni];
            ++result.Diagnostics.VisitedNodes;

            if (PointAabbSquaredDistance(node.Aabb, point) > radiusSq)
                continue;

            if (node.IsLeaf)
            {
                const std::size_t end =
                    static_cast<std::size_t>(node.FirstElement) +
                    node.NumElements;
                for (std::size_t i = node.FirstElement; i < end; ++i)
                {
                    const BVH::ElementIndex elem = elementIndices[i];
                    std::optional<MeshClosestFaceHit> hit = EvaluateElement(
                        impl,
                        elem,
                        point,
                        result.Diagnostics.DistanceEvaluations);
                    if (hit.has_value() && hit->SquaredDistance <= radiusSq)
                    {
                        result.Hits.push_back(*hit);
                    }
                }
                continue;
            }

            const BVH::NodeIndex left = node.Left;
            const BVH::NodeIndex right = node.Right;
            const float dl = PointAabbSquaredDistance(nodes[left].Aabb, point);
            const float dr = PointAabbSquaredDistance(nodes[right].Aabb, point);
            if (dl < dr)
            {
                stack.push_back(right);
                stack.push_back(left);
            }
            else
            {
                stack.push_back(left);
                stack.push_back(right);
            }
        }

        std::sort(
            result.Hits.begin(),
            result.Hits.end(),
            [](const MeshClosestFaceHit& lhs, const MeshClosestFaceHit& rhs)
            {
                return HitLess(lhs, rhs);
            });
        result.Status = MeshClosestFaceStatus::Success;
        result.Diagnostics.ReturnedCount = result.Hits.size();
        return result;
    }
}
