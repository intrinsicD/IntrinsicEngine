---
id: UI-050
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
---
# UI-050 — Vector-field properties cannot be visualized

## Goal
- Let the Geometry Visualization panel display `vec3` element-domain properties
  (normals, principal curvature directions) as a vector field.

## Non-goals
- No new vector-field *computation*; this task visualizes properties that
  already exist.
- No streamline/LIC/tensor-glyph research surface.
- No change to the scalar-field or isoline paths that already work.

## Context
- Symptom: with `sculpt.obj` selected after running mesh curvature, the
  Geometry Visualization panel lists `v:normal`, `v:principal_dir1` and
  `v:principal_dir2` (all `[MeshVertices, Vec3, 21464]`) but offers no action
  for them. Each shows the line:
  "Vector-field candidate; adapter residency is not owned by this UI slice."
- Two problems in one:
  1. **Missing capability.** Principal-direction and normal visualization is a
     core instrument for a geometry-processing engine; the properties are
     computed and published but cannot be seen. By contrast `v:mean_curvature`
     and `v:gaussian_curvature` offer working `Scalar` and `Isolines` actions,
     so the scalar path proves the surrounding plumbing works.
  2. **Wrong audience for the message.** "adapter residency is not owned by this
     UI slice" is an internal implementation note about slice ownership shown
     verbatim to end users. Whatever the disposition, the user-facing text
     should say what they can or cannot do.
- Also observed: `UInt32` properties (`v:source_vertex`, `f:source_face`) offer
  no action at all, with no explanation. Decide in this task whether integer
  properties are visualizable (as categorical/label color) or explicitly out of
  scope, and say so in the UI.
- Owner: `runtime` owns the visualization model, residency, and recipe; `app`
  owns presentation. `graphics` already carries per-entity visualization
  configuration and colormap machinery used by the scalar path.
- Line-drawing infrastructure already exists (`assets/shaders/line.vert`,
  `line.frag`) and is a plausible substrate for glyph/segment rendering.

## Control surfaces
- Config: vector-field scale/normalization/subsampling through the existing
  `VisualizationConfig` lane, not panel-private state.
- UI: a `Vector field` action alongside `Scalar` / `Isolines`.
- Agent/CLI: reachable through the same validated visualization recipe path.

## Required changes
- [ ] Add a vector-field visualization source to the runtime visualization model
      and recipe for count-matched `vec3` element-domain properties.
- [ ] Add the `Vector field` action to the Geometry Visualization property list.
- [ ] Provide scale, normalization, and subsampling controls through the
      validated config lane.
- [ ] Replace the "adapter residency is not owned by this UI slice" text with a
      user-facing statement of capability or prerequisite.
- [ ] Decide and state the disposition for integer-typed properties.

## Tests
- [ ] Add a runtime contract test asserting a `vec3` vertex property is offered
      as a vector-field candidate and that the recipe round-trips.
- [ ] Add a test asserting a count-mismatched property is rejected with a
      reason.
- [ ] Add an opt-in `gpu;vulkan` readback smoke asserting vector-field geometry
      is actually drawn.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update the visualization prose in `src/app/Sandbox/README.md` and the
      owning runtime visualization doc.

## Acceptance criteria
- [ ] `v:normal` and `v:principal_dir1/2` can be displayed as a vector field.
- [ ] No internal slice-ownership wording remains in user-facing text.
- [ ] Integer-property disposition is stated in the UI.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'Visualization|SandboxEditor' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Panel-private renderer switches that bypass `VisualizationConfig`.
- Shipping the capability while leaving the internal-ownership message in place.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere
  else. The readback smoke closes `Operational`.
