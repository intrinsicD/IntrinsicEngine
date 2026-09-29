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
# RUNTIME-292 — First end-to-end GPU property transaction: scalar smoothing

## Goal
- ADR 0030 decisions 6-7 on one scalar consumer before any position preview.
- Vulkan property smoothing (PropertyFilter, then implicit CG) does this:
  - reads its input through the residency;
  - writes a typed output ring;
  - shows each published chunk as a colormap preview through the visualization recipe
    `BufferBDA` (no readback);
  - at the end reads the front back once and publishes through the existing undoable
    publication, then binds the revision (the next run uploads nothing).
- Cancel and stale publish nothing and remove the preview.
- Undo changes the CPU revision, and the next GPU run uploads once.

## Acceptance criteria
- [ ] Contract tests: commit binds the revision; cancel, stale and undo behave as ADR 0030 says;
      "Applied" is reported only after the CPU publication.
- [ ] gpu;vulkan smoke:
  - the preview colormap changes before commit;
  - after commit the CPU values equal the readback and the CPU reference within the existing
    tolerance;
  - a second run on the same revision uploads zero input bytes.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
