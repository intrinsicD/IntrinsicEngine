module;

#include <entt/fwd.hpp>

export module Graphics:Systems.MeshRendererLifecycle;

import :Geometry;
import :GPUScene;
import :MaterialSystem;

export namespace Graphics::Systems::MeshRendererLifecycle
{
    // Lifecycle glue for GPUScene slots.
    // Contract:
    //  - Allocate a GPUScene slot for every MeshRenderer that doesn't have one.
    //  - Push an initial instance packet including bounds (radius > 0) so the slot is considered active.
    //  - On entity destruction, deactivate its bounds (radius = 0) and free the slot.
    //
    // NOTE: This keeps GPUSceneSync focused on incremental per-frame updates.
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  const Core::Assets::AssetManager& assetManager,
                  const MaterialSystem& materialSystem,
                  const GeometryPool& geometryStorage,
                  uint32_t defaultTextureId);
}
