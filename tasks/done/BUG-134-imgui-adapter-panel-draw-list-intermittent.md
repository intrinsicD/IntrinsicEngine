---
id: BUG-134
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T00:37:32Z"
contract_schema: 1
contracts: [repo.source-documentation]
---
# BUG-134 — ImGui timing test retains an obsolete containment assertion

## Goal

- Replace the diagnosed obsolete callback-versus-EndFrame assertion in
  `ImGuiAdapter.EditorPanelDrawProducesNonEmptyDrawList` with deterministic
  phase-lifecycle coverage, preserving draw-data checks and the true render/copy
  containment assertions. Production timing behavior stays unchanged.

## Non-goals

- No quarantine, retry wrapper, weakened assertion, global timeout increase,
  or exclusion from the default CPU selector.
- No production ImGui behavior change without a failing-state diagnosis that
  demonstrates the adapter, rather than the test setup, is wrong.
- No coupling to the unrelated `RUNTIME-216` render-extraction forwarding
  deletion during which the failure was observed.

## Context

- Symptom: on 2026-08-06, the default CPU selector at implementation commit
  `7e61e215` reported only
  `ImGuiAdapter.EditorPanelDrawProducesNonEmptyDrawList` failed. The other
  4,101 executed tests passed and the environment-gated GLFW/LSan case
  skipped.
- Immediate evidence: the exact case then passed ten isolated
  `--repeat until-fail:10` executions, and an immediate complete selector
  rerun passed all 4,103 selected cases with the same expected skip. The
  failure is therefore intermittent and currently lacks a stable repro.
- Original expectation (later disproved for callback timing): the panel produces
  a non-empty ImGui draw list, and the measured EndFrame duration contains all
  measured phases. The corrected contract below retains only truly nested spans.
- Impact: a recurrence can make the required full CPU gate nondeterministic.
  The fresh `REVIEW-003` audit completed without recurrence, so that readiness
  gate retired cleanly and this bug remains an independent follow-up.
- **Recurrence captured 2026-08-09** during an unrelated `BUG-145` gate run, with
  the exact assertion this task asked to preserve. It is **not** the draw-list
  assertion the test is named for. Every draw-list, byte-count, and command
  assertion passed; the failure was
  `Test.ImGuiAdapter.cpp:298`,
  `EXPECT_GE(diag.LastEndFrameMicros, diag.LastEditorCallbackMicros)`, actual
  `12 vs 13`; an immediate complete selector rerun passed 4156/4156. The
  initial interpretation treated the callback as nested, which September source
  and history inspection disproved. At that time the candidates were timing instrumentation
  (independent clock reads, rounding at 1 µs granularity, or a phase timer that
  is not strictly nested inside the frame timer), not the ImGui frame lifecycle
  or draw-data translation. The three `LastEndFrameMicros >= <phase>` assertions
  at `:298-300` share the shape.
- The captured recurrence establishes the failed timer assertion, not its
  root cause. Another passing run does not resolve this defect.

## Recurrence — 2026-09-15

RUNTIME-253's complete CPU gate reproduced the exact assertion at line 298:
`LastEndFrameMicros` was 11 and `LastEditorCallbackMicros` was 12. Every draw-list
assertion passed. The full failing receipt and output remain in
`tasks/evidence/RUNTIME-253/commands/full-cpu.json` and its bound stdout log.
No ImGui adapter/test source was changed by that config-helper refactor.

Current source supplies a stronger diagnosis: `BuildEditorFrame()` measures the
callback before returning; `EndFrame()` starts its own timer afterward. Commit
`2785191443` (2026-07-19, editor UI module extraction) split those phases while
retaining the old containment assertion. Thus callback time is outside the
measured EndFrame interval. `LastImGuiRenderMicros` and `LastDrawDataCopyMicros`
remain genuinely nested in EndFrame and their containment assertions stay valid.
A separate scoped repair must replace the invalid cross-phase assertion with
phase-lifecycle coverage, keep draw-list and genuine nested-phase checks, and
provide a deterministic discriminator. Another passing complete run does not
close this bug. Claude independently confirmed the source/history diagnosis
and recommended exact counter/reset/unchanged-snapshot checks across
BeginFrame, BuildEditorFrame and EndFrame; no production fix is part of RUNTIME-253.

