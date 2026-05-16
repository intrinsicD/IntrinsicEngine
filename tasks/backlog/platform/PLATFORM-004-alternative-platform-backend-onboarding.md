# PLATFORM-004 — Alternative-platform backend onboarding policy (planning seed)

## Goal
- Capture a single, planning-only umbrella for adding alternative platform
  backends (Wayland, Windows, macOS, and any future host) under the
  `src/platform/backends/<name>/` slot already documented in
  `src/platform/README.md`. This task is the canonical place to record
  open questions, contract requirements, and CMake/test conventions for
  any future platform backend before that backend is actually implemented.
  It stays planning-only until a concrete need promotes a follow-up
  `PLATFORM-005…` implementation task.

## Non-goals
- Do not add any new backend code, CMake selection branches, or platform
  preprocessor defines as part of this task.
- Do not change the existing `Null` or `Glfw` backends, the public
  `Platform.IWindow` / `Platform.Input` modules, or
  `Platform.CreateWindow.cpp`.
- Do not introduce new dependencies into `src/platform`. Any backend-local
  dependency belongs to a future implementation task that adds the backend
  module itself.
- Do not refactor `src/platform/CMakeLists.txt`'s `INTRINSIC_PLATFORM_BACKEND`
  selection block. New backend names get added by the implementing task,
  not by this planning seed.
- Do not start work on any specific platform (Wayland/Windows/macOS) here —
  each is a separate implementation task once justified.

## Context
- Owning subsystem/layer: `src/platform`.
- Architecture rule: `platform -> core` only (`/AGENTS.md` §2, §4). Backends
  may pull backend-local dependencies (analogous to GLFW for `Backends::Glfw`)
  but must not pull `graphics`, `ecs`, or `runtime`.
- Documented future work: `src/platform/README.md` ("interface/backend
  split is deliberate: headless tests and alternative platforms
  (Windows/macOS/Wayland) plug in by adding a sibling backend directory
  under `backends/` without touching the interface modules").
- Predecessor: [`PLATFORM-003`](../../done/PLATFORM-003-explicit-platform-backends.md)
  established the explicit `Null` / `Glfw` backend split, the
  `INTRINSIC_PLATFORM_BACKEND=Auto|Null|Glfw` selection policy, and the
  `INTRINSIC_HEADLESS_NO_GLFW` constraint.
- Backend conventions to consider when this task is promoted:
  - One module per backend, named `Extrinsic.Platform.Backend.<Name>`.
  - One backend-local Vulkan surface helper module per backend that owns a
    Vulkan surface (`Extrinsic.Platform.Backend.<Name>VulkanSurface`),
    matching the `GlfwVulkanSurface` precedent.
  - `Platform.CreateWindow.cpp` extended only via additional preprocessor
    branches (`INTRINSIC_PLATFORM_BACKEND_<NAME>=1`).
  - CTest labels: `platform` for headless-safe tests; `<name>` (e.g.
    `wayland`, `win32`, `cocoa`) for opt-in platform-specific smoke tests
    excluded from the default CPU gate.

## Required changes
- [ ] This task remains planning-only. No source or build changes.
- [ ] When promoting this task, the promoting agent must:
  - Pick a single concrete backend (not a bundle).
  - Create `PLATFORM-005-<backend>-backend.md` (or next free `PLATFORM-` ID)
    under `tasks/backlog/platform/` using `tasks/templates/task.md`.
  - Cite this task in the new task's Context section.

## Tests
- [ ] None required by this planning task.
- [ ] The eventual implementation task per backend must add:
  - A headless contract test confirming the backend factory honors the
    backend selection (or is correctly compiled out when not selected).
  - An opt-in smoke test under a backend-specific label (analogous to
    `tests/integration/platform/Test.GlfwPlatformSmoke.cpp`).
  - Layering coverage equivalent to
    `tests/contract/platform/Test.PlatformLayering.cpp` for any new module
    surface introduced.

## Docs
- [ ] No doc changes by this planning task.
- [ ] The implementing task must update `src/platform/README.md` to list the
  new backend, refresh `docs/api/generated/module_inventory.md`, and add
  any backend-specific build prerequisites to the relevant
  `docs/architecture/` page.

## Acceptance criteria
- [ ] This file exists in `tasks/backlog/platform/` and is referenced from
  `tasks/backlog/platform/README.md`.
- [ ] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.
- [ ] The task is left in `tasks/backlog/`. Promoting it to `tasks/active/`
  requires producing at least one concrete backend implementation task as
  described in "Required changes".

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding any backend code, CMake branch, or preprocessor define under this
  task.
- Adding Platform imports of Graphics, ECS, or Runtime.
- Bundling multiple platform backends into one promotion of this task.
