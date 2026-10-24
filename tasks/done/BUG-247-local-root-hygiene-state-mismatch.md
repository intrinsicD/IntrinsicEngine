---
id: BUG-247
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: environmental failure recorded during interactive HARDEN-091 verification; no implementation in this slice
contract_schema: 1
contracts: []
contract_review: Root ownership and local tool state do not change an engine API or integration contract.
---
# BUG-247 — Local root hygiene rejects existing tool state

## Goal
Track the environmental verification gap discovered while completing HARDEN-091,
without changing unrelated local directories or weakening the root policy.

## Acceptance criteria
- [x] Resolve ownership of the existing `.aws/` and `.serena/` directories with the operator before changing policy or local state. Operator decision 2026-10-24: ".serena und .aws nicht ins git".
- [x] The agreed local setup passes strict root hygiene and its existing regression tests without broad exclusions. Independent review of `4c30f2b82..972b7c8d9`: "met" (see Completion).

## Evidence and limits
On 2026-10-17 at `a13948cc5`, with the two intended formatter roots explicitly
registered, strict root hygiene reports only `.aws/` and `.serena/` as unexpected.
`Test.RootHygiene.py` passes 12 of 13 methods; its repository-root method fails
both checker subtests on those same directories. An isolated snapshot of all
maintained tracked/new files passes all 13 methods. This is a local setup issue;
no hosted-CI failure or engine behavior defect is inferred. Both directories are
preserved. Raw evidence: `tasks/evidence/BUG-247/root-hygiene.log`.

On 2026-10-24, in the main checkout at `599f02ff1`, the failure no longer
reproduces: `.aws/` is absent (its contents were never read), and `.serena/` is
an exact ignored local entry in `tools/repo/root_allowlist.yaml` since
`f849aafb1` (2026-10-23). Strict root hygiene exits 0 and `Test.RootHygiene.py`
passes 13 of 13. Nothing records whether `f849aafb1` was the operator's decision
for criterion 1 or how `.aws/` disappeared, so criterion 1 still awaits the
operator.

## Verification
```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tests/regression/tooling/Test.RootHygiene.py
```

## Operator decision — 2026-10-24

Verbatim (German): ".serena und .aws nicht ins git" (.serena and .aws not into git).
This is the criterion-1 decision.

Current state and the agent's reading (not part of the operator's words):
- `.serena/` is local, git-ignored (`.gitignore`) and classified as an ignored local
  entry in `tools/repo/root_allowlist.yaml`, as since `f849aafb1`.
- `.aws/` is absent and unclassified (neither in `.gitignore` nor in the allowlist),
  so strict root hygiene would fail if it reappeared.
- Open question put to the operator on 2026-10-24: should `.aws/` instead be listed
  in `.gitignore`? Not answered at retirement.
- How `.aws/` disappeared from the main checkout is unknown; the operator did not say.

## Completion — 2026-10-24
Completed 2026-10-24. Commit: the retirement commit on the night integration branch
`claude/night-2026-10-24-bug-sweep`; the policy state it relies on is `f849aafb1`. No
source or policy change.

Criterion 2: independent review of `4c30f2b82..972b7c8d9` (2026-10-24, relayed verbatim
by the coordinator; the record is outside the repository), decision verbatim:
"BUG-247 criterion 2 reproduces. I reran it read-only in /home/alex/Documents/IntrinsicEngine at 599f02ff1 with python3 -B, and git status was unchanged: .aws is absent and .serena is present; check_root_hygiene --strict exits 0; Test.RootHygiene.py: 13 tests OK. My decision on criterion 2: met."
The integrator's own earlier read-only rerun (`PYTHONDONTWRITEBYTECODE=1`, same checkout
and revision) gave the same results.

Evidence: the ignored file `tasks/evidence/BUG-247/root-hygiene.log` in the main checkout
stays; the main checkout is not touched by this retirement. The integration worktree has
none. Nothing was deleted.
