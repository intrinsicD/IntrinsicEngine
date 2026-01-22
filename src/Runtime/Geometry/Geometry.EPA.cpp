module;
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <limits>
#include <array>

module Geometry:EPA.Impl;
import :EPA;
import Core; // For logging

namespace Geometry::Internal
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
// Calculate normal and distance for a face
    PolytopeFace MakeFace(const std::vector<glm::vec3>& polytope, int i0, int i1, int i2)
    {
        PolytopeFace f;
        f.Indices = {i0, i1, i2};
        glm::vec3 a = polytope[i0];
        glm::vec3 b = polytope[i1];
        glm::vec3 c = polytope[i2];

        // Winding order matters for EPA. We assume CCW relative to origin.
        f.Normal = glm::normalize(glm::cross(b - a, c - a));
        f.Distance = glm::dot(f.Normal, a);

        // If normal points towards origin, flip it. (We want normal pointing OUT of the Minkowski Difference)
        // Since Origin is INSIDE, Distance > 0 means Normal points OUT.
        // If Distance < 0, normal points IN.
        if (f.Distance < 0)
        {
            f.Normal = -f.Normal;
            f.Distance = -f.Distance;
            // std::swap(f.Indices[0], f.Indices[1]); // correct winding
        }
        return f;
    }

    template <typename ShapeA, typename ShapeB>
    std::optional<EPAResult> EPA_Solver(const ShapeA& a, const ShapeB& b, const Simplex& gjkSimplex)
    {
        // 1. Initialize Polytope from GJK Simplex
        std::vector<glm::vec3> polytope;
        std::vector<PolytopeFace> faces;

        // GJK returns a Simplex of size 4 (Tetrahedron) if it encloses the origin in 3D.
        // If size < 4, we have a degenerate case (touching contact or 2D).
        // For robustness, we handle size 4.
        if (gjkSimplex.Size != 4) return std::nullopt;

        for (int i = 0; i < 4; ++i) polytope.push_back(gjkSimplex.Points[i]);

        faces.push_back(MakeFace(polytope, 0, 1, 2));
        faces.push_back(MakeFace(polytope, 0, 2, 3));
        faces.push_back(MakeFace(polytope, 2, 1, 3));
        faces.push_back(MakeFace(polytope, 1, 0, 3));

        constexpr int MAX_ITER = 32;
        constexpr float TOLERANCE = 0.001f;

        for (int iter = 0; iter < MAX_ITER; ++iter)
        {
            // 2. Find face closest to origin
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

            glm::vec3 searchDir = faces[closestFaceIdx].Normal;

            // 3. Search for new support point in that direction
            // Minkowski Support = Support(A, dir) - Support(B, -dir)
            glm::vec3 support = Geometry::Support(a, searchDir) - Geometry::Support(b, -searchDir);

            float sDist = glm::dot(support, searchDir);

            // 4. Convergence check
            if (std::abs(sDist - minDist) < TOLERANCE)
            {
                EPAResult result;
                result.Normal = -searchDir; // Normal A->B
                result.Depth = sDist;

                result.ContactPoint = Geometry::Support(a, searchDir);

                return result;
            }

            // 5. Expand Polytope
            // Remove all faces visible from the new support point (lit faces)
            std::vector<PolytopeFace> newFaces;
            std::vector<Edge> uniqueEdges;

            auto AddEdge = [&](int a, int b) {
                for (auto it = uniqueEdges.begin(); it != uniqueEdges.end(); ) {
                    if (it->B == a && it->A == b) {
                        // Edge shared with a removed face -> internal edge -> remove
                        uniqueEdges.erase(it);
                        return;
                    }
                    ++it;
                }
                uniqueEdges.push_back({a, b});
            };

            for (size_t i = 0; i < faces.size(); ++i)
            {
                // Is face visible from support point?
                if (glm::dot(faces[i].Normal, support - polytope[faces[i].Indices[0]]) > 0)
                {
                    // Face is visible, remove it and add its edges
                    AddEdge(faces[i].Indices[0], faces[i].Indices[1]);
                    AddEdge(faces[i].Indices[1], faces[i].Indices[2]);
                    AddEdge(faces[i].Indices[2], faces[i].Indices[0]);
                }
                else
                {
                    newFaces.push_back(faces[i]);
                }
            }

            // If no faces were removed, we are inside/done (or convex issue).
            if (newFaces.size() == faces.size()) break;

            // Add new point
            polytope.push_back(support);
            int newIdx = (int)polytope.size() - 1;

            // Reconstruct horizon
            for (const auto& edge : uniqueEdges)
            {
                newFaces.push_back(MakeFace(polytope, edge.A, edge.B, newIdx));
            }

            faces = std::move(newFaces);
        }

        return std::nullopt;
    }
}