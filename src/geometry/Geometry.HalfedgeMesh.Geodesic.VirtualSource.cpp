// Intrinsic virtual-source propagation with the published visibility heuristic.
module;
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <span>
#include <vector>
module Geometry.Geodesic;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace Geometry::Geodesic
{
    namespace
    {
        constexpr double Infinity = std::numeric_limits<double>::infinity();
        double Square(double x)
        {
            return x * x;
        }
        double SqrtSat(double x)
        {
            return std::sqrt(std::max(0.0, x));
        }
        struct TriangleState
        {
            double Center{Infinity};
            double Extra{0.0};
        };
    } // namespace

    const char* ToString(VirtualSourceStatus status) noexcept
    {
        switch (status)
        {
        case VirtualSourceStatus::Success:
            return "Success";
        case VirtualSourceStatus::EmptyMesh:
            return "Mesh has no triangles";
        case VirtualSourceStatus::UnsupportedSubmeshView:
            return "Submesh views are unsupported";
        case VirtualSourceStatus::InvalidParameters:
            return "Expansion budget must be positive";
        case VirtualSourceStatus::InvalidSources:
            return "Select valid source vertices";
        case VirtualSourceStatus::InvalidPositions:
            return "Positions must be finite float3 vertex values";
        case VirtualSourceStatus::NonTriangleFace:
            return "Geodesics require triangle faces";
        case VirtualSourceStatus::DegenerateFace:
            return "Mesh contains degenerate triangles";
        case VirtualSourceStatus::ExpansionLimit:
            return "Propagation expansion budget exhausted";
        case VirtualSourceStatus::NumericalFailure:
            return "Propagation produced nonfinite distances";
        }
        return "Unknown geodesics status";
    }

    VirtualSourceResult ComputeVirtualSourceDistance(const HalfedgeMesh::Mesh& mesh,
                                                     std::span<const std::size_t> sources,
                                                     const VirtualSourceParams& params)
    {
        return ComputeVirtualSourceDistance(mesh, mesh.Positions(), sources, params);
    }

