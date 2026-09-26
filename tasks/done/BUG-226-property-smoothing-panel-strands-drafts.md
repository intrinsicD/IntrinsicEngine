---
id: BUG-226
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-reported interactive UI bug; evidence is the reproducing ImGui session test (fails before, passes after), the contract workflow tests and the CPU gate.
contract_schema: 1
contracts: [geometry.property-coherence]
---
# BUG-226 — Smooth Property panel strands drafts after method, solver or input switches

## Completion — 2026-09-27
Commit: the enclosing `claude/smoothing-panel-workflows` commit records this
retirement.

## Goal
Keep every Smooth Property workflow runnable within one session: switching method,
solver, penalty or input must leave a draft the button accepts, or name the exact
conflicting option.

## Symptom
Operator report (2026-09-27): after trying methods on mean curvature in one session,
switching to a vector property, a face property or another solver left the
**Smooth property** button disabled ("positions needed for mesh faces", fit
parameters "no longer valid after total variation").

## Causes
- Fit-only combinations (reweighted solver with delta 0, second order or ball
  bounds) were validated for every method, so fit leftovers blocked other methods and
  the reweighted solver after TGV or delta-0 TV was rejected.
- The lumped mesh-area Laplacian stayed selected when switching from implicit to an
  explicit filter and was rejected from then on.
- A new input kept mesh-only weights, boundary pinning and lumped mass (rejected off
  mesh vertices) and the old output name, which then existed with a different
  storage type for vector inputs.
- The disabled reason was a tooltip listing every parameter range.

## Fix
- `ValidatePropertyFilterParams` checks fit-only combinations only for the
  variational fit.
- `Runtime::ReconcilePropertySmoothingConfig` resolves conflicts after each panel
  edit in favor of the edited choice; the panel calls it before applying the draft.
- The validator reports specific reasons (ADMM-only options, positive delta, lumped
  Laplacian, the mesh-only option), and the panel shows the disabled reason inline.

## Acceptance criteria
- [x] ImGui session test drives the panel's combos and button on mean curvature through averaging, TV (ADMM), TGV, reweighted, implicit with lumped mass, averaging, a vec3 input and a face input; it fails on the previous code at exactly the reported steps.
- [x] Contract workflow tests run every method and fit variant on mean curvature in one draft, leave the fit with every fit-only option set, and switch to vector and face inputs.
- [x] Validation tests updated for the relaxed non-fit combinations and the specific diagnostics.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -R 'PropertySmoothing' --output-on-failure
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
