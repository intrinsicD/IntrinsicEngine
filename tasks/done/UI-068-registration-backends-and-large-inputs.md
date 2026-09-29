---
id: UI-068
theme: G
depends_on: [UI-067]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive fixes from an operator check on two Vlasic meshes (2026-09-29); verified by contract and panel tests and a live sandbox rerun.
contract_schema: 1
contracts: [repo.source-documentation]
---
# UI-068 — Visible registration backends, usable large deforming runs

## Completion — 2026-09-29
Commit: `2a14cb1a9` on `claude/cpd-nystrom`. Live rerun on mesh_0060 -> mesh_0000 (10002
vertices each): the ICP target survives opening the panel; nonrigid CPD starts with defaults
(automatic rank 100, 58 effective eigenpairs, 150 iterations in under 10 s); BCPD on 2000/2000
subsamples went from 0.21 s to 5 ms per iteration with the same sigma^2 (5.04e-4); Discard
removes the preview; every trace plot axis is labelled. Operational.

## Goal
- Operator report (2026-09-29): every registration method felt far too slow on two
  10002-vertex Vlasic meshes and the Vulkan backend could not be selected. Findings (with
  Fable 5.1 and Codex 6 Astra reviews): nonrigid and BCPD were refused above 8192 points with
  the default full kernel and cost O(m^3) per iteration below; nine panels showed a locked
  "Backend: CPU" combo above the real choice; CPD's E-step choice hid in a collapsed node;
  three UI bugs (preview kept after Apply/Discard, ICP target cleared on single selection,
  unlabelled log axes). Frame latency of the device path is GRAPHICS-150.

## Acceptance criteria
- [x] Apply and Discard drop the CPD preview in the frame that clicks them (panel test).
- [x] A registration target follows the selection only when two entities are selected (unit
      and panel tests).
- [x] Log trace plots within one decade and near-constant series have labelled axes.
- [x] Panels with a real backend choice show it as "Backend" without a dummy; ICP and CPD name
      why Vulkan would fall back before a run starts, from the gate the run uses.
- [x] `auto_low_rank` (default on) runs nonrigid/BCPD above 1024 registered source points
      with rank 100; off keeps the exact full kernel (contract test).

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift|Registration|SandboxProcessingPanels|SpatialIndexCache' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```
