# BUG-010 — Minimal recipe present-pass barrier acceptance asserts wrong layout transition

## Goal
- Align `MinimalDebugPresentPassContract.AcceptanceFrameRecordsBothPassesAndBarrierSequence` with the framegraph's actual end-of-graph backbuffer barrier so the acceptance test passes and codifies the real contract (`Undefined -> Present`) rather than the speculative `ColorAttachment -> Present` shape that the recipe never produces.

## Non-goals
- No change to the framegraph compiler's backbuffer policy. `Graphics.RenderGraph.cpp:206` keeps rejecting `Write(backbuffer, ...)` declarations and `Graphics.RenderGraph.cpp:190` keeps rejecting `Write(*, Present)` declarations.
- No change to the minimal recipe's pass declarations. `MinimalDebugPresent` continues to declare `Read(backbuffer, TextureUsage::Present)` + `SideEffect()` as its finalization signal, matching GRAPHICS-032.
- No new diagnostic counter; the barrier shape is asserted off the existing `MockCommandContext::TextureBarrierCalls` log.
- No `gpu;vulkan` smoke change (deferred to `GRAPHICS-033D`).

## Context
- Status: done.
- Owner/agent: Claude (session `claude/inspect-engine-state-k3aik`).
- Owner/layer: `tests/contract/graphics` (test-side expectation only).
- Symptom: `MinimalDebugPresentPassContract.AcceptanceFrameRecordsBothPassesAndBarrierSequence` fails:
  ```
  Test.MinimalDebugPresentPass.cpp:377: Failure
    Expected: (backbufferToPresentIndex) >= (0), actual: -1 vs 0
    Minimal recipe must finalize the backbuffer to Present as the
    end-of-graph sentinel.
  ```
  An instrumented diagnostic confirms the recipe does emit exactly one
  backbuffer barrier — `Before = TextureLayout::Undefined (0)`,
  `After = TextureLayout::Present (8)` — but the test was filtering on
  `ColorAttachment -> Present` and reported `-1`. The failure reproduces
  on baseline `c4c63ca` without any work-in-progress slice.
- Repro command:
  ```bash
  build/ci/bin/IntrinsicGraphicsContractCpuTests \
    --gtest_filter='MinimalDebugPresentPassContract.AcceptanceFrameRecordsBothPassesAndBarrierSequence'
  ```
- Root cause: the test was authored speculatively against the GRAPHICS-032 design note that says `Pass.Present.MinimalDebug` "writes the imported Backbuffer". In practice the framegraph implementation forbids backbuffer writes (`Graphics.RenderGraph.cpp:206` returns `false` for any `Write` on an imported backbuffer; `Graphics.RenderGraph.cpp:190` forbids `Write(*, Present)`). The recipe therefore declares the present-finalization side effect via `Read(backbuffer, TextureUsage::Present) + SideEffect()`. The barrier compiler emits a single `Undefined -> Present` transition derived from the imported `InitialState = Undefined`, `FinalState = Present` set by `ImportBackbuffer`. There is no intermediate `ColorAttachment` state because no pass writes the backbuffer.
- Impact: the contract acceptance test for the minimal recipe was red on the baseline, masking real regressions and forcing PR authors to mentally diff against "two pre-existing failures" rather than reviewing 100% green output.

## Required changes
- [x] Replace the `FindBarrierIndex(ColorAttachment, Present)` lookup in `Test.MinimalDebugPresentPass.cpp::AcceptanceFrameRecordsBothPassesAndBarrierSequence` with a scan that finds the first backbuffer barrier with `After == Present`, regardless of the `Before` state.
- [x] Assert `Before == Undefined` on the matching barrier to document the canonical shape and trip if a future framegraph change ever inserts an intermediate state without updating the recipe contract.
- [x] Drop the (now unreachable) `Undefined -> ColorAttachment` precondition lookup and the unused `FindBarrierIndex` helper.
- [x] Inline-comment the rationale and reference BUG-010 + GRAPHICS-032 so future readers see why the assertion shape diverges from the design-note prose.

## Tests
- [x] `MinimalDebugPresentPassContract.*` — all 6 cases pass after the fix.
- [x] `IntrinsicGraphicsContractCpuTests` — full file 97/97.
- [x] `ctest --test-dir build/ci -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 224/224.

## Docs
- [x] No external doc churn: the GRAPHICS-032 design note retains its "Pass.Present writes the imported Backbuffer" language because that describes the eventual Vulkan dynamic-rendering semantic (the slot-0 pipeline binds a color attachment derived from the swapchain image). The framegraph just doesn't model it as a graph-level write — the inline comment in the test captures that nuance and links back to this bug for traceability.

## Acceptance criteria
- [x] The acceptance test passes on `MockDevice` without GPU support.
- [x] The barrier-shape assertion catches a regression if the framegraph ever inserts an intermediate state (the `EXPECT_EQ(finalBarrier.Before, Undefined)` line trips and tells the reader to update both this test and the recipe documentation).
- [x] No regression in any other `MinimalDebug*` contract test.
- [x] The contract-test build is warning-clean (the unused `FindBarrierIndex` helper is removed).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
build/ci/bin/IntrinsicGraphicsContractCpuTests \
  --gtest_filter='MinimalDebugPresentPassContract.*'
build/ci/bin/IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -L 'contract' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding a `Write(backbuffer, ...)` to the minimal recipe (would require relaxing the framegraph's imported-backbuffer policy, which is out of scope).
- Changing the framegraph compiler's barrier inference for imported backbuffers.
- Skipping the backbuffer barrier assertion entirely (this is the central contract the acceptance test exists to lock).

## Completion
- Completed: 2026-05-14.
- Commit reference: pending (this session's commit on `claude/inspect-engine-state-k3aik`).
- Verification:
  - All commands above ran clean — `ctest` reports 224/224 across the full contract gate.
