// -------------------------------------------------------------------------
// DataAuthority — Zero-size tag components declaring which geometry data
//                 authority an entity carries.
// -------------------------------------------------------------------------
//
// Each entity must have at most ONE DataAuthority tag. The tag is emplaced
// alongside the corresponding Data component (Mesh::Data, Graph::Data,
// PointCloud::Data) by lifecycle systems and spawn code.
//
// Purpose:
//   - O(1) ECS queries for entity type without probing multiple Data types.
//   - Runtime enforcement of the single-authority invariant (debug builds).
//   - Elimination of type-probing heuristics in RenderExtraction and Picking.

export module Graphics.Components.DataAuthority;

export namespace ECS::DataAuthority
{
    /// Entity carries Mesh::Data (faces, edges, halfedges, vertices).
    struct MeshTag {};

    /// Entity carries Graph::Data (nodes + edges).
    struct GraphTag {};

    /// Entity carries PointCloud::Data (points).
    struct PointCloudTag {};
}
