# ADR 0031: Declared operations over one shared operation runtime

- **Status:** Accepted (operator, 2026-10-29: "Do 1. - 3.": accept, lift the property-write limit for operations, start phase 0 and the pilot)
- **Date:** 2026-10-29
- **Owners:** runtime (editor operations), with geometry and Sandbox app owners for their seams
- **Related tasks:** `ARCH-021` (this design), `ARCH-020` (moonshot tracker, M2), `RUNTIME-343`,
  `RUNTIME-344`, `RUNTIME-345` (declared fields and generated panels), `RUNTIME-311` (shared GPU
  transaction lifecycle), `RUNTIME-314` (operation contract follow-ups)

## Context

### What an operation costs today

`src/runtime/Editor` holds 52,047 lines (2026-10-29). Read-only inventories of the code (checked on
samples) put about 1% of it into algorithm code; the algorithms live in `src/geometry` and
`src/graphics`. The rest is the code around each operation:

| Share | Concern |
|---|---|
| 24% | execution plumbing: job submission, delivery, GPU run adapters, publication timing |
| 19% | undo and commit: per-operation state capture, comparison and restore |
| 18% | validation, readiness, config sections and their wrappers |
| 14% | kernel adaptation: copying scene data into method inputs and results back |
| 13% | view models for panels |
| 11% | other: module boilerplate, service wiring, cache signatures |

Three operations traced end to end (file and line references in the `ARCH-021` log):

| Operation | Shape | Files touched | Operation-specific lines outside `src/geometry` |
|---|---|---|---|
| Kernel density | one float output, CPU and GPU | ~11 | ~425 (its method trait is 31) |
| Outlier analysis | two outputs, CPU/LBVH/GPU, a second compacting mode | ~12 | ~1,890 |
| Mesh remesh | topology replacement, explicit parameters, CPU job | ~12 | ~830 |

One operation spreads over the config module (`src/runtime/Modules/<Op>`), a hand-written config
codec (`src/runtime/Config/internal/*Codecs.cpp`, 136-200 lines each), 4-6 line Apply/Get config
wrappers (24 and 20 of them), the command, readiness and job code in `src/runtime/Editor/Operations`,
a result struct with about six touch points in the workspace session (sink, snapshot member, slot,
dismiss case, `retain` line), an agent row in `src/runtime/Agent`, a panel section in
`src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp` (5.6k lines for all panels), and tests.

### What is already shared

The pieces a common operation runtime needs exist, but each serves a subset of operations:

- **Parameters:** `ConfigFieldSpec` tables drive validation, JSON schema and panel hints for every
  config section. `BoundField<Cmd>` (`RUNTIME-343`) binds fields to command members and drives range
  checks, agent params and generated panel controls (`RUNTIME-344`), but only for five commands.
- **Execution:** `JobDesc` and the editor job surface (submit, cancel, progress, outcome);
  `QueuedJobDelivery<Result>` (9 operations); `GpuTransactionCore` (`RUNTIME-311`, all 5 GPU
  transactions); `PointScalarTransaction` (5 users); `FramedGpuJob` (4 users).
- **Undo:** `EditorCommandHistory` (the only undo owner), `ExecuteUndoableEntityMutation` (13
  files), `CommitMeshTopologyReplacement` (3 topology operations), `PublishPointScalarField` (one
  history entry over N scalar columns; 7 users). Two further private snapshot schemes exist
  (`PropertyGraphDetail::Snapshot`, the curvature property capture), and about 12 operations still
  write their own history lambdas.
- **Readiness:** `ActionReadiness` with coded, field-level reasons; only 5 of 27 `Preview*`
  producers fill them, the rest report text that arrives as `Unclassified`.
- **The nearest thing to the target:** `PointFieldWork<M>` in
  `Runtime.PointFieldOperations.Processing.cpp`: a static trait struct per method (config, result,
  names, GPU params, output slot, `Validate`, a pure `Evaluate`) and one template that does capture,
  compute, publication, preview, apply and the GPU start for kernel density and point spacing.

