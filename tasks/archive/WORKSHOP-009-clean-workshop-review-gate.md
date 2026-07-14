# WORKSHOP-009 — Add clean-workshop architecture review gate

## Status

- Status: done (retired 2026-06-06).
- Completed: 2026-06-06.
- Commit: this commit adds `docs/agent/clean-workshop-review.md` (the
  eight-row scorecard + "when required" triggers), the
  `tools/ci/run_clean_workshop_review.sh` validator bundle, the example record
  `docs/reviews/2026-06-06-clean-workshop-review-example.md`, links from
  `docs/index.md` / `docs/agent/review-checklist.md` / the `AGENTS.md` related
  docs table, and retires this task.
- Result: the gate is a non-blocking scorecard that composes existing
  validators (strict layering, allowlist quality, task policy, doc links) and
  produces follow-up task IDs for the manual rows. No new CI gate was added and
  no existing gate was weakened.

## Goal
- Create a recurring architecture-review gate that detects god-object growth, boundary drift, and scaffold accumulation before they become expensive foundations to revisit.

## Non-goals
- Do not block every PR with heavy manual review.
- Do not introduce subjective style policing.
- Do not rewrite existing architecture docs wholesale.
- Do not build a complex metrics dashboard.

## Context
- The repo now has strong architectural intent, but the main risk is slow drift: one convenience dependency, one renderer member, one string-routed pass, one scaffold marked done.
- This task creates a lightweight scorecard/checklist so agents and humans can detect "well-documented mess" early.

## Required changes
- [x] Add `docs/agent/clean-workshop-review.md` or equivalent.
- [x] Define a short architecture scorecard with objective checks:
  - promoted layer imports match `/AGENTS.md`;
  - CMake target links match layer policy;
  - no new public API exposes higher-layer types to lower layers;
  - renderer member/subsystem growth is justified by an owning seam;
  - new passes use typed IDs, not string routing;
  - new frame recipe dependencies are resource-driven or explicitly justified;
  - scaffold tasks have follow-up maturity gates (see the
    [`task-maturity.md`](../../docs/agent/task-maturity.md) taxonomy and
    its `Scaffolded` closure rule, landed by WORKSHOP-008 on 2026-05-17);
  - legacy exceptions have task IDs and expiry.
- [x] Add a small script or documented command bundle under `tools/ci/` that runs the relevant existing checks and prints the review checklist location.
- [x] Link the review doc from `docs/index.md` and `/AGENTS.md` related docs table if appropriate.
- [x] Add guidance for when the review is required:
  - changing dependency boundaries;
  - adding renderer subsystems/passes;
  - changing RHI/platform/runtime wiring;
  - closing scaffold or parity tasks;
  - adding allowlist entries.
- [x] Add one example review record under `docs/reviews/` showing how to record findings and follow-up task IDs.

## Tests
- [x] Run docs-link validation.
- [x] Run task-policy validation if new task references are added.
- [x] Run layering validation to ensure the command bundle uses the strict checker.
- [x] If a helper script is added, add a focused smoke test or self-check for it.

## Docs
- [x] Add clean-workshop review doc.
- [x] Update docs index.
- [x] Update agent review checklist to reference the clean-workshop gate.
- [x] Update `/AGENTS.md` related docs table only if this becomes an authoritative trigger document.

## Acceptance criteria
- [x] A future agent can determine when architecture review is required.
- [x] The review gate explicitly checks the failure modes identified in this task pack.
- [x] The review process produces follow-up task IDs instead of vague TODOs.
- [x] Docs links and task policy remain green.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "contract|unit" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not make the review gate a replacement for tests.
- Do not introduce subjective rules without objective evidence or examples.
- Do not create root-level planning files; keep review docs under `docs/` and follow-up work under `tasks/`.
- Do not weaken existing CI gates.
