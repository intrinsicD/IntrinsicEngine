// Point-set keypoints and point-cloud descriptor/registration kernels with explicit numerical outputs.
module;

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

export module Geometry.PointCloud.Features;

import Geometry.PointCloud;
export import Geometry.SpatialQueries;

export namespace Geometry::PointCloud::Features
{
    struct KeypointParams
    {
        // Neighborhood radius for the saliency covariance. <= 0 selects a radius
        // from the mean nearest-neighbor spacing.
        float SalientRadius{0.0f};

        // Non-maximum-suppression radius. <= 0 selects from spacing.
        float NonMaxRadius{0.0f};

        // ISS eigenvalue-ratio gates (lambda are sorted descending). A point is
        // salient only when lambda2/lambda1 <= Gamma21 and lambda3/lambda2 <= Gamma32.
        double Gamma21{0.975};
        double Gamma32{0.975};

        // Minimum neighbors required to accept a saliency estimate.
        std::uint32_t MinNeighbors{5};
    };

    struct KeypointSet
    {
        // Input-span indices, or original Cloud slots, sorted ascending.
        std::vector<std::uint32_t> Indices;

        // Per-keypoint saliency (the smallest covariance eigenvalue lambda3),
        // aligned with Indices.
        std::vector<double> Saliency;
    };


    struct KeypointScale
    {
        float MeanSpacing{}, SalientRadius{}, NonMaxRadius{};
    };
    struct KeypointAnalysis
    {
        KeypointSet Keypoints{};
        // Pre-NMS candidate scores; rejected candidates are zero. Mask marks
        // only retained keypoints, including a valid zero-saliency maximum.
        std::vector<float> Saliency{};
        std::vector<std::uint32_t> Mask{};
        KeypointScale Scale{};
    };

    // ---------------------------------------------------------------- Descriptors

    enum class DescriptorKind : std::uint8_t
    {
        FPFH
    };

    struct DescriptorParams
    {
        DescriptorKind Kind{DescriptorKind::FPFH};

        // Neighborhood radius for the histogram. <= 0 selects from spacing.
        float FeatureRadius{0.0f};

        // Optional cap on neighbors per point (0 = all within radius). Caps are
        // applied by ascending point index for determinism.
        std::uint32_t MaxNeighbors{0};
    };

    struct DescriptorScale
    {
        float MeanSpacing{}, FeatureRadius{};
    };

    struct DescriptorSet
    {
        DescriptorKind Kind{DescriptorKind::FPFH};
        std::uint32_t Dimension{0};
        std::uint32_t Count{0};

        // Row-major Count x Dimension histogram data.
        std::vector<float> Data;

        // Point index each descriptor row was computed at (aligned with rows).
        std::vector<std::uint32_t> SourceIndices;
        DescriptorScale Scale{};

        [[nodiscard]] std::span<const float> Row(std::uint32_t i) const
        {
            if (Dimension == 0 || i >= Count)
            {
                return {};
            }
            const std::size_t offset = static_cast<std::size_t>(i) * Dimension;
            return std::span<const float>(Data.data() + offset, Dimension);
        }
    };

    // -------------------------------------------------------------- Correspondences

    struct Correspondence
    {
        // Row indices into the source / target DescriptorSet (NOT raw point
        // indices); map back through DescriptorSet::SourceIndices.
        std::uint32_t SourceRow{0};
        std::uint32_t TargetRow{0};
        float Distance{0.0f}; // L2 descriptor distance
    };

    struct CorrespondenceParams
    {
        // Require reciprocal nearest neighbors (source->target and target->source).
        bool MutualBest{true};

        // Lowe ratio test: keep a match only if best/second-best < MaxRatio.
        // <= 0 disables the test.
        float MaxRatio{0.0f};
    };

    struct CorrespondenceSet
    {
        std::vector<Correspondence> Pairs;
    };

    // ------------------------------------------------------------ Coarse alignment

    enum class CoarseAlignmentStatus : std::uint8_t
    {
        Success,
        InsufficientCorrespondences,
        NoConsensus,
        DegenerateInput
    };

    struct CoarseAlignmentParams
    {
        // RANSAC iteration budget and minimal sample size.
        std::uint32_t MaxIterations{4000};
        std::uint32_t SampleSize{3};

        // Inlier distance threshold after applying a candidate transform.
        // <= 0 selects from spacing.
        float InlierThreshold{0.0f};

