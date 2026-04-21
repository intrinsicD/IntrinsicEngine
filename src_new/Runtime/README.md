# Runtime

`src_new/Runtime` is the composition root for the `src_new` engine. It owns
the explicit instantiation order of subsystems, the frame loop, and deterministic
startup/shutdown.

## Public module surface

- `Extrinsic.Runtime.Engine`

## Frame loop contract

The engine runs a single canonical frame shape:

```text
begin_frame → extract_render_world → prepare_frame → execute_frame → end_frame
```

No subsystem bypasses this shape. Platform event pumping, simulation, render
extraction, and GPU maintenance are explicit phases — not implicit side effects.

## Directory layout

```text
Runtime.Engine.cppm
Runtime.Engine.cpp
```

## Dependency note

`Runtime` depends on `Core`, `Assets`, `ECS`, `Platform`, and `Graphics`. It
wires them together but does not itself own rendering or scene logic. Every
subsystem it instantiates is injected by reference into the others that need
it — no global state, no service locator.
