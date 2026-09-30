// Mesh vertex normals from weighted incident-face contributions.
module;

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMesh.Vertices.Normals;

export import Geometry.NormalEstimation.Types;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

export namespace Geometry::HalfedgeMesh::VertexNormals
{
    inline constexpr std::string_view kDefaultOutputProperty = "v:normal";

    enum class RecomputeStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        InvalidOutputProperty,
        PropertyTypeConflict,
    };

    struct Params
    {
        AveragingMode Weighting{AveragingMode::AreaWeighted};
        std::string_view OutputProperty{kDefaultOutputProperty};
        glm::vec3 FallbackNormal{0.0f, 1.0f, 0.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
        bool SkipDeleted{true};
    };

    struct Result
    {
        RecomputeStatus Status{RecomputeStatus::Success};
        AveragingMode Weighting{AveragingMode::AreaWeighted};
        VertexProperty<glm::vec3> Normals{};

        std::size_t VertexSlotCount{0};
        std::size_t WrittenCount{0};
        std::size_t ValidNormalVertexCount{0};
        std::size_t ProcessedFaceCount{0};
        std::size_t DegenerateFaceCount{0};
        std::size_t NonFiniteFaceCount{0};
        std::size_t InvalidTopologyFaceCount{0};
        std::size_t DegenerateCornerCount{0};
        std::size_t FallbackVertexCount{0};
        std::size_t SkippedDeletedFaceCount{0};
        std::size_t SkippedDeletedVertexCount{0};
        bool FallbackNormalWasRepaired{false};
    };

    [[nodiscard]] std::string_view DebugName(AveragingMode mode) noexcept;
    [[nodiscard]] std::string_view DebugName(RecomputeStatus status) noexcept;

    [[nodiscard]] Result Recompute(Mesh& mesh,
                                   const Params& params = {});

    // The face rings Recompute walks, in its corner order, as CSR: face f's corner vertices
    // are Corners[FaceOffsets[f] .. FaceOffsets[f + 1]). A face Recompute skips for its
    // topology (deleted, a deleted halfedge or vertex on the ring, a broken ring, under three
    // corners) has an empty range. Corner positions are not inspected here; a backend that
    // gathers from this table applies the reference's position rules itself.
    struct FaceCornerTable
    {
        std::vector<std::uint32_t> FaceOffsets{};
        std::vector<std::uint32_t> Corners{};
        std::size_t SkippedDeletedFaceCount{0};
        std::size_t InvalidTopologyFaceCount{0};
        std::size_t DegenerateFaceCount{0};
    };
    [[nodiscard]] FaceCornerTable GatherFaceCornerTable(const Mesh& mesh, bool skipDeleted = true);

    // The fallback normal Recompute writes: `params.FallbackNormal` normalized in float, or
    // +Y (`repaired`) when it is not finite or shorter than the epsilon.
    [[nodiscard]] glm::vec3 ResolveFallbackNormal(const Params& params, bool& repaired) noexcept;
} // namespace Geometry::HalfedgeMesh::VertexNormals
