module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string_view>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.PointCloud.SurfaceSampling;

import Geometry.HalfedgeMesh.Utils;
import Geometry.Properties;

namespace Geometry::PointCloud::SurfaceSampling
{
    namespace
    {
        struct TriangleRecord
        {
            FaceHandle Face{};
            VertexHandle V0{};
            VertexHandle V1{};
            VertexHandle V2{};
            glm::vec3 P0{};
            glm::vec3 P1{};
            glm::vec3 P2{};
            glm::vec3 FaceNormal{0.0f, 1.0f, 0.0f};
            double Area{0.0};
            double CumulativeArea{0.0};
        };

        [[nodiscard]] bool IsFinite(glm::vec3 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] glm::vec3 SafeUnitNormal(glm::vec3 n) noexcept
        {
            if (!IsFinite(n))
            {
                return {0.0f, 1.0f, 0.0f};
            }
            const float len = glm::length(n);
            if (!std::isfinite(len) || len <= 1.0e-12f)
            {
                return {0.0f, 1.0f, 0.0f};
            }
            return n / len;
        }

        [[nodiscard]] glm::vec3 TriangleFaceNormal(const glm::vec3 p0,
                                                   const glm::vec3 p1,
                                                   const glm::vec3 p2) noexcept
        {
            return SafeUnitNormal(glm::cross(p1 - p0, p2 - p0));
        }

        [[nodiscard]] bool HasFiniteNormal(const ConstProperty<glm::vec3>& normals,
                                           const VertexHandle v) noexcept
        {
            if (!normals.IsValid() || v.Index >= normals.Vector().size())
            {
                return false;
            }
            return IsFinite(normals[v.Index]);
        }

        [[nodiscard]] bool HasTriangleNormals(const ConstProperty<glm::vec3>& normals,
                                              const TriangleRecord& triangle) noexcept
        {
            return HasFiniteNormal(normals, triangle.V0) &&
                   HasFiniteNormal(normals, triangle.V1) &&
                   HasFiniteNormal(normals, triangle.V2);
        }

        [[nodiscard]] std::vector<TriangleRecord> BuildTriangleTable(const HalfedgeMesh::Mesh& mesh,
                                                                     const Params& params,
                                                                     Diagnostics& diagnostics)
        {
            std::vector<TriangleRecord> triangles;
            triangles.reserve(mesh.FaceCount());

            double totalArea = 0.0;
            for (const FaceHandle face : mesh.LiveFaces())
            {
                ++diagnostics.TotalFaceCount;

                const std::size_t valence = mesh.Valence(face);
                if (valence != 3u)
                {
                    ++diagnostics.RejectedNonTriangleFaceCount;
                    continue;
                }

                MeshUtils::TriangleFaceView view;
                if (!MeshUtils::TryGetTriangleFaceView(mesh, face, view))
                {
                    ++diagnostics.RejectedNonTriangleFaceCount;
                    continue;
                }

                if (!IsFinite(view.P0) || !IsFinite(view.P1) || !IsFinite(view.P2))
                {
                    ++diagnostics.RejectedNonFiniteTriangleCount;
                    continue;
                }

                const double area = MeshUtils::TriangleArea(view.P0, view.P1, view.P2);
                if (!std::isfinite(area) || area <= params.MinTriangleArea)
                {
                    ++diagnostics.RejectedDegenerateTriangleCount;
                    continue;
                }

                totalArea += area;
                triangles.push_back(TriangleRecord{
                    .Face = view.Face,
                    .V0 = view.V0,
                    .V1 = view.V1,
                    .V2 = view.V2,
                    .P0 = view.P0,
                    .P1 = view.P1,
                    .P2 = view.P2,
                    .FaceNormal = TriangleFaceNormal(view.P0, view.P1, view.P2),
                    .Area = area,
                    .CumulativeArea = totalArea,
                });
            }

            diagnostics.AcceptedTriangleCount = triangles.size();
            diagnostics.TotalSurfaceArea = totalArea;
            return triangles;
        }
    }

