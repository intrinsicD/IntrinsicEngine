---
id: RUNTIME-210
theme: I
depends_on: [METHOD-002]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-210 — Signed Heat runtime and config integration

## Goal

- Integrate the existing Signed Heat CPU reference through a mesh-only typed
  runtime operation and validated config/agent lane, publishing its per-vertex
  results onto the selected mesh for `UI-042`.

## Non-goals

- No point-cloud/volumetric variant, numerical rewrite, optimized/GPU backend,
  implicit curve authoring, or UI implementation.
- No weakening of the method's triangle-mesh plus oriented-halfedge-curve input
  contract merely to advertise more domains.

## Context

- `methods/geometry/signed_heat` and `Geometry.SignedHeatMethod` are complete
  CPU-reference assets but have no runtime/config/Sandbox binding. Unlike the
  point-span violations, this method legitimately requires mesh faces and an
  oriented halfedge curve.
- Re-read Crane, Weischedel, and Wardetzky's scalar Heat Method predecessor
  (DOI `10.1145/2516971.2516977`), Feng and Crane, “A Heat Method for
  Generalized Signed Distance” (DOI `10.1145/3658220`), and the official
  geometry-central Signed Heat reference implementation
  (`https://geometry-central.net/surface/algorithms/signed_heat_method/`)
  before integration. Keep the repository's documented surface Variant A
  limitations; do not infer that the package implements the paper's other
  discretizations merely because the paper and reference suite describe them.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Triangle mesh faces/halfedges plus an oriented halfedge source curve. |
| Compatible entity sources | Mesh entities only; graph and point-cloud entities do not satisfy the surface operator. |
| RuntimeModule | Add a typed operation to the existing geometry-processing module and world job/history path. |
| Config/agent | Serializable Signed Heat params and source-halfedge-property reference through preview/validate/apply. |
| UI | `UI-042` registers Mesh / Processing / Signed Heat and selects a compatible halfedge property. |
| Publication | Same-count `v:signed_heat_distance` and `v:is_signed_heat_source` properties on the originating mesh vertices. |
| End-to-end tests | Mesh source extraction, config/agent command, async publication/history, visualization, and panel discovery. |

## Required changes

- [ ] Define a minimal runtime config containing Signed Heat numeric params and
      the canonical boolean halfedge-property name whose oriented handles form
      the source curve; validate schema, property kind/count, and non-empty
      source side-effect-free.
- [ ] Build an immutable selected-mesh snapshot through existing geometry-source
      extraction, execute `Geometry.SignedHeatMethod` on the runtime job lane,
      and reject stale entity/source/config completion.
- [ ] Publish both result properties through one generation-validated undoable
      history mutation, preserving topology and unrelated properties and
      marking only required attribute/presentation dirtiness.
- [ ] Expose pointer-free availability, diagnostics, backend identity, and
      property-catalog options for `UI-042`; direct/agent/UI callers share one
      validated apply and execute path.
- [ ] Add a scalar visualization binding path using existing presentation
      recipes rather than a Signed-Heat-specific renderer seam.

## Tests

- [ ] Add runtime contracts for valid closed/open source curves, missing/wrong
      property types, invalid params, stale async completion, and deterministic
      diagnostics.
- [ ] Verify publication and undo/redo preserve mesh topology and unrelated
      properties exactly.
- [ ] Verify config file/agent/direct command parity and copied availability
      options.
- [ ] Add an integration test proving the published distance property can drive
      the existing scalar visualization path.

## Docs

- [ ] Update method/runtime/config docs with the engine matrix, source-property
      convention, diagnostics, literature review, and current surface-only
      limitations.

## Acceptance criteria

- [ ] Signed Heat is reachable through RuntimeModule and non-UI config/agent
      controls on compatible mesh entities.
- [ ] Results publish as same-cardinality vertex properties with exact history
      and visualization compatibility.
- [ ] Unsupported graph/point-cloud domains fail for the method's real topology
      requirement, not provenance convenience.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SignedHeat' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No graph/point-cloud advertisement, synthetic curve fallback, UI-owned method
  execution, topology mutation, private visualization pass, or backend token
  without an implementation.
