# Direct mesh import preparation and deferred UV review

Scope: queued direct mesh imports prepare geometry, normals, bounds and ECS
component buffers in the import worker. Publication transfers ownership. Only
missing UVs schedule the subsequent atlas job, which publishes UV attributes
without replacing topology, normals or unrelated properties.

Reuse: `GeometrySources::PopulateFromMesh` now composes `PrepareFromMesh` and
`PublishPreparedMesh`; queued imports use the same preparation separately.
The existing job/world/binding validation, atlas generator, corner mapping and
normal-texture bake path remain the owners. No new dependency edges or tuning
state were introduced.

## Architecture scorecard

| Row | Result | Evidence |
| --- | --- | --- |
| 1. Layer imports | pass | Strict layering check, no allowlist entries |
| 2. CMake edges | pass | No link-edge changes |
| 3. Public types | pass | ECS preparation returns its own owned component structs; geometry remains ECS-free |
| 4. Renderer ownership | n/a | No renderer subsystem added |
| 5. Typed passes | n/a | No new passes |
| 6. Recipe resources | n/a | No frame-recipe changes |
| 7. Maturity closure | n/a | No task retirement |
| 8. Exceptions | n/a | No temporary exceptions |

## Review corrections

- `Mesh::Reserve` reserves the eight built-in element arrays rather than the
  registry's named-property slots. A capacity/address-stability test covers it.
- Revision mismatches caused by component relocation are checked against the
  immutable prepared geometry. Changed geometry, new UVs, changed bindings and
  invalid world/entity ownership still reject delivery. Unrelated attributes
  survive UV-only publication, including curvature computed while UVs run.
- Non-manifold display fallback retains original source adjacency for atlas
  generation and maps source corner UVs onto the disconnected render vertices.
- Vertex and corner UVs survive first publication and bypass automatic atlasing.
- The asset loader transfers its worker-prepared first value and retains an
  immutable snapshot for subsequent reload calls.
- Geometry preparation failures remain materialization failures and cannot
  silently fall through to another decoded payload kind.

Focused tests cover buffer ownership, authored UVs without an atlas job, UV-only
publication retaining position/topology storage, reload, non-manifold atlas
adjacency, concurrent curvature, relocation, geometry/binding edits, cancellation,
world replacement and recycled entities. Full CPU and sanitizer matrices remain
PR/merge gates. Debug/sanitizer UI runs are diagnostic, not performance benchmarks.

Independent read-only review used Claude Opus 5.5 with medium effort. Reported
issues were corrected and the final follow-up reported no remaining concrete
issues. The main focused CTest selection passed 408 tests; reserve consumers
(subdivision, convex hull and marching cubes) passed another 58 focused tests.
Live Vulkan UI checks exercised authored UVs without atlas scheduling, a small
mesh whose generated UVs completed, and Nefertiti becoming visible/selectable
while its separate UV job remained pending. The large atlas run was not used to
claim atlas completion or a loading speedup.
