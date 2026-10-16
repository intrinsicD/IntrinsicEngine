# Assets Architecture

`assets` manages asset identifiers, metadata, loading contracts, and ownership boundaries.

## Responsibilities

- Stable asset IDs and lookup paths.
- Serialization/deserialization seams.
- Import pipeline interfaces for runtime and tools.
- CPU-only typed payloads and decoder-callback bridges for geometry routes,
  model scenes, textures, and external-resource diagnostics.
- Runtime reads model/texture primary and external resources through Core
  `IIOBackend`, calls concrete tinygltf/stb decoders, and owns ECS/GPU handoff.

## Dependencies

- Allowed: `core`.
- Disallowed: direct dependency on graphics/runtime/app layers.

## Load completion and event ordering

`AssetService` serializes publication, identity/path updates and payload lookup.
Typed loader callbacks run outside the service transaction, producing a payload
that is published only after the provisional ID, path winner or previous reload
ticket is rechecked. A newer reload wins over an older decode; destroy cannot be
undone by rollback. Loaders are supplied per call and are not retained.

`AssetLoadPipeline` serializes lifecycle transitions, registry updates, queued
events and in-flight removal. `CompleteCpuLoad` joins a scheduled or direct
transition before deciding whether the asset is Ready. Cancellation uses the
same lock, so a completing transition cannot publish a late Ready after cancel
returns. The lock order is service, pipeline, then registry/event queue. No
listener runs under these locks. Constructor test hooks pause under the pipeline
mutex and must not enter either the pipeline or `AssetService`.

`AssetService::CompleteCpuLoadAndFlushEvent` joins completion and then dispatches
that asset's pending events outside the service lock, without waiting for
unrelated scheduler jobs. Destroy cancels before dispatch and again afterward
to join a reload started by a callback. A nested destroy retires the ID once;
the outer destroy also succeeds. Nonterminal events are skipped before each
listener if the ID has died. They describe history, not a pinned payload version;
a newer reload may already be visible. Concurrent destroy after the live check
can still make a listener's read fail. Runtime dispatches on the main thread;
other concurrent dispatchers must synchronize their listener state.

Read spans borrow the current payload. The read call is synchronized, but callers
must order span use against reload and destroy of that ID. The facade does not
provide retained lifetime across concurrent mutation.

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
work. PTS, PWN, CSV, 3D and TXT use their dedicated strict point-cloud loaders;
XYZ/XYZRGB routes retain the permissive XYZ parser. PTS (`LoadPTS`) enforces an
optional positive count header exactly and rejects malformed rows, scan-line
markers and non-finite values with `InvalidFormat`, never a partial cloud. For
its accepted 3/4/6/7-column layouts it produces the same positions and colors as
the former XYZ routing (see the geometry architecture note); other column counts
(for example 8, which `LoadXYZ` accepts) are rejected.

## Import validation reports

`Asset.ImportValidation` defines the report shared by runtime decode and CPU
publication. `RuntimeAssetImportEvent::StageTrace::ValidationReport` retains it
on both success and failure, for synchronous and queued model imports. Each
check area has a status: `NotRun`, `NotApplicable`, `Skipped`, `Passed`, `Warning`,
`Unsupported`, or `Failed`. Counts include every finding; diagnostic examples
are bounded per check and status. Unsupported optional glTF features remain
visible; unsupported required extensions and consumed sparse accessors fail.

Model imports report format/buffer checks, references, numeric data, attributes,
face shape, topology, duplicate vertices, transforms, resources, and CPU
materialization. Other import routes populate the checks they actually execute;
`NotRun` does not mean passed. A cancellation before report publication retains
an independent empty report; an unfinished worker cannot update that event.
`MaterializationAttempted` separates decoder failures from CPU publication failures.
CPU publication does not prove GPU visibility.
Reflected transforms are reported as requiring world-winding verification.

Per-call `ImportValidationOptions` on the import request/recipe select duplicate
vertex, face-quality, topology and attribute-quality checks. Mandatory index,
finite-position and consumed-accessor storage checks cannot be disabled.
Supplied normals must also be finite and nonzero, and supplied UVs finite;
unusable data is omitted with a warning even when optional attribute-quality
checks are disabled. The
same options reach queued and synchronous model imports. Duplicate-vertex
checks default off: coincident positions commonly represent authored seams.
No welding or face deletion is performed. Distinct-index zero-area faces remain
in the render mesh with warnings; repeated-index or non-manifold topology can
use the existing disconnected-corner fallback, recorded in the report. Strict
geometry conversions can still reject zero-area faces in later editor operations.
When normals are absent, runtime import generates area-weighted vertex normals
with double-precision cross products, accumulation and normalization, without
absolute length cutoffs. Authored normals remain unchanged. Isolated vertices
and exactly zero accumulated area vectors retain zero normals; near-cancelling
contributions can still leave a direction determined by rounding.

Model/texture source-byte and decoded-image budgets are opt-in per-call limits;
defaults impose no additional byte cap. An explicitly configured source limit
counts the primary file plus unique external resources. Exceeding either budget
fails the import. These limits do not bound total process memory or decoder
scratch allocations.
`IORequest::MaxBytes` rejects oversized file reads before allocation. Image
headers are checked before decompression. External glTF resource existence,
size and data callbacks share one per-import read cache; failed reads are cached
too, and no cache survives a new import. Geometry-format readers have their own
format-specific limits; the model/texture budgets do not apply to them.
