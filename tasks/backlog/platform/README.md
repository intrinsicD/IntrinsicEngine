# Platform Backlog

Platform layer (`src/platform`) tracks windowing and input ports plus their
explicit backends. The authoritative engine contract is
[`/AGENTS.md`](../../../AGENTS.md); this directory tracks proposed or pending
work scoped to the platform layer.

The current public surface (`Extrinsic.Platform.Window`,
`Extrinsic.Platform.Input`) and the `Null` and `Glfw` backends were established
by `PLATFORM-003` (done).
See [`src/platform/README.md`](../../../src/platform/README.md) for the current
backend layout, selection policy, and dependency note.

## Tasks

- [PLATFORM-004 — Alternative-platform backend onboarding policy (planning seed)](PLATFORM-004-alternative-platform-backend-onboarding.md):
  planning-only umbrella for Wayland / Windows / macOS backends documented as
  future plug-in slots in `src/platform/README.md`. Stays planning-only until
  there is a concrete need. ARCH-013 confirmed this remains unchanged: runtime
  UI capture sits above platform, and `platform -> core` stays the only layer
  edge.

Retired:


## Convergence

The platform layer is not currently a member of any P0 convergence theme. It
contributes to **Theme F — Architecture/runtime/UI foundation seeds** by
keeping the platform port/backend split honest and discoverable. New
dependency edges out of `platform` are forbidden by `AGENTS.md` §2/§4 and must
not be introduced under cover of any task in this directory.
Retired `PLATFORM-006` is also a child of the `LEGACY-011` legacy feature map.

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
- [`/AGENTS.md`](../../../AGENTS.md) §2, §4 — platform layering invariants and
  backend-selection policy.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [PLATFORM-005 — Platform module implementation splits](../../done/PLATFORM-005-platform-module-implementation-splits.md):
  module-interface hygiene follow-up for `Platform.Input`, `Platform.Backend.Glfw`,
  and `Platform.Backend.Null`; moves non-trivial bodies and backend-only
  includes/imports out of `.cppm` interfaces without changing platform behavior.
- [HARDEN-067 — Remove stale `src/platform/LinuxGlfwVulkan/` legacy subtree](../../done/HARDEN-067-remove-stale-platform-linuxglfwvulkan.md)
  (done 2026-05-15): the orphaned pre-`PLATFORM-003` window/input duplicate
  has been deleted; the explicit-backend split now stands without dead-code
  remnants under `src/platform/`.
- [PLATFORM-006 — Platform event parity and editor boundary](../../done/PLATFORM-006-platform-event-parity.md)
  (done 2026-06-09, `CPUContracted`): current editor/runtime event contracts
  are explicit for resize/minimize, key/mouse/scroll/cursor, text codepoints,
  clipboard, cursor mode, and dropped paths. Native file dialogs,
  multi-window, non-Linux backend parity, and IME composition remain deferred
  until a concrete runtime/UI task accepts them.
- [`tasks/done/PLATFORM-003-explicit-platform-backends.md`](../../done/PLATFORM-003-explicit-platform-backends.md)
  — establishment of the explicit backend split.
