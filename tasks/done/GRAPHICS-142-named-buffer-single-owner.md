---
id: GRAPHICS-142
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive duplicate-state cleanup; source review and preset verification.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-142 — Store each named GPU buffer once

## Goal
Consolidate GpuSceneSlot's two synchronized maps and repeated entry name into
one existing named-entry map. The operator requests continued reuse/compilation
cleanup with Claude outside the standing product focus.

## Decisions
- Baseline `d78fd33c0`. Only Upsert, Remove and Find use NamedBuffers. External
  callers use NamedBufferEntries.size(), Find, and FindEntry's handle/metadata;
  only one test reads BufferEntry.Name. No persistence or external API clients.
- Keep NamedBufferEntries as canonical storage; Find delegates to FindEntry.
  Upsert writes one entry with the moved key and Remove erases it once. Delete
  NamedBuffers and BufferEntry.Name directly, preserving key lookup, handle
  generations, metadata, unrelated entries and asset/residency behavior.
- Claude reviewed the exact prepared diff with no blocking findings. Confirmed
  BufferHandle has IsValid and FindEntry is const. Existing default-state test
  covers absent names; add an overwrite/remove/unrelated-key regression.
  No new facade, accessor, helper file, backend or compatibility mechanism.

## Acceptance criteria
- [x] One named-entry authority; all in-tree consumers and docs use it.
- [x] Overwrite/removal regression and relevant consumers pass focused CTest.
- [x] Complete ci build/full CPU gate and structural/docs checks pass.
- [x] Claude findings resolved; task retired without a timing or GPU claim.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'GraphicsGpuSceneSlot|VisualizationSync|RenderExtraction|GpuWorld|GeometryResidency|CompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
```

Clang 20 linkage/producer verification belongs to preceding GRAPHICS-141.
This slice changes ordinary container storage, with current Clang 23 CPU
verification; it does not claim GPU/sanitizer execution or measured build speed.

## Review and structural verification
- Applied source/test diff exactly matches Claude's fixed reviewed patch. No
  NamedBuffers or entry->Name use remains in source/tests. The missing-name lookup,
  const return type, handle validity API and moved-key evaluation are verified.
- Strict clean-workshop and documentation-sync checks pass. Module inventory
  remains 417; source documentation has zero errors and five reviewed contract
  comments. Map fields stay plain data; no new API or layer edge is introduced.
- Manual scorecard: rows 1–3 pass (ownership and public layer flow preserved),
  rows 4–7 n/a (no renderer growth, passes or capability change), row 8 pass
  (no exception). The existing map is the sole stored handle/metadata authority.

## Completion — 2026-09-15
- Commit reference: the enclosing named-buffer consolidation commit.
- Canonical ci Clang 23 configure and full IntrinsicTests build pass. Focused
  CTest: 139 passed, including the new overwrite/remove regression and all 62
  compilation-locality checks. Full CPU: 4,658 passed, zero failures, one
  expected ASan-only GLFW skip (4,659 selected, 134.30 seconds).
- One map and one stored key replace two maps and a repeated entry name;
  handle lookup reuses FindEntry. The production delta is ten fewer lines,
  with no new production file. No measured timing or runtime GPU claim.
- Earlier GRAPHICS-141 is committed separately as `d78fd33c0`. The shared scene
  record still has a broad consumer dependency chain; narrowing it needs its
  own ownership/cost review rather than an unmeasured compile-time claim here.
