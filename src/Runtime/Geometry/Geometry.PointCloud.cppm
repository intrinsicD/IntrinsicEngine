module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloud;

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
    class Cloud
    {
    public:
        Cloud();
        Cloud(const Cloud&)            = default;
        Cloud(Cloud&&) noexcept        = default;
        Cloud& operator=(const Cloud&) = default;
        Cloud& operator=(Cloud&&) noexcept = default;
        ~Cloud()                       = default;

        // ---- Capacity / sizing ----
        // Canonical names match Mesh (VerticesSize/IsEmpty) and Graph conventions.

        [[nodiscard]] std::size_t PointCount() const noexcept { return m_Points.Size(); }
        [[nodiscard]] bool        IsEmpty()    const noexcept { return m_Points.Size() == 0; }

        void Reserve(std::size_t n) { m_Points.Reserve(n); }
        void Clear();

        // ---- Point addition ----

        // Add a point with a mandatory position. Returns its handle.
        VertexHandle AddPoint(glm::vec3 position);

        // ---- Built-in attribute presence ----

        [[nodiscard]] bool HasNormals() const noexcept { return m_PNormal.IsValid(); }
        [[nodiscard]] bool HasColors()  const noexcept { return m_PColor.IsValid(); }
        [[nodiscard]] bool HasRadii()   const noexcept { return m_PRadius.IsValid(); }

        // ---- Built-in attribute activation ----
        // Call once to allocate the property; subsequent AddPoint calls will
        // initialise the slot with the default value supplied here.

        void EnableNormals(glm::vec3 defaultNormal = {0.f, 1.f, 0.f});
        void EnableColors (glm::vec4 defaultColor  = {1.f, 1.f, 1.f, 1.f});
        void EnableRadii  (float     defaultRadius = 0.f);

        // ---- Per-point accessors (by handle) ----

        [[nodiscard]] const glm::vec3& Position(VertexHandle p) const { return m_PPoint[p]; }
        [[nodiscard]]       glm::vec3& Position(VertexHandle p)       { return m_PPoint[p]; }

        [[nodiscard]] const glm::vec3& Normal(VertexHandle p) const { return m_PNormal[p]; }
        [[nodiscard]]       glm::vec3& Normal(VertexHandle p)       { return m_PNormal[p]; }

        [[nodiscard]] const glm::vec4& Color(VertexHandle p) const { return m_PColor[p]; }
        [[nodiscard]]       glm::vec4& Color(VertexHandle p)       { return m_PColor[p]; }

        [[nodiscard]] float  Radius(VertexHandle p) const { return m_PRadius[p]; }
        [[nodiscard]] float& Radius(VertexHandle p)       { return m_PRadius[p]; }

        // ---- Span accessors (for bulk GPU upload / SIMD loops) ----

        [[nodiscard]] std::span<const glm::vec3> Positions() const { return m_PPoint.Span(); }
        [[nodiscard]] std::span<glm::vec3>       Positions()       { return m_PPoint.Span(); }

        [[nodiscard]] std::span<const glm::vec3> Normals() const { return m_PNormal.Span(); }
        [[nodiscard]] std::span<glm::vec3>       Normals()       { return m_PNormal.Span(); }

        [[nodiscard]] std::span<const glm::vec4> Colors() const { return m_PColor.Span(); }
        [[nodiscard]] std::span<glm::vec4>       Colors()       { return m_PColor.Span(); }

        [[nodiscard]] std::span<const float> Radii() const { return m_PRadius.Span(); }
        [[nodiscard]] std::span<float>       Radii()       { return m_PRadius.Span(); }

        // ---- Handle from index ----

        [[nodiscard]] static constexpr VertexHandle Handle(std::size_t index) noexcept
        {
            return VertexHandle{static_cast<PropertyIndex>(index)};
        }

        // ---- User-defined per-point properties ----

        template <class T>
        [[nodiscard]] VertexProperty<T> GetOrAddVertexProperty(std::string_view name, T defaultValue = T())
        {
            return VertexProperty<T>(m_Points.GetOrAdd<T>(std::string{name}, std::move(defaultValue)));
        }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetVertexProperty(std::string_view name) const
        {
            return ConstPropertySet(m_Points).Get<T>(name);
        }

        [[nodiscard]] Vertices& PointProperties() noexcept { return m_Points; }
        [[nodiscard]] ConstPropertySet PointProperties() const noexcept { return ConstPropertySet(m_Points); } // NOLINT(readability-convert-member-functions-to-static)

        // Validate internal consistency (built-in optional arrays, if present, match Size()).
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        Vertices m_Points;

        // Built-in properties — always present (position) or lazily allocated.
        VertexProperty<glm::vec3> m_PPoint;   // "p:position"
        VertexProperty<glm::vec3> m_PNormal;  // "p:normal"   (optional)
        VertexProperty<glm::vec4> m_PColor;   // "p:color"    (optional)
        VertexProperty<float>     m_PRadius;  // "p:radius"   (optional)
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

} // namespace Geometry::PointCloud
