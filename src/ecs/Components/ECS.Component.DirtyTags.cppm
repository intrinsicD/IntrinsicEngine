module;

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Component.DirtyTags;

export namespace Extrinsic::ECS::Components::DirtyTags
{
    struct GpuDirty
    {
    }; // full geometry re-upload
    struct DirtyVertexPositions
    {
    }; // positions changed, re-upload vertex buffer
    struct DirtyVertexAttributes
    {
    }; // colors/normals changed, re-upload attrib buffer
    struct DirtyEdgeTopology
    {
    }; // edge connectivity changed
    struct DirtyFaceTopology
    {
    }; // face connectivity changed
    struct DirtyTransform
    {
    }; // world matrix needs GPU sync (distinct from IsDirtyTag which is CPU-only)

    // Producer-side stamping helpers for geometry dirty domains.
    //
    // Each helper emplace_or_replace's the corresponding tag on `entity` so
    // calls are idempotent and safe against entities that already carry the
    // tag. The fine-grained domain tags (`DirtyVertexPositions`,
    // `DirtyVertexAttributes`, `DirtyEdgeTopology`, `DirtyFaceTopology`) are
    // independent partial-upload markers and do not implicitly stamp
    // `GpuDirty`; the coarse `MarkGpuDirty` is reserved for callers that want
    // a full geometry re-upload signal.
    //
    // Clearing-side ownership: ECS does not clear these tags. Downstream
    // consumers (runtime render extraction, future GPU residency drains)
    // remove them after the corresponding upload, mirroring the existing
    // `DirtyTags::DirtyTransform` drain in
    // `Runtime.RenderExtraction::ExtractAndSubmit`.
    inline void MarkVertexPositionsDirty(entt::registry& registry, entt::entity entity)
    {
        registry.emplace_or_replace<DirtyVertexPositions>(entity);
    }

    inline void MarkVertexAttributesDirty(entt::registry& registry, entt::entity entity)
    {
        registry.emplace_or_replace<DirtyVertexAttributes>(entity);
    }

    inline void MarkEdgeTopologyDirty(entt::registry& registry, entt::entity entity)
    {
        registry.emplace_or_replace<DirtyEdgeTopology>(entity);
    }

    inline void MarkFaceTopologyDirty(entt::registry& registry, entt::entity entity)
    {
        registry.emplace_or_replace<DirtyFaceTopology>(entity);
    }

    inline void MarkGpuDirty(entt::registry& registry, entt::entity entity)
    {
        registry.emplace_or_replace<GpuDirty>(entity);
    }
}
