# Geometry

CPU geometry containers, file IO and algorithms live here. This layer depends on
Core and owns no live ECS, Runtime or graphics state. Import the narrow module
needed by the caller; there is no umbrella module.

- **Data and properties:** `Geometry.Properties`, `Geometry.DomainViews`,
  `Geometry.HalfedgeMesh` and `Geometry.Graph` define storage and borrowed views.
- **Files:** `Geometry.IO` describes supported formats; `Geometry.HalfedgeMesh.IO`,
  `Geometry.PointCloud.IO` and `Geometry.Graph.IO` own their readers and writers.
- **Spatial queries:** `Geometry.KDTree` and `Geometry.SupportRadius` provide
  point-set queries; Runtime owns entity-specific caches.
- **Mesh processing:** the `Geometry.HalfedgeMesh.*` families own topology,
  fields, parameterization and mesh algorithms.
- **Point processing:** the `Geometry.PointCloud.*` and `Geometry.Registration.*`
  families consume typed samples and publish their own results.

Use the [architecture](../../docs/architecture/geometry.md),
[API and numeric policy](../../docs/architecture/geometry-api-style.md), and
[module inventory](../../docs/api/generated/module_inventory.md) for contracts
and exact module names. Unit and IO coverage lives in
[`tests/unit/geometry`](../../tests/unit/geometry).
