---
id: BUG-110
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug110"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-07T23:28:31Z"
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this repair moves boundary Dirichlet conditions inside the existing implicit-smoothing linear solve and adds one narrow fixed-variable free function to Geometry.Sparse. It publishes nothing to an ECS geometry source, changes no element-domain property, adjacency, support-radius, parameterization, or method-integration surface, and adds no reusable task-workflow contract."
---
# BUG-110 — Implicit smoothing applies boundary pins after rather than during solve

## Status

- Completed and retired on 2026-08-08 at `CPUContracted`, the declared target;
  no `Operational` follow-up is owed.
- `ImplicitLaplacian` solved the all-free backward-Euler system over every
  vertex and then overwrote the boundary entries of the returned vector. The
  boundary *looked* preserved, but the interior had already been solved against
  the boundary's own unconstrained values, so an open patch did not satisfy the
  Dirichlet problem it reported.
- Added one narrow `Geometry::Sparse::SolveCGShiftedFixed` beside the existing
  `SolveCGShifted` seam, with the established `Geometry::DEC` forwarding wrapper
  mirrored. It assembles the same `alpha*M + beta*A`, folds each fixed column's
  coupling into the free right-hand side, and replaces each fixed row and column
  with identity. The operator stays SPD because the result is block-diagonal
  `[C_ff, 0; 0, I]` and `C_ff` is a principal submatrix of an SPD matrix, so
  plain CG still applies and its diagnostics keep their meaning. Seeding `x` at
  the fixed values makes those residual rows exactly zero, and they are now
  decoupled, so CG never moves them — boundary coordinates come back bit-exact.
- Constraints are validated before any write to the caller's buffer: index
  range, index uniqueness, matching index/value lengths, and value finiteness.
  A malformed set returns `InvalidInput` with the output untouched, so the mesh
  cannot be partially mutated.
- `ImplicitLaplacian` now collects live boundary vertices once per iteration and
  supplies per-axis fixed values; the post-solve reset loop is gone.
  `PreserveBoundary=false` still calls the unchanged `SolveCGShifted` path.
  Deleted and isolated vertices are explicitly never constrained: they carry no
  mass or Laplacian coupling and are skipped on write-back, so pinning them
  would perturb the unconstrained system for no observable gain.
- Seven regressions were added. The load-bearing one assembles the reduced
  Dirichlet system by hand from the DEC operators and solves it with an
  independent dense Gaussian elimination, then requires every interior
  coordinate to match. A second reproduces the old solve-then-overwrite result
  inline and requires the interior to differ from it. Both fail against the
  unfixed source, so they discriminate the two solves rather than describing
  one. The remaining five cover exact boundary preservation across five
  iterations, `PreserveBoundary=false` finiteness at a 1e6 timestep, the
  reduced-system solve directly, the empty-fixed-set equivalence to
  `SolveCGShifted`, and all five malformed-constraint cases.
- Verified: 76/76 `Smoothing`/`Sparse` cases pass and the default CPU gate
  passes 4138/4138 with its expected GLFW/LeakSanitizer skip. The generated
  module inventory is unchanged at 382 modules, since only free functions were
  added to existing modules.
- Completion commit: this retirement commit.

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
- [x] Add one narrow, directly testable fixed-variable/projected free-function variant of `Geometry.Sparse::SolveCGShifted`; implicit smoothing is its immediate adopter.
- [x] Validate fixed indices for uniqueness and range and validate fixed values for finiteness before iteration; malformed constraints fail without mutating the mesh.
- [x] Apply boundary constraints during each axis solve and remove the post-solve boundary reset.
- [x] Preserve convergence/iteration diagnostics and the unconstrained solve path.
- [x] Keep deleted and isolated vertex handling explicit and deterministic.

## Tests
- [x] Add an open-patch fixture with at least one interior vertex and compare every interior coordinate against an independently assembled reduced-system oracle.
- [x] Assert boundary coordinates remain exact through multiple iterations.
- [x] Demonstrate that the regression fixture differs from the old solve-then-overwrite result.
- [x] Assert `PreserveBoundary=false` remains behavior-identical and large timesteps stay finite.
- [x] Cover duplicate/out-of-range fixed indices and non-finite fixed values without partial mesh mutation.

## Docs
- [x] Correct the implicit-smoothing interface comments to describe in-solve Dirichlet elimination/projection.
- [x] Document any new narrow `Geometry.Sparse` free function; regenerate the module inventory only if the module surface changes.
- [x] Add this issue to `tasks/backlog/bugs/index.md`.

## Acceptance criteria
- [x] Preserved-boundary interior values match the independent reduced system within solver tolerance.
- [x] The previous post-solve overwrite loop is gone.
- [x] Invalid constraints fail closed before mesh mutation.
- [x] The complete existing smoothing suite remains green.

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
