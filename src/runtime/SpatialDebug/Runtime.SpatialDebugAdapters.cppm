module;

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.SpatialDebugAdapters;

import Geometry.BVH;
import Extrinsic.Graphics.SpatialDebugVisualizers;

export namespace Extrinsic::Runtime
{
    struct SpatialDebugSnapshotBatch
    {
        std::vector<Extrinsic::Graphics::SpatialDebugAabb>           Bounds{};
        std::vector<Extrinsic::Graphics::SpatialDebugHierarchyNode>  HierarchyNodes{};
        std::vector<Extrinsic::Graphics::SpatialDebugSplitPlane>     SplitPlanes{};
        std::vector<glm::vec3>                                       ConvexHullVertices{};
        std::vector<Extrinsic::Graphics::SpatialDebugWireEdge>       ConvexHullEdges{};
        std::vector<glm::vec3>                                       PointMarkers{};

        void Clear() noexcept;
    };

    struct SpatialDebugAdapterOptions
    {
        bool          LeafOnly{false};
        bool          OccupancyOnly{false};
        std::uint32_t MaxDepth{32u};
    };

    struct SpatialDebugAdapterStats
    {
        std::uint32_t LeafNodeCount{0u};
        std::uint32_t InnerNodeCount{0u};
        std::uint32_t SplitPlaneCount{0u};
        std::uint32_t EmptyNodeSkippedCount{0u};
        std::uint32_t DepthCapTruncationCount{0u};
    };

    class ISpatialDebugAdapter
    {
    public:
        virtual ~ISpatialDebugAdapter() = default;

        virtual void Append(SpatialDebugSnapshotBatch&        out,
                            const SpatialDebugAdapterOptions& options,
                            SpatialDebugAdapterStats&         stats) const = 0;
    };

    class BvhAdapter final : public ISpatialDebugAdapter
    {
    public:
        explicit BvhAdapter(const Geometry::BVH& bvh) noexcept;

        // BvhAdapter stores a non-owning pointer; binding to a temporary
        // would leave m_Bvh dangling at the end of the full expression.
        BvhAdapter(const Geometry::BVH&&) = delete;

        void Append(SpatialDebugSnapshotBatch&        out,
                    const SpatialDebugAdapterOptions& options,
                    SpatialDebugAdapterStats&         stats) const override;

    private:
        const Geometry::BVH* m_Bvh{nullptr};
    };
}
