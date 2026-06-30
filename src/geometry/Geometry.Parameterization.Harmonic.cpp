module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.Parameterization.Harmonic;

import Geometry.HalfedgeMesh;
import Geometry.Sparse;
import Geometry.Parameterization.Diagnostics;

namespace Geometry::Parameterization
{
    namespace
    {
        using Geometry::HalfedgeMesh::Mesh;

        [[nodiscard]] bool IsFinite2(glm::vec2 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }

        // Cotangent of the angle at apex X of triangle (X, Y, Z).
        [[nodiscard]] double CotAtApex(glm::dvec3 x, glm::dvec3 y, glm::dvec3 z) noexcept
        {
            const glm::dvec3 a = y - x;
            const glm::dvec3 b = z - x;
            const double crossLen = glm::length(glm::cross(a, b));
            if (crossLen < 1e-18)
            {
                return 0.0;
            }
            return glm::dot(a, b) / crossLen;
        }

        // Ordered boundary loop starting at a boundary halfedge. Returns the loop
        // vertices in traversal order, or empty on failure.
        [[nodiscard]] std::vector<VertexHandle> ExtractBoundaryLoop(const Mesh& mesh)
        {
            std::vector<VertexHandle> loop;

            // Find a starting boundary halfedge by direct scan (robust to the
            // vertex circulator's behavior at the boundary).
            HalfedgeHandle start{};
            for (std::size_t hi = 0; hi < mesh.HalfedgesSize(); ++hi)
            {
                const HalfedgeHandle h{static_cast<PropertyIndex>(hi)};
                if (mesh.IsDeleted(mesh.Edge(h)))
                {
                    continue;
                }
                if (mesh.IsBoundary(h))
                {
                    start = h;
                    break;
                }
            }
            if (!start.IsValid())
            {
                return loop;
            }

            HalfedgeHandle cur = start;
            const std::size_t guard = mesh.HalfedgesSize() + 1;
            std::size_t steps = 0;
            do
            {
                loop.push_back(mesh.FromVertex(cur));
                cur = mesh.NextHalfedge(cur);
                if (++steps > guard)
                {
                    return {}; // malformed boundary
                }
            } while (cur != start);

            return loop;
        }

        [[nodiscard]] std::size_t CountBoundaryVertices(const Mesh& mesh)
        {
            std::size_t count = 0;
            for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
            {
                const VertexHandle v{static_cast<PropertyIndex>(vi)};
                if (!mesh.IsDeleted(v) && !mesh.IsIsolated(v) && mesh.IsBoundary(v))
                {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] double SignedAreaUv(glm::vec2 a, glm::vec2 b, glm::vec2 c) noexcept
        {
            return 0.5 * (static_cast<double>(b.x - a.x) * static_cast<double>(c.y - a.y)
                        - static_cast<double>(c.x - a.x) * static_cast<double>(b.y - a.y));
        }

        // Auto-placed boundaries have no caller-chosen orientation, so the loop
        // walk direction may invert the UV winding relative to the face order
        // (which the flip diagnostic measures). If the net signed UV area is
        // negative, mirror about the boundary centroid to restore orientation.
        void FixOrientation(const Mesh& mesh,
                            std::vector<glm::vec2>& uv,
                            const std::vector<VertexHandle>& boundaryLoop)
        {
            double net = 0.0;
            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                const FaceHandle f{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(f))
                {
                    continue;
                }
                const HalfedgeHandle h0 = mesh.Halfedge(f);
                const HalfedgeHandle h1 = mesh.NextHalfedge(h0);
                const HalfedgeHandle h2 = mesh.NextHalfedge(h1);
                net += SignedAreaUv(uv[mesh.ToVertex(h0).Index],
                                    uv[mesh.ToVertex(h1).Index],
                                    uv[mesh.ToVertex(h2).Index]);
            }
            if (net >= 0.0)
            {
                return;
            }
            double cx = 0.0;
            for (const VertexHandle v : boundaryLoop)
            {
                cx += static_cast<double>(uv[v.Index].x);
            }
            cx = boundaryLoop.empty() ? 0.0 : cx / static_cast<double>(boundaryLoop.size());
            for (glm::vec2& p : uv)
            {
                p.x = static_cast<float>(2.0 * cx - static_cast<double>(p.x));
            }
        }

        // Position on the unit-square perimeter for t in [0,1).
        [[nodiscard]] glm::vec2 SquarePerimeter(double t) noexcept
        {
            t -= std::floor(t);
            const double s = t * 4.0;
            const int seg = std::min(3, static_cast<int>(std::floor(s)));
            const double f = s - seg;
            switch (seg)
            {
            case 0: return {static_cast<float>(f), 0.0f};
            case 1: return {1.0f, static_cast<float>(f)};
            case 2: return {static_cast<float>(1.0 - f), 1.0f};
            default: return {0.0f, static_cast<float>(1.0 - f)};
            }
        }
    } // namespace

