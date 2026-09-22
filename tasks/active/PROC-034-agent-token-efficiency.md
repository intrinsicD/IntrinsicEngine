---
id: PROC-034
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive workflow maintenance; evidence is the diff, structural checks, and matched task measurements when available
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# PROC-034 — Reduce agent token consumption without weakening verification

## Goal

Reduce repeated instruction/context processing and unnecessary model calls while
preserving correctness, engineering contracts, and completed-task quality.

## Context

- Status: in-progress. Owner: Codex. Branch: `codex/proc-034-token-efficiency`.
- Operator explicitly requested this process work outside the Framework24 P0
  selection focus on 2026-09-22, with four slices and immediate completion of
  slice 1 only; the operator subsequently authorized slice 2. Slices 3–4 remain planned.
- The preceding local audit identified large repeated contexts as the leading
  candidate. Byte reductions are instruction-size measurements, not demonstrated
  token, credit, latency, or completed-task savings.
- Reuse the existing `AGENTS.md`, `docs/agent/contract.md`, session workflow,
  core skill router, and skill-mirror generator. Preserve existing contract
  ownership; do not introduce another instruction system or weaken gates.

## Slice plan

1. **Reduce mandatory instructions.** Keep essential invariants and scoped
   reading routes in `AGENTS.md`; retain detailed requirements in the existing
   expanded contract. Count current, complete instructions supplied in context
   as read; fetch only missing or changed sections. Stop requiring the entire
   session workflow at startup. Verify preservation, routes, and generated mirrors.
2. **Bound output and avoid redundant calls.** Prefer focused excerpts and
   failure summaries while retaining full logs on disk; batch independent reads
   and use completion/change-aware waits. Choose output limits with explicit
   access to omitted evidence. Preserve required tests and progress updates.
3. **Gate research bookkeeping before loading it.** Ordinary engineering work
   exits the applicability check before loading research ledgers; research work
   retains the existing claim/evidence requirements. Inspect the installed
   research-manager skill and its source/consumers before changing its trigger.
4. **Tune reasoning using completed-task evidence.** Evaluate routine versus
   difficult work at appropriate reasoning levels after the context changes.
   Compare matched verified tasks, including retries, defects, model, caching,
   and service tier; retain higher effort where it earns its cost. Record any
   effective config changes and distinguish proposals from measured savings.

## Acceptance criteria

- [x] Slice 1: root instructions fit comfortably below 32 KiB, detailed contract
  requirements remain reachable under explicit scope triggers, duplicate reads
  and the mandatory full workflow read are removed, and structural checks pass.
- [x] Slice 2: bounded output and fewer redundant calls preserve access to full
  evidence and required verification; representative task measurements recorded.
- [ ] Slice 3: ordinary engineering avoids research-ledger loading while research
  work still records and validates required evidence.
- [ ] Slice 4: reasoning choices are justified by matched completed-task results;
  any settings changed are validated and residual uncertainty is explicit.

## Verification

Slice 1 (documentation and routing only):

```bash
python3 tools/agents/sync_skills.py --write
python3 tools/agents/generate_session_brief.py
python3 tools/agents/sync_skills.py --check
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/agents/check_codex_config.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
wc -c AGENTS.md tools/agents/skills/intrinsicengine-core/SKILL.md
```

Review every relocated root section against the pre-slice Git revision, validate
the linked section anchors, and exercise routing for ordinary code, docs/task,
method, setup, and unattended scopes. Define concrete matched-run commands for
each later slice before starting it; do not use C++ builds as evidence for this
documentation-only slice.

Slice 2 (configuration and workflow): run the structural commands above, plus:

```bash
python3 -c 'import pathlib, tomllib; d = tomllib.loads(pathlib.Path(".codex/config.toml").read_text()); assert d == {"tool_output_token_limit": 3000}'
codex app-server generate-json-schema --out /tmp/proc-034-codex-schema
```

Use the generated `config/read` schema to read the effective repository limit
without starting a model turn. Execute a representative independent structural
verification bundle once, retain every full log, and compare its raw output size
with the bounded summary of the same results; record command/status equivalence
and presentation-call counts. Check the documented pipeline with a deliberate
nonzero producer exit and a diagnostic outside the tail, then recover that
diagnostic from the retained log. Exercise a running terminal handle with one
completion wait. These are output/transport checks, not matched end-to-end model
or credit benchmarks; slice 4 owns that comparison.

## Log

- 2026-09-22 — Slice 1 completed against pre-slice revision `a56574f44`.
  `AGENTS.md` decreased from 41,626 to 17,628 bytes. Mandatory startup sources
  (root + core skill + formerly required full workflow; excludes task text and
  harness skill metadata) decreased from 64,715 to 24,219 bytes, or 62.6%.
  This measures source bytes only; actual token/credit savings remain unmeasured.
- Preserved all 15 detailed root contract sections verbatim in the existing
  `docs/agent/contract.md`, including method/property binding, test/sanitizer/GPU
  gates, vcpkg setup, workflow custody, and standing Claude authorization. The
  root retains every section title, essential invariants, and explicit routes.
  Retained the expanded document's optional graph and focused-verification guidance.
- Reviewed ordinary code (coding + verification), docs/tasks (structural checks
  + task workflow), methods (method/property/integration + evidence), setup
  (shared setup), and unattended (execution + overnight workflow/custody) routes.
  All workflow section names and 63 local document links/anchors resolved.
- Task policy, doc links, docs sync, skill mirrors, generated session brief,
  Codex config, ARA structure, root hygiene, and whitespace checks passed.
  The review found no layer/compatibility/verification-policy changes. No engine
  code, user settings, output limits, research-manager triggers, or reasoning
  defaults changed. Slices 2–4 remain open in this task.
- 2026-09-22 — Slice 2 completed against pre-slice revision `901a2d564`.
  Added the repository runtime setting `.codex/config.toml` with
  `tool_output_token_limit = 3000`; Codex 0.153.4 accepted strict configuration
  and `config/read` returned 3000 with this project's `.codex` directory as its
  origin. The read used `cwd` equal to the repository and `includeLayers: true`,
  without starting a model turn. Existing loaded threads may need configuration
  reload; explicit per-call budgets can override the default.
- Reused shell `mktemp`/`tee`/`pipefail` and existing tool wait/orchestration APIs;
  no new execution wrapper or polling service. The canonical workflow now names
  output budgets, full-log/response retention, failure and truncation handling,
  independent batching, and completion/change-aware waits. Root/core links and
  generated mirrors expose the same procedure. Verification commands, gate
  selectors, write isolation, and progress-update requirements are preserved.
- Representative same-result presentation measurement: eight actual structural
  checks executed with four independent workers and returned in one batch, all
  exit 0. Full command output totaled 1,675 bytes; named exit-status/log-path
  summaries totaled 634 bytes (62.1% smaller). All eight full logs were retained
  under `/tmp/proc-034-verification-alv826hs/`; `results.json` records commands,
  statuses, sizes, and timings. This is a local presentation comparison, not a
  before/after model task or token/credit savings claim.
- The documented pipeline preserved an intentional producer exit of 23 and
  complete stdout/stderr: 2,149 raw bytes versus a 660-byte, 60-line tail. The
  first diagnostic was outside the excerpt and was recovered from the full log
  (`/tmp/proc-034-output-probe-n0mshmum/`). This was an expected probe, not a
  failing repository check. The real doc-link checker used one launch and one
  completion wait on its existing terminal handle; all 4,031 links passed.
- All applicable structural/configuration checks and the four-point review
  passed. No reasoning defaults, global settings, or research-manager behavior
  changed. Slices 3–4 remain open.
