---
id: GRAPHICS-130
theme: F
depends_on:
  - GRAPHICS-006
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-graphics130"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T15:56:14Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# GRAPHICS-130 — Retire unused RHI pipeline registry

## Status

- In progress on `main`: remove the test-only registry and its stale capability
  prose while preserving the concrete PipelineManager/renderer path.

## Goal

- Delete the production-unused `Extrinsic.RHI.PipelineRegistry` cache layer
  while preserving the live `PipelineManager` and renderer-owned pipeline
  behavior.

## Non-goals

- No pipeline descriptor, shader compilation, hot-reload, manager, renderer,
  or backend behavior change.
- No timeline-semaphore work; `GRAPHICS-129` owns that independent finding.
- No new cache, registry, factory, alias, or wrapper.

## Context

- `PipelineRegistry` has one implementation but zero production consumers.
  Unit tests instantiate its isolated shader-path/generation cache while the
  renderer uses `PipelineManager` and concrete pipeline owners directly.
- Archived plans described the registry as a promoted cache/hot-reload seam,
  but no live workflow adopted it. Retaining its module and fixture prices a
  hypothetical consumer into current RHI.

## Right-sizing decision

- **Element:** `RHI::PipelineRegistry` and its module/implementation.
- **Deletion test:** remove the implementation, isolated direct tests, and
  stale capability prose; no production logic moves because there is no
  production caller.
- **Simpler alternative:** keep `PipelineManager` plus current renderer-owned
  concrete pipeline lifecycle.
- **Reintroduction trigger:** two present production pipeline owners need a
  shared shader-identity/generation cache with one live invalidation workflow.

## Required changes

- [ ] Delete the module, implementation unit, CMake entry, and isolated unit
      tests.
- [ ] Preserve focused PipelineManager and renderer pipeline coverage.
- [ ] Remove current-state architecture/RHI/parity claims for the unused cache.
- [ ] Update `LEGACY-043` so the removed registry fixture is no longer a
      prerequisite for eventual `line.frag` cleanup.
- [ ] Add a structural ratchet proving the module/name is not recreated.

## Tests

- [ ] Focused PipelineManager, renderer pipeline, reload, and fail-closed
      contracts pass.
- [ ] Complete default CPU-supported gate passes.
- [ ] Strict layering, docs/task, clean-workshop, and inventory gates pass.

## Docs

- [ ] Update canonical graphics/RHI/parity inventories and `LEGACY-043` to name
      only surviving production owners.
- [ ] Regenerate module/task/session records.

## Acceptance criteria

- [ ] No `RHI.PipelineRegistry` module, implementation, CMake entry, isolated
      test, or current capability claim remains.
- [ ] PipelineManager/renderer behavior stays covered and unchanged.
- [ ] No replacement abstraction appears.
- [ ] Independent fixed-surface review accepts the revision-bound deletion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PipelineManager|Pipeline|Renderer|Reload' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Altering pipeline creation, reload, or backend behavior.
- Replacing the unused registry under another name.
- Deleting owner-level pipeline tests with isolated registry tests.

## Maturity

- Target: `Retired`; the unused cache module disappears after surviving owner
  behavior is proven.