## Settled repair plan

Operator-authorized overnight work follows RUNTIME-253's local retirement and
seal (`b20c68f92`, `ed840a724`). Claim clean main first. The user authorized the
cleanup and necessary correctness fixes through 08:00 Berlin, 2026-09-15;
stop new implementation by 07:15 and commit locally without pushing. Claude
reviewed the fixed-source diagnosis and the replacement below. This is a test
contract correction; no graphics feature, timer implementation or API redesign.

Ranked explanations and current evidence:
1. Obsolete nesting expectation: predicts that callback work completes before
   EndFrame's timer starts. Current source and July history support this; exact
   phase snapshots/counters will check it without an elapsed-time bound.
2. Microsecond rounding of nested spans: cannot invert truly nested durations
   under the same monotonic clock and truncation. Render and copy remain nested;
   preserve both original assertions and investigate separately if either fails.
3. Stale callback diagnostics or duplicate work: predicts incorrect reset/count
   behavior across Begin/Build/End. The replacement checks that lifecycle directly.

One bounded slice in the existing test file:
- Keep the two-frame fixture and all draw-list, byte-count, command and texture
  assertions. Keep EndFrame >= ImGuiRender and EndFrame >= DrawDataCopy verbatim.
- Snapshot diagnostics after the warmup EndFrame, after the next BeginFrame,
  after BuildEditorFrame and after EndFrame. Begin resets callback timing; the
  render/copy/EndFrame timings retain the prior completed frame through Build.
- Check that the callback invocation count has reached two immediately after
  the second Build, capture/produced counts have not advanced yet, and EndFrame
  advances capture/produced counts while retaining the completed callback timing.
  The callback may record the counts it observes to make ordering explicit.
- Replace only the invalid sibling-duration inequality with these exact counter,
  equality and reset checks. Do not assert any time upper bound or relative speed
  between callback and EndFrame; no sleeps, fake clock seam or injected API.
- Backtest the new assertion using a temporary local mutation that postpones
  callback work from BuildEditorFrame into EndFrame. The after-Build invocation
  assertion must fail deterministically. Capture the patch/log, then restore the
  exact production source before final builds. Callback-observed capture count
  alone is insufficient: a callback moved before capture inside EndFrame can
  still see the previous count. The after-Build invocation count is decisive.

The original failure is already captured twice in this task and again in the
RUNTIME-253 receipt. This source/history diagnosis plus the deterministic phase
regression replaces a load-sensitive false invariant. It does not claim a clock
or renderer fix, and a passing rerun alone does not satisfy acceptance.

## Required changes
- [x] Add exact phase reset/retention/order assertions in the existing draw-list
  test, then replace only the diagnosed invalid callback containment comparison.
- [x] Preserve every draw-data and genuine nested-phase assertion; make no
  permanent production source change or new source file.
- [x] Claude reviews the fixed test diff and mutation interpretation; fix concrete
  findings and record the diagnosis against all observed failures/passes.

## Tests
- [x] Capture deterministic failure with callback work postponed until EndFrame;
  restore exact production source and rebuild before the passing check.
- [x] Exact case passes 1,000 repeats; ImGuiAdapter and editor-host tests pass.
- [x] Full CPU and focused isolated ASan/UBSan gates pass with unchanged selectors.

## Docs
- [x] Record the obsolete premise and replacement coverage, update bug index,
  retire and seal completed evidence. No runtime architecture change is needed.

## Acceptance criteria
- [x] Captured original failure is explained by disjoint phases; deterministic
  regression detects callback execution being moved back into EndFrame.
- [x] Current-phase checks and all retained rendering assertions pass, without
  quarantine, retry semantics, clock injection or timing upper bounds.
