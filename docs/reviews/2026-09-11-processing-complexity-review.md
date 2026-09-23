# Processing complexity review — 11 September 2026

The density/spacing pilot is now implemented in the working tree under
RUNTIME-231 and UI-054; builds, CPU/UI tests, actual Vulkan tests and a bounded
compile check passed. Findings below describe the
pre-pilot snapshot, so their historical line references are not current source
locations. Current ownership is documented in
[Sandbox editor boundaries](../architecture/sandbox-editor-feature-boundaries.md).
The explicit [reuse skill](../../tools/agents/skills/intrinsicengine-reuse/SKILL.md)
provides discovery before implementation and bounded consolidation afterward.

## Verdict and limits

The concern is justified in the runtime/editor processing integration. Several
methods independently implement the same property resolution, revision checks,
neighborhood acquisition, publication, job delivery and panel workflow. Those
mechanisms should have canonical implementations. The geometry kernels and the
contracts around them can remain explicit and typed.

This is a source review of the current uncommitted tree, with two fixed-packet
Claude Code CLI reviews. It is not a completed whole-engine deletion audit or
an implementation of the proposed redesign. The strongest finding is proven
with density and spacing; conclusions about other families are qualified below.
No credible whole-engine deletion percentage follows from this sample.

The previous compilation pass mostly moved declarations, hid storage and split
translation units. Comparing the saved starting source with current `.cpp`,
`.cppm` and `.hpp` files gives **790 → 800 files** and **292,351 → 293,145
physical lines**: ten more files and 794 more lines. Compilation boundaries and
conceptual simplicity are different measures. That pass did not settle this
review's concern, and additional PImpl wrappers alone will not settle it.

## Measured source inventory

Counts include physical lines, comments and blanks in project-owned C++ files
under `src`; exclude external dependencies, tests, shaders, docs and tools.
These are size observations, not a classification of all runtime code as glue.

| Area | Files | Physical lines |
|---|---:|---:|
| Runtime | 219 | 109,731 |
| Runtime editor, included above | 59 | 42,413 |
| App | 18 | 14,215 |
| Geometry | 222 | 78,360 |
| Graphics renderer | 140 | 47,116 |
| Graphics Vulkan | 26 | 12,155 |
| Core | 63 | 10,767 |
| Assets | 19 | 3,674 |
| ECS | 40 | 2,932 |
| Platform | 10 | 994 |

Runtime also contains necessary import materialization, scene persistence,
physics integration and GPU lifetime handling. Its total cannot honestly be
called unnecessary middleware. The sampled processing duplication is much
stronger evidence than the layer totals.

## Findings

### F1 — Same scalar-property operation implemented twice: confirmed

[Density](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Density.cpp)
and spacing (`Runtime.GeometryProcessingOperations.Spacing.cpp`, later merged into the density unit)
are each 449 lines. After trimming whitespace, 363 of their 448 nonblank lines
match exactly in sequence. That includes braces and imports: it is textual
overlap, not a promise that 81% of either file can be deleted.

Both have the same stages: local watch/domain helpers (lines 34–110), capture
(112–193), a method-specific compute adapter (194 onward), framed GPU kNN
continuation (around 229–269), scalar publication and undo (270–300),
readiness/catalog projection (303–333), and job submission/finalization
(334–439). The distinct numerical work already calls geometry-owned
`EstimateKernelDensity[FromNeighbors]` or `EstimateRadii[FromNeighbors]`.

**Collapse:** common live-sample capture, property watches, framed kNN batch
continuation and a guarded scalar-property transaction. Share matching job
delivery through the existing `JobService`; retain direct CPU execution when
jobs are unavailable. Method-owned validation, the kNN width floor, numerical
kernel, statistics and diagnostic vocabulary remain explicit.

**Owner and follow-up:** runtime geometry-processing implementation units;
[RUNTIME-231](../../tasks/done/RUNTIME-231-shared-point-property-operations.md).
No new service, scheduler, plugin registry or public method framework is needed.

### F2 — Existing common helpers bypassed by sibling implementations: confirmed

