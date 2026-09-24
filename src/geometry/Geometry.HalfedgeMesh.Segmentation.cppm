// Exposes bounded face-feature GMM segmentation guided by any floating-point
// vertex/face fields, plus the curvature preset, so geometry and runtime share
// deterministic labels and diagnostics.
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMesh.Segmentation;

export import Geometry.Segmentation.Diagnostics;

import Geometry.HalfedgeMesh;

export namespace Geometry::Segmentation
{
    inline constexpr std::uint32_t kInvalidLabel =
        std::numeric_limits<std::uint32_t>::max();

    enum class ComponentSelectionMode : std::uint8_t
    {
        FixedCount = 0,
        Automatic,
    };

    struct SegmentationParams
    {
        ComponentSelectionMode SelectionMode{
            ComponentSelectionMode::Automatic};

        // FixedCount fits exactly this many Gaussian components. Spatial
        // regularization may leave a statistically redundant component with
        // no winning faces; diagnostics distinguish fitted and active counts.
        std::uint32_t FixedComponentCount{6u};

        // Automatic mode evaluates every count in this inclusive range. A
        // candidate satisfying AutomaticFitTolerance is preferred; weighted
        // BIC resolves model complexity within the qualifying set (or across
        // all candidates when no count reaches the requested fit).
        std::uint32_t AutomaticMinComponents{1u};
        std::uint32_t AutomaticMaxComponents{12u};
        double AutomaticFitTolerance{0.35};
        double AutomaticComplexityWeight{1.0};

        std::uint32_t MaxEmIterations{100u};
        double EmRelativeTolerance{1.0e-6};
        double CovarianceFloor{1.0e-5};
        std::uint32_t Seed{42u};

        // Potts regularization on the face-dual graph. SpatialWeight is the
        // pairwise/data balance. FeatureSensitivity makes cuts progressively
        // cheaper across signed-curvature jumps and face-normal bends.
        double SpatialWeight{0.75};
        double FeatureSensitivity{4.0};
        std::uint32_t MaxSpatialIterations{24u};

        // Connected regions smaller than this face count are greedily merged
        // into the lowest-energy adjacent component. One disables cleanup.
        std::uint32_t MinimumRegionFaces{2u};
    };

    [[nodiscard]] bool IsValidMixtureParams(const SegmentationParams& params) noexcept;
    [[nodiscard]] bool IsValidSegmentationParams(const SegmentationParams& params) noexcept;

    struct FeatureNormalization
    {
        double Center{0.0};
        double Scale{1.0};
    };

    // Called after finite face-sample preflight. Keeps the curvature median and
    // MAD-to-RMS-to-unit fallback policy, including the empty-input defaults.
    [[nodiscard]] FeatureNormalization ComputeFeatureNormalization(
        std::span<const double> values);

    struct SegmentationResult
    {
        // Slot-aligned outputs. Deleted face slots retain kInvalidLabel;
        // deleted/non-boundary edge slots retain zero/transparent values.
        std::vector<std::uint32_t> FaceComponents{};
        std::vector<std::uint32_t> FaceRegions{};
        std::vector<std::uint8_t> EdgeBoundaries{};
        std::vector<glm::vec4> FaceRegionColors{};
        std::vector<glm::vec4> EdgeBoundaryColors{};
        SegmentationDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Diagnostics.Succeeded();
        }
    };

    [[nodiscard]] const char* ToString(
        ComponentSelectionMode mode) noexcept;
    [[nodiscard]] const char* ToString(
        SegmentationStatus status) noexcept;

    // Features are face-slot aligned and dimension selects the active leading
    // channels (1..3). Inactive packed channels and deleted-face slots are
    // ignored. The kernel does not mutate the mesh.
    [[nodiscard]] SegmentationResult SegmentFaceFeatures(
        const HalfedgeMesh::Mesh& mesh,
        std::span<const glm::dvec3> faceFeatures,
        std::uint32_t dimension,
        const SegmentationParams& params = {});

    enum class GuideDomain : std::uint8_t
    {
        Vertex = 0,
        Face,
    };

    // One scalar guide channel aligned to vertex or face storage slots.
    // Vertex channels are averaged over each triangle's corners.
    struct Guide
    {
        GuideDomain Domain{GuideDomain::Vertex};
        std::span<const double> Values{};
    };

    // A named mesh property used as guide channels: float and double give one
    // channel, glm::vec2/glm::vec3 give one channel per component.
    struct GuideProperty
    {
        GuideDomain Domain{GuideDomain::Vertex};
        std::string_view Name{};
    };

    // Segment a triangle mesh from 1..3 guide channels, in order. Wrong-sized
    // channels fail with FeatureCountMismatch; missing or non-floating-point
    // properties fail with MissingGuideProperty. The mesh is not mutated.
    [[nodiscard]] SegmentationResult Segment(
        const HalfedgeMesh::Mesh& mesh,
        std::span<const Guide> guides,
        const SegmentationParams& params = {});
    [[nodiscard]] SegmentationResult Segment(
        const HalfedgeMesh::Mesh& mesh,
        std::span<const GuideProperty> guides,
        const SegmentationParams& params = {});

    // Curvature preset over slot-aligned signed principal curvatures:
    // maxPrincipal[i] is k1 and minPrincipal[i] is k2, with k1 >= k2 under the
    // caller's orientation convention, as two vertex guides.
    [[nodiscard]] SegmentationResult SegmentCurvature(
        const HalfedgeMesh::Mesh& mesh,
        std::span<const double> maxPrincipal,
        std::span<const double> minPrincipal,
        const SegmentationParams& params = {});

    // Convenience path using Geometry.Curvature's signed per-vertex estimate.
    // Curvature properties are materialized only on the supplied mesh object;
    // runtime calls this on its detached snapshot and publishes selected output
    // properties explicitly after the solve.
    [[nodiscard]] SegmentationResult ComputeAndSegment(
        HalfedgeMesh::Mesh& mesh,
        const SegmentationParams& params = {});
}
