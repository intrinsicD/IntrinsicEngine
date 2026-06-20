module;

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMesh.Vertices.Normals;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

export namespace Geometry::HalfedgeMesh::VertexNormals
{
    inline constexpr std::string_view kDefaultOutputProperty = "v:normal";

    enum class AveragingMode : std::uint8_t
    {
        UniformFace,
        AreaWeighted,
        AngleWeighted,
        MaxWeighted,
    };

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
} // namespace Geometry::HalfedgeMesh::VertexNormals
