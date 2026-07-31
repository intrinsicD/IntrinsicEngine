# RUNTIME-200 diagnostic command observations

These byte-identical command receipts are retained outside the completion
command set because they are failed diagnostic attempts, not acceptance gates.
Their original stdout/stderr logs remain at the paths hash-bound inside each
receipt. Moving the receipt files preserves their recorded command, required
flag, exit code, timestamps, and output hashes; no receipt was rewritten.

- `ci-asan-full-cpu.json` records the first exit-8 ASan run that exposed two
  lifetime defects. `commands/ci-asan-lifetime-regressions-clean.json` and
  `commands/ci-asan-full-cpu-fixed.json` are the clean focused and complete
  passing acceptance receipts after the fix.
- `ci-vulkan-import-model-smoke.json` records the first exit-8 targeted run
  that exposed the generated-texture duplicate-upload race.
  `commands/ci-vulkan-import-model-smoke-fixed.json` is the passing targeted
  acceptance receipt after deterministic Ready-event completion and
  state-validated contention handling.
- `ci-vulkan-full-gate.json` records the extra 48-case sweep whose sole
  unrelated failure is tracked by `BUG-124`. The task-required four-case
  import/model selector is independently green in
  `commands/ci-vulkan-import-model-smoke-fixed.json`.
- `review-v2-asan-full-cpu.json` records a revised-surface serial ASan sweep
  whose sole exit-8 case was the already-tracked intermittent
  `RuntimeSceneLifecycle.RetiredQueuedSceneSavePublishesTerminalEvent` failure
  (`BUG-123`). The exact case then passed 20 consecutive direct ASan
  repetitions, while the independently receipted
  `review-v2-asan-bug123-repeat20.json` reproduced the same loss on its third
  iteration. This confirms the existing timing defect independently of the
  RUNTIME-200 asset-import surface; a separate complete passing sweep is the
  acceptance receipt.

The generated completion report references this directory as diagnostic
artifacts and includes every JSON receipt that remains in `commands/` without
post-generation filtering.
