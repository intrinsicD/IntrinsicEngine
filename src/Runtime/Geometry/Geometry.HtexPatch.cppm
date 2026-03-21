module;

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.HtexPatch;

import Geometry.HalfedgeMesh;
import Geometry.Properties;

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

    struct PatchAtlasLayout
    {
        std::uint32_t TileSize = 16u;
        std::uint32_t Columns = 1u;
        std::uint32_t Rows = 1u;
        std::uint32_t Width = 1u;
        std::uint32_t Height = 1u;
    };

    [[nodiscard]] std::optional<PatchBuildResult> BuildPatchMetadata(
        const Halfedge::Mesh& mesh,
        const PatchBuildParams& params = {});

    [[nodiscard]] PatchAtlasLayout ComputeAtlasLayout(
        std::size_t patchCount,
        std::uint32_t tileSize = 16u,
        std::uint32_t maxColumns = 32u) noexcept;

    [[nodiscard]] bool BuildCategoricalPatchAtlas(
        const Halfedge::Mesh& mesh,
        std::span<const HalfedgePatchMeta> patches,
        const Property<std::uint32_t>& labels,
        std::vector<std::uint32_t>& outTexels,
        PatchAtlasLayout& outLayout,
        std::uint32_t invalidValue = kInvalidIndex);

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