    const char* ToString(HarmonicStatus status) noexcept
    {
        switch (status)
        {
        case HarmonicStatus::Success: return "Success";
        case HarmonicStatus::EmptyMesh: return "EmptyMesh";
        case HarmonicStatus::NotTriangleMesh: return "NotTriangleMesh";
        case HarmonicStatus::NotDiskTopology: return "NotDiskTopology";
        case HarmonicStatus::DegenerateBoundary: return "DegenerateBoundary";
        case HarmonicStatus::InsufficientVertices: return "InsufficientVertices";
        case HarmonicStatus::MismatchedBoundaryArray: return "MismatchedBoundaryArray";
        case HarmonicStatus::InvalidPins: return "InvalidPins";
        case HarmonicStatus::NonFiniteBoundaryUV: return "NonFiniteBoundaryUV";
        case HarmonicStatus::SingularSystem: return "SingularSystem";
        case HarmonicStatus::SolverFailed: return "SolverFailed";
        }
        return "Unknown";
    }

    std::optional<HarmonicResult> ComputeHarmonic(const Mesh& mesh, const HarmonicParams& params)
    {
        HarmonicResult result;
        const std::size_t nV = mesh.VerticesSize();
        result.UVs.assign(nV, glm::vec2(0.0f));

        const auto fail = [&](HarmonicStatus s) -> std::optional<HarmonicResult> {
            result.Status = s;
            return result;
        };

        if (mesh.FacesSize() == 0 || mesh.VertexCount() == 0)
        {
            return fail(HarmonicStatus::EmptyMesh);
        }
        if (mesh.VertexCount() < 3)
        {
            return fail(HarmonicStatus::InsufficientVertices);
        }

        // Triangle-mesh precondition.
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            const FaceHandle f{static_cast<PropertyIndex>(fi)};
            if (!mesh.IsDeleted(f) && mesh.Valence(f) != 3)
            {
                return fail(HarmonicStatus::NotTriangleMesh);
            }
        }

        if (params.PinnedVertices.size() != params.PinnedUVs.size())
        {
            return fail(HarmonicStatus::MismatchedBoundaryArray);
        }

        // Validate pins: in range, live, finite, unique.
        std::vector<glm::vec2> uv(nV, glm::vec2(0.0f));
        std::vector<std::uint8_t> fixed(nV, 0u);
        for (std::size_t i = 0; i < params.PinnedVertices.size(); ++i)
        {
            const std::uint32_t idx = params.PinnedVertices[i];
            if (idx >= nV)
            {
                return fail(HarmonicStatus::InvalidPins);
            }
            const VertexHandle v{static_cast<PropertyIndex>(idx)};
            if (mesh.IsDeleted(v) || fixed[idx] != 0u)
            {
                return fail(HarmonicStatus::InvalidPins);
            }
            if (!IsFinite2(params.PinnedUVs[i]))
            {
                return fail(HarmonicStatus::NonFiniteBoundaryUV);
            }
            fixed[idx] = 1u;
            uv[idx] = params.PinnedUVs[i];
        }

        // Disk topology: exactly one boundary loop covering all boundary verts.
        const std::vector<VertexHandle> loop = ExtractBoundaryLoop(mesh);
        if (loop.empty() || loop.size() != CountBoundaryVertices(mesh))
        {
            return fail(HarmonicStatus::NotDiskTopology);
        }
        if (loop.size() < 3)
        {
            return fail(HarmonicStatus::DegenerateBoundary);
        }
        result.BoundaryVertexCount = loop.size();

        // Place / validate the boundary.
        if (params.Boundary == HarmonicBoundaryPolicy::Custom)
        {
            for (const VertexHandle v : loop)
            {
                if (fixed[v.Index] == 0u)
                {
                    return fail(HarmonicStatus::InvalidPins); // boundary not fully pinned
                }
            }
        }
        else
        {
            // Cumulative arc length for arc-length spacing.
            const std::size_t B = loop.size();
            std::vector<double> cumulative(B + 1, 0.0);
            for (std::size_t k = 0; k < B; ++k)
            {
                const glm::vec3 a = mesh.Position(loop[k]);
                const glm::vec3 b = mesh.Position(loop[(k + 1) % B]);
                cumulative[k + 1] = cumulative[k] + glm::distance(a, b);
            }
            const double perimeter = cumulative[B];
            for (std::size_t k = 0; k < B; ++k)
            {
                double t;
                if (params.ArcLengthSpacing && perimeter > 1e-18)
                {
                    t = cumulative[k] / perimeter;
                }
                else
                {
                    t = static_cast<double>(k) / static_cast<double>(B);
                }
                glm::vec2 target;
                if (params.Boundary == HarmonicBoundaryPolicy::Circle)
                {
                    const double angle = 2.0 * 3.14159265358979323846 * t;
                    target = glm::vec2(
                        0.5f + 0.5f * static_cast<float>(std::cos(angle)),
                        0.5f + 0.5f * static_cast<float>(std::sin(angle)));
                }
                else
                {
                    target = SquarePerimeter(t);
                }
                const std::uint32_t idx = loop[k].Index;
                uv[idx] = target;
                fixed[idx] = 1u;
            }
        }

