# HARDEN-062 — Harden ECS layering and component boundaries

## Goal
- Audit and tighten promoted ECS dependencies and component ownership so future physics/render/runtime integrations have a clean boundary.

## Non-goals
- No behavior changes to hierarchy traversal or scene bootstrap; those belong to `HARDEN-060` and `HARDEN-061`.
- No new physics subsystem or rigid-body solver.
- No broad source reformatting or legacy cleanup.
- No promotion of `Extrinsic::Assets::AssetId` (the typed `Core::StrongHandle<AssetTag>`) onto ECS components — adopting that type would create an `ecs -> assets` dependency edge that the current contract does not permit (`tools/repo/check_layering.py::ALLOWED_DEPS["ecs"] == {"core", "geometry"}`); recorded as a Slice 2 follow-up architecture decision rather than mechanical work in this slice.

## Context
- Status: in-progress (Slice 1).
- Owner/agent: Claude (HARDEN-062).
- Branch: `claude/setup-agentic-workflow-TvDlg`.
- Owner/layer: `ecs` with architecture review implications.
- `AGENTS.md` allows `ecs -> core` and only explicitly required geometry handles/types.
- `src/ecs/Components/CMakeLists.txt` previously linked `ExtrinsicCore`, `ExtrinsicAssets`, `IntrinsicGeometry`, and `glm::glm`. No component module imports any `Extrinsic.Asset.*` or `Extrinsic.Core.*` symbol; ExtrinsicCore enters `ExtrinsicECS` transitively via `Systems/CMakeLists.txt` for the `Extrinsic.Core.FrameGraph` import in `ECS.System.TransformHierarchy`.
- `ECS.Component.AssetInstance.cppm` stores a raw `std::uint32_t AssetId`. The architecture contract prefers declared asset ID types (`Extrinsic::Assets::AssetId`), but ECS is not permitted to import `Extrinsic.Asset.Registry` under the current `ecs -> {core, geometry}` policy. The runtime assembles the typed handle at the `ecs -> runtime -> graphics` seam.
- `ECS.Component.Collider.cppm` imports `Geometry.Sphere`. That CPU descriptor is the legitimate collider representation; rigid-body authoring is governed by `HARDEN-064` (gated on `ARCH-001`) and is explicitly out of scope here.
- Collider and rigid-body authoring policy is split into `HARDEN-064`; this task enforces the boundary that ECS stores CPU-only descriptors/IDs, not physics-world solver handles or runtime sidecars.

## Contract decisions (Slice 1)
- **Components link policy.** `src/ecs/Components/CMakeLists.txt` links only the libraries actually imported by component modules (`IntrinsicGeometry`, `glm::glm`). The previous `ExtrinsicCore` / `ExtrinsicAssets` entries were unused; they have been removed and replaced with an in-file comment recording the policy. ExtrinsicCore continues to enter `ExtrinsicECS` transitively via `Systems/CMakeLists.txt` for the `Extrinsic.Core.FrameGraph` import in `ECS.System.TransformHierarchy`.
- **ECS contract test seam.** A new CPU-only `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` reads the `src/ecs` source files and rejects imports of `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Runtime.*`, `Extrinsic.Platform.*`, `Extrinsic.App.*`, and any `Extrinsic.Asset.*` (including `Asset.Registry`). The test runs under labels `contract;ecs` via the `IntrinsicEcsContractTests` executable.
- **Prohibited symbols.** The same contract test rejects accidental introduction of `PhysicsBodyHandle`, `RigidBodyHandle`, `BroadphaseProxyHandle`, `ContactCache`, `IslandId`, `SolverIndex`, `RhiTextureHandle`, `BindlessHandle`, `BindlessIndex`, and `AssetService` mentions inside `src/ecs`. Targeted assertions also lock down `AssetInstance`, `Collider`, and `Hierarchy` components against accidental coupling.
- **AssetId typing decision.** `ECS.Component.AssetInstance::Source::AssetId` remains a raw `std::uint32_t` for this slice. Promoting it to the typed `Extrinsic::Assets::AssetId` would create an `ecs -> assets` edge; that decision is deferred to Slice 2 / a follow-up architecture task. The README documents the rationale and the runtime-side assembly point (`Runtime.RenderExtraction`) where the typed handle is constructed.
- **Collider vs rigid-body separation.** `ECS.Component.Collider` continues to store CPU-only `Geometry::Sphere` descriptors; the README captures that rigid-body authoring belongs to `HARDEN-064` and is gated on `ARCH-001`.
- **Scene hierarchy vs collider hierarchy separation.** The README captures that `ECS.Component.Hierarchy` is the scene-graph parent/child relationship only and never implicitly defines compound-collider topology.

