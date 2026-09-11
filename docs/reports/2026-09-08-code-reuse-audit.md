# Code reuse audit — 2026-09-08

**Completion — 2026-09-11:** All 38 numbered findings are resolved through
shared implementations or documented retention of distinct caller policies.
The run stops because the list is complete, before the 08:00 Europe/Berlin
limit. The combined cleanup is committed as `dd18432a8b3b5690d5f8bb36e1881b995634a378`,
with the existing changes preserved. Final verification: 4,508 CPU tests selected (zero failures, one
expected unsanitized leak-control skip); all 84 Vulkan tests passed with
ASan+UBSan; 116 workflow regressions passed. All thirteen changed shader
entry points compile and pass SPIR-V validation. Final engine/shader source
reduction is 3,077 lines, plus 872 benchmark lines; these counts include new
shared files and supersede intermediate cumulative estimates below.


The audit identifies **38 actionable reuse opportunities**, grouped by the shared responsibility rather than counting every matching block separately. The strongest opportunities concern shared editor decision rules, geometry IO helpers, GPU identity/layout contracts, and repeated resource-management code.

This is a source review of the **current dirty working tree**, including untracked source files, based on HEAD `314db9ea5`. Existing implementation changes were left untouched. Priorities express the value of addressing duplication; they are **not assertions of reproduced bugs**.

## Scope and method

- Scanned **1,556 project-owned source/build/workflow files**, containing **581,467 physical lines**: `src/`, `methods/`, `benchmarks/`, `tests/`, `tools/`, `cmake/`, `research/`, `assets/shaders/`, `.github/workflows/`, and the root `CMakeLists.txt`.
- The exact-match probe ignores whitespace, comments, import/include lines and punctuation-only lines; it finds repeated runs of at least nine substantive lines. It produced **2,357 candidate block pairs**. Those are overlapping lexical candidates, not 2,357 findings or an estimate of removable lines.
- Followed with identifier-normalized probes and targeted searches for existing helpers, duplicated function names, hashes, numerical routines, shader layouts and shared-index adoption.
- Manually reviewed the source for each numbered finding below and checked the proposed ownership against the repository layering rules. The detector is a discovery aid, not a compiler or semantic proof.
- Excluded external/vendor code, build output, generated skill mirrors, historical task/evidence trees and Framework24 reference code. No source file was treated as needing consolidation merely because it implemented a distinct backend, format, algorithm or test case.
- **Completeness limit:** this is a broad, systematic inventory of confirmed opportunities, not proof that every semantic duplication has been found. Large test files and low-value lexical matches were sampled. Additional candidates are distinguished below.
- Session-local probe artifacts: `/tmp/intrinsic_reuse_scan.py`, `/tmp/intrinsic-reuse-scan.json`, and the normalized scan files. These are scratch artifacts, not maintained repository tools.
- Verification: source comparisons; all 155 local source/document links and cited source-line bounds checked; `python3 tools/docs/check_doc_links.py --root . --strict` passed (3,917 relative links); report whitespace checked. No C++ build or test suite was run because the audit changes no implementation.

## Finding index

### Follow-up slice — 2026-09-10

The operator requested a bounded cleanup with Claude Code CLI, conserving weekly
usage. R14, R16 and R28 are now addressed. Other findings have not been
re-audited in this slice. The right-sizing plan applied was:

- R14: replace point-cloud copies of text/path parsing with the existing
  `Geometry::IOText` helpers and preserve the Core error-code adapter. All
  three geometry IO owners then use one text implementation.
- R16: use one private PCD writer for validation, file opening, header and
  flushing, retaining separate ASCII and binary row encoding and both public
  entry points. No new file or public surface is needed.
- R28: remove the byte-identical `culling/instance_cull.comp` source, point
  tests and current documentation at the production `instance_cull.comp`, and
  preserve every shader assertion. Historical task evidence remains historical.

These changes stay within geometry or graphics ownership. Geometry IO and
culling/renderer contract tests cover the consumers; the complete CPU gate
and canonical shader compilation verify the combined change. Separate helpers
or shader entry points should return only if a caller needs a different parsing,
serialization or shader contract.

Verification on the combined diff:

- `cmake --preset ci` and builds of `IntrinsicGeometryIoTests` and
  `IntrinsicTests` passed with Clang 23.
- `ctest --test-dir build/ci --output-on-failure -R '^GeometryIO_'
  --no-tests=error --timeout 60`: 217 passed, including missing-file errors for
  all eight point-cloud readers, whitespace/path preservation, and repeated
  exact-byte ASCII/binary PCD output with normals-only and colors-only inputs.
- The default CPU selector (`-LE 'gpu|vulkan|slow|flaky-quarantine'
  --no-tests=error --timeout 60`) selected 4,504 tests: 4,498 passed, six
  environment-dependent window/ImGui tests skipped, zero failures (109.49 s).
- `glslc --target-env=vulkan1.3` compiled the retained shader and the removed
  source from Git to byte-identical SPIR-V. No shader ABI or execution change
  was introduced; no Vulkan-device test was run for this source consolidation.
- Strict layering, test layout, documentation links, and docs-sync checks over
  the actual working diff passed. `git diff --check` passed.
- One bounded Claude Code CLI review approved the fixed code/test diff with
  no defects. Its compile and temporary-file checks are covered by the build
  and IO tests. A source search found no old shader-path consumer in current
  source, tests, CMake or workflows; CMake discovers shader sources by glob.
  Historical task records retain their original paths. Existing tests cover
  combined normals/colors round-trips and rejection for both PCD encodings.

High priority means a shared eligibility, identity, parsing, layout or lifetime rule currently has multiple implementations. Medium priority means a clear shared helper with a useful maintenance payoff. Low priority covers small projections and test convenience code.

### Editor follow-up slice — 2026-09-10

R01–R03 are now addressed. Source comparisons confirmed the duplicated import
evaluator, property rules and vertex-channel evaluator were identical apart
from whitespace. Their implementations now live in the existing private editor
module, with declarations in `Runtime.EditorFeatures.Detail.cppm` and bodies
alongside the existing context adapters. Scene/visualization actions
and workspace models consume the shared rules; command-specific mutation and
model formatting remain at their current owners. Share the existing timing
helper as part of preserving vertex-channel validation statistics.

No new file, service, registry or public editor API is needed. The private
module already imports the operation contracts, and only implementation units
gain imports of that module, preserving acyclic interface dependencies and
the app boundary. Reintroduce separate eligibility implementations only if an
explicitly different command/model contract requires them. Existing import,
property-catalog, visualization, vertex-binding and snapshot-statistics tests
are the focused verification surface, followed by the full CPU gate.

Verification and review:

- The editor slice removes 650 production lines and adds no files. Together
  with R14/R16/R28, the cleanup removes 1,067 production lines and one file.
- `cmake --preset ci` and builds of `IntrinsicRuntimeContractTests`,
  `IntrinsicSandboxEditorIntegrationTests`, and `IntrinsicTests` passed with
  Clang 23. Regenerating the module inventory produced no content changes.
- Extended import rejection tests compare displayed and command-returned
  reasons; vertex-channel tests check incompatible scalar options alongside
  rejected commands on mesh, graph and point-cloud sources. Existing property,
  snapshot/cache and validation-timing tests passed.
- The 262-case editor/layering selection identified the two new implementation
  consumers missing from the private-import ratchet. The ratchet now names
  those action units, and its focused rerun passed. Public-interface and app
  boundary assertions remain enforced.
- The full CPU selector (`-LE 'gpu|vulkan|slow|flaky-quarantine'
  --no-tests=error --timeout 60`) then selected 4,504 tests: 4,498 passed, six
  environment-dependent window/ImGui tests skipped, zero failures (112.68 s).
- One bounded Claude Code CLI review confirmed the shared bodies and module
  attachment. Its missing `<chrono>` finding and orphaned comments were fixed;
  implementation imports were made explicit. Suggested export removals were
  checked against live workspace-model callers and retained where needed.
- Strict layering, test layout, documentation links, docs-sync over the actual
  working diff, and whitespace checks passed. The private-interface source-doc
  audit had no errors; its one existing comment finding documents the
  prepared-frame visitor's required reference lifetime.

### Signature/history follow-up slice — 2026-09-10

The next bounded slice addresses R04, R05 and R07 in the existing private
editor module. The right-sizing plan is to share the byte/string signature
mixing and geometry metadata assembly, the transform undo adapter, and the
history-status switch. Stable-entity lookup is identical in all four editor
owners and joins the same implementation. Existing method-internal wrappers
delegate to these helpers so their consumers need no new dependency. No new
file, public API or transaction framework is needed.

Consumers are workspace cache signatures, geometry job stale-state checks,
scene transform edits, registration, and scene/visualization/method history
results. Preserve signature seed, byte order, domain tags, descriptor order,
transform equality, dirty stamping, stale-state rejection and status mapping.
Existing cache invalidation, queued-method, transform and registration
undo/redo tests provide the focused check, followed by the full CPU gate.
Separate helpers should return only when a consumer acquires a different
identity, metadata or mutation contract.

R06 was rechecked and is deferred: visualization render-hint undo now also
captures/restores the stored surface visualization configuration, whereas
scene render-hint undo owns only the render components. The original audit's
whole-adapter equivalence no longer holds; both transactions remain intact.

Verification and review:

- Including the review correction below, this slice removes 283 production
  lines, bringing the combined cleanup to 1,350 production lines and one file
  removed, with no files added.
