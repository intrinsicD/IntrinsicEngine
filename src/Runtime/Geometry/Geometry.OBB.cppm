module;

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <span>
#include <array>

export module Runtime.Geometry.OBB;

export namespace Runtime::Geometry
{
    struct OBB
    {
        glm::vec3 Center{0.0f};
        glm::vec3 Extents{1.0f}; // Half-extents
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};

        [[nodiscard]] bool IsValid() const
        {
            return Extents.x > 0.0f && Extents.y > 0.0f && Extents.z > 0.0f;
        }

        [[nodiscard]] glm::vec3 GetSize() const
        {
            return 2.0f * Extents;
        }

        [[nodiscard]] float GetSurfaceArea() const
        {
            const glm::vec3 size = GetSize();
            return 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
        }

        [[nodiscard]] float GetVolume() const
        {
            const glm::vec3 size = GetSize();
            return size.x * size.y * size.z;
        }

        [[nodiscard]] float GetLongestAxisLength() const
        {
            const glm::vec3 size = GetSize();
            return glm::compMax(size);
        }

        // Get the 8 corners in World Space
        [[nodiscard]] std::array<glm::vec3, 8> GetCorners() const
        {
            std::array<glm::vec3, 8> corners;
            glm::vec3 x = glm::rotate(Rotation, glm::vec3(Extents.x, 0, 0));
            glm::vec3 y = glm::rotate(Rotation, glm::vec3(0, Extents.y, 0));
            glm::vec3 z = glm::rotate(Rotation, glm::vec3(0, 0, Extents.z));

            corners[0] = Center + x + y + z;
            corners[1] = Center + x + y - z;
            corners[2] = Center + x - y + z;
            corners[3] = Center + x - y - z;
            corners[4] = Center - x + y + z;
            corners[5] = Center - x + y - z;
            corners[6] = Center - x - y + z;
            corners[7] = Center - x - y - z;
            return corners;
        }

        // Transform a point into the Local Space of this OBB
        // Returns position relative to Center, aligned with Axes
        [[nodiscard]] glm::vec3 ToLocal(const glm::vec3& worldPos) const
        {
            return glm::conjugate(Rotation) * (worldPos - Center);
        }

        // Transform a local point back to World Space
        [[nodiscard]] glm::vec3 ToWorld(const glm::vec3& localPos) const
        {
            return Center + (Rotation * localPos);
        }
    };

    OBB Union(const OBB& obb, const glm::vec3& point)
    {
        // 1. Transform point to local space
        glm::vec3 localP = obb.ToLocal(point);

        // 2. Compute local AABB union
        glm::vec3 minLocal = -obb.Extents;
        glm::vec3 maxLocal = obb.Extents;

        minLocal = glm::min(minLocal, localP);
        maxLocal = glm::max(maxLocal, localP);

        // 3. Recompute OBB properties
        OBB result;
        result.Rotation = obb.Rotation;
        result.Extents = (maxLocal - minLocal) * 0.5f;

        // The new center in local space is the midpoint of the new bounds
        glm::vec3 localCenterOffset = minLocal + result.Extents;

        // Transform center back to world
        result.Center = obb.ToWorld(localCenterOffset);

        return result;
    }

    OBB Union(const OBB& a, const OBB& b)
    {
        if (!b.IsValid()) return a;
        if (!a.IsValid()) return b;

        // Start with A's bounds in A's space
        glm::vec3 minLocal = -a.Extents;
        glm::vec3 maxLocal = a.Extents;

        // Project all 8 corners of B into A's local space
        auto bCorners = b.GetCorners();
        for (const auto& p : bCorners)
        {
            glm::vec3 localP = a.ToLocal(p);
            minLocal = glm::min(minLocal, localP);
            maxLocal = glm::max(maxLocal, localP);
        }

        OBB result;
        result.Rotation = a.Rotation; // Inherit A's rotation
        result.Extents = (maxLocal - minLocal) * 0.5f;

        glm::vec3 localCenterOffset = minLocal + result.Extents;
        result.Center = a.ToWorld(localCenterOffset);

        return result;
    }

    OBB Union(std::span<const OBB> obbs)
    {
        if (obbs.empty()) return OBB{};
        if (obbs.size() == 1) return obbs[0];

        // Use the first OBB as the "Seed" for orientation
        OBB result = obbs[0];

        // Accumulate the rest
        // Note: For large sets, calculating the Covariance Matrix (PCA) of all centers
        // is better to find the optimal axis, but that is O(N) and expensive.
        // This is the greedy O(N) approach suitable for runtime updates.
        for (size_t i = 1; i < obbs.size(); ++i)
        {
            result = Union(result, obbs[i]);
        }
        return result;
    }

    double Volume(const OBB& obb)
    {
        return obb.GetVolume();
    }

    double Distance(const OBB& a, const glm::vec3& p)
    {
        glm::vec3 localP = a.ToLocal(p);
        glm::vec3 d = glm::abs(localP) - a.Extents;
        return glm::length(glm::max(d, glm::vec3(0.0))) + glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
    }

    double SquaredDistance(const OBB& a, const glm::vec3& p)
    {
        glm::vec3 localP = a.ToLocal(p);
        glm::vec3 d = glm::abs(localP) - a.Extents;
        // Clamp to 0 (only care about outside distance for squared)
        d = glm::max(d, glm::vec3(0.0f));
        return glm::dot(d, d);
    }
}
