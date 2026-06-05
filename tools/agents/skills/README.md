# IntrinsicEngine Agent Skills

This folder contains nine [Agent Skills](https://agentskills.io). Six of them
wrap the IntrinsicEngine `AGENTS.md` contract and the procedure docs under
`docs/agent/`; three add cross-cutting workflow disciplines (diagnosis,
zoom-out, handoff) that don't have a single source doc under `docs/agent/`.
The skills are designed to load *progressively*: only the metadata (~100 tokens
per skill) sits in context by default, with full content loaded only when the
skill's trigger description matches the current task.

## Why these skills exist

The IntrinsicEngine repo already encodes its agent workflow extensively in
`AGENTS.md` plus 13 expanded procedure docs under `docs/agent/`. That works,
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

## The nine skills

### Source-doc mirrors (six)

| Skill | Wraps | Triggers on |
| --- | --- | --- |
| `intrinsicengine-core` | `AGENTS.md` + `docs/agent/contract.md` + `docs/agent/roles.md` + `docs/agent/prompt/prompt.md` | Any work in this repo — always-on master |
| `intrinsicengine-task-workflow` | `docs/agent/task-format.md` + `docs/agent/task-maturity.md` + `tasks/templates/task.md` | Creating, promoting, retiring, or materially editing task files |
| `intrinsicengine-review` | `docs/agent/review-checklist.md` + `docs/agent/architecture-review-checklist.md` + `docs/agent/agent-output-review-checklist.md` | Pre-commit, PR review, architecture-impacting changes, weekly audit |
| `intrinsicengine-method` | `docs/agent/method-workflow.md` + `docs/agent/method-review-checklist.md` | Paper/method implementation under `methods/` |
| `intrinsicengine-benchmark` | `docs/agent/benchmark-workflow.md` + `docs/agent/benchmark-review-checklist.md` | Benchmark manifests, runners, performance claims |
| `intrinsicengine-docs-sync` | `docs/agent/docs-sync-policy.md` | File moves, API surface changes, inventory regeneration |

Together these cover all 13 of the `docs/agent/*.md` files plus `AGENTS.md`
itself, the session-onboarding prompt under `docs/agent/prompt/prompt.md`, and
the task template at `tasks/templates/task.md`.

### Cross-cutting workflow disciplines (three)

These skills do not wrap a single source doc under `docs/agent/`. They encode
disciplines that apply across multiple touched scopes. The skill body is
authoritative for these.

| Skill | Purpose | Triggers on |
| --- | --- | --- |
| `intrinsicengine-diagnose` | Disciplined diagnosis loop: feedback-loop → reproduce → rank hypotheses → instrument with tagged probes → fix → regression-test → cleanup. Adapted with C++/Vulkan tooling (ctest labels, validation layers, RenderDoc, backend differentials, benchmark baselines). | "diagnose this", crashes, validation-layer errors, CPU/null gate failures, reference-vs-optimized parity mismatches, benchmark regressions |
| `intrinsicengine-zoom-out` | One-shot layer-cake map of an unfamiliar file using the engine's domain vocabulary (`core`/`geometry`/`assets`/`ecs`/`physics`/`graphics/*`/`runtime`/`app`/`methods`) and `.cppm` module surfaces. | "zoom out", "where does this fit", "give me the layer map" |
| `intrinsicengine-handoff` | Compact the current conversation into a handoff document for the next agent. Saves to `$TMPDIR`, never into the repo (no in-tree planning docs). | "handoff", "compact this", end-of-session summarization |

## Authority chain (important)

These skills are **mirrors**, not replacements. The authority chain is:

1. `AGENTS.md` in the repo — authoritative.
2. `docs/agent/*.md` in the repo — expanded source procedures, authoritative
   over these skills if they ever disagree.
3. **These skills** — agent-facing wrappers that route to the source docs via
   `references/` and provide triggering metadata.

If a skill body and a source doc disagree, the source doc wins. Resync the
skill (see "Keeping in sync" below).

## Anatomy of each skill

Every skill follows the standard `agentskills.io` layout:

```
intrinsicengine-<name>/
├── SKILL.md              # YAML frontmatter + routing/summary body
└── references/           # Source docs, copied verbatim; loaded on demand
    └── <doc>.md
```

The `SKILL.md` body is a routing layer with checklist summaries and pointers;
the `references/` files are the verbatim source procedure for deep reads.

## Installation

### Claude Code

```bash
# User-level (available across all projects)
cp -r intrinsicengine-* ~/.claude/skills/

# Or project-level (only this repo)
mkdir -p .claude/skills
cp -r intrinsicengine-* .claude/skills/
```

### Codex

```bash
mkdir -p ~/.codex/skills
cp -r intrinsicengine-* ~/.codex/skills/
```

### Cursor / Windsurf / Gemini CLI / others

Most tools support the agentskills.io standard. Either:

- copy the folders into the tool's skills directory (path varies per tool), or
- use `gh skill install` from a published skills repository (see
  https://github.blog/changelog/2026-04-16-manage-agent-skills-with-github-cli/).

### As a repo-local plugin (recommended)

Drop this whole directory into the IntrinsicEngine repo at `.claude/skills/`
(or `.codex/skills/`, etc., depending on tooling). That way every agent
session in the repo automatically gets the skills, and the skills travel with
the codebase.

## Keeping in sync with `docs/agent/`

The `references/` files in each skill are **copies** of the source docs. When
the source docs change, the skill references must update too.

A trivial resync script:

```bash
#!/bin/bash
set -euo pipefail
REPO_ROOT="$(git rev-parse --show-toplevel)"
SKILLS="$REPO_ROOT/.claude/skills"

# Core
cp "$REPO_ROOT/docs/agent/contract.md"          "$SKILLS/intrinsicengine-core/references/contract.md"
cp "$REPO_ROOT/docs/agent/roles.md"             "$SKILLS/intrinsicengine-core/references/roles.md"
cp "$REPO_ROOT/docs/agent/prompt/prompt.md"     "$SKILLS/intrinsicengine-core/references/session-onboarding.md"

# Task workflow
cp "$REPO_ROOT/docs/agent/task-format.md"       "$SKILLS/intrinsicengine-task-workflow/references/task-format.md"
cp "$REPO_ROOT/docs/agent/task-maturity.md"     "$SKILLS/intrinsicengine-task-workflow/references/task-maturity.md"
cp "$REPO_ROOT/tasks/templates/task.md"         "$SKILLS/intrinsicengine-task-workflow/references/task-template.md"

# Review
cp "$REPO_ROOT/docs/agent/review-checklist.md"               "$SKILLS/intrinsicengine-review/references/review-checklist.md"
cp "$REPO_ROOT/docs/agent/architecture-review-checklist.md"  "$SKILLS/intrinsicengine-review/references/architecture-review-checklist.md"
cp "$REPO_ROOT/docs/agent/agent-output-review-checklist.md"  "$SKILLS/intrinsicengine-review/references/agent-output-review-checklist.md"

# Method
cp "$REPO_ROOT/docs/agent/method-workflow.md"          "$SKILLS/intrinsicengine-method/references/method-workflow.md"
cp "$REPO_ROOT/docs/agent/method-review-checklist.md"  "$SKILLS/intrinsicengine-method/references/method-review-checklist.md"

# Benchmark
cp "$REPO_ROOT/docs/agent/benchmark-workflow.md"          "$SKILLS/intrinsicengine-benchmark/references/benchmark-workflow.md"
cp "$REPO_ROOT/docs/agent/benchmark-review-checklist.md"  "$SKILLS/intrinsicengine-benchmark/references/benchmark-review-checklist.md"

# Docs sync
cp "$REPO_ROOT/docs/agent/docs-sync-policy.md"  "$SKILLS/intrinsicengine-docs-sync/references/docs-sync-policy.md"

echo "Skill references resynced."
```

Save this as `tools/agents/resync_skills.sh` if you adopt the skills. A CI
job that runs `diff -r` between source docs and skill references would catch
drift automatically.

## When to edit which file

| Edit case | What to change |
| --- | --- |
| Contract rule changed | Edit `AGENTS.md` + matching `docs/agent/*` source doc, then resync references. |
| Skill body needs better triggering | Edit the skill's `SKILL.md` `description` field (and run a triggering eval if you have one set up). |
| Routing between skills changed | Edit the `## Routing` or `## References` section in the relevant `SKILL.md` bodies. |
| Pure typo in source doc | Edit the source doc, then resync. |

## Pairing with non-IntrinsicEngine skills

These skills are designed to coexist with general-purpose skills like the
Anthropic-bundled `skill-creator`, `mcp-builder`, or third-party language
skills (e.g. `Jeffallan/claude-skills` `cpp-pro`). The IntrinsicEngine skills
take precedence on repo-specific workflow questions; general-purpose skills
add complementary knowledge (modern C++ templates, sanitizer setup, etc.).

**Skills to avoid pairing with** in this codebase:

- Workflow-opinionated skills that prescribe a brainstorm → plan →
  subagent-driven-development pipeline (e.g. `obra/superpowers` as a whole).
  They assume there's no upstream spec; IntrinsicEngine task files *are* the
  spec, so these create two competing sources of truth.
- "Senior C++ developer" skills with their own preferred CMake/test/commit
  workflow — they'll fight `AGENTS.md` §5–§7.

Pull *references* from those skills (Jeffallan/cpp-pro's `references/`
templates and sanitizer docs are valuable as static reference material) but
don't install their `SKILL.md` files alongside these.
