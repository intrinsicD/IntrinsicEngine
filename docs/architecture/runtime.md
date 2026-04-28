# Runtime Architecture

`runtime` is the composition root for IntrinsicEngine.

## Responsibilities

- Construct and wire subsystem boundaries.
- Own lifecycle/state transitions for engine execution.
- Mediate between platform, graphics, assets, ECS, and geometry services.

## Non-responsibilities

- Runtime should not become a utility grab-bag for lower layers.
- Lower layers must remain reusable without runtime internals.

## Related references

- Historical details: `runtime-subsystem-boundaries.md` (`legacy-background`).
- Layer policy: [layering.md](layering.md).
