# agentkit

Bootstrap a portable, contract-driven **agentic development workflow** into any
repository — new or existing. One authoritative contract, mirrored to every
agent surface; a task lifecycle; config-driven validators; split CI gates; and
session-provisioning hooks.

agentkit is **zero-dependency** (Python standard library only, 3.11+), so it
runs anywhere — locked-down CI, agent sandboxes, your laptop — with no install
step.

## Quickstart

```bash
# From the target repository root:
python3 /path/to/tools/agentkit/agentkit.py init --name "My Project" --language "Python"

# Then fill in the placeholders it created and run the checks. The generated
# repo ships a vendored, dependency-free runner — no agentkit install needed:
python3 tools/agent/check.py --strict
```

`init` is idempotent: it skips files that already exist unless you pass
`--force`. Use `--dry-run` to preview.

> **Generated repo vs. agentkit CLI.** `init` does not vendor `agentkit` itself
> into the target. A bootstrapped repo is self-sufficient via the files under
> `tools/agent/`: run the checks with `python3 tools/agent/check.py`, re-mirror
> skills with `bash tools/agent/resync_skills.sh`, and create tasks by copying
> `tasks/templates/`. The `agentkit <command>` forms below additionally require
> the launcher on disk or `pip install ./tools/agentkit`.

## What it generates

```
AGENTS.md                       the authoritative agent contract
CLAUDE.md                       thin redirect -> AGENTS.md (Claude)
.github/copilot-instructions.md thin redirect -> AGENTS.md (Copilot)
.codex/config.yaml              Codex config -> AGENTS.md + verification command
agentkit.toml                   single source of truth (drives generator + checks)
.claude/                        SessionStart hook (setup.sh) + wait helper + skills symlink
.codex/skills                   skills symlink
docs/agent/                     task-format, review, architecture-review,
                                agent-output-review, docs-sync, roles, task-maturity, prompt
tasks/                          backlog / active / done + templates + README
tools/agent/
  checks/                       config-driven validators (stdlib-only)
  skills/                       core router + task-workflow / review / docs-sync / diagnose / handoff
  resync_skills.sh              re-mirror docs into skill references
  docs_sync_rules.toml          "change X => update doc Y" rules
.github/workflows/              pr-fast.yml + ci-docs.yml
.github/pull_request_template.md
```

The contract, hooks, and CI build/test steps contain clearly marked `TODO`
placeholders — agentkit scaffolds the *structure and discipline*; you fill in
the parts specific to your stack.

## Commands

| Command | What it does |
|---------|--------------|
| `init` | Scaffold the workflow into `--path` (default `.`). |
| `check [--strict]` | Run the shipped validators — a local preview of the CI gate. |
| `doctor` | Report which workflow files are present, missing, or drifted. |
| `resync` | Re-mirror `docs/agent/*` into skill `references/`. |
| `new-task ID "Title" [--template task|bug|review]` | Create a task in `tasks/backlog/`. |

Run `python3 tools/agentkit/agentkit.py <command> --help` for options.

## The config (`agentkit.toml`)

A single TOML file is the source of truth for both the generator and the shipped
checks. Highlights:

- `[project]` — name, slug (skill prefix), language, contract filename.
- `[commands]` — configure / build / test / lint (the verification gate).
- `[tasks]` — lifecycle dirs, id prefixes, id regex, required + actionable sections.
- `[pr]` — required PR-template sections.
- `[workflows]` — allowed / required workflow filenames.
- `[hygiene]` — allowed root markdown, ignore globs, optional top-level allowlist.
- `[harness]` — which agent surfaces to emit (claude / codex / copilot / setup hook).

Edit it, then re-run `init --force` to regenerate, or just re-run the checks.

## The shipped checks

Each is standard-library only, reads `agentkit.toml`, and follows one
convention: `--root` + `--strict`, exit codes `0` pass / `1` strict failure /
`2` usage or environment failure.

- `check_tasks.py` — task dirs, task shape, lifecycle integrity, duplicate ids.
- `check_doc_links.py` — relative + root-relative markdown links resolve.
- `check_docs_sync.py` — "change X => update doc Y" rules over a git diff.
- `check_pr_contract.py` — PR template carries required sections.
- `check_root_hygiene.py` — only allowed markdown at the repo root.
- `check_workflow_names.py` — workflow naming/structure policy.
- `check_agent_config.py` — every agent surface points at the one contract.

## Design

- **One contract, mirrored.** `AGENTS.md` is authoritative; `CLAUDE.md`,
  Copilot, and Codex are thin redirects; skills mirror the docs verbatim. The
  `check_agent_config.py` validator guards against drift.
- **Lean core.** This build ships the universal process scaffolding only — no
  language-, layering-, method-, or benchmark-specific machinery. Extend it by
  adding your own checks under `tools/agent/checks/` and steps to the workflows.
- **Config-driven.** Repo-specific values live in `agentkit.toml`, never
  hardcoded in the checks — so the same checks work for any project.

## Optional install

```bash
pip install ./tools/agentkit   # exposes the `agentkit` console command
```

Installation is optional; the launcher works without it.