### Gaps the inventories found

- No traced worker reads job cancellation (outlier analysis, kernel density, remesh); others do.
- `GpuQueryBatchSize` is persisted and drawn in panels for the point scalar operations but read by
  no runtime path (normals and consolidation do read theirs).
- The synchronous fallback (`if (!JobCommands.Available()) { Compute; Publish; }`) is written per
  operation.
- Agent results of config-sourced operations carry only status and message.

### How mature tools structure this

Large tools converge on parameters declared once and a framework that owns the surrounding
lifecycle; how pure the operation itself is varies.

- Unreal's modeling tools declare tool parameters as reflected property sets, run a mesh operator
  produced by a factory off the UI thread, cancel and restart it when parameters change, and hand
  the result back on accept (`UMeshOpPreviewWithBackgroundCompute`, `TGenericDataBackgroundCompute`).
  Undo records object state through `Modify()` inside a transaction, with custom changes for what
  that does not cover.
- Houdini's SOP verbs declare their parameter interface as a dialog-script string; a generated,
  typed parameter header (`SOP_*.proto.h`) is what `cook()` reads, and the node delegates to the
  verb.
- Blender operators declare properties, `poll()` and `exec()`; the redo panel and the Python API
  are generated from the properties; undo is a stack of stateful and differential steps, mostly
  global. Operators may mutate the context directly, and Houdini verbs may cook in place: a pure
  operator over a copy is Unreal's modeling-tool pattern, not a universal one.
- Unreal's parameter declarations are reflected through generated code (the header tool), so the
  choice there is a code generator, not runtime type information.