[PointProperties](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.PointProperties.cpp)
already implements `ObserveGeometryProperty`, `MutableGeometryProperties`,
`PrimaryPointDomain`, `FinitePosition` and `GeometryPropertiesCurrent`.
Density and spacing still define their own equivalents. Normals has another
typed-watch implementation; its topology inputs require additional review.

**Collapse:** make the existing property helpers the canonical owner, then
delete local equivalents where their semantics match. Preserve typed preflight,
property existence, revision and cardinality checks. Process-monotonic revisions
already distinguish replaced storage; this is not permission to drop checks.

[RadiusRows](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp)
is a positive example: descriptors, keypoints and density weights already share
framed radius pagination. Add the corresponding narrowly specified kNN mechanism
instead of repeating its state machine per method. Radius and kNN have different
completion and membership contracts; preserve both.

**Follow-up:** RUNTIME-231. Broaden to other methods only after comparing their
actual input, output, deletion and neighborhood contracts.

### F3 — Processing panel scaffolding copied per method: confirmed

[MeshProcessingPanels](../../src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp)
contains repeated draft records around lines 260–334 and near-identical density
and spacing windows at 2520 and 2909. Selection tracking, config synchronization,
input catalog selection, output naming, execution, status, visualization and
dismissal are repeated around different parameter widgets.

**Collapse:** a shared typed panel state and common interaction helpers in the
existing app support owner. Keep method-specific parameter widgets and rich
result displays. UI and config/agent callers must continue through the same
validated apply functions. A general reflective UI language is unnecessary.

**Follow-up:** [UI-054](../../tasks/done/UI-054-shared-processing-panel-workflow.md),
starting with the same two methods after the runtime pilot.

### F4 — Central result and context wiring grows per method: real cost, redesign unproven

[EditorWorkspaceSession](../../src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp)
references 26 retained `m_Last*Result` members and has 24 method-result dismissal
cases. A result passes through a sink, a session member, borrowed bindings,
context adapters, a copied workspace snapshot and often a panel-local cache.
[EditorFeatureContextAdapters](../../src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp)
repeats corresponding member projections. A new method consequently touches
several central files even when its numerical kernel is independent.

Some copies establish lifetime-safe snapshots; the session and copied view are
not automatically competing authorities. Claude recommends retaining the typed
slots in this pilot, and that is the accepted disposition. An erased result map
would lose useful type information and require migration of unrelated methods.
UI-054 will identify the authority and lifetime purpose of each copy for its two
panels; remove only redundant mutable caches. A broader result-store redesign
requires a concrete demonstrated reduction in central edits, not just fewer
fields in one header.

### F5 — Mechanical forwarders and layout assertions: selective treatment

`Runtime.GeometryProcessingOperations.Public.cpp` (retired by RUNTIME-236)
contained many forwarding overloads. Its `ContextOrEmpty` path also enforced the
attachment epoch and cleared borrowed live state. Removing the guard to eliminate
forwarders would have exchanged visible boilerplate for a lifetime defect. The
file went away only once its last caller moved to a family handle that keeps the
same guard.

[SandboxEditorPresentation](../../tests/integration/runtime/Test.SandboxEditorPresentation.cpp)
also checks source placement and interface size. Such checks can make harmless
ownership changes look like regressions, as the preceding PImpl pass showed.
Keep executable layering and ownership checks, and behavior tests for detach,
staleness, selection, publication and visualization. When a layout assertion
changes during the pilot, explain the invariant it replaces; do not mechanically
raise a line ceiling or delete an inconvenient gate.

## The simpler design

For the demonstrated property-field family, use this sequence:

```mermaid
flowchart LR
    U[UI or config / agent request] --> V[Typed validation and input resolution]
    V --> S[Owned input snapshot]
    S --> N[Optional CPU or framed GPU neighborhoods]
    N --> K[Geometry kernel]
    K --> P[Guarded property transaction]
    P --> R[Typed result and copied presentation]
```

These boxes are responsibilities, not instructions to create six new classes
or files. Reuse the existing geometry API, property helpers, spatial cache,
`JobService`, command history, visualization apply path and panel support.
Common non-trivial bodies belong in compiled implementation units. Small
templates for typed storage are reasonable when two real callers require them.

