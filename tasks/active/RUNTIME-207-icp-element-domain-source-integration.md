---
id: RUNTIME-207
theme: I
depends_on: [BUG-096, HARDEN-087]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: codex-interactive
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-08T14:06:03.847080+00:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-207 — ICP element-domain source integration

## Goal

- Make ICP registration accept every pair of caller-selected finite `vec3`
  properties on resolved element domains, drive only the source transform, and
  move its tunable state through one validated config/agent/runtime lane.

## Non-goals

- No registration-kernel rewrite, new ICP variant, geometry conversion, or
  mutation of source/target element properties.
- No panel redesign; `UI-040` owns multi-domain selection and discovery.
- No bypass of the point-to-plane normal semantics fixed by `BUG-096`.

## Context

- `ApplyEditorRegistrationCommand` currently rejects mesh and graph entities
  by exact point-cloud provenance although `Geometry.Registration::AlignICP`
  consumes point sets. Both operands may independently name any count-matched
  property domain; mesh face centers are valid without becoming vertices.
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
| Least-structured input | Two caller-selected finite `Property<vec3>` spans; same-domain target normals only for variants that require them. |
| Compatible entity sources | Every pair across mesh vertex/edge/halfedge/face, graph node/edge/halfedge, and point-cloud point domains satisfying the named property contract. |
| RuntimeModule | Extend the existing geometry-processing registration command/job path. |
| Config/agent | Add one serializable validated ICP section for variant and numeric parameters, consumed identically by direct/agent/UI callers. |
| UI | `UI-040` supplies cross-domain entity selection and appropriate domain menu entries. |
| Publication | Apply the solved transform to the source entity only; geometry sources and target transform remain unchanged. |
| End-to-end tests | Parameterized cross-provenance and non-vertex property cases, normal readiness, async staleness, transform history, config round-trip, and UI discovery. |

## Spatial acceleration consideration

Consider sharing the unchanged target-property index across ICP iterations and
requests. Point-LBVH nearest queries fit correspondence search, but the current
kernel owns a KD-tree; record a scoped adapter decision without widening this
property-integration task into a kernel/GPU rewrite. Preserve target-
normal/source-slot mapping and the world/local distance metric, especially under
nonuniform scale.

See the [shared spatial-index consumer inventory](../../docs/architecture/spatial-index-consumers.md).

## Required changes

- [x] Carry canonical `GeometryPropertyRef` identities for both operands and
      resolve them through the property catalog; retain each element domain in
      diagnostics/job identity without requiring matching provenance or a
      `VertexProperty` wrapper.
- [x] Keep world-space transform handling and generation validation for all
      source pairs; point-to-plane readiness uses the corrected target-normal
      contract from `BUG-096`.
- [x] Add a right-sized serializable ICP config section with side-effect-free
      preview/validate then apply, and make runtime command creation consume the
      resolved config used by agents and UI.
- [x] Preserve the existing typed job and command-history owner; publish only
      one undoable source-transform mutation and no geometry replacement.
- [x] Expose copied availability/disabled-reason data for `UI-040`, including
      missing positions, normals, transform, stale entity, and same-entity
      rejection.

## Tests

- [x] Parameterize registration contracts over all provenance pairs and every
      physical property-domain family, including mesh face centers, and verify
      the named finite spans reach the same kernel.
- [x] Cover point-to-plane normal requirements independently of provenance and
      preserve `BUG-096` regressions.
- [x] Verify only the source transform changes and undo/redo round-trips it for
      mesh, graph, and point-cloud sources.
- [x] Add config serialization/preview/apply parity plus stale async result
      rejection across mixed-domain pairs.

## Docs

- [x] Update runtime/config docs and the ICP literature/limitations note with
      the element-source matrix and transform-only publication policy.

## Acceptance criteria

- [x] Any two live entities with valid required typed properties can be
      registered, regardless of property domain or mesh/graph/point-cloud
      provenance.
- [x] Config, agent, UI, and direct commands share validation and diagnostics.
- [x] No registration path converts or mutates geometry topology.

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

## Interactive implementation note — 2026-09-08

The operator accepted the combined canonical ICP workflow and shared-index
integration proposal. This explicitly adds cached CPU LBVH and framed Vulkan
correspondence queries to the original binding/UI scope. The CPU KD-tree default
and existing CPU solve remain. No GICP, Trimmed ICP overlap estimator, Anderson
acceleration, kNN or triangle query method is included. Implementation acceptance
is verified in the shared working tree. These notes remain active pending
review/commit. The design is documented in
[registration](../../docs/architecture/registration.md).

## Verification record — 2026-09-08

- CPU cohort: 4,349 selected; 4,348 passed with host display access, no
  failures, and one expected unsanitized LeakSanitizer-control skip.
- Two Vulkan/ASan/UBSan readback tests passed, including framed point-to-point
  and point-to-plane registration with same-variant CPU references.
- CPU and runtime GPU comparison output is schema-v2 validated, dirty-source,
  and non-claim-eligible. The default remains CPU KD-tree.
- Canonical 8-by-8 domain pairs, live-row mapping, normal preflight, stale
  binding/deletion rejection, source-only undo/redo, config preview/apply/run,
  and one shared window behind the domain menu entries are covered.
- [Review and limitations](../../docs/reviews/2026-09-08-icp-spatial-integration.md).