        // Reject geometrically inconsistent sample triplets whose pairwise
        // source/target edge-length ratios fall below this similarity.
        double EdgeLengthSimilarity{0.9};

        // Deterministic seed for the internal sampler (no std::random).
        std::uint64_t Seed{0x9E3779B97F4A7C15ull};
    };

    struct CoarseAlignmentResult
    {
        // Rigid transform aligning source keypoints to target keypoints; apply
        // as aligned = Transform * vec4(source, 1).
        glm::dmat4 Transform{1.0};
        CoarseAlignmentStatus Status{CoarseAlignmentStatus::DegenerateInput};
        std::uint32_t InlierCount{0};
        double InlierRmse{0.0};
        std::uint32_t IterationsUsed{0};
    };

    // ----------------------------------------------------------------------- API

    [[nodiscard]] std::optional<float> EstimateSpacing(std::span<const glm::vec3> positions);
    // Positive nearest-other spacing is required even with manual radii.
    [[nodiscard]] std::optional<KeypointScale> ResolveKeypointScale(
        std::span<const glm::vec3> positions, const KeypointParams& params = {});
    [[nodiscard]] std::optional<KeypointAnalysis> AnalyzeKeypoints(
        std::span<const glm::vec3> positions, const KeypointParams& params = {});
    // Scale must come from ResolveKeypointScale for these inputs/parameters.
    // Supply complete max(salient,NMS) radius rows: ascending unique nonself IDs.
    // Membership/order/shape are checked; the caller guarantees completeness.
    [[nodiscard]] std::optional<KeypointAnalysis> AnalyzeKeypointsFromNeighbors(
        std::span<const glm::vec3> positions, const KeypointParams& params,
        const KeypointScale& scale, Geometry::PointNeighborhoods neighborhoods);

    // Mean nearest-neighbor spacing of a cloud; the auto-radius reference scale.
    // Returns nullopt for fewer than two finite points.
    [[nodiscard]] std::optional<float> EstimateSpacing(const Cloud& cloud);

    // ISS keypoint detection. Returns nullopt for empty/degenerate input.
    [[nodiscard]] std::optional<KeypointSet> DetectKeypoints(
        const Cloud& cloud, const KeypointParams& params = {});

    [[nodiscard]] std::optional<DescriptorScale> ResolveDescriptorScale(
        std::span<const glm::vec3> positions, const DescriptorParams& params = {});
    // Count-matched finite positions and finite nonzero normals. Empty indices
    // selects all samples; explicit order and repeated indices are preserved.
    [[nodiscard]] std::optional<DescriptorSet> ComputeDescriptors(
        std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
        std::span<const std::uint32_t> indices = {}, const DescriptorParams& params = {});
    // Scale must be resolved for these inputs. Supply complete inclusive-radius
    // rows or, when MaxNeighbors>0, their exact lowest-ID prefixes up to that cap.
    // IDs are sorted, unique and nonself. Shape/membership are checked; the
    // caller guarantees completeness or that every required prefix ID is present.
    [[nodiscard]] std::optional<DescriptorSet> ComputeDescriptorsFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
        std::span<const std::uint32_t> indices, const DescriptorParams& params,
        const DescriptorScale& scale, Geometry::PointNeighborhoods neighborhoods);

    // FPFH descriptors at the given point indices (empty = all points).
    // Precondition: cloud.HasNormals(). Returns nullopt otherwise.
    [[nodiscard]] std::optional<DescriptorSet> ComputeDescriptors(
        const Cloud& cloud,
        std::span<const std::uint32_t> indices,
        const DescriptorParams& params = {});

    // Brute-force descriptor matching with deterministic lower-index tie-breaks.
    [[nodiscard]] std::optional<CorrespondenceSet> MatchDescriptors(
        const DescriptorSet& source,
        const DescriptorSet& target,
        const CorrespondenceParams& params = {});

    // RANSAC coarse alignment over feature correspondences. sourcePoints /
    // targetPoints are indexed by the DescriptorSet rows referenced in the
    // correspondences (pass the descriptor SourceIndices' positions).
    [[nodiscard]] CoarseAlignmentResult EstimateCoarseAlignment(
        std::span<const glm::vec3> sourcePoints,
        std::span<const glm::vec3> targetPoints,
        const CorrespondenceSet& correspondences,
        const CoarseAlignmentParams& params = {});
}
