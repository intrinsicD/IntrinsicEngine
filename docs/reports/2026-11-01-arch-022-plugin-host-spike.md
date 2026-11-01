# ARCH-022 plugin-host spike: answers, link variant, history-entry form

Date: 2026-11-01. Task: `ARCH-022` (slice 2, S1 and S2; slice 3 step 1). This is the spike report that slice 2's
done criterion asks for. The operator decided on its recommendations on 2026-11-01 (Q51, Q52). Slice 3 step 1
turned the spike host into the runtime's plugin host.

## Questions
The operator's questions (verbatim):
- Q36: "Ich will auch Laufzeit Kompilierung damit Agenten etwas ändern kannst du die sandbox/engine weiterlaufen
  kann."
- Q50: "Ja bitte, mach 1. - 5." (5: S2, reload without a restart).
- Q51: "2. ja ich würde gerne anfangen mit refactoring und Plugins bauen, damit die Engine aufgeräumt und sauber
  wird, runtime und editor auch."

The spike's three questions (the target's open points):
1. Do C++23 modules work across a shared-library boundary with clang?
2. What do registration and unregistration need?
3. Which state survives a reload?

## Answers
| question | answer | evidence |
|---|---|---|
| 1. modules across the boundary | **yes**, in both link variants: every symbol resolves at load; the host's counter, inline static, thread-local, ImGui context and allocator and ImPlot context have one identity; static construction and destruction run once, in order; the command answers over MCP; the window draws | S1 driver, 58 of 58 in (a) static engine with an exporting executable (run 15) and (b) shared engine libraries (run b6) |
| 2. registration | four registries with unregistration (config sections, operations, windows, history commands), an atomic commit, and a host that holds every callable a plugin hands over. A failed build, load, Register or commit leaves no plugin and no partial registration | S1 failure stages; S2 driver, 105 of 105 in both variants |
| 3. state across a reload | the scene, the config (saved payloads validated again), the undo history and host state survive, over 20 ASan-instrumented cycles. Jobs are cancelled and reaped and GPU work retired before the old image goes, within 5 s | S2 driver; S2c draining checks |
| undo and redo across a reload | **yes** with registered commands (entries hold data); **no** with today's closure entries: after the unload the undo crashed inside the closed image's former range (the S2b control) | S2b control, 15 of 15 in both variants |
| R4, save to running | 4.49 s median with shared libraries, 9.74 s with the static engine (5.4 s of it the host-module collection); both under 10 s, spike fixture only | [R4 baseline](2026-11-01-arch-022-r4-baseline.md) |

Observed, not compared: (a) an executable of 738 MB and a plugin of 43 KB; (b) an executable of 7.4 MB plus 13 engine
libraries and a plugin of 4.8 MB. Variant (b) needed `CMAKE_POSITION_INDEPENDENT_CODE` (a static vcpkg archive went
into a shared library).

## Recommendations and decisions
- **Link variant: shared engine libraries for dev builds.** A plugin builds like any target; the static variant
  needs a host-module collection on every build. Decided (Q52); CI and release stay static and load no plugins.
- **Plugin boundary:** `Extrinsic.Runtime.PluginContract` with versioned entry points and a build id that covers the
  build settings and every engine module interface; the shape is in the
  [target](../architecture/engine-and-feature-modules.md#plugins-what-goes-where-operator-2026-10-30).
- **History-entry form:** registered commands with a data payload (`RegisterCommand`, `MakeRegisteredCommandRecord`).

## Slice 3 step 1: the spike host as the runtime's plugin host
The host moved to `src/runtime/Services/` as `Extrinsic.Runtime.PluginHost`, a runtime module, and the contract as
`Extrinsic.Runtime.PluginContract`. The spike option, its Sandbox code and the static variant are deleted. Its
verification, the reviews and two findings (the engine's module order by name; the R4 driver ending a session
without `app_quit` after a failed sample) are recorded in the `ARCH-022` task log, entry "Slice 3 step 1b".

## Not shown
- R4 for a typical plugin with several translation units.
- dlopen in CI: CI is static; the drivers run on the shared dev build only.
- A plugin kept from an older engine interface state: the stale-id fixture returns an artificial id, which shows
  the refusal but not that an old build carries its old id (the id is a macro compiled into the plugin).
- GLFW's exactly-once static teardown in shared builds (its test runs in static builds only).
