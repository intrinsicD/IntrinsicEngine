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

- Status: done. Maturity: Retired (workflow-maintenance endpoint).
  Completed: 2026-09-22. Owner: Codex. Branch: `codex/proc-034-token-efficiency`.
  Final implementation commit: `043312937`; earlier slices:
  `901a2d564`, `80ceeac33`, `35b1f8140`;
  the enclosing retirement commit records lifecycle closure.
- Operator explicitly requested this process work outside the Framework24 P0
  selection focus on 2026-09-22, with four slices and immediate completion of
  slice 1 only; the operator subsequently authorized slices 2, 3, and 4, with
  retirement after all acceptance criteria are satisfied.
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
- [x] Slice 3: ordinary engineering avoids research-ledger loading while research
  work still records and validates required evidence.
- [x] Slice 4: reasoning choices are justified by matched completed-task results;
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

Slice 3 (research applicability and installed skill): run the structural commands
above, plus:

```bash
python3 /home/alex/.codex/skills/.system/skill-creator/scripts/quick_validate.py /home/alex/.codex/skills/research-manager
git -C /home/alex/.codex/skills/research-manager apply --reverse --check "$PWD/tools/agents/patches/research-manager-2.1.0-relevance.patch"
```

Before editing, compare the installed skill with its cached package source and
record provenance. Keep the package cache unchanged. Verify that the compact
entry point gates all recording/briefing reads, reference links resolve, and
the recording schemas, provenance rules, evidence bindings, and closure-signal
requirements survive the split. Exercise ordinary engineering, research results,
research affirmation, and explicit ARA maintenance/briefing cases; unrelated
work must not load ledgers just because `ara/` exists. Reload the skill catalog
through `skills/list` with `forceReload: true` and confirm the installed trigger.
Do not create research records for this workflow-maintenance turn.

The [portable local-skill patch](../../tools/agents/patches/research-manager-2.1.0-relevance.patch)
targets the research-manager skill from `@orchestra-research/ara-skills` 0.2.0
(skill metadata 2.1.0). To reproduce after reinstall, run `git apply --check`
and then `git apply` with the absolute patch path from the installed
research-manager directory. A newer upstream version requires reviewing/rebasing
the patch first. The reverse check above verifies the already-patched installation.

Slice 4: run the structural checks above. Replay the captured functional checks
without new model calls:

```bash
python3 - <<'PY'
import hashlib, json
from pathlib import Path
bundle = json.loads(Path('tasks/evidence/PROC-034/reasoning-trial.json').read_text())
source = bundle['evaluator_source']
assert hashlib.sha256(source.encode()).hexdigest() == bundle['evaluator_sha256']
# Load only scorer definitions; never execute the model-run loop during replay.
prefix = source.split('if len(sys.argv)>1', 1)[0]
prefix = prefix.replace("MANIFEST = json.loads((ROOT/'manifest.json').read_text())", 'MANIFEST = {}')
scope = {'__file__': '/tmp/proc-034-offline-replay.py'}
exec(compile(prefix, '<captured-scorer>', 'exec'), scope)
assert len(bundle['results']) == 8
for run in bundle['results']:
    for attempt in run['attempts']:
        assert scope['verify'](run['task'], attempt['source']) == attempt['score']['checks']
print('8 captured completions passed: 87 routine / 94 harder checks per run')
PY
python3 -c 'import pathlib, tomllib; p = pathlib.Path.home()/".codex/intrinsic-routine.config.toml"; assert tomllib.loads(p.read_text()) == {"model": "gpt-6-astra", "model_reasoning_effort": "medium"}'
```

For live profile validation, start `codex --profile intrinsic-routine --strict-config`
in this repository, confirm the status shows `gpt-6-astra medium`, then exit
without submitting a prompt. `app-server` does not accept this profile selector;
`debug prompt-input` ignores it and is not valid evidence of profile resolution.
The [measurement bundle](../evidence/PROC-034/reasoning-trial.json) retains exact
prompts, run order, commands, evaluator source, every candidate, usage, and checks.
Its runner can be extracted into a fresh temporary directory with the manifest
and workspace instructions to repeat the paid/model portion deliberately.

## Log

- Slice 4 evaluation plan (fixed before runs): GPT-6 Astra, requested service
  tier `default`, `medium` versus the current `xhigh`; two repetitions each of
  a routine path-normalization repair and a harder atomic graph transaction.
  Each run starts from an identical self-contained prompt and isolated context;
  effort order is counterbalanced. Fixed functional checks determine completion.
  Include all attempts, at most one repair attempt per run, input/cache/output
  tokens, elapsed time, defects, and tool calls. These small Python tasks do not
  establish C++/Vulkan or whole-repository efficiency. Retain deep effort outside
  the demonstrated scope. Reuse Codex CLI JSON telemetry and Python's standard
  library; targeted searches found no existing matched reasoning evaluator.

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
- 2026-09-22 — Slice 3 completed against pre-slice revision `80ceeac33`.
  The installed user-scope skill at `/home/alex/.codex/skills/research-manager`
  matched the cached `@orchestra-research/ara-skills` 0.2.0 package byte-for-byte
  (skill metadata 2.1.0; original `SKILL.md` SHA-256
  `9f17eea8da5bcc97e76466bcd3ca400ee6a65c8ff7a9b1a7853f7088ddb3e4cf`).
  Updated that installation to `2.1.0-local.1`; retained the package cache and
  installation metadata. The portable patch above reproduces all three skill
  files exactly from the cached baseline and passes a reverse check against the
  installed copy. No duplicate discoverable repository skill was added.