## Slice plan
- **Slice 1 (this PR)** — Boundary hardening of the existing surface:
  - Drop the unused `ExtrinsicCore` and `ExtrinsicAssets` link entries from `src/ecs/Components/CMakeLists.txt`; record the policy in an in-file comment.
  - Add `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` and wire `IntrinsicEcsContractTests` (`contract;ecs`) through `tests/CMakeLists.txt`.
  - Update `src/ecs/README.md` with the dependency contract, the contract test reference, the geometry-types-allowed enumeration, the AssetInstance follow-up, the collider-vs-rigid-body split, and the scene-hierarchy-vs-collider-hierarchy split.
  - Run focused verification: `cmake --preset ci` + `cmake --build --preset ci --target IntrinsicECSTests IntrinsicContractBuildTests IntrinsicEcsContractTests`, `ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60`, plus the structural tools (`check_layering`, `check_test_layout`, `check_doc_links`, `check_task_policy`).
- **Slice 2 (follow-up)** — Architecture decision on the AssetInstance typed handle:
  - Decide whether to widen the `ecs` contract to include `assets` (Asset.Registry types only) following the same model as `graphics -> assets`.
  - If accepted, replace `std::uint32_t AssetId` on `ECS.Component.AssetInstance::Source` with `Extrinsic::Assets::AssetId`, update `tools/repo/check_layering.py::ALLOWED_DEPS`, update `Runtime.RenderExtraction`, and tighten the contract test to require the typed handle.
  - If rejected, document the rationale and remove the `Extrinsic.Asset.Registry` entry from the contract test's prohibited-import list with a targeted comment so the rule remains current.

## Required changes
- Run and review the strict layering check for `src` (no new violations introduced).
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
cmake --build --preset ci --target IntrinsicECSTests IntrinsicContractBuildTests IntrinsicEcsContractTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding live `AssetService`, graphics handles, RHI handles, runtime sidecars, or physics-world handles to canonical ECS components.
- Hiding dependency violations behind umbrella imports.
- Combining dependency cleanup with semantic ECS system rewrites.

## Execution log
- 2026-05-09: Promoted task to `tasks/active/`. Recorded Slice 1 contract decisions and slice plan above.
- 2026-05-09: Slice 1 implemented:
  - Audited `src/ecs/**/*.cppm` and `*.cpp` imports; confirmed no component module imports `Extrinsic.Asset.*`, `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Runtime.*`, `Extrinsic.Platform.*`, or `Extrinsic.App.*`. Component-side `Extrinsic.Core.*` imports are also absent; only `Systems/ECS.System.TransformHierarchy` imports `Extrinsic.Core.FrameGraph` (and `Extrinsic.Core.Hash` in its `.cpp`).
  - Tightened `src/ecs/Components/CMakeLists.txt`: removed unused `ExtrinsicCore` and `ExtrinsicAssets` link entries, kept `IntrinsicGeometry` and `glm::glm`, and recorded the policy with an in-file comment.
  - Added `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` covering: (1) prohibited high-layer imports under `src/ecs`, (2) prohibited solver/runtime/RHI symbols anywhere under `src/ecs`, (3) `ECS.Component.AssetInstance` remains a CPU-only stable ID surface, (4) `ECS.Component.Collider` rejects rigid-body / broadphase / contact-cache / island-id state, (5) `ECS.Component.Hierarchy` does not encode collider topology.
  - Wired `EcsContractTestObjs` and `IntrinsicEcsContractTests` (labels `contract ecs`) through `tests/CMakeLists.txt`.
  - Updated `src/ecs/README.md` with: dependency contract reference to `/AGENTS.md §2`, contract-test cross-link, linked-dependencies enumeration, prohibited-handles list, geometry types allowed on ECS, AssetInstance follow-up rationale, collider-vs-rigid-body split (with cross-links to `HARDEN-064` and `ARCH-001`), and scene-hierarchy-vs-collider-hierarchy separation.
  - Structural checks clean in this session: `tools/agents/check_task_policy.py --strict`, `tools/repo/check_layering.py --strict`, `tools/repo/check_test_layout.py --strict`, `tools/docs/check_doc_links.py`.

## Next verification step
- CI must run `cmake --preset ci` + `cmake --build --preset ci --target IntrinsicECSTests IntrinsicContractBuildTests IntrinsicEcsContractTests` + `ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60`. Local sandbox lacks `clang-20` (default-preset compiler) so the C++ build is deferred to CI for this slice.
