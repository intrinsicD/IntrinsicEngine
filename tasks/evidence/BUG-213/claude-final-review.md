**PASS.** The final change does what my runtime-inventory suggestion asked for, and none of the checks got weaker.

- **Where the fixture comes from:** `UNCONSUMED_LEGACY_TASK` is the first key, in sorted order, of `contract_legacy_tasks.json["tasks"]`. That's the same mapping the validator reads for `expected_legacy_hash` (`validate_tasks.py:702`). The current pick is `backlog/geometry/GEOM-013-feature-preserving-dual-contouring.md`, and BUG-091 is no longer in `tasks`.
- **The bytes match the inventory:** the baseline blob at `577b458…` for GEOM-013 hashes to `96e0817f…`. That equals the inventory hash, so the grandfathered test really exercises the `expected_legacy_hash == actual_hash` path. The copy no longer depends on the working tree.
- **All three tests still test something real:**
  - *grandfathered:* the exact key and its frozen bytes go into a temp root. The stale-entry sweep at `:671` only runs against the real repo root, so the missing sibling entries can't cause errors here.
  - *changed:* the same bytes plus an appended edit give a hash mismatch, so it must enroll. The `contract_schema: 1` assertion is unchanged.
  - *promoted:* the task moves to `active/GEOM-013-….md`. That path isn't in `tasks`, and a promoted task isn't looked up in `consumed`, so it must enroll. The assertion is unchanged.
- **Failure modes are loud:** if the inventory is ever empty, the `next(iter(...))` lookup fails at import time. Sorting puts `active/` keys before `backlog/`, but the inventory only ever shrinks and has no `active/` entries today. So the promoted path can't collide with a legacy key.
- **Negative coverage kept:** `test_consumed_legacy_snapshot_cannot_be_replayed` (`:679`) still asserts the replay rejection.
- **Result:** the log shows `Ran 25 tests … OK`, which matches your claim.

One small note, not a finding: `copyfile` may now be unused in the test file. If so, it's only a lint concern.
