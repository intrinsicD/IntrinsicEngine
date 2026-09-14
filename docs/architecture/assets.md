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

## Geometry-format capability authority

`src/core/Core.GeometryFormatCatalog.inc` declares the geometry format kinds,
canonical extensions, aliases, import/export domains and binary capability flags
once. It contains data tokens only. `Geometry.IO` and `Asset.ImportRouter` expand
those rows into their own typed enums and tables; asset-only model/texture rows
stay in the asset router. Neither layer imports the other.

An importable geometry row must resolve through the asset router for every
declared domain and alias. Export domain sets and binary flags must agree too.
The cross-layer contract test in `Test.AssetWorkflowModule.cpp` checks both
directions, including negative domain/operation combinations. Runtime selects
the existing concrete loader and preserves format-specific validation and
property semantics; recognizing a filename alone does not prove a working import.

OFF has a geometry writer and a mesh export route. Export UI remains separate
work. PWN, CSV, 3D and TXT use their dedicated point-cloud loaders; existing
XYZ/PTS/XYZRGB routes retain their current permissive XYZ parser behavior.
