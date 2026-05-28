# BUG-013 — Default-recipe + minimal-debug backbuffer readback contract tests SEGV under clang-20 modules

## Goal
- Restore `MinimalDebugBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` and `DefaultRecipeBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` to the green default CPU gate. Both contract tests currently SEGV (PC = 0x0) the moment the renderer's per-recipe backbuffer readback block calls `graphicsContext.CopyTextureToBuffer(...)` on a `MockCommandContext`, so neither the pre-existing GRAPHICS-033D readback contract nor the GRAPHICS-076E follow-up contract are actually verifiable on `main`.

## Non-goals
- Do not change the renderer-visible API surface of `IRenderer::SetMinimalDebugBackbufferReadbackBuffer(...)` / `IRenderer::SetDefaultRecipeBackbufferReadbackBuffer(...)` or the counter shapes — both are correct as authored.
- Do not retire the `MinimalDebug` scaffold (`GRAPHICS-081`) until this regression is resolved; both fixtures it deletes depend on this contract gate being green.
- Do not silently widen the HARDEN-072 fallback by re-pure-virtualizing `ICommandContext::CopyTextureToBuffer` (that would force every backend/mock/recording test class to override and is a much larger blast radius than this bug warrants if a narrower fix exists).
- Do not weaken AddressSanitizer / default CPU gate strictness to hide the SEGV.

## Context
- Symptom: `IntrinsicGraphicsContractCpuTests --gtest_filter='*BackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce'` SEGVs with `ASAN: SEGV on unknown address 0x000000000000 (pc 0x000000000000)`. gdb backtrace shows the crash inside `NullRenderer::ExecuteFrame` at `src/graphics/renderer/Graphics.Renderer.cpp:2303` (the default-recipe `graphicsContext.CopyTextureToBuffer(...)` call), and the equivalent MinimalDebug call site one block earlier (renderer line ~2269). Both call sites pass the explicit 10-argument form already mandated by HARDEN-072.
- Expected behavior: when the renderer's readback hook is armed under an operational MockDevice the per-frame `Present → TransferSrc → CopyTextureToBuffer → Present` triplet records on `MockCommandContext` and the per-recipe `*BackbufferReadbackCopyCount` counter increments to `1`, matching the cases the contract tests assert.
- Impact: GRAPHICS-076E cannot close — the CPU contract gate it lists as verification (`DefaultRecipeBackbufferReadbackContract`) is currently a SEGV. GRAPHICS-081 (MinimalDebug scaffold retirement) is also blocked because the equivalent MinimalDebug readback contract no longer passes either. The two failing tests are not listed in `ctest -N` output (they live in `IntrinsicGraphicsContractCpuTests`, registered by gtest discovery rather than as separate CTest targets), which masks the regression from the default-gate summary even though every direct invocation of the binary reproduces the crash.
- Root cause hypothesis: commit `cc06edef` ("Add default recipe readback seam") added a new non-pure virtual `BindFrameSampledTexture(TextureHandle)` with an inline `{ (void)texture; }` body to `RHI::ICommandContext` in `src/graphics/rhi/RHI.CommandContext.cppm`. Disassembly of the resulting `IntrinsicGraphicsContractCpuTests` shows `MockCommandContext`'s vtable only contains 26 virtual slots — the `BindFrameSampledTexture` slot is missing and `CopyTextureToBuffer` ends up at the slot the renderer's translation unit expects to hold `CopyBufferToTexture` (or past the end of the vtable, depending on the subclass). The renderer's TU sees the new virtual in its imported view of the interface, so it calls slot 26 for `CopyTextureToBuffer`, dereferencing a null function pointer.
- HARDEN-072 (commit `c7d5a762`) previously fixed a related clang-20 / C++23-modules vtable-mangling bug for the same `CopyTextureToBuffer` virtual when default arguments were present in the interface. This regression has the same shape but a different trigger (a new inline-bodied virtual inserted between `BindPipeline` and `BindIndexBuffer`).
- Affected `ICommandContext` subclasses (none currently override `BindFrameSampledTexture`): `tests/support/MockRHI.hpp::MockCommandContext`, `src/graphics/renderer/Backends/Null/Backends.Null.cpp::NullCommandContext`, and the `RecordingCommandContext` test doubles in `tests/contract/graphics/Test.PostProcessChainContract.cpp`, `Test.MinimalDebugSurfacePass.cpp`, `Test.SurfacePassContracts.cpp`, `Test.PresentPass.cpp`, `Test.LightingShadowContracts.cpp`, `Test.MinimalTriangleAcceptance.cpp`, `Test.MinimalRecipeBackendParity.cpp` (`TraceRecordingContext`), `Test.CullingPassContracts.cpp`, `Test.DebugViewContract.cpp`, `Test.MinimalDebugPresentPass.cpp`, `Test.SelectionPassContracts.cpp`, `Test.LinePointPassContracts.cpp`, `Test.ImGuiPresentContract.cpp`. The Vulkan backend (`src/graphics/vulkan/Backends.Vulkan.CommandPools.cppm` + `Backends.Vulkan.CommandContext.cpp`) already overrides `BindFrameSampledTexture` and is unaffected at runtime.
- Naive workaround attempts that did NOT resolve the SEGV (recorded so the next agent does not repeat them):
  1. Moving the inline bodies of `BindFrameSampledTexture` and `CopyTextureToBuffer` out of `RHI.CommandContext.cppm` into a new `RHI.CommandContext.cpp` module implementation unit (per AGENTS.md §5 and the HARDEN-072 mental model). The .cpp emits both symbols correctly and the linker resolves them, but the subclass vtables still omit the `BindFrameSampledTexture` slot under clang-20.
  2. Adding an explicit `void BindFrameSampledTexture(RHI::TextureHandle) override {}` to every subclass listed above. The build succeeded (`override` only compiles when the base virtual is visible, so the interface IS being seen by the test TUs), but it surfaced a *different* SEGV: the renderer's `Pass.PostProcess.ToneMap::Execute` call to `cmd.PushConstants(data, size, offset)` now lands on `MockCommandContext::PushConstants` with `data = 0x3, size = 1, offset = 0`, i.e. another vtable-slot mismatch shifted by one position elsewhere in the inheritance hierarchy. This suggests the bug is not purely "MockCommandContext misses one slot" but a more general clang-20 modules layout disagreement that needs deeper investigation (possibly extraction of a minimal repro for an upstream clang report).
