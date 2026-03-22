module;
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <glm/geometric.hpp>

module Geometry.MeshUtils;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Core.Logging;

namespace Geometry::MeshUtils
{
    namespace
    {
        constexpr float kTexcoordEpsilon = 1.0e-6f;

        struct QuantizedPositionKey
        {
            std::int64_t X{0};
            std::int64_t Y{0};
            std::int64_t Z{0};

            [[nodiscard]] friend bool operator==(const QuantizedPositionKey&, const QuantizedPositionKey&) = default;
        };

        struct QuantizedPositionKeyHash
        {
            [[nodiscard]] std::size_t operator()(const QuantizedPositionKey& key) const noexcept
            {
                std::size_t h = 1469598103934665603ull;
                auto mix = [&h](std::uint64_t v)
                {
                    h ^= static_cast<std::size_t>(v);
                    h *= 1099511628211ull;
                };
                mix(static_cast<std::uint64_t>(key.X));
                mix(static_cast<std::uint64_t>(key.Y));
                mix(static_cast<std::uint64_t>(key.Z));
                return h;
            }
        };

        [[nodiscard]] QuantizedPositionKey MakeQuantizedPositionKey(const glm::vec3& p, float epsilon) noexcept
        {
            if (epsilon > 0.0f)
            {
                const double inv = 1.0 / static_cast<double>(epsilon);
                return {
                    static_cast<std::int64_t>(std::llround(static_cast<double>(p.x) * inv)),
                    static_cast<std::int64_t>(std::llround(static_cast<double>(p.y) * inv)),
                    static_cast<std::int64_t>(std::llround(static_cast<double>(p.z) * inv))};
            }

            return {
                static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(p.x)),
                static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(p.y)),
                static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(p.z))};
        }

        [[nodiscard]] bool PositionsCoincide(const glm::vec3& a, const glm::vec3& b, float epsilon) noexcept
        {
            if (epsilon > 0.0f)
            {
                return glm::distance2(a, b) <= static_cast<double>(epsilon) * static_cast<double>(epsilon);
            }
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        [[nodiscard]] bool TexcoordsCoincide(const glm::vec2& a, const glm::vec2& b, float epsilon = kTexcoordEpsilon) noexcept
        {
            return glm::distance2(a, b) <= static_cast<double>(epsilon) * static_cast<double>(epsilon);
        }

        [[nodiscard]] glm::vec2 AuxToTexcoord(const glm::vec4& aux) noexcept
        {
            return {aux.x, aux.y};
        }
    }

    bool TryGetTriangleFaceView(const Halfedge::Mesh& mesh, FaceHandle f, TriangleFaceView& out)
    {
        if (!f.IsValid() || mesh.IsDeleted(f))
            return false;

        const HalfedgeHandle h0 = mesh.Halfedge(f);
        if (!h0.IsValid())
            return false;

        const HalfedgeHandle h1 = mesh.NextHalfedge(h0);
        const HalfedgeHandle h2 = mesh.NextHalfedge(h1);
        const HalfedgeHandle h3 = mesh.NextHalfedge(h2);

        // Restrict helper to pure triangle faces to keep downstream logic explicit.
        if (h3 != h0)
            return false;

        out.Face = f;
        out.H0 = h0;
        out.H1 = h1;
        out.H2 = h2;
        out.V0 = mesh.ToVertex(h0);
        out.V1 = mesh.ToVertex(h1);
        out.V2 = mesh.ToVertex(h2);
        out.P0 = mesh.Position(out.V0);
        out.P1 = mesh.Position(out.V1);
        out.P2 = mesh.Position(out.V2);
        return true;
    }

    std::vector<BoundaryLoopData> CollectBoundaryLoops(const Halfedge::Mesh& mesh)
    {
        std::vector<BoundaryLoopData> loops;
        if (mesh.IsEmpty() || mesh.HalfedgesSize() == 0)
            return loops;

        const std::size_t nH = mesh.HalfedgesSize();
        std::vector<bool> visited(nH, false);

        for (std::size_t hi = 0; hi < nH; ++hi)
        {
            HalfedgeHandle h{static_cast<PropertyIndex>(hi)};
            if (visited[hi]) continue;
            if (mesh.IsDeleted(mesh.Edge(h))) continue;
            if (!mesh.IsBoundary(h)) continue;

            BoundaryLoopData loop;
            for (const HalfedgeHandle cur : mesh.BoundaryHalfedges(h))
            {
                visited[cur.Index] = true;
                loop.Halfedges.push_back(cur);
            }

            for (const VertexHandle v : mesh.BoundaryVertices(h))
            {
                loop.Vertices.push_back(v);
            }

            if (!loop.Vertices.empty())
                loops.push_back(std::move(loop));
        }

        return loops;
    }

    void CalculateNormals(std::span<const glm::vec3> positions, std::span<const uint32_t> indices,
                          std::span<glm::vec3> normals)
    {
        // Reset normals to zero
        std::fill(normals.begin(), normals.end(), glm::vec3(0.0f));

        // Determine count based on whether we are using indices or raw vertices
        size_t count = indices.empty() ? positions.size() : indices.size();

        for (size_t i = 0; i < count; i += 3)
        {
            uint32_t i0 = indices.empty() ? (uint32_t)i : indices[i];
            uint32_t i1 = indices.empty() ? (uint32_t)(i + 1) : indices[i + 1];
            uint32_t i2 = indices.empty() ? (uint32_t)(i + 2) : indices[i + 2];

            // Safety check
            if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;

            glm::vec3 v0 = positions[i0];
            glm::vec3 v1 = positions[i1];
            glm::vec3 v2 = positions[i2];

            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;

            // Area-weighted normal (magnitude of cross product is 2x area)
            // This ensures larger triangles contribute more to the smooth normal.
            glm::vec3 normal = glm::cross(edge1, edge2);

            normals[i0] += normal;
            normals[i1] += normal;
            normals[i2] += normal;
        }

        // Normalize
        for (auto& n : normals)
        {
            float len2 = glm::length2(n);
            if (len2 > 1e-12f)
            {
                n = n * glm::inversesqrt(len2);
            }
            else
            {
                // Degenerate normal (e.g., zero area triangle), default to Up
                n = glm::vec3(0, 1, 0);
            }
        }
    }

    // --- Helper: UV Generation ---
    // Uses Planar Projection based on the largest dimensions of the mesh
    int GenerateUVs(std::span<const glm::vec3> positions, std::span<glm::vec4> aux)
    {
        if (positions.empty()) return -1;
        if (aux.size() < positions.size())
        {
            Core::Log::Error("GenerateUVs: aux span smaller than positions.");
            return -1;
        }
        // 1. Calculate AABB
        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

        for (const auto& pos : positions)
        {
            minBounds = glm::min(minBounds, pos);
            maxBounds = glm::max(maxBounds, pos);
        }

        glm::vec3 size = maxBounds - minBounds;

        // 2. Determine dominant plane (Find smallest axis to collapse)
        // 0=X, 1=Y, 2=Z
        int flatAxis = 0;
        if (size.y < size.x && size.y < size.z) flatAxis = 1; // Flatten Y -> XZ Plane
        else if (size.z < size.x && size.z < size.y) flatAxis = 2; // Flatten Z -> XY Plane
        // else Flatten X -> YZ Plane

        // Avoid divide by zero for 2D meshes or points
        if (size.x < 1e-6f) size.x = 1.0f;
        if (size.y < 1e-6f) size.y = 1.0f;
        if (size.z < 1e-6f) size.z = 1.0f;

        // 3. Generate UVs
        for (size_t i = 0; i < positions.size(); ++i)
        {
            const auto& pos = positions[i];
            glm::vec2 uv(0.0f);

            if (flatAxis == 0)
            {
                uv.x = (pos.z - minBounds.z) / size.z;
                uv.y = (pos.y - minBounds.y) / size.y;
            }
            else if (flatAxis == 1)
            {
                uv.x = (pos.x - minBounds.x) / size.x;
                uv.y = (pos.z - minBounds.z) / size.z;
            }
            else
            {
                uv.x = (pos.x - minBounds.x) / size.x;
                uv.y = (pos.y - minBounds.y) / size.y;
            }

            // Note: Vulkan UVs top-left is 0,0. GLTF/OpenGL bottom-left is 0,0.
            // We usually flip V here or in shader. Let's flip V to match standard texture mapping expectations.
            uv.y = 1.0f - uv.y;

            // Store in Aux (xy = UV)
            aux[i].x = uv.x;
            aux[i].y = uv.y;
        }

        return flatAxis;
    }

    // =========================================================================
    // Halfedge mesh math utilities
    // =========================================================================

    double Cotan(glm::vec3 u, glm::vec3 v)
    {
        auto crossVec = glm::cross(u, v);
        double sinVal = static_cast<double>(glm::length(crossVec));
        double cosVal = static_cast<double>(glm::dot(u, v));

        if (sinVal < 1e-10)
            return 0.0;

        return cosVal / sinVal;
    }

    double TriangleArea(glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        return 0.5 * static_cast<double>(glm::length(glm::cross(b - a, c - a)));
    }

    double AngleAtVertex(glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        const glm::dvec3 ab = glm::dvec3(b) - glm::dvec3(a);
        const glm::dvec3 ac = glm::dvec3(c) - glm::dvec3(a);
        const double lenAB = glm::length(ab);
        const double lenAC = glm::length(ac);

        if (lenAB < 1e-10f || lenAC < 1e-10f)
            return 0.0;

        const double sinAngle = glm::length(glm::cross(ab, ac));
        const double cosAngle = glm::dot(ab, ac);
        return std::atan2(sinAngle, cosAngle);
    }

    double EdgeLengthSq(const Halfedge::Mesh& mesh, EdgeHandle e)
    {
        HalfedgeHandle h{static_cast<PropertyIndex>(2u * e.Index)};
        glm::vec3 a = mesh.Position(mesh.FromVertex(h));
        glm::vec3 b = mesh.Position(mesh.ToVertex(h));
        glm::vec3 d = b - a;
        return static_cast<double>(glm::dot(d, d));
    }

    double MeanEdgeLength(const Halfedge::Mesh& mesh)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e)) continue;
            sum += std::sqrt(EdgeLengthSq(mesh, e));
            ++count;
        }
        return (count > 0) ? (sum / static_cast<double>(count)) : 0.0;
    }

    double EdgeCotanWeight(const Halfedge::Mesh& mesh, EdgeHandle e)
    {
        if (!mesh.IsValid(e) || mesh.IsDeleted(e))
        {
            return 0.0;
        }

        const HalfedgeHandle h0{static_cast<PropertyIndex>(2u * e.Index)};
        const HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);

        const VertexHandle v0 = mesh.FromVertex(h0);
        const VertexHandle v1 = mesh.ToVertex(h0);

        double cotSum = 0.0;

        if (!mesh.IsBoundary(h0))
        {
            const VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h0));
            const glm::vec3 u = mesh.Position(v0) - mesh.Position(vOpp);
            const glm::vec3 v = mesh.Position(v1) - mesh.Position(vOpp);
            cotSum += Cotan(u, v);
        }

        if (!mesh.IsBoundary(h1))
        {
            const VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h1));
            const glm::vec3 u = mesh.Position(v1) - mesh.Position(vOpp);
            const glm::vec3 v = mesh.Position(v0) - mesh.Position(vOpp);
            cotSum += Cotan(u, v);
        }

        return cotSum / 2.0;
    }

    glm::vec3 FaceNormal(const Halfedge::Mesh& mesh, FaceHandle f)
    {
        HalfedgeHandle h0 = mesh.Halfedge(f);
        HalfedgeHandle h1 = mesh.NextHalfedge(h0);
        HalfedgeHandle h2 = mesh.NextHalfedge(h1);

        glm::vec3 a = mesh.Position(mesh.ToVertex(h0));
        glm::vec3 b = mesh.Position(mesh.ToVertex(h1));
        glm::vec3 c = mesh.Position(mesh.ToVertex(h2));

        return glm::cross(b - a, c - a);
    }

    glm::vec3 VertexNormal(const Halfedge::Mesh& mesh, VertexHandle v)
    {
        glm::vec3 n(0.0f);
        for (const FaceHandle f : mesh.FacesAroundVertex(v))
        {
            n += FaceNormal(mesh, f);
        }

        float len = glm::length(n);
        return (len > 1e-8f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    int TargetValence(const Halfedge::Mesh& mesh, VertexHandle v)
    {
        return mesh.IsBoundary(v) ? 4 : 6;
    }

    std::size_t EqualizeValenceByEdgeFlip(Halfedge::Mesh& mesh, bool preserveBoundary)
    {
        std::size_t flipCount = 0;

        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle e{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(e) || mesh.IsBoundary(e))
                continue;

            if (!mesh.IsFlipOk(e))
                continue;

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);

            VertexHandle a = mesh.FromVertex(h0);
            VertexHandle b = mesh.ToVertex(h0);
            VertexHandle c = mesh.ToVertex(mesh.NextHalfedge(h0));
            VertexHandle d = mesh.ToVertex(mesh.NextHalfedge(h1));

            if (preserveBoundary)
            {
                if (mesh.IsBoundary(a) || mesh.IsBoundary(b) ||
                    mesh.IsBoundary(c) || mesh.IsBoundary(d))
                    continue;
            }

            auto valenceError = [&](VertexHandle v, int delta) -> int {
                int val = static_cast<int>(mesh.Valence(v)) + delta;
                return std::abs(val - TargetValence(mesh, v));
            };

            int devBefore = valenceError(a, 0) + valenceError(b, 0) +
                            valenceError(c, 0) + valenceError(d, 0);
            int devAfter = valenceError(a, -1) + valenceError(b, -1) +
                           valenceError(c, +1) + valenceError(d, +1);

            if (devAfter < devBefore)
            {
                (void)mesh.Flip(e);
                ++flipCount;
            }
        }

        return flipCount;
    }

    void TangentialSmooth(Halfedge::Mesh& mesh, double lambda, bool preserveBoundary)
    {
        const std::size_t nV = mesh.VerticesSize();
        std::vector<glm::vec3> newPositions(nV);

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh) ||
                (preserveBoundary && mesh.IsBoundary(vh)))
            {
                newPositions[vi] = mesh.Position(vh);
                continue;
            }

            const glm::vec3 p = mesh.Position(vh);
            glm::vec3 centroid = glm::vec3(ComputeOneRingCentroid(mesh, vh));
            glm::vec3 displacement = centroid - p;
            glm::vec3 n = VertexNormal(mesh, vh);
            glm::vec3 tangential = displacement - glm::dot(displacement, n) * n;
            newPositions[vi] = p + static_cast<float>(lambda) * tangential;
        }

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
                continue;
            mesh.Position(vh) = newPositions[vi];
        }
    }

    std::vector<double> ComputeMixedVoronoiAreas(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nF = mesh.FacesSize();

        std::vector<double> areas(nV, 0.0);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;

            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            glm::vec3 eAB = pb - pa;
            glm::vec3 eAC = pc - pa;
            glm::vec3 eBC = pc - pb;

            double area = TriangleArea(pa, pb, pc);
            if (area < 1e-12) continue;

            double dotA = static_cast<double>(glm::dot(eAB, eAC));
            double dotB = static_cast<double>(glm::dot(-eAB, eBC));
            double dotC = static_cast<double>(glm::dot(-eAC, -eBC));

            if (dotA < 0.0)
            {
                areas[va.Index] += area / 2.0;
                areas[vb.Index] += area / 4.0;
                areas[vc.Index] += area / 4.0;
            }
            else if (dotB < 0.0)
            {
                areas[va.Index] += area / 4.0;
                areas[vb.Index] += area / 2.0;
                areas[vc.Index] += area / 4.0;
            }
            else if (dotC < 0.0)
            {
                areas[va.Index] += area / 4.0;
                areas[vb.Index] += area / 4.0;
                areas[vc.Index] += area / 2.0;
            }
            else
            {
                double cotA = Cotan(eAB, eAC);
                double cotB = Cotan(-eAB, eBC);
                double cotC = Cotan(-eAC, -eBC);

                double lenSqAB = static_cast<double>(glm::dot(eAB, eAB));
                double lenSqAC = static_cast<double>(glm::dot(eAC, eAC));
                double lenSqBC = static_cast<double>(glm::dot(eBC, eBC));

                areas[va.Index] += (lenSqAB * cotC + lenSqAC * cotB) / 8.0;
                areas[vb.Index] += (lenSqAB * cotC + lenSqBC * cotA) / 8.0;
                areas[vc.Index] += (lenSqAC * cotB + lenSqBC * cotA) / 8.0;
            }
        }

        return areas;
    }

    std::vector<glm::dvec3> ComputeCotanLaplacian(
        const Halfedge::Mesh& mesh,
        bool clampNonNegative)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();

        std::vector<glm::dvec3> laplacian(nV, glm::dvec3(0.0));

        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh)) continue;

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};

            VertexHandle vi = mesh.FromVertex(h0);
            VertexHandle vj = mesh.ToVertex(h0);

            const double edgeWeight = EdgeCotanWeight(mesh, eh);
            const double w = clampNonNegative ? std::max(0.0, edgeWeight) : edgeWeight;
            glm::dvec3 diff = glm::dvec3(mesh.Position(vj)) - glm::dvec3(mesh.Position(vi));

            laplacian[vi.Index] += w * diff;
            laplacian[vj.Index] -= w * diff;
        }

        return laplacian;
    }

    std::vector<double> ComputeVertexAngleSums(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nF = mesh.FacesSize();
        std::vector<double> angleSums(nV, 0.0);

        TriangleFaceView tri{};
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            const FaceHandle f{static_cast<PropertyIndex>(fi)};
            if (!TryGetTriangleFaceView(mesh, f, tri))
            {
                continue;
            }

            angleSums[tri.V0.Index] += AngleAtVertex(tri.P0, tri.P1, tri.P2);
            angleSums[tri.V1.Index] += AngleAtVertex(tri.P1, tri.P2, tri.P0);
            angleSums[tri.V2.Index] += AngleAtVertex(tri.P2, tri.P0, tri.P1);
        }

        return angleSums;
    }

    double ComputeVertexAngleDefect(const Halfedge::Mesh& mesh,
                                    VertexHandle v,
                                    double angleSumAtVertex)
    {
        return mesh.IsBoundary(v)
            ? std::numbers::pi - angleSumAtVertex
            : 2.0 * std::numbers::pi - angleSumAtVertex;
    }

    glm::dvec3 ComputeOneRingCentroid(const Halfedge::Mesh& mesh, VertexHandle v)
    {
        glm::dvec3 centroid(0.0);
        std::size_t count = 0;

        for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(v))
        {
            centroid += glm::dvec3(mesh.Position(mesh.ToVertex(h)));
            ++count;
        }

        if (count == 0)
            return glm::dvec3(mesh.Position(v));

        return centroid / static_cast<double>(count);
    }

    std::optional<Halfedge::Mesh> BuildHalfedgeMeshFromIndexedTriangles(
        std::span<const glm::vec3> positions,
        std::span<const uint32_t> indices,
        const TriangleSoupBuildParams& params)
    {
        return BuildHalfedgeMeshFromIndexedTriangles(positions, indices, std::span<const glm::vec4>{}, params);
    }

    std::optional<Halfedge::Mesh> BuildHalfedgeMeshFromIndexedTriangles(
        std::span<const glm::vec3> positions,
        std::span<const uint32_t> indices,
        std::span<const glm::vec4> aux,
        const TriangleSoupBuildParams& params)
    {
        if (positions.empty() || indices.empty() || (indices.size() % 3u) != 0u)
        {
            return std::nullopt;
        }

        const bool hasAux = aux.empty() ? false : (aux.size() == positions.size());
        if (!aux.empty() && !hasAux)
        {
            Core::Log::Warn("BuildHalfedgeMeshFromIndexedTriangles: aux vertex count ({}) does not match positions ({})",
                            aux.size(), positions.size());
            return std::nullopt;
        }

        Halfedge::Mesh mesh;
        std::vector<VertexHandle> remap(positions.size(), VertexHandle{});
        VertexProperty<glm::vec2> texcoord;
        if (hasAux)
        {
            texcoord = VertexProperty<glm::vec2>(
                mesh.VertexProperties().GetOrAdd<glm::vec2>(kVertexTexcoordPropertyName, glm::vec2(0.0f)));
        }

        const bool weldVertices = params.WeldVertices;
        const float weldEpsilon = params.WeldEpsilon;

        if (!weldVertices)
        {
            for (std::size_t i = 0; i < positions.size(); ++i)
            {
                remap[i] = mesh.AddVertex(positions[i]);
                if (hasAux)
                {
                    texcoord[remap[i]] = AuxToTexcoord(aux[i]);
                }
            }
        }
        else
        {
            std::unordered_map<QuantizedPositionKey, std::vector<std::size_t>, QuantizedPositionKeyHash> buckets;
            buckets.reserve(positions.size());

            for (std::size_t i = 0; i < positions.size(); ++i)
            {
                const auto key = MakeQuantizedPositionKey(positions[i], weldEpsilon);
                auto& candidates = buckets[key];

                VertexHandle welded;
                for (const std::size_t candidate : candidates)
                {
                    if (!PositionsCoincide(positions[i], positions[candidate], weldEpsilon))
                    {
                        continue;
                    }
                    if (hasAux && !TexcoordsCoincide(AuxToTexcoord(aux[i]), AuxToTexcoord(aux[candidate])))
                    {
                        continue;
                    }

                    welded = remap[candidate];
                    break;
                }

                if (!welded.IsValid())
                {
                    welded = mesh.AddVertex(positions[i]);
                    if (hasAux)
                    {
                        texcoord[welded] = AuxToTexcoord(aux[i]);
                    }
                    candidates.push_back(i);
                }

                remap[i] = welded;
            }
        }

        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t i0 = indices[i];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            if (i0 >= remap.size() || i1 >= remap.size() || i2 >= remap.size())
            {
                Core::Log::Warn("BuildHalfedgeMeshFromIndexedTriangles: triangle {} has out-of-range indices ({}, {}, {})",
                                i / 3u, i0, i1, i2);
                return std::nullopt;
            }

            const VertexHandle v0 = remap[i0];
            const VertexHandle v1 = remap[i1];
            const VertexHandle v2 = remap[i2];
            if (!v0.IsValid() || !v1.IsValid() || !v2.IsValid())
            {
                return std::nullopt;
            }
            if (v0 == v1 || v1 == v2 || v2 == v0)
            {
                continue;
            }

            auto face = mesh.AddTriangle(v0, v1, v2);
            if (!face)
            {
                face = mesh.AddTriangle(v0, v2, v1);
            }
            if (!face)
            {
                Core::Log::Warn("BuildHalfedgeMeshFromIndexedTriangles: failed to insert triangle {} after welding", i / 3u);
                return std::nullopt;
            }
        }

        return mesh;
    }

    void ExtractIndexedTriangles(
        const Halfedge::Mesh& mesh,
        std::vector<glm::vec3>& positions,
        std::vector<uint32_t>& indices,
        std::vector<glm::vec4>* aux,
        std::vector<uint32_t>* triangleFaceIds)
    {
        positions.clear();
        indices.clear();
        if (aux != nullptr)
        {
            aux->clear();
        }
        if (triangleFaceIds != nullptr)
        {
            triangleFaceIds->clear();
        }

        positions.reserve(mesh.VertexCount());
        indices.reserve(mesh.FaceCount() * 6u);

        std::vector<uint32_t> vertexMap(mesh.VerticesSize(), 0u);
        uint32_t currentIndex = 0u;

        const auto texcoord = mesh.VertexProperties().Get<glm::vec2>(kVertexTexcoordPropertyName);
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            const VertexHandle v{static_cast<PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
            {
                continue;
            }

            vertexMap[i] = currentIndex++;
            positions.push_back(mesh.Position(v));
            if (aux != nullptr)
            {
                glm::vec4 packed(0.0f);
                if (texcoord)
                {
                    const glm::vec2 uv = texcoord[i];
                    packed.x = uv.x;
                    packed.y = uv.y;
                }
                aux->push_back(packed);
            }
        }

        for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
        {
            const FaceHandle f{static_cast<PropertyIndex>(i)};
            if (!mesh.IsValid(f) || mesh.IsDeleted(f))
            {
                continue;
            }

            const HalfedgeHandle h0 = mesh.Halfedge(f);
            if (!mesh.IsValid(h0))
            {
                continue;
            }

            std::vector<VertexHandle> faceVerts;
            faceVerts.reserve(mesh.Valence(f));
            HalfedgeHandle h = h0;
            std::size_t safety = 0;
            const std::size_t maxIter = mesh.HalfedgesSize();
            do
            {
                const VertexHandle v = mesh.ToVertex(h);
                if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                {
                    faceVerts.clear();
                    break;
                }
                faceVerts.push_back(v);
                h = mesh.NextHalfedge(h);
                if (++safety > maxIter)
                {
                    faceVerts.clear();
                    break;
                }
            } while (h != h0);

            if (faceVerts.size() < 3)
            {
                continue;
            }

            const VertexHandle v0 = faceVerts.front();
            for (std::size_t corner = 1; corner + 1 < faceVerts.size(); ++corner)
            {
                const VertexHandle v1 = faceVerts[corner];
                const VertexHandle v2 = faceVerts[corner + 1];
                indices.push_back(vertexMap[v0.Index]);
                indices.push_back(vertexMap[v1.Index]);
                indices.push_back(vertexMap[v2.Index]);
                if (triangleFaceIds != nullptr)
                    triangleFaceIds->push_back(static_cast<uint32_t>(f.Index));
            }
        }
    }
}
