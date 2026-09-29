---
id: RUNTIME-295
theme: I
depends_on: [RUNTIME-293]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision (2026-09-29, ADR 0030); implementation owes IO-counter contract tests and a gpu;vulkan smoke.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# RUNTIME-295 — Extraction fills render blocks from the residency

## Goal
- ADR 0030 decision 3, later step. When a property the renderer draws is resident, extraction
  fills its render block by GPU copy (a gather for seam-split meshes) from the canonical slot
  instead of uploading it from the CPU again. Each change is then uploaded exactly once
  (CPU -> residency), and render buffers stay pure observers.
- It covers positions first, then colors and visualization scalars. The `GpuWorld` CPU shadow is
  kept coherent (patched from the CPU revision, not read back).
- Non-resident properties keep today's direct CPU -> render upload.

## Acceptance criteria
- [ ] IO counters: a resident point cloud whose positions change uploads once per revision
      (residency only), not twice.
- [ ] gpu;vulkan smoke: render output is identical with and without the residency route
      (pixels), for a point cloud and a seam-split mesh.
- [ ] Compaction and device-loss rebuild stay correct with the residency route.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
