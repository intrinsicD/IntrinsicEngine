# METHOD-001 — Rigid-body dynamics reference backend

## Goal
- Create a deterministic CPU reference backend for rigid-body dynamics under the method workflow, suitable as the correctness oracle for future engine integration.

## Non-goals
- No optimized CPU backend.
- No GPU backend.
- No runtime/ECS integration until [`ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md) defines physics layer ownership.
- No ECS authoring component changes; that is owned by [`HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md).
- No performance claims without benchmark baselines.

## Context
- Paper/method: foundational rigid-body dynamics and impulse/constraint solving; exact references must be selected during intake.
- Method package: `methods/physics/rigid_body_reference/`
- Geometry already has contact and overlap primitives, but rigid-body dynamics needs state integration, mass/inertia, contacts, constraints, solver iterations, diagnostics, and determinism guarantees.
- The reference method should model collision shapes separately from rigid-body state so future ECS authoring components and physics-world integration can compare against the same collider/body split.
- This task follows `docs/agent/method-workflow.md`: paper/method intake, CPU reference first, correctness tests, benchmark harness/manifests, then documentation.
- Convergence: part of **Theme C — Physics readiness** in [`tasks/backlog/README.md`](../README.md). The physics layer ownership decision in [`ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md) and the ECS authoring contract in [`HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) bound where state and runtime sync ultimately live.

## Required changes
- [ ] Intake paper(s) or textbook method references and define the method contract.
- [ ] Define body state/descriptors: pose, linear/angular velocity, mass, inverse mass, inertia tensor, damping, gravity, sleep flags, and collision shape references.
- [ ] Implement CPU reference integration for unconstrained rigid bodies with deterministic fixed-step stepping.
- [ ] Implement reference contact generation inputs/outputs using simple analytic dynamic-capable shapes first: sphere, capsule, and box/AABB/OBB, with compound-shape inputs represented as explicit child shapes with local poses.
- [ ] Treat triangle mesh colliders as static/kinematic-only reference inputs unless a later convex-decomposition or specialized method task expands dynamic concave support.
- [ ] Implement a correctness-first contact/constraint solve path with explicit diagnostics for non-convergence, penetration tolerance, and energy drift.
- [ ] Add benchmark harness/manifests with stable IDs and machine-readable metrics.
- [ ] Document diagnostics, unit conventions, numerical tolerances, and known limitations.

## Tests
- [ ] Add analytic tests for free fall, constant acceleration, angular integration sanity, elastic/inelastic collision toy cases, resting contact stability, and deterministic repeatability.
- [ ] Add degenerate-input tests: zero/negative mass rejection, invalid inertia, NaN state rejection, huge/small scale tolerances, and overlapping-start diagnostics.
- [ ] Keep tests CPU-only and independent of runtime/ECS.

## Docs
- [ ] Add `methods/physics/rigid_body_reference/README.md` with method contract, references, backend identity, limitations, and diagnostics.
- [ ] Add or update benchmark manifests under `benchmarks/` only for smoke/reference metrics, not performance claims.

## Acceptance criteria
- [ ] CPU reference implementation is present and tested.
- [ ] Benchmarks and manifests are present or explicitly stubbed with stable IDs.
- [ ] Numerical limitations and diagnostics are documented.
- [ ] Future `src/physics` or runtime integration can compare against this reference backend.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding optimized CPU or GPU backend before reference parity.
- Claiming performance wins without baseline comparison.
- Importing runtime, graphics, platform, app, or live ECS ownership into the method package.

