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
- [ ] Detect insufficient headroom before expensive local verification, with an actionable diagnosis.
- [ ] Preserve completed evidence and report failures to write result records explicitly.
- [ ] Document a conservative policy for reclaiming inactive generated artifacts.

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
- Docs: canonical [Disk headroom](../../../docs/build-troubleshooting.md#disk-headroom)
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
  ([summary](../../../docs/results/keypoint_compute_verification_2026-09-14.md)).
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

**What remains.**
- Tooling detection covers only `record-command`. Direct `cmake --build` /
  `ctest` runs and `touched_scope.py` rely on the documented manual `df` check.
  Whether interactive multi-preset builds should get a tooling preflight is an
  operator decision.
- `tools/ci/time_command.py` writes its CI timing JSON with a plain
  `write_text`; CI-only, and `aggregate_gate_timing.py` rejects malformed JSON.
