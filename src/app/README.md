# App

`src/app` hosts reference and test applications built on top of the
engine runtime. The canonical entry point is `Sandbox`.

## Applications

- `Sandbox/` — reference integration target. Links to `Runtime`, opens a window,
  and drives the frame loop with the runtime-owned reference configuration. Used
  to validate the composition root end-to-end without adding specialized app
  behavior.

## Directory layout

```text
Sandbox/
  Sandbox.cppm
  main.cpp
  CMakeLists.txt
```

## Dependency note

`App` depends on `Runtime` only. Runtime transitively owns lower subsystem
composition and exposes app-facing helpers such as the reference engine
configuration. Applications must not import lower engine layers for convenience
and must not expose types back to the engine. No other engine layer may import
an application module.
