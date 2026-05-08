# ARCH-001 — Define physics layer ownership and ECS integration

## Goal
- Decide and document where rigid-body dynamics and other physical simulation systems live, and how they integrate with ECS/runtime without violating layer boundaries.

## Non-goals
- No rigid-body solver implementation.
- No soft-body, fluid, cloth, FEM, particle, or GPU simulation implementation.
- No changes to the canonical source layout before the architecture decision is recorded.

## Context
- Owner/layer: architecture planning across `core`, `geometry`, `ecs`, `runtime`, `methods`, and potential `physics` ownership.
- `AGENTS.md` target source layout currently does not include `src/physics`, so adding that layer requires a documented architecture/source-layout update.
- Geometry already provides collision/math building blocks such as `Geometry.GJK`, `Geometry.EPA`, `Geometry.ContactManifold`, `Geometry.Overlap`, `Geometry.SDF`, and `Geometry.SDFContact`.
- `docs/architecture/patterns.md` already sketches a `PhysicsWorld` owner and `PhysicsTickJob` worker split, but it is only an architectural pattern example.
- ECS should hold authoritative scene intent/components; runtime should own composition/wiring; physics simulation state should avoid back-importing runtime, graphics, platform, or app.

## Required changes
- Write an architecture note or ADR deciding whether to introduce `src/physics/` and its allowed dependency edges.
- Define the split between:
  - ECS physics components: body descriptors, collider references, material IDs, simulation intent.
  - Physics world/state: solver islands, broadphase, contacts, constraints, integration caches.
  - Runtime bridge: fixed-step scheduling, ECS-to-physics synchronization, physics-to-ECS transform writeback.
  - Methods packages: reference algorithms, parity baselines, diagnostics, and papers.
- Decide whether physics can depend on `geometry` directly and whether ECS may store geometry collider primitives or only handles/descriptors.
- Define deterministic fixed-step policy, units, coordinate convention, sleep/island policy, collision filtering, event ownership, and diagnostics/reporting expectations.
- Add follow-up implementation tasks for rigid-body, collision broadphase/narrowphase, constraints, and non-rigid phenomena after the layer decision.

## Tests
- Add/update architecture/contract tests only if source layout or dependency checks change.
- If a `src/physics` layer is approved, update layering tooling and add a failing-first contract test that prevents physics from importing runtime/graphics/platform/app.

## Docs
- Update `AGENTS.md` only if the canonical source layout or dependency invariant table changes.
- Update `docs/architecture/` with the physics layer contract and ECS/runtime integration diagram.
- Update `docs/migration/nonlegacy-parity-matrix.md` only if promoted source roots or retirement gates change.

## Acceptance criteria
- The repository has an explicit, reviewed answer for where rigid-body dynamics and other physics systems belong.
- ECS, runtime, geometry, methods, and any new physics layer each have clear ownership and dependency constraints.
- Follow-up tasks can implement physics without inventing cross-layer shortcuts.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding `src/physics` code before the architecture contract is accepted.
- Letting physics systems mutate graphics state or own runtime composition.
- Treating geometry collision algorithms as a full dynamics engine without a physics ownership boundary.

