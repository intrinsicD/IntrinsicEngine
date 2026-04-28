module;

#include <entt/fwd.hpp>

export module Graphics.Systems.PropertySetDirtySync;

import Core.FrameGraph;

export namespace Graphics::Systems::PropertySetDirtySync
{
    // Per-frame PropertySet dirty-domain synchronization system.
    //
    // Detects ECS::DirtyTag::* tag components on entities and performs
    // the minimum necessary re-sync for each independent data domain:
    //
    //   VertexPositions  → Sets GpuDirty=true on Graph::Data / PointCloud::Data
    //                      so the existing lifecycle system performs a full
    //                      vertex buffer re-upload on the same frame.
    //
    //   VertexAttributes → Re-extracts per-vertex colors/radii from PropertySets
    //                      into cached attribute vectors (CachedNodeColors,
    //                      CachedNodeRadii, CachedColors, CachedRadii).
    //                      Does NOT trigger full vertex buffer re-upload.
    //
    //   EdgeTopology     → Sets GpuDirty=true on Graph::Data so the lifecycle
    //                      system rebuilds the edge index buffer.
    //
    //   EdgeAttributes   → Re-extracts per-edge colors from PropertySets into
    //                      cached edge color vectors. Does NOT trigger full
    //                      geometry re-upload.
    //
    //   FaceTopology     → Sets FaceColorsDirty=true on Surface::Component.
    //                      Full face index rebuild is deferred to mesh lifecycle.
    //
    //   FaceAttributes   → Re-extracts per-face colors from mesh PropertySets
    //                      into Surface::Component::CachedFaceColors.
    //
    // All dirty tags are cleared after processing. Multiple simultaneous
    // dirty domains are handled independently — a face color change does
    // NOT trigger a vertex buffer re-upload.
    //
    // Scheduling: runs BEFORE existing lifecycle systems (GraphLifecycle,
    // PointCloudLifecycle, MeshViewLifecycle) so that GpuDirty flags
    // set here are consumed in the same frame.
    //
    // Thread model: main thread only (reads/writes ECS components).
    void OnUpdate(entt::registry& registry);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Write<Graph::Data>, Write<PointCloud::Data>,
    //           Write<Surface::Component>, Signal("PropertySetDirtySync").
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry);
}
