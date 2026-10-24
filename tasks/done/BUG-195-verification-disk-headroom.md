---
id: BUG-195
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Host verification reliability incident; no performance or capability claim.
contract_schema: 1
contracts: []
contract_review: Existing build and verification hygiene applies; no new engine contract.
---
# BUG-195 — Verification can exhaust host disk space

## Goal
Add actionable disk-headroom handling to local multi-preset verification so
builds cannot silently lose test logs and result records when the volume fills.
Do not delete source, datasets, active build artifacts or diagnostic records.

## Evidence
RUNTIME-247 on 2026-09-14 filled the 1.8 TB root volume while reconciling native,
ASan, UBSan and Vulkan builds. The sandbox could not create its temporary mount
(`No space left on device`). CPU CTest and UBSan build wrappers could not write
their JSON completion records; the full CPU log and benchmark JSON were truncated.
Those interrupted/incomplete records are not accepted as passing evidence.
Logs: `/tmp/intrinsic-keypoint-vulkan-20260914/` (`full-cpu.log`,
`reconcile-ubsan.log`, `raw-benchmark/keypoint-compute.json`).

No build processes remained when cleanup was inspected. Removing only compiled
objects/libraries and ELF executables from inactive `ci-no-ccache`, `ci-clang20`,
`ci-coretasks-clean` and `ci007-interface-no-ccache` trees reclaimed about 5 GiB.
Their configuration/logs, all source and all datasets were retained. Required
verification must be rerun with complete output; cleanup does not close the
missing preflight/recovery behavior.

## Acceptance criteria
- [x] Detect insufficient headroom before expensive local verification, with an actionable diagnosis.
- [x] Preserve completed evidence and report failures to write result records explicitly.
- [x] Document a conservative policy for reclaiming inactive generated artifacts.

Ticked from the criterion-1 follow-up review of `cb816e606` (verdict
`approve`; structured verdict relayed by the night workflow coordinator, no
transcript on disk), which decided all three criteria `met`. See Completion.

## Verification
Reproduce a write failure on a bounded temporary filesystem or injected writer,
without deliberately filling the host volume. Check that the runner reports
an incomplete evidence record and retains earlier completed output. Re-run the
original native/sanitizer selectors with sufficient headroom; do not weaken them.

## Resolution — 2026-10-24

**Root cause.** `tools/agents/workflow_evidence.py record-command` is the only
repository runner that writes JSON receipts for local verification commands.
It checked no free space, and it wrote the stdout log, the stderr log and the
receipt with `_atomic_write` outside any `OSError` handling. Under a write
failure (reproduced with RLIMIT_FSIZE=256 B and SIGXFSZ ignored; no volume
filled, no mount) it exited 1 with a traceback: either the receipt was missing
and the command's real exit 0 was hidden behind a code that looks like a
failing command, or a large stdout log failed first and the captured output was
lost entirely (no files kept). On a real ENOSPC the same `OSError` path applies
(inferred, not reproduced). RUNTIME-247's own wrappers were ad hoc scripts, not
`record-command`, so this preflight would not have caught that incident; the
manual check for direct `cmake`/`ctest` runs is documented instead (below).
`tools/ci/touched_scope.py` also writes route JSON after a build or test, and
its `write_route` has no `OSError` handling either. It is out of scope: a
failed write still exits nonzero with a traceback, and `.tmp`+replace keeps
the prior route intact. `tools/ci/run_source_coverage.py` already reports
`OSError` explicitly.

**Change.**
- `record-command` refuses before running (exit 2, `ERROR: insufficient disk
  headroom ...`, nothing run or written) when the repository volume has less
  than `--min-free-gib` available. The message gives free and required GiB, the
  path, the override and the reclaim doc. Chosen default:
  `DEFAULT_MIN_FREE_GIB = 20`, a floor that catches a nearly full volume, not a
  measured footprint; `0` or a negative value disables it. Only the repository
  volume is checked (`shortcut:` comment).
- Log and receipt writes stop at the first `OSError`. Files already written
  stay. Any unwritten log is printed to its own stream, and the tool exits 2
  with `incomplete command evidence for <label>: could not write <path>
  (<err>); the command exited <rc>; kept <files|nothing>; ... This is not a
  receipt`. No receipt is ever written without both logs. Each log is printed
  under its own `OSError` guard, so an unwritable tool stdout cannot drop the
  captured stderr; a log that cannot be printed either is named in the message
  (`could not print <log> either`) instead of claiming it was printed. Python's
  failed final flush on that stdout then makes the exit 120, still nonzero.
