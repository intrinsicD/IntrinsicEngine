module;

#include <memory>
#include <entt/fwd.hpp>
#include <entt/signal/dispatcher.hpp>

export module Graphics.Systems.GraphGeometrySync;

import Graphics.Geometry;
import Graphics.GPUScene;
import Core.FrameGraph;
import RHI;

export namespace Graphics::Systems::GraphGeometrySync
{
    // Uploads graph node positions to vertex buffers, extracts edge index pairs
    // from graph topology, extracts per-node attributes (colors, radii) from
    // PropertySets, and allocates GPUScene slots for frustum culling — for
    // retained-mode BDA rendering.
    //
    // Upload mode is selected per-entity by ECS::Graph::Data::StaticGeometry:
    //   false (default) → Direct (host-visible, CPU_TO_GPU) for dynamic graphs.
    //   true            → Staged (device-local, GPU_ONLY) for static graphs.
    //
    // Phase 6 migration: directly populates per-pass typed components
    // (Line::Component for edges, Point::Component for nodes) from
    // Graph::Data, replacing ComponentMigration's graph bridging.
    //
    // Contract:
    //  - Iterates entities with ECS::Graph::Data where GpuDirty == true.
    //  - Compacts positions (skips deleted vertices, builds remap table).
    //  - Creates GeometryGpuData via Direct or Staged upload (per StaticGeometry).
    //  - Extracts per-node colors ("v:color") and radii ("v:radius") from
    //    graph PropertySets into CachedNodeColors / CachedNodeRadii.
    //  - Stores the GeometryHandle, edge pairs, and node attributes on the component.
    //  - Allocates a GPUScene slot after successful upload (if not yet allocated),
    //    queues initial instance data with bounding sphere for frustum culling.
    //  - Clears GpuDirty after successful upload.
    //  - Populates sibling Line::Component (edges) and Point::Component
    //    (nodes) every frame for visible entities with valid GPU geometry.
    //
    // Thread model: main thread only (writes ECS components + GPU resources).
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager,
                  entt::dispatcher& dispatcher);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Write<ECS::Graph::Data>, Write<ECS::Line::Component>,
    //           Write<ECS::Point::Component>, WaitFor("TransformUpdate").
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        GeometryPool& geometryStorage,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager,
                        entt::dispatcher& dispatcher);
}
