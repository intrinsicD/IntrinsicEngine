---
id: GRAPHICS-133
theme: F
depends_on:
  - GRAPHICS-078
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-graphics133"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T16:53:46Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# GRAPHICS-133 — Concretize the visualization-overlay upload helper

## Status

- Completed and retired on 2026-08-06. The unused
  `IVisualizationOverlayUploadHelper` base and virtual dispatch are gone; the
  concrete helper implementation unit, Renderer call sequence, per-lane
  buffers, upload behavior, and teardown order are unchanged. Focused
  visualization-overlay/renderer coverage passed 161/161 and the complete
  default CPU selector passed 4,101/4,101 with its expected GLFW/LSan skip.
- Completion commit: this implementation/retirement commit.

## Goal

- Remove the single-owner `IVisualizationOverlayUploadHelper` base while
  retaining `VisualizationOverlayUploadHelper` and exact Renderer behavior.

## Non-goals

- No visualization packet, lane, buffer, BDA, renderer, RHI, or Vulkan change.
- No transient-debug or ImGui helper change; `GRAPHICS-132/134` own them.
- No new factory, variant, registry, adapter, or backend stub.

## Context

- The interface has one implementation and one production owner, Renderer,
  with no alternate or test adapter. Future source-BDA/backend variants did not
  become present implementations or tasks.

## Right-sizing decision

- **Element:** `IVisualizationOverlayUploadHelper`.
- **Deletion test:** switch Renderer ownership to the existing concrete helper
  and remove inheritance/virtual qualifiers; behavior does not move.
- **Simpler alternative:** one concrete feature-owned helper.
- **Reintroduction trigger:** a real second implementation shares a current
  production selection boundary.

## Required changes

- [x] Remove the interface and concrete inheritance.
- [x] Preserve concrete initialization, per-lane upload/reset/release, and
      teardown order under Renderer ownership.
- [x] Update focused source-contract tests/docs and add an absence ratchet.

## Tests

- [x] Focused visualization-overlay and renderer lifecycle contracts pass.
- [x] Complete default CPU-supported gate plus strict structural/docs gates
      pass.

## Docs

- [x] Update renderer/canonical graphics prose and task/session records.

## Acceptance criteria

- [x] The interface name is absent; concrete behavior stays covered.
- [x] No other helper family or replacement abstraction changes.
- [x] Independent fixed-surface review accepts the revision-bound deletion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'VisualizationOverlay|Renderer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Changing visualization-overlay contracts or touching other helper families.
- Retaining an alias/wrapper for the deleted base.

## Maturity

- Target: `Retired` for the speculative base; concrete behavior is retained.