- Docs: canonical [Disk headroom](../../docs/build-troubleshooting.md#disk-headroom)
  section (manual `df` check, the floor and its limits, a run with a failed
  write is never a pass, manual-only reclaim policy for inactive generated
  artifacts). Pointers from `docs/agent/contract.md` §"Verification hygiene"
  and `docs/agent/workflow-evidence.md` §"Completion evidence"; skill mirrors
  regenerated.

**Verification (this session).**
- New tests in `tests/regression/tooling/Test.WorkflowEvidence.py`
  (`RecordCommandResourceTests`): headroom refusal with faked
  `shutil.disk_usage` (floor − 1 B refuses and does not run; exactly the floor
  and `--min-free-gib 0` run), unwritable receipt (exit 2, `exited 0`, both logs
  kept, no receipt or temp file), unwritable stdout log (exit 2, all 4000
  bytes on stdout, `kept nothing`, empty `commands/`). Existing fixtures pass
  `--min-free-gib 0` so CI runner free space cannot change their outcome.
- Before the fix: 3/3 new tests fail (AttributeError on the constant; argparse
  rejects the flag). After: `python3 tests/regression/tooling/Test.WorkflowEvidence.py`
  → 41 tests OK (baseline 38 OK).
- Mutations with the fix in place, each restored afterwards: headroom `if`
  disabled → headroom test fails (`ValueError not raised`); `OSError` handling
  removed → both write-failure tests fail (`1 != 2`, traceback); log dump
  disabled → stdout-log test fails.
- Reproduction rerun after the fix: receipt case and stdout-log case both exit
  2 with the explicit message; the 4000 bytes reach stdout.
- `sync_skills.py --check`, `check_doc_links.py --root . --strict`,
  `check_task_policy.py --root . --strict`, `workflow_evidence.py validate
  --root .`, `check_root_hygiene.py --root . --strict` pass.
- "Re-run the original native/sanitizer selectors": RUNTIME-247 later reran
  full CPU, ASan and UBSan selectors with complete output, all passing
  ([summary](../../docs/results/keypoint_compute_verification_2026-09-14.md)).
  Those reruns predate this fix: they re-establish RUNTIME-247's evidence and
  do not exercise the preflight. No selector was weakened.

**Fix round 1 (both independent reviews: one stdout failure skipped the stderr dump).**
- Regression `test_unprintable_stdout_does_not_drop_captured_stderr`: every
  write fails with ENOSPC, the tool's stdout raises ENOSPC, its stderr is
  writable. Predictions written beforehand: the child's stderr reaches the tool's
  stderr byte for byte, the message says `exited 0` and
  `could not print .../lost.stdout.log`, and does not say `printed above`. On f8eddf657 the test failed
  (`b'' != b'CHILD DIAGNOSTIC\n'`); after the fix the full suite passes,
  42 tests OK. Mutation: dropping the lost-log report makes the message
  assertion fail; code restored.
- End-to-end double fault (tool stdout redirected to a file under
  RLIMIT_FSIZE=256 B, stderr a pipe, the command prints 4000 B to stdout and a
  stderr marker): f8eddf657 gives exit 120, marker lost, message says
  "printed above"; after the fix it gives exit 120, marker kept, and the
  message names the unprinted stdout log, with no traceback.
- Dropped the test's redundant `SIGXFSZ` ignore. CPython already ignores it
  at startup, and the suite still passes without it.
- Untested by design: the stderr log failing after the stdout log was
  written (index 1). It runs the same loop as the tested cases.

**Option B — 2026-10-24 (both independent reviews rated criterion 1 partially
met: the local router `touched_scope.py --run` had no preflight).**
- `tools/ci/touched_scope.py --run` now checks free space before its first
  step (plan, structural, configure, build, test). Below the floor it prints
  `touched_scope: error: insufficient disk headroom: <free> GiB free on the
  volume of <dir> (build directory <build-dir>), <N> GiB required; nothing
  was run. Reclaim space per docs/build-troubleshooting.md#disk-headroom, or
  pass --min-free-gib N ...` and exits 3. Exit 2 is already its route error.
  The volume is that of `--build-dir`. A tree that does not exist yet, as
  before a fresh configure, is checked on its nearest existing parent.
- Same default and flag as `record-command`: `DEFAULT_MIN_FREE_GIB = 20`,
  `--min-free-gib`, `0` or negative disables. The value is restated with a
  comment, not imported. Importing it would make the CI planner load
  `tools/agents/workflow_evidence.py` and `validate_tasks`. The test reads the
  floor from `workflow_evidence`, so a drifted default fails it.
- Only `--run` is gated. CI calls the separate `--action` steps, so hosted
  runners are not affected.
- Test `test_run_refuses_below_record_command_headroom_floor` in
  `tests/regression/tooling/Test.TouchedScope.py` fakes `shutil.disk_usage`
  and stubs the plan step (sentinel 7) and `_run_command`. It covers three
  cases: floor − 1 B refuses with exit 3, the message parts above, and no step
  run; exactly the floor runs; `--min-free-gib 0` with 0 B free runs. In all
  three, `disk_usage` gets the existing parent of the missing build directory.
  Predictions were written as assertions before the first run.
- Before the fix (6577e4cf6 tool): the test fails (`7 != 3`, `disk_usage`
  called 0 times, argparse rejects `--min-free-gib`). After: the test passes,
  and `python3 tests/regression/tooling/Test.TouchedScope.py` gives 38 tests
  OK (37 at baseline). `Test.WorkflowEvidence.py` still gives 42 OK.
- Mutations, each restored afterwards: headroom `if` disabled → `7 != 3`;
  default changed to 21 → `3 != 7`; `disk_usage(build_dir)` without the
  parent walk → `expected call not found`.
- Real path, no mock: `touched_scope.py --root . --changed-file
  docs/build-troubleshooting.md --output /tmp/bug195-route.json --build-dir
  build/never-created/ci-fast --min-free-gib 999999 --run` exits 3 with the
  message above, naming the volume of the existing `build/`. Neither the route
  file nor the build directory was created.
- Not run: a real `--run` with configure/build. C++ builds were held back on
  this host tonight. The refusal path returns before any subprocess.
- Structural checks pass: `check_doc_links.py --root . --strict`,
  `check_task_policy.py --root . --strict`, `check_task_state_links.py --root
  . --strict`, `generate_session_brief.py --check`, `sync_skills.py --check`,
  `workflow_evidence.py validate --root .`, `check_root_hygiene.py --root .
  --strict`.

**Relative build directory — 2026-10-24 (independent Codex review of
f21ba9425, P2: a relative `--build-dir` was resolved against the caller's cwd,
while configure and build run with cwd=root).** The preflight now resolves
`--build-dir` under `--root`; absolute paths are unchanged. New test
`test_run_checks_relative_build_dir_under_root_not_cwd` runs from another cwd
with the default relative build directory, no space under root and ample
elsewhere. It failed before the fix (`7 != 3`) and passes after; the suite
gives 39 OK. Real path from `/tmp` with `--root <worktree> --min-free-gib
999999 --run` exits 3 and names `<worktree>/build`. Only the build-dir volume
is checked; that limit is now commented in code and documented.

**What remains.**
- Tooling detection covers `record-command` and `touched_scope.py --run`.
  Direct `cmake --build` / `ctest` runs and the separate `touched_scope.py
  --action` steps rely on the documented manual `df` check. Whether more
  entry points should get a preflight is an operator decision.
- `tools/ci/time_command.py` writes its CI timing JSON with a plain
  `write_text`; CI-only, and `aggregate_gate_timing.py` rejects malformed JSON.

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `cb816e606` (head), with `f8eddf657`, `6577e4cf6`
and `f21ba9425`, merged into the night integration branch
`claude/night-2026-10-24-bug-sweep`; not yet on `main`, and hosted CI has not
run on them.
Reviewer decisions, base `599f02ff1`:
- Round 1 on `f8eddf657`: independent Claude review `approve` (criterion 1
  `partially met`, 2 and 3 `met`; the native/sanitizer rerun line of
  Verification `partially met`); independent Codex review (thread
  `01a16b2d-a6f9-7880-abf9-56a3715fbf08`) "VERDICT: revise required"
  (criterion 1 met within the documented coverage, 2 partially met, 3 met).
  Fixed in `6577e4cf6`.
- Round 2 on `6577e4cf6`: independent Claude review `approve` (criterion 1
  `partially met`, 2 and 3 `met`). Option B (`touched_scope.py --run`
  preflight) followed in `f21ba9425`.
- On `f21ba9425`: independent Codex review (thread
  `01a16b75-874d-7032-8254-f3194a783b24`) "VERDICT: revise required"
  (criterion 1 partially met: a relative `--build-dir` was resolved against
  the caller's cwd; 2 and 3 met). Fixed in `cb816e606`.
- Criterion-1 follow-up on `cb816e606`: `approve`, all three criteria `met`
  ("tools/agents/workflow_evidence.py is unchanged between f21ba9425 and
  cb816e606, so the prior 'met' assessment still holds"; the reclaim policy
  "has not changed"). The boxes above are ticked from this decision.
Limitations, not changed here:
- Not run: a real `touched_scope.py --run` with configure/build (C++ builds
  were held back on this host), and the native/sanitizer selectors after the
  fix; RUNTIME-247's earlier reruns predate it.
- Direct `cmake`/`ctest` runs and `touched_scope.py --action` steps rely on the
  documented manual `df` check; whether more entry points get a preflight is
  an operator decision (What remains).
No tracked evidence existed. The raw logs stay git-ignored in the `bug-195`
lane worktree under `tasks/evidence/BUG-195/`.
