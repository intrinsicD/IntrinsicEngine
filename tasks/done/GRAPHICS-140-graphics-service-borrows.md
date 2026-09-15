---
id: GRAPHICS-140
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive dependency cleanup; reviewed diff, compiler metadata and preset verification.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-140 — Keep complete device APIs inside graphics implementations

## Goal
Reuse existing borrowed-service declarations in eight compute, transfer and
visualization interfaces. The operator explicitly requests continued compile
and reuse cleanup with Claude, outside the standing convergence priority.

## Decision
- Baseline `688ec68ca`. PointLBVH, PointKeypoints, ComputeParallelPrimitives and
  GpuTransfer use IDevice and ICommandContext only by pointer/reference.
  VisualizationPropertyBufferResidency, VisualizationOverlayUploadHelper,
  ImGuiOverlaySystem and ImGuiUploadHelper similarly borrow IDevice.
- Reuse the existing globally attached C++ declarations; keep full value-type
  owners and import service APIs directly in implementations that use them.
  No new wrapper, file, lifetime rule, algorithm or backend behavior.
- Original compiler metadata rejects all eight Device boundary guards and
  both point-workspace CommandContext guards. Interface dependency counts before editing:
  14, 14, 18, 16, 21, 21, 18, 21 respectively. These are not timing claims.
- The first build exposed a source-review miss: MemoryAccess belongs to
  CommandContext, so ComputeParallelPrimitives and GpuTransfer keep that import.
  No enum split or new module is needed. Both implementations already import
  Device and CommandContext directly.
- Include ImGuiOverlaySystem to remove the indirect edge from ImGuiUploadHelper.
  The whole build caught the Keypoints runtime operation relying on the
  complete Device API transitively; it now imports that owner directly.
  Defer TransientDebugUploadHelper's separate RenderWorld/GpuWorld dependency.

## Acceptance criteria
- [x] Eight interfaces borrow services without complete device APIs; algorithm,
      field and signature bodies unchanged and no superseded wrapper remains.
- [x] Boundary guards pass on final metadata and original negative controls fail.
- [x] Claude review resolved; canonical ci build, focused/full CPU tests and
      clean cache-disabled Clang 20 graphics producer build pass.
- [x] Documentation and inventory current; no unmeasured timing claim.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality|PointLBVH|PointKeypoint|ComputeParallel|GpuTransfer|VisualizationProperty|VisualizationOverlay|ImGui' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
```

Clang 20 verification uses a fresh disposable ci-derived Null/headless build,
explicit matching compilers/scanner, tests/benchmarks/cache off, and package
installation disabled, targeting ExtrinsicGraphics. This is minimum-compiler
producer verification, not a full Clang 20 suite or GPU runtime claim.

## Structural verification
- Final interface dependency counts: PointLBVH 14 → 2, PointKeypoints 14 → 2,
  ComputeParallelPrimitives 18 → 9, GpuTransfer 16 → 9,
  VisualizationPropertyBufferResidency 21 → 10, VisualizationOverlayUploadHelper
  21 → 10, ImGuiOverlaySystem 18 → 8 and ImGuiUploadHelper 21 → 11.
  No added dependency appears in any interface closure. Counts are compiler
  metadata, not elapsed time, unique aggregate modules or consumer rebuild counts.
- All eight interface declaration bodies and implementation bodies are unchanged
  after removing imports/includes, borrow declarations, comments and whitespace
  for comparison. Four unused standard headers and historical overlay prose
  removed; fixture limitations and lifetime contracts remain explicit.
- Fresh cache-disabled Clang 20 ci-derived ExtrinsicGraphics build passes,
  including a final reconciliation after source cleanup. All eight Device and
  both point-workspace CommandContext checks pass on its fresh metadata.
- Strict clean-workshop and changed-path documentation-sync checks pass. Source
  documentation audit: zero errors, 22 reviewed contract comments retained.
  Generated module inventory remains 417. Manual scorecard: rows 1–3 pass
  (layer, target and public ownership preserved), rows 4–7 n/a (no renderer
  growth, recipe/pass or capability change), row 8 pass (no new exceptions).

## Review resolution
- Claude's corrected source review accepts the borrowed declarations, retained
  MemoryAccess owner and unused standard-header removal. The requested direct
  implementation imports were already present in ComputeParallelPrimitives
  and GpuTransfer; the runtime Keypoints caller was corrected after compilation.
- Its final test-import warning names three test files that do not exist in the
  repository. Do not create those files or make speculative import changes;
  complete IntrinsicTests compilation and actual CTest cases verify consumers.
  Clarified the README subject as suggested. No unrelated formatting sweep.
- Production source delta is 5,642 → 5,587 lines across touched source files,
  primarily historical comment removal. No new production file/module or
  algorithm deletion; eight guards reuse the existing CMake test helper.

## Completion — 2026-09-15
- Commit reference: the enclosing graphics service-borrow commit.
- Canonical ci Clang 23 configure and complete IntrinsicTests build pass.
  Focused CTest: 185 passed, including all 58 compilation-locality checks.
  Full CPU: 4,653 passed, zero failures, one expected ASan-only GLFW skip
  (4,654 selected, 137.94 seconds).
- Fresh Clang 20 producer and boundary evidence remains scoped to graphics
  compilation. No GPU/sanitizer runtime or elapsed compile-time improvement
  is claimed. The remaining RenderWorld/GpuWorld and upload-sharing candidates
  need separate ownership and behavior review before implementation.
