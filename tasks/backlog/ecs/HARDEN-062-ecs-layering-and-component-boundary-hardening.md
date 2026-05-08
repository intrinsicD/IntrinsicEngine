# HARDEN-062 — Harden ECS layering and component boundaries

## Goal
- Audit and tighten promoted ECS dependencies and component ownership so future physics/render/runtime integrations have a clean boundary.

## Non-goals
- No behavior changes to hierarchy traversal or scene bootstrap; those belong to `HARDEN-060` and `HARDEN-061`.
- No new physics subsystem or rigid-body solver.
- No broad source reformatting or legacy cleanup.

## Context
- Owner/layer: `ecs` with architecture review implications.
- `AGENTS.md` allows `ecs -> core` and only explicitly required geometry handles/types.
- `src/ecs/Components/CMakeLists.txt` currently links `ExtrinsicCore`, `ExtrinsicAssets`, `IntrinsicGeometry`, and `glm::glm`.
- `ECS.Component.AssetInstance.cppm` stores a raw `std::uint32_t AssetId` while the architecture contract prefers declared asset ID types, and ECS should not have live `AssetService` traffic.
- `ECS.Component.Collider.cppm` imports `Geometry.Sphere`, which may be legitimate but needs an explicit documented boundary because physics will likely consume collider data later.
- Collider and rigid-body authoring policy is split into `HARDEN-064`; this task should enforce the boundary that ECS stores CPU-only descriptors/IDs, not physics-world solver handles or runtime sidecars.

## Required changes
- Run and review the strict layering check for `src`.
- Remove unnecessary ECS link dependencies if they are not required by promoted modules.
- Replace untyped asset IDs in ECS components with the promoted asset registry ID type only if that preserves the documented `ecs` dependency policy; otherwise record a follow-up architecture exception instead of adding an ad hoc type.
- Document which geometry types ECS components may store directly and why.
- Document that scene hierarchy and collider hierarchy are separate concepts: ECS parent/child relationships do not implicitly become compound-collider topology.
- Add checks or docs that prohibit `PhysicsBodyHandle`, broadphase proxy handles, contact caches, island IDs, solver indices, runtime sync sidecars, graphics handles, and RHI handles from canonical ECS components.
- Add a contract test or structural check coverage for prohibited ECS imports into graphics, runtime, platform, app, and live asset services.

## Tests
- Add/update a contract test under `tests/contract/ecs/` if existing tooling does not already cover the import/dependency rule.
- Keep tests CPU-only and label them `contract;ecs`.

## Docs
- Update `src/ecs/README.md` dependency notes.
- Update `docs/migration/nonlegacy-parity-matrix.md` only if readiness or blockers change.
- Update `tasks/README.md` only if a new task prefix/category convention is introduced.

## Acceptance criteria
- ECS CMake dependencies match actual promoted module imports.
- ECS component docs explicitly separate CPU scene data from runtime/graphics/physics sidecars.
- ECS component docs explicitly separate collider descriptors from rigid-body descriptors and defer solver-owned state to the future physics/runtime integration boundary.
- Structural checks catch accidental high-layer imports into ECS.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicContractBuildTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding live `AssetService`, graphics handles, RHI handles, runtime sidecars, or physics-world handles to canonical ECS components.
- Hiding dependency violations behind umbrella imports.
- Combining dependency cleanup with semantic ECS system rewrites.

