# HARDEN-067 — Remove stale `src/platform/LinuxGlfwVulkan/` legacy subtree

## Goal
- Delete the orphaned `src/platform/LinuxGlfwVulkan/` subtree that predates the
  explicit-backend split established by `PLATFORM-003`. The directory is not
  added by `src/platform/CMakeLists.txt` (no `add_subdirectory`), is not
  reachable from any module surface, duplicates the modern
  `Backends::Glfw::Window` implementation in an obsolete direct-callback event
  shape, and contains an unfinished Vulkan surface stub
  (`LinuxGlfwVulkan/Platform.Window.cpp:201` `// TODO: Create the
  VulkanSurface…`). Removing it eliminates dead code, prevents accidental
  reintroduction, and matches the "interface + `backends/<name>/`" pattern
  documented in `src/platform/README.md`.

## Non-goals
- Do not modify the `Null` or `Glfw` backends, the public `Platform.IWindow` /
  `Platform.Input` modules, or `Platform.CreateWindow.cpp`.
- Do not change CMake selection logic in `src/platform/CMakeLists.txt`.
- Do not retire or shrink `src/legacy`.
- Do not migrate any code out of the deleted directory; nothing in it is
  reachable, and the modern `Glfw` backend already supplies the same window /
  input behavior plus a dedicated Vulkan surface helper
  (`backends/glfw/Platform.Backend.GlfwVulkanSurface.cppm`).
- Do not introduce new platform features, tests, or documentation beyond what
  the deletion requires.

## Context
- Owning subsystem/layer: `src/platform`.
- Architecture rule: `platform -> core` only (`/AGENTS.md` §2, §4).
- Predecessor: [`PLATFORM-003`](../../done/PLATFORM-003-explicit-platform-backends.md)
  established `backends/null/`, `backends/glfw/`, and the
  `INTRINSIC_PLATFORM_BACKEND` selection policy. After that task,
  `LinuxGlfwVulkan/` was left behind without being wired into the parent
  `CMakeLists.txt`.
- Concrete state of the dead subtree:
  - `src/platform/LinuxGlfwVulkan/CMakeLists.txt` (~14 lines) — never invoked
    by parent; would re-add `Platform.Input.cpp` and `Platform.Window.cpp` to
    a `target_name` that does not exist in this scope.
  - `src/platform/LinuxGlfwVulkan/Platform.Window.cpp` (~333 lines) — defines
    `LinuxGlfwVulkanWindow : public IWindow` using the pre-`PLATFORM-003`
    direct-callback `Callback(event)` event model, incompatible with the
    promoted queued `Event` variant used by `Backends::Glfw::Window`.
  - `src/platform/LinuxGlfwVulkan/Platform.Input.cpp` (~108 lines) —
    monolithic GLFW wrapper, not the reusable `Platform::Input::Context` class
    promoted in `Platform.Input.cppm`.
  - `src/platform/LinuxGlfwVulkan/README.md` (~8 lines) — stub directory
    description.
- Layering: confirmed by `src/platform` exploration that the dead subtree
  introduces no `import Extrinsic.Graphics`, `Extrinsic.ECS`, or
  `Extrinsic.Runtime` edges, so its removal cannot regress layering policy.

## Required changes
- Delete `src/platform/LinuxGlfwVulkan/` in its entirety
  (`CMakeLists.txt`, `Platform.Window.cpp`, `Platform.Input.cpp`,
  `README.md`).
- Confirm no reference remains in `src/platform/CMakeLists.txt`, the top-level
  `CMakeLists.txt`, `cmake/`, `tests/`, `tools/`, or `docs/`. There should be
  none today; treat any hit as a separate finding to surface in the task PR.
- No edits to public modules, backend modules, the create-window dispatcher,
  or the CMake backend-selection block.

## Tests
- No new tests. The deleted code is unreachable from any compiled target.
- Re-run the existing platform contract and unit tests to confirm no
  regression in either backend configuration:
  - `tests/contract/platform/Test.PlatformLayering.cpp`
  - `tests/unit/platform/Test.NullPlatform.cpp`
  - `tests/integration/platform/Test.GlfwPlatformSmoke.cpp` (under the opt-in
    `glfw` label).

## Docs
- Update `src/platform/README.md` only if it references `LinuxGlfwVulkan/`
  (current text does not — verify before editing).
- Refresh `docs/api/generated/module_inventory.md` with
  `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
  if the inventory tool currently lists `LinuxGlfwVulkan/` files. If not, no
  regeneration is needed.
- No new architecture docs. Removal of dead code does not change the layering
  invariants in `/AGENTS.md`.

## Acceptance criteria
- `src/platform/LinuxGlfwVulkan/` no longer exists.
- `python3 tools/repo/check_layering.py --root src --strict` passes with no
  new findings.
- `python3 tools/repo/check_test_layout.py --root . --strict` passes.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check`
  reports the inventory up-to-date.
- Null backend configure + build + platform-labeled CTest succeed.
- Optional GLFW backend configure + build + `glfw`-labeled CTest succeed.
- The change is a pure deletion (plus, if needed, a single regenerated
  inventory) — no semantic edits to platform sources.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

cmake --preset ci -DINTRINSIC_PLATFORM_BACKEND=Null -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformTests
ctest --test-dir build/ci --output-on-failure -L 'platform' --timeout 60

# Optional GLFW coverage (opt-in label):
cmake --preset ci -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformGlfwSmokeTests
ctest --test-dir build/ci --output-on-failure -L 'glfw' --timeout 60
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Modifying any code in `backends/null/`, `backends/glfw/`, the public
  `Platform.IWindow` / `Platform.Input` modules, or `Platform.CreateWindow.cpp`.
- Editing `src/platform/CMakeLists.txt` backend selection logic.
- Adding Platform imports of Graphics, ECS, or Runtime.
- Migrating, restoring, or "salvaging" code out of the deleted subtree —
  there is no callable surface to migrate to.
