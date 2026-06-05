# METHOD-001 — Rigid-body dynamics reference backend

## Status

- Completed: 2026-06-05.
- Maturity: `CPUContracted`.
- Commit/PR: this task retirement commit.
- `Operational` owned by `PHYSICS-001`.


## Goal
- Create a deterministic CPU reference backend for rigid-body dynamics under the method workflow, suitable as the correctness oracle for future engine integration.

## Non-goals
- No optimized CPU backend.
- No GPU backend.
- No runtime/ECS integration in this method task; [`ARCH-001`](ARCH-001-physics-layer-ownership-and-ecs-integration.md)
  / [ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md)
  assigns runtime integration to physics/runtime follow-ups.
- No ECS authoring component changes; that is owned by retired [`HARDEN-064`](HARDEN-064-ecs-collider-rigidbody-authoring-contract.md).
- No performance claims without benchmark baselines.

## Context
- Paper/method: foundational rigid-body dynamics and impulse/constraint solving; exact references must be selected during intake.
- Method package: [`methods/physics/rigid_body_reference/`](../../methods/physics/rigid_body_reference/)
- Geometry already has contact and overlap primitives, but rigid-body dynamics needs state integration, mass/inertia, contacts, constraints, solver iterations, diagnostics, and determinism guarantees.
- The reference method should model collision shapes separately from rigid-body state so ECS authoring components from `HARDEN-064` and future physics-world integration can compare against the same collider/body split.
- This task follows `docs/agent/method-workflow.md`: paper/method intake, CPU reference first, correctness tests, benchmark harness/manifests, then documentation.
- Convergence: part of **Theme C — Physics readiness** in [`tasks/backlog/README.md`](../backlog/README.md). The accepted physics layer ownership decision in [`ARCH-001`](ARCH-001-physics-layer-ownership-and-ecs-integration.md) / [ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md) and the ECS authoring contract in retired [`HARDEN-064`](HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) bound where state and runtime sync ultimately live.

## Required changes
- [x] Intake paper(s) or textbook method references and define the method contract.
- [x] Define body state/descriptors: pose, linear/angular velocity, mass, inverse mass, inertia tensor, damping, gravity, sleep flags, and collision shape references.
- [x] Implement CPU reference integration for unconstrained rigid bodies with deterministic fixed-step stepping.
- [x] Implement reference contact generation inputs/outputs using simple analytic dynamic-capable shapes first: sphere, capsule, and box/AABB/OBB, with compound-shape inputs represented as explicit child shapes with local poses.
- [x] Treat triangle mesh colliders as static/kinematic-only reference inputs unless a later convex-decomposition or specialized method task expands dynamic concave support.
- [x] Implement a correctness-first contact/constraint solve path with explicit diagnostics for non-convergence, penetration tolerance, and energy drift.
- [x] Add benchmark harness/manifests with stable IDs and machine-readable metrics.
- [x] Document diagnostics, unit conventions, numerical tolerances, and known limitations.

## Tests
- [x] Add analytic tests for free fall, constant acceleration, angular integration sanity, elastic/inelastic collision toy cases, resting contact stability, and deterministic repeatability.
- [x] Add degenerate-input tests: zero/negative mass rejection, invalid inertia, NaN state rejection, huge/small scale tolerances, and overlapping-start diagnostics.
- [x] Keep tests CPU-only and independent of runtime/ECS.

## Docs
- [x] Add `methods/physics/rigid_body_reference/README.md` with method contract, references, backend identity, limitations, and diagnostics.
- [x] Add or update benchmark manifests under `benchmarks/` only for smoke/reference metrics, not performance claims.

## Acceptance criteria
- [x] CPU reference implementation is present and tested.
- [x] Benchmarks and manifests are present or explicitly stubbed with stable IDs.
- [x] Numerical limitations and diagnostics are documented.
- [x] Future `src/physics` or runtime integration can compare against this reference backend.

## Verification

Completed in this slice:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicPhysicsMethodTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci -R 'RigidBodyReference|IntrinsicBenchmarkSmoke' --output-on-failure --timeout 60
cmake --build --preset ci --target IntrinsicBenchmarks
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark/IntrinsicBenchmarkSmoke --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_pr_contract.py
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Results:
- Focused rigid-body/reference-smoke CTest subset passed: 14/14.
- Method manifests passed strict validation: 3 manifests.
- Benchmark manifests passed strict validation: 3 manifests.
- Benchmark result validation passed for `build/ci/benchmark/IntrinsicBenchmarkSmoke`: 2 files.
- `check_test_layout.py`, `check_doc_links.py`, `check_task_policy.py`, `check_task_state_links.py`,
  `check_docs_sync.py --diff-mode`, `check_pr_contract.py`, and `git diff --check` passed.
- `check_root_hygiene.py` passed with the existing warning-mode `.agents/` notice.
- `IntrinsicTests` built successfully.
- Default CPU-supported CTest gate passed: 2,775/2,775 tests.

## Forbidden changes
- Adding optimized CPU or GPU backend before reference parity.
- Claiming performance wins without baseline comparison.
- Importing runtime, graphics, platform, app, or live ECS ownership into the method package.
