module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Geometry.HtexPatch;

import Geometry.KMeans;

namespace Geometry::HtexPatch
{
    namespace
    {
        struct HalfedgeTriangle
        {
            std::array<VertexHandle, 3> Vertices{};
            std::array<glm::vec3, 3> Positions{};
            bool Valid = false;
        };

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

            HalfedgeHandle h = h0;
            std::size_t safety = 0;
            const std::size_t maxIter = mesh.HalfedgesSize();
            std::size_t vertexCount = 0;
            glm::dvec3 p0{0.0};
            glm::dvec3 prev{0.0};
            double area = 0.0;
            do
            {
                const VertexHandle v = mesh.ToVertex(h);
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                {
                    return 0.0;
                }

                const glm::dvec3 position = glm::dvec3(mesh.Position(v));
                ++vertexCount;
                if (vertexCount == 1u)
                {
                    p0 = position;
                }
                else if (vertexCount == 2u)
                {
                    prev = position;
                }
                else
                {
                    area += 0.5 * glm::length(glm::cross(prev - p0, position - p0));
                    prev = position;
                }

                h = mesh.NextHalfedge(h);
                if (++safety > maxIter)
                {
                    return 0.0;
                }
            } while (h != h0);

            if (vertexCount < 3u)
                return 0.0;

            return area;
        }

        [[nodiscard]] float TriangleAreaSquared(const HalfedgeTriangle& tri) noexcept
        {
            const glm::vec3 e0 = tri.Positions[1] - tri.Positions[0];
            const glm::vec3 e1 = tri.Positions[2] - tri.Positions[0];
            const glm::vec3 n = glm::cross(e0, e1);
            return glm::dot(n, n);
        }

        [[nodiscard]] HalfedgeTriangle BuildHalfedgeTriangle(const Halfedge::Mesh& mesh, HalfedgeHandle h) noexcept
        {
            HalfedgeTriangle tri{};
            if (!h.IsValid() || !mesh.IsValid(h) || mesh.IsDeleted(h) || mesh.IsBoundary(h))
                return tri;

            const HalfedgeHandle next = mesh.NextHalfedge(h);
            if (!next.IsValid() || !mesh.IsValid(next) || mesh.IsDeleted(next))
                return tri;

            tri.Vertices[0] = mesh.FromVertex(h);
            tri.Vertices[1] = mesh.ToVertex(h);
            tri.Vertices[2] = mesh.ToVertex(next);

            for (std::size_t i = 0; i < tri.Vertices.size(); ++i)
            {
                if (!tri.Vertices[i].IsValid() || !mesh.IsValid(tri.Vertices[i]) || mesh.IsDeleted(tri.Vertices[i]))
                    return {};

                tri.Positions[i] = mesh.Position(tri.Vertices[i]);
                if (!std::isfinite(tri.Positions[i].x) || !std::isfinite(tri.Positions[i].y) || !std::isfinite(tri.Positions[i].z))
                    return {};
            }

            if (tri.Vertices[0] == tri.Vertices[1] || tri.Vertices[1] == tri.Vertices[2] || tri.Vertices[2] == tri.Vertices[0])
                return {};

            if (TriangleAreaSquared(tri) <= 1.0e-20f)
                return {};

            tri.Valid = true;
            return tri;
        }

        [[nodiscard]] glm::vec3 EvaluateTrianglePoint(const HalfedgeTriangle& tri, glm::vec2 localUV) noexcept
        {
            const float w0 = 1.0f - localUV.x - localUV.y;
            return w0 * tri.Positions[0] + localUV.x * tri.Positions[1] + localUV.y * tri.Positions[2];
        }

        [[nodiscard]] std::uint32_t ClassifyPointToCluster(const HalfedgeTriangle& tri,
                                                           glm::vec2 localUV,
                                                           std::span<const glm::vec3> centroids,
                                                           std::uint32_t invalidValue) noexcept
        {
            if (!tri.Valid || centroids.empty())
                return invalidValue;

            const glm::vec3 p = EvaluateTrianglePoint(tri, localUV);
            if (const auto winner = Geometry::KMeans::ClassifyPointToCentroid(p, centroids))
                return *winner;

            return invalidValue;
        }