- [x] Final source restored, independently reviewed, verified and retired with
  strict task/layering/evidence checks and local commits.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci --output-on-failure -R '^ImGuiAdapter\.EditorPanelDrawProducesNonEmptyDrawList$' --repeat until-fail:1000 --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^(ImGuiAdapter|EditorUiHost)' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-asan --output-on-failure -R '^(ImGuiAdapter|EditorUiHost)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(ImGuiAdapter|EditorUiHost)' --no-tests=error --timeout 60 --parallel 1
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```
Use existing Clang 23 preset trees; run build/test variants sequentially because
fixtures share temporary paths. BUG-178 cache and BUG-195 disk limits remain.
Purposeful failing mutation receipts are historical evidence; final gates must
run on the restored source. No GPU execution change or capability promotion.

## Forbidden changes
- Simply deleting the old assertion without replacement phase coverage, or
  removing/weakening any true render/copy containment or draw-data assertion.
- Quarantine labels, retries in CI, relaxed timeouts, or production timer changes
  that manufacture the obsolete duration relationship.
- New source files, clock abstractions, or unrelated UI/runtime cleanup.

## Observation ledger — 2026-09-15 repair

1. **Prior failing loop:** RUNTIME-253 `full-cpu` receipt retains the original
   callback-versus-EndFrame failure (`11 vs 12`), with all draw-data checks passing.
   Earlier `12 vs 13` failures and intermittent passes fit disjoint intervals;
   their relative durations depend on the work and scheduling of each phase.
2. **Source/history discriminator:** July commit `2785191443` moved the callback
   into `BuildEditorFrame`, before EndFrame's start timestamp. This supports the
   obsolete nesting premise and rules out rounding of truly nested intervals as
   an explanation for this specific inequality. No production timing defect found.
3. **Mutation:** temporarily moved the unchanged callback body from Build to
   EndFrame, before capture. Predicted `afterBuild.EditorCallbackInvocations == 2`
   would fail. `mutation-regression` did fail with `1 vs 2`; callback retention also
   failed (`7 vs 0`). The latter alone is clock-resolution dependent, while the
   count mismatch is deterministic. Patch and raw exit-8 receipt are preserved.
4. **Restoration:** production source SHA-256 exactly matches the clean baseline
   (`restored-source.json`). Root shortened test comments after mutation without
   changing any assertion. The required final gates use this restored source.
5. **Focused verification:** `repeat-1000` completed 1,000 consecutive isolated
   executions; `focused-ci` passed all selected ImGui/editor-host cases. No retries
   were added to CI. Full CPU passed 4,632 executed cases with one expected
   unsanitized GLFW/LSan control skip; focused ASan and UBSan passed 30/30 each.
6. **Review/docs:** Claude approved the fixed final test diff. An initial structural
   run found a moved task link in the historical REVIEW-003 report; corrected the
   link without changing that report's historical finding. Raw failure receipt is
   retained; the final structural run is the completion gate.

Reuse decision: extend the existing panel-draw test and public diagnostic snapshot
seam. No new fixture, production helper, clock abstraction, source file or API.
The regression would have been prevented by checking phase ownership and ordering
when the callback phase was extracted, rather than retaining a duration relation.

## Completion
- Date: 2026-09-15.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/BUG-134/seal.yaml` binds the exact committed evidence.
- Endpoint: **Retired**, test-contract correction with CPU/ImGui and isolated
  sanitizer evidence. No renderer/backend capability or performance promotion.
- Four diagnostic snapshots replace the obsolete sibling-duration inequality.
  All draw-data checks and both true containment assertions remain intact.
  Production source and public APIs are unchanged; no added production file.
- Independent source review: `tasks/evidence/BUG-134/source-review.txt`.
- Mutation, restoration, command logs and final report are in
  `tasks/evidence/BUG-134/`. The mutation's optional exit-8 result is expected
  sensitivity evidence; it is not a failed final gate.
- No runtime defect remained after correcting the stale test premise. Existing
  compiler-cache, Vulkan leak, pacing and disk-headroom tasks remain separate.
