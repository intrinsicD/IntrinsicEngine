---
name: intrinsicengine-core
description: Entry point for IntrinsicEngine repository work and workflow questions. Routes from the authoritative AGENTS.md contract to the specialist procedure needed for the touched scope.
---

# IntrinsicEngine Core

`AGENTS.md` owns the engineering contract and takes precedence over this router.
Startup reading is the root contract, this skill, and the task being continued
when one exists. Current, complete text supplied in context counts as read;
fetch only missing, truncated, or changed sections. Use a canonical document or
its generated reference, never both. Do not reload unchanged material.

Follow the root contract's scoped links into `docs/agent/contract.md` (or
`references/contract.md`) before acting on the matching scope. Those detailed
requirements are mandatory; neither expanded contract nor full session workflow
is a startup prerequisite. Re-evaluate reading routes when scope changes.
Read `tasks/SESSION-BRIEF.md` and `tasks/backlog/README.md` only for work selection.

## Workflow sections

`references/session-onboarding.md` mirrors `docs/agent/prompt/prompt.md` and owns
the expanded session procedure. Select the named section with a heading search
and bounded read; do not load the whole file to use one section.

| Trigger | Section to read |
| --- | --- |
| Giving hints, asking a material question, or observing the human's work | `Pair (default)` |
| Bounded implementation hand-off | `Delegate (on explicit hand-off)` |
| Asked for direction or method selection | `Advisor (when they are stuck or ask for direction)` |
| Authorization/scope decision or a new risk signal | `Risk gates` |
| Choosing work | `Work selection` |
| Selecting/running verification | `Verification` |
| Running tools, presenting large results, or waiting on work | `Tool output and waits` |
| A failed check | `When CI fails` |
| Committing or opening a PR | `Commit and PR hygiene` |
| Filing an offered deferred hint | `Deferred-hint ledger` |
| Unattended execution | `Unattended overnight mode`, plus the applicable selection/verification/commit sections |
| Blocked work or unexpected state | `When stuck` |

Task notes and audits use their specialist routes below. Workflow-policy edits
also read the affected sections and `Anti-patterns to refuse`. Interactive work
uses the micro lane for persistent notes; claims/work graphs/completion reports
belong to unattended or opt-in custody work. Research evidence still follows
`AGENTS.md` §8b in every posture.

## Specialist routing

Load only the procedure whose scope applies. The names below resolve under
`tools/agents/skills/<name>/SKILL.md`.

| Touched scope | Skill to consult |
| --- | --- |
| Before adding a non-trivial implementation/helper/file, finding canonical code, or consolidating duplicated mechanisms | `intrinsicengine-reuse` |
| Creating, promoting, retiring, or materially updating files under `tasks/` | `intrinsicengine-task-workflow` |
| Before committing or reporting completion for a non-trivial change (the pre-merge sweep) | `intrinsicengine-review` |
| Changing dependency boundaries, module ownership, source layout, runtime wiring | `intrinsicengine-review` (architecture deep review + clean-workshop scorecard) |
| Planning or reviewing new abstraction surface (interfaces, service/bridge/registry facades, module frameworks, event/command indirection), a small change fanning out across many files, or suspected over-engineering/glue | `intrinsicengine-right-sizing` |
| Proposing novel, unconventional, cross-domain, or potentially publishable research directions (the ideation front end that feeds the method track) | `intrinsicengine-research-ideation` |
| Auditing method, benchmark, backend-parity, capability-maturity, or quantitative claims after evidence exists | `intrinsicengine-results-audit` (then the method/benchmark/review specialists it routes) |
| Implementing or modifying paper/method work under `methods/` | `intrinsicengine-method` |
| Adding, changing, or running benchmark harnesses/manifests/baselines | `intrinsicengine-benchmark` |
| Moving files, changing public APIs/module surfaces, refreshing inventories | `intrinsicengine-docs-sync` |
| Creating or materially changing `.cppm` files, headers, source comments, or README files; auditing comment/README debt | `intrinsicengine-source-documentation` |
| Adding or changing an asset import/materialization path, or an import that "succeeds" but is not visible/selectable in the sandbox | `intrinsicengine-import-visibility-contract` |
| Adding or changing a geometry importer/exporter (OBJ/OFF/PLY/STL/PCD/XYZ/TGF), parsing an untrusted header count, or defining IO diagnostics/fixtures | `intrinsicengine-geometry-io-format` |
| Diagnosing a hard bug, validation-layer error, parity mismatch, or perf regression | `intrinsicengine-diagnose` |
| Sandbox input capture, window-close/exit, edit-flush ordering, camera/cursor sign, or drag-drop poll-thread wiring in `Engine::RunFrame` | `intrinsicengine-sandbox-input-lifecycle` |
| Debugging a black/wrong frame, VUID cascade, or driver crash on the promoted Vulkan path | `intrinsicengine-vulkan-frame-triage` |
| Authoring or changing an opt-in `gpu;vulkan` readback smoke, or proving a fix `Operational` | `intrinsicengine-gpu-smoke-authoring` |
| Unexplained SEGV/ASan/vtable/ICE failure, especially after `.cppm` module changes | `intrinsicengine-stale-build-triage` |
| Getting a layer-cake map of an unfamiliar file before editing | read `tools/agents/skills/intrinsicengine-zoom-out/SKILL.md` directly (user-invoked slash skill, not model-invocable) |
| Drawing a layer, module-neighbourhood, change-impact, composition, sequence, or data-flow architecture diagram | `intrinsicengine-draw-architecture` |
| Navigating module deps, change impact, or paper→method→code links | Knowledge-graph discovery aid (below) |
| Compacting a long session into a handoff doc for the next agent | `intrinsicengine-handoff` |

## Discovery and expanded references

The optional knowledge graph helps navigate module imports and paper/method
links. It omits header includes and is never a gate: confirm findings in
source and with `tools/repo/check_layering.py`. If unavailable, use source
search. Read `references/contract.md` §"Knowledge-graph discovery aid
(optional)" for query details only when needed.

- `references/session-onboarding.md` — only the workflow sections triggered above.
- `references/contract.md` — expanded engineering rationale and setup details;
  mandatory sections selected by `AGENTS.md`, also used for contract changes.
- `references/roles.md` — responsibilities by posture and lane.
