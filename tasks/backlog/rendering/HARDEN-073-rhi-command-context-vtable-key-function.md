# HARDEN-073 — Anchor RHI::ICommandContext vtable with an out-of-line key function

## Goal
- Give `Extrinsic::RHI::ICommandContext` (`src/graphics/rhi/RHI.CommandContext.cppm`) a single out-of-line key function by declaring the destructor in the `.cppm` and defining it — together with the currently-inline virtual bodies (`SubmitBarriers`, `BindFrameSampledTexture`, `CopyTextureToBuffer`) — in a new `RHI.CommandContext.cpp` module implementation unit, so the interface's non-trivial bodies live in the implementation unit per AGENTS.md §5 and the vtable is anchored to one TU.

## Non-goals
- Do not change the *semantics* or signatures of any `ICommandContext` virtual; this is a code-organization / module-hygiene move, not an API change.
- Do not add `override` declarations to the ~16 subclasses (the failed "override-everywhere" approach recorded in BUG-013 — it does not help and surfaced a secondary slot mismatch on an incremental tree).
- Do not re-pure-virtualize any defaulted virtual (would force every backend/mock to override; larger blast radius).
- Do not claim this fixes BUG-013. See `## Context` — it does **not** prevent the stale-BMI recurrence.

## Context
- Owning layer: `graphics/rhi` (exported module `Extrinsic.RHI.CommandContext`). Consumers: `graphics/renderer`, the Null and Vulkan backends, and ~16 test `RecordingCommandContext` / `MockCommandContext` doubles.
- Origin: deferred optional hardening recorded in `tasks/done/BUG-013-backbuffer-readback-contract-vtable-segv.md` (`## Non-goals`). BUG-013 closed as not-reproducible on a clean build; this task captures the structural option discussed in PR #946 review.
- **Honest value assessment (read before implementing):** this does **not** prevent the BUG-013 failure mode. BUG-013 was a vtable *slot-index* (offset) mismatch from stale module BMIs on an incremental tree. Slot indices are offsets, not symbols, so an out-of-line key function changes only *where the base vtable is emitted*, not the offsets a stale TU bakes into its call sites or its derived vtable — the silent runtime SEGV would recur identically on a stale tree. The authoritative prevention for that failure mode is the clean-rebuild rule documented in `src/graphics/rhi/README.md` (and AGENTS.md §7), not this refactor.
- The justified value is: (1) AGENTS.md §5 compliance — `SubmitBarriers` owns non-trivial control-flow/loops and belongs in a `.cpp` implementation unit, not inline in the interface; (2) defense against the *compiler-bug* variant of vtable divergence (the HARDEN-072 class, where the module-interface TU and importer TUs can disagree under clang-20 weak/COMDAT emission) by forcing a single authoritative emission; (3) reduced future layout churn when bodies move out of the interface.
- On a clean build the `ICommandContext` vtable is already a single module-owned symbol (`vtable for ...@Extrinsic.RHI.CommandContext`), so item (2) is speculative upside, not an observed defect today.

## Required changes
- [ ] In `RHI.CommandContext.cppm`, declare `virtual ~ICommandContext();` (remove the in-class `= default`) and declare `SubmitBarriers`, `BindFrameSampledTexture`, `CopyTextureToBuffer` without inline bodies. Keep all signatures and the export surface identical.
- [ ] Add `src/graphics/rhi/RHI.CommandContext.cpp` as a private module implementation unit of the `ExtrinsicRHI` module library (wire via `target_sources(... PRIVATE ...)` in `src/graphics/rhi/CMakeLists.txt`) defining the out-of-line destructor and the three moved virtual bodies (preserving the existing no-op / alignment-guarded `SubmitBarriers` logic verbatim).
- [ ] Confirm no subclass needs changes: the moved bodies remain inherited; do not touch the Null/Vulkan backends or any test double.

## Tests
- [ ] Full clean gate (not just the contract target): `rm -rf build/ci && cmake --preset ci && cmake --build --preset ci --target IntrinsicTests`, then the default CPU gate `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` stays green (pre-existing `IntrinsicBenchmarkSmoke.HalfedgeSmoke.{Run,Validate}` failures excepted, if still present).
- [ ] `build/ci/bin/IntrinsicGraphicsContractCpuTests` reports 225/225 (unchanged from the BUG-013 baseline) — proves no vtable-dispatch regression across the command-context subclasses.

## Docs
- [ ] Update `src/graphics/rhi/README.md` to note the bodies now live in `RHI.CommandContext.cpp` and that the destructor is the key function.
- [ ] If module-surface tooling flags it, refresh `docs/api/generated/module_inventory.md` via `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] `IntrinsicTests` builds cleanly under a fresh `ci` preset and the default CPU gate is green for touched scope.
- [ ] `IntrinsicGraphicsContractCpuTests` is 225/225.
- [ ] No `ICommandContext` signature/export change (diff is bodies-moved + dtor-declaration only).
- [ ] No new layering violations (`python3 tools/repo/check_layering.py --root src --strict`).

## Verification
```bash
set -o pipefail
rm -rf build/ci && cmake --preset ci
cmake --build --preset ci --target IntrinsicTests 2>&1 | tail -n 20
build/ci/bin/IntrinsicGraphicsContractCpuTests 2>&1 | tail -n 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 2>&1 | tail -n 20
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Editing any `ICommandContext` subclass (renderer, Null/Vulkan backend, or test double) — this task is interface-unit-local.
- Verifying on an incremental/stale build tree; the whole point is a clean-build proof.

## Maturity
- Target: `CPUContracted`. This is a CPU/null-gated module-hygiene change with no operational backend dimension; closing at `CPUContracted` is the endpoint and no `Operational` follow-up is owed.
