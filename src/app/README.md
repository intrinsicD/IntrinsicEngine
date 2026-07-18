# App

`src/app` hosts reference and test applications built on top of the
engine runtime. The canonical entry point is `Sandbox`.

## Applications

- `Sandbox/` — reference integration target. Links to `Runtime`, opens a window,
  drives the frame loop with the runtime-owned reference configuration, and owns
  Sandbox-specific editor presentation registered through runtime facades.
  Method command/config/result implementations remain private runtime units, so
  app panels do not import their geometry, ECS, graphics, or RHI dependencies.

## Directory layout

```text
Sandbox/
  Editor/
    Sandbox.EditorController.cppm
    Sandbox.EditorController.cpp
    Sandbox.EditorShell.cppm
    Sandbox.EditorShell.cpp
    Sandbox.DomainPanels.cppm
    Sandbox.DomainPanels.cpp
    Sandbox.MethodPanels.cppm
    Sandbox.MethodPanels.cpp
    Sandbox.MeshProcessingPanels.cppm
    Sandbox.MeshProcessingPanels.cpp
  Sandbox.ConfigSections.cppm
  Sandbox.ConfigSections.cpp
  Sandbox.cppm
  Sandbox.cpp
  main.cpp
  CMakeLists.txt
```

## Dependency note

`App` depends on `Runtime` only. Runtime transitively owns lower subsystem
composition and exposes app-facing helpers such as the reference engine
configuration. Applications must not import lower engine layers for convenience
and must not expose types back to the engine. No other engine layer may import
an application module.

`ExtrinsicSandboxEditor` is the app-owned editor composition library. Its
`Sandbox.ConfigSections` module composes the two current runtime-owned Sandbox
config codecs into a pre-boot registry. `main.cpp` passes that registry to
`ResolveEngineConfigForBoot(...)` and then moves it into `Engine`, so file,
agent/CLI, and UI applies share one validated lane without adding Sandbox types
to Core. The same library's
`Sandbox.Editor.Controller` module owns the app-side `EditorShell` and the
Method, Mesh Processing, and Domain panel lifetimes behind one idempotent
attach/detach interface. `EditorShell` owns the ten core Sandbox windows,
menus, ImGui state, and frame presentation; runtime exposes only generic editor
hosting plus data/model/command facades. The Sandbox executable no longer
composes those panel families individually.

`Sandbox.Editor.MethodPanels` also owns the registered
`Mesh > Processing > Parameterize (UV)` window. The window offers only LSCM,
harmonic cotangent, uniform Tutte, and Boundary First Flattening; maintains an
explicit typed draft; and applies that draft through the runtime-owned
registered `sandbox.parameterization` validation lane before executing the
undoable `v:texcoord` command. Its draggable controls/UV split renders the
pointer-free runtime UV model with fit, grid/checker, zoom, pan, and aggregate
last-run diagnostics. The app does not bind a UV/checker material: UV writeback
is visible on the 3D mesh when its existing material already samples those
coordinates.