There should be one implementation of each shared mechanism, and a small
number of explicit operation families:

- Property-producing operations preserve topology and publish named outputs.
- In-place operations additionally define input/output aliasing and spatial-cache invalidation.
- Topology/cardinality-changing operations own remapping, attribute preservation and replacement.
- Multi-entity operations such as ICP validate both identities and their source/target roles.
- GPU work retains its cross-frame execution and resource retirement rules.

Do not force these differences into a growing collection of flags on one
universal executor. Share their common property/history mechanisms where the
contracts coincide, and retain distinct algorithms and publication policies.

## What earns its keep

- `RHI::IDevice` has real Null and Vulkan implementations; `IWindow` has Null
  and GLFW implementations. They provide actual backend substitution and tests.
- There are **12** production `IRuntimeModule` implementors in the reviewed
  tree. The old historical single-consumer objection to this mechanism no
  longer describes the source.
- Geometry algorithms remain independent of live ECS; graphics receives
  copied plans/views; runtime composes them. These ownership boundaries make
  the engine reusable and testable.
- Job cancellation, stale-result rejection, history, property revisions,
  snapshots, GPU completion and retirement carry correctness obligations.
  Share their implementation; preserve the obligations.
- The 11,360-line renderer and 13,482-line runtime asset workflow deserve
  further ownership reviews, but size and role-named files alone do not prove
  dispensable middleware. This review does not prescribe their deletion.

## Claude review and adjudication

The first review independently confirmed the density/spacing duplication and
panel scaffolding. The second accepted a compiled-free-function pilot, allowing
some readable repeated orchestration at the method entry points.

Accepted: use the existing property helpers; share a precisely specified kNN
continuation and scalar transaction; check deleted rows across batch boundaries,
ties/self membership, property replacement before undo, cancellation and actual
GPU behavior; evaluate the net footprint including newly shared code.

Corrections and recommendations not adopted:

- The common property helpers predate the compilation pass. Claude's initial
  attribution to that pass was wrong. Their migration is not “zero risk.”
- Same-name type replacement preserving a revision, a null mutable view and
  the dismissal switch's mixed `return`/`break` were not established defects.
  Revision tokens change on replacement and there is no work after the switch.
- A large header-defined traits executor is unnecessary for this pilot.
  Common result inheritance is not a free change: existing designated
  aggregate initialization and public result fields must remain compatible.
- Map the two public backend enums to one internal neighborhood policy if
  useful. Type-aliasing them is not automatically source-compatible with their
  existing overload sets and serialization APIs.
- Preserve current publication/history ordering. `EditorCommandHistory::Execute`
  calls `Redo()` before recording it; the current mutation closure marks dirty
  state inside `Redo()`. Claude's proposed observer-order test would impose a
  new behavior rather than verify the existing one.
- Reject arbitrary per-file line caps and “no five matching lines” gates.
  They encourage cosmetic changes. Require removal of duplicated mechanisms,
  net deletion after accounting for shared code, preserved behavior and
  measured compilation impact instead.
- The follow-up packet accidentally said 13 module implementations; the
  verified count above is 12. The keep decision is unaffected.

## Delivery plan and success criteria

RUNTIME-231 proves the runtime mechanism with density and spacing first.
UI-054 then shares their panel workflow. Both are proposals in the backlog;
this review changes no production implementation.

The pilot succeeds when a fix to property capture, scalar publication or kNN
pagination is made in one owner and exercised by both methods; the previous
duplicate bodies are gone; total affected production code decreases including
helpers/adapters; typed results, all existing domains, config controls, backend
semantics, visualization and lifetime behavior survive. Record test and build
results on the final combined source. A clean dependency graph alone is not
proof of a simpler engine.

If the pilot works, review bilateral filtering, outlier analysis, normals and
other property methods individually. The adoption criterion is matching
semantics and a net reduction in obligations for a new method. Do not rewrite
the entire engine to achieve a numerical file-count target.

