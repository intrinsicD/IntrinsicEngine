**Verdict: FAIL.** One finding blocks this. The benchmark baseline side is fine.

### Finding 1 (blocking): the "unchanged" fixture now fails, because BUG-091's baseline snapshot counts as already used

`tests/regression/tooling/Test.ValidateTasks.py:552-567`, `test_byte_identical_legacy_task_is_grandfathered`.

To reproduce (it only writes to a tempdir; the validator is unchanged from HEAD):
```
python3 tests/regression/tooling/Test.ValidateTasks.py \
  ValidateTasksTests.test_byte_identical_legacy_task_is_grandfathered \
  ValidateTasksTests.test_changed_legacy_task_must_enroll \
  ValidateTasksTests.test_promoted_legacy_task_must_enroll
```
```
FAIL: test_byte_identical_legacy_task_is_grandfathered ... line 567
AssertionError: 1 != 0 : [ERROR] tasks/backlog/bugs/BUG-091-...md: consumed legacy task
snapshots cannot be replayed; create or retain the enrolled successor instead.
Ran 3 tests — FAILED (failures=1)
```
Your own `tasks/evidence/BUG-213/task-regression.log` shows the same failure. It was written at 04:55:14, after the test edit at 04:55:02, and points at the current line 567. So it is a result of this fix, not a leftover from before it.

**Cause:** BUG-091 was enrolled on main in `fcf191b89`. Its old-version hash `6ace4339…` moved into the `consumed` section of `tools/agents/contract_legacy_tasks.json` and is no longer an unconsumed entry. The validator's check at `tools/agents/validate_tasks.py:710` rejects any file whose hash matches a consumed entry, and it does so before the grandfather check. So putting the old BUG-091 bytes back is a replay by design, and the validator is right to reject it.

**Background:** the original CI failure (`ci-docs-failure.log`) was only in the changed and promoted tests. The unchanged test passed before this change only because it copied the current enrolled file. It had quietly stopped testing grandfathering at all.

**Suggested fix:** don't use BUG-091 for the unchanged test. Pick an entry from `inventory["tasks"]` in `contract_legacy_tasks.json` that is still byte-identical on disk. `backlog/geometry/GEOM-013-feature-preserving-dual-contouring.md` qualifies today. Choosing the entry from the inventory at runtime would keep the test from breaking the next time a task gets enrolled. Optionally, add a separate test that a consumed snapshot is rejected, using the current BUG-091 case.

### Checks that pass
- **Historical baseline binding:** the working-tree manifest hashes to `962fb130…eac8`. That matches `manifest.sha256` in `benchmarks/baselines/geometry_uv_atlas_fast_staged_edge_grouping_scaling_8ca52438.json`, and the file is byte-identical to its version on main. `ci-timing-regression.log` shows 20 tests OK, and `baseline-validation.log` shows 7 files passing.
- **Runner still fixed to Angle:** `Bench_UvAtlasSmoke.cpp:251` `MakeOptions` sets `Distortion = UvAtlasDistortion::Angle` explicitly, overriding the library default of `Both`. The README wording is accurate.
- **Changed and promoted fixtures:** both now start from the old baseline bytes and fail with the expected "must declare `contract_schema: 1`" error, so they correctly test the enrollment rules again. None of the assertions changed.
- **No recorded results or validators changed:** the diff doesn't touch `benchmarks/baselines`, `benchmarks/reports` or `tools/agents`.

Remote CI is still pending, and the docs-validation job will hit Finding 1.
