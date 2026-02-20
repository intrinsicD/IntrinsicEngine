module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

export module Geometry:PointCloud;

import :AABB;

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
    //   - GPU rendering via PointCloudRenderPass (flat splatting, surfel, EWA, Gaussian splat).
    //   - Spatial queries via Octree/KDTree acceleration.
    //
    // Design:
    //   - SoA (Structure-of-Arrays) layout for cache-friendly batch processing.
    //   - Positions are mandatory; normals, colors, radii are optional.
    //   - Follows the Geometry operator pattern: Params/Result structs, optional
    //     return for degenerate input.
    //
    // References:
    //   - Gross & Pfister, "Point-Based Graphics" (Morgan Kaufmann, 2007)
    //   - Zwicker et al., "Surface Splatting" (SIGGRAPH 2001)
    //   - Botsch et al., "Point-Based Surface Representation" (IEEE CG&A 2005)

    // -------------------------------------------------------------------------
    // Rendering modes (selectable per point cloud entity)
    // -------------------------------------------------------------------------
    enum class RenderMode : uint32_t
    {
        FlatDisc      = 0,  // Screen-space constant-size circular splats
        Surfel        = 1,  // Oriented discs derived from surface normals
        EWA           = 2,  // Elliptical Weighted Average (Zwicker et al. 2001)
        GaussianSplat = 3,  // Isotropic Gaussian blob with smooth opacity falloff (3DGS-style)
    };

    // -------------------------------------------------------------------------
    // Core Point Cloud Data
    // -------------------------------------------------------------------------
    struct Cloud
    {
        // Mandatory: 3D positions (one per point).
        std::vector<glm::vec3> Positions;

        // Optional per-point attributes (empty = not present).
        std::vector<glm::vec3> Normals;      // Unit-length surface normals.
        std::vector<glm::vec4> Colors;       // RGBA float colors [0,1].
        std::vector<float>     Radii;        // Per-point world-space splat radius.

        // ---- Queries ----

        [[nodiscard]] std::size_t Size() const noexcept { return Positions.size(); }
        [[nodiscard]] bool        Empty() const noexcept { return Positions.empty(); }
        [[nodiscard]] bool        HasNormals() const noexcept { return Normals.size() == Positions.size(); }
        [[nodiscard]] bool        HasColors() const noexcept { return Colors.size() == Positions.size(); }
        [[nodiscard]] bool        HasRadii() const noexcept { return Radii.size() == Positions.size(); }

        // Validate internal consistency (sizes match).
        [[nodiscard]] bool IsValid() const noexcept
        {
            if (Positions.empty()) return true; // Empty cloud is valid.
            if (!Normals.empty() && Normals.size() != Positions.size()) return false;
            if (!Colors.empty() && Colors.size() != Positions.size()) return false;
            if (!Radii.empty() && Radii.size() != Positions.size()) return false;
            return true;
        }
    };

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
        AABB        BoundingBox;
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
        std::vector<float> Radii;
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
        Cloud Subsampled;
        std::vector<std::size_t> SelectedIndices;  // Original indices of kept points.
    };

    // Returns nullopt if cloud is empty.
    [[nodiscard]] std::optional<SubsampleResult> RandomSubsample(
        const Cloud& cloud,
        const SubsampleParams& params = {});

} // namespace Geometry::PointCloud