- Fixed-source comparisons verified that all nine moved function bodies are
  unchanged apart from whitespace. The method-internal status wrapper now
  delegates to the shared switch. Local review checked module attachment,
  identity/lifetime, undo validation and unchanged render-hint ownership.
- `cmake --preset ci` and builds of `IntrinsicRuntimeContractTests`,
  `IntrinsicSandboxEditorIntegrationTests`, and `IntrinsicTests` passed with
  Clang 23. A conflicting leftover local forward declaration was removed;
  verification then used a fixed source snapshot.
- All 262 editor/layering tests passed, including geometry metadata cache
  invalidation, queued method stale-state rejection, transform and registration
  undo/redo, and the private-import ratchet with its exact new mesh
  implementation consumer. No behavior change or new test file was needed.
- The full CPU selector (`-LE 'gpu|vulkan|slow|flaky-quarantine'
  --no-tests=error --timeout 60`) selected 4,504 tests: 4,498 passed, six
  environment-dependent window/ImGui tests skipped, zero failures (112.47 s).
- Strict layering, test layout, documentation links, docs-sync, and whitespace
  checks passed. Regenerating the module inventory changed no content; the
  private-interface source-doc audit reported zero errors and the same existing
  reference-lifetime comment finding.
- Claude Code reviewed the fixed diff and existing transaction helper on
  2026-09-11 in one bounded call. The packet was checked against current source
  before sending; raw review output remains in session-local scratch.

The review's missing-visibility concern did not apply: workspace models already
import the private module and are defined inside `EditorFeatureDetail`.
The adapter explicitly includes `<utility>` and imports `Geometry.Properties`
and geometry sources; topology fingerprints use the shared integer/string
mixing helpers, never the private byte primitive.

