# Assets Architecture

`assets` manages asset identifiers, metadata, loading contracts, and ownership boundaries.

## Responsibilities

- Stable asset IDs and lookup paths.
- Serialization/deserialization seams.
- Import pipeline interfaces for runtime and tools.

## Dependencies

- Allowed: `core`.
- Disallowed: direct dependency on graphics/runtime/app layers.