- The skill now decides relevance from conversation before references or ledger
  reads. Ordinary engineering skips silently; research events and explicit ARA
  work retain scoped recording, briefing, and validation. A clear contextual
  confirmation still qualifies. Required claim rows precede publication, and
  unrecorded engineering turns cannot establish topic abandonment. Repository
  policy and generated mirrors expose the same gate.
- Split the detailed recording procedure into an on-demand reference. The entry
  point decreased from 16,317 to 2,735 bytes (83.2%); this is source size, not
  measured token or credit savings. Exact comparisons preserved seven schema,
  crystallization, contradiction, stale-flagging, provenance, ID, and evidence
  sections; five local links resolve. Removed inherited unsupported frontmatter
  keys so skill validation passes. Codex's forced catalog reload finds one
  enabled user-scope skill with the new description.
- Independent skill forward tests used isolated synthetic fixtures: ordinary
  engineering with an existing ARA directory made zero ledger reads/writes;
  a research result recorded an experiment and promoted its observation with
  evidence; explicit pending-observation inspection read only staging and wrote
  nothing; contextual affirmation promoted the existing observation without
  duplicating the earlier experiment. YAML, provenance, bindings, and duplicate
  assertions passed. Reports: `/tmp/proc-034-forward-cdv2tul0/REPORT.md` and
  `/tmp/proc-034-forward-final-oybhp7ga/REPORT.md`.
- Resolved the forward review's ambiguous skipped-turn output and acknowledgment
  wording, then reran ordinary engineering and contextual affirmation against the
  final skill. The latter also obeyed a fixture-local repository proof-format
  rule; existing validator checks for claim fields, status, proof paths, and
  staging IDs passed. The minimal fixture has no claim dependencies or complete
  repository scaffold; full repository validation separately passed all 108
  existing claims. No actual repository research records changed.
- Task policy, all 4,037 relative doc links, docs sync, skill mirrors, session
  brief, Codex config, ARA structure, root hygiene, skill validation, patch
  round-trip, and whitespace checks passed. The four-point review found one
  workflow intent, unchanged engine layering, verified routing behavior, and
  synchronized docs/task state. Slice 4 remains open; reasoning defaults are
  unchanged.
- 2026-09-22 — Slice 4 completed against `35b1f8140`. The current thread and
  effective base configuration use GPT-6 Astra at `xhigh`, requested tier
  `default`; the runtime output limit remains 3,000. Compared `medium`/`xhigh`
  with identical user prompts, counterbalanced order, two repetitions of each
  task/effort, and one allowed repair attempt per run. All eight completions
  passed first try, with zero model tool calls and zero detected defects across
  87 normalization checks or 94 transaction checks per run. The task checks
  cover ordered/case-sensitive normalization, final-graph validation, revision
  checks, invalid inputs, atomicity, and independent result ownership.

  | Task / effort | Runs passed | Input tokens | Cached input | Output tokens | Reasoning subset | Total seconds |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: |
  | Routine / medium | 2/2 | 30,958 | 23,040 | 172 | 0 | 14.720 |
  | Routine / xhigh | 2/2 | 28,282 | 25,472 | 298 | 122 | 19.386 |
  | Harder / medium | 2/2 | 28,694 | 23,040 | 1,745 | 362 | 62.263 |
  | Harder / xhigh | 2/2 | 28,694 | 23,040 | 4,444 | 3,104 | 144.718 |

- These are aggregate CLI counters over verified completions, including all
  attempts; reasoning is already included in output. Medium used 42.3% fewer
  output tokens in the routine trial and 60.7% fewer in the harder trial.
  The harder trial's input/cache counts matched, giving 8.1% fewer raw
  input-plus-output tokens. No quality advantage for xhigh appeared in these
  checks. The first routine run had 2,676 extra input tokens of unknown origin,
  and routine caching differed; neither pair of routine runs establishes a
  clean total-cost comparison. Delivered tier and credit charges are unavailable.
  The trial itself consumed 123,287 input-plus-output tokens. Results do not
  establish general engine-task, subscription-credit, or end-to-end savings.
- Decision: offer `medium` for bounded routine work with explicit acceptance
  checks, retaining `xhigh` for unmeasured complex work and escalation. Installed
  the opt-in `/home/alex/.codex/intrinsic-routine.config.toml` profile and verified
  live Codex 0.153.4 startup reports `gpt-6-astra medium`, without a model turn.
  Base user defaults, requested service tier, project output limit, and existing
  thread effort remain unchanged. Removed the misleading unconditional
  `reasoning_effort: xhigh` workflow-metadata entry from `.codex/config.yaml`;
  it was not a runtime setting. The canonical workflow documents the selection,
  escalation, profile reconstruction, measurement limits, and official source;
  the core route and generated mirror expose it.
- All eight captured completions replayed successfully from the committed-data
  candidate; prompt hashes, aggregate counters, and profile contents matched.
  Structural task policy, all 4,040 relative doc links, docs sync, skill mirrors,
  session brief, Codex configuration, ARA structure, root hygiene, and whitespace
  checks passed. Four-point review: one workflow intent; no engine layering or
  source changes; measured behavior and installed profile verified; current
  docs/task state and explicit limits. No engine research claims were created.
- 2026-09-22 — Retired after all four slices met their acceptance criteria.
  Larger representative C++/Vulkan comparisons and credit-efficiency estimates
  remain unestablished limitations, not deferred acceptance work. No blanket
  runtime effort change is justified by this pilot; the installed routine
  profile is explicitly opt-in. No follow-up task is required for this endpoint.