        [[nodiscard]] std::uint32_t ClassifyPatchTexel(const HalfedgeTriangle& tri,
                                                       glm::vec2 patchUV,
                                                       std::uint32_t halfedgeIndex,
                                                       std::uint32_t twinIndex,
                                                       std::span<const glm::vec3> centroids,
                                                       std::uint32_t invalidValue) noexcept
        {
            if (!tri.Valid)
                return invalidValue;

            const glm::vec2 localUV = PatchToTriangleUV(halfedgeIndex, patchUV, twinIndex);
            if (!IsTriangleLocalUV(localUV))
                return invalidValue;

            return ClassifyPointToCluster(tri, localUV, centroids, invalidValue);
        }
    }

    std::optional<PatchBuildResult> BuildPatchMetadata(
        const Halfedge::Mesh& mesh,
        const PatchBuildParams& params)
    {
        if (mesh.EdgeCount() == 0u)
            return std::nullopt;

        PatchBuildResult result{};
        result.Patches.reserve(mesh.EdgeCount());

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
            meta.LayerIndex = static_cast<std::uint32_t>(result.Patches.size());
            meta.Flags = 0u;

            if (!h0.IsValid() || !h1.IsValid() || mesh.IsBoundary(e))
            {
                meta.Flags |= Boundary;
                ++result.BoundaryPatchCount;
            }
            else
            {
                ++result.InteriorPatchCount;
            }

            const double area0 = meta.Face0Index != kInvalidIndex ? PolygonArea(mesh, mesh.Face(h0)) : 0.0;
            const double area1 = meta.Face1Index != kInvalidIndex ? PolygonArea(mesh, mesh.Face(h1)) : 0.0;
            const float target = params.TexelsPerUnit * static_cast<float>(std::sqrt(std::max(0.0, area0 + area1)));
            meta.Resolution = BucketResolution(target, params);
            if (meta.Resolution == 0u)
            {
                meta.Resolution = std::max<std::uint16_t>(params.MinResolution, 8u);
            }
            result.MaxAssignedResolution = std::max(result.MaxAssignedResolution, meta.Resolution);

            result.Patches.push_back(meta);
        }

        if (result.Patches.empty())
            return std::nullopt;

        return result;
    }

    PatchAtlasLayout ComputeAtlasLayout(std::size_t patchCount, std::uint32_t tileSize, std::uint32_t maxColumns) noexcept
    {
        PatchAtlasLayout layout{};
        layout.TileSize = std::max(1u, tileSize);

        if (patchCount == 0u)
            return layout;

        const std::uint32_t columnsLimit = std::max(1u, maxColumns);
        layout.Columns = std::max(1u,
                                  std::min(columnsLimit,
                                           static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(patchCount))))));
        layout.Rows = static_cast<std::uint32_t>((patchCount + layout.Columns - 1u) / layout.Columns);
        layout.Width = layout.Columns * layout.TileSize;
        layout.Height = layout.Rows * layout.TileSize;
        return layout;
    }

    bool BuildCategoricalPatchAtlas(const Halfedge::Mesh& mesh,
                                    std::span<const HalfedgePatchMeta> patches,
                                    std::span<const glm::vec3> centroids,
                                    std::vector<std::uint32_t>& outTexels,
                                    PatchAtlasLayout& outLayout,
                                    std::uint32_t invalidValue)
    {
        outLayout = ComputeAtlasLayout(patches.size());
        outTexels.assign(static_cast<std::size_t>(outLayout.Width) * static_cast<std::size_t>(outLayout.Height), invalidValue);

        if (patches.empty() || centroids.empty())
            return false;

        bool wroteAny = false;
        const std::uint32_t tileSize = outLayout.TileSize;

        for (std::size_t i = 0; i < patches.size(); ++i)
        {
            const auto& patch = patches[i];
            const std::uint32_t px = static_cast<std::uint32_t>(i % outLayout.Columns) * tileSize;
            const std::uint32_t py = static_cast<std::uint32_t>(i / outLayout.Columns) * tileSize;

            const HalfedgeHandle h0{static_cast<PropertyIndex>(patch.Halfedge0Index)};
            const HalfedgeHandle h1{static_cast<PropertyIndex>(patch.Halfedge1Index)};
            const HalfedgeTriangle tri0 = BuildHalfedgeTriangle(mesh, h0);
            const HalfedgeTriangle tri1 = BuildHalfedgeTriangle(mesh, h1);

            for (std::uint32_t y = 0; y < tileSize; ++y)
            {
                for (std::uint32_t x = 0; x < tileSize; ++x)
                {
                    const glm::vec2 patchUV{(static_cast<float>(x) + 0.5f) / static_cast<float>(tileSize),
                                            (static_cast<float>(y) + 0.5f) / static_cast<float>(tileSize)};

                    std::uint32_t texel = ClassifyPatchTexel(
                        tri0,
                        patchUV,
                        patch.Halfedge0Index,
                        patch.Halfedge1Index,
                        centroids,
                        invalidValue);
                    if (texel == invalidValue)
                    {
                        texel = ClassifyPatchTexel(
                            tri1,
                            patchUV,
                            patch.Halfedge1Index,
                            patch.Halfedge0Index,
                            centroids,
                            invalidValue);
                    }

                    const std::size_t dst = static_cast<std::size_t>(py + y) * static_cast<std::size_t>(outLayout.Width) +
                                            static_cast<std::size_t>(px + x);
                    outTexels[dst] = texel;
                    wroteAny |= (texel != invalidValue);
                }
            }
        }

        return wroteAny;
    }

    glm::vec2 TriangleToPatchUV(std::uint32_t halfedgeIndex, glm::vec2 localUV, std::uint32_t twinIndex) noexcept
    {
        if (twinIndex == kInvalidIndex)
            return localUV;

        if (halfedgeIndex > twinIndex)
            return localUV;

        return glm::vec2{1.0f - localUV.x, 1.0f - localUV.y};
    }

    glm::vec2 PatchToTriangleUV(std::uint32_t halfedgeIndex, glm::vec2 patchUV, std::uint32_t twinIndex) noexcept
    {
        return TriangleToPatchUV(halfedgeIndex, patchUV, twinIndex);
    }

    bool IsTriangleLocalUV(glm::vec2 localUV, float epsilon) noexcept
    {
        return localUV.x >= -epsilon && localUV.y >= -epsilon && (localUV.x + localUV.y) <= (1.0f + epsilon);
    }
}
