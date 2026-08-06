---
id: GRAPHICS-131
theme: F
depends_on:
  - GRAPHICS-040B
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# GRAPHICS-131 — Concretize the reference reconstructor

## Goal

- Remove the single-implementation `IReconstructor` base while retaining
  `ReferenceTAAReconstructor` and its exact CPU-contracted behavior.

## Non-goals

- No TAA algorithm, history, recipe, renderer, diagnostic, or backend change.
- No vendor SDK/backend integration and no speculative replacement strategy,
  registry, factory, enum, or variant.
- No renaming of the concrete reference implementation.

## Context

- `ReferenceTAAReconstructor` is the only implementation. Renderer production
  code owns and calls that concrete type directly; no interface-typed
  production consumer, test double, or alternate implementation exists.
- Archived `GRAPHICS-040B` explicitly called vendor implementations future,
  per-vendor work and opened none. The base therefore prices hypothetical
  integrations into the current public surface.

## Right-sizing decision

- **Element:** `Graphics.Reconstruction::IReconstructor`.
- **Deletion test:** remove inheritance and virtual dispatch while keeping the
  same public records and concrete `ReferenceTAAReconstructor::Apply/Reset`
  contract; no complexity moves to callers.
- **Simpler alternative:** one concrete class for the one current algorithm.
- **Reintroduction trigger:** a real second reconstructor implementation is
  integrated and shares a current production selection/call boundary.

## Required changes

- [ ] Remove `IReconstructor` and make the concrete reference class own the
      existing methods directly.
- [ ] Preserve result, diagnostics, reset, and deterministic CPU algorithm
      behavior byte-for-byte.
- [ ] Update tests and docs to name the concrete current seam without a
      hypothetical vendor-polymorphism claim.
- [ ] Add a structural ratchet against recreating the deleted base without a
      second implementation.

## Tests

- [ ] Reconstruction unit/contract and renderer integration coverage passes.
- [ ] Complete default CPU-supported gate passes.
- [ ] Strict layering, docs/task, clean-workshop, and inventory checks pass.

## Docs

- [ ] Update canonical graphics and renderer architecture prose plus task
      index history to distinguish retained TAA behavior from removed
      speculative polymorphism.
- [ ] Regenerate module/task/session records when required.

## Acceptance criteria

- [ ] `IReconstructor` is absent from production, tests, and current-state
      documentation.
- [ ] `ReferenceTAAReconstructor` behavior and public data records are
      unchanged and covered.
- [ ] No replacement abstraction or vendor placeholder appears.
- [ ] Independent fixed-surface review accepts the revision-bound deletion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Reconstruction|TAA|Renderer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Changing reconstruction math, history lifetime, recipe selection, or
  renderer execution.
- Adding a vendor/backend abstraction before a real second implementation.
- Renaming or wrapping the retained concrete class to preserve the old shape.

## Maturity

- Target: `Retired` for the speculative base; reference TAA remains
  `CPUContracted` at its existing maturity.
