# RORG-034 — Promote frame-loop contracts to core
## Goal
Move the dependency-free promoted frame-loop contract from `runtime` to `core` and generalize its public module/symbol names.
## Non-goals
- No changes to legacy `src/legacy/Runtime/Runtime.FrameLoop.*`.
- No runtime engine behavior changes.
- No migration of procedural geometry, reference scene, render extraction, streaming executor, or camera controller modules in this task.
## Context
- Owning layer after the change is `core` because the contract has no imports beyond C++ standard library headers and only defines reusable phase-ordering hooks.
- `runtime` remains the composition root and consumes the core contract.
- Follow-up ownership audit conclusion: `Runtime.Engine`, `Runtime.RenderExtraction`, `Runtime.ReferenceScene`, `Runtime.StreamingExecutor`, and `Runtime.CameraControllers` should remain runtime-owned for now because each composes lower layers or owns runtime/editor policy. `Runtime.ProceduralGeometry` and `Runtime.ProceduralGeometryPacker` remain candidates for a later graphics-assets/geometry split because they currently bridge ECS procedural descriptors to graphics upload descriptors. `Graphics.CameraSnapshots` does not strictly have to remain in graphics; it is currently renderer-owned because it validates immutable render camera inputs and derives renderer-consumed snapshots/pick rays, but its platform extent import makes a future split to a lower data/math contract worth tracking separately.
## Required changes
- [x] Move `src/runtime/Runtime.FrameLoop.cppm` to `src/core/Core.FrameLoop.cppm`.
- [x] Rename the promoted module from `Extrinsic.Runtime.FrameLoop` to `Extrinsic.Core.FrameLoop`.
- [x] Generalize exported symbols from `Runtime*` / `IRuntime*` names to core-owned frame contract names.
- [x] Update runtime engine call sites, tests, docs, CMake, and generated module inventory.
- [x] Audit remaining runtime modules for ownership recommendations.
## Tests
- [x] Build `IntrinsicTests` with the configured `ci` tree using the available Clang 22 toolchain override.
- [x] Run focused frame-loop/runtime contract tests.
- [x] Run structural layering/task checks relevant to the move.
## Docs
- [x] Update `src/runtime/README.md` and migration/generated inventory references.
## Acceptance criteria
- [x] No promoted `src/runtime/Runtime.FrameLoop.cppm` remains.
- [x] `ExtrinsicCore` exports `Extrinsic.Core.FrameLoop`.
- [x] Runtime imports `Extrinsic.Core.FrameLoop` and continues to orchestrate phases through the contract.
- [x] Layering checks remain clean.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```
Observed verification:
- `/home/alex/.local/bin/cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22 -DTINYGLTF_BUILD_LOADER_EXAMPLE=OFF -DTINYGLTF_HEADER_ONLY=ON -DTINYGLTF_INSTALL=OFF` passed after clearing stale `external/cache/draco-build` and `build/ci` artifacts.
- `/home/alex/.local/bin/cmake --build --preset ci --target IntrinsicTests` passed.
- `ctest --test-dir build/ci --output-on-failure -R 'RuntimeCameraControllers|RuntimeFrameLoopContract|RuntimeEngineLayering|RenderWorldContract' --timeout 60` passed: 32/32 tests.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` passed.
## Forbidden changes
- Mixing this semantic module promotion with unrelated runtime module moves.
- Changing runtime frame ordering or shutdown/maintenance semantics.
- Introducing dependencies from `core` to higher layers.

## Completion metadata
- Completion date: 2026-05-13.
- Commit reference: pending current workspace/PR.