Sources: [UMeshOpPreviewWithBackgroundCompute](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ModelingComponents/UMeshOpPreviewWithBackgroundComp-),
[TGenericDataBackgroundCompute](https://docs.unrealengine.com/4.27/en-US/API/Plugins/ModelingComponents/TGenericDataBackgroundCompute/index.html),
[UModel::Modify](https://docs.unrealengine.com/4.27/en-US/API/Runtime/Engine/UModel/Modify/index.html),
[FTransaction](https://docs.unrealengine.com/4.26/API/Editor/UnrealEd/Editor/FTransaction/index.html),
[SOP_NodeVerb](https://sidefx.com/docs/hdk/class_s_o_p___node_verb.html),
[SOP_CopyPacked.C](https://www.sidefx.com/docs/hdk/_s_o_p___copy_packed_8_c_source.html),
[Blender undo system](https://developer.blender.org/docs/features/core/undo/),
[bpy.types.Operator](https://docs.blender.org/api/5.0/bpy.types.Operator.html),
[Unreal Header Tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine).

### Constraints

- `docs/architecture/method-api-contract.md` (canonical): methods are pure
  `Execute(Input, Params) -> Expected<Result, MethodError>`; **typed, method-specific records are
  preferred over a dynamic universal slot schema**; slots bind full `GeometryPropertyRef`s.
- `-fno-exceptions -fno-rtti`; type erasure today is `std::function`, `std::move_only_function`,
  function pointers and internal `extern "C++"` templates.
- `AGENTS.md`: plain structs and free functions; abstraction only for a present second caller.
- `kernel-target-state.md`: domain extension points are module-provided, never kernel knobs.
- Earlier task-scoped limits: `RUNTIME-311` ("Do not introduce a service, registry, additional job
  hop or public template framework") and "no generic property-write API" in the scope notes of
  `CORE-011`, `GEOM-109`, `RUNTIME-285`. The first is respected (the framework here is internal);
  the second was a scope limit of those tasks, which this ADR lifts for operations (Decision 9).

## Decision

An operation is **one typed definition**; one internal operation runtime does the lifecycle around
it. The definition is a static struct, checked at compile time, in the operation's module
directory. There is no runtime registry and no dynamic schema. The decisions below say what the
definition declares, what the runtime owns, and what stays with the operation.

### 1. The operation definition

A definition declares, and only declares:

- **Identity:** a stable name (agent, jobs, outputs), a history label, and its config section
  identity (name, schema id, version) when it has one.
- **Parameters:** its typed parameter record (a config section struct, or a command struct for
  explicit-parameter operations) and one table of bound fields over it.
- **Entity rule:** whether the target entity comes from the parameters (persisted config) or from
  the call (an argument), which entity sources and element domains qualify, and the menu path the
  panel is discovered under.
- **Input kind** and the parameter fields that name its input slots.
- **Output kind** and the fields that name its outputs.
- **Compute:** a function `Compute(params, input, context)` returning the outputs plus a small
  typed report, or a failure with a reason and the backend that ran. It calls the `src/geometry`
  method; it touches no ECS, history, jobs or GPU state.
- **Optionally:** a cross-field `Check(params)` returning a field-level reason, a GPU backend
  (Decision 6), a custom publication (Decision 5), a `Describe(report)` for agent and panel results,
  and a custom panel.

Illustrative sketch (names to be settled in phase 0):

```cpp
struct KernelDensityOperation
{
    static constexpr std::string_view Name = "kernel_density", Label = "Estimate kernel density";
    using Params = KernelDensityConfig;                    // persisted section, edited by panel, agent, files
    static constexpr SectionIdentity Section{kKernelDensityConfigSectionName, kKernelDensityConfigSectionSchemaId, 1};
    static constexpr EntityRule Entity = EntityRule::FromParams;
    static constexpr std::array Fields{                   // one declaration: codec, checks, schema, panel
        BindEntity<&Params::StableEntityId>(),
        BindToken<&Params::Backend>({.Name = "backend", .EnumValues = kBackendTokens}),
        BindRef<&Params::Positions>({.Name = "positions", .RefKinds = kVec3Kinds}),
        BindRef<&Params::Density>({.Name = "density", .RefKinds = kScalarKinds}),
        Bind<&Params::KNeighbors>({.Name = "k_neighbors", .Type = UInt, .Min = 1, .Max = 256}),
        Bind<&Params::Bandwidth>({.Name = "bandwidth", .Type = Float, .Min = 0}),  // stored as float: checked as sent and as stored
    };
    using Input = PointSamples;                           // framework input kind
    static constexpr auto Inputs = Slots(&Params::Positions);
    using Output = PropertyColumns<1>;                    // same cardinality, one named column
    static constexpr auto Outputs = Slots(&Params::Density);
    struct Report { float Bandwidth, Mean, Minimum, Maximum; };
    static Expected<Computed<Output, Report>, OperationFailure> Compute(
        const Params&, const Input&, OperationContext&);  // calls Geometry::PointCloud::EstimateKernelDensity
    static constexpr PointScalarGpu Gpu{.Method = 0};      // optional, Decision 6
};
```

### 2. Parameters declared once, validated before conversion

`BoundField` (`RUNTIME-343`) grows the row kinds config sections need: property references
(`BindRef`), string-enum tokens mapped to enumerators (`BindToken`), and the entity id
(`BindEntity`). From one table the runtime derives what is written by hand today, under these rules:

- **Validation happens on the payload, before conversion.** The derived codec merges the payload
  over the serialized defaults, rejects unknown keys, checks every declared field on the JSON value
  (type, range, tokens, nested reference shape and kinds), and checks a stored representation too
  where it differs (a positive float that would round to zero in `float` storage is refused, as
  today's kernel density codec does and as `RUNTIME-330` does for style rows). Only then is the
  record converted. `Check(params)` is for cross-field rules on the converted record and reports a
  field.
- **The section identity is declared** (name, schema id, version) and kept. The derived codec must
  read every current-format payload to the same record and serialize records to the same canonical
  JSON as the hand-written codec (golden round trips per migrated section). Older formats are not in
  scope; none is supported today.
- **Agent rows keep their shape.** A config-sourced operation still refuses `params` (it reads its
  section; the agent sets it with `config_apply`); an explicit-parameter operation takes `params`
  from its table, as `ParamsOf` does now. The derived rows must equal today's, so the committed tool
  schema baseline stays unchanged.
- The default panel controls come from the same table (`DrawBoundFields`), and range, enum and
  reference checks produce field-level readiness reasons.

### 3. Execution stages

The runtime runs every definition through the same stages. Each stage has one owner and a defined
failure:

| Stage | Thread | Owner | Result |
|---|---|---|---|
| 1. Resolve | main | runtime | entity, domain and declared-field checks, `Check`; refused with a field reason, or continue |
| 2. Input readiness | main | input kind | ready, **pending** (a deferred verdict, as `PreparePointInput` caches today), or refused |
| 3. Capture | main | input kind | an immutable snapshot plus staleness watches; refused on a mismatch |
| 4. Acquire | main | input kind | resources taken after capture (the shared spatial index snapshot; GPU residency); refused or stale |
| 5. Execute | worker, or inline | definition | `Compute`; outputs, report and backend, or a failure |
| 6. Gate | main | runtime | cancelled, abandoned or stale (watches no longer current): nothing is published |
| 7. Publish | main | output kind | one history entry, labelled with the definition's label and the caller's prefix (`Agent: ` is carried as `CarryEditorLabelPrefix` does) |
| 8. Deliver | main | runtime | exactly once, to the result slot and the caller's sink |

- **Preview and apply take different paths.** Preview (readiness) runs stages 1-2: pending and
  refused are different answers; a panel shows pending as "checking" with the action disabled, and
  the agent's preview reports it as pending, not as a refusal. The deferred-verdict cache stays the
  input kind's. **Apply never waits for that cache:** it runs stage 1 and then captures and
  validates afresh at stage 3, as `CapturePointInput` does today, so a pending or stale preview
  neither blocks nor replaces apply's own checks (the existing session tests that submit while
  readiness is pending keep passing).
- **Cancellation is cooperative.** The runtime checks it before stage 5 and at stage 6;
  `OperationContext` lets `Compute` poll it between chunks. A method call that does not poll runs to
  its end and its result is dropped at the gate. This is the behaviour the job service already
  offers; the change is that every operation uses the same two checks.
- **The synchronous fallback** (no job lane) runs stages 3-8 inline in the same call, with the same
  statuses and messages, and delivers before returning. It is written once, in the runtime.

### 4. Input kinds

The runtime offers a small closed set of input kinds; each owns stages 2-4 for its data:

- **PointSamples:** live samples of any element domain (`PointInputCapture` and its revision
  watches; optional acquisition of the shared spatial index after capture).
- **MeshSurface:** a halfedge mesh for processing (`BuildHalfedgeMeshForProcessing` plus signatures).
- **MeshTopologyEdit:** the same for topology replacement (`BuildHalfedgeMeshForTopologyEdit`).
- **PropertyGraph:** samples and edges on any domain (`PropertyGraphDetail::CaptureSamples`).

A new kind is added when a second operation needs it, not before. Phase 0 builds only
`PointSamples`.

### 5. Output kinds and the property-write transaction

- **PropertyColumns<N>:** N same-cardinality columns on the input's element domain. Published by
  one **property-write transaction**:
  - columns may differ in kind (each column carries its own storage kind; the transaction stores
    exact values for every property kind, generalizing `GeometryScalarPropertySnapshot`);
  - two outputs naming the same destination are refused at stage 1;
  - an output may alias an input property only when the definition declares it; its before-state
    is captured at stage 3 like any output;
  - only live slots are written; deleted rows keep their values;
  - every guard and representability check runs before any column is written; then all columns are
    written, one history entry is recorded that restores exact values and a missing column's
    absence, and the revision watches are rebased.
  It generalizes `PublishPointScalarField` (scalar kinds only today). Phase 0 needs only what the
  scalar pilot uses; vector kinds follow with their first operation.
- **TopologyReplacement:** a new halfedge mesh, published by the existing
  `CommitMeshTopologyReplacement`.
- **Custom:** publications the kinds above do not express, written by the definition with
  `ExecuteUndoableEntityMutation` or the property-write transaction. Known members: outlier
  analysis (its columns plus the `AnalysisStamp` component, and its compacting mode), curvature
  segmentation (face and edge properties in one step), transforms, new entities. The runtime still
  does stages 1-4, 6 and 8, and exposure, for them. Savings estimates count these operations only
  for what the runtime takes over.

### 6. GPU backends

A definition may declare a GPU backend. The runtime owns the lifecycle; the definition owns the
method-specific steps:

| Step | Owner |
|---|---|
| Admission (backend available, inputs resident, kinds supported) | definition, through the runtime's admission call |
| Output reservations (one ring per output, float or uint32; optional presentation ring) | declared by the definition, created by the runtime |
| Recording the dispatch, polling a chunked run | definition |
| Decoding the readback into report fields and statistics | definition |
| Running, readback, preview, Accept, Discard, auto-accept, teardown, staleness | runtime (`GpuTransactionCore`; scalar outputs through `PointScalarTransaction`) |
| Publication on Accept | the output kind (stage 7), as for a CPU result |

- **Provenance:** every result carries the requested backend, the actual backend and a fallback
  reason, as the method contract requires. When the requested GPU backend cannot run, the
  operation follows its declared policy: refuse (today's behaviour for the point scalar operations)
  or fall back to its CPU backend, reported as a fallback.
- **Cancellation** while a run awaits readback discards the run (no publication); a run awaiting
  Accept is discarded by Discard or by staleness.
- Phase 0 covers the scalar shape only (one or more scalar output rings, run mode); the normals and
  smoothing shapes migrate later with their operations.

### 7. Results and module exposure

- **Result storage without RTTI or a central variant.** The workspace session keeps, per
  operation name, a common header readable without the operation's type (status, message, requested
  and actual backend, run key, entity, world, completion order) and the typed report in a slot
  owned by a per-definition accessor: `OperationResults<Def>` stores
  `std::shared_ptr<const Result<Def>>` under a key that is the address of a per-definition constant,
  so the typed access is a `static_cast` justified by the key, not by runtime type information.
  Jobs lists, the agent and the generic panel read the header; the operation's own panel reads the
  typed report.
- **Run identity and ordering:** every apply is given an immutable invocation id at stage 1 from a
  per-session counter (submission order), for queued, inline and GPU runs alike; a GPU run's Accept
  and Discard carry the id of the run they finish. The result header stores it. Each operation has
  one result slot (its last result, as today's panels show); a delivery whose id is older than the
  one stored in that slot does not replace it, whichever entity either run targeted, so a late
  result of an older run cannot overwrite a newer one. `EditorOperationRunKey` stays the lookup
  key for jobs and progress, not the identity. Dismissal clears the slot; results for a detached
  workspace are dropped, as `GuardEditorProcessingResult` does.
- **Module surface.** The framework is one exported module, `Extrinsic.Runtime.OperationRuntime`,
  for operation modules only. Its interface holds the definition vocabulary (bound-field helpers,
  input and output kinds, `OperationContext`, result header) **and the definitions of the runtime
  templates**, so an operation's implementation unit can instantiate `OperationRuntime<Def>`; its
  implementation unit holds the non-template machinery (capture, the property-write transaction,
  result storage, the stage driver). "Internal" in this ADR means not part of the runtime's surface
  for apps and tools, not hidden from operation modules. Each operation module exports its
  definition's declaration (the parameter record, field table, names, report) and non-template entry
  points (`Preview`, `Apply`, `Start`, the agent row and section registration builders); its
  implementation unit instantiates the runtime explicitly. Consumers (config, agent, Sandbox) use
  only those entry points and the exported definition. A layering rule forbids operation modules
  from including `Editor/internal` headers, which today's module operations do; the framework
  module is their only route into the editor.
- **Agent and config:** one line per operation builds its agent row (`OperationRow<Def>()`) and its
  section registration (`RegisterOperationSection<Def>()`). The lists stay explicit.
- **Panel:** a default `DrawOperation<Def>` in the Sandbox app: entity picker, generated fields over
  a draft of the active config (`ProcessingDraftState<Params>`, synchronized with the active
  section), action button with readiness, GPU controls, result summary. It is listed once per
  operation under the definition's menu path and domains. A custom panel replaces it where the
  design needs it (M2's escape hatch); it still reads the same definition.

### 8. Placement

- The definition and its compute live in `src/runtime/Modules/<Op>/`, beside its config today.
- The operation runtime lives in `src/runtime/Editor`. `src/runtime/Editor` keeps the framework:
  history, jobs, GPU transactions, readiness, workspace, view models.
- Methods stay in `src/geometry` and `src/graphics`; custom panels stay in the Sandbox app.

### 9. Relation to earlier limits and to M2

- `OperationRuntime<Def>` is a template exported only to operation modules (Decision 7), in the
  style of `QueuedJobDelivery` and `PointFieldWork<M>`; it is not part of the runtime's public
  surface, not a service or a registry, and adds no job hop, so `RUNTIME-311`'s limit (no public
  template framework) holds.
- The property-write transaction lifts the "no generic property-write API" scope notes of earlier
  tasks for operations, for the reasons in Context.
- M2's fallback for unusable C++26 reflection is a schema compiler emitting C++. The definitions
  here are the C++ such a compiler would emit; writing them by hand first settles their shape, and
  a compiler can replace the hand-written tables later without changing the runtime.

### 10. Non-goals

- Reloading operations at runtime or a stable binary interface (M3).
- Runtime reflection, a runtime registry, or a universal slot schema.
- Migrating every operation at once.

## Consequences

### Positive

- Adding an operation means: the method in `src/geometry`, one definition (interface and
  implementation unit), its tests, and optionally a custom panel.
- The candidates a panel or the agent offers, the config validator and the command's checks come
  from one table, which is M2's central point.
- Readiness reasons, cancellation checks, the gate before publication, results and the synchronous
  fallback are written once, so the gaps found (unobserved cancellation, text-only readiness) close
  for every migrated operation.
- `src/runtime/Editor` becomes the framework; per-operation code leaves it.

### Trade-offs and risks

- **Template cost.** Compile time and error messages grow with every definition. Mitigation: a thin
  template layer, explicit instantiation in each operation's implementation unit, non-template work
  in ordinary functions; incremental and clean builds measured in the pilot.
- **A framework that grows to fit everything.** Mitigation: closed input and output kinds, a
  second-caller rule for new kinds, and the custom escape hatch.
- **An easy pilot passing while hard publications stay bespoke.** Mitigation: the decision gate
  comes after outlier analysis (Validation), not after the scalar pilot.
- **Behaviour drift during migration.** Messages, statuses and refusals are pinned by existing
  tests. Each migration keeps them or changes them in its own, stated slice.
- **Resource cost.** A run holds its input snapshot, the undo history holds before and after
  columns (as today), and GPU staging can coexist with both; capture runs on the main thread.
  Peak memory and capture time are measured in the pilot on the largest test inputs.
- **Escape-hatch overuse.** Each custom publication is named in its definition and reviewed.

### Follow-up tasks

- Phase 0: `BindRef`/`BindToken`/`BindEntity` and the payload-first codec; the property-write
  transaction for scalar columns; the operation runtime with stages 1-8, the `PointSamples` kind,
  the scalar GPU shape, result slots; the framework module and the layering rule; a table of named
  operations with their measured removable code (the projection basis:
  `docs/architecture/declared-operations-table.md`, an estimate until phase 2 measures).
- Phase 1: kernel density and point spacing (implementation shakedown).
- Phase 2: outlier analysis (two outputs, GPU, custom publication): the architectural test.
- Phase 3: mesh remesh (topology replacement, explicit parameters, argument entity).
- Independently: wire or remove the unused `GpuQueryBatchSize` of the point scalar operations.

## Alternatives Considered

- **Keep the structure, add glue helpers** (shared GPU adapter helpers, an undo convenience layer,
  config wrappers): about 1.5-2.5k lines, but each operation still owns capture, commit, results
  and exposure. It remains the fallback if the contract is rejected at the gate.
- **A code generator from schema files** (as Unreal's header tool or Houdini's generated parameter
  headers): not rejected; it is M2's own fallback and can generate the definitions later
  (Decision 9). Starting with hand-written C++ definitions avoids a build step before their shape
  is proven; the decisive point is integration cost, not the absence of runtime type information.
- **Runtime reflection and a registry:** rejected: no usable C++26 reflection in the pinned
  toolchain, `-fno-rtti`, and the method contract prefers typed records.
- **A virtual `IOperation` interface:** rejected. Typed parameters, inputs and results would need
  casts or variants across the interface; static definitions keep them typed.
- **Moving files into modules without changing the contract:** rejected as a goal in itself. It
  moves lines without removing them, and the modules would keep reaching into editor internals.

## Validation

### Accounting

- **Operation-specific code:** every line in the operation's own files and every line it adds to
  shared files (registrations, exports, build lists, panel listings, session slots), outside
  `src/geometry` and `src/graphics` method code; tests counted separately.
- **Framework code:** counted once, in the phase that adds it.
- **Files touched:** every file a change edits, registrations, build lists and tests included.
- **Two measured changes, with the structure Decision 7 implies:**
  - *Adding an operation:* its interface and implementation unit, its test, one line each in the
    agent row list, the section registration list, the panel listing and the build list, the
    regenerated module inventory (`docs/api/generated/module_inventory.md`) and the tool schema
    baseline (`tests/data/agent_declared_tool_schemas.json`, where `run_operation` gains its name):
    at most 9 files, of which 3 are its own and the other 6 are one-line or generated changes.
  - *Adding an option* (a declared field) to a migrated operation: the record and field table
    (interface), its use in `Compute` (implementation unit) and its test: **at most 3 files in
    total**, M2's criterion; agent schema, codec, checks and panel control follow from the table.

### Gates

- **After phase 1 (implementation shakedown, may stop or adjust the implementation, does not accept
  the contract):** kernel density and point spacing each fit the "adding an operation" structure
  (at most 9 files, 3 of them their own) with at most 150 operation-specific lines (today about 11
  files and 425 lines for kernel density); their existing
  tests pass unchanged, including messages and statuses; the agent tool schemas stay equal to the
  committed baseline; derived codecs pass golden round trips; incremental rebuild after editing one
  definition and a controlled clean build are each no more than 10% slower; peak memory on the
  largest test input does not grow.
- **After phase 2 (the architectural decision):** outlier analysis on the contract keeps its tests,
  its custom publication fits Decision 5 without bypassing stages 1-4, 6 and 8, and its
  operation-specific code falls by at least 40%. Adding an option to a migrated operation touches
  at most 3 files in total, its test included (M2's criterion, measured by doing it). The
  projected net saving over the named operation table is at least 5,000 lines.
- **Failure classes:** a missed size, build-time or memory threshold is an implementation failure
  to fix within the phase; outlier analysis needing to bypass the runtime's stages, or the
  three-files criterion failing, rejects the contract, and the glue-helper alternative is taken.
- **After phase 3:** remesh confirms the topology output kind and the argument entity rule.

### Contract tests

- Per definition, generated: every bound field round-trips through the codec, the agent schema and
  the panel control; the validator and the agent agree on every row (as `DeclaredFieldContracts`
  does for style tables); golden section payloads round-trip unchanged.
- Per stage: pending versus refused readiness; capture then acquisition failure statuses;
  cancellation before execute and at the gate; the synchronous fallback delivers once with the same
  statuses; the history label carries the agent prefix.
- Per output kind: the property-write transaction restores exact values and absence, refuses
  duplicate destinations and stale guards without writing, preserves deleted rows, and leaves one
  history entry.
- GPU: CPU, GPU preview with Accept and Discard, GPU auto-accept, and an unavailable backend
  (refusal or declared fallback with provenance).

### Metrics recorded in ARCH-020

Files and lines per migrated operation; framework size; build times; `src/runtime/Editor` lines
against M2's 25k objective, which stays the program target.
