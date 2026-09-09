// Point-set utilities and outlier analysis over typed samples, with owning cloud adapters.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloud.Utils;

export import Geometry.PointCloud;


import Geometry.AABB;
import Geometry.Properties;

export namespace Geometry::PointCloud
{
    // =========================================================================
    // Point Cloud — First-Class Point Cloud Data Structure
    // =========================================================================
    //
    // A structured container for unstructured 3D point sets with optional
    // per-point attributes (normals, colors, radii). Designed for:
    //
    //   - Import from PLY, XYZ, PCD, LAS and other point cloud formats.
    //   - Input to geometry operators (normal estimation, surface reconstruction).
    //   - GPU rendering via PointCloudRenderPass (flat disc splats).
    //   - Spatial queries via Octree/KDTree acceleration.
    //
    // Design:
    //   - Storage via PropertySet (same pattern as HalfedgeMesh / Graph).
    //   - SoA layout for cache-friendly batch processing; each attribute is a
    //     contiguous vector inside the PropertySet registry.
    //   - Positions are mandatory (built-in "v:point" property shared with mesh/graph vertex positions).
    //   - Normals, colors and radii are optional built-in properties;
    //     additional per-point attributes may be added via GetOrAddVertexProperty.
    //   - Follows the Geometry operator pattern: Params/Result structs, optional
    //     return for degenerate input.
    //
    // References:
    //   - Gross & Pfister, "Point-Based Graphics" (Morgan Kaufmann, 2007)
    //   - Botsch et al., "Point-Based Surface Representation" (IEEE CG&A 2005)

    // -------------------------------------------------------------------------
    // Rendering modes (selectable per point cloud entity)
    // -------------------------------------------------------------------------
    enum class RenderMode : uint32_t
    {
        FlatDisc = 0,  // Screen-space constant-size circular splats (camera-facing billboard)
        Surfel   = 1,  // Normal-oriented disc with Lambertian shading
        EWA      = 2,  // Elliptical Weighted Average splatting (Zwicker et al. 2001)
        Sphere   = 3,  // Impostor spheres with gl_FragDepth for correct depth occlusion
    };

    // -------------------------------------------------------------------------
    // Core Point Cloud Data
    // -------------------------------------------------------------------------
    // -------------------------------------------------------------------------
    // Bounding Box
    // -------------------------------------------------------------------------
    [[nodiscard]] AABB ComputeBoundingBox(const Cloud& cloud);

    // -------------------------------------------------------------------------
    // Statistics
    // -------------------------------------------------------------------------
    struct CloudStatistics
    {
        std::size_t PointCount{0};
        AABB        BoundingBox{};
        glm::vec3   Centroid{0.0f};
        float       AverageSpacing{0.0f};    // Mean distance to nearest neighbor.
        float       MinSpacing{0.0f};
        float       MaxSpacing{0.0f};
        float       BoundingBoxDiagonal{0.0f};
    };

    struct StatisticsParams
    {
        std::size_t SpacingSampleCount{0};   // 0 = compute for all points.
        std::size_t OctreeMaxPerNode{32};
        std::size_t OctreeMaxDepth{10};
    };

    // Returns nullopt if cloud is empty.
    [[nodiscard]] std::optional<CloudStatistics> ComputeStatistics(
        const Cloud& cloud,
        const StatisticsParams& params = {});

