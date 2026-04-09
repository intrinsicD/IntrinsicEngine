module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloudUtils;

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
    //   - Positions are mandatory (built-in "p:position" property).
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

    // Returns nullopt if cloud is empty or VoxelSize <= 0.
    [[nodiscard]] std::optional<DownsampleResult> VoxelDownsample(
        const Cloud& cloud,
        const DownsampleParams& params = {});

    // -------------------------------------------------------------------------
    // Radius Estimation
    // -------------------------------------------------------------------------
    //
    // Estimates per-point splat radius from local point density using k nearest
    // neighbors. The radius at each point is set to a fraction of the average
    // distance to its k neighbors, ensuring overlap for hole-free rendering.
    //
    // r_i = scale * (1/k) * sum_{j in kNN(i)} ||p_i - p_j||
    //
    // Typical scale factor: 1.0 (touching surfels) to 1.5 (overlapping).

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
    };

    // Returns nullopt if cloud has < 2 points.
    [[nodiscard]] std::optional<RadiusEstimationResult> EstimateRadii(
        const Cloud& cloud,
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
    // Returns nullopt if cloud has < 2 points.
    [[nodiscard]] std::optional<OutlierEstimationResult> EstimateOutlierProbability(
        Cloud& cloud,
        const OutlierEstimationParams& params = {});

    // -------------------------------------------------------------------------
    // Kernel Density Estimation (KDE)
    // -------------------------------------------------------------------------
    //
    // Per-point density estimation using Gaussian KDE with adaptive bandwidth.
    //
    //   ρ(p_i) = (1/k) * sum_{j in kNN(i)} K_h(||p_i - p_j||)
    //
    // where K_h is a Gaussian kernel with bandwidth h. Bandwidth selection
    // follows Silverman's rule of thumb: h = (4σ⁵/3n)^(1/5) ≈ 1.06·σ·n^(-1/5)
    // where σ is the standard deviation of nearest-neighbor distances.
    //
    // Reference:
    //   - Silverman, "Density Estimation for Statistics and Data Analysis" (1986)

    struct KDEParams
    {
        std::size_t KNeighbors{15};        // Neighbors for density estimation.
        float       Bandwidth{0.0f};       // Gaussian bandwidth h. 0 = auto (Silverman's rule).
    };

    struct KDEResult
    {
        std::vector<float> Densities;      // Per-point density estimate.
        float              MeanDensity{0.0f};
        float              MinDensity{0.0f};
        float              MaxDensity{0.0f};
        float              UsedBandwidth{0.0f}; // Actual bandwidth used.
    };

    // Publishes "p:density" property on the cloud.
    // Returns nullopt if cloud has < 2 points.
    [[nodiscard]] std::optional<KDEResult> EstimateKernelDensity(
        Cloud& cloud,
        const KDEParams& params = {});

} // namespace Geometry::PointCloud