- The currently-working set of `*BackbufferReadback*` tests is the four `DefaultStateIsDisabled...` / `NonOperationalDeviceSkipsCopy` / `DefaultRecipeIgnoresReadbackHandle` cases — these never enter the readback recording block, so they pass even with the broken vtable layout. Only the two `ConfiguredHandleRecordsReadbackTripletOnce` cases fail.

## Required changes
- [ ] Reproduce the SEGV on a fresh `cmake --preset ci` build and capture the gdb backtrace + `nm` output for `MockCommandContext`'s vtable so the fix can target the correct missing slot.
- [ ] Identify the minimal clang-20 / C++23 modules pattern that restores layout agreement between the `Extrinsic.RHI.CommandContext` exporter TU and importing TUs (test sources, Null backend, renderer). Candidate strategies, ordered by blast radius (smallest first):
    1. Add `RHI.CommandContext.cpp` module implementation unit + explicit `override` of `BindFrameSampledTexture` (and any other non-pure virtual that is added in the future) in **every** `ICommandContext` subclass under a shared macro / helper. (Attempted; surfaced a secondary slot mismatch — needs investigation, not abandonment.)
    2. Remove `BindFrameSampledTexture` from `RHI::ICommandContext` and replace the renderer's two call sites with a separate Vulkan-only opt-in capability surface (e.g. `RHI::IFrameSampledTextureSink` queried via `IDevice::QueryCommandContextCapability`).
    3. Revert the cc06edef hunk that introduced `BindFrameSampledTexture` and re-land it behind a different mechanism only after a minimal clang-20 repro is filed upstream.
- [ ] Land the chosen fix as a single semantic change (no mixed mechanical moves) with the regression test below pinning the contract.
- [ ] Cross-link this bug from `GRAPHICS-076E`, `GRAPHICS-081`, and the `cc06edef` follow-up note in `tasks/done/GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md` so the renderer-API/scaffold-retirement chain is aware of the blocker.

## Tests
- [ ] `tests/contract/graphics/Test.DefaultRecipeBackbufferReadback.cpp::ConfiguredHandleRecordsReadbackTripletOnce` passes on the default CPU gate.
- [ ] `tests/contract/graphics/Test.MinimalDebugBackbufferReadback.cpp::ConfiguredHandleRecordsReadbackTripletOnce` passes on the default CPU gate (regression confirmation; it currently fails too).
- [ ] Add a CTest-registered alias or label so a future regression in either `*BackbufferReadbackContract` suite is visible from a top-level `ctest -L 'contract' --output-on-failure` invocation rather than only from a direct `IntrinsicGraphicsContractCpuTests` binary run.
- [ ] Add a focused vtable-layout contract test (or extend `tests/contract/graphics/Test.RendererRhiBoundary.cpp`) that records the virtual-slot index for `CopyTextureToBuffer` and asserts it matches across one renderer-side caller and one mock subclass so a future inline-virtual insertion in `RHI.CommandContext.cppm` fails closed.

## Docs
- [ ] Update `src/graphics/rhi/README.md` (if one exists; otherwise the parent `src/graphics/README.md`) with a note about the clang-20 modules guidance: "do not add non-pure virtuals with inline bodies to `RHI::ICommandContext` between existing virtuals; declare them and place the body in `RHI.CommandContext.cpp`, and add an explicit `override` to every existing subclass in the same patch."
- [ ] Extend the HARDEN-072 done-task note in `tasks/done/HARDEN-072-rhi-surface-fixes-for-default-recipe-pipeline-bringup.md` with a back-reference pointing at this bug as a follow-on manifestation of the same upstream clang-20 issue.

## Acceptance criteria
- [ ] `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests` builds cleanly.
- [ ] `build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='*BackbufferReadbackContract*'` reports 8/8 passing (4 `MinimalDebug` + 4 `DefaultRecipe`).
- [ ] `ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` is green for the touched scope and reports an entry that would have caught the original regression.
- [ ] No new layering violations (`python3 tools/repo/check_layering.py --root src --strict`).

## Verification
```bash
set -o pipefail
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests 2>&1 | tee /tmp/bug013-build.log | tail -n 20
build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='*BackbufferReadbackContract*' 2>&1 | tee /tmp/bug013-tests.log | tail -n 40
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 2>&1 | tee /tmp/bug013-ctest.log | tail -n 40
```

## Forbidden changes
- Disabling AddressSanitizer, quarantining the failing tests, or labelling them `flaky-quarantine` to dodge the regression.
- Adding a `gpu`/`vulkan` label to the CPU contract tests to push them out of the default gate.
- Reverting GRAPHICS-076E's renderer-visible API surface (`SetDefaultRecipeBackbufferReadbackBuffer` + counter) instead of fixing the vtable layout.

