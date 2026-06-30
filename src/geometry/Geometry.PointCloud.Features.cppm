module;

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

export module Geometry.PointCloud.Features;

import Geometry.PointCloud;

export namespace Geometry::PointCloud::Features
{
    // =========================================================================
    // Point-cloud keypoint / descriptor / correspondence / coarse-registration
    // seams (GEOM-017)
    // =========================================================================
    //
    // Generic, paper-neutral CPU contracts for robust point-cloud registration.
    // This module owns the reusable pieces that initialize the existing
    // `Geometry.Registration` ICP path and feed later paper-specific robust /
    // global registration method packages under `methods/geometry`:
    //
    //   keypoints  -> descriptors -> correspondences -> coarse alignment -> ICP
    //
    // The default keypoint family is ISS (Intrinsic Shape Signatures, Zhong
    // 2009) saliency; the default descriptor family is FPFH (Fast Point Feature
    // Histograms, Rusu et al. 2009, 33-D). Both are deterministic, allocation-
    // bounded, and `geometry -> core` only. No Eigen types cross this interface.
    //
    // Preconditions are explicit: descriptor computation requires the cloud to
    // carry normals (`Cloud::HasNormals()`); generate them first with
    // `Geometry::PointCloud::Normals`. Functions fail closed (return `nullopt` /
    // a non-`Success` status) on unmet preconditions rather than fabricating
    // data.

    // ------------------------------------------------------------------ Keypoints

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
        // Point indices into the source cloud, sorted ascending (deterministic).
        std::vector<std::uint32_t> Indices;

        // Per-keypoint saliency (the smallest covariance eigenvalue lambda3),
        // aligned with Indices.
        std::vector<double> Saliency;
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

    struct DescriptorSet
    {
        DescriptorKind Kind{DescriptorKind::FPFH};
        std::uint32_t Dimension{0};
        std::uint32_t Count{0};

        // Row-major Count x Dimension histogram data.
        std::vector<float> Data;

        // Point index each descriptor row was computed at (aligned with rows).
        std::vector<std::uint32_t> SourceIndices;

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

    // Mean nearest-neighbor spacing of a cloud; the auto-radius reference scale.
    // Returns nullopt for fewer than two finite points.
    [[nodiscard]] std::optional<float> EstimateSpacing(const Cloud& cloud);

    // ISS keypoint detection. Returns nullopt for empty/degenerate input.
    [[nodiscard]] std::optional<KeypointSet> DetectKeypoints(
        const Cloud& cloud, const KeypointParams& params = {});

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
