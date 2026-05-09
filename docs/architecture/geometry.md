# Geometry Architecture

`geometry` is the canonical home for geometry-processing algorithms and mesh-domain operations.

## Responsibilities

- Deterministic geometric kernels and data transformations.
- Robust handling of degenerate/non-ideal input cases.
- Integration seams for method packages and benchmark harnesses.

## Dependencies

- Allowed: `core`.
- Disallowed: runtime/app-specific ownership and rendering backend internals.

## Topology connectivity ownership

- `Geometry::Graph::VertexConnectivity` and `Geometry::Graph::HalfedgeConnectivity`
  are the canonical face-free traversal records for vertex-to-halfedge and
  halfedge-to-vertex/next/prev links.
- `Geometry::HalfedgeMesh` reuses those graph traversal records for
  `v:connectivity` and `h:connectivity`, so mesh-backed graph views can share
  topology storage without compatibility-copy properties.
- Mesh-only face incidence is stored separately in
  `Geometry::HalfedgeMesh::HalfedgeFaceConnectivity` under `h:face`; graph
  connectivity must not grow face ownership fields.

## Migration note

As of RORG-093, canonical Geometry code is promoted to `src/geometry`. Remaining `src/legacy` geometry shims (if any) must be temporary, tracked, and removed via follow-up migration tasks.