    [[nodiscard]] std::optional<CloudStatistics> ComputeStatistics(
        std::span<const glm::vec3> positions, const StatisticsParams& params = {});
    // Two nearest candidates per sampled row (none for a singleton). Sample i
    // addresses i*floor(n/sampleCount), preserving StatisticsParams sampling.
    // Sorted by squared distance then ID; caller guarantees nearest membership.
    [[nodiscard]] std::optional<CloudStatistics> ComputeStatisticsFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const std::uint32_t> candidates,
        const StatisticsParams& params = {});

    // -------------------------------------------------------------------------
    // Voxel Grid Downsampling
    // -------------------------------------------------------------------------
    //
    // Spatially uniform downsampling by partitioning space into a regular grid
    // and replacing all points in each cell with their centroid. This produces
    // an approximately uniform point density and reduces point count for LOD.
    //
    // Algorithm:
    //   1. Compute bounding box and quantize each point to a cell index.
    //   2. For each occupied cell, compute the centroid of its points.
    //   3. If normals/colors/radii exist, average them within each cell.
    //
    // Complexity: O(n) expected (hash-based cell lookup).

    struct DownsampleParams
    {
        float VoxelSize{0.01f};        // Cell edge length (world units).
        bool  PreserveNormals{true};   // Average normals in each cell.
        bool  PreserveColors{true};    // Average colors in each cell.
        bool  PreserveRadii{true};     // Average radii in each cell.
    };

    struct DownsampleResult
    {
        Cloud Downsampled;
        std::size_t OriginalCount{0};
        std::size_t ReducedCount{0};
        float       ReductionRatio{0.0f};  // ReducedCount / OriginalCount.
    };

    // Fails closed, returning nullopt without publishing any partial result,
    // when the cloud is empty, when VoxelSize is not finite and strictly
    // positive, or when any point cannot be quantized safely — that is, a
    // non-finite position component, or a floored scaled coordinate outside the
    // range of the `int` cell key. Invalid coordinates are never clamped into a
    // cell.
    //
    // On success, reduced points are emitted in ascending lexicographic order of
    // their cell key: by x first, then y, then z. That order is independent of
    // hash-container iteration, so repeated calls and different builds return
    // byte-identical results. Accumulation within a cell still runs in input
    // order.
    [[nodiscard]] std::optional<DownsampleResult> VoxelDownsample(
        const Cloud& cloud,
        const DownsampleParams& params = {});

    // -------------------------------------------------------------------------
    // Radius Estimation
    // -------------------------------------------------------------------------
    //
    // Heuristic radius in input-coordinate units: mean retained neighbor distance
    // times ScaleFactor. This does not guarantee surface coverage or hole-free rendering.
    // Query min(n,max(k,1)+1), then remove self; coincident peers remain eligible.

    struct RadiusEstimationParams
    {
        std::size_t KNeighbors{6};         // Neighbors for density estimation.
        float       ScaleFactor{1.0f};     // Multiplier on average spacing.
        std::size_t OctreeMaxPerNode{32};
        std::size_t OctreeMaxDepth{10};
    };

    struct RadiusEstimationResult
    {
        std::vector<float> Radii{};
        float AverageRadius{0.0f};
        float MinRadius{0.0f};
        float MaxRadius{0.0f};
        CloudStatistics Statistics{}; // Full nearest-other spacing, bounds and centroid.
    };

    // Reject fewer than two samples, nonfinite inputs/results or negative scale.
    [[nodiscard]] std::optional<RadiusEstimationResult> EstimateRadii(
        const Cloud& cloud,
        const RadiusEstimationParams& params = {});

    [[nodiscard]] std::optional<RadiusEstimationResult> EstimateRadii(
        std::span<const glm::vec3> positions, const RadiusEstimationParams& params = {});
    // Row-major min(n,max(k,1)+1) candidates per input, sorted by squared distance
    // then source ID, including self if selected. Caller guarantees nearest membership.
    [[nodiscard]] std::optional<RadiusEstimationResult> EstimateRadiiFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const std::uint32_t> candidates,
        const RadiusEstimationParams& params = {});

    // -------------------------------------------------------------------------
    // Random Subsampling
    // -------------------------------------------------------------------------
    //
    // Uniform random subsampling with deterministic seed for reproducibility.

    struct SubsampleParams
    {
        std::size_t TargetCount{1000};     // Desired output point count.
        uint32_t    Seed{42};              // RNG seed for reproducibility.
    };

    struct SubsampleResult
    {
        Cloud Subsampled{};
        std::vector<std::size_t> SelectedIndices{};  // Original indices of kept points.
    };

    // Returns nullopt if cloud is empty.
    [[nodiscard]] std::optional<SubsampleResult> RandomSubsample(
        const Cloud& cloud,
        const SubsampleParams& params = {});

    // -------------------------------------------------------------------------
    // Bilateral Filter (Edge-Preserving Smoothing)
    // -------------------------------------------------------------------------
    //
    // Edge-preserving point cloud smoothing using spatial and normal-space
    // Gaussian weighting. Preserves sharp features that uniform Laplacian
    // smoothing destroys.
    //
    // For each point p_i with normal n_i, the filtered position is:
    //
    //   p_i' = p_i + n_i * [ sum_j w_s(||p_j - p_i||) * w_n(1 - n_i·n_j)
    //                         * <p_j - p_i, n_i> ]
    //          / [ sum_j w_s(||p_j - p_i||) * w_n(1 - n_i·n_j) ]
    //
    // where w_s and w_n are Gaussian kernels with spatial and normal bandwidths.
    // Positions move only along the normal direction, preserving tangential
    // geometry.
    //
    // References:
    //   - Fleishman, Drori, Cohen-Or, "Bilateral Mesh Denoising" (SIGGRAPH 2003)
    //   - Zheng, Fu, Au, Tai, "Bilateral Normal Filtering for Mesh Denoising"
    //     (IEEE TVCG 2011)

    struct BilateralFilterParams
    {
        std::size_t KNeighbors{15};        // Neighbors for local averaging.
        float       SpatialSigma{0.0f};    // Spatial Gaussian σ. 0 = auto (2× avg spacing).
        float       NormalSigma{0.25f};    // Normal-space Gaussian σ. Controls feature sensitivity.
        uint32_t    Iterations{1};         // Number of filter passes.
    };

    struct BilateralFilterResult
    {
        std::size_t PointsFiltered{0};
        std::size_t DegenerateNormals{0};  // Points with zero-length normals (skipped).
        float       AverageDisplacement{0.0f};
        float       MaxDisplacement{0.0f};
    };

    // Modifies cloud positions in-place. Requires normals.
    // Returns nullopt if cloud has < 2 points or normals are not enabled.
    [[nodiscard]] std::optional<BilateralFilterResult> BilateralFilter(
        Cloud& cloud,
        const BilateralFilterParams& params = {});

    // -------------------------------------------------------------------------
    // Outlier Probability Estimation
    // -------------------------------------------------------------------------
    //
    // Per-point outlier score based on local density deviation. Points whose
    // mean distance to k nearest neighbors deviates significantly from the
    // neighborhood average are flagged as statistical outliers.
    //
    // Score_i = mean_kNN_dist(i) / mean_j_in_kNN(mean_kNN_dist(j))
    //
    // A score >> 1.0 indicates the point is in a sparse region relative to its
    // neighbors (likely an outlier). A score near 1.0 indicates consistent
    // local density.
    //
    // Reference:
    //   - Breunig, Kriegel, Ng, Sander, "LOF: Identifying Density-Based Local
    //     Outliers" (SIGMOD 2000)

    struct OutlierEstimationParams
    {
        std::size_t KNeighbors{20};        // Neighbors for density estimation.
        float       ScoreThreshold{2.0f};  // Points above this score are flagged.
    };

    struct OutlierEstimationResult
    {
        std::vector<float> Scores;         // Per-point outlier score (≥ 0).
        std::size_t        OutlierCount{0}; // Points with score > threshold.
        float              MeanScore{0.0f};
        float              MaxScore{0.0f};
    };

    // Publishes "p:outlier_score" property on the cloud.
    // Reject fewer than two samples, nonfinite inputs/results or a negative threshold.
    [[nodiscard]] std::optional<OutlierEstimationResult> EstimateOutlierProbability(
        Cloud& cloud,
        const OutlierEstimationParams& params = {});

    // This distance-ratio heuristic is not a calibrated probability or full LOF.
    [[nodiscard]] std::optional<OutlierEstimationResult> EstimateOutlierProbability(
        std::span<const glm::vec3> positions, const OutlierEstimationParams& params = {});

    // Packed rows contain min(n,max(k,2)+1) IDs, sorted by squared distance/ID.
    // Query without self exclusion, then discard the source ID in the reduction.
    [[nodiscard]] std::optional<OutlierEstimationResult> EstimateOutlierProbabilityFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const std::uint32_t> candidates,
        const OutlierEstimationParams& params = {});

    // -------------------------------------------------------------------------
    // Outlier Removal (explicit kept/rejected partitions)
    // -------------------------------------------------------------------------
    //
    // Whereas EstimateOutlierProbability only publishes a per-point score and
    // leaves interpretation to the caller, the removal APIs return an explicit
    // partition into kept/rejected points, an owned filtered cloud carrying the
    // kept points (with their normals/colors/radii), and normalized diagnostics
    // with an explicit status. The input cloud is never mutated.
    //
    // Both APIs produce deterministic output: kept and rejected index lists are
    // sorted ascending by original point index, and the filtered cloud is built
    // in that same ascending order, so identical inputs always yield identical
    // outputs regardless of spatial-acceleration traversal order.
    //
    // References:
    //   - Rusu et al., "Towards 3D Point Cloud Based Object Maps for Household
    //     Environments" (Robotics and Autonomous Systems, 2008) — statistical
    //     outlier removal.
    //   - Rusu & Cousins, "3D is here: Point Cloud Library (PCL)" (ICRA 2011) —
    //     radius outlier removal.

    enum class OutlierRemovalStatus : std::uint8_t
    {
        Success,             // Partition computed (RejectedCount may be 0).
        EmptyInput,          // Cloud has no points.
        InsufficientPoints,  // Too few points to evaluate the neighborhood.
        InvalidParameters,   // KNeighbors == 0, SearchRadius <= 0, etc.
        BuildFailed          // Spatial acceleration structure could not be built.
    };

    // Shared result for both removal operators. The distance-distribution fields
    // are populated by RemoveStatisticalOutliers and left at 0 by
    // RemoveRadiusOutliers (whose decision is neighbor-count based).
    struct OutlierRemovalResult
    {
        OutlierRemovalStatus     Status{OutlierRemovalStatus::Success};
        Cloud                    Filtered{};          // Kept points (owned copy).
        std::vector<std::size_t> KeptIndices{};       // Original indices, ascending.
        std::vector<std::size_t> RejectedIndices{};   // Original indices, ascending.
        std::size_t              OriginalCount{0};
        std::size_t              KeptCount{0};
        std::size_t              RejectedCount{0};
        std::size_t              NonFiniteCount{0};    // Always rejected, included above.

        // Statistical-removal diagnostics (0 for radius removal).
        float MeanDistance{0.0f};       // Global mean of per-point mean-kNN distance.
        float StdDevDistance{0.0f};     // Global std-dev of that distribution.
        float DistanceThreshold{0.0f};  // MeanDistance + StdDevMultiplier*StdDevDistance.
    };

    // Statistical outlier removal: for each point, compute the mean distance to
    // its K nearest neighbors, then reject points whose mean distance exceeds
    // MeanDistance + StdDevMultiplier * StdDevDistance. Points with non-finite
    // positions are always rejected. Returns InsufficientPoints when fewer than
    // KNeighbors + 1 points are available.
    struct StatisticalOutlierRemovalParams
    {
        std::size_t KNeighbors{16};        // Neighbors used per point (excludes self).
        float       StdDevMultiplier{1.0f}; // Higher keeps more points.
        std::size_t OctreeMaxPerNode{32};
        std::size_t OctreeMaxDepth{10};
    };

    [[nodiscard]] OutlierRemovalResult RemoveStatisticalOutliers(
        const Cloud& cloud,
        const StatisticalOutlierRemovalParams& params = {});

    // Radius outlier removal: reject points that have fewer than MinNeighbors
    // other points within SearchRadius. Points with non-finite positions are
    // always rejected. Returns InvalidParameters when SearchRadius <= 0.
    struct RadiusOutlierRemovalParams
    {
        float       SearchRadius{0.0f};   // World-space radius; must be > 0.
        std::size_t MinNeighbors{4};      // Minimum neighbors (excludes self) to keep.
        std::size_t OctreeMaxPerNode{32};
        std::size_t OctreeMaxDepth{10};
    };

    [[nodiscard]] OutlierRemovalResult RemoveRadiusOutliers(
        const Cloud& cloud,
        const RadiusOutlierRemovalParams& params = {});

    // Analysis preserves input cardinality: 1 marks an outlier. Statistical
    // scores are mean neighbor distances; radius scores are counts excluding self.
    struct OutlierAnalysisResult
    {
        OutlierRemovalStatus Status{OutlierRemovalStatus::Success};
        std::vector<std::uint32_t> Mask{};
        std::vector<float> Scores{};
        std::size_t NonFiniteCount{}, RejectedCount{};
        float MeanDistance{}, StdDevDistance{}, DistanceThreshold{};
    };
    [[nodiscard]] OutlierAnalysisResult AnalyzeStatisticalOutliers(
        std::span<const glm::vec3> points, const StatisticalOutlierRemovalParams& params = {});
    [[nodiscard]] OutlierAnalysisResult AnalyzeRadiusOutliers(
        std::span<const glm::vec3> points, const RadiusOutlierRemovalParams& params = {});
    // Query backends supply per-row mean distances or exact radius counts.
    // NaN distance denotes an invalid input row; classification keeps the existing
    // population-variance rule and strict greater-than rejection threshold.
    [[nodiscard]] OutlierAnalysisResult ClassifyStatisticalOutliers(
        std::span<const float> meanDistances, float stdDevMultiplier);
    [[nodiscard]] OutlierAnalysisResult ClassifyRadiusOutliers(
        std::span<const std::uint32_t> counts, std::uint32_t minimumNeighbors);

    // -------------------------------------------------------------------------
    // Kernel Density Estimation (KDE)
    // -------------------------------------------------------------------------
    //
    // Local Gaussian density: average over max(k,2)+1 nearest candidates after
    // removing the source ID. Coincident peers remain eligible. Units: length^-3.
    // Auto bandwidth uses 1.06*max(stddev(NN),mean(NN))*n^-0.2, floored at 1e-8.
    // This inherited spacing heuristic is not full-sample multivariate KDE.

    struct KDEParams
    {
        std::size_t KNeighbors{15};        // Neighbors for density estimation.
        float       Bandwidth{0.0f};       // Gaussian bandwidth h. 0 = spacing heuristic.
    };

    struct KDEResult
    {
        std::vector<float> Densities;      // Per-point density estimate.
        float              MeanDensity{0.0f};
        float              MinDensity{0.0f};
        float              MaxDensity{0.0f};
        float              UsedBandwidth{0.0f}; // Actual bandwidth used.
    };

    // Finite samples, at least two rows; no mutation. Invalid parameters or
    // unrepresentable float kernel values return nullopt.
    [[nodiscard]] std::optional<KDEResult> EstimateKernelDensity(
        std::span<const glm::vec3> positions, const KDEParams& params = {});
    // Row-major min(n,max(k,2)+1) nearest candidate IDs per sample, including
    // self if selected, sorted by distance then source ID. The caller guarantees
    // nearest membership; bounds, uniqueness and cardinality are validated here.
    [[nodiscard]] std::optional<KDEResult> EstimateKernelDensityFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const std::uint32_t> candidates,
        const KDEParams& params = {});

    // Publishes "p:density" property on the cloud.
    // Reject fewer than two samples, nonfinite inputs/results or negative scale.
    [[nodiscard]] std::optional<KDEResult> EstimateKernelDensity(
        Cloud& cloud,
        const KDEParams& params = {});

    // -------------------------------------------------------------------------
    // Gaussian Noise
    // -------------------------------------------------------------------------
    //
    // Deterministic true Gaussian per-component displacement. The standard
    // deviation is `StdDevFraction * AverageSpacing`, where AverageSpacing comes
    // from ComputeStatistics.

    struct PointCloudGaussianNoiseParams
    {
        float StdDevFraction{0.0F};
        std::uint64_t Seed{0};
    };

    enum class GaussianNoiseStatus : std::uint8_t
    {
        Success,
        EmptyInput,
        InvalidParameters,
        NonFinitePosition,
        DegenerateScale
    };

    struct GaussianNoiseResult
    {
        GaussianNoiseStatus Status{GaussianNoiseStatus::Success};
        std::size_t ElementCount{0};
        std::size_t DisplacedCount{0};
        float Scale{0.0F};
        glm::vec3 MeanDisplacement{0.0F};
        float MaxDisplacement{0.0F};
    };

    [[nodiscard]] GaussianNoiseResult ApplyGaussianNoise(
        Cloud& cloud, const PointCloudGaussianNoiseParams& params = {});

} // namespace Geometry::PointCloud
