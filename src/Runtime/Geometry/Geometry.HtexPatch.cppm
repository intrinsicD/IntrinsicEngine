module;

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:HtexPatch;

import :HalfedgeMesh;

export namespace Geometry::HtexPatch
{
    inline constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    enum PatchFlags : std::uint16_t
    {
        Boundary = 1u << 0,
        NonManifold = 1u << 1
    };

    struct PatchBuildParams
    {
        float TexelsPerUnit = 32.0f;
        std::uint16_t MinResolution = 8u;
        std::uint16_t MaxResolution = 128u;
    };

    struct HalfedgePatchMeta
    {
        std::uint32_t EdgeIndex = 0u;
        std::uint32_t Halfedge0Index = kInvalidIndex;
        std::uint32_t Halfedge1Index = kInvalidIndex;
        std::uint32_t Face0Index = kInvalidIndex;
        std::uint32_t Face1Index = kInvalidIndex;
        std::uint32_t LayerIndex = 0u;
        std::uint16_t Resolution = 8u;
        std::uint16_t Flags = 0u;
    };

    struct PatchBuildResult
    {
        std::vector<HalfedgePatchMeta> Patches{};
        std::uint32_t BoundaryPatchCount = 0u;
        std::uint32_t InteriorPatchCount = 0u;
        std::uint16_t MaxAssignedResolution = 0u;
    };

    [[nodiscard]] std::optional<PatchBuildResult> BuildPatchMetadata(
        const Halfedge::Mesh& mesh,
        const PatchBuildParams& params = {});

    [[nodiscard]] glm::vec2 TriangleToPatchUV(
        std::uint32_t halfedgeIndex,
        glm::vec2 localUV,
        std::uint32_t twinIndex) noexcept;

    [[nodiscard]] glm::vec2 PatchToTriangleUV(
        std::uint32_t halfedgeIndex,
        glm::vec2 patchUV,
        std::uint32_t twinIndex) noexcept;

    [[nodiscard]] bool IsTriangleLocalUV(glm::vec2 localUV, float epsilon = 1.0e-6f) noexcept;
}
