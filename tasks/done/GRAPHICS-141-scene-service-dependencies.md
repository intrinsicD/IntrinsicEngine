---
id: GRAPHICS-141
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive dependency and reuse cleanup; reviewed diff, compiler metadata and preset verification.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-141 — Reuse scene handles and narrow GPU service dependencies

## Goal
Remove redundant GPU handle declarations and complete device imports from the
scene/debug-upload dependency chain. The operator explicitly requests continued
reuse and compilation cleanup with Claude outside the standing product focus.

## Decisions
- Baseline `68bcf0993`. GpuSceneSlot already owns the two handle aliases repeated
  in GpuWorld. Remove the repeats and have RenderWorld import their owner.
- GpuWorld only borrows IDevice and ICommandContext; reuse their established
  globally attached declarations, retaining value-type owners (including
  Descriptors for Format). TransientDebugUploadHelper also borrows Device.
  Add direct Device imports in both implementations; preserve algorithms,
  resource ownership and lifetimes. No new module, header or compatibility path.
- The build and source audit identify eight handle consumers needing direct
  GpuSceneSlot imports, plus the GpuWorld implementation needing StrongHandle
  for its slot allocator. Update these owners directly; no re-export shim.
- LightSystem still imports GpuWorld for its synchronization method. Do not
  change GpuWorld's module attachment or split light records merely to remove
  that edge. Guard the actual transitive Device/CommandContext boundaries.
- Original metadata fails all four proposed guards. Baseline dependencies:
  GpuWorld 20, RenderWorld 25, LightSystem 21, TransientDebugUploadHelper 26.
  These are structural counts, not elapsed build timings.

## Acceptance criteria
- [x] Canonical handles reused; complete service imports confined to consumers
      that need them, with behavior and resource ownership preserved.
- [x] Claude findings resolved, final boundary checks and focused/full CPU pass.
- [x] Fresh cache-disabled Clang 20 graphics producer verification passes.
- [x] Documentation/inventory current; task retired with evidence and no timing claim.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality|GpuWorld|GpuSceneSlot|LightSystem|RenderWorld|TransientDebug|VisualizationOverlay' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
```

Minimum-compiler verification uses a disposable ci-derived Null/headless
Clang 20 build with matching scanner, package installation and caches disabled,
tests/benchmarks off, targeting ExtrinsicGraphics. No GPU runtime claim.

## Review
+- Claude accepts the reference-only service uses and canonical handle owner.
+  Actual RHI class definitions already use export extern "C++"; no attachment
+  change is introduced. The debug helper has no smart-pointer member, so its
+  unused memory header is removed. LightSystem exists at the guarded path and
+  the pre-edit metadata checks rejected both full APIs there.
+- Clarified architecture wording to include all handle consumers. Removed
+  obsolete snapshot/slot descriptions and repetitive debug-upload commentary;
+  retained invalid-slot, per-frame reuse and manager lifetime contracts.
+- No new renderer ownership, pass, config, synchronization or backend behavior.
+  Manual scorecard: rows 1–3 pass; 4–7 n/a; 8 pass (no new exceptions).

## Completion — 2026-09-15
- Commit reference: the enclosing scene-service dependency commit.
- Canonical ci Clang 23 configure, full IntrinsicTests build and final source
  reconciliation pass. Focused CTest: 159 passed. Full CPU: 4,657 passed,
  zero failures, one expected ASan-only GLFW skip (4,658 selected, 137.34 s).
- Fresh cache-disabled Clang 20 ExtrinsicGraphics build and reconciliation
  pass. Both forbidden-module checks pass on all four interface closures;
  a GpuWorld implementation control correctly rejects the Device dependency.
- Dependency counts: GpuWorld 20 → 10, RenderWorld 25 → 16, LightSystem
  21 → 11, TransientDebugUploadHelper 26 → 17, with no added transitive module.
  All C++ bodies are unchanged apart from two redundant aliases. Production
  source is 3,702 → 3,648 lines, primarily comment cleanup; no new source file.
- Strict clean-workshop and docs-sync checks pass. Source documentation has
  zero errors and 19 reviewed contract comments; inventory remains 417.
  No timing, GPU-runtime or sanitizer claim. Named-buffer map consolidation
  is a separately reviewed follow-up, not part of this dependency-only result.
