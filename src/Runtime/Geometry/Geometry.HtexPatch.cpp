module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

module Geometry:HtexPatch.Impl;

import :HtexPatch;

namespace Geometry::HtexPatch
{
    namespace
    {
        [[nodiscard]] std::uint16_t BucketResolution(float target, const PatchBuildParams& params) noexcept
        {
            constexpr std::array<std::uint16_t, 5> kBuckets{8u, 16u, 32u, 64u, 128u};

            const float minRes = static_cast<float>(std::max<std::uint16_t>(1u, params.MinResolution));
            const float maxRes = static_cast<float>(std::max(params.MinResolution, params.MaxResolution));
            const float clampedTarget = std::clamp(target, minRes, maxRes);

            for (const std::uint16_t bucket : kBuckets)
            {
                if (clampedTarget <= static_cast<float>(bucket))
                    return std::clamp(bucket, params.MinResolution, params.MaxResolution);
            }

            return std::clamp(kBuckets.back(), params.MinResolution, params.MaxResolution);
        }

        [[nodiscard]] double PolygonArea(const Halfedge::Mesh& mesh, FaceHandle f) noexcept
        {
            if (!f.IsValid() || mesh.IsDeleted(f))
                return 0.0;

            const HalfedgeHandle h0 = mesh.Halfedge(f);
            if (!mesh.IsValid(h0))
                return 0.0;

            std::vector<VertexHandle> verts;
            verts.reserve(mesh.Valence(f));

            HalfedgeHandle h = h0;
            std::size_t safety = 0;
            const std::size_t maxIter = mesh.HalfedgesSize();
            do
            {
                const VertexHandle v = mesh.ToVertex(h);
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                {
                    verts.clear();
                    break;
                }

                verts.push_back(v);
                h = mesh.NextHalfedge(h);
                if (++safety > maxIter)
                {
                    verts.clear();
                    break;
                }
            } while (h != h0);

            if (verts.size() < 3u)
                return 0.0;

            const glm::dvec3 p0 = glm::dvec3(mesh.Position(verts.front()));
            double area = 0.0;
            for (std::size_t i = 1; i + 1 < verts.size(); ++i)
            {
                const glm::dvec3 p1 = glm::dvec3(mesh.Position(verts[i]));
                const glm::dvec3 p2 = glm::dvec3(mesh.Position(verts[i + 1]));
                area += 0.5 * glm::length(glm::cross(p1 - p0, p2 - p0));
            }

            return area;
        }
    }

    std::vector<HalfedgePatchMeta> BuildPatchMetadata(
        const Halfedge::Mesh& mesh,
        const PatchBuildParams& params)
    {
        std::vector<HalfedgePatchMeta> patches;
        patches.reserve(mesh.EdgeCount());

        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            const EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (!mesh.IsValid(e) || mesh.IsDeleted(e))
                continue;

            const HalfedgeHandle h0 = mesh.Halfedge(e, 0);
            const HalfedgeHandle h1 = mesh.Halfedge(e, 1);

            HalfedgePatchMeta meta{};
            meta.EdgeIndex = static_cast<std::uint32_t>(e.Index);
            meta.Halfedge0Index = h0.IsValid() ? static_cast<std::uint32_t>(h0.Index) : kInvalidIndex;
            meta.Halfedge1Index = h1.IsValid() ? static_cast<std::uint32_t>(h1.Index) : kInvalidIndex;
            meta.Face0Index = (h0.IsValid() && !mesh.IsBoundary(h0)) ? static_cast<std::uint32_t>(mesh.Face(h0).Index) : kInvalidIndex;
            meta.Face1Index = (h1.IsValid() && !mesh.IsBoundary(h1)) ? static_cast<std::uint32_t>(mesh.Face(h1).Index) : kInvalidIndex;
            meta.LayerIndex = static_cast<std::uint32_t>(patches.size());
            meta.Flags = 0u;

            if (!h0.IsValid() || !h1.IsValid() || mesh.IsBoundary(e))
            {
                meta.Flags |= Boundary;
            }

            const double area0 = meta.Face0Index != kInvalidIndex ? PolygonArea(mesh, mesh.Face(h0)) : 0.0;
            const double area1 = meta.Face1Index != kInvalidIndex ? PolygonArea(mesh, mesh.Face(h1)) : 0.0;
            const float target = params.TexelsPerUnit * static_cast<float>(std::sqrt(std::max(0.0, area0 + area1)));
            meta.Resolution = BucketResolution(target, params);
            if (meta.Resolution == 0u)
            {
                meta.Resolution = std::max<std::uint16_t>(params.MinResolution, 8u);
            }

            patches.push_back(meta);
        }

        return patches;
    }

    glm::vec2 TriangleToPatchUV(std::uint32_t halfedgeIndex, glm::vec2 localUV, std::uint32_t twinIndex) noexcept
    {
        if (twinIndex == kInvalidIndex)
            return localUV;

        if (halfedgeIndex > twinIndex)
            return localUV;

        return glm::vec2{1.0f - localUV.x, 1.0f - localUV.y};
    }
}

