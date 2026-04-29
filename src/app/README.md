# App

`src_new/App` hosts reference and test applications built on top of the
`src_new` engine. The canonical entry point is `Sandbox`.

## Applications

- `Sandbox/` — reference integration target. Builds the full engine (`Core` +
  `Assets` + `ECS` + `Platform` + `Graphics` + `Runtime`), opens a window, and
  drives the frame loop. Used to validate the composition root end-to-end.

## Directory layout

```text
Sandbox/
  Sandbox.cppm
  main.cpp
  CMakeLists.txt
```

## Dependency note

`App` depends on `Runtime` (and transitively on every subsystem it wires). An
application is the only layer that may depend on everything; it must not
expose types back to the engine. No other `src_new` module may import an
application module.