Supplementary local inventory, source hashes and both Claude transcripts are
retained under `build/analysis/right-sizing-2026-09-11/` (ignored artifacts).
The source findings and decisions needed to assess this review are included
above. Verification for this review is documentation/task validation and the
read-only layering check; C++ tests are required by the implementation tasks.

Strict task policy, documentation links, layering, session-brief freshness and
diff whitespace checks passed. Source hashes confirm that all 800 inventoried
production C++ files remained unchanged during this review.

## Implemented pilot and reuse discovery

The pilot removes 137 physical production lines after counting the shared code:
runtime 966 → 853, app 4609 → 4585. One private 47-line PointFields header holds
the shared data declarations for three runtime implementation units; no public
module was added. The methods retain their typed kernels, config/results, width
floors, backend gates and job-delivery closures. UI-specific widgets/statistics
and lifetime-safe result copies remain explicit.

The new `intrinsicengine-reuse` skill supplies intent-based owner routes,
source/caller/test verification, a semantic-contract comparison and bounded
consolidation. AGENTS and the core router invoke it before new mechanisms;
Codex and Claude discover the same skill through the existing shared directory.
Claude's forward test found the geometry IO and radius-query owners and added
the parser-finiteness caveat. Its code-review findings were fixed and the final
UI test guards both output revisions and job submissions around Show.

Verification: IntrinsicTests and Sandbox built; full CPU selector passed 4527
cases with one expected skip, the final focused suite passed 57, and both real
Vulkan candidate-policy tests passed. A bounded same-command object-compile
comparison did not reproduce the high UI timings observed during concurrent
dependency setup; it is a local diagnostic, not a full-build speedup claim.
Source hashes, footprint, review transcripts, logs and validated compile-probe
results are retained locally under `build/analysis/reuse-2026-09-11/`.

## Processing/editor integration follow-through

[RUNTIME-232](../../tasks/done/RUNTIME-232-processing-editor-integration-reuse.md)
extends the pilot with two bounded structural changes:

- The existing 24-field `EditorGeometryProcessingResultsSnapshot` is also the
  session's owned result value. Geometry contexts and private feature bindings
  carry one borrowed pointer; adapter projection and public snapshot copying
  no longer enumerate every result again. A private typed sink factory owns the
  identical epoch guard for 22 sinks. The two service completion subscriptions,
  explicit per-slot dismissal, selected model subset and public copy boundary
  keep their distinct roles.
- Seven more point panels reuse draft synchronization and the primary property
  chooser, including explicit coupled-domain updates and a mesh-vertex filter
  for face-normal input. Four more panels reuse the existing validated execution
  loop. Scalar/label/color Show actions reuse their existing recipe helper.
  Outlier removal, normal estimation, construction and face-lane display retain
  their distinct sequences. Descriptor histogram-follow reset remains explicit.

This removes parallel declarations and repeated mechanisms without adding a
production source file, module, registry or erased result representation. The
borrowed result pointer is cleared in expired contexts; callbacks test their
captured attachment epoch before accessing session storage. Preparing another
processing frame observes current results, while already returned values stay
independent. This also avoids borrowing a pointer to an optional's contained
value after that value has been dismissed.

Local implementation review covered scope, typed result delivery, source/test
context migration, ownership and config sequencing. The compile check caught
and corrected an incomplete-type declaration-order error: the aggregate retains
its definition after the complete result types and is forward-declared for the
context. After the user's explicit payload approval, Claude reviewed the exact
scoped diff. Source inspection ruled out its purported use-after-free: the epoch
check is a free function and runs before session access. The existing Show helper
already dispatches label/scalar/color recipes by typed property reference; both
aggregate declarations are exported, and the full build/source search establish
that the old pointer consumers were migrated.
The combined build of `IntrinsicTests` and Sandbox passes. All 271 focused tests
pass. The full CPU selector has 4,524 passes, six capability skips and no
failures; the five display-dependent skips pass when rerun with display access,
leaving only the expected unsanitized GLFW LeakSanitizer skip. Combined evidence
covers 4,529 passing tests. No fresh GPU or sanitizer execution is claimed for
this integration slice. The structural checks pass.

