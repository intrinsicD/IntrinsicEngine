# WORKSHOP-004 — Replace renderer string routing with a typed command router

## Goal
- Move render command recording dispatch out of ad-hoc string comparisons inside the renderer and into a typed command-router layer keyed by frame-pass identity.

## Non-goals
- Do not implement missing pass command bodies in this task.
- Do not redesign `ICommandContext`.
- Do not change the default frame recipe's feature set.
- Do not remove the MinimalDebug scaffold unless a separate retirement task authorizes it.

## Context
- Current frame execution compares pass names like `CullingPass`, `DepthPrepass`, and MinimalDebug pass names to decide what command body to record.
- This is fragile because rename/debug-label changes can affect execution.
- WORKSHOP-003 provides typed pass identity; this task consumes it.

## Required changes
- [ ] Introduce a renderer-owned `RenderCommandRouter` module/class under `src/graphics/renderer/`.
- [ ] Register command recorders by typed pass ID.
- [ ] Move current command-recording branches for culling, depth prepass, MinimalDebug surface, and MinimalDebug present into router registrations.
- [ ] Preserve current skipped-status behavior:
  - `SkippedNonOperational` when device is missing/non-operational.
  - `SkippedUnavailable` when required pass resources are unavailable.
  - `Recorded` when command body records successfully.
- [ ] Make unknown/unimplemented pass IDs report structured `SkippedUnavailable` diagnostics rather than silently no-op.
- [ ] Ensure pass debug names are used only in stats/debug output, not as routing keys.
- [ ] Keep `RenderGraphCommandRecordStats` behavior equivalent or better.
- [ ] Add a narrow compatibility path only if some graph entries cannot yet carry typed IDs; document it as temporary with removal task.

## Tests
- [ ] Add contract tests proving command routing uses typed pass IDs, not debug string names.
- [ ] Add a test where a pass debug name changes but the typed ID remains the same and routing still succeeds.
- [ ] Add tests for unknown/unimplemented typed pass IDs producing `SkippedUnavailable` diagnostics.
- [ ] Update MinimalDebug and depth/culling pass contract tests to assert router behavior.
- [ ] Keep renderer frame lifecycle tests passing.

## Docs
- [ ] Update rendering architecture docs to describe the command router seam.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` graphics/renderer row to mention typed command routing.
- [ ] Update generated module inventory if public module surfaces changed.

## Acceptance criteria
- [ ] No command body in renderer execution is selected by comparing pass debug-name strings.
- [ ] Command status accounting remains visible per pass.
- [ ] Typed ID routing survives debug-name changes.
- [ ] CPU-supported graphics contract tests pass.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "contract|unit" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not add new string-routed passes.
- Do not hide unknown pass IDs as success.
- Do not remove skip diagnostics.
- Do not couple command routing to runtime or ECS.
