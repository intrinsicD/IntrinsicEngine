// =========================================================================
// DirtyTag — Per-domain dirty tracking for PropertySet CPU->GPU sync.
// =========================================================================
//
// Zero-size tag components for fine-grained dirty tracking. Each tag
// represents an independent data domain. Presence of a tag on an entity
// signals that the corresponding data has changed and needs re-sync.
//
// Six domains:
//   VertexPositions  — node/point positions changed -> vertex buffer re-upload.
//   VertexAttributes — per-vertex colors/radii changed -> re-extract cached attributes.
//   EdgeTopology     — edge connectivity changed -> rebuild edge index buffer.
//   EdgeAttributes   — per-edge colors/widths changed -> re-extract cached edge attributes.
//   FaceTopology     — face connectivity changed -> rebuild face index buffer.
//   FaceAttributes   — per-face colors changed -> re-extract cached face attributes.
//
// Consumed by PropertySetDirtySyncSystem. Cleared after successful sync.
// Multiple tags can coexist on the same entity (independent domains).

export module Graphics.Components.DirtyTag;

export namespace ECS::DirtyTag
{
    struct VertexPositions {};
    struct VertexAttributes {};
    struct EdgeTopology {};
    struct EdgeAttributes {};
    struct FaceTopology {};
    struct FaceAttributes {};
}
