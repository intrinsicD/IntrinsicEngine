module;

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

module Geometry.Boolean;

import Geometry.AABB;
import Geometry.HalfedgeMesh;
import Geometry.MeshUtils;

namespace Geometry::Boolean
{
    namespace
    {
        struct Triangle
        {
            glm::vec3 A{};
            glm::vec3 B{};
            glm::vec3 C{};
        };

        struct MeshExtract
        {
            std::vector<glm::vec3> Vertices;
            std::vector<std::array<uint32_t, 3>> Triangles;
            AABB Bounds{};
        };

        [[nodiscard]] bool RayTriangleIntersect(const glm::vec3& origin, const glm::vec3& dir,
                                                const Triangle& t, float eps, float& outT)
        {
            const glm::vec3 e1 = t.B - t.A;
            const glm::vec3 e2 = t.C - t.A;
            const glm::vec3 p = glm::cross(dir, e2);
            const float det = glm::dot(e1, p);
            if (std::abs(det) <= eps)
                return false;

            const float invDet = 1.0f / det;
            const glm::vec3 s = origin - t.A;
            const float u = invDet * glm::dot(s, p);
            if (u < -eps || u > 1.0f + eps)
                return false;

            const glm::vec3 q = glm::cross(s, e1);
            const float v = invDet * glm::dot(dir, q);
            if (v < -eps || (u + v) > 1.0f + eps)
                return false;

            const float tHit = invDet * glm::dot(e2, q);
            if (tHit <= eps)
                return false;

            outT = tHit;
            return true;
        }

        [[nodiscard]] bool IsPointInsideMesh(const MeshExtract& mesh, const glm::vec3& p, float eps)
        {
            // Deterministic non-axis aligned direction to avoid edge-aligned degeneracies.
            const glm::vec3 rayDir = glm::normalize(glm::vec3{0.743f, 0.533f, 0.405f});
            int hitCount = 0;
            for (const auto& triIdx : mesh.Triangles)
            {
                const Triangle tri{mesh.Vertices[triIdx[0]], mesh.Vertices[triIdx[1]], mesh.Vertices[triIdx[2]]};
                float t = 0.0f;
                if (RayTriangleIntersect(p, rayDir, tri, eps, t))
                    ++hitCount;
            }
            return (hitCount & 1) != 0;
        }

        [[nodiscard]] MeshExtract Extract(const Halfedge::Mesh& mesh)
        {
            MeshExtract out;
            out.Vertices.reserve(mesh.VertexCount());

            std::vector<int32_t> remap(mesh.VerticesSize(), -1);
            for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
            {
                VertexHandle vh{static_cast<PropertyIndex>(vi)};
                if (mesh.IsDeleted(vh))
                    continue;
                remap[vi] = static_cast<int32_t>(out.Vertices.size());
                const glm::vec3 p = mesh.Position(vh);
                out.Vertices.push_back(p);
                out.Bounds.Min = glm::min(out.Bounds.Min, p);
                out.Bounds.Max = glm::max(out.Bounds.Max, p);
            }

            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                FaceHandle fh{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(fh))
                    continue;

                std::vector<uint32_t> face;
                for (const VertexHandle v : mesh.VerticesAroundFace(fh))
                {
                    const uint32_t vi = v.Index;
                    const int32_t mapped = (vi < remap.size()) ? remap[vi] : -1;
                    if (mapped >= 0)
                        face.push_back(static_cast<uint32_t>(mapped));
                }

                if (face.size() < 3)
                    continue;

                for (std::size_t i = 1; i + 1 < face.size(); ++i)
                    out.Triangles.push_back({face[0], face[i], face[i + 1]});
            }

            return out;
        }

        void AppendMesh(const Halfedge::Mesh& src, Halfedge::Mesh& dst)
        {
            std::vector<VertexHandle> remap(src.VerticesSize(), VertexHandle{});
            for (std::size_t vi = 0; vi < src.VerticesSize(); ++vi)
            {
                VertexHandle vh{static_cast<PropertyIndex>(vi)};
                if (src.IsDeleted(vh))
                    continue;
                remap[vi] = dst.AddVertex(src.Position(vh));
            }

            for (std::size_t fi = 0; fi < src.FacesSize(); ++fi)
            {
                FaceHandle fh{static_cast<PropertyIndex>(fi)};
                if (src.IsDeleted(fh))
                    continue;

                std::vector<VertexHandle> face;
                for (const VertexHandle v : src.VerticesAroundFace(fh))
                {
                    const uint32_t vid = v.Index;
                    if (vid < remap.size())
                        face.push_back(remap[vid]);
                }

                if (face.size() >= 3)
                    (void)dst.AddFace(face);
            }
        }

        [[nodiscard]] bool AABBOverlap(const AABB& a, const AABB& b)
        {
            return (a.Min.x <= b.Max.x && a.Max.x >= b.Min.x) &&
                   (a.Min.y <= b.Max.y && a.Max.y >= b.Min.y) &&
                   (a.Min.z <= b.Max.z && a.Max.z >= b.Min.z);
        }

        [[nodiscard]] bool AllVerticesInsideMesh(const MeshExtract& container,
                                                 const MeshExtract& candidate,
                                                 const float eps)
        {
            if (candidate.Vertices.empty())
                return false;

            for (const glm::vec3& v : candidate.Vertices)
            {
                if (!IsPointInsideMesh(container, v, eps))
                    return false;
            }
            return true;
        }
    }

    std::optional<BooleanResult> Compute(const Halfedge::Mesh& a, const Halfedge::Mesh& b, const Operation op,
                                         Halfedge::Mesh& out, const BooleanParams& params)
    {
        if (a.IsEmpty() || b.IsEmpty())
            return std::nullopt;

        const MeshExtract ma = Extract(a);
        const MeshExtract mb = Extract(b);
        if (ma.Triangles.empty() || mb.Triangles.empty())
            return std::nullopt;

        const bool aabbOverlap = AABBOverlap(ma.Bounds, mb.Bounds);
        const bool aInsideB = AllVerticesInsideMesh(mb, ma, params.Epsilon);
        const bool bInsideA = AllVerticesInsideMesh(ma, mb, params.Epsilon);
        const bool overlap = aabbOverlap && (aInsideB || bInsideA);

        out.Clear();
        BooleanResult result{};
        result.UsedFallback = true;
        result.VolumesOverlap = overlap;

        if (!aabbOverlap)
        {
            result.ExactResult = true;
            result.Diagnostic = "disjoint volumes";
            switch (op)
            {
            case Operation::Union:
                AppendMesh(a, out);
                AppendMesh(b, out);
                break;
            case Operation::Intersection:
                break;
            case Operation::Difference:
                AppendMesh(a, out);
                break;
            }
            return result;
        }

        if (aInsideB || bInsideA)
        {
            result.ExactResult = true;
            result.Diagnostic = "full containment";
            switch (op)
            {
            case Operation::Union:
                AppendMesh(aInsideB ? b : a, out);
                break;
            case Operation::Intersection:
                AppendMesh(aInsideB ? a : b, out);
                break;
            case Operation::Difference:
                if (aInsideB)
                {
                    // A \ B is empty if A is fully inside B.
                }
                else
                {
                    // B is inside A; robust shell subtraction requires face splitting.
                    return std::nullopt;
                }
                break;
            }
            return result;
        }

        // Partial-overlap CSG requires robust triangle clipping + stitching.
        return std::nullopt;
    }
}
