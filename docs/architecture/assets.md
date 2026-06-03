# Assets Architecture

`assets` manages asset identifiers, metadata, loading contracts, and ownership boundaries.

## Responsibilities

- Stable asset IDs and lookup paths.
- Serialization/deserialization seams.
- Import pipeline interfaces for runtime and tools.
- CPU-only typed payloads and decoder-callback bridges for geometry routes,
  model scenes, textures, and external-resource diagnostics.
- Asset-owned primary/external byte transport for model/texture decoders while
  runtime owns concrete tinygltf/stb registration and later ECS/GPU handoff.

## Dependencies

- Allowed: `core`.
- Disallowed: direct dependency on graphics/runtime/app layers.
