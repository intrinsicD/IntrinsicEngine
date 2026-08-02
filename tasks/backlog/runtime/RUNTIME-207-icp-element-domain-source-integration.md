---
id: RUNTIME-207
theme: I
depends_on: [BUG-096, HARDEN-087]
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
# RUNTIME-207 — ICP element-domain source integration

## Goal

- Make ICP registration accept every pair of entities exposing compatible
  `Vertices` sources, drive only the source transform, and move its tunable
  state through one validated config/agent/runtime lane.

## Non-goals

- No registration-kernel rewrite, new ICP variant, geometry conversion, or
  mutation of source/target element properties.
- No panel redesign; `UI-040` owns multi-domain selection and discovery.
- No bypass of the point-to-plane normal semantics fixed by `BUG-096`.

## Context

- `ApplyEditorRegistrationCommand` currently rejects mesh and graph entities
  by exact point-cloud provenance although `Geometry.Registration::AlignICP`
  consumes point sets. Both source and target may independently be mesh,
  graph, or point cloud.
- Re-check Besl–McKay ICP (DOI `10.1109/34.121791`), Chen–Medioni
  point-to-plane registration (DOI `10.1016/0262-8856(92)90066-C`), Segal et
  al. Generalized ICP (DOI `10.15607/RSS.2009.V.021`), Chetverikov et al.
  Trimmed ICP (DOI `10.1109/ICPR.2002.1047997`), and Zhang et al. Fast and
  Robust ICP (DOI `10.1109/TPAMI.2021.3054619`) before changing the binding.
  These formulations constrain point, normal, correspondence, and robustness
  inputs, not ECS provenance; do not expose a variant unless its kernel exists.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two finite point/vertex spans; target normals only for variants that require them. |
| Compatible entity sources | All 3x3 source/target pairs across mesh vertices, graph nodes, and point-cloud points. |
| RuntimeModule | Extend the existing geometry-processing registration command/job path. |
| Config/agent | Add one serializable validated ICP section for variant and numeric parameters, consumed identically by direct/agent/UI callers. |
| UI | `UI-040` supplies cross-domain entity selection and appropriate domain menu entries. |
| Publication | Apply the solved transform to the source entity only; geometry sources and target transform remain unchanged. |
| End-to-end tests | Parameterized 3x3 source matrix, normal readiness, async staleness, transform history, config round-trip, and UI discovery. |

## Required changes

- [ ] Resolve both operands through canonical `Vertices` availability and
      retain each source domain in diagnostics/job identity without requiring
      matching provenance.
- [ ] Keep world-space transform handling and generation validation for all
      source pairs; point-to-plane readiness uses the corrected target-normal
      contract from `BUG-096`.
- [ ] Add a right-sized serializable ICP config section with side-effect-free
      preview/validate then apply, and make runtime command creation consume the
      resolved config used by agents and UI.
- [ ] Preserve the existing typed job and command-history owner; publish only
      one undoable source-transform mutation and no geometry replacement.
- [ ] Expose copied availability/disabled-reason data for `UI-040`, including
      missing positions, normals, transform, stale entity, and same-entity
      rejection.

## Tests

- [ ] Parameterize registration contracts over all nine source/target domain
      combinations and verify the same finite point spans reach the kernel.
- [ ] Cover point-to-plane normal requirements independently of provenance and
      preserve `BUG-096` regressions.
- [ ] Verify only the source transform changes and undo/redo round-trips it for
      mesh, graph, and point-cloud sources.
- [ ] Add config serialization/preview/apply parity plus stale async result
      rejection across mixed-domain pairs.

## Docs

- [ ] Update runtime/config docs and the ICP literature/limitations note with
      the element-source matrix and transform-only publication policy.

## Acceptance criteria

- [ ] Any two live entities with valid required vertex data can be registered,
      regardless of their mesh/graph/point-cloud provenance.
- [ ] Config, agent, UI, and direct commands share validation and diagnostics.
- [ ] No registration path converts or mutates geometry topology.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Registration|ICP' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No exact provenance gate, mesh/graph conversion, target mutation, UI-owned
  registration state, or second registration service.
- No point-to-plane fallback that silently substitutes source normals.
