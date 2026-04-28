module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.ImplicitPlaneField;

import Geometry.AABB;
import Geometry.Grid;
import Geometry.HalfedgeMesh;
import Geometry.MarchingCubes;
import Geometry.Octree;
import Geometry.Properties;

export namespace Geometry::Implicit
{
    enum class PlaneFieldNodeFlags : std::uint32_t
    {
        None                  = 0u,
        Active                = 1u << 0u,
        Ambiguous             = 1u << 1u,
        SharpFeatureCandidate = 1u << 2u,
        MultiSheetCandidate   = 1u << 3u
    };

    [[nodiscard]] constexpr PlaneFieldNodeFlags operator|(PlaneFieldNodeFlags a, PlaneFieldNodeFlags b) noexcept
    {
        return static_cast<PlaneFieldNodeFlags>(
            static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    [[nodiscard]] constexpr PlaneFieldNodeFlags operator&(PlaneFieldNodeFlags a, PlaneFieldNodeFlags b) noexcept
    {
        return static_cast<PlaneFieldNodeFlags>(
            static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }

    [[nodiscard]] constexpr bool HasAnyFlag(PlaneFieldNodeFlags value, PlaneFieldNodeFlags mask) noexcept
    {
        return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(mask)) != 0u;
    }

    struct BuildParams
    {
        std::size_t MaxDepth{8};
        std::size_t MinDepth{0};

        float BoundingBoxPadding{0.05f};
        float NarrowBandFactor{3.0f};
        float SupportRadiusFactor{1.5f};

        float MaxPlaneErrorFraction{0.125f};
        float MaxNormalDeviationDegrees{12.5f};
        float BlendNormalCompatibilityDegrees{35.0f};

        bool DetectAmbiguity{true};
        bool StoreSourcePrimitive{true};
    };

    enum class BuildStatus : std::uint32_t
    {
        Success = 0,
        EmptyInput,
        InvalidInput,
        MaxDepthReached
    };

    struct BuildStats
    {
        std::size_t NodeCount{0};
        std::size_t ActiveLeafCount{0};
        std::size_t AmbiguousLeafCount{0};
        std::size_t MaxReachedDepth{0};
    };

    class PlaneField
    {
    public:
        using NodeIndex = Octree::NodeIndex;

        PlaneField() = default;

        [[nodiscard]] const Octree& Hierarchy() const noexcept { return m_Tree; }
        [[nodiscard]] Octree& Hierarchy() noexcept { return m_Tree; }

        [[nodiscard]] float BlendNormalCompatibilityDot() const noexcept { return m_BlendNormalCompatibilityDot; }
        void SetBlendNormalCompatibilityDot(float value) noexcept { m_BlendNormalCompatibilityDot = value; }

        [[nodiscard]] bool IsActive(NodeIndex nodeIndex) const;
        [[nodiscard]] bool IsAmbiguous(NodeIndex nodeIndex) const;

        [[nodiscard]] std::optional<glm::vec3> ClosestPoint(NodeIndex nodeIndex) const;
        [[nodiscard]] std::optional<glm::vec3> Normal(NodeIndex nodeIndex) const;
        [[nodiscard]] std::optional<float> SignedDistance(NodeIndex nodeIndex) const;
        [[nodiscard]] std::optional<float> MaxPlaneError(NodeIndex nodeIndex) const;
        [[nodiscard]] std::optional<float> SupportRadius(NodeIndex nodeIndex) const;

        [[nodiscard]] std::vector<NodeIndex> CollectInfluencingLeaves(const glm::vec3& worldPoint) const;
        [[nodiscard]] std::optional<float> Evaluate(const glm::vec3& worldPoint) const;
        [[nodiscard]] std::optional<glm::vec3> Project(
            const glm::vec3& worldPoint,
            std::size_t maxIterations = 4u,
            float convergenceEpsilon = 1.0e-4f) const;

    private:
        Octree m_Tree;
        float m_BlendNormalCompatibilityDot{0.81915204428f}; // cos(35 deg)
    };

    struct BuildResult
    {
        PlaneField Field;
        BuildStats Stats;
        BuildStatus Status{BuildStatus::Success};
    };

    [[nodiscard]] std::optional<BuildResult> BuildPlaneField(
        const Halfedge::Mesh& mesh,
        const BuildParams& params = {});

    [[nodiscard]] std::optional<Grid::DenseGrid> SampleToDenseGrid(
        const PlaneField& field,
        const Grid::GridDimensions& dims,
        std::string_view scalarPropertyName = "scalar");

    [[nodiscard]] std::optional<Halfedge::Mesh> ExtractMesh(
        const PlaneField& field,
        const Grid::GridDimensions& dims,
        const MarchingCubes::MarchingCubesParams& mcParams = {},
        std::string_view scalarPropertyName = "scalar");
}
