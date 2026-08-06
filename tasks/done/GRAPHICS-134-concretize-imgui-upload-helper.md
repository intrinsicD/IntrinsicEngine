---
id: GRAPHICS-134
theme: F
depends_on:
  - GRAPHICS-079
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-graphics134"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T17:16:26Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# GRAPHICS-134 — Concretize the ImGui upload helper

## Status

- Completed and retired on 2026-08-06. The unused `IImGuiUploadHelper` base
  and virtual dispatch are gone; the concrete helper implementation unit,
  Renderer call sequence, frame-slot buffers, upload behavior, and teardown
  order are unchanged. Focused ImGui/renderer coverage passed 194/194 and the
  complete default CPU selector passed 4,102/4,102 with its expected
  GLFW/LSan skip.
- Completion commit: this implementation/retirement commit.

## Goal

- Remove the single-owner `IImGuiUploadHelper` base while retaining
  `ImGuiUploadHelper` and exact Renderer-owned overlay upload behavior.

## Non-goals

- No ImGui packet, font/user texture, vertex/index buffer, scissor, Renderer,
  RHI, or Vulkan change.
- No transient-debug or visualization helper change; `GRAPHICS-132/133` own
  those findings.
- No new factory, variant, registry, adapter, or backend stub.

## Context

- The interface has one implementation and one production owner, Renderer,
  with no alternate implementation or test adapter. The concrete RHI-backed
  helper is already the promoted Vulkan path.

## Right-sizing decision

- **Element:** `IImGuiUploadHelper`.
- **Deletion test:** switch Renderer ownership to the concrete helper and
  remove inheritance/virtual qualifiers; no behavior moves.
- **Simpler alternative:** one concrete feature-owned helper.
- **Reintroduction trigger:** a real second implementation shares a current
  production selection boundary.

## Required changes

- [x] Remove the interface and concrete inheritance.
- [x] Preserve concrete initialization, upload, reset/release, and teardown
      order under Renderer ownership.
- [x] Update focused source-contract tests/docs and add an absence ratchet.

## Tests

- [x] Focused ImGui upload/overlay and renderer lifecycle contracts pass.
- [x] Complete default CPU-supported gate plus strict structural/docs gates
      pass.

## Docs

- [x] Audit the already-concrete renderer/canonical graphics prose and refresh
      task/session records.

## Acceptance criteria

- [x] The interface name is absent; concrete behavior stays covered.
- [x] No other helper family or replacement abstraction changes.
- [x] Independent fixed-surface review accepts the revision-bound deletion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ImGui|Renderer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Changing ImGui upload/render contracts or touching other helper families.
- Retaining an alias/wrapper for the deleted base.

## Maturity

- Target: `Retired` for the speculative base; concrete behavior is retained.
