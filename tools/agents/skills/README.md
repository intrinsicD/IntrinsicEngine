# IntrinsicEngine Agent Skills

This folder contains nineteen [Agent Skills](https://agentskills.io). Six of
them wrap the IntrinsicEngine `AGENTS.md` contract and the procedure docs
under `docs/agent/`; ten add IntrinsicEngine-specific cross-cutting workflow
disciplines (diagnosis, Vulkan frame triage, GPU smoke authoring, stale-build
triage, import-visibility contract, geometry-IO format, sandbox input/lifecycle,
research ideation, zoom-out, handoff) that don't have a single source doc under
`docs/agent/`; three are imported third-party productivity skills.
The skills are designed to load *progressively*: only the metadata (~100 tokens
per skill) sits in context by default, with full content loaded only when the
skill's trigger description matches the current task.

## Why these skills exist

The IntrinsicEngine repo already encodes its agent workflow extensively in
`AGENTS.md` plus the expanded procedure docs under `docs/agent/`. That works,
but relies on each agent session remembering to read `AGENTS.md` and follow
its routing table.

Packaging the same content as Agent Skills gives three additional benefits:

1. **Automatic triggering.** Each skill's `description` field acts as a
   relevance filter — agents pick up the right procedure based on what they're
   doing, without needing a human to point them at the right file.
2. **Progressive disclosure.** Specialist procedures (method work, benchmarking)
   don't pollute the context window unless they're relevant.
3. **Cross-tool portability.** Skills work in Claude Code, Codex, Cursor,
   Gemini CLI, and any other harness that supports the `agentskills.io`
   standard.

## The skills

### Source-doc mirrors (six)

| Skill | Wraps | Triggers on |
| --- | --- | --- |
| `intrinsicengine-core` | `AGENTS.md` + `docs/agent/contract.md` + `docs/agent/roles.md` + `docs/agent/prompt/prompt.md` | Any work in this repo — always-on master |
| `intrinsicengine-task-workflow` | `docs/agent/task-format.md` + `docs/agent/task-maturity.md` + `tasks/templates/task.md` | Creating, promoting, retiring, or materially editing task files |
| `intrinsicengine-review` | `docs/agent/review-checklist.md` + `docs/agent/architecture-review-checklist.md` + `docs/agent/agent-output-review-checklist.md` + `docs/agent/clean-workshop-review.md` + `docs/agent/drift-audit-checklist.md` | Pre-commit, PR review, architecture-impacting changes, weekly audit, drift audit |
| `intrinsicengine-method` | `docs/agent/method-workflow.md` + `docs/agent/method-review-checklist.md` | Paper/method implementation under `methods/` |
| `intrinsicengine-benchmark` | `docs/agent/benchmark-workflow.md` + `docs/agent/benchmark-review-checklist.md` | Benchmark manifests, runners, performance claims |
| `intrinsicengine-docs-sync` | `docs/agent/docs-sync-policy.md` | File moves, API surface changes, inventory regeneration |

Together these cover all 14 of the `docs/agent/*.md` files plus `AGENTS.md`
itself, the session-onboarding prompt under `docs/agent/prompt/prompt.md`, and
the task template at `tasks/templates/task.md`. The authoritative
source-to-mirror mapping is `REFERENCE_MAP` in `tools/agents/sync_skills.py`;
if that map and this table disagree, the map wins.

### Cross-cutting workflow disciplines (ten)

These skills do not wrap a single source doc under `docs/agent/`. They encode
disciplines that apply across multiple touched scopes. The skill body is
authoritative for these.

| Skill | Purpose | Triggers on |
| --- | --- | --- |
| `intrinsicengine-diagnose` | Disciplined diagnosis loop: feedback-loop → reproduce → rank hypotheses → instrument with tagged probes → fix → regression-test → cleanup. Adapted with C++/Vulkan tooling (ctest labels, validation layers, RenderDoc, backend differentials, benchmark baselines). | "diagnose this", crashes, validation-layer errors, CPU/null gate failures, reference-vs-optimized parity mismatches, benchmark regressions |
| `intrinsicengine-vulkan-frame-triage` | Domain playbook for wrong-frame-content defects on the promoted Vulkan path: validation-first triage, per-stage readback bisection, and the engine invariants (bindless bridge slot ownership, render-id conventions, integer clears, QFOT pairing, Y-flip) that past bugs re-derived repeatedly. | black frame, black readback, VUID cascade, driver crash in `vkCmd*`, descriptor/bindless anomaly |
| `intrinsicengine-gpu-smoke-authoring` | House pattern for opt-in `gpu;vulkan` readback smoke tests: label policy, skip-vs-fail discipline, pixel-sampling idioms, `ci-vulkan` incantations, and when a fix owes an `Operational` smoke follow-up. | adding/changing gpu/vulkan-labeled tests, proving a fix `Operational`, readback assertions |
| `intrinsicengine-stale-build-triage` | Rule out stale C++23-module/ccache artifacts (BMIs) before diagnosing any unexplained SEGV/ASan/vtable/ICE failure; clean-rebuild ladder and staleness signatures. | unexplained SEGV after module changes, PC=0x0 dispatch, "not reproducible", ccache/ICE anomalies |
| `intrinsicengine-import-visibility-contract` | Acceptance checklist so a "successful" asset import is actually visible and selectable in the sandbox: reference-triangle component parity, count-matched normals/UVs never dropped or overwritten, culling bounds + camera focus for off-origin geometry, non-blocking derived post-processing, and never-silent import logging. Each item cites its evidencing retired bug. | adding/changing an import/materialization path, import "succeeds" but nothing renders or is pickable, dropped normals/UVs, off-origin/culled geometry, silent drop failure |
| `intrinsicengine-geometry-io-format` | The proven `geometry -> core` importer/exporter slice template distilled from the ~35-slice GEOIO-002 series: anonymous-namespace parsers behind an unchanged `.cppm`, `Core::Expected` readers / `*IOWriteStatus` writers, untrusted-header-count validation bounded against the payload, and `unit;geometry` round-trip / determinism / fail-closed tests with committed or byte-level fixtures. | adding/changing an OBJ/OFF/PLY/STL/PCD/XYZ/TGF importer or exporter, parsing an untrusted header count or binary body, IO diagnostics/fixtures, format-slice closure wording |
| `intrinsicengine-sandbox-input-lifecycle` | Runtime frame-loop wiring pitfalls that regressed repeatedly: ImGui capture gating camera/gizmo/pick input, window-close routing + re-check-before-render + idle-wait-before-GPU-teardown, the pre-render transform flush for post-fixed-step edits, no blocking decode on the poll thread, and camera/cursor sign + HiDPI conventions. Each pitfall cites its evidencing retired bug. | `Engine::RunFrame` ordering, PollEvents/window-close/exit, ImGui capture gating, click-pick/gizmo drive, camera sign conventions, drag-drop on the poll thread, shutdown/device-idle teardown |
| `intrinsicengine-research-ideation` | Generates and adversarially audits a diverse, falsifiable research portfolio (recombination + assumption surgery + primitive invention + new-evidence programs + cross-domain mechanism transfer), with an operational novelty taxonomy, a prior-art audit, and a cheapest-killing-experiment per candidate. Never claims absolute novelty; ideation only — a selected candidate enters the Theme I / method workflow. First-party (MIT, A. Dieckmann), adapted from `transformational-research-skill-kit` v1.0.0; carries hand-authored `references/`, `assets/`, `evals/`, and `scripts/` companion files. | proposing novel/unconventional/cross-domain/transformational/publishable research directions, research roadmaps, high-risk/high-reward experiments — not ordinary feature brainstorming or implementing an already-chosen method |
| `intrinsicengine-zoom-out` | One-shot layer-cake map of an unfamiliar file using the engine's domain vocabulary (`core`/`geometry`/`assets`/`ecs`/`physics`/`graphics/*`/`runtime`/`app`/`methods`) and `.cppm` module surfaces. | user-invoked only (`/intrinsicengine-zoom-out`): "zoom out", "where does this fit" |
| `intrinsicengine-handoff` | Compact the current conversation into a handoff document for the next agent. Saves to `$TMPDIR`, never into the repo (no in-tree planning docs). | "handoff", "compact this", end-of-session summarization |

### Imported productivity skills (three)

These skills are imported from
[`mattpocock/skills`](https://github.com/mattpocock/skills) productivity
skills at commit `6eeb81b5fcfeeb5bd531dd47ab2f9f2bbea27461`. They are
MIT-licensed; see [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

| Skill | Purpose | Triggers on |
| --- | --- | --- |
| `teach` | Stateful teaching workspace for lessons, resources, glossary, learning records, and mission-grounded instruction. Includes a local guardrail so teaching files are not created at the IntrinsicEngine repo root unless explicitly intended. | User asks to be taught a topic or concept over one or more sessions |
| `grilling` | Relentless one-question-at-a-time interview for stress-testing a plan or design, exploring code first when the answer is discoverable. | "grill", "stress-test this plan", "interview me about this design" |
| `grill-me` | Short alias that starts a `grilling` session. | "grill me" |

## Authority chain (important)

The IntrinsicEngine workflow skills are **mirrors**, not replacements. Their
authority chain is:

1. `AGENTS.md` in the repo — authoritative.
2. `docs/agent/*.md` in the repo — expanded source procedures, authoritative
   over these skills if they ever disagree.
3. **These skills** — agent-facing wrappers that route to the source docs via
   `references/` and provide triggering metadata.

If a skill body and a source doc disagree, the source doc wins. Resync the
skill (see "Keeping in sync" below).

### Skill-canonical sections in three mirror skills (PROC-023)

Three of the six source-doc mirror skills carry named sections that **extend
beyond** their `docs/agent/*` source and have no source counterpart. Those
sections are **skill-canonical** — the skill body is their authoritative home
(the discipline-skill model) — while every other section of the skill still
mirrors its source and defers to it per the chain above. `AGENTS.md` wins on
anything it covers. Each skill body marks the split with an **Authority
(PROC-023)** note.

| Skill | Skill-canonical section(s) | Mirrored source |
| --- | --- | --- |
| `intrinsicengine-benchmark` | `Anti-patterns` | `benchmark-workflow.md` + `benchmark-review-checklist.md` |
| `intrinsicengine-method` | knowledge-graph claim→code aid; `How method work maps to the maturity taxonomy` | `method-workflow.md` + `method-review-checklist.md` |
| `intrinsicengine-docs-sync` | `Decision rules for common cases` | `docs-sync-policy.md` |

The cross-cutting discipline skills have no `docs/agent/` source; their
SKILL.md bodies are authoritative, but the `AGENTS.md` contract still wins on
anything it covers.

For imported productivity skills, upstream content is the source material, but
the IntrinsicEngine `AGENTS.md` contract still wins on repository hygiene,
task workflow, verification, and commit behavior.

## Anatomy of each skill

Every IntrinsicEngine workflow skill follows the standard `agentskills.io`
layout:

```
intrinsicengine-<name>/
├── SKILL.md              # YAML frontmatter + routing/summary body
└── references/           # Generated mirrors of the source docs; loaded on demand
    └── <doc>.md
```

The `SKILL.md` body is a routing layer with checklist summaries and pointers;
the `references/` files are generated from the source procedure docs by
`tools/agents/sync_skills.py`, which **rewrites relative links** during the
copy so they resolve from the mirror location — the mirrors are therefore not
byte-identical to their sources and must never be edited by hand or copied
with plain `cp`. Most cross-cutting discipline skills are SKILL.md-only, but a
discipline skill may carry **hand-authored** companion files when its method
warrants them — `intrinsicengine-research-ideation` ships hand-authored
`references/`, `assets/`, `evals/`, and `scripts/` (these are *not* generated by
`sync_skills.py` and are authoritative in place). Imported productivity skills
may likewise use standalone companion files instead of a `references/` directory
when that is how the upstream skill is structured.

## Where the skills live in this repo

The physical skill tree is this directory, `tools/agents/skills/`. The
harness-facing roots `.claude/skills` and `.codex/skills` are symlinks to it,
so every agent session in the repo picks the skills up automatically — no
installation step is needed in-tree. The copy-based installation flows below
are only for exporting the skills to *another* machine or repository.

### Exporting to Claude Code / Codex user scope

```bash
# Claude Code, user-level (available across all projects)
cp -r intrinsicengine-* teach grilling grill-me ~/.claude/skills/

# Codex, user-level
mkdir -p ~/.codex/skills
cp -r intrinsicengine-* teach grilling grill-me ~/.codex/skills/
```

### Cursor / Windsurf / Gemini CLI / others

Most tools support the agentskills.io standard. Either:

- copy the folders into the tool's skills directory (path varies per tool), or
- use `gh skill install` from a published skills repository (see
  https://github.blog/changelog/2026-04-16-manage-agent-skills-with-github-cli/).

## Keeping in sync

The `references/` files are **generated**. The one and only sync mechanism is:

```bash
# Regenerate the mirrors after editing any mapped source doc
python3 tools/agents/sync_skills.py --write

# Verify (this is what CI runs)
python3 tools/agents/sync_skills.py --check
```

`tools/agents/resync_skills.sh` is a thin wrapper around `--write`. The
source-to-mirror mapping lives in `REFERENCE_MAP` inside `sync_skills.py`;
when a new `docs/agent/*` doc should be surfaced through a skill, add a map
entry and re-run `--write`. CI (`ci-docs.yml`) runs the `--check` mode on
every docs run and fails on drift, and `tools/ci/touched_scope.py` includes
the same check locally. Do not hand-copy files into `references/` — plain
`cp` skips the link rewriting and will fail the CI check.

Imported third-party productivity skills are not generated from
`docs/agent/*`; update them manually from upstream, preserve local
guardrails, and keep `THIRD_PARTY_LICENSES.md` provenance current.

## When to edit which file

| Edit case | What to change |
| --- | --- |
| Contract rule changed | Edit `AGENTS.md` + matching `docs/agent/*` source doc, then `python3 tools/agents/sync_skills.py --write`. |
| Skill body needs better triggering | Edit the skill's `SKILL.md` `description` field (and run a triggering eval if you have one set up). |
| Routing between skills changed | Edit the `## Routing` or `## References` section in the relevant `SKILL.md` bodies. |
| Pure typo in source doc | Edit the source doc, then re-run the sync. |
| New doc should surface through a skill | Add a `REFERENCE_MAP` entry in `tools/agents/sync_skills.py`, re-run `--write`, update the mirror table above. |
| Third-party productivity skill update | Update the imported skill files manually from upstream, preserve local guardrails, and keep `THIRD_PARTY_LICENSES.md` provenance current. Do not add them to `sync_skills.py`; that tool only mirrors IntrinsicEngine canonical docs. |

## Pairing with non-IntrinsicEngine skills

These skills are designed to coexist with general-purpose skills like the
Anthropic-bundled `skill-creator`, `mcp-builder`, or third-party language
skills (e.g. `Jeffallan/claude-skills` `cpp-pro`). The IntrinsicEngine skills
take precedence on repo-specific workflow questions; general-purpose skills
add complementary knowledge (modern C++ templates, sanitizer setup, etc.).

**Skills to avoid pairing with** in this codebase:

- Broad workflow-opinionated skill bundles that prescribe a brainstorm → plan →
  subagent-driven-development pipeline (e.g. `obra/superpowers` as a whole).
  They assume there's no upstream spec; IntrinsicEngine task files *are* the
  spec, so these create two competing sources of truth. Small interactive
  helpers such as `grilling` are acceptable only when they support, rather than
  replace, the task workflow.
- "Senior C++ developer" skills with their own preferred CMake/test/commit
  workflow — they'll fight `AGENTS.md` §5–§7.

Pull *references* from those skills (Jeffallan/cpp-pro's `references/`
templates and sanitizer docs are valuable as static reference material) but
don't install their `SKILL.md` files alongside these.
