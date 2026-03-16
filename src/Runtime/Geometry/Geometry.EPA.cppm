module;
#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <optional>
#include <algorithm>
#include <limits>
#include <array>
#include <span>

export module Geometry:EPA;

import :GJK;
import :Support;
import :Primitives;
import Core.Memory;

export namespace Geometry::Internal
{
    struct EPAResult
    {
        glm::vec3 Normal;
        float Depth;
        glm::vec3 ContactPoint; // On object A
    };

    namespace Detail
    {
        struct PolytopeFace
        {
            glm::vec3 Normal;
            float Distance;
            std::array<int, 3> Indices;
        };

        struct Edge
        {
            int A, B;
            bool operator==(const Edge& other) const { return A == other.A && B == other.B; }
        };

        // Returns a valid face, or a face with Normal=(0,0,0) and Distance=max
        // when the three vertices are collinear (degenerate).
        [[nodiscard]] inline PolytopeFace MakeFace(std::span<const glm::vec3> polytope, int i0, int i1, int i2)
        {
            PolytopeFace f{};
            f.Indices = {i0, i1, i2};
            const glm::vec3 a = polytope[i0];
            const glm::vec3 b = polytope[i1];
            const glm::vec3 c = polytope[i2];

            const glm::vec3 cross = glm::cross(b - a, c - a);
            const float lenSq = glm::dot(cross, cross);

            // Guard: collinear vertices produce a zero cross product.
            // Mark as degenerate with infinite distance so it is never chosen
            // as the closest face during EPA expansion.
            if (lenSq < 1e-20f)
            {
                f.Normal = glm::vec3(0.0f);
                f.Distance = std::numeric_limits<float>::max();
                return f;
            }

            // Winding order matters for EPA. We assume CCW relative to origin.
            f.Normal = cross * (1.0f / std::sqrt(lenSq));
            f.Distance = glm::dot(f.Normal, a);

            // Ensure outward-facing normal.
            if (f.Distance < 0)
            {
                f.Normal = -f.Normal;
                f.Distance = -f.Distance;
            }
            return f;
        }
    }

    // =========================================================================
    // Normalized-workspace EPA
    //
    // The incoming simplex from GJK_Intersection is already in normalized
    // space (points * invScale). EPA continues in the same normalized space:
    // all new support queries are scaled by the same invScale factor.
    //
    // On convergence, the penetration normal is scale-invariant (unit vector),
    // and the depth is unscaled back to world space (depth / invScale).
    // The contact point on A is computed from the original-space support
    // function so it is already in world coordinates.
    // =========================================================================

    template <typename ShapeA, typename ShapeB>
    [[nodiscard]] inline std::optional<EPAResult> EPA_Solver(
        const ShapeA& a,
        const ShapeB& b,
        const Simplex& gjkSimplex,
        Core::Memory::LinearArena& scratch)
    {
        constexpr int MAX_ITER = Config::EPA_MAX_ITERATIONS;

        using Vec3Vec = std::vector<glm::vec3, Core::Memory::ArenaAllocator<glm::vec3>>;
        using FaceVec = std::vector<Detail::PolytopeFace, Core::Memory::ArenaAllocator<Detail::PolytopeFace>>;
        using EdgeVec = std::vector<Detail::Edge, Core::Memory::ArenaAllocator<Detail::Edge>>;

        Core::Memory::ArenaAllocator<glm::vec3> vec3Alloc(scratch);
        Core::Memory::ArenaAllocator<Detail::PolytopeFace> faceAlloc(scratch);
        Core::Memory::ArenaAllocator<Detail::Edge> edgeAlloc(scratch);

        if (gjkSimplex.Size != 4) return std::nullopt;

        // Use the same normalization factor that GJK used.
        const float invScale = gjkSimplex.InvScale;
        const float scale = (invScale > 1e-30f) ? (1.0f / invScale) : 1.0f;

        Vec3Vec polytope(vec3Alloc);
        FaceVec faces(faceAlloc);

        polytope.reserve(4 + MAX_ITER + 1);
        faces.reserve(64);

        for (int i = 0; i < 4; ++i) polytope.push_back(gjkSimplex.Points[i]);

        faces.push_back(Detail::MakeFace(std::span<const glm::vec3>(polytope), 0, 1, 2));
        faces.push_back(Detail::MakeFace(std::span<const glm::vec3>(polytope), 0, 2, 3));
        faces.push_back(Detail::MakeFace(std::span<const glm::vec3>(polytope), 2, 1, 3));
        faces.push_back(Detail::MakeFace(std::span<const glm::vec3>(polytope), 1, 0, 3));

        for (int iter = 0; iter < MAX_ITER; ++iter)
        {
            float minDist = std::numeric_limits<float>::max();
            size_t closestFaceIdx = 0;

            for (size_t i = 0; i < faces.size(); ++i)
            {
                if (faces[i].Distance < minDist)
                {
                    minDist = faces[i].Distance;
                    closestFaceIdx = i;
                }
            }

            const glm::vec3 searchDir = faces[closestFaceIdx].Normal;

            // Degenerate face picked (should not happen — distance is max).
            if (glm::dot(searchDir, searchDir) < 1e-20f) return std::nullopt;

            // Support query in original space, normalized into the workspace.
            const glm::vec3 support =
                (Geometry::Support(a, searchDir) - Geometry::Support(b, -searchDir)) * invScale;
            const float sDist = glm::dot(support, searchDir);

            if (std::abs(sDist - minDist) < Config::EPA_EPSILON)
            {
                EPAResult result{};
                result.Normal = -searchDir; // Normal A->B (unit vector, scale-invariant)
                result.Depth = sDist * scale; // Unscale penetration depth to world space
                result.ContactPoint = Geometry::Support(a, searchDir); // Already in world space
                return result;
            }

            FaceVec newFaces(faceAlloc);
            EdgeVec uniqueEdges(edgeAlloc);
            newFaces.reserve(faces.size() + 8);
            uniqueEdges.reserve(faces.size() * 3);

            auto AddEdge = [&](int ea, int eb) {
                for (auto it = uniqueEdges.begin(); it != uniqueEdges.end(); )
                {
                    if (it->B == ea && it->A == eb)
                    {
                        uniqueEdges.erase(it);
                        return;
                    }
                    ++it;
                }
                uniqueEdges.push_back({ea, eb});
            };

            for (const Detail::PolytopeFace& f : faces)
            {
                if (glm::dot(f.Normal, support - polytope[f.Indices[0]]) > 0)
                {
                    AddEdge(f.Indices[0], f.Indices[1]);
                    AddEdge(f.Indices[1], f.Indices[2]);
                    AddEdge(f.Indices[2], f.Indices[0]);
                }
                else
                {
                    newFaces.push_back(f);
                }
            }

            if (newFaces.size() == faces.size()) break;

            polytope.push_back(support);
            const int newIdx = static_cast<int>(polytope.size()) - 1;
            const auto polySpan = std::span<const glm::vec3>(polytope);

            for (const Detail::Edge& edge : uniqueEdges)
            {
                newFaces.push_back(Detail::MakeFace(polySpan, edge.A, edge.B, newIdx));
            }

            faces = std::move(newFaces);
        }

        return std::nullopt;
    }
}
