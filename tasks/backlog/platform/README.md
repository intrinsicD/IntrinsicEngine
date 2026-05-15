# Platform Backlog

Platform layer (`src/platform`) tracks windowing and input ports plus their
explicit backends. The authoritative engine contract is
[`/AGENTS.md`](../../../AGENTS.md); this directory tracks proposed or pending
work scoped to the platform layer.

The current public surface (`Extrinsic.Platform.Window`,
`Extrinsic.Platform.Input`) and the `Null` and `Glfw` backends were established
by [`PLATFORM-003` (done)](../../done/PLATFORM-003-explicit-platform-backends.md).
See [`src/platform/README.md`](../../../src/platform/README.md) for the current
backend layout, selection policy, and dependency note.

## Tasks

- [HARDEN-067 — Remove stale `src/platform/LinuxGlfwVulkan/` legacy subtree](HARDEN-067-remove-stale-platform-linuxglfwvulkan.md):
  delete the orphaned pre-`PLATFORM-003` window/input duplicate that is not
  added by `src/platform/CMakeLists.txt` and contains an unfinished Vulkan
  surface stub.
- [PLATFORM-004 — Alternative-platform backend onboarding policy (planning seed)](PLATFORM-004-alternative-platform-backend-onboarding.md):
  planning-only umbrella for Wayland / Windows / macOS backends documented as
  future plug-in slots in `src/platform/README.md`. Stays planning-only until
  there is a concrete need.

## Convergence

The platform layer is not currently a member of any P0 convergence theme. It
contributes to **Theme F — Architecture/runtime/UI foundation seeds** by
keeping the platform port/backend split honest and discoverable. New
dependency edges out of `platform` are forbidden by `AGENTS.md` §2/§4 and must
not be introduced under cover of any task in this directory.

## Promotion checklist

In addition to the global promotion checklist in
[`tasks/backlog/README.md`](../README.md):

- Platform tasks must keep `platform -> core` only. Any proposed edge to
  `graphics`, `ecs`, or `runtime` is a layering bug, not a task.
- Backend selection must remain explicit via `INTRINSIC_PLATFORM_BACKEND`
  (`Auto|Null|Glfw`, plus `INTRINSIC_HEADLESS_NO_GLFW`). Auto-detection
  changes require an architecture review.
- Tests added by platform tasks should cover both the `Null` and (under opt-in
  `glfw` label) the `Glfw` backend where the surface allows it.

## Related

- [`src/platform/README.md`](../../../src/platform/README.md) — current
  backend layout and selection policy.
- [`tasks/done/PLATFORM-003-explicit-platform-backends.md`](../../done/PLATFORM-003-explicit-platform-backends.md)
  — establishment of the explicit backend split.
- [`/AGENTS.md`](../../../AGENTS.md) §2, §4 — platform layering invariants and
  backend-selection policy.