The enum-linkage concern needed a different diagnosis. All production consumers
include the transaction header inside named-module implementation units, where
the unexported enum has module linkage, rather than the external/global linkage
assumed by the review. However, three existing geometry-processing implementation
units define that same enum in the same module. That conflicts with the
[one-definition rule for named modules](https://eel.is/c++draft/basic.def.odr#16.3).
The enum now lives in an unnamed namespace, giving each translation unit its
own state tag under the [linkage rules](https://eel.is/c++draft/basic.link#4.9),
matching the existing static transaction template. No call-site syntax, state
transition or exported module surface changes. The added contract case checks
that destroying an entity makes both transform undo and redo return
`StaleEntity` without advancing history.

Post-review verification used `cmake --preset ci`, the focused runtime/editor
targets, and `IntrinsicTests`, all with Clang 23. The expanded editor/history/
gizmo selection passed 285 cases with one environment skip, including the new
destroyed-entity contract. Strict layering, test layout, documentation links,
docs-sync and whitespace checks passed. The touched header's source-doc audit
reported zero errors; its retained declaration comment specifies the required
validate/apply/stamp contract. The full CPU selector then selected 4,505 cases:
4,499 passed, six environment-dependent window/ImGui skips, zero failures
(113.32 s).

### Message/job follow-up slice — 2026-09-11

R08 and R09 are addressed by sharing the six import/scene-file message builders
and job-record projection through the existing private editor module. Comparisons confirmed
the original bodies were identical apart from whitespace. Message wording,
path/statistics formatting, errors, all job fields and dependency order remain
unchanged. The one-call dependency projection is folded into the shared job
loop, removing that extra helper.

The right-sizing choice is free functions beside the existing context
adapters, with declarations in the existing private module interface. Direct
scene actions, queued completion adapters, workspace job lists and the UV-job
view are the consumers. No new file, public API, dependency edge or service is
needed; existing private imports suffice. Split these functions again only if
a consumer acquires a distinct message or job-view contract. Existing direct
and queued import/scene, job dependency/progress and UV-regeneration tests are
the focused checks, followed by the full CPU selector and a bounded Claude
Code review of this slice.

The slice removes 155 production lines (1,505 across the combined cleanup),
with no files added. Claude Code's single bounded review found no code defects.
Its requested symbol checks confirmed one definition per shared function,
no callers of the removed dependency helper, and a live remaining use of the
scene asset namespace alias. Both job records and models are exported from
`EditorJobProjection`; the private interface already imports that module.
The code still matches the reviewed patch, based on HEAD
`90909d7507bf6da1014b6b16ce0a88f05cd0db0c` plus the slice diff with SHA-256
`99086bbbbec52315ce89718287c1eb9d6b2654db494902b6b57b690162c18e44`.
The patch and raw reviewer response are session-local scratch artifacts.

Verification uses the repository's canonical `ci` preset and its selected
Clang 23 toolchain. The review suggested an additional Clang 20 build; this
slice does not change the compiler contract, and no second compiler tree is
used or claimed as evidence. Strict layering, test layout, documentation links,
docs-sync and whitespace checks passed. Module-inventory regeneration changed
no content; the private-interface source-doc audit reported zero errors and
the same existing reference-lifetime comment finding.

`cmake --preset ci` and the focused runtime/editor builds passed. All 263
editor/layering tests passed (5.84 s), including direct and queued import/scene
commands, custom messages, job dependency/progress projection and UV-job views.
The `IntrinsicTests` build also passed. The full CPU selector
(`-LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60`) selected
4,505 cases: 4,499 passed, six environment-dependent window/ImGui skips, zero
failures (109.81 s). The final source still matches the reviewed slice diff.

### Panel helper follow-up slice — 2026-09-11

R10 and R11 share repeated diagnostic/domain/vector widgets, result dismissal,
texture-bake option tables and state views, target/encoder lookup, and appearance
command construction through the existing app-owned editor shell module.
Original helper bodies and tables matched apart from whitespace and runtime
namespace qualification. Uniform-color commands reuse the common projection
and explicitly disable baked textures, preserving their previous behavior and
the scalar styling needed when switching back.

The right-sizing choice is free functions, constants and a borrowed state
record in the existing shell module; no new file, library or runtime API is
needed. All consuming panels already import the shell. ImGui implementation
stays in its implementation unit; the shared dismissal template uses one
button function so its interface needs no ImGui headers. The template preserves
local-result reset before the session callback, and callers still draw it after
their last result reader. Bake-state pointers borrow panel storage only during
the draw call. Keep panel-specific bake-control flows separate, and split a
shared helper only when a consumer needs a distinct behavior or ownership
contract. Verification uses focused app integration coverage and the full CPU
selector, with a bounded Claude Code review of the fixed slice diff.

The slice removes 228 production lines (1,733 across the combined cleanup),
with no files added. Claude Code's single bounded review found no defects.
Its requested checks confirmed the test directly imports the shell, no use of
the removed shell alias remains, and all dismissal calls compile against the
shared template. The new regression case compiles and passes, including its
GLM and fixed-array comparisons. It checks uniform-color overrides disable
baking while the base projection preserves it, and both retain scalar styling.
The review's optional suggestion to replace existing enum casts with named
enumerators is outside this behavior-preserving extraction; the regression
case checks the uniform source against its named enumerator.

The reviewed source is based on HEAD
`90909d7507bf6da1014b6b16ce0a88f05cd0db0c` plus the slice diff with SHA-256
`87f505ba96dab05c35812f0ec451433e84727ff2c24758c194fb8a2bf298ea83`.
The patch and raw reviewer response remain session-local scratch artifacts.
Architecture review and clean-workshop rows 1–3 pass: imports respect the app
boundary, target links are unchanged, and exported app helpers use runtime
views. Rows 4–8 are not applicable: no renderer/pass/recipe change, maturity
closure, or temporary exception is introduced.

`cmake --preset ci` and both `IntrinsicSandboxEditorIntegrationTests` and
`IntrinsicTests` builds pass with Clang 23. The focused selector passes all 256
cases (192 contract and 64 integration, 6.99 s). Strict layering, test layout,
docs-sync, documentation links and whitespace checks pass. Module-inventory
regeneration changes no content; the shell interface's source-doc audit reports
zero errors, with two retained lifetime-contract comments flagged for review.

The full CPU selector
(`-LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60`) selected
4,506 cases: 4,500 passed, six environment-dependent window/ImGui skips, zero
failures (110.04 s). Final source still matches the reviewed slice diff.

### Queued import follow-up slice — 2026-09-11

R12 shares queued ingest submission and execution-identity capture, route/decode
transitions, and the successful-decode/apply preflight inside the existing
import executor. The right-sizing choice is three private member functions:
the geometry and model/texture queues are present consumers, and no new file,
service, dependency edge or public entry point is needed. The existing copied
stage-trace identity supplies request, world and binding generation to the
shared transitions; its scene pointer remains callback-scoped captured state.

Payload-specific routing, required-service checks, worker decoding and
materialization stay explicit. In particular, missing model/texture loaders
retain their callback-failure diagnostic, distinct from geometry decode
failure. Unpublished cancellation already delegates to one finalizer and keeps
its original request capture. Split a shared lifecycle step only if a payload
acquires a distinct transition or publication contract. Existing queue,
reimport, cancellation, shutdown, world-replacement and away/back binding tests
provide the focused coverage, followed by the full CPU selector and a bounded
Claude Code review.

The final slice removes 181 production lines (1,914 across the combined
cleanup), with no files added. Claude's single bounded review found that the
model/texture callback still captured `existingAsset` after its local
declaration was removed; the first build confirmed that compile error. The
declaration is restored only in that queue path, preserving its existing-asset
materialization calls. Claude found no other defects. Source comparisons
confirm workers, payload-specific failure classification, materialization and
completion, and unpublished callbacks are unchanged.

The reviewed slice diff has SHA-256
`6a87eb2a480375b7f6486865a6e3742c4b6fa3e28b879bd0a1012ef4aace6d24`;
the final slice diff has SHA-256
`7bf2e106cb4ff59997bcde5ba62eef0970d2d0f9719daf1f7f7748b418b44f32`.
An exact comparison confirms the only post-review code change is the
one-line declaration restoration recommended by Claude. Both diffs are against
the saved start-of-slice source atop HEAD
`90909d7507bf6da1014b6b16ce0a88f05cd0db0c`; review artifacts remain in session
scratch storage.

Architecture review confirms the helpers remain private to the existing
executor, with unchanged module imports and target links. Clean-workshop rows
1–3 pass; rows 4–8 are not applicable. Strict layering, test layout, docs-sync,
documentation-link and whitespace checks pass. Module-inventory regeneration
changes no content; the touched interface's source-doc audit reports zero
errors or review findings.

Verification uses `cmake --preset ci` with Clang 23. After the fix,
`IntrinsicRuntimeContractTests` builds and all 71 focused import/lifecycle
cases pass (4.46 s), including cancellation, world/binding changes, queued
reimport and shutdown ordering. The `IntrinsicTests` build passes. The full
CPU selector (`-LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error
--timeout 60`) selects 4,506 cases: 4,500 passed, six environment-dependent
window/ImGui skips, zero failures (110.23 s). Final source matches the recorded
post-fix diff.

### Queued scene-file follow-up slice — 2026-09-11

R13 shares the captured document binding, unpublished-result finalization and
successful submission bookkeeping inside `SceneDocumentModule.cpp`. The
right-sizing choice is one local binding record and helpers over the two
existing operation-state types. Both save and load are present consumers;
there is no new file, module, public surface or dependency. Worker snapshots
and loaded scenes remain separate from the captured live-registry identity.

Save snapshotting/history updates and load replacement/result publication keep
their distinct behavior. Finalization still forgets the owned task before
checking the captured binding and emits an error event only for a current
document. Completion still forgets the task before a load can replace the
scene. Split these helpers only if save and load acquire different ownership,
binding-validation or finalization rules. Existing scene lifecycle tests cover
queued save/load, parse errors, stale worlds and binding epochs, and expired
module state; cancellation coverage checks exactly one terminal event for
both operation kinds. Verification includes those focused tests, the full CPU
selector and one bounded Claude Code review.

The final slice removes 54 production lines (1,968 across the combined
cleanup), with no files added. Claude found duplicate closing delimiters left
by the extraction in both queue functions; the compiler confirmed the error.
Those four lines are removed, and Claude reported no other code defects.
The corrected runtime contract target builds. The focused scene/job lifecycle
selector passes all 48 cases (2.40 s), including the cancellation case extended
from save to both save/load under
`RuntimeSceneLifecycle.CancelledQueuedSceneFilesPublishOneTerminalEvent`.
Historical evidence retains the original test name; no active selectors
reference it. Review checks confirmed the logger accepts the shared format,
fixture cleanup is synchronous, and `JobService` checks cancellation before
any result publication. Workers and publish callbacks remain byte-identical
to their pre-slice bodies.

The reviewed slice diff has SHA-256
`ab61dd786a0ffc4959f4045a3ae53b975f4fb6843f891ef4580ae7ba37f77b56`;
the final slice diff has SHA-256
`e2eedab0fc716251c1e27c502d24d5492bd2dc949f459b3a48859319664c7d5d`.
An exact comparison confirms the post-review changes are removal of the
four duplicate closing lines and the ownership-check update described below. Both diffs are against the saved
start-of-slice source atop HEAD
`90909d7507bf6da1014b6b16ce0a88f05cd0db0c`; review artifacts stay in session
scratch storage.

Strict layering, test layout, docs-sync, documentation-link and whitespace
checks pass. Module interfaces, imports, target links and the source-file
inventory are unchanged by this slice.

The first full CPU run found one source-check mismatch:
`RuntimeEngineLayering.ProductionAsyncSubmissionsCarryOwningWorldScope`
still counted `.Scope = world`. Both scene submissions now use
`.Scope = binding.World`, whose capture comes from `BoundWorld`. Updating that
exact expected spelling retains the requirement for two scoped submissions;
no assertion is removed or relaxed. `IntrinsicTests` rebuilds and that
ownership case passes. The first run's 4,499 passes, one failure and six
environment skips (111.08 s) are superseded by the final run below.

Verification uses the canonical `ci` preset with Clang 23. The final
`IntrinsicTests` build passes. The full CPU selector
(`-LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60`) selects
4,506 cases: 4,500 passed, six environment-dependent window/ImGui skips, zero
failures (110.79 s). Final source matches the recorded corrected slice diff.

### Geometry primitive and residency identity follow-up — 2026-09-11

R15, R17 and R19 are complete. Mesh and point-cloud PLY readers share the
byte-identical scalar names, property descriptors, endian swapping and binary
decoding in the existing `Geometry.IOText.hpp`; list-count validation and public
error results remain reader-owned. `Geometry.Raycast` owns the unchanged slab
interval used by overlap and contact queries. `GpuWorld` exposes its existing
surface-index fingerprint for texture-bake atlas acceptance.

This batch removes 194 production lines and adds no source files. A fixed-vector
fingerprint regression uses an independent Python little-endian byte encoding
and FNV-1a calculation. Existing PLY endian/truncation/list validation and
ray-boundary tests cover the other extractions.

Claude Code CLI reviewed diff
`e0aad5572599fbb1f83df76491ad67e5e737e31dec2cc6c7a505cbb6d110e037`.
It identified no confirmed defect. Its checks for stale ray declarations,
fingerprint imports, removed constant users and header completeness were
resolved by repository searches; no stale declaration or missing include was
present. The source did not change after that review.

Verification: ci configured with Clang 23; IntrinsicTests built; full CPU gate
passed (4,501 passed, six environment-dependent skips, zero failures; 109.43 s).
Strict layering, test layout and task policy checks passed; module inventory
refreshed; doc links and whitespace checks passed. The command receipt is under
`tasks/evidence/RORG-134/commands/primitives-cpu.json`. Cumulative production
reduction across the cleanup is 2,162 lines.

### Graphics upload and command-context follow-up — 2026-09-11

R21, R26 and R27 are complete. Transient debug and visualization overlays use
one packed color-vertex ABI, color encoder and buffer growth/write routine in
the existing transient upload module. Each caller retains its caps, frame slots,
packet expansion, diagnostics and resource lifetime. Five pipeline factories
share their identical descriptor defaults. Three inert command-context classes
are replaced by `RHI::NullCommandContext`; recording test doubles and Vulkan
contexts retain their own behavior.

Claude Code CLI reviewed
`4c4e7b6bfaecfd26548a071a540390ba9ccf4c900f8ff9fa5f9fa5cae5086a16`
and confirmed behavioral equality. Its namespace, include, import-cycle and
remaining-reference checks were resolved from source. Trivial empty inline
bodies satisfy AGENTS.md §5; the contradictory implementation comment was
corrected. The final diff differs only by that comment:
`e1105414ac3cdb077eec0442b648370115e7774d1fafc9f2cde8675e14111bee`.

The ci build and full CPU gate passed (4,501 passed, six environment-dependent
skips, zero failures; 109.96 s). Strict layering, test layout and docs-sync
checks passed; module inventory was refreshed and doc links/whitespace checked.
Receipt: `tasks/evidence/RORG-134/commands/graphics-cpu.json`. This batch removes
308 production lines without adding source files; cumulative reduction is
2,470 production lines. GPU execution is reserved for the combined graphics
verification in RORG-134; these CPU results make no new backend capability claim.

### Render placement, draw recording and vertex channels — 2026-09-11

R20, R22 and R23 are complete. Geometry plan builders share normal/color
preparation while mesh fallback and corner expansion remain local. Opaque
surface passes share bucket recording; the depth pass retains its explicit
draw-count ceiling. The render graph and renderer share the first-fit transient
placement algorithm, with caller-owned requirements, memory-type compatibility,
execution-rank mapping and final heap alignment.

Claude reviewed the render diff
`c4d5ba6e1dc835a5c4453b55c112d278681e34ee8b0b091138664dee224fb3b5`
and channel diff
`c0e8107c38529355c3ee100e626082ad40299c38fe96d006393e270642ee5946`.
Review prompted an explicit success result and failure clearing for color
preparation. Compilation caught a missing direct GeometryAvailability import;
the new draw-limit regression also received a fully initialized GpuWorld fixture.
Claude's focused follow-up found no remaining defect. Final diff hashes are
`0a4ae3637e798ffeba174312d470c862914e2cc34d006fab9d8cd20dbcfe501d`
and `1ab34a0dec2dc13a69263b4f87685481c4f7ff6f2f0f602bb6efb902b54ad8c2`.

IntrinsicTests built with the ci preset. The CPU gate selected 4,508 tests and
reported zero failures and one CTest environment skip (113.29 s); receipt:
`tasks/evidence/RORG-134/commands/render-kernels-and-channels-cpu.json`.
Layering, test layout and docs-sync checks passed; module inventory refreshed.
This batch removes 216 production lines without adding source files, bringing
the cumulative reduction to 2,686. GPU execution remains part of the combined
graphics verification.

### Curvature policy and profile support — 2026-09-11

R18, R33 and R38 are complete. Curvature segmentation and patch fitting share
their identical median/MAD/RMS normalization and mixture validity predicates;
runtime config validation and execution reuse the existing parameter converter.
Patch fitting retains its narrower validity checks. General Statistics median
keeps its distinct finite-filtering and sorting policy.

Two benchmark runners share five fixture/result types and twelve identical
fixture and measurement functions in `CurvatureProfileSupport.hpp`. These
oracles remain independent of the production methods. This removes 46 engine
lines and 515 benchmark lines; the shared benchmark header is the only new
source file in this batch. Cumulative engine-source reduction is 2,732 lines.

Claude reviewed curvature diff
`3b4916efce89b52b6bdfb5f4098b6226d0b93c18826edb3abd6b0c2a9641bf9c`
and profile diff
`d0e2e739e26922034c02363218f475eeec7fd44360afa869cee9694b408444a7`.
It confirmed equivalent predicates and numerical behavior. All three span
includes already existed; the retained patch constant and algorithm include
still have callers. Explicit geometry imports remain the intended policy for
callers naming geometry types. A header comment documents non-module runner
usage; this is the only post-review source change (final profile diff
`8cd50c8863b747df872a41aeb47bdba5b9e9eb6e698727fdd09223444ab73ba0`).

ci configured, IntrinsicCpuTests and both profile targets built. The CPU gate
selected 4,508 tests, with zero failures and one CTest environment skip
(112.90 s); receipt: `tasks/evidence/RORG-134/commands/curvature-profile-cpu.json`.
All five before/after profile payloads are identical after excluding `*_ms`
and `memory_peak_bytes`, recorded in
`tasks/evidence/RORG-134/curvature-profile-comparison.json`. Fixed segmentation
and three patch smokes pass; automatic segmentation reproduces the existing
[C40 refutation](../../ara/logic/claims.md#c40-method-037-automatic-selection-does-not-guarantee-two-regime-recovery)
(four components, 0.01 error, failed disposition). No gate or numerical policy
was changed to make that diagnostic pass. Layering, test layout, docs sync,
doc links, workflow evidence validation and whitespace checks passed; module
inventory refreshed. These local comparisons make no performance claim.

### Benchmark envelopes, fixtures and render-hint equality — 2026-09-11

R06, R34, R36 and R37 are complete. Render-hint transactions share component
equality, including bitwise float comparison; the different scene and stored
visualization snapshots remain separate. The smoke runner uses one result
envelope with caller-owned precision and payloads. Three editor partitions
share six canonical geometry fixture builders. Five graphics tests share pass
inspection; two readback tests share exact pixel conversion, while mock barrier
inspection is an explicit `MockDevice` member. Individual expectations and
packet setup remain local.

Claude reviewed the frozen diffs and follow-up corrections. Review/compiler
findings fixed: direct `ECS.Scene.Handle` visibility; ADL ambiguity from a free
mock helper (replaced by a member); and inconsistent JSON control-character
escaping (now the existing nlohmann encoder). Its remaining UTF-8 concern was
disproved by replay: the original mandatory KNN serializer already aborted
before output on invalid UTF-8, and both versions retain that behavior.
An intermediate CLI follow-up invented unavailable working-tree inspections;
those statements were discarded. The final follow-up was explicitly text-only.

Final diff hashes: render hints
`8c6661cbd5871192ada3d0d345712680d7c785c1d8730dc84c890c0a73cf551a`;
benchmark envelope
`4d7b4a58070b7871b5362ef0910171e056ed848158fb521e57dc83f82519f329`;
editor fixtures
`e3a5af6c13018dc8a0d02192bbafaaaddbbb67a2be0968d6aaf161f10df636ad`;
graphics fixtures
`27c3281452f3ba093ec05a9c28c444d3b9d331a212a3d6b487f5cd2a6f2c38bd`.

ci configured and CPU/profile/smoke targets built. The CPU selector passed
with 4,508 selected tests, zero failures and one CTest environment skip
(113.18 s). The opt-in smoke runner and strict result validation both passed
(16.77 s). Receipts are
`tasks/evidence/RORG-134/commands/fixtures-hints-envelope-cpu.json` and
`tasks/evidence/RORG-134/commands/smoke-envelope-workloads.json`.
`tasks/evidence/RORG-134/smoke-envelope-comparison.json` records exact equality
of 105 synthetic parsed payloads, 35 valid control-character/Unicode outputs,
and identical invalid-UTF-8 failure behavior. Layering, test layout, docs sync,
doc links, benchmark manifests and whitespace checks passed. This batch removes
19 engine, 357 benchmark and 243 test lines; cumulative engine reduction is
2,751 lines. One shared graphics-test header is added.

### Vulkan and shader consolidation — 2026-09-11

R24–R25 and R29–R32 are implemented and independently reviewed. Swapchain
bootstrap and recreation share creation/view descriptors and imported-image
metadata; their distinct diagnostics, partial cleanup and adoption lifecycles
remain separate. Transfer/readback share command-buffer completion and locked
timeline submission, retaining lane-specific retirement under the same lock.

K-means and progressive-Poisson shaders share state layouts. Three surface
shaders share material sampling and normal resolution, retaining entry-owned
descriptor sets and visualization choices. Point/surfel shaders share identical
projection, tangent-frame, covariance-inversion and lighting primitives;
covariance construction and degeneracy policies stay with each caller. Four
shared includes serve these present consumers. The batch removes 73 C++ lines
and 253 shader lines, including the added includes.

Claude found no defects in the frozen code. Reviewed SHA-256 digests:

| Surface | Digest |
| --- | --- |
| Swapchain | `73381956bad12e8d39066b5a220c2698f4fea3fa99fae9a42f15a06cf641e007` |
| Transfer | `dc76f3f1781416fbd276384019ba36590bfcf5e91777710ed5fb82cc46d99cfb` |
| Compute shaders | `883b2bcaa85fc9fee9fee1622a071072f9f8198dbc8f505d030224ab0a0a23ca` |
| Surface shaders | `1f3efb26a0e334dc17686ef9dc7c68e73992f1cd1c46a18a0a729d5b143fbc26` |
| Point/surfel shaders | `21966bcd1bf7d82650f710a27d271a62fa04530ce50ead9a3d6e1614f96d3054` |

All thirteen affected entry points compile for Vulkan 1.3 and pass SPIR-V
validation with scalar block layout. Descriptor/interface/member layout
records agree for all thirteen. The five compute shaders also produce
byte-identical optimized SPIR-V; the other eight binaries differ, so this is
not GPU output-equivalence evidence. Recorded comparison:
`tasks/evidence/RORG-134/shader-layout-comparison.json`.

The ci IntrinsicTests build passed. The first CPU gate found stale source
assertions after the surface extraction. The correction checks the shared
implementation and each entry point's include/calls, retaining the runtime
pipeline rebuild checks. Claude correctly questioned the exact relative
include spelling; the first correction assumed one spelling and was fixed
using all three source declarations. A focused attempt built the wrong
similarly named target and ran an old binary; CTest's JSON registry identified
`IntrinsicGraphicsContractCpuTests`, whose rebuilt source contract passes.
The interlocked-worker inventory was corrected to its actual 78 entries
(three two-processor, 51 three-processor, 22 four-processor and two eight-processor
cases). An earlier review prompt incorrectly reported that intermediate count
as passing; actual command output takes precedence. The final repairs bind
`891c29f33201c8339df3aa89a7101e45458c7c1511fb79368e37b92aa8164ba9`.
Final CPU/Vulkan verification is recorded below.

### CI setup consolidation — 2026-09-11

R35 uses one local `setup-build` composite action across ten jobs in six
workflows. Explicit package profiles retain exact package sets; the
self-hosted job selects `none`. Cache key/path, bootstrap, environment and
cache-hit forwarding are shared. Gates, triggers, runners, permissions and
concurrency remain in their workflows. The full-CPU pip step now follows
bootstrap; neither depends on the other. Vulkan retains its compiler check.

Claude accepted diff
`728b86fac6aa9b5ba887dc66431517bed564d362c954a3e284f36feeb007042d`.
Stubbed shell execution and parsed before/after comparisons confirm the ten
package sets and unchanged cache/bootstrap/gate policy in
`tasks/evidence/RORG-134/ci-setup-comparison.json`. No hosted GitHub job was
launched. Workflow regression checks also exposed
[BUG-187](../../tasks/done/BUG-187-interlocked-worker-budget-inventory.md),
a pre-existing stale worker-reservation audit. Its repair reconciles the
three paused-worker tests without changing scheduler behavior.

### Final verification and stopping condition — 2026-09-11

- `ci` and `ci-vulkan` configured with Clang 23, and IntrinsicTests built in
  both trees. The last CPU-only assertion edit was also rebuilt in ci-vulkan;
  it changes none of the GPU executables or shaders used by the Vulkan run.
- The corrected full CPU selector passed: 4,508 selected, zero failures, one
  expected unsanitized leak-control skip, 113.29 seconds. Receipt:
  `tasks/evidence/RORG-134/commands/final-cpu-corrected.json`.
- The full `gpu`/`vulkan` label intersection passed all 84 tests, with no skips,
  in 681.81 seconds. This includes transfer/readback, shader-driven surface
  and overlay rendering, import/materialization, frame pacing, and the
  leak-enabled Vulkan shutdown contract. Receipt:
  `tasks/evidence/RORG-134/commands/graphics-vulkan.json`.
- Six workflow regression suites passed 116 tests; workflow naming also
  passed. Receipt: `tasks/evidence/RORG-134/commands/ci-workflow-regressions.json`.
- Final shader-source/interlock repairs were accepted by Claude with no
  findings. All per-batch code reviews and verified corrections are recorded
  above; the final integration review and exact dirty source binding live in
  `tasks/evidence/RORG-134/`. Failed intermediate receipts remain historical
  artifacts, superseded by the passing final producer and CPU receipts.
- Strict layering, test layout, allowlist quality, task policy, docs links and
  docs synchronization passed. Skill mirrors are current; the module
  inventory and session brief were refreshed. The root-hygiene command exits
  zero but retains the pre-existing warning for the local `.agents/` directory.
- No hosted GitHub workflow or separate ci-asan/ci-ubsan CPU job was launched;
  those CI gates remain required at integration. Local Vulkan verification
  used the combined sanitizer preset. These are refactoring checks, not a new
  performance or backend-maturity claim.

No numbered reuse item remains. Conditional candidates below still require
their stated semantic/workload decisions and are not part of the confirmed
38-item inventory. RORG-134 and BUG-187 are retired under `tasks/done/`, with the implementation
commit recorded and final retirement evidence sealed under
`tasks/evidence/RORG-134/`. Staging also exposed mixed leading indentation in
the new shared graphics-test header. A whitespace-only correction preserved
every C++ line after stripping indentation and was accepted by Claude; the
prior code verification remains applicable.

### Combined architecture review — 2026-09-11

The change has one scope: remove repeated responsibilities while preserving
caller-specific behavior. Shared implementations remain in their owning
layers or existing private modules. Added public declarations expose only
allowed lower-layer types. No new subsystem, virtual interface, recipe edge,
backend choice, config policy, test oracle dependency or temporary exception
is introduced. Resource retirement, queue locking, diagnostics and shader
layouts are checked explicitly in the batch reviews above.

Clean-workshop scorecard: layer imports **pass** (strict checker); target links
**pass**; exported type direction **pass**; renderer ownership **pass**;
new pass identity **n/a**; new recipe dependencies **n/a**; scaffold/parity
retirement **n/a** (no new capability claim); temporary exceptions **pass**
(none added). New shared files each have multiple current consumers; the
redundant culling shader is removed. Distinct snapshots, lifecycle branches,
format encodings and independent numerical oracles remain intentional.

| ID | Priority | Area | Shared responsibility |
| --- | --- | --- | --- |
| R01 | high | editor | [Import eligibility and disabled reasons](#r01) |
| R02 | high | editor | [Property catalog and visualization eligibility](#r02) |
| R03 | high | editor | [Vertex-channel source preflight](#r03) |
| R04 | high | editor | [Geometry metadata signatures](#r04) |
| R05 | high | editor | [Transform undo adapter](#r05) |
| R06 | high | editor | [Render-hint undo adapter](#r06) |
| R07 | medium | editor | [History-status conversion](#r07) |
| R08 | medium | editor | [Scene-file and import result messages](#r08) |
| R09 | low | editor | [Job-to-editor model conversion](#r09) |
| R10 | medium | app | [Repeated panel widgets](#r10) |
| R11 | medium | app | [Appearance command construction and bake UI helpers](#r11) |
| R12 | high | runtime | [Queued asset import lifecycle](#r12) |
| R13 | medium | runtime | [Queued scene IO bookkeeping](#r13) |
| R14 | high | geometry | [Point-cloud IO bypasses existing text helpers](#r14) |
| R15 | high | geometry | [PLY scalar/header machinery](#r15) |
| R16 | medium | geometry | [ASCII and binary PCD export preamble](#r16) |
| R17 | medium | geometry | [Ray/AABB slab intersection](#r17) |
| R18 | medium | geometry | [Curvature median and robust scale](#r18) |
| R19 | high | runtime/graphics | [Surface-index fingerprint contract](#r19) |
| R20 | medium | runtime | [Geometry upload channel setup](#r20) |
| R21 | high | graphics | [Packed transient/overlay upload allocation](#r21) |
| R22 | medium | graphics | [GPU surface bucket draw recording](#r22) |
| R23 | high | graphics | [Transient memory placement algorithm](#r23) |
| R24 | high | vulkan | [Initial and recreated swapchain construction](#r24) |
| R25 | medium | vulkan | [Transfer and readback submission](#r25) |
| R26 | medium | graphics | [Debug/overlay pipeline descriptor defaults](#r26) |
| R27 | medium | runtime | [No-op command contexts](#r27) |
| R28 | high | shaders | [Two copies of the culling shader](#r28) |
| R29 | high | shaders | [Promoted surface material sampling](#r29) |
| R30 | high | shaders | [Progressive-Poisson GPU layout and cell encoding](#r30) |
| R31 | high | shaders | [K-means GPU layout declarations](#r31) |
| R32 | medium | shaders | [Point/surfel EWA math](#r32) |
| R33 | medium | benchmarks | [Curvature profile measurement helpers](#r33) |
| R34 | medium | benchmarks | [Smoke result JSON envelope](#r34) |
| R35 | medium | tooling | [CI toolchain and vcpkg setup](#r35) |
| R36 | low | tests | [Graphics inspection and setup helpers](#r36) |
| R37 | low | tests | [Editor geometry fixtures](#r37) |
| R38 | medium | geometry/runtime | [Curvature parameter validity rules](#r38) |

## Evidence and smallest useful consolidation

<a id="r01"></a>

### R01 — Import eligibility and disabled reasons

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:129](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L129); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:152](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L152).

Both files contain the payload list, importer availability checks, reason builders and the complete EvaluateFileImportPrerequisites implementation. The largest exact match spans 263 physical lines.

**Reuse:** Put the pure evaluator and its result record in runtime editor internals; both the snapshot builder and import command should call it. This prevents the displayed availability rules and actual command validation from diverging.

<a id="r02"></a>

### R02 — Property catalog and visualization eligibility

**Locations:** [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:435](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L435); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:555](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L555); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:632](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L632); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:817](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L817).

Copies include internal/connectivity-property filtering, scalar eligibility, domain conversion, property-set lookup, catalog supported kinds and AppendVisualizationPropertiesForDomain.

**Reuse:** Share these runtime editor rules as free functions over GeometryEntityAvailability and canonical property references. Keep UI formatting and mutation separate while using one eligibility policy.

<a id="r03"></a>

### R03 — Vertex-channel source preflight

**Locations:** [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:733](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L733); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:812](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L812); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:1210](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L1210); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:1306](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L1306).

Property-domain/source-type selection, SourceTypeAllowedForVertexChannel, resolver scratch accounting and EvaluateVertexChannelBinding are copied between action and snapshot code.

**Reuse:** Expose one internal preflight used by both paths, retaining the existing VertexAttributeBinding resolver. The command and UI must agree about types, domain, fallback and normalization.

<a id="r04"></a>

### R04 — Geometry metadata signatures

**Locations:** [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:818](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L818); [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:978](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L978); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:2620](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L2620); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:2783](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L2783).

Byte/string mixing, property descriptor traversal and GeometryMetadataSignatureForEntity are implemented twice.

**Reuse:** Share the exact metadata-signature implementation inside runtime. Preserve the existing seed, ordering, domain tags and deleted-count handling; changing the hash during consolidation would change cache and stale-result behavior.

<a id="r05"></a>

### R05 — Transform undo adapter

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:809](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L809); [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:8056](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L8056).

SameTransformComponent, EditorTransformMutationIdentity and ExecuteEditorTransformMutation duplicate the same stable-entity validation, write and dirty-tag logic.

**Reuse:** Reuse one transform mutation adapter around the already existing Internal::ExecuteUndoableEntityMutation. No new history framework is needed; registration and scene edits should share the same transform transaction.

<a id="r06"></a>

### R06 — Render-hint undo adapter

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:539](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L539); [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:628](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L628); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1121](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1121); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1336](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1336).

EditorRenderHintState, component comparison, ApplyRenderHintState and ExecuteEditorRenderHintMutation are copied.

**Reuse:** Move the shared render-hint state/transaction functions into runtime editor internals. Keep scene-specific commands and visualization-specific command construction at their current owners.

<a id="r07"></a>

### R07 — History-status conversion

**Locations:** [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:236](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L236); [src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:8776](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L8776); [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:741](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L741); [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1695](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1695).

The same history-status to EditorCommandStatus switch appears in three translation units, with a further copy in the mesh operations file.

**Reuse:** Use one conversion in the common editor/history support surface. A new history status should require one mapping decision.

<a id="r08"></a>

### R08 — Scene-file and import result messages

**Locations:** [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:393](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L393); [src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp:439](../../src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp#L439); [src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp:476](../../src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp#L476); [src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp:556](../../src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp#L556).

Import error wording and scene-file success/failure message builders are duplicated between direct actions and context adapters.

**Reuse:** Share the message builders in runtime editor internals so synchronous and queued completion messages stay consistent.

<a id="r09"></a>

### R09 — Job-to-editor model conversion

**Locations:** [src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp:1754](../../src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp#L1754); [src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp:1792](../../src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp#L1792).

ToEditorJobDependencyModel and ToEditorJobModel repeat the same job identity, dependency, progress and diagnostic projection.

**Reuse:** Use one runtime editor projection helper. UV regeneration can reuse it when locating a job; the workspace snapshot can reuse it for the full job list.

<a id="r10"></a>

### R10 — Repeated panel widgets

**Locations:** [src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp:130](../../src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp#L130); [src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp:90](../../src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp#L90); [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:172](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L172).

DrawDiagnostics is repeated; the mesh and method panels also copy DrawDismissLastResultButton and DrawDomainWindowHeader.

**Reuse:** Keep a small shared Sandbox panel helper surface. In particular, one dismissal helper should clear both the local result and its session result slot.

<a id="r11"></a>

### R11 — Appearance command construction and bake UI helpers

**Locations:** [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:60](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L60); [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:143](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L143); [src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:189](../../src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp#L189); [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp:63](../../src/app/Sandbox/Editor/Sandbox.EditorShell.cpp#L63); [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp:132](../../src/app/Sandbox/Editor/Sandbox.EditorShell.cpp#L132); [src/app/Sandbox/Editor/Sandbox.EditorShell.cpp:220](../../src/app/Sandbox/Editor/Sandbox.EditorShell.cpp#L220).

The two files copy bake option tables/state views and visualization model-to-command construction. Uniform and scalar command constructors also repeat the field projection.

**Reuse:** Share the app-local tables and state views; create the base command from a model once, then override the fields an individual action changes. This reduces the chance that a new appearance field is reset or dropped by one panel.

<a id="r12"></a>

### R12 — Queued asset import lifecycle

**Locations:** [src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp:1983](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp#L1983); [src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp:2502](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp#L2502).

QueueGeometryImportWithIngest and QueueModelTextureImportWithIngest repeat submission identity, route/decode transitions, event recording, stale-target handling and unpublished-result finalization. Several long blocks match exactly.

**Reuse:** Extract the shared submission/transition/finalization steps inside this executor. Keep payload decoding and materialization explicit, including the different required services. Preserve cancellation and world/binding generations.

<a id="r13"></a>

### R13 — Queued scene IO bookkeeping

**Locations:** [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:808](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L808); [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:935](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L935); [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:1033](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L1033); [src/runtime/Scene/Runtime.SceneDocumentModule.cpp:1165](../../src/runtime/Scene/Runtime.SceneDocumentModule.cpp#L1165).

Queued save and load repeat binding capture, job bookkeeping, completion-event publication and FinalizeUnpublishedOnMainThread handling.

**Reuse:** Share the scene-file job identity and event/finalization helpers locally. Keep save snapshotting and load replacement transactions separate: their mutation semantics differ.

<a id="r14"></a>

### R14 — Point-cloud IO bypasses existing text helpers

**Locations:** [src/geometry/Geometry.PointCloud.IO.cpp:34](../../src/geometry/Geometry.PointCloud.IO.cpp#L34); [src/geometry/Geometry.IOText.hpp:22](../../src/geometry/Geometry.IOText.hpp#L22); [src/geometry/Geometry.Graph.IO.cpp:28](../../src/geometry/Geometry.Graph.IO.cpp#L28); [src/geometry/Geometry.HalfedgeMesh.IO.cpp:36](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L36).

PointCloud.IO independently implements PathInfo, MakePathInfo, ReadTextFile, Trim, NextLine, SplitWhitespace and ParseNumber. Graph and mesh IO already use Geometry::IOText.

**Reuse:** Use Geometry.IOText.hpp in point-cloud IO too, with a small adapter for its existing Core::ErrorCode return contract. This is an existing helper adoption, not a new parser framework.

<a id="r15"></a>

### R15 — PLY scalar/header machinery

**Locations:** [src/geometry/Geometry.HalfedgeMesh.IO.cpp:338](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L338); [src/geometry/Geometry.HalfedgeMesh.IO.cpp:442](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L442); [src/geometry/Geometry.PointCloud.IO.cpp:593](../../src/geometry/Geometry.PointCloud.IO.cpp#L593); [src/geometry/Geometry.PointCloud.IO.cpp:650](../../src/geometry/Geometry.PointCloud.IO.cpp#L650).

Both readers define PLY format/scalar enums, scalar-name parsing, byte-size lookup, endian swapping, property descriptions and binary scalar decoding.

**Reuse:** Share a geometry-owned internal PLY lexical/scalar layer. Preserve separate mesh-face construction, cloud publication, list-count validation and public diagnostics. A parsing correction should reach both importers.

<a id="r16"></a>

### R16 — ASCII and binary PCD export preamble

**Locations:** [src/geometry/Geometry.PointCloud.IO.cpp:2477](../../src/geometry/Geometry.PointCloud.IO.cpp#L2477); [src/geometry/Geometry.PointCloud.IO.cpp:2642](../../src/geometry/Geometry.PointCloud.IO.cpp#L2642).

WritePCD and WritePCDBinary repeat input/property validation and the PCD field, size, type, count, width and point-count header construction.

**Reuse:** Share the validated export view and PCD header writer with an explicit data encoding. Keep ASCII and binary row encoders distinct.

<a id="r17"></a>

### R17 — Ray/AABB slab intersection

**Locations:** [src/geometry/Geometry.ContactManifold.cpp:16](../../src/geometry/Geometry.ContactManifold.cpp#L16); [src/geometry/Geometry.Overlap.cpp:21](../../src/geometry/Geometry.Overlap.cpp#L21).

The two modules carry the same RayAabbSlabInterval implementation, including parallel-axis handling and interval accumulation.

**Reuse:** Place the interval primitive in an appropriate geometry-owned shared helper; keep overlap/contact result construction separate. Preserve zero-direction, tangent and negative-parameter behavior.

<a id="r18"></a>

### R18 — Curvature median and robust scale

**Locations:** [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp:114](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp#L114); [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp:193](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp#L193); [src/geometry/Geometry.Statistics.cppm:75](../../src/geometry/Geometry.Statistics.cppm#L75).

Segmentation and patches each implement Median and the same MAD-to-RMS-to-one RobustScale fallback. Geometry::Statistics already exposes an exact median.

**Reuse:** Share the robust-scale policy. Reuse the statistics median for validated finite inputs, explicitly adapting empty-input behavior; its non-finite filtering contract must not silently replace a different method policy.

<a id="r19"></a>

### R19 — Surface-index fingerprint contract

**Locations:** [src/graphics/renderer/Graphics.GpuWorld.cpp:37](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L37); [src/graphics/renderer/Graphics.GpuWorld.cpp:72](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L72); [src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp:75](../../src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp#L75); [src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp:1764](../../src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp#L1764).

GpuWorld::FingerprintUint32Stream and texture baking's FingerprintIndices independently implement the same little-endian FNV-1a word stream. The bake path directly compares their results to accept GPU atlas residency.

**Reuse:** Give this fingerprint one implementation in a layer both consumers can legally use, retaining the seed, byte order and zero-to-one mapping. This is a shared identity contract, not merely similar hashing code.

<a id="r20"></a>

### R20 — Geometry upload channel setup

**Locations:** [src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Graph.cpp:174](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Graph.cpp#L174); [src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.PointCloud.cpp:126](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.PointCloud.cpp#L126); [src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Mesh.cpp:215](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Mesh.cpp#L215).

Graph and point-cloud builders duplicate normal/color binding and channel publication; mesh packing repeats the explicit color binding path.

**Reuse:** Share the canonical-domain normal/color channel preparation around existing ResolveVec3Channel and ResolveColorChannelPackedUnorm8. Leave primitive topology, mesh corner expansion and mesh default-color policy at their owners.

<a id="r21"></a>

### R21 — Packed transient/overlay upload allocation

**Locations:** [src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp:114](../../src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp#L114); [src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp:153](../../src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp#L153).

Both upload helpers contain the same lease/capacity growth, cap checking, host-visible BDA buffer allocation and upload operation under different packed vertex types. The overlay implementation explicitly describes itself as a copy of the transient helper.

**Reuse:** Share the packed-buffer upload routine inside graphics with an explicit stride or small typed helper. Keep packet expansion, lane limits and frame-slot ownership separate; retain in-flight resource lifetimes.

<a id="r22"></a>

### R22 — GPU surface bucket draw recording

**Locations:** [src/graphics/renderer/Passes/Pass.Selection.EntityId.cpp:21](../../src/graphics/renderer/Passes/Pass.Selection.EntityId.cpp#L21); [src/graphics/renderer/Passes/Pass.Selection.FaceId.cpp:21](../../src/graphics/renderer/Passes/Pass.Selection.FaceId.cpp#L21); [src/graphics/renderer/Passes/Pass.Deferred.GBuffers.cpp:23](../../src/graphics/renderer/Passes/Pass.Deferred.GBuffers.cpp#L23); [src/graphics/renderer/Passes/Pass.Forward.Surface.cpp:23](../../src/graphics/renderer/Passes/Pass.Forward.Surface.cpp#L23); [src/graphics/renderer/Passes/Pass.DepthPrepass.cpp:24](../../src/graphics/renderer/Passes/Pass.DepthPrepass.cpp#L24).

The passes repeat bucket validity checks, managed index-buffer binding, GpuScenePushConstants construction and DrawIndexedIndirectCount.

**Reuse:** Use a graphics-local draw-recording helper with pipeline, bucket and frame inputs. Keep each pass's eligibility, attachments, shader ABI and recipe identity explicit.

<a id="r23"></a>

### R23 — Transient memory placement algorithm

**Locations:** [src/graphics/framegraph/Graphics.RenderGraph.cpp:133](../../src/graphics/framegraph/Graphics.RenderGraph.cpp#L133); [src/graphics/renderer/Graphics.Renderer.cpp:1083](../../src/graphics/renderer/Graphics.Renderer.cpp#L1083).

BuildTransientPlacementPlan and BuildRendererTransientPlacementPlan independently implement active-range expiry, sorted free ranges, aligned first-fit placement, splitting and alias-reuse tracking.

**Reuse:** Extract the common pure placement kernel. The framegraph supplies estimated requirements; the renderer supplies device requirements and retains memory-type/dedicated-allocation checks. Do not reuse estimated sizes as device allocation sizes.

<a id="r24"></a>

### R24 — Initial and recreated swapchain construction

**Locations:** [src/graphics/vulkan/Backends.Vulkan.Device.cpp:1112](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp#L1112); [src/graphics/vulkan/Backends.Vulkan.Device.cpp:1962](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp#L1962).

Initialize repeats swapchain create-info construction, image-count/usage policy and image/view setup already implemented by CreateSwapchainResources.

**Reuse:** Have initialization and recreation use one backend-local swapchain builder, with explicit initial/old-swapchain inputs. Preserve initialization diagnostics, transactional adoption and cleanup on failure.

<a id="r25"></a>

### R25 — Transfer and readback submission

**Locations:** [src/graphics/vulkan/Backends.Vulkan.Transfer.cpp:168](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cpp#L168); [src/graphics/vulkan/Backends.Vulkan.Transfer.cpp:230](../../src/graphics/vulkan/Backends.Vulkan.Transfer.cpp#L230).

Submit and SubmitReadback duplicate command finalization, timeline-ticket allocation, semaphore submit records, queue submission and failure cleanup.

**Reuse:** Share the private submission primitive returning the ticket/result. Keep upload/readback-specific token and sink bookkeeping separate, including lock scope and readback slot ownership.

<a id="r26"></a>

### R26 — Debug/overlay pipeline descriptor defaults

**Locations:** [src/graphics/renderer/Graphics.Renderer.cpp:6026](../../src/graphics/renderer/Graphics.Renderer.cpp#L6026); [src/graphics/renderer/Graphics.Renderer.cpp:6067](../../src/graphics/renderer/Graphics.Renderer.cpp#L6067); [src/graphics/renderer/Graphics.Renderer.cpp:6094](../../src/graphics/renderer/Graphics.Renderer.cpp#L6094); [src/graphics/renderer/Graphics.Renderer.cpp:6144](../../src/graphics/renderer/Graphics.Renderer.cpp#L6144); [src/graphics/renderer/Graphics.Renderer.cpp:6183](../../src/graphics/renderer/Graphics.Renderer.cpp#L6183).

The transient triangle/line/point and visualization vector/isoline factories repeat the same rasterizer, depth, blend and target-format settings.

**Reuse:** Use a small common descriptor initializer, then set each shader pair, topology, push-constant size and name explicitly. Existing initialization/rebuild factory reuse should remain intact.

<a id="r27"></a>

### R27 — No-op command contexts

**Locations:** [src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp:45](../../src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp#L45); [src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp:217](../../src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp#L217); [src/graphics/renderer/Backends/Null/Backends.Null.cpp:113](../../src/graphics/renderer/Backends/Null/Backends.Null.cpp#L113).

Two runtime compute paths copy a complete NoopCommandContext implementation; the Null renderer backend has the same underlying no-op command surface.

**Reuse:** Share the two identical runtime helpers or expose an appropriate existing Null command-context seam. Do not replace recording test doubles or real Vulkan contexts; their observable behavior is different.

<a id="r28"></a>

### R28 — Two copies of the culling shader

**Locations:** [assets/shaders/instance_cull.comp:1](../../assets/shaders/instance_cull.comp#L1); `assets/shaders/culling/instance_cull.comp` (removed in the September 10 follow-up); [src/graphics/renderer/Graphics.Renderer.cpp:6344](../../src/graphics/renderer/Graphics.Renderer.cpp#L6344); [tests/integration/graphics/Test.GpuWorldAndCulling.cpp:378](../../tests/integration/graphics/Test.GpuWorldAndCulling.cpp#L378).

The shader sources are byte-identical (confirmed with cmp). Production loads the root shader path while multiple tests use the culling/ path; a renderer test even reads both sources.

**Reuse:** Use one canonical shader source and update consumers, or retain thin include-based entry points if both asset paths are required. Both paths must compile the same implementation.

<a id="r29"></a>

### R29 — Promoted surface material sampling

**Locations:** [assets/shaders/forward/default_debug_surface.frag:48](../../assets/shaders/forward/default_debug_surface.frag#L48); [assets/shaders/deferred/default_debug_gbuffer.frag:56](../../assets/shaders/deferred/default_debug_gbuffer.frag#L56); [assets/shaders/deferred/gbuffer.frag:40](../../assets/shaders/deferred/gbuffer.frag#L40).

Texture-ID validation, normal-texture decoding/space handling and albedo/visualization resolution are independently maintained in forward and deferred fragment shaders; deferred variants also repeat metallic/roughness sampling.

**Reuse:** Move matching material sampling functions into common GLSL includes. Preserve each entry point's outputs and push-constant layout; the existing property_texture_normal.glsl include demonstrates the available sharing mechanism.

<a id="r30"></a>

### R30 — Progressive-Poisson GPU layout and cell encoding

**Locations:** [assets/shaders/progressive_poisson_accept_phase.comp:37](../../assets/shaders/progressive_poisson_accept_phase.comp#L37); [assets/shaders/progressive_poisson_build_cells.comp:39](../../assets/shaders/progressive_poisson_build_cells.comp#L39).

Both shaders repeat the state BDA record, push-constant layout and packCell2D/packCell3D bit encodings.

**Reuse:** Use a method-local shared GLSL include for the exact ABI and cell encoding. Keep acceptance and cell-building algorithms distinct, and preserve CPU/GPU layout checks.

<a id="r31"></a>

### R31 — K-means GPU layout declarations

**Locations:** [assets/shaders/kmeans_assign.comp:9](../../assets/shaders/kmeans_assign.comp#L9); [assets/shaders/kmeans_reset.comp:24](../../assets/shaders/kmeans_reset.comp#L24); [assets/shaders/kmeans_update.comp:26](../../assets/shaders/kmeans_update.comp#L26).

KMeansStateRef, KMeansPush and matching reduction declarations are copied across the compute stages.

**Reuse:** Share the method-local GLSL ABI declarations, including the new NodesBDA field. Keep stage-specific buffer access qualifiers and execution bodies explicit.

<a id="r32"></a>

### R32 — Point/surfel EWA math

**Locations:** [assets/shaders/point.vert:91](../../assets/shaders/point.vert#L91); [assets/shaders/point_retained.vert:106](../../assets/shaders/point_retained.vert#L106); [assets/shaders/point_surfel.vert:118](../../assets/shaders/point_surfel.vert#L118); [assets/shaders/point.frag:29](../../assets/shaders/point.frag#L29); [assets/shaders/point_retained.frag:39](../../assets/shaders/point_retained.frag#L39).

The point shader family repeats tangent-frame projection, covariance construction, EWA evaluation and guarded lighting.

**Reuse:** Share the matching mathematical pieces as GLSL functions taking values. Retain distinct attribute sources, per-point radius/color policy and push-constant ABIs; consolidate only after comparing covariance floors and other numerical details.

<a id="r33"></a>

### R33 — Curvature profile measurement helpers

**Locations:** [benchmarks/runners/CurvaturePatchProfileRunner.cpp:191](../../benchmarks/runners/CurvaturePatchProfileRunner.cpp#L191); [benchmarks/runners/CurvatureSegmentationProfileRunner.cpp:352](../../benchmarks/runners/CurvatureSegmentationProfileRunner.cpp#L352).

The runners copy point-to-segment distance, tube intersection, boundary geometry collection and boundary-quality evaluation. Multiple long exact matches extend beyond the initial helper.

**Reuse:** Use a benchmark-owned common boundary evaluator and reporting helpers. Keep this oracle independent from the segmentation/patch implementation under evaluation.

<a id="r34"></a>

### R34 — Smoke result JSON envelope

**Locations:** [benchmarks/runners/BenchmarkSmokeRunner.cpp:564](../../benchmarks/runners/BenchmarkSmokeRunner.cpp#L564); [benchmarks/runners/BenchmarkSmokeRunner.cpp:605](../../benchmarks/runners/BenchmarkSmokeRunner.cpp#L605); [benchmarks/runners/BenchmarkSmokeRunner.cpp:1079](../../benchmarks/runners/BenchmarkSmokeRunner.cpp#L1079).

Each emitter reconstructs benchmark/method/backend/dataset/commit metadata, metrics/diagnostic object wrappers and pass/fail output. Existing emitters already differ in formatting and serialization approach.

**Reuse:** Create one envelope/serialization function around explicit per-benchmark metadata, metrics and diagnostics. Preserve each run's precision and measured settings; do not turn smoke output into a stronger evidence claim.

<a id="r35"></a>

### R35 — CI toolchain and vcpkg setup

**Locations:** [.github/workflows/ci-linux-clang.yml:264](../../.github/workflows/ci-linux-clang.yml#L264); [.github/workflows/ci-sanitizers.yml:24](../../.github/workflows/ci-sanitizers.yml#L24); [.github/workflows/ci-source-coverage.yml:34](../../.github/workflows/ci-source-coverage.yml#L34); [.github/workflows/ci-vulkan.yml:56](../../.github/workflows/ci-vulkan.yml#L56); [.github/workflows/ci-release.yml:104](../../.github/workflows/ci-release.yml#L104); [.github/workflows/nightly-deep.yml:18](../../.github/workflows/nightly-deep.yml#L18).

Workflows repeatedly specify the base apt packages, vcpkg binary-cache key/location, bootstrap and VCPKG_BINARY_SOURCES setup. Bootstrap itself already uses the shared script.

**Reuse:** Share just the common setup in a local composite action or setup helper with explicit capability additions. Keep separate CPU/sanitizer/Vulkan jobs, selectors, privileges and concurrency budgets.

<a id="r36"></a>

### R36 — Graphics inspection and setup helpers

**Locations:** [tests/contract/graphics/Test.TransientDebugSurfacePass.cpp:55](../../tests/contract/graphics/Test.TransientDebugSurfacePass.cpp#L55); [tests/contract/graphics/Test.VisualizationOverlayPass.cpp:58](../../tests/contract/graphics/Test.VisualizationOverlayPass.cpp#L58); [tests/integration/graphics/Test.TransientDebugSurfaceGpuSmoke.cpp:114](../../tests/integration/graphics/Test.TransientDebugSurfaceGpuSmoke.cpp#L114); [tests/integration/graphics/Test.VisualizationOverlaySurfaceGpuSmoke.cpp:87](../../tests/integration/graphics/Test.VisualizationOverlaySurfaceGpuSmoke.cpp#L87).

Tests repeat command-pass lookup, barrier inspection, ReorderToRgba and SrgbToLinearPixel, plus portions of smoke readback scaffolding. MockRHI.hpp and MinimalTriangleReadback.hpp are already shared.

**Reuse:** Move stable inspection/setup helpers into tests/support. Keep the assertions, expected pixels and pass-specific packet creation visible in each test so the oracle remains readable and independent.

<a id="r37"></a>

### R37 — Editor geometry fixtures

**Locations:** [tests/contract/runtime/Test.SandboxEditorModels.cpp:371](../../tests/contract/runtime/Test.SandboxEditorModels.cpp#L371); [tests/contract/runtime/Test.SandboxEditorVisualization.cpp:221](../../tests/contract/runtime/Test.SandboxEditorVisualization.cpp#L221); [tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp:279](../../tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp#L279); [tests/support/EditorFeatureTestContext.hpp:1](../../tests/support/EditorFeatureTestContext.hpp#L1).

Several editor suites copy low-level property/topology fixture construction such as SetFloatProperty, SetEdges and SetHalfedges even though a shared editor test-support location exists.

**Reuse:** Share the repeated fixture constructors in tests/support, preserving explicit test-specific property values and expected outcomes. Avoid deriving expected results through production code.

<a id="r38"></a>

### R38 — Curvature parameter validity rules

**Locations:** [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp:89](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cpp#L89); [src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp:142](../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp#L142); [src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cpp:49](../../src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cpp#L49).

Geometry and runtime config validation repeat the mixture-count, tolerance, covariance-floor and related numerical bounds. Runtime adds method/UI-specific bounds.

**Reuse:** Expose or reuse a pure geometry parameter validator after config-to-parameter conversion, then apply runtime-specific rules separately. Preserve the selected method's own bounds; superficially similar patch and feature controls are not automatically interchangeable.

## Further candidates requiring a scope decision

These source similarities are real, but the audit does not classify them as unconditional consolidation work.

| Candidate | Evidence | What must be established first |
| --- | --- | --- |
| BVH/KD-tree median-split construction | [BVH:95](../../src/geometry/Geometry.BVH.cpp#L95), [KDTree:87](../../src/geometry/Geometry.KDTree.cpp#L87) | A small shared split/partition primitive may help. Their point-versus-AABB bounds and query contracts do not justify merging the indices into a generic tree framework. |
| General 64-bit hash helpers | [IOBackend:18](../../src/core/Core.IOBackend.cpp#L18), [TaskGraph:56](../../src/core/Core.Dag.TaskGraph.cppm#L56), [FrameGraph:54](../../src/core/Core.FrameGraph.cppm#L54), [GpuWorld:34](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L34), [procedural geometry:20](../../src/runtime/GeometryIntegration/Runtime.GeometryPlanBuilders.Procedural.cpp#L20), [parameterization:92](../../src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cpp#L92), [recipe policies:68](../../src/runtime/AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp#L68) | Share byte/word mixing only after documenting seeds, framing, floating-point normalization and token stability. Existing Core.Hash is 32-bit, so it is not a drop-in replacement. R19 is the narrower confirmed identity contract. |
| Saturating arithmetic and alignment | [SupportRadius:105](../../src/geometry/Geometry.SupportRadius.cpp#L105), [consolidation:379](../../src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp#L379), [LinearArena:26](../../src/core/Core.Memory.LinearArena.cpp#L26), [GpuWorld:83](../../src/graphics/renderer/Graphics.GpuWorld.cpp#L83) | Tiny helpers may merit a common core primitive if enough callers need the exact same overflow, zero-alignment and failure contract. Do not hide different policies behind a generic utility. |
| Retained history texture pairs | [HZB:207](../../src/graphics/renderer/Graphics.HZB.cpp#L207), [reconstruction:347](../../src/graphics/renderer/Graphics.Reconstruction.cpp#L347) | Common allocation/retirement details exist, but ownership and initialization semantics must be compared before choosing the smallest helper. |
| Mesh export traversal | [HalfedgeMesh.IO:2427](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L2427), [HalfedgeMesh.IO:2644](../../src/geometry/Geometry.HalfedgeMesh.IO.cpp#L2644) | As with R16, share validated geometry/header pieces only after preserving per-format normal/color/topology rules. |
| Repeated geometry operation job scaffolding | [mesh operations:5889](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L5889), [mesh operations:7347](../../src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp#L7347) | Extract a stable repeated snapshot/publication operation only where the same stale-source and output policy applies. A generic method service would be an unjustified expansion. |
| Offline thickness CLI wrapper | [refine_thickness_parts.py:91](../../tools/diagnostics/curvature/refine_thickness_parts.py#L91), [select_thickness_parts.py:112](../../tools/diagnostics/curvature/select_thickness_parts.py#L112) | Common input/provenance/output code exists, but these are bounded experiment entry points. Consolidate if they become maintained workflows, retaining per-script source provenance. |
| Shared spatial-index adoption | [consumer inventory](../architecture/spatial-index-consumers.md), [SpatialIndexCache](../../src/runtime/GeometryIntegration/Runtime.SpatialIndexCache.cppm) | Remaining point-analysis callers may benefit from shared index ownership. This requires workload evidence and exact neighborhood semantics; it is not a mandate to replace every KD-tree, octree, grid or brute-force oracle. |

## Similar code that should remain distinct

- **Backend implementations and recording doubles:** Null/Vulkan and Null/GLFW are supported implementation seams. They are not duplicate features to delete. R27 concerns identical no-op helpers only.
- **Independent numerical oracles:** CPU references and exhaustive benchmark truth must remain independent of the optimized/GPU kernel being checked. R33 shares the measurement code across runners, not with the algorithm under test.
- **Graph and point-cloud Gaussian noise:** [graph](../../src/geometry/Geometry.Graph.Utils.cpp#L1456) scales by bounding-box diagonal; [point cloud](../../src/geometry/Geometry.PointCloud.Utils.cpp#L969) scales by average spacing. Both already call `Geometry::Sampling::GaussianDisplacement`. Merging their complete operations would change semantics.
- **DEC and sparse solver declarations:** [DEC](../../src/geometry/Geometry.HalfedgeMesh.DEC.cppm#L269) exposes aliases/forwarding over the sparse solver contract. Similar declarations alone do not establish duplicated solver implementations.
- **LOP grid shader stages:** [count](../../assets/shaders/lop_grid_count.comp) and [scatter](../../assets/shaders/lop_grid_scatter.comp) already include `lop_gpu_common.glslinc`. Their counting versus indexed-scatter writes are different operations.
- **Distinct geometry domains and result records:** repeated fields in DTOs, const/mutable views, graph/mesh containers and exported pass APIs are not sufficient evidence that a new inheritance hierarchy or template framework would improve them.
- **Different geometric queries:** point-nearest, nearest triangle, distance to a ray, topology-geodesic distance and descriptor-space matching cannot share an index merely because each searches for something close.
- **Test assertions and scenario setup:** similarity alone does not justify parameterizing every test. Share stable fixtures and inspection routines while keeping expected behavior legible.

## Suggested order

1. Start with **R14**: point-cloud IO can adopt an existing helper, with a small and clear ownership boundary.
2. Address **R01–R03**, then **R04–R06**: these unify editor rules and mutations used by current product workflows.
3. Address **R19** and **R28–R31**: shared identity and shader ABI definitions reduce the chance of paired implementations drifting.
4. Take resource-lifecycle work (**R12, R21, R23–R25**) in separate reviewed slices with focused lifetime/failure tests.
5. Apply lower-risk panel, output and fixture consolidation when those areas are next touched.

The numbered entries are a reviewable follow-up inventory, not authorization for a bulk refactor. Each implementation should preserve current behavior, remain within its owning layer, and run the relevant focused CPU tests; rendering/ABI changes additionally need their applicable Vulkan coverage.
