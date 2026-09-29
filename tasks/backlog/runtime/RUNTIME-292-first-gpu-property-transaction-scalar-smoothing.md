---
id: RUNTIME-292
theme: I
depends_on: [GRAPHICS-155]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision and two independent design reviews (2026-09-29, ADR 0030); implementation owes the contract tests and gpu;vulkan smokes listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# RUNTIME-292 — First end-to-end GPU property transaction: scalar smoothing with Accept

## Goal
- ADR 0030 decisions 5-7 on one scalar consumer before any position observation.
- Vulkan property smoothing (PropertyFilter, then implicit CG):
  - reads its input from the canonical slot;
  - writes a typed ring.
- The renderer observes it when the appearance shows that scalar. The ring front is bound
  directly through the visualization recipe `BufferBDA` (float presentation view for doubles),
  with no readback.
- When the method finishes or is stopped, the panel shows Accept / Discard:
  - Accept: one readback, the existing undoable publication, then the front becomes the
    canonical slot for the new revision (the next run uploads nothing).
  - Discard: nothing is published, and observation returns to the canonical slot.
- Stale while pending: Accept is disabled with the reason. Undo changes the CPU revision, and
  the next GPU use uploads once.
- Batch and agent commands keep automatic publication.

## Acceptance criteria
- [ ] Contract tests:
  - Accept binds the revision;
  - Discard, cancel, stale-while-pending and undo behave as ADR 0030 says;
  - "Applied" is reported only after the CPU publication.
- [ ] gpu;vulkan smoke:
  - the observed colormap changes before Accept;
  - after Accept the CPU values equal the readback and the CPU reference within the existing
    tolerance;
  - a second run on the same revision uploads zero input bytes.
- [ ] Panel test: Accept / Discard appear after finishing or stopping; Accept is disabled on a
      stale input.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
