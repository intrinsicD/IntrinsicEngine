module;

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.SpatialDebugAdapters;

import Geometry.BVH;
import Geometry.ConvexHull;
import Geometry.KDTree;
import Geometry.Octree;
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

    class KdTreeAdapter final : public ISpatialDebugAdapter
    {
    public:
        explicit KdTreeAdapter(const Geometry::KDTree& kdTree) noexcept;

        // KdTreeAdapter stores a non-owning pointer; binding to a temporary
        // would leave m_KdTree dangling at the end of the full expression.
        KdTreeAdapter(const Geometry::KDTree&&) = delete;

        void Append(SpatialDebugSnapshotBatch&        out,
                    const SpatialDebugAdapterOptions& options,
                    SpatialDebugAdapterStats&         stats) const override;

    private:
        const Geometry::KDTree* m_KdTree{nullptr};
    };

    class OctreeAdapter final : public ISpatialDebugAdapter
    {
    public:
        explicit OctreeAdapter(const Geometry::Octree& octree) noexcept;

        // OctreeAdapter stores a non-owning pointer; binding to a temporary
        // would leave m_Octree dangling at the end of the full expression.
        OctreeAdapter(const Geometry::Octree&&) = delete;

        void Append(SpatialDebugSnapshotBatch&        out,
                    const SpatialDebugAdapterOptions& options,
                    SpatialDebugAdapterStats&         stats) const override;

    private:
        const Geometry::Octree* m_Octree{nullptr};
    };

    // ConvexHullAdapter translates a Geometry::ConvexHull (V-Rep + H-Rep) into
    // the data-only ConvexHullVertices / ConvexHullEdges spans consumed by
    // Extrinsic::Graphics::BuildSpatialDebugConvexHullWireframe. Edges are
    // derived from vertex/plane incidence: two hull vertices form an edge when
    // they share at least two face planes (within IncidenceEpsilon of each
    // plane), which holds for every convex polytope edge by construction. The
    // adapter ignores LeafOnly / OccupancyOnly / MaxDepth because none of the
    // tree-shaped traversal options apply to a flat hull; it leaves the
    // SpatialDebugAdapterStats accumulator untouched for the same reason.
    class ConvexHullAdapter final : public ISpatialDebugAdapter
    {
    public:
        explicit ConvexHullAdapter(const Geometry::ConvexHull& hull,
                                   float                       incidenceEpsilon = 1e-4f) noexcept;

        // ConvexHullAdapter stores a non-owning pointer; binding to a temporary
        // would leave m_Hull dangling at the end of the full expression.
        ConvexHullAdapter(const Geometry::ConvexHull&&) = delete;

        void Append(SpatialDebugSnapshotBatch&        out,
                    const SpatialDebugAdapterOptions& options,
                    SpatialDebugAdapterStats&         stats) const override;

    private:
        const Geometry::ConvexHull* m_Hull{nullptr};
        float                       m_IncidenceEpsilon{1e-4f};
    };

    // SpatialDebugAdapterRegistry maps an opaque renderable key onto a
    // non-owning ISpatialDebugAdapter*. Slice C introduces this registry as
    // the seam Slice D will use from RenderExtractionCache::ExtractAndSubmit
    // to resolve the active adapter per entity. The registry stores raw
    // pointers; the caller owns adapter lifetime and must Unregister before
    // the adapter or its source geometry tree is destroyed. Re-registering
    // the same key overwrites the previous entry.
    class SpatialDebugAdapterRegistry
    {
    public:
        using Key = std::uint64_t;

        void                                Register(Key key, const ISpatialDebugAdapter& adapter);
        bool                                Unregister(Key key) noexcept;
        [[nodiscard]] const ISpatialDebugAdapter* Find(Key key) const noexcept;
        [[nodiscard]] bool                  Contains(Key key) const noexcept;
        [[nodiscard]] std::size_t           Size() const noexcept;
        [[nodiscard]] bool                  Empty() const noexcept;
        void                                Clear() noexcept;

    private:
        std::unordered_map<Key, const ISpatialDebugAdapter*> m_Adapters{};
    };
}
