module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module Geometry.PointCloud.SurfaceSampling;

export import Geometry.PointCloud;

import Geometry.HalfedgeMesh;

export namespace Geometry::PointCloud::SurfaceSampling
{
    inline constexpr std::string_view kDefaultSourceNormalProperty = "v:normal";

    enum class SurfaceSamplingStatus : std::uint8_t
    {
        Success,
        InvalidSampleCount,
        EmptyMesh,
        NoValidTriangles,
    };

    struct Params
    {
        std::int64_t SampleCount{0};
        std::uint64_t Seed{0};
        double MinTriangleArea{1.0e-14};
        std::string_view SourceNormalProperty{kDefaultSourceNormalProperty};
        bool InterpolateVertexNormals{true};
    };

    struct Diagnostics
    {
        std::int64_t RequestedSampleCount{0};
        std::uint64_t Seed{0};
        std::size_t WrittenSampleCount{0};
        std::size_t TotalFaceCount{0};
        std::size_t AcceptedTriangleCount{0};
        std::size_t RejectedNonTriangleFaceCount{0};
        std::size_t RejectedDegenerateTriangleCount{0};
        std::size_t RejectedNonFiniteTriangleCount{0};
        std::size_t SourceVertexNormalCount{0};
        std::size_t InterpolatedNormalCount{0};
        std::size_t GeometricNormalCount{0};
        double TotalSurfaceArea{0.0};
    };

    struct Result
    {
        SurfaceSamplingStatus Status{SurfaceSamplingStatus::InvalidSampleCount};
        PointCloud::Cloud Cloud{};
        Diagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SurfaceSamplingStatus::Success;
        }
    };

    [[nodiscard]] std::string_view ToString(SurfaceSamplingStatus status) noexcept;

    [[nodiscard]] Result SampleTriangleMeshSurface(const HalfedgeMesh::Mesh& mesh,
                                                   const Params& params);
}
