---
id: BUG-156
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude"
branch: "claude/mesh-curvature-analysis-4zvnwi"
worktree: "/home/user/IntrinsicEngine"
claimed_at: "2026-08-12T08:20:00Z"
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence]
contract_review: "This repair changes the documented numerical semantics of the public curvature tensor operators: hinge support narrows from the PMP-default two-ring to one-ring and the three damped eigenvalue-smoothing passes are removed, so published H/K equal the principal invariants exactly at every vertex. No layer edge, module surface, property name, cardinality, import path, or backend axis changes; the reusable Geometry.Smoothing operation keeps its public contract for callers that stabilize fields explicitly."
maturity_target: Operational
---
# BUG-156 — Two-ring support and eigenvalue smoothing cancel genuine curvature

## Status

- Root cause isolated, correction implemented, and regression tests added on
  branch `claude/mesh-curvature-analysis-4zvnwi`; awaiting reporter-side visual
  confirmation in the Sandbox and independent review.

## Goal

- Make the published curvature field on well-conditioned meshes agree with the
  surface's actual curvature — no near-zero bands, sign flips, or large
  magnitude loss beside sharp creases — while keeping the signed edge-dihedral
  formulation, fail-closed quality gating, and deterministic behavior.

## Non-goals

- No new curvature backend, estimator family (Rusinkiewicz, corrected
  measures, jet fitting), tuning API, or config surface.
- No change to the standalone Meyer operators, OBJ import, topology
  construction, or the reusable `Geometry.Smoothing` public contract.
- No universal accuracy claim beyond the measured assets and fixtures.

## Context

- Reported symptom: on `tests/data/sculpt.obj` (the BUG-137 acceptance asset)
  the Sandbox curvature visualization looked correct in parts but showed other
  parts as all zeros or much too small where similar curvature was expected.
- Import, halfedge construction, circulators, and connectivity were explicitly
  ruled out: the asset parses to a closed genus-2 manifold (V=3669, E=11013,
  F=7342, Euler characteristic −2, zero boundary, minimum triangle quality
  0.46), and the engine builds exactly that mesh with zero rejected triangles.
  The engine field equals an independent NumPy replica of the same algorithm
  to 1.4e-14, so the defect is in the algorithm's parameters, not the engine
  data structures.
- Root cause, two stacked effects faithfully ported from PMP defaults during
  BUG-153/BUG-154:
  1. Two-ring hinge support integrates sharp-crease bending into flanking
     smooth vertices (raw κ_max ≈ +11 one ring from a crease whose flank truly
     has H ≈ −2 by the independent Meyer cotan operator).
  2. Three damped nonnegative-cotan smoothing passes average the algebraically
     ranked κ min/max channels across convex/concave transitions, cancelling
     genuine curvature: 65 vertices lost curvature entirely, 917 flipped sign,
     and the median relative mean-curvature error versus the Meyer cross-check
     was 62% (p90 339%).
- The BUG-154 corpus differential compared Intrinsic against PMP with
  identical parameters, so this shared-defect class was invisible to it; the
  2026-08-12 estimator study measured parity, not accuracy against an
  independent operator.
- Measured variants on the reporting asset (relative H error vs Meyer,
  median/p90, zero-band and sign-flip counts): current two-ring+3-pass
  0.62/3.39/65/917; one-ring raw 0.001/0.035/0/0. Under 0.5%–2% normal-noise
  perturbations the one-ring raw variant also dominates the current default on
  every aggregate. Both corrected parameters remain expressible in PMP's API
  (`two_ring_neighborhood = false`, zero post-smoothing steps).

## Control surfaces

- Config: unchanged; the operator stays deterministic with no new tuning axis.
- UI: unchanged Mesh / Processing / Curvature action and diagnostics record.
- Agent/CLI: unchanged runtime operation surface.

## Backends

