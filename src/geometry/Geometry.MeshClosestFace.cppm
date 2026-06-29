module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <glm/glm.hpp>

export module Geometry.MeshClosestFace;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.SpatialQueries;

// Geometry.MeshClosestFace — accelerated exact nearest-face query over a
// triangle/polygon mesh. Builds a BVH over per-face AABBs and resolves exact
// point-to-triangle distances (polygon faces are fan-triangulated), with
// branch-and-bound pruning. Replaces brute-force nearest-face scans in geometry
// consumers while preserving CPU-only fail-closed behavior.
//
// Fail-closed (GEOM-005/007): querying an unbuilt index, a mesh with no usable
// faces, or a non-finite probe returns explicit status and no NaNs.
export namespace Geometry
{
    enum class MeshClosestFaceStatus : std::uint8_t
    {
        Success,
        UnbuiltIndex,
        EmptyIndex,
        InvalidQueryPoint,
        InvalidParameter,
    };

    [[nodiscard]] const char* DebugName(MeshClosestFaceStatus status) noexcept;

    struct MeshClosestFaceHit
    {
        FaceHandle Face{};
        glm::vec3 Point{0.0f};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        float SquaredDistance{0.0f};
        std::uint32_t PrimitiveIndex{0u};
    };

    struct MeshClosestFaceResult
    {
        bool Found{false};
        FaceHandle Face{};
        glm::vec3 Point{0.0f};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        float SquaredDistance{0.0f};
        std::uint32_t PrimitiveIndex{0u};
        MeshClosestFaceStatus Status{MeshClosestFaceStatus::UnbuiltIndex};
        SpatialKNNResult Diagnostics{};
    };

    struct MeshClosestFaceKNearestResult
    {
        std::vector<MeshClosestFaceHit> Hits{};
        MeshClosestFaceStatus Status{MeshClosestFaceStatus::UnbuiltIndex};
        SpatialKNNResult Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == MeshClosestFaceStatus::Success;
        }
    };

    struct MeshClosestFaceRadiusResult
    {
        std::vector<MeshClosestFaceHit> Hits{};
        MeshClosestFaceStatus Status{MeshClosestFaceStatus::UnbuiltIndex};
        SpatialRadiusResult Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == MeshClosestFaceStatus::Success;
        }
    };

    class MeshClosestFaceIndex
    {
    public:
        MeshClosestFaceIndex();
        ~MeshClosestFaceIndex();
        MeshClosestFaceIndex(MeshClosestFaceIndex&&) noexcept;
        MeshClosestFaceIndex& operator=(MeshClosestFaceIndex&&) noexcept;
        MeshClosestFaceIndex(const MeshClosestFaceIndex&) = delete;
        MeshClosestFaceIndex& operator=(const MeshClosestFaceIndex&) = delete;

        // Build (or rebuild) the index over the mesh's non-deleted faces.
        // Returns false (and leaves the index empty) when there are no usable
        // faces.
        bool Build(const HalfedgeMesh::Mesh& mesh);
        bool Build(const HalfedgeMesh::Mesh& mesh,
                   std::span<const FaceHandle> faces);

        [[nodiscard]] bool IsBuilt() const noexcept;
        [[nodiscard]] std::size_t FaceCount() const noexcept;

        // Exact nearest face to a world-space point.
        [[nodiscard]] MeshClosestFaceResult Query(const glm::vec3& point) const;
        [[nodiscard]] MeshClosestFaceKNearestResult QueryKNearest(
            const glm::vec3& point,
            std::size_t k) const;
        [[nodiscard]] MeshClosestFaceRadiusResult QueryRadius(
            const glm::vec3& point,
            float radius) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
