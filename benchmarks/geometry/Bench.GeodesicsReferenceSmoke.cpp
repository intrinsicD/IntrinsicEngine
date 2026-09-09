#include "Bench.GeodesicsReferenceSmoke.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
import Geometry.Geodesic;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
namespace Intrinsic::Bench::Geometry
{
    GeodesicsReferenceSmokeMetrics RunGeodesicsReferenceSmoke()
    {
        ::Geometry::HalfedgeMesh::Mesh mesh;
        constexpr unsigned n = 8;
        for (unsigned y = 0; y <= n; ++y)
            for (unsigned x = 0; x <= n; ++x)
                (void)mesh.AddVertex({float(x), float(y), 0});
        for (unsigned y = 0; y < n; ++y)
            for (unsigned x = 0; x < n; ++x)
            {
                auto a = ::Geometry::VertexHandle{y * (n + 1) + x},
                     b = ::Geometry::VertexHandle{a.Index + 1};
                auto c = ::Geometry::VertexHandle{a.Index + n + 1},
                     d = ::Geometry::VertexHandle{c.Index + 1};
                if (!mesh.AddTriangle(a, b, d) || !mesh.AddTriangle(a, d, c))
                    return {};
            }
        GeodesicsReferenceSmokeMetrics metrics;
        metrics.Succeeded = true;
        for (unsigned run = 0; run < 9; ++run)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto result = ::Geometry::Geodesic::ComputeVirtualSourceDistance(
                mesh, std::array<std::size_t, 1>{0});
            const double milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            if (run > 0)
                metrics.RuntimeMilliseconds += milliseconds / 8;
            if (!result.Succeeded() || result.UnreachableVertexCount != 0)
            {
                metrics.Succeeded = false;
                continue;
            }
            double sumSquared = 0, maxAbsolute = 0;
            for (unsigned y = 0; y <= n; ++y)
                for (unsigned x = 0; x <= n; ++x)
                {
                    const double exact = std::hypot(x, y);
                    const double error = std::abs(result.Distances[y * (n + 1) + x] - exact);
                    sumSquared += error * error;
                    maxAbsolute = std::max(maxAbsolute, error);
                }
            metrics.QualityErrorL2 = std::sqrt(sumSquared / 81);
            metrics.MaxAbsoluteError = maxAbsolute;
            metrics.HalfedgeExpansions = result.HalfedgeExpansions;
            metrics.Succeeded =
                metrics.Succeeded && maxAbsolute <= .075 && metrics.QualityErrorL2 <= .05;
        }
        return metrics;
    }
}