    std::string_view ToString(SurfaceSamplingStatus status) noexcept
    {
        switch (status)
        {
        case SurfaceSamplingStatus::Success:
            return "Success";
        case SurfaceSamplingStatus::InvalidSampleCount:
            return "InvalidSampleCount";
        case SurfaceSamplingStatus::EmptyMesh:
            return "EmptyMesh";
        case SurfaceSamplingStatus::NoValidTriangles:
            return "NoValidTriangles";
        }
        return "Unknown";
    }

    Result SampleTriangleMeshSurface(const HalfedgeMesh::Mesh& mesh,
                                     const Params& params)
    {
        Result result;
        result.Info.RequestedSampleCount = params.SampleCount;
        result.Info.Seed = params.Seed;

        if (params.SampleCount <= 0)
        {
            result.Status = SurfaceSamplingStatus::InvalidSampleCount;
            return result;
        }

        if (mesh.FaceCount() == 0u || mesh.VertexCount() == 0u)
        {
            result.Status = SurfaceSamplingStatus::EmptyMesh;
            return result;
        }

        const std::vector<TriangleRecord> triangles = BuildTriangleTable(mesh, params, result.Info);
        if (triangles.empty() || result.Info.TotalSurfaceArea <= 0.0)
        {
            result.Status = SurfaceSamplingStatus::NoValidTriangles;
            return result;
        }

        const ConstProperty<glm::vec3> sourceNormals =
            mesh.VertexProperties().Get<glm::vec3>(params.SourceNormalProperty);
        if (sourceNormals.IsValid())
        {
            result.Info.SourceVertexNormalCount = sourceNormals.Vector().size();
        }

        const std::size_t sampleCount = static_cast<std::size_t>(params.SampleCount);
        result.Cloud.Reserve(sampleCount);
        result.Cloud.EnableNormals();

        std::mt19937_64 rng{params.Seed};
        std::uniform_real_distribution<double> areaDistribution{
            0.0,
            std::nextafter(result.Info.TotalSurfaceArea, std::numeric_limits<double>::max())};
        std::uniform_real_distribution<double> unitDistribution{0.0, 1.0};

        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            const double areaTarget = areaDistribution(rng);
            const auto triangleIt = std::lower_bound(
                triangles.begin(),
                triangles.end(),
                areaTarget,
                [](const TriangleRecord& triangle, const double target) {
                    return triangle.CumulativeArea < target;
                });
            const TriangleRecord& triangle = triangleIt != triangles.end() ? *triangleIt : triangles.back();

            const double r0 = unitDistribution(rng);
            const double r1 = unitDistribution(rng);
            const float sqrtR0 = static_cast<float>(std::sqrt(r0));
            const float b0 = 1.0f - sqrtR0;
            const float b1 = sqrtR0 * (1.0f - static_cast<float>(r1));
            const float b2 = sqrtR0 * static_cast<float>(r1);

            const glm::vec3 position = (b0 * triangle.P0) + (b1 * triangle.P1) + (b2 * triangle.P2);
            const VertexHandle point = result.Cloud.AddPoint(position);

            if (params.InterpolateVertexNormals && HasTriangleNormals(sourceNormals, triangle))
            {
                const glm::vec3 interpolated = SafeUnitNormal(
                    (b0 * sourceNormals[triangle.V0.Index]) +
                    (b1 * sourceNormals[triangle.V1.Index]) +
                    (b2 * sourceNormals[triangle.V2.Index]));
                result.Cloud.Normal(point) = interpolated;
                ++result.Info.InterpolatedNormalCount;
            }
            else
            {
                result.Cloud.Normal(point) = triangle.FaceNormal;
                ++result.Info.GeometricNormalCount;
            }
        }

        result.Info.WrittenSampleCount = result.Cloud.VertexCount();
        result.Status = SurfaceSamplingStatus::Success;
        return result;
    }
}
