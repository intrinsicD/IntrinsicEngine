module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <span>
#include <limits>
#include <algorithm>

module Geometry:MeshUtils.Impl;
import :MeshUtils;
import Core.Logging;

namespace Geometry::MeshUtils
{
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

            switch (flatAxis)
            {
            case 0: // YZ Plane (Side view)
                uv.x = (pos.z - minBounds.z) / size.z;
                uv.y = (pos.y - minBounds.y) / size.y; // Typically Y is V
                break;
            case 1: // XZ Plane (Top-down / Floor)
                uv.x = (pos.x - minBounds.x) / size.x;
                uv.y = (pos.z - minBounds.z) / size.z;
                break;
            case 2: // XY Plane (Front view)
                uv.x = (pos.x - minBounds.x) / size.x;
                uv.y = (pos.y - minBounds.y) / size.y; // Y is V (might need 1.0 - y for Vulkan)
                break;
            default: break;
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
}