        // Compact interior indexing.
        std::vector<std::uint32_t> interiorOf(nV, std::numeric_limits<std::uint32_t>::max());
        std::vector<VertexHandle> interior;
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            const VertexHandle v{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
            {
                continue;
            }
            if (fixed[vi] == 0u)
            {
                interiorOf[vi] = static_cast<std::uint32_t>(interior.size());
                interior.push_back(v);
            }
        }
        result.InteriorVertexCount = interior.size();

        // No interior unknowns: the boundary placement is the full result.
        if (interior.empty())
        {
            if (params.Boundary != HarmonicBoundaryPolicy::Custom)
            {
                FixOrientation(mesh, uv, loop);
            }
            result.UVs = uv;
            result.Diagnostics = EvaluateParameterizationDiagnostics(mesh, result.UVs);
            result.Status = HarmonicStatus::Success;
            return result;
        }

        // Per-edge weights.
        std::vector<double> edgeWeight(mesh.EdgesSize(), 0.0);
        if (params.Weights == HarmonicWeightType::Uniform)
        {
            for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
            {
                const EdgeHandle e{static_cast<PropertyIndex>(ei)};
                if (!mesh.IsDeleted(e))
                {
                    edgeWeight[ei] = 1.0;
                }
            }
        }
        else
        {
            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                const FaceHandle f{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(f))
                {
                    continue;
                }
                const HalfedgeHandle h0 = mesh.Halfedge(f);
                const HalfedgeHandle h1 = mesh.NextHalfedge(h0);
                const HalfedgeHandle h2 = mesh.NextHalfedge(h1);
                const VertexHandle a = mesh.ToVertex(h0);
                const VertexHandle b = mesh.ToVertex(h1);
                const VertexHandle c = mesh.ToVertex(h2);
                const glm::dvec3 pa(mesh.Position(a));
                const glm::dvec3 pb(mesh.Position(b));
                const glm::dvec3 pc(mesh.Position(c));
                // Edge(h0)=(c,a) opposite apex b; Edge(h1)=(a,b) opposite c;
                // Edge(h2)=(b,c) opposite a.
                edgeWeight[mesh.Edge(h0).Index] += 0.5 * CotAtApex(pb, pc, pa);
                edgeWeight[mesh.Edge(h1).Index] += 0.5 * CotAtApex(pc, pa, pb);
                edgeWeight[mesh.Edge(h2).Index] += 0.5 * CotAtApex(pa, pb, pc);
            }
            if (params.ClampNonConvexWeights)
            {
                for (double& w : edgeWeight)
                {
                    w = std::max(0.0, w);
                }
            }
        }

        // Assemble the SPD interior system A u = b (separately for x and y).
        const std::size_t n = interior.size();
        Sparse::SparseBuilder builder(n, n);
        std::vector<double> bx(n, 0.0);
        std::vector<double> by(n, 0.0);
        for (std::size_t row = 0; row < n; ++row)
        {
            const VertexHandle vi = interior[row];
            double diag = 0.0;
            for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vi))
            {
                const VertexHandle vj = mesh.ToVertex(h);
                const double w = edgeWeight[mesh.Edge(h).Index];
                diag += w;
                if (fixed[vj.Index] != 0u)
                {
                    bx[row] += w * static_cast<double>(uv[vj.Index].x);
                    by[row] += w * static_cast<double>(uv[vj.Index].y);
                }
                else
                {
                    builder.Add(row, interiorOf[vj.Index], -w);
                }
            }
            builder.Add(row, row, diag);
        }

        const Sparse::SparseBuildResult built = builder.Build();
        if (!built.Valid)
        {
            return fail(HarmonicStatus::SingularSystem);
        }

        Sparse::SparseLDLT solver;
        const Sparse::SparseFactorizationDiagnostics factored = solver.factor(built.Matrix);
        if (!factored.Succeeded())
        {
            const bool singular =
                factored.Status == Sparse::SparseFactorizationStatus::NonSPD ||
                factored.Status == Sparse::SparseFactorizationStatus::ZeroPivot ||
                factored.Status == Sparse::SparseFactorizationStatus::NumericalIssue;
            return fail(singular ? HarmonicStatus::SingularSystem : HarmonicStatus::SolverFailed);
        }

        std::vector<double> ux(n, 0.0);
        std::vector<double> uy(n, 0.0);
        const Sparse::SparseFactorizationDiagnostics sx = solver.solve(bx, ux);
        const Sparse::SparseFactorizationDiagnostics sy = solver.solve(by, uy);
        if (!sx.Succeeded() || !sy.Succeeded())
        {
            return fail(HarmonicStatus::SolverFailed);
        }

        for (std::size_t row = 0; row < n; ++row)
        {
            uv[interior[row].Index] = glm::vec2(static_cast<float>(ux[row]), static_cast<float>(uy[row]));
        }

        if (params.Boundary != HarmonicBoundaryPolicy::Custom)
        {
            FixOrientation(mesh, uv, loop);
        }

        result.UVs = uv;
        result.Diagnostics = EvaluateParameterizationDiagnostics(mesh, result.UVs);
        result.Status = HarmonicStatus::Success;
        return result;
    }
} // namespace Geometry::Parameterization
