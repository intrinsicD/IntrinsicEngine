---
id: PROC-036
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: mechanical formatting work; the diff must be whitespace-only (verified by a token-equivalence check), evidence is that check, the build and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. A formatter configuration and format-only commits change no module surface, binding, publication, config or agent/UI control, and no catalog contract owns formatting.
---
# PROC-036 — Consistent C++ formatting

## Goal
“hat die implementierung einen durchgenden stil?”

Use the repository's majority style for changed C++ lines and the approved
14 outlier files, with semantic changes kept out of format-only commits.
The operator selected option 2 and on 2026-10-17 accepted the measured
MaterialSystem churn and preserved long string literals, then requested main/push.

## Acceptance criteria
- [x] Root `.clang-format` and conventions use validated clang-format 20.1.8, four spaces, Allman braces and a 120-column target.
- [x] Fourteen files in five families are token/directive-identical and formatter-idempotent, with no remaining `if(` or compressed statement blocks.
- [x] Five separate format-only commits are listed in `.git-blame-ignore-revs`; its header documents usage. Semantic changes are in separate commits.
- [x] Actual formatter sample, accepted limits, build/CPU evidence and independent review are recorded without overstating checks.

## Verification
```bash
clang-format --version
clang-format --dry-run --Werror <approved-family-files>
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/sync_skills.py --check
```
The already executed integrated `ci` build/full CPU run includes these exact
formatted source bytes. Git commits do not require repeating compilation.
Token comparison includes comments, literal spellings, macro adjacency and
preprocessor directive boundaries, with six negative controls.

## Accepted limits
The MaterialSystem comparison is −175/+134 lines, not near-zero; that sample
itself was not reformatted. The 14 changed files retain 34 string-literal lines
over 120 columns. All other code/comment-only lines meet the target. Preserving
literal spelling takes priority, as explicitly accepted by the operator.
No whole-tree formatting, shader formatting, new CI style gate or `.editorconfig`.

## Completion
Completed 2026-10-17. Commit: `d2dcac986`; the five format-only revisions are
listed in `.git-blame-ignore-revs`. Independent reviewer `/root/final_review`
accepted all four criteria, bound to final commits through `a13948cc5` and
closure file-set `8944e583b9c2b52e75df758a312145bc00e561074a732b0f5d673d50b0abcc60`.
See the canonical [completion and verification record](RETIREMENT-LOG.md#2026-10-17--src-workflow-follow-up).
