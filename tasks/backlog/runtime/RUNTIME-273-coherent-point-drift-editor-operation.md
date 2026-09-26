---
id: RUNTIME-273
theme: I
depends_on: [METHOD-015]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the 2026-09-27 Framework24 gap audit; implementation owes its own tests and evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# RUNTIME-273 — Coherent Point Drift editor operation

## Goal
Bind the CPD family (METHOD-015; later METHOD-049 policies and METHOD-050) to the
editor as a validated, asynchronous, undoable operation on any canonical point
domain, so UI-055 and agents can register one entity onto another.

## Context
Operator request 2026-09-27: CPD with a proper UI and the fast method. Reuse the
ICP pattern (retired RUNTIME-207/UI-040, `Runtime.RegistrationOperations`) rather
than a new service. Framework24's `bcg_system_coherent_point_drift.cpp` is the
feature checklist (see UI-055).

## Acceptance criteria
- [ ] Config section `sandbox.coherent_point_drift` with one validator for preview/apply/execution: source and target entity/domain/positions, variant (rigid default, affine, nonrigid, later bayesian), `w`, sigma² initialization/floor, max iterations, convergence criteria (relative/absolute on RMSE, sigma², parameter change), nonrigid kernel and regularization (β, λ, low-rank k), Anderson toggle and window, E-step policy (METHOD-049) with tolerance, and source/target subsampling (strategies from GEOM-061/GEOM-105, fixed seed).
- [ ] Asynchronous job with cancellation, per-iteration trace (RMSE, sigma², parameter change, NLL) streamed to the UI, and an optional step mode (init / single step / run to convergence) for inspection.
- [ ] Publication in one guarded history transaction: rigid/affine update the source entity transform; nonrigid (and BCPD) writes displaced positions or a named displacement property, chosen explicitly; stale source/target revisions reject publication.
- [ ] Requested/actual variant and E-step backend reported; unsupported combinations rejected at validation, no hidden fallback.
- [ ] Contract tests for config round trip/validation, every variant on at least two domains, cancellation, stale rejection and undo/redo.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two point sets from canonical domains plus CPD parameters. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds, and derived face centers/edge midpoints via the shared sample capture. |
| RuntimeModule | New operation file next to `Runtime.RegistrationOperations`, sharing capture and history helpers. |
| Config/agent | `sandbox.coherent_point_drift` as above. |
| UI | UI-055. |
| Publication | Transform or displacement as above, guarded and undoable. |
| End-to-end tests | Contract tests here; ImGui path in UI-055. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
