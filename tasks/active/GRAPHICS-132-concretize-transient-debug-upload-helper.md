---
id: GRAPHICS-132
theme: F
depends_on:
  - GRAPHICS-077
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-graphics132"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T16:26:56Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# GRAPHICS-132 — Concretize the transient-debug upload helper

## Status

- In progress on `main`: remove only the unused base and virtual qualifiers;
  keep the concrete helper implementation and Renderer lifecycle unchanged.

## Goal

- Remove the single-owner `ITransientDebugUploadHelper` base while retaining
  `TransientDebugUploadHelper` and exact Renderer-owned behavior.

## Non-goals

- No buffer growth/reuse, BDA, transient packet, render-phase, RHI, or Vulkan
  behavior change.
- No visualization-overlay or ImGui helper change; `GRAPHICS-133/134` own those
  independent findings.
- No replacement factory, variant, registry, adapter, or backend stub.

## Context

- The interface has one concrete implementation and one production owner,
  Renderer. No alternate implementation or test adapter exists.
- Archived `GRAPHICS-077` justified polymorphism with a future Vulkan-tuned
  variant, but promoted Vulkan uses the same RHI-backed concrete helper and no
  such variant/task exists.

## Right-sizing decision

- **Element:** `ITransientDebugUploadHelper`.
- **Deletion test:** make Renderer own the existing concrete class and remove
  inheritance/virtual qualifiers; no behavior or responsibility moves.
- **Simpler alternative:** one concrete feature-owned helper.
- **Reintroduction trigger:** a real second implementation shares a current
  production selection boundary.

## Required changes

- [ ] Remove the interface and concrete inheritance.
- [ ] Make Renderer own the concrete helper while preserving initialize,
      upload, reset, release, and teardown order.
- [ ] Update focused source-contract tests and remove hypothetical
      backend-variant prose.
- [ ] Add a structural ratchet proving the base name is absent.

## Tests

- [ ] Focused transient-debug and renderer lifecycle contracts pass.
- [ ] Complete default CPU-supported gate passes.
- [ ] Strict layering, docs/task, clean-workshop, and inventory checks pass.

## Docs

- [ ] Update renderer/canonical graphics prose to name the concrete current
      owner without a future-variant claim; refresh task/session records.

## Acceptance criteria

- [ ] `ITransientDebugUploadHelper` is absent from production, tests, and
      current-state docs.
- [ ] Concrete behavior remains covered and unchanged.
- [ ] No replacement abstraction appears.
- [ ] Independent fixed-surface review accepts the revision-bound deletion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'TransientDebug|Renderer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Changing transient-debug data/command contracts or backend behavior.
- Touching the other upload-helper families.
- Retaining an alias or wrapper for the deleted base.

## Maturity

- Target: `Retired` for the speculative base; concrete behavior retains its
  existing maturity.
