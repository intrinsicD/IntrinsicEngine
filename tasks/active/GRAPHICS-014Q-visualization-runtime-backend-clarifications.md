# GRAPHICS-014Q — Visualization runtime/backend clarification follow-ups

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-013CQ` retirement cleared `tasks/active/`.
- Branch: `claude/agentic-workflow-session-T6CQd`.
- Promotion commit: pending (this commit moves the file from `tasks/backlog/rendering/` to `tasks/active/`).
- Implementation commit: pending docs sync into `docs/architecture/rendering-three-pass.md`, `docs/architecture/graphics.md`, and `src/graphics/renderer/README.md`.
- Task-state commit: pending retirement commit (will move the file from `tasks/active/` to `tasks/done/`).
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` after docs sync lands.

## Goal
- Clarify runtime, geometry, and backend details that remain after the CPU/null `GRAPHICS-014` visualization packet and snapshot contracts.

## Non-goals
- No C++ behavior changes.
- No GPU texture residency implementation; that belongs to `GRAPHICS-015`.
- No editor widget or importer/exporter work.

## Context
- `GRAPHICS-014` established data-only visualization packets, UV-vs-Htex fragment bake mapping policy, diagnostics, overlay summaries, and renderer-owned `RenderWorld::Visualization` snapshot spans.
- Remaining questions affect runtime/geometry producers, concrete GPU upload, and backend shader/resource binding.

## Required changes
- Clarify runtime ownership for translating PropertySet attributes, KMeans labels, isoline results, vector fields, and Htex metadata into `RuntimeRenderSnapshotBatch` visualization packet spans.
- Clarify whether invalid visualization packets are dropped by runtime producers, filtered by future upload stages, or surfaced as diagnostics-only records in tooling.
- Clarify backend upload strategy for vector-field/isoline overlays after texture residency lands, including line/point bucket expansion versus auxiliary draw resources.
- Clarify UV-backed versus Htex-backed fragment bake selection policy in UI/runtime terms, including when user-requested Htex regeneration is scheduled.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update rendering architecture, graphics architecture, and runtime extraction docs with chosen producer/upload responsibilities.

## Acceptance criteria
- Runtime/geometry/backend work can implement concrete visualization uploads without changing the CPU/null graphics contracts from `GRAPHICS-014`.
- Graphics remains free of live ECS, geometry algorithm, and editor widget ownership.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Implementing texture/atlas residency outside `GRAPHICS-015`.
- Depending on legacy visualization managers.