The ten affected production files shrink from 16,608 to 15,893 physical lines:
715 lines removed (428 runtime, 287 app), with no added production file/module.
The scoped diff, source identities and logs are retained locally under
`build/analysis/processing-integration-2026-09-11/`, including Claude's transcript
and the per-finding disposition. Review and corrections are complete. The useful
coverage gaps were addressed with empty-result checks before attachment and after
detachment, plus invalid numeric edits and missing-input rejection in each of the
four shared execution panels. Accepted configuration, job submissions and outputs
remain unchanged on rejection; a corrected request then produces the expected
output. The test targets rebuilt and all 271 focused tests passed again. No
production code changed after the reviewed diff.

Clean-workshop review: rows 1–3 pass (same allowed imports/links and runtime-owned
public types); rows 4–8 are not applicable (no renderer/pass/recipe changes,
scaffold closure or temporary exceptions). The strict layering checker passes
with no allowlist entries. The retained attachment and copy boundaries are
load-bearing; parallel result pointer lists are not.

## Shared point input/publication slice (2026-09-12)

The operator requested extending canonical runtime capture and publication. This
slice covers density, spacing, density weights, keypoints and outlier
analysis. Their duplicated position/deletion loops have identical domain,
cardinality, finite-sample and revision contracts. The existing scalar
capture's input portion now uses a plain private record and compiled free function in
`Runtime.GeometryProcessingOperations.PointProperties.cpp`, with shared
same-domain output preflight. Density weights also use the existing scalar
history transaction. No new production file or public module was added.

Radius versus kNN queries, sample minima, backend gates, keypoint mask/score
publication, and outlier analysis-stamp/removal transactions remain explicit. Normal-based
methods have additional input contracts and are outside this slice. A different
capture path is justified when a caller needs different deletion mapping or input
ownership, not merely a different algorithm name. Tests already cover these five
methods across all eight domains, deleted rows, history and stale/cancelled jobs.
The new density-weight tests cover publication notifications, replaced-storage
history rejection, and malformed deletion masks. Its standalone publication
omitted the dirty notifications used by other same-cardinality properties; the
shared transaction corrects that on apply, undo and redo.

Baseline including prior uncommitted work:
`build/analysis/point-inputs-2026-09-12/before.json`. Claude reviewed this slice's
fixed `cleanup.diff`; the large pre-existing worktree remains intact. Claude found
no confirmed correctness defect. The explicit standard-header includes it flagged
are fixed; other questions were resolved against source/tests and recorded in
`claude-review-disposition.md`. The final source/test identity is `after.json`.

The five affected production files total **1,488 → 1,390 physical lines** (98
removed, no new production files). This is code consolidation, not a measured
compile-time or runtime-performance claim. The new capture/preflight owner routes
are recorded in the reuse skill.

Verification:

- `ci` configured; `IntrinsicTests` and `ExtrinsicSandbox` built successfully.
- Full CPU selector: **4,532 passed, one expected unsanitized GLFW LeakSanitizer
  capability skip**, no failures (129.83 s). The subsequent review edits were three
  explicit includes and one test unused-return cast; final build and all **55
  focused cases** passed again (5.30 s).
- Strict layering, test layout, docs sync, task policy, links, skill mirrors,
  session brief and diff whitespace checks pass. Private-header documentation
  audit has zero objective errors; its three comments explain actual prerequisites
  and capture/query lifetime contracts.
- `ci-vulkan` configured with Clang 23 and ASan+UBSan. Its initial cached rebuild
  crashed in two compiler units while include edits overlapped the build. The
  stabilized retry with `CCACHE_DISABLE=1` passed. Root cause remains unisolated
  under [BUG-178](../../tasks/backlog/bugs/BUG-178-clang23-incremental-module-ice.md);
  no source workaround or verification weakening was applied.
- All **nine affected Vulkan GPU integration cases passed**, no skips or failures
  (121.29 s), with ASan+UBSan instrumentation. These cover outlier/distance-ratio,
  density, spacing, keypoint and density-weight paths, including the keypoint and
  density-weight stale/cancel/overflow cases. No fresh separate full-CPU sanitizer
  suite was run for this slice.
