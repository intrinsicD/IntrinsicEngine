// Bounded EPA penetration queries over GJK-normalized support-map simplices.
module;
#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <optional>
#include <algorithm>
#include <limits>
#include <array>
#include <span>

export module Geometry.EPA;

import Geometry.GJK;
import Geometry.Support;
import Geometry.Primitives;
import Geometry.RobustPredicates;
import Extrinsic.Core.Memory;

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

        struct SeedDirection
        {
            glm::vec3 Direction;
            double TouchingSlack;
        };

        // Complete an accepted line/triangle without replacing its vertices.
        [[nodiscard]] std::optional<SeedDirection> SeedExpansion(std::span<const glm::vec3> simplex);
        // Preserve directed horizon winding; Distance remains signed.
        [[nodiscard]] PolytopeFace MakeFace(std::span<const glm::vec3> polytope, int i0, int i1, int i2);
        [[nodiscard]] PolytopeFace MakeSeedFace(std::span<const glm::vec3> polytope,
                                                int i0, int i1, int i2, int opposite);
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
        Extrinsic::Core::Memory::LinearArena& scratch)
    {
        constexpr int MAX_ITER = Config::EPA_MAX_ITERATIONS;

        using Vec3Vec = std::vector<glm::vec3, Extrinsic::Core::Memory::ArenaAllocator<glm::vec3>>;
        using FaceVec = std::vector<Detail::PolytopeFace, Extrinsic::Core::Memory::ArenaAllocator<Detail::PolytopeFace>>;
        using EdgeVec = std::vector<Detail::Edge, Extrinsic::Core::Memory::ArenaAllocator<Detail::Edge>>;

        Extrinsic::Core::Memory::ArenaAllocator<glm::vec3> vec3Alloc(scratch);
        Extrinsic::Core::Memory::ArenaAllocator<Detail::PolytopeFace> faceAlloc(scratch);
        Extrinsic::Core::Memory::ArenaAllocator<Detail::Edge> edgeAlloc(scratch);

        if (gjkSimplex.Size < 1 || gjkSimplex.Size > 4) return std::nullopt;

        // Use the same normalization factor that GJK used.
        const float invScale = gjkSimplex.InvScale;
        if (!std::isfinite(invScale) || invScale <= 0.0f) return std::nullopt;
        const auto contactAt = [&](const glm::vec3& direction, double normalizedDepth) -> std::optional<EPAResult>
        {
            const float depth = static_cast<float>(std::max(normalizedDepth, 0.0) / double(invScale));
            const glm::vec3 point = Geometry::Support(a, direction);
            if (!std::isfinite(depth) || !std::isfinite(point.x) ||
                !std::isfinite(point.y) || !std::isfinite(point.z)) return std::nullopt;
            return EPAResult{direction, depth, point};
        };
        // GJK returns one point only when its initial +X support is within its origin tolerance.
        if (gjkSimplex.Size == 1)
            return contactAt(glm::vec3(1.0f, 0.0f, 0.0f), 0.0);

        Vec3Vec polytope(vec3Alloc);
        FaceVec faces(faceAlloc);

        polytope.reserve(4 + MAX_ITER + 1);
        faces.reserve(64);

        for (int i = 0; i < gjkSimplex.Size; ++i) polytope.push_back(gjkSimplex.Points[i]);
        while (polytope.size() < 4)
        {
            const auto seed = Detail::SeedExpansion(std::span<const glm::vec3>(polytope));
            if (!seed) return std::nullopt;
            const auto& direction = seed->Direction;
            const glm::vec3 positive = MinkowskiDifference::Support(a, b, direction) * invScale;
            const glm::vec3 negative = MinkowskiDifference::Support(a, b, -direction) * invScale;
            const glm::dvec3 normal(direction);
            const double upper = glm::dot(glm::dvec3(positive), normal);
            const double lower = glm::dot(glm::dvec3(negative), normal);
            if (!std::isfinite(upper) || !std::isfinite(lower)) return std::nullopt;
            if (std::abs(upper) <= seed->TouchingSlack && std::abs(lower) <= seed->TouchingSlack)
                return contactAt(direction, 0.0);
            const double origin = glm::dot(glm::dvec3(polytope.front()), normal);
            const double positiveHeight = std::abs(upper - origin);
            const double negativeHeight = std::abs(lower - origin);
            if (!(std::max(positiveHeight, negativeHeight) > 0.0)) return std::nullopt;
            polytope.push_back(positiveHeight >= negativeHeight ? positive : negative);
        }

        const auto seed = std::span<const glm::vec3>(polytope);
        faces.push_back(Detail::MakeSeedFace(seed, 0, 1, 2, 3));
        faces.push_back(Detail::MakeSeedFace(seed, 0, 2, 3, 1));
        faces.push_back(Detail::MakeSeedFace(seed, 2, 1, 3, 0));
        faces.push_back(Detail::MakeSeedFace(seed, 1, 0, 3, 2));
        for (const auto& face : faces)
            if (glm::dot(face.Normal, face.Normal) == 0.0f) return std::nullopt;

        for (int iter = 0; iter < MAX_ITER; ++iter)
        {
            if (faces.empty()) return std::nullopt;
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
            const double sDist = glm::dot(glm::dvec3(support), glm::dvec3(searchDir));
            if (!std::isfinite(sDist)) return std::nullopt;

            if (std::abs(sDist - minDist) < Config::EPA_EPSILON)
            {
                // The outward Minkowski A-B face normal is the A->B contact
                // normal. Accepted GJK slack can leave a nonpositive distance;
                // that is touching, never a negative penetration.
                return contactAt(searchDir, sDist);
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
                // A rounded unit normal can make an exactly coplanar point
                // visible and create a zero-area horizon face. Classify from
                // the vertices with the canonical filtered volume predicate.
                const auto side = RobustPredicates::Orientation3D(polytope[f.Indices[0]],
                    polytope[f.Indices[1]], polytope[f.Indices[2]], support);
                if (side.Certainty == RobustPredicates::Certainty::Certain &&
                    side.Sign == RobustPredicates::Sign::Positive)
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
                const auto face = Detail::MakeFace(polySpan, edge.A, edge.B, newIdx);
                if (glm::dot(face.Normal, face.Normal) == 0.0f) return std::nullopt;
                newFaces.push_back(face);
            }

            faces = std::move(newFaces);
        }

        return std::nullopt;
    }
}
