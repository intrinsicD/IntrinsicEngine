# App

`src/app` hosts reference and test applications built on top of the
engine runtime. The canonical entry point is `Sandbox`.

## Applications

- `Sandbox/` — reference integration target. Links to `Runtime`, opens a window,
  drives the frame loop with the runtime-owned reference configuration, and owns
  Sandbox-specific editor presentation registered through runtime facades. Its
  default composition explicitly adds the optional runtime camera module and
  owns initial-world reference-content bootstrap/teardown policy.
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

The concrete Sandbox application resolves the exact optional
`Runtime::CameraControllerRegistry` once after module registration. It passes
that pointer to default import/input policies and, when reference content is
enabled, seeds the registry only for the initial owning world. The app retains
the matching reference population and tears it down only through that world.
Generic Engine neither composes `CameraModule` nor interprets
`ReferenceSceneConfig`.

The converged Engine surface is not an app service locator. Sandbox imports
`Extrinsic.Runtime.InputActions` explicitly and registers its default focus
action on the published `RuntimeInputActionRegistry`; Engine has no
input-registration or record re-export facade. Render-extraction and
visualization observations likewise come from their published owning cache,
while renderer statistics stay on `IRenderer`. The only retained diagnostic
getter used by the executable is the read-only
`GetLastFramePacingDiagnostics()` sample for the bounded report mode.

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
