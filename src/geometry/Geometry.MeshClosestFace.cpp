module;

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
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
        bool Built{false};
    };

    namespace
    {
        [[nodiscard]] float PointAabbSquaredDistance(const AABB& aabb, const glm::vec3& p)
        {
            const glm::vec3 cp = ClosestPoint(aabb, p);
            const glm::vec3 d = p - cp;
            return glm::dot(d, d);
        }
    }

    MeshClosestFaceIndex::MeshClosestFaceIndex() : m_Impl(std::make_unique<Impl>()) {}
    MeshClosestFaceIndex::~MeshClosestFaceIndex() = default;
    MeshClosestFaceIndex::MeshClosestFaceIndex(MeshClosestFaceIndex&&) noexcept = default;
    MeshClosestFaceIndex& MeshClosestFaceIndex::operator=(MeshClosestFaceIndex&&) noexcept = default;

    bool MeshClosestFaceIndex::IsBuilt() const noexcept { return m_Impl && m_Impl->Built; }
    std::size_t MeshClosestFaceIndex::FaceCount() const noexcept { return m_Impl ? m_Impl->FaceForElement.size() : 0u; }

    bool MeshClosestFaceIndex::Build(const HalfedgeMesh::Mesh& mesh)
    {
        Impl& impl = *m_Impl;
        impl.Bvh.Clear();
        impl.FaceForElement.clear();
        impl.TriVertices.clear();
        impl.TriBegin.clear();
        impl.TriCount.clear();
        impl.Built = false;

        std::vector<AABB> aabbs;
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            const FaceHandle f{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(f) || !mesh.IsValid(f))
            {
                continue;
            }

            // Collect the face's corner positions.
            std::vector<glm::vec3> corners;
            for (const VertexHandle v : mesh.VerticesAroundFace(f))
            {
                corners.push_back(mesh.Position(v));
            }
            if (corners.size() < 3)
            {
                continue;
            }

            AABB box{};
            for (const glm::vec3& c : corners)
            {
                box.Min = glm::min(box.Min, c);
                box.Max = glm::max(box.Max, c);
            }

            const std::uint32_t triBegin = static_cast<std::uint32_t>(impl.TriVertices.size());
            std::uint32_t triCount = 0;
            // Fan triangulation: (c0, c_i, c_{i+1}).
            for (std::size_t i = 1; i + 1 < corners.size(); ++i)
            {
                impl.TriVertices.push_back(corners[0]);
                impl.TriVertices.push_back(corners[i]);
                impl.TriVertices.push_back(corners[i + 1]);
                ++triCount;
            }
            if (triCount == 0)
            {
                continue;
            }

            impl.FaceForElement.push_back(f);
            impl.TriBegin.push_back(triBegin);
            impl.TriCount.push_back(triCount);
            aabbs.push_back(box);
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
            return false;
        }
        impl.Built = true;
        return true;
    }

    MeshClosestFaceResult MeshClosestFaceIndex::Query(const glm::vec3& point) const
    {
        MeshClosestFaceResult result{};
        const Impl& impl = *m_Impl;
        if (!impl.Built)
        {
            return result;
        }
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            return result;
        }
        const std::vector<BVH::Node>& nodes = impl.Bvh.Nodes();
        if (nodes.empty())
        {
            return result;
        }
        const std::vector<BVH::ElementIndex>& elementIndices = impl.Bvh.ElementIndices();

        float best = std::numeric_limits<float>::max();
        std::vector<BVH::NodeIndex> stack;
        stack.reserve(64);
        stack.push_back(0u);

        while (!stack.empty())
        {
            const BVH::NodeIndex ni = stack.back();
            stack.pop_back();
            const BVH::Node& node = nodes[ni];

            if (PointAabbSquaredDistance(node.Aabb, point) >= best)
            {
                continue; // prune
            }

            if (node.IsLeaf)
            {
                const std::size_t end = static_cast<std::size_t>(node.FirstElement) + node.NumElements;
                for (std::size_t i = node.FirstElement; i < end; ++i)
                {
                    const BVH::ElementIndex elem = elementIndices[i];
                    const std::uint32_t triBegin = impl.TriBegin[elem];
                    const std::uint32_t triCount = impl.TriCount[elem];
                    for (std::uint32_t t = 0; t < triCount; ++t)
                    {
                        const std::size_t base = static_cast<std::size_t>(triBegin) + static_cast<std::size_t>(t) * 3u;
                        const Triangle tri{impl.TriVertices[base], impl.TriVertices[base + 1], impl.TriVertices[base + 2]};
                        const float sd = static_cast<float>(SquaredDistance(tri, point));
                        if (sd < best)
                        {
                            best = sd;
                            result.Found = true;
                            result.Face = impl.FaceForElement[elem];
                            result.SquaredDistance = sd;
                            result.Point = ClosestPoint(tri, point);
                        }
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

        return result;
    }
}
