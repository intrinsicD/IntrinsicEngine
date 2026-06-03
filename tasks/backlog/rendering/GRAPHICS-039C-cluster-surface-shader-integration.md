# GRAPHICS-039C — Clustered surface-shader integration + recipe wiring

## Goal
- Bind the packed light-index buffer + offset/count header on the per-frame global
  descriptor set and integrate per-cell light iteration into the surface/deferred
  lighting shaders (`GRAPHICS-039` decision 6), with recipe wiring and integration tests.

## Non-goals
- No async-compute affinity (that is `GRAPHICS-039D`).
- No IBL probe parallel-buffer extension (decision 7 — owned by `GRAPHICS-042`).

## Context
- Owner layer: `graphics/renderer` (shader integration + recipe wiring).
- Depends on `GRAPHICS-039B` (assignment) and `GRAPHICS-008` (surface/G-buffer passes, done).
- Decision 6: bind the packed index buffer + offset/count header as a read-only SSBO pair
  on the per-frame global descriptor set (`set 0`, next free bindings against the then-
  current global layout); surface shaders derive their cell from `gl_FragCoord.xy` (tile)
  and view-Z (log-Z slice), read the header, and iterate only the assigned indices.

## Required changes
- [ ] Bind the cluster index/header SSBO pair on the global descriptor set.
- [ ] Update the surface/deferred lighting shaders to derive the cell and iterate the
      per-cell light list instead of the full light loop.
- [ ] Wire the cluster build + assignment + lighting consumption into the default recipe.
- [ ] `contract;graphics` + integration tests for recipe wiring and per-cell iteration shape.

## Tests
- [ ] `contract;graphics` — recipe includes cluster build/assignment before lighting;
      binding layout correctness.
- [ ] `integration` — clustered lighting produces the same lit result as the full-loop
      path for a small known scene (parity within tolerance).
- [ ] CPU gate green.

## Docs
- [ ] Document the clustered surface-shader path in `src/graphics/renderer/README.md`
      and `docs/architecture/rendering-three-pass.md`.

## Acceptance criteria
- [ ] Surface/deferred shaders iterate per-cell lists; recipe wiring is CPU-tested.
- [ ] Clustered lighting matches the full-loop reference within tolerance.
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding per-material light descriptors instead of the global-set SSBO pair.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` — clustered lighting runs in the default recipe and is parity-
  tested against the full-loop path on the CPU/null gate; an opt-in `gpu;vulkan` shader-
  iteration smoke may follow but is not owed for the CPU parity contract.
