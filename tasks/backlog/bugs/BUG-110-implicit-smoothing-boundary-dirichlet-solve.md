---
id: BUG-110
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-110 — Implicit smoothing applies boundary pins after rather than during solve

## Goal
- Make `ImplicitLaplacian(..., PreserveBoundary=true)` solve the actual fixed-boundary Dirichlet system instead of solving an all-free system and overwriting boundary values afterward.

## Non-goals
- No KKT solver, arbitrary equality-constraint framework, solver factory, or general optimization package.
- No rewrite of explicit, cotangent, Taubin, geodesic, harmonic, or BFF methods.
- No change to `PreserveBoundary=false` behavior.

## Context
- Symptom: `Geometry.HalfedgeMesh.Smoothing.cpp` calls `DEC::SolveCGShifted` over every vertex, then resets boundary entries in the returned vector. Boundary coordinates look fixed, but their unconstrained solved values already influenced the interior solution.
- Expected behavior: fixed boundary degrees of freedom are eliminated or projected inside the linear solve, including their matrix contribution on the free-variable right-hand side.
- Impact: an open patch with at least one interior vertex produces different interior positions from the true backward-Euler Dirichlet system while reporting successful boundary preservation.
- `Geometry.Sparse::SolveCGShifted` is the existing narrow shifted-SPD seam. Add only the smallest fixed-index path needed here; do not generalize to arbitrary constraints.

## Required changes
- [ ] Add one narrow, directly testable fixed-variable/projected free-function variant of `Geometry.Sparse::SolveCGShifted`; implicit smoothing is its immediate adopter.
- [ ] Validate fixed indices for uniqueness and range and validate fixed values for finiteness before iteration; malformed constraints fail without mutating the mesh.
- [ ] Apply boundary constraints during each axis solve and remove the post-solve boundary reset.
- [ ] Preserve convergence/iteration diagnostics and the unconstrained solve path.
- [ ] Keep deleted and isolated vertex handling explicit and deterministic.

## Tests
- [ ] Add an open-patch fixture with at least one interior vertex and compare every interior coordinate against an independently assembled reduced-system oracle.
- [ ] Assert boundary coordinates remain exact through multiple iterations.
- [ ] Demonstrate that the regression fixture differs from the old solve-then-overwrite result.
- [ ] Assert `PreserveBoundary=false` remains behavior-identical and large timesteps stay finite.
- [ ] Cover duplicate/out-of-range fixed indices and non-finite fixed values without partial mesh mutation.

## Docs
- [ ] Correct the implicit-smoothing interface comments to describe in-solve Dirichlet elimination/projection.
- [ ] Document any new narrow `Geometry.Sparse` free function; regenerate the module inventory only if the module surface changes.
- [ ] Add this issue to `tasks/backlog/bugs/index.md`.

## Acceptance criteria
- [ ] Preserved-boundary interior values match the independent reduced system within solver tolerance.
- [ ] The previous post-solve overwrite loop is gone.
- [ ] Invalid constraints fail closed before mesh mutation.
- [ ] The complete existing smoothing suite remains green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Smoothing.*Implicit|Sparse' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Preserving the apparent boundary while leaving the interior all-free solution unchanged.
- Introducing an abstract constraint interface or unrelated sparse solver.
- Shipping without an independent reduced-system oracle.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
