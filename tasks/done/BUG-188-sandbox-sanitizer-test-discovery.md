---
id: BUG-188
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive diagnosis of host test-discovery restrictions; no engine capability or performance claim.
contract_schema: 1
contracts: []
contract_review: Existing sanitizer verification and local execution policies apply; no new reusable engine contract.
---
# BUG-188 — Sandbox blocks LeakSanitizer during CTest discovery

## Goal
Give agent verification a deterministic route for sanitizer test discovery on
hosts whose filesystem sandbox prevents LeakSanitizer thread inspection.

## Evidence
- During RUNTIME-233, `ci-asan` configured and built `IntrinsicCpuTests` successfully.
  The unchanged full CPU CTest command then exited 8 during GoogleTest discovery
  of `IntrinsicRuntimeIntegrationTests`, before any test ran.
- LeakSanitizer reported that it cannot operate under the sandbox's ptrace
  restriction. Log: `/tmp/intrinsic-runtime233-20260912/ci-asan-test.log`.
- The same limitation was previously noted under BUG-180; it is distinct from
  that task's allocation-retention investigation.
- The identical command passed all 2,916 registered tests outside the sandbox, retaining the test
  selector, serial execution and sanitizer environment. Host retry log:
  `/tmp/intrinsic-runtime233-20260912/ci-asan-test-host.log`.

## Acceptance criteria
- [x] Record the identical host retry outcome and distinguish discovery failure from a sanitizer finding.
- [x] Document or route required discovery through a supported local execution context.
- [x] Preserve test selectors, dedicated leak controls and sanitizer settings; do not alter host ptrace policy or add suppressions to hide this limitation.

## Verification
```bash
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```
Run outside the restrictive filesystem sandbox when reproducing the host control.

## Reproduction — 2026-09-14
ASSETIO-012's focused `ci-asan` CTest selector again failed in discovery of the
unchanged `IntrinsicRuntimeIntegrationTests`, before executing any selected test.
The same 272-test selector passed with host access, with serial execution and
sanitizer settings unchanged. Logs: `focused-asan.log` and
`focused-asan-host.log` under
`build/analysis/assetio012-format-catalog-2026-09-14/`. Keep this workflow task
open; the host retry does not implement automatic supported-context routing.

## Reproduction — 2026-09-25
Scalar-gradient UI integration reproduced the same discovery-only failure in
`IntrinsicRuntimeIntegrationTests` after a successful fresh `ci-asan` configure
and `IntrinsicCpuTests` build. LeakSanitizer reported its ptrace restriction
before executing tests (`/tmp/scalar-gradient-asan-tests.log`). Host access allowed the unchanged serial CPU selector to complete the
3,310-registration run (`/tmp/scalar-gradient-asan-host-tests.log`), with no
sanitizer runtime findings. Two stale menu expectations and their compilation
metadata check required rebuilding the edited test binary; the final recheck is
recorded in `/tmp/scalar-gradient-asan-final-tests.log` (46/46 passed). No sanitizer flags,
suppressions, test labels or host ptrace policy were changed.

Property-smoothing verification on the same date reproduced the discovery
restriction (`/tmp/property-smoothing-asan-tests.log`). The unchanged serial
selector then passed all 3,322 registrations with host access
(`/tmp/property-smoothing-asan-host-tests.log`; two native-window checks skipped
in the Null/headless configuration). No sanitizer flags or suppressions changed.

## Resolution — 2026-10-24
Covers criterion 2 (documented route) and criterion 3 (nothing weakened). Raw logs are in
`tasks/evidence/BUG-188/` (git-ignored).

- **Cause.** The ASan test executables compile in `detect_leaks=1`
  (`tests/support/Test_SanitizerConfig.cpp`). PRE_TEST GoogleTest discovery
  (`gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)` in `tests/CMakeLists.txt`) runs
  `--gtest_list_tests` for every discovered executable that is newer than its cached list,
  whatever the `-R`/`-L` selection is. Discovery also runs outside the tests' `ENVIRONMENT`,
  which sets `ASAN_OPTIONS`/`LSAN_OPTIONS` `detect_leaks=0` for ordinary cases
  (`_intrinsic_test_env`). So ordinary test runs skip the exit-time leak check; discovery, and any
  process that enables leak detection, run it, and it needs ptrace. In the September sessions the sandbox blocked LeakSanitizer's ptrace
  attach; the logs show a namespaced PID (`==6==`) but do not isolate the mechanism.
  `GoogleTestAddTests.cmake` then raises a fatal error and `ctest` exits 8 before any test runs.
  Because the lists are cached per binary timestamp, the failure appears only after a rebuild.
