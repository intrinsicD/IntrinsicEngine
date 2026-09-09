# Virtual-source geodesics

CPU reference for approximate distances over triangle surfaces, exposed by
`Geometry::Geodesic::ComputeVirtualSourceDistance` in `Geometry.Geodesic`.
See [formulation and limitations](paper.md) and [manifest](method.yaml).

Open **Mesh → Geodesics → Virtual Source Propagation**, select a mesh, add
picked vertices or explicit vertex indices, and compute. The panel uses the
same validated `sandbox.geodesics` config path as programmatic callers and
activates `v:geodesic_distance` scalar coloring after successful publication.
The vertex float3 position binding is selected from the property catalog.

```json
{
  "source_vertices": [0, 12],
  "max_halfedge_expansions": 10000000,
  "position_property": "v:position"
}
```

This is the payload of app section `sandbox.geodesics`, schema
`intrinsic.runtime.sandbox.geodesics`, version 1. Empty source lists can be
stored while preparing an operation, but computation requires a nonempty set.
`ApplyEditorGeodesicsCommand` also accepts an explicit config, and
`ApplyEditorConfiguredGeodesicsCommand` uses the active config.

Publication changes only `v:geodesic_distance` (double) and
`v:is_geodesic_source` (bool) on the originating mesh. Sources are exactly zero;
unreachable/deleted slots are infinity. Undo/redo preserves unrelated
properties and topology and rejects stale geometry or conflicting output edits.
The result reports `cpu_reference`, propagation counts, and unreachable count.
A failed computation leaves existing output properties unchanged.

| Integration | Source and behavior |
|---|---|
| Geometry API | Triangle surface topology plus a vertex-domain float3 span; default overload binds mesh positions |
| Runtime | Detached triangle snapshot, original vertex slot correspondence, explicit config preflight |
| Config / agents | Serializable source indices, position property, and expansion budget; shared preview/apply |
| UI | Mesh menu, picked or entered sources, compatible vertex float3 property catalog, diagnostics |
| Publication | Named vertex properties, dirty marking, scalar display, guarded undo/redo |
| Other domains | Graph adjacency or point samples alone cannot supply the required triangle surface |

Focused verification:

```sh
ctest --test-dir build/ci -R '^(GeodesicVirtualSource|GeodesicsOperations|SandboxEditorGeodesics)\.' --output-on-failure --timeout 60
```

The declared smoke workload runs through `IntrinsicBenchmarkSmoke` and the
standard benchmark sealing tool. Measurements from a dirty working tree are
local development evidence; no optimized/GPU backend or performance gain is
claimed.
