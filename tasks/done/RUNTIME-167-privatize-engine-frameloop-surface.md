---
id: RUNTIME-167
theme: F
depends_on:
  - CI-003
  - RUNTIME-150
maturity_target: Operational
---
# RUNTIME-167 — Privatize the Engine frame-loop surface

## Status

- Completed on 2026-07-16 at `Operational`.
- Implementation commit: `25e3e7c5`.
- Verification: the default-lane privacy guard
  passed 1/1, the duplicated frame-loop behavior contracts passed 18/18, the
  focused CPU selection passed 54/54, explicit resize coverage passed 1/1, and
  the unaffected opt-in layering selection passed 23/23. The final default
  CPU-supported gate passed 3,781/3,781 in 407.53 seconds, including the new
  `contract;runtime` privacy guard. The strict structural/review bundle is
  green; the one stale pre-existing async-work assertion is tracked separately
  as `BUG-090`.

## Goal
- Remove the exported `Extrinsic.Runtime.Engine:FrameLoop` module partition as a
  public build-graph surface by moving its frame-loop helper declarations and
  bodies behind `Runtime.Engine` private source/header glue, preserving current
  frame behavior.

## Non-goals
- No frame scheduling, fixed-step, render, platform, or shutdown behavior change.
- No new public `Engine` API and no runtime/kernel architecture decision.
- No broad `Runtime.Engine` decomposition beyond the frame-loop partition.

## Context
- Owner/layer: `runtime`; this is implementation locality inside the runtime
  composition root.
- `src/runtime/Runtime.Engine.FrameLoop.cppm` is a low-fanout compile hotspot:
  local 2026-07-10 triage measured up to 138.098s for an interface now at 452
  lines and 31 imports with only `Runtime.Engine.cpp` as a production importer.
- The original extraction is retired in `RUNTIME-150`; this task narrows the
  outcome from "module partition" to "private implementation seam".
- The repository rule is to keep `.cppm` surfaces to exported types and small
  declarations; frame-loop hook adapters and control-flow bodies are private
  implementation detail.

## Right-sizing

- Measured current surface: 452 interface lines, 31 imports, one production
  importer (`Runtime.Engine.cpp`), no C++ test importers, and nine source-reading
  references in one layering test file. A recent contention-sensitive
  `.ninja_log` sample was 142.139 s; the task retains the earlier 138.098 s
  observation as baseline evidence rather than a performance claim.
- Simpler alternative: one include-only private header consumed exactly once by
  `Runtime.Engine.cpp`. Keep the helper bodies together because the abbreviated
  fixed-step template must be visible in its consuming translation unit; do not
  add a replacement module, object library, interface, registry, or factory.
- Blast radius: the partition/source owner, runtime CMake/README and architecture
  wording, source-reading layering contracts, the runtime backlog index, and the
  generated module inventory.
- Reintroduction trigger: reconsider a private partition only when a present
  second `Runtime.Engine` implementation unit needs these helpers and comparative
  measurements justify a shared BMI.

## Implementation evidence

- All 12 declarations from the former partition remain implementation detail:
  `kIdleSleepSeconds`, `ElapsedMicros`, `RuntimeFrameContext`, the six platform/
  operational/render/transfer/streaming/asset hook adapters,
  `HasPendingPreRenderTransformFlush`, `RunFixedStepSimulationTicks`, and
  `PopulateMainCameraForFrame`. They now reside in one include-only header whose
  sole production consumer is `Runtime.Engine.cpp`.
- After removing the 44-line module/import prologue, the 408-line helper
  namespace body is byte-identical (SHA-256 prefix `56f35b7e`) to the retired
  partition. The `Runtime.Engine.cpp` namespace body is also byte-identical;
  only the prologue replaces `import :FrameLoop` with the private include and
  directly imports `Extrinsic.RHI.TransferQueue`.
- The module surface changes from 452 interface lines and 31 direct imports to
  zero interface lines/imports. The textual helper is 413 lines with no module
  directives or imports, while `Runtime.Engine.cpp` remains at 93 imports; the
  combined direct-import count therefore drops from 124 to 93. Generated module
  inventory decreases from 391 to 390.
- A post-change no-ccache exact `Runtime.Engine.cpp.o` rebuild completed in
  17.91 s wall time (`.ninja_log` compile edge: 14.565 s), against the recorded
  138.098 s pre-change partition sample (a later contention-sensitive sample
  was 142.139 s). These different build-graph samples are diagnostic only; no
  compile-time speedup is claimed. The demonstrated result is removal of one
  module/BMI surface and 31 partition imports.

## Required changes
- [x] Inventory every declaration in `Runtime.Engine.FrameLoop.cppm` as either
      frame-loop implementation detail or required by `Runtime.Engine.cpp`.
- [x] Replace the exported partition with a private header/source seam included
      only by `Runtime.Engine.cpp` after its required module imports, or fold
      the declarations directly into the implementation unit if that is cleaner.
- [x] Remove the partition from `src/runtime/CMakeLists.txt` module file sets
      and update imports from `import :FrameLoop` to the chosen private seam.
- [x] Preserve exact frame-loop call order, diagnostics, and shutdown behavior.
- [x] Record before/after interface count, import count, and local `.ninja_log`
      compile timing against the `CI-003` baseline.

## Tests
- [x] Run focused runtime frame-loop, engine lifecycle, and sandbox acceptance
      tests covering fixed-step, minimized/resize, render extraction, and
      shutdown paths.
- [x] Run strict layering and test-layout checks.
- [x] Run the default CPU-supported CTest gate.

## Docs
- [x] Update `src/runtime/README.md` and runtime architecture docs if they name
      the frame-loop module partition.
- [x] Regenerate `docs/api/generated/module_inventory.md` after removing the
      module surface.

## Acceptance criteria
- [x] `Extrinsic.Runtime.Engine:FrameLoop` is no longer an exported module
      surface or CMake module file-set entry.
- [x] `Runtime.Engine.cpp` remains the only owner of frame-loop helper code.
- [x] Focused and default CPU gates prove behavior parity.
- [x] Build-timing evidence is recorded without claiming a speedup from one run.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R '^RuntimeEnginePrivateGlue.FrameLoopHelpersArePrivateTextualGlue$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R '^RuntimeFrameLoopContract\.' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngine|RuntimeSandboxAcceptance|FrameLoop|RenderExtraction' -E '^RuntimeEnginePrivateGlue.FrameLoopHelpersArePrivateTextualGlue$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapter.PumpsInputAndResizeReportsFramebufferPixelsWithoutDoubleScaling' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R '^RuntimeEngineLayering\.' -E '^RuntimeEngineLayering.AsyncWorkServiceKeepsStreamingAndDerivedJobOwnershipOutOfEngine$' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing frame sequencing or event ordering while moving the surface.
- Moving runtime ownership into `platform`, `graphics`, `app`, or tests.
- Leaving a compatibility module re-export for the retired partition.

## Maturity
- Target: `Operational`; this is a behavior-preserving privatization of an
  already operational runtime frame path, so no `Operational` follow-up is owed.