- Backend axis: not applicable; one deterministic geometry-owned CPU
  implementation remains.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: an oriented triangle surface with finite positions and vertex/edge/halfedge adjacency. |
| Compatible entity sources | Unchanged mesh geometry entities. |
| `RuntimeModule` | Unchanged synchronous/queued curvature operation. |
| Config/agent | Unchanged command surface. |
| UI | Unchanged action; diagnostics counts now describe the unsmoothed field. |
| Publication | Same property names, domains, and cardinality; published H/K now equal the principal invariants exactly at every vertex, including zero sentinels. |
| End-to-end tests | Geometry unit regressions (oracle fixture, crease flanks, sculpt acceptance, open-mesh invariants) plus the existing runtime editor-operation coverage. |

## Required changes

- [x] Restrict hinge-tensor support to the vertex's own incident edges
      (one-ring) so crease bending stays at crease-adjacent vertices.
- [x] Publish unsmoothed eigenvalues; remove the three damped smoothing passes
      and the smoothing-mask plumbing from the curvature path while keeping
      reliability propagation for boundary interpolation.
- [x] Keep boundary interpolation, complementary eigenpair publication,
      orientation conventions, dimensionless quality gating, and diagnostics.
- [x] Update the module interface contract and implementation rationale.

## Tests

- [x] Replace the frozen two-ring/triple-smoothing PMP fixture with an
      independent NumPy-replica oracle of the corrected formulation (engine
      matches to 3e-16; fixture tolerance 1e-12).
- [x] Add a crease-flank regression where the superseded pipeline loses up to
      79% of |H| (7/14 probes fail) and the corrected path stays within 0.05%.
- [x] Add the sculpt.obj acceptance regression: full support, no zero-band
      vertices, no sign flips against the independent Meyer field.
- [x] Add an open-mesh invariant regression proving H=(κ₁+κ₂)/2 and K=κ₁κ₂ at
      every vertex and diagnostics extrema bounding all nonzero published
      values (BUG-154 review finding: smoothing previously wrote nonzero κ to
      unsupported corner vertices whose H/K stayed zero).
- [x] Keep analytic sphere/cylinder/saddle, orientation, scale-invariance,
      fail-closed, determinism, and full-field coherence coverage passing.
- [x] Run the focused curvature selectors and the default CPU-supported gate.

## Docs

- [x] Update `src/geometry/Geometry.HalfedgeMesh.Curvature.cppm` numerical
      contract and the implementation synopsis.
- [x] Update `docs/architecture/geometry.md` estimator description.
- [x] Record the defect analysis, measurements, and decision in
      `docs/reports/2026-08-12-curvature-support-smoothing-defect.md`.
- [x] Synchronize the active task index and session brief.

## Acceptance criteria

- [x] On `tests/data/sculpt.obj`, the published field has full vertex support
      and zero zero-band or sign-flip vertices against the independent Meyer
      cross-check; median relative H deviation is below 1%.
- [x] Crease-flank vertices retain sign and at least 30% of the Meyer
      magnitude on the tent-ridge fixture.
- [x] Published H and K equal the principal invariants exactly at every
      vertex, and diagnostics extrema bound every nonzero published value.
- [x] The default CPU-supported gate passes on the changed surface.
- [ ] Reporter confirms the Sandbox visualization on the reporting asset.
- [ ] Independent review accepts the semantics change and its documentation.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^(CurvatureTensor\.|Curvature_|CurvatureSegmentation|CurvaturePatch|SandboxEditorUi\.MeshCurvature)' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Reintroducing implicit post-smoothing or widened support inside the operator
  instead of leaving stabilization to explicit `Geometry.Smoothing` calls.
- Weakening the quality gate, finite-value guarantees, or determinism.
- Mixing a second estimator backend or tuning API into this repair.

## Maturity

- Target: `Operational` through the existing Sandbox curvature command on the
  reporting asset. The geometry correction and CPU gates are complete;
  reporter-side visual confirmation and independent review close the task.
