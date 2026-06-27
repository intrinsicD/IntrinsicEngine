module;

#include <memory>
#include <glm/glm.hpp>

export module Geometry.MeshClosestFace;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

// Geometry.MeshClosestFace — accelerated exact nearest-face query over a
// triangle/polygon mesh. Builds a BVH over per-face AABBs and resolves the
// closest FaceHandle + closest point using exact point-to-triangle distance
// (faces are fan-triangulated), with branch-and-bound pruning. Replaces the
// brute-force ForEachFace scans previously used by simplification, the implicit
// field oracle, and adaptive remeshing.
//
// Fail-closed (GEOM-005/007): querying an unbuilt index, or a mesh with no
// usable faces, returns Found=false; a non-finite probe returns Found=false.
export namespace Geometry
{
    struct MeshClosestFaceResult
    {
        bool Found{false};
        FaceHandle Face{};
        glm::vec3 Point{0.0f};
        float SquaredDistance{0.0f};
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

        [[nodiscard]] bool IsBuilt() const noexcept;
        [[nodiscard]] std::size_t FaceCount() const noexcept;

        // Exact nearest face to a world-space point.
        [[nodiscard]] MeshClosestFaceResult Query(const glm::vec3& point) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