    VirtualSourceResult ComputeVirtualSourceDistance(const HalfedgeMesh::Mesh& mesh,
                                                     std::span<const glm::vec3> positions,
                                                     std::span<const std::size_t> sources,
                                                     const VirtualSourceParams& params)
    {
        VirtualSourceResult result;
        const auto fail = [&](VirtualSourceStatus status) {
            result.Status = status;
            result.Distances.clear();
            return result;
        };
        if (mesh.IsSubmeshView())
            return fail(VirtualSourceStatus::UnsupportedSubmeshView);
        if (mesh.FaceCount() == 0)
            return result;
        if (params.MaxHalfedgeExpansions == 0)
            return fail(VirtualSourceStatus::InvalidParameters);
        if (positions.size() != mesh.VerticesSize())
            return fail(VirtualSourceStatus::InvalidPositions);
        for (auto v : mesh.LiveVertices())
        {
            const auto p = positions[v.Index];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                return fail(VirtualSourceStatus::InvalidPositions);
        }
        std::vector<bool> isSource(mesh.VerticesSize(), false);
        if (sources.empty())
            return fail(VirtualSourceStatus::InvalidSources);
        for (auto s : sources)
        {
            if (s >= mesh.VerticesSize() ||
                mesh.IsDeleted(VertexHandle{static_cast<PropertyIndex>(s)}))
                return fail(VirtualSourceStatus::InvalidSources);
            if (!isSource[s])
                ++result.SourceCount;
            isSource[s] = true;
        }
        std::vector<double> lengths(mesh.EdgesSize(), 0.0);
        for (auto e : mesh.LiveEdges())
        {
            const auto h = mesh.Halfedge(e, 0);
            const glm::dvec3 a{positions[mesh.FromVertex(h).Index]};
            const glm::dvec3 b{positions[mesh.ToVertex(h).Index]};
            lengths[e.Index] = glm::length(b - a);
            result.MeanEdgeLength += lengths[e.Index];
        }
        result.MeanEdgeLength /= static_cast<double>(mesh.EdgeCount());
        if (!(result.MeanEdgeLength > 0) || !std::isfinite(result.MeanEdgeLength))
            return fail(VirtualSourceStatus::DegenerateFace);
        for (auto& length : lengths)
            length /= result.MeanEdgeLength;
        const auto edgeLength = [&](HalfedgeHandle h) { return lengths[mesh.Edge(h).Index]; };
        std::vector<TriangleState> faces(mesh.FacesSize());
        std::vector<double> distancesSquared(mesh.HalfedgesSize(), -1.0);
        std::vector<bool> behind(mesh.HalfedgesSize(), false);
        std::vector<HalfedgeHandle> from, to;
        for (auto f : mesh.LiveFaces())
        {
            const auto h0 = mesh.Halfedge(f);
            const auto h1 = mesh.NextHalfedge(h0);
            const auto h2 = mesh.NextHalfedge(h1);
            if (mesh.NextHalfedge(h2) != h0)
                return fail(VirtualSourceStatus::NonTriangleFace);
            const std::array hs{h0, h1, h2};
            std::array<glm::dvec3, 3> ps;
            for (std::size_t i = 0; i < 3; ++i)
                ps[i] = glm::dvec3{positions[mesh.ToVertex(hs[i]).Index]} / result.MeanEdgeLength;
            const auto ab = ps[1] - ps[0], ac = ps[2] - ps[0];
            const double maxLength = std::max({edgeLength(h0), edgeLength(h1), edgeLength(h2)});
            if (glm::length(glm::cross(ab, ac)) <= 1e-14 * maxLength * maxLength)
                return fail(VirtualSourceStatus::DegenerateFace);
            const std::array<double, 3> squaredEdges{Square(edgeLength(h0)), Square(edgeLength(h1)),
                                                     Square(edgeLength(h2))};
            // One virtual source per face; deterministic source ties use the
            // smallest vertex slot, independent of the caller's source order.
            std::size_t winner = mesh.VerticesSize();
            for (std::size_t i = 0; i < 3; ++i)
            {
                const auto v = mesh.ToVertex(hs[i]).Index;
                if (!isSource[v])
                    continue;
                std::array<double, 3> seedDistances{};
                seedDistances[(i + 1) % 3] = squaredEdges[(i + 1) % 3];
                seedDistances[(i + 2) % 3] = squaredEdges[i];
                // Use the same intrinsic metric for seed and propagated states;
                // world-space centroid arithmetic can change near-equal updates.
                const double distance =
                    SqrtSat((2 * (seedDistances[0] + seedDistances[1] + seedDistances[2]) -
                             squaredEdges[0] - squaredEdges[1] - squaredEdges[2]) /
                            9);
                if (distance < faces[f.Index].Center ||
                    (distance == faces[f.Index].Center && v < winner))
                {
                    winner = v;
                    faces[f.Index].Center = distance;
                    for (std::size_t j = 0; j < 3; ++j)
                        distancesSquared[hs[j].Index] = seedDistances[j];
                }
            }
            if (winner != mesh.VerticesSize())
                for (auto h : hs)
                    if (mesh.ToVertex(h).Index == winner)
                        from.push_back(mesh.PrevHalfedge(h));
        }
        while (!from.empty())
        {
            ++result.Iterations;
            to.clear();
            for (std::size_t q = 0; q < from.size(); ++q)
            {
                if (result.HalfedgeExpansions == params.MaxHalfedgeExpansions)
                    return fail(VirtualSourceStatus::ExpansionLimit);
                ++result.HalfedgeExpansions;
                const auto source = from[q];
                const auto h1 = mesh.OppositeHalfedge(source);
                const auto target = mesh.Face(h1);
                if (!mesh.IsValid(target) || mesh.IsDeleted(target))
                    continue;
                const auto h2 = mesh.PrevHalfedge(h1), h3 = mesh.PrevHalfedge(h2);
                const double e1 = edgeLength(h1), e2 = edgeLength(h2), e3 = edgeLength(h3);
                const double d1 = distancesSquared[source.Index];
                const double d2 = distancesSquared[mesh.PrevHalfedge(source).Index];
                const double px = (e1 * e1 + e2 * e2 - e3 * e3) / (2 * e1);
                const double py = SqrtSat(e2 * e2 - px * px);
                const double sx = (e1 * e1 + d1 - d2) / (2 * e1);
                const double sourceHeightSquared = d1 - sx * sx;
                const double cancellationFloor =
                    64 * std::numeric_limits<double>::epsilon() * std::max(d1, sx * sx);
                const double sy =
                    sourceHeightSquared <= cancellationFloor ? 0.0 : std::sqrt(sourceHeightSquared);
                const double cx = (px + e1) / 3, cy = py / 3;
                double sigma = faces[mesh.Face(source).Index].Extra;
                const double ds1 = std::hypot(sx, sy), ds2 = std::hypot(sx - e1, sy);
                const double dc1 = std::hypot(cx, cy), dc2 = std::hypot(cx - e1, cy);
                bool left = false, right = false, behind2 = false, behind3 = false;
                if (behind[source.Index])
                {
                    left = dc1 + ds1 < dc2 + ds2;
                    right = !left;
                }
                else
                {
                    // Trettner et al. supplemental visibility decision tree.
                    constexpr std::array<double, 16> lambda{
                        .320991, .446887, .595879,  .270094,  .236679, .159685,  .0872932, .434132,
                        1.,      .726262, .0635997, .0515979, .56903,  .0447586, .0612103, .718198};
                    const double maxE = std::max({e1, e2, e3}), minE = std::min({e1, e2, e3});
                    const unsigned index =
                        unsigned(maxE > 5.1424 * e1) + 2 * unsigned(maxE > 4.20638 * minE) +
                        4 * unsigned(py < .504201 * e1) + 8 * unsigned(py < 2.84918 * maxE);
                    const double l = lambda[index], qx = px * (1 - l) + cx * l,
                                 qy = py * (1 - l) + cy * l;
                    const double intersection = qx * sy + sx * qy;
                    left = intersection < 0;
                    right = intersection > e1 * (qy + sy);
                }
                double da, db, dc, center;
                if (left)
                {
                    da = 0;
                    db = e1 * e1;
                    dc = e2 * e2;
                    sigma += ds1;
                    center = sigma + dc1;
                }
                else if (right)
                {
                    da = e1 * e1;
                    db = 0;
                    dc = e3 * e3;
                    sigma += ds2;
                    center = sigma + dc2;
                }
                else
                {
                    da = d1;
                    db = d2;
                    dc = Square(px - sx) + Square(py + sy);
                    behind2 = py * sx + px * sy < 0;
                    behind3 = py * (e1 - sx) - (px - e1) * sy < 0;
                    center = sigma + std::hypot(cx - sx, cy + sy);
                }
                if (!std::isfinite(center) || !std::isfinite(dc))
                    return fail(VirtualSourceStatus::NumericalFailure);
                auto& face = faces[target.Index];
                // Ignore arithmetic noise in equal-distance arrivals: replacing
                // a source by a numerically shorter bent source can alter the
                // field far more than the tiny centroid improvement.
                if (center < face.Center &&
                    (!std::isfinite(face.Center) ||
                     face.Center - center >
                         64 * std::numeric_limits<double>::epsilon() * std::max(1.0, face.Center)))
                {
                    ++result.TriangleUpdates;
                    face = {center, sigma};
                    distancesSquared[h1.Index] = db;
                    distancesSquared[h2.Index] = da;
                    distancesSquared[h3.Index] = dc;
                    behind[h1.Index] = true;
                    behind[h2.Index] = behind2;
                    behind[h3.Index] = behind3;
                    auto& queue = center < static_cast<double>(result.Iterations) ? from : to;
                    queue.push_back(h2);
                    queue.push_back(h3);
                }
            }
            from.swap(to);
        }
        result.Distances.assign(mesh.VerticesSize(), Infinity);
        for (auto h : mesh.LiveHalfedges())
        {
            const auto f = mesh.Face(h);
            if (!mesh.IsValid(f) || mesh.IsDeleted(f) || distancesSquared[h.Index] < 0)
                continue;
            const double distance = (faces[f.Index].Extra + std::sqrt(distancesSquared[h.Index])) *
                                    result.MeanEdgeLength;
            auto& value = result.Distances[mesh.ToVertex(h).Index];
            value = std::min(value, distance);
        }
        for (auto v : mesh.LiveVertices())
        {
            if (isSource[v.Index])
                result.Distances[v.Index] = 0;
            if (!std::isfinite(result.Distances[v.Index]))
                ++result.UnreachableVertexCount;
        }
        result.Status = VirtualSourceStatus::Success;
        return result;
    }
} // namespace Geometry::Geodesic