- **This session's two contexts.** Both the default Bash context and the
  `dangerouslyDisableSandbox` context showed the same host namespaces (pid 4026531836,
  mnt 4026531841, user 4026531837), Seccomp 0, `yama/ptrace_scope` 1, and an allowed write
  outside the worktree. So **no sandbox isolation was active in this session; the restriction
  was not exercised**. In both contexts, a fresh discovery (`touch` of
  `IntrinsicRuntimeIntegrationTests`, after which its cached list was rewritten) with the
  unchanged selector `-LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
  --parallel 1 -N` exited 0 and listed 4101 tests.
- **Simulated restriction** (host, `strace -f -o /dev/null ctest --test-dir build/ci-asan -N
  -R '^CoreIndexedHeap\.PopsInNonDecreasingKeyOrder$'`), with predictions fixed beforehand:
  - `touch bin/IntrinsicCoreTests`: rc 8, `LeakSanitizer has encountered a fatal error`,
    `HINT: LeakSanitizer does not work under ptrace`, "Error running test executable". This
    matches the September signature.
  - Control, `touch` plus `ASAN_OPTIONS=detect_leaks=0`: rc 0. This confirms that the
    compiled-in leak check is the trigger. It is not documented as a route.
  - Control, cached lists: predicted rc 0, observed rc 8 on the first try. The reason was that
    `IntrinsicRuntimeContractTests` had been rebuilt minutes earlier, so its discovery ran under
    strace even though `-R` selected another executable. Once every list was fresh, the rerun
    gave rc 0. This shows that discovery is not limited by the selector.
- **Change (documentation).** A new `tests/README.md` §"Sanitizer discovery under ptrace
  restrictions" records the signature, the cause, the route and the host reproduction. The route
  is to rerun the unchanged command in a host context (Claude Code Bash `dangerouslyDisableSandbox`,
  or a Codex escalated command) and record both outcomes; it is not a sanitizer finding. The
  section forbids changing ASan/LSan options, suppressions, selectors, labels or the ptrace
  policy. One sentence and a link were added to the sanitizer bullet of `docs/agent/contract.md`,
  plus its synchronized skill mirror. The section holds the signature and lesson from the
  `/tmp` logs above, which will not survive.
- **Chosen default: documentation only.** A probe in `TEST_INCLUDE_FILES` was rejected because
  it would also fail cached sandbox runs that pass today. Discovery-time `detect_leaks=0` is a
  sanitizer-setting change and is left to the operator.
- **Unchanged:** selectors, labels, `__asan_default_options`/LSan options, suppressions and
  `kernel.yama.ptrace_scope`.
- **Verification:** `python3 tools/agents/sync_skills.py --write` then `--check` (13 files, in
  sync); `python3 tools/docs/check_doc_links.py --root .` (clean);
  `python3 tools/docs/check_docs_sync.py --root .` (satisfied);
  `python3 tools/agents/check_task_policy.py --root . --strict` (0 findings).
- **Remaining:** reviewer decision on criteria 2 and 3, then retirement by the integrator.

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `b0ffa3b66`, `abb88a4c1` (merged into the night integration
branch; not yet on `main`, hosted CI not run on them). They share a lane with BUG-240, which stays
open.
Reviewer decisions, both on `599f02ff1..abb88a4c1` (reviewed head `abb88a4c1`), round 1:
- Independent Claude review: approve, no blocking findings; criterion 1 "met" (ticked before the
  branch, from the September host retries), 2 "met", 3 "met".
- Independent Codex review: "VERDICT: approve" ("Approval covers this partial change, not BUG-240
  retirement"); 1 "**Met from existing recorded evidence.**", 2 "**Met.**", 3 "**Met.**".
Outcome: documented route, not automatic routing. `tests/README.md` §"Sanitizer discovery under
ptrace restrictions" records the signature, cause and host-context rerun; selectors, leak
controls, sanitizer settings, suppressions and ptrace policy are unchanged.
Limitations the reviewers named:
- This session's two execution contexts both had host access, so there is no fresh
  sandbox-versus-host reproduction. The mechanism was reproduced under `strace`; the route rests
  on the September host-retry evidence.
- The 2026-09-14 note asked for automatic supported-context routing; the docs-only default was
  chosen in the lane, and the operator may still want more (Claude).
- Claude, optional: the README's host-context rerun could say that it still goes through the
  session's normal permission approval, so it is not read as a standing authorization. Not changed.
No tracked evidence existed. The raw logs stay git-ignored in the `asan` lane worktree under
`tasks/evidence/BUG-188/`.
