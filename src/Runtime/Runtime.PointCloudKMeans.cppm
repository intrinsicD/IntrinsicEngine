module;

#include <cstdint>

#include <entt/entity/entity.hpp>

export module Runtime.PointCloudKMeans;

import Runtime.Engine;
import Graphics.Components;
import Geometry.KMeans;
import ECS;

export namespace Runtime::PointCloudKMeans
{
    enum class Domain : uint8_t
    {
        Auto = 0,
        MeshVertices,
        GraphVertices,
        PointCloudPoints,
    };

    struct TargetInfo
    {
        Domain ResolvedDomain = Domain::Auto;
        std::size_t PointCount = 0;
        bool JobPending = false;
        bool SupportsCuda = false;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return ResolvedDomain != Domain::Auto && PointCount > 0;
        }
    };

    // Describe the selected entity's compatible point domain for K-means.
    // The requested domain is used when available; otherwise Auto picks the
    // first compatible authoritative point set.
    [[nodiscard]] TargetInfo DescribeTarget(
        Engine& engine,
        entt::entity entity,
        Domain requestedDomain = Domain::Auto);

    // Schedule an asynchronous point-domain k-means computation for an entity.
    //
    // Backend selection:
    //   - CPU: worker-thread compute over a point-position snapshot.
    //   - CUDA: available for any authoritative point-set source (mesh vertices,
    //           graph nodes, point-cloud points) via stream-backed device
    //           snapshots polled from the main thread each frame. The transient
    //           CUDA job registry for mesh/graph domains is thread-affine and
    //           must only be touched from the owning main-thread frame loop.
    //
    // Returns false when the entity is invalid, has no compatible point domain,
    // or already has an in-flight k-means job on the selected domain.
    [[nodiscard]] bool Schedule(
        Engine& engine,
        entt::entity entity,
        const Geometry::KMeans::Params& params,
        Domain requestedDomain = Domain::Auto);

    // Publish a completed K-means result into the selected authoritative data
    // source using domain-native property names:
    //   - mesh / graph vertices: v:kmeans_*
    //   - point clouds:          p:kmeans_*
    //
    // Publication also refreshes/creates a retained centroid child entity with
    // a point-cloud authority component so centroids render through the normal
    // point pipeline and can carry persistent ECS-side acceleration structures.
    [[nodiscard]] bool PublishResult(
        Engine& engine,
        entt::entity entity,
        const Geometry::KMeans::Result& result,
        double durationMs,
        Domain requestedDomain = Domain::Auto);

    // Pure publication helpers used by PublishResult() after the target entity
    // has been resolved. These keep the PropertySet publication contract
    // testable without requiring a fully constructed Engine instance.
    [[nodiscard]] bool PublishMeshVertexResult(
        ECS::Mesh::Data& meshData,
        const Geometry::KMeans::Result& result,
        double durationMs);

    [[nodiscard]] bool PublishGraphVertexResult(
        ECS::Graph::Data& graphData,
        const Geometry::KMeans::Result& result,
        double durationMs);

    [[nodiscard]] bool PublishPointCloudPointResult(
        ECS::PointCloud::Data& pointCloudData,
        const Geometry::KMeans::Result& result,
        double durationMs);

    // Poll in-flight CUDA jobs and publish completed label/color properties back
    // into authoritative PropertySets. Call once per frame on the owning main
    // thread; the CUDA job registry is explicitly thread-affine.
    void PumpCompletions(Engine& engine);

    // Explicitly releases persistent CUDA buffers/events/stream for one entity.
    // This only affects the point-cloud CUDA cache. CPU jobs are not cancelled.
    void ReleaseEntityBuffers(Engine& engine, entt::entity entity);
}
