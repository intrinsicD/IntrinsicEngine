---
id: UI-037
theme: F
depends_on: [BUG-093, BUG-096, RUNTIME-202]
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive staged implementation; fixed diffs, review, tests and task checkpoints retain verification without unattended custody.
maturity_target: Operational
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, runtime.editor-prepared-frame-locality, runtime.render-diagnostics-locality, runtime.processing-compilation-locality, runtime.spatial-query-locality, runtime.kernel-interface-locality, runtime.texture-bake-interface-locality]
---
# UI-037 — Linear domain-action readiness and disabled-reason tooltips

Current continuation: see [normal topology checkpoint and open points](#continuation--normal-topology-readiness-2026-09-20).
Read that checkpoint plus the initial scope before consulting the historical slices.

## Remaining closure estimate — 2026-09-19

Working estimate at `d15cc352a`: **12 bounded implementation/verification slices,
with a planning range of 10–14**, not twelve promised sessions. Medium confidence:
this is a targeted source inspection, not an exhaustive completed action matrix.
Each implementation slice includes its own focused tests and review; the final
slices close cross-family coverage and the task, rather than postponing testing.

| Slice | Closure deliverable |
| --- | --- |
| 1 — complete | Shared cached input readiness for density, density weights and spacing; see the scalar checkpoint. |
| 2 — complete | Bilateral/descriptor readiness with revision-keyed position-plus-normal validation; see the point/normal checkpoint. |
| 3 — complete | Point-construction readiness without per-preview point capture; see the construction checkpoint. |
| 4 — complete | Normal topology readiness without deletion-mask copies/count scans; empty-face no-op/fallback semantics retained and tested. |
| 5 | ICP source/target and point-to-plane normal readiness without repeated captures. |
| 6 | Close mesh/curvature/UV admission gaps against their existing validators; reuse current metadata previews. |
| 7 | Runtime-owned parameterization strategy, pin and boundary prerequisites. |
| 8 | Texture-bake request-specific property/UV/device/range readiness and shared presentation. |
| 9 | Close service-action/backend/variant gaps for K-Means, Progressive Poisson, consolidation and outlier actions. |
| 10 | Finish common prepared-frame readiness, visible disabled actions and remaining app-owned prerequisite decisions. |
| 11 | Complete the cross-family table-driven readiness/validator matrix, stale-state and zero-scan regressions. |
| 12 | Finish real ImGui action/option tooltip and command coverage; full verification, review and retirement. |

Evidence behind the original estimate: scalar previews still called `CapturePointScalarField` synchronously;
bilateral/descriptors scan live positions/normals; construction and ICP preview
capture data; normal topology still copies masks. Mesh/curvature/UV already have
shared metadata admission and buttons, so those are gap closure, not rewrites.
Parameterization Run still combines selection booleans with config-lane readiness,
and bake presentation retains `CanBake`/inline-only prerequisite handling.

Largest split risks are paired-property/ICP cache keys and parameterization
prerequisites. Smaller service/presentation items may combine. The finite-input
cache, shared action button/tooltip and earlier per-family tests are already done
and must be reused. Do not restart those implementations.

**Non-gating follow-ups:** general compile-owner cleanup or compile-time benchmarks;
worker scheduling/per-drain budgeting unless a measured problem prevents the
existing nonblocking-readiness contract; extra normal-row mapping coverage unless
mapping changes. These do not enlarge UI-037 closure. Keep compilation work with
its own task owner. No additional algorithms or broad UI redesign are included.

## Goal
- Keep every action in the Sandbox's linear mesh, UV, bake, point-cloud,
  registration, and parameterization workflow visible while making its current
  readiness explicit: each typed runtime operation supplies the same
  authoritative `ActionReadiness { Enabled, DisabledReason }` used by apply,
  and the app disables unavailable controls and explains the prerequisite.

## Non-goals
- No new geometry algorithm, method backend, processing parameter, or automatic prerequisite repair.
- No app-side inspection of geometry properties, selection cardinality, device state, or method configuration to rediscover whether an action is valid.
- No replacement of command-time validation. Readiness is a side-effect-free preview for presentation and automation; every runtime command still revalidates immediately before apply.
- No redesign of Sandbox navigation, input capture, window registration, or panel layout beyond keeping the existing linear controls present and understandable.
- No broad selected-entity analysis service. Add a feature-owned
  `JobService` derivation only when a concrete readiness predicate cannot be
  answered from existing copied metadata or cached results.

## Context
- Continuation after RUNTIME-264/265: retain canonical service Types owners,
  shared context adapters and family-owned prepared frames. This task remains
  the owner of readiness/reason consolidation; RUNTIME-266/267 only narrow
  compilation dependencies. UI-037 is independently actionable and does not
  wait for BUILD-009 measurements.
- Owner/layers: runtime feature owners own selection/domain/config/capability
  validation and expose copied readiness with their operation snapshots;
  family-owned runtime prepared frames carry those values to
  `src/app/Sandbox/Editor/`, which owns only ImGui presentation. The dependency remains `app -> runtime`.
- UV regeneration now exposes typed admission readiness; other operations still
  carry separate availability fields. `Sandbox.PanelSupport.hpp` supplies the
  shared `DrawProcessingActionButton` and `DrawDisabledReasonTooltip`.
  Unify the remaining readiness representation and cover the full action
  inventory without moving family logic into a shared editor workspace interface.
- The readiness inventory covers mesh processing actions (denoise, curvature, remesh, subdivide, simplify, and recompute normals), selected-mesh UV regeneration, texture bake, point/graph/mesh normal generation where offered, point-cloud outlier removal, K-Means, Progressive Poisson, ICP, and parameterization.
- ICP readiness requires two distinct compatible entities/property sources,
  not point-cloud provenance. Reuse the canonical property/topology preflight
  shared with `RUNTIME-207` and `UI-040`, including point-to-plane's finite,
  count-matched target normals. `BUG-096` supplies the authoritative normal
  semantics; readiness must never advertise point-to-plane while executing
  point-to-point. This task consumes that preflight, not a duplicate ICP
  integration or provenance filter.
- Parameterization readiness includes the selected editable mesh, validated
  strategy/config, and strategy-specific pin or boundary prerequisites.
  Texture-bake readiness includes an operational device, canonical compatible
  source property, finite UVs, and valid output resolution/range; consumer
  binding is a separate caller-owned operation. Backend choices report their
  own capability readiness without changing requested-versus-actual fallback
  policy.
- Control surfaces remain co-equal: each typed runtime operation exposes the
  same plain readiness value to UI and agent/controller callers. Actions that already
  have a config lane keep config-file/UI/agent parity through their typed
  preview/apply path; this task does not invent config state for commands that
  are not currently config-backed.
- Readiness uses the smallest existing source of truth in order: copied
  selection/config/capability snapshots, the `RUNTIME-192` canonical property
  catalog and compatibility queries, then an already available generation-
  keyed result. A feature owner may add one generation-keyed `JobService`
  derivation only for a named finite/full-buffer predicate that cannot be
  answered by those sources. Pending expensive results disable the affected
  action; no path may reintroduce a full-buffer scan in the per-frame ImGui
  model build.

## Slice plan
- **Slice A — Runtime readiness contract.** Add the shared plain readiness
  record to feature-operation snapshots,
  stable reason priority, validator reuse, generation keys, and table-driven
  model tests while reusing canonical metadata/current caches and adding only
  concrete feature-owned derived results that the readiness matrix proves it
  needs.
- **Slice B — App presentation.** Reuse `DrawDisabledReasonTooltip` and
  the exact app-internal free-function/hover-flag convention from
  `BUG-093`; keep controls visible in linear order, remove duplicated app
  validation, and pin command/no-command behavior without changing algorithms.
- **Slice C — Inventory and operational proof.** Cover every named workflow and
  backend/variant option, add the two-frame real ImGui hover integration, and
  cite that run before claiming `Operational`.

## Required changes
- [ ] Export one right-sized runtime value record,
      `ActionReadiness { Enabled, DisabledReason }`, reuse it in each typed
      feature-operation snapshot, and include a value for every listed action
      and selectable backend/variant in its family-owned prepared frame. Do
      not create a monolithic readiness service or an all-method interface
      that expands shared editor compile dependencies.
- [ ] Derive readiness in runtime from the same selection snapshots, config preview results, capability state, property compatibility checks, and command validators that govern apply. Factor shared pure predicates/results where necessary; do not copy command rules into a parallel readiness implementation.
- [ ] Make every disabled reason deterministic, non-empty, and actionable: name the failed prerequisite and the user action that can satisfy it. Preserve the first stable blocking reason when several prerequisites are absent.
- [ ] Keep actions in their existing linear order and render them even when
      unavailable. Wrap disabled widgets/options with
      `ImGui::BeginDisabled()` / `ImGui::EndDisabled()` and invoke the shared
      `BUG-093` helper immediately after the item; its exact convention is
      `ImGuiHoveredFlags_ForTooltip |
      ImGuiHoveredFlags_AllowWhenDisabled`.
- [ ] Reuse the one app-internal disabled-reason free function established by
      `BUG-093` across affected buttons, menu/selectable entries, and
      backend/strategy options. It uses
      `ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled`,
      accepts readiness text only, and performs no selection, geometry,
      config, or device validation.
- [ ] Remove affected panel early returns and duplicated app-side prerequisite checks that hide actions or manufacture independent reasons. Retain app-only layout decisions and local editable config drafts.
- [ ] Keep runtime apply paths fail-closed against stale selection/config/capability state after a readiness model was built, including asynchronous/derived-job submission and completion.
- [ ] Resolve each readiness input through copied metadata and canonical
      compatibility queries first, then reuse an existing generation-keyed
      cached result where available. Only when a named readiness rule still
      requires a full-buffer finite/property derivation may its feature owner
      add a generation-keyed `JobService` result; pending work disables that
      action with a reason, and steady per-frame model construction performs
      no full-buffer geometry/property scan.

## Tests
- [ ] Add a pure runtime/model test named
      `SandboxEditorUi.ActionReadinessDerivesDomainPrerequisiteReasons` that
      table-drives every listed action through ready and representative blocked
      states, asserts `Enabled` parity with the authoritative validation result,
      and asserts a stable non-empty unlock reason when disabled.
- [ ] Cover ICP separately: one selection, duplicate source/target, compatible
      mesh/graph/point-cloud properties (including non-vertex sample domains),
      missing/invalid target normals, and valid point-to-plane normals. Assert
      readiness matches runtime preflight; no point-cloud-only restriction or
      point-to-point substitution is permitted.
- [ ] Cover parameterization strategy prerequisites, UV regeneration, texture-bake property/device requirements, and unavailable GPU/backend options without invoking ImGui.
- [x] Add an app integration test named
      `SandboxEditorPresentation.DisabledActionReasonTooltipAppearsAfterTwoFrames`.
      Frame one establishes the disabled item's rectangle; frame two positions
      the mouse over it and asserts the tooltip window/text. The test must
      exercise `AllowWhenDisabled` and the exact runtime-provided reason rather
      than a duplicated app literal.
- [ ] Assert enabled controls emit their existing typed command and show no disabled-reason tooltip; disabled controls emit no command on click. Command-level stale-state tests continue to prove apply-time revalidation.
- [ ] Add a steady-selection regression proving metadata/cached readiness
      performs zero full-buffer finite/property scans. For every concrete
      derived readiness result introduced by this task, prove a changed
      generation invalidates the old result and reports pending until the
      fresh result applies; metadata-only rules owe no artificial pending
      state.

## Docs
- [x] Update `src/runtime/README.md` with the readiness record, authoritative-validation reuse, deterministic reason policy, and the distinction between preview readiness and apply-time validation.
- [x] Update `src/app/Sandbox/README.md` with the linear disabled-control convention, `AllowWhenDisabled` hover behavior, and config/agent parity.
- [x] Regenerate `docs/api/generated/module_inventory.md` if the exported runtime module surface changes.

## Acceptance criteria
- [ ] Mesh/UV/bake/normal/outlier/K-Means/Progressive-Poisson/ICP/parameterization controls remain visible in their linear workflow and cannot be invoked while their runtime readiness is disabled.
- [ ] Hovering every disabled action or unavailable option presents its runtime-supplied prerequisite reason, including in the two-frame ImGui integration test.
- [ ] Runtime feature owners are the sole owners of action readiness and app
      code contains no duplicate geometry, config, capability, or selection
      validation for the affected actions.
- [ ] Agent/controller consumers can inspect the same readiness records. For
      config-backed actions, config files, UI, and agents share the existing
      typed preview/apply path; non-config-backed commands remain on the same
      UI/agent runtime seam without adding fictitious config parity.
- [ ] ICP point-to-plane is never shown as ready, executed, or reported unless its finite count-matched target normals are actually consumed.
- [ ] Per-frame readiness construction remains nonblocking: it reads metadata
      and current generation-keyed results, never scans selected geometry or
      properties synchronously.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.ActionReadinessDerivesDomainPrerequisiteReasons$|^SandboxEditorPresentation\.DisabledActionReasonTooltipAppearsAfterTwoFrames$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No app-owned readiness truth, geometry/property scans, command-validator copies, or UI-specific validation rules.
- No hiding an unavailable action, replacing its hover reason with inline-only status text, or omitting `AllowWhenDisabled` from disabled-item hover detection.
- No second tooltip wrapper or hover-flag convention alongside the app-internal
  free function established by `BUG-093`.
- No weakening, skipping, or treating readiness as a substitute for runtime apply-time and derived-job completion validation.
- No silent backend/algorithm substitution or requested-versus-actual misreporting, especially for ICP point-to-plane.
- No per-action service/interface/registry hierarchy or replacement Sandbox
  readiness facade; use the shared plain record in feature snapshots and the
  app-owned domain-window model.
- No synchronous full-buffer finite/property scan from the per-frame readiness
  or ImGui path. When a concrete readiness rule needs an expensive derived
  result, pending work remains an explicit disabled state; do not introduce a
  global selected-analysis module for it.
- No unrelated algorithm, renderer, input-lifecycle, import, scene-management, or navigation changes.

## Maturity
- Target: `Operational` through the app-linked two-frame ImGui integration
  test plus runtime contracts covering every listed action. No Vulkan-specific
  follow-up is owed because readiness and tooltip presentation are
  backend-neutral; backend availability remains an input to the model.

## Interactive checkpoint — 2026-09-16
Operator-directed compilation/reuse continuation on `codex/editor-compile-locality`,
baseline `ede64a7dc`; root is the only writer, Claude reviews fixed packets under
standing authorization. This is the first bounded implementation, not task closure.

Reuse/size decision: config-backed controls already share `DrawProcessingExecution`
and runtime method previews. Put the plain `ActionReadiness` in existing
`Runtime.EditorProcessing`, combine the live config lane with those previews, and
reuse one private config-availability predicate in preview and apply. Add the common
button to existing PanelSupport; it invokes the existing tooltip function directly.
No new module/file/state owner, geometry rule, async job or config schema.

Adopt seven shared execution controls (normal estimation, keypoints, descriptors,
density, density weights, spacing, bilateral filtering). Construction shares the
readiness/button but retains its resolved request. Keep ICP's trajectory trigger and
outlier Analyze/RemoveMarked separate pending their own inventory. Normal's local
`config` is a reference to `Normals.Draft`; use the existing template and reapply
that draft at click time. Preserve result sinks and immediate/queued semantics.
Historical config errors remain display state, no longer an independent disabled
predicate; a successful retry clears them. Method command validation still runs.

Remaining: convert family snapshots/records to the common representation, cover
mesh/UV/bake/Poisson/K-Means/ICP/outlier controls and every backend/variant, remove
remaining action-hiding early returns, complete authoritative property/finite-cache
readiness and the full table-driven inventory. Do not check off broad acceptance
criteria or claim Operational from this first button/control slice.

### Verified first slice
- Canonical `ci` configure, focused targets and `IntrinsicTests` build pass.
  Full exclusion-only CPU gate: 4,668 passed, one expected ASan-only lifecycle
  skip, zero failures among 4,669 selected (140.86 s). No GPU/sanitizer execution
  or compilation-speed claim. Existing compiler locality guards pass.
- Real two-frame tooltip and disabled/enabled mouse-click tests pass through
  the production button; disabled clicks emit no config command, enabled clicks
  use the runtime normal-config apply path. Extend the existing real-panel test
  to normals: invalid draft and missing inputs publish nothing; valid output
  matches the direct runtime reference. Corrected its initial window-ID typo.
- Runtime coverage checks every missing config-lane component, expiration,
  stable reason priority, nonempty rejection text, and zero config callbacks
  during readiness. Full method/property/config tests remain green.
- Claude reviewed the plan and fixed source. Preserve construction normalization
  and the distinct ICP/outlier triggers. Strengthen GUI logging isolation and
  teardown; surrounding Resolve/using declarations and compiled tests refute
  the review's null-guard/type-mismatch concerns. No new helper is a test-only seam.
- Five existing production C++ files: +26 net physical lines; shared panel
  implementation -15 lines, with shared readiness/button behavior added to the
  existing owners. Zero new production files/modules, import edges or CMake entries.
  Module inventory regenerated (419 modules, no content change).
- Scope/layering/tests/docs sweep passes. Clean-workshop: rows 1–3 and 8 pass;
  renderer/pass/recipe rows 4–6 are unchanged; row 7 records this verified slice
  with UI-037 still active. No compatibility wrapper, new service or ownership layer.

Focused reproduction after the canonical build:
```bash
ctest --test-dir build/ci --output-on-failure -R 'NormalEstimation|SandboxProcessingPanels|SandboxEditorPresentation|PointConstruction|ProcessingCompilationLocality|EditorCompilationLocality|SandboxEditorSessionLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
```
The original full-inventory test selector remains the later task-closure gate;
this checkpoint does not represent every action/backend or complete the task.

### Verified execution slice — 2026-09-16
Baseline `93cf136aa`, same operator direction and sole-writer/Claude review policy.
Reuse decision: `DrawProcessingExecution`, construction, outlier and ICP duplicate
apply-config / record error / execute / publish. Extract only that mechanism to a
private template in the existing panel implementation. Construction keeps its
resolved request; outlier Analyze/RemoveMarked keep separate canonical previews;
ICP keeps final-pose versus trajectory-edit triggers. Reuse the existing runtime
readiness/button for both outlier actions and ICP. Rejecting click-time config must
not run previously accepted settings; programmatic trajectory triggers must obey
live readiness too. No new module, file, import, service, validator or state owner.
Keep family previews and asynchronous command validation unchanged. Add real-panel
coverage using the existing harness, including injected config validation rejection,
retry, trajectory step versus final pose, and removal provenance. A separate helper
surface would only be justified by an actual caller outside this implementation.

The injected section-rejection test exposed a shared-owner gap: the file parser
retains a rejected section with `FallbackApplied`, which the typed processing
apply previously accepted. At `ApplyEditorProcessingConfig`, reapply the existing
pure section updater to the fallback preview and reject if it changes the accepted
sections: that means the preview lost requested edits. Unrelated section fallback
remains usable, and identical requested/accepted settings remain a valid no-change.
This avoids diagnostic-string parsing and new section metadata/signature fan-out;
Claude's review caught the overly broad initial `Valid`-only guard. A direct
shared-owner test and both actual panels cover section fallback. The real-panel harness
now composes AsyncWorkModule so job-count assertions observe submitted work.

- Canonical `ci` configure, focused builds and final `IntrinsicTests` build pass.
  Final full CPU gate: 4,670 passed, one expected ASan-only lifecycle skip,
  zero failures among 4,671 selected (139.28 s). No GPU/sanitizer execution or
  compile-speed claim. Compiler locality guards remain green.
- New real-panel tests prove no publication or queued work after lost config
  edits, successful retries, separate Analyze/RemoveMarked requests, re-detection
  after removal, ICP final pose versus zero-step, invalid iteration rejection
  and unavailable-input trajectory blocking. Existing runtime contracts retain
  domain, stale-input, publication and undo coverage. The shared scalar edit
  test driver replaces duplicate ImGui input sequencing in the existing test.
- Claude plan/source/fix review completed. Corrected unrelated-fallback handling;
  verified that only Valid/FallbackApplied are usable loader states, that every
  updater sets app sections, and that removal assigns the existing PropertySet.
  No speculative future-state machinery or weakened exact-once job assertion.
- Scope/layering/tests/docs sweep and structural gates pass. Two existing
  production implementation files: +1 net physical line (-8 panel, +9 runtime),
  including the lost-edit guard. No new production files, modules, imports,
  public surfaces or CMake entries; no inventory change required.
- UI-037 remains active: family readiness records, mesh/UV/bake/Poisson/K-Means,
  backend/variant options and metadata/cache-based preflight inventory still
  need completion. Do not infer whole-task Operational closure from this slice.

Focused regression selector:
```bash
ctest --test-dir build/ci --output-on-failure -R '^NormalEstimationConfig.RoundTripAndSharedPreviewApplyRun$|^SandboxProcessingPanels\.(OutlierActionsApplyTheirOwnRequestAndRetryRejectedConfig|RegistrationRetriesConfigBeforeRunningAndPreservesTrajectoryChoice|ReusedExecutionPanelsRejectInvalidRequestsBeforePublishing)$' --no-tests=error --timeout 60
```

### Progressive Poisson config slice — 2026-09-16
Baseline `5cf671e06`, operator-directed reuse/compilation continuation with Claude
review and one writer. `ApplyEditorProgressivePoissonConfigCommand` duplicates
`ApplyEditorProcessingConfig` and publishes a bespoke status/command/result, with
both a full preview and an apply record that already contains that preview.
Replace it directly with the same typed config/apply result used by other methods;
remove the superseded records and unused interface import. No compatibility shim,
new file or module. Preserve source IDs, registered validation, diagnostics,
NoChange, attachment guards and shared lost-edit fallback rejection.
Manual and debounced execution share one apply-before-run lambda; a rejected
attempt clears pending auto-run so it cannot spin. Actual GUI tests must exercise
manual and debounced execution, rejection and retry. Other family readiness and
property/cache preflights remain scoped follow-ups in this task. The narrow shared
config owner is sufficient; no replacement service or generic panel framework.

Verified checkpoint:
- Progressive Poisson now reuses `ApplyEditorProcessingConfig` and
  `RuntimeEngineConfigApplyResult`; delete the three superseded public records,
  duplicate config preview/apply implementation and unused direct config import.
- Manual and debounced runs share apply-before-run and clear pending execution
  on rejection. Preserve source IDs, NoChange success, registered validation,
  diagnostics and lost-edit fallback rejection. Fix the unrelated K-Means
  no-selection button incorrectly displaying the Progressive Poisson label.
- Contract coverage includes missing/expired config lanes, typed validation,
  apply rejection, NoChange, source IDs and lost-edit fallback. Real production
  ImGui coverage exercises manual rejection/retry, debounce rejection without
  repeated retries, manual recovery and successful debounce execution. Advance
  the controlled ImGui clock instead of sleeping.
- Claude reviewed the plan, fixed source and strengthened regression. No blocking
  finding remains: exact job counts use the deliberately quiet existing harness;
  scalar-edit helper steps are equality-selected and negative steps are no-ops.
- Canonical `cmake --preset ci` and `IntrinsicTests` build pass. Three focused
  cases pass; full exclusion-only CPU CTest selects 4672, with 4671 passes and
  one expected ASan-only lifecycle skip, zero failures (140.99 s).
- Layering, test layout, task policy/links, docs links/sync, root hygiene and skill
  mirror checks pass. Module inventory regenerated (419 modules, no content diff).
  Three existing production C++ files lose 86 net physical lines; no new files,
  modules or CMake entries. No new timing, GPU or sanitizer runtime evidence.
- Scope/layering/tests/docs sweep passes. Clean-workshop rows 1–3 and 8 pass;
  renderer/pass/recipe rows 4–6 unchanged; row 7 records this bounded slice.
  UI-037 stays active for mesh/UV/bake, family readiness, backend/variant and
  metadata/cache preflight coverage. This is not whole-inventory closure.


### Parameterization config slice — 2026-09-16
Baseline `5d5c3fe39`, continuing the operator-directed compile/reuse work with
one writer and fixed-packet Claude review. Reuse `ApplyEditorProcessingConfig`
and its generic result for parameterization; remove the redundant status,
command and nested-preview result, plus the app request-builder record/alias.
Keep the typed pre-serialization validator (including inactive strategy fields)
and source IDs; this is required because serialization normalizes invalid enums.
The existing app action remains the single apply-before-execute owner. Preserve
zero-entity rejection; let runtime own strategy validation. Use the existing
readiness/button for config availability without claiming full method preflight.
Add config-lane and real-panel rejection/retry coverage, keeping rejected drafts
out of the solver. No new file, module, service, compatibility wrapper or method.
Broader mesh/UV/bake readiness and cached geometry predicates remain open here.

Verified checkpoint:
- Replaced three public parameterization config wrapper types with the existing
  typed config and shared apply result. Removed the app request-builder type,
  builder, config alias and redundant strategy whitelist. Five existing production
  C++ files lose 142 net physical lines; zero new files/modules/CMake entries and
  one redundant direct interface import removed. No compile-time measurement.
- Apply and Run use shared config readiness/buttons. Run retains its existing
  selected-mesh predicate and apply-time validation; complete strategy/topology
  readiness remains open. Invalid typed enums, including inactive strategy fields,
  fail before serialization. Zero-entity actions fail before config mutation.
- Config fallback cannot silently discard requested edits and execute accepted
  settings. Rejected drafts stay editable/retryable. NoChange still executes;
  success feedback and panel/default source IDs are preserved.
- Claude reviewed the plan and fixed diff. Restored applied/unchanged UI feedback
  from its valid finding; checked exact section constant, complete serialized
  fields, runtime strategy switch, missing-entity button gating and tests against
  remaining questions. Final bounded verdict finds no blocker.
- Runtime tests cover missing/expired config lanes without callbacks, apply
  rejection, lost-edit fallback, default/custom source IDs and NoChange. Replaced
  the request-copy test with real config round-trip/no-side-effects coverage.
  Actual ImGui controls prove rejected Apply and Run leave config/UV untouched,
  retry without another edit succeeds, and a NoChange run restores changed UVs.
- `cmake --preset ci` and focused targets pass under canonical Clang 23; all 32
  focused parameterization cases pass. Final `IntrinsicTests` build and full CPU
  selector pass: 4672 passes plus one expected ASan-only lifecycle skip, zero
  failures, 140.39 s. No GPU or sanitizer runtime claim.
- Layering, test layout, task policy/state links, docs links/sync, root hygiene,
  skill mirrors and diff whitespace checks pass. Module inventory regenerated
  (419 modules, no content change); touched header/module documentation has zero
  errors, with existing lifetime/control-boundary comments retained.
- Scope/layering/tests/docs sweep passes. Clean-workshop rows 1–3 and 8 pass;
  renderer/pass/recipe rows 4–6 unchanged; row 7 records this bounded slice with
  UI-037 still active. No new ownership layer or compatibility path.

## UV regeneration admission slice — plan

- Continue the operator-directed reuse/readiness work. Replace the app's mesh-only
  availability pair with a typed runtime preview and the existing shared action
  button/tooltip. Keep the command and preview on one validation path.
- Reuse discovery: `BuildMeshSoupFromGeometrySources` owns source metadata and
  topology validation; extract its metadata-only prefix in the existing private
  MeshSupport implementation. Reuse the UV job identity and pending result before
  mesh preparation so duplicate requests do not repeat source copies. Keep queued
  result delivery, attachment guards, undo and stale-source publication checks.
- Right-sizing: one typed preview in the existing parameterization owner, no new
  files, modules, services, config lane or broad editor dependencies. Delete the
  obsolete availability pair from `EditorCommon`. No compile-time claim.
- This slice covers cheap admission checks only. Full finite/topology feasibility
  remains command-time validation; generation-keyed expensive readiness is still
  open under this task, and preview documentation must make that boundary clear.

## UV regeneration admission slice — implementation/review checkpoint

- Preview and apply now share session/parameter/entity/source-metadata checks and
  the UV job identity. Both bake controls use one command and the shared action
  button/disabled tooltip. The obsolete common-model availability fields are gone.
  Invalid finite/non-negative texel density is rejected for direct callers too.
- `ValidateMeshSoupSourceMetadata` extracts the existing builder prefix and
  returns status plus diagnostic, preserving the builder's non-empty-mesh success
  contract. Preview performs no mesh construction or buffer walk. Duplicate jobs
  return `Pending` before mesh snapshots and add no result callback; original
  synchronous fallback and terminal publication/undo guards remain.
- Claude reviewed the plan and fixed diff. Adopted the status/diagnostic-only
  helper and retained the original `JobCommands.Available()` dedup boundary.
  Verified `<cmath>` and removed all old-field readers. A final review concern
  that a sticky failed first-click result could make the new UI test pass was
  rejected: its explicit no-result, success, job-count and publication assertions
  fail that scenario. Do not reset the result merely to hide a failed assertion.
- Focused canonical-ci run: all 32 selected UV, shared-button and real processing
  panel tests pass. The new UI test initially omitted its own ImGui window;
  fixed its callback with Begin/End and reran. Contracts cover preview/apply
  rejection parity, expired handles, metadata-only admission, async dedup before
  topology traversal, one terminal delivery, and readiness restored at completion.
  The real control test proves disabled activation submits nothing and an enabled
  request publishes UVs and adopts the returned atlas extent.
- Scope/architecture review: app remains a runtime consumer; metadata helpers
  remain private compiled runtime code, no new dependency edge/file/module/service
  or config lane. Production code grows by 45 lines across seven existing files
  to expose the missing preview and share its validation. This is readiness and
  reuse work, not a measured code-size or compilation-time reduction.
- UI-037 remains active: complete numerical/topology readiness and the remaining
  action/backend inventory still need closure. This preview deliberately does
  not certify full-buffer numerical feasibility.
- Final verification: `cmake --preset ci`, full `IntrinsicTests` build, and the
  canonical exclusion-only CPU gate: **4,674 passed, one expected ASan-only GLFW
  lifecycle skip**, zero failures (4,675 selected; 139.92 s). Layering, test layout,
  task validation/policy/state links, docs links/sync, root hygiene and skill
  freshness pass; module inventory regenerated unchanged. No sanitizer or Vulkan
  runtime execution was claimed for this CPU/UI slice.

## Mesh-field draft/execution slice — plan

- Continue operator-directed reuse work with curvature and geodesics. Both retain
  independent draft/init/dirty state and prevent retry after rejected config.
  Reuse `ProcessingDraftState`, the existing config serializers,
  `ApplyProcessingExecution`, and shared readiness/action controls.
- Keep geodesics' entity/source-index relationship explicit: load initial active
  sources, clear them on later entity changes including deselection, and preserve
  rejected drafts until active config changes or a retry succeeds. Curvature keeps
  its queued callback and both methods keep typed Show-property actions.
- No new file/module/public surface or geometry validator. Current method
  admission predicates remain; complete runtime-owned numerical readiness stays
  open. The change removes panel state/execution duplication, with no timing claim.

## Mesh-field draft/execution slice — checkpoint

- Curvature and geodesics reuse `ProcessingDraftState` and
  `ApplyProcessingExecution`. Run always reapplies the visible draft, executes
  only after accepted config (including `NoChange`), and permits retry after a
  rejected edit. Shared action controls expose the existing admission reasons.
  Config failures and property-display feedback no longer replace geodesics'
  last operation message.
- Synchronize only a present active config. Initial geodesics sources load when
  the first mesh becomes available; later entity changes clear mesh-local sources
  even if reset publication is rejected. External active-config changes replace
  the draft through the existing serialized-key comparison. No new config or
  geometry validation implementation, source file, module, or dependency.
- Claude reviewed the plan/diff/fix. Fixed missing-config default substitution;
  local review also caught first-use ordering when the window opens without a
  selected mesh. The reviewer withdrew an incorrect active-versus-draft test
  objection after the actual rejection/retry sequence was explained.
- Final focused canonical-ci run: **52 integration tests passed**. New real-widget
  cases cover rejected edits/no execution, retry without another edit, `NoChange`
  re-execution, external configuration updates and rejected entity reset. Existing
  selection-reset coverage now starts with no selected mesh. Curvature's queued
  result sink and geodesics' synchronous publication remain distinct.
- Production footprint: **29 lines removed from one existing implementation
  unit**. No public interface changes or measured compile-time claim. Task stays
  open for remaining action/backend controls and full runtime numerical readiness.
- Full verification: canonical `ci` configure, `IntrinsicTests` build and CPU
  exclusion-only CTest selector pass: **4,676 passes plus one expected ASan-only
  GLFW lifecycle skip**, zero failures (4,677 selected; 141.27 s). Layering,
  test-layout, task validation/policy/state links, docs links/sync, root hygiene,
  skill freshness and diff checks pass. No public surface/inventory change,
  sanitizer execution or Vulkan runtime claim.


## Segmentation draft/execution checkpoint — 2026-09-16

- Operator-directed reuse: curvature segmentation now uses `ProcessingDraftState`,
  the canonical serializer, `ApplyProcessingExecution` and shared action buttons.
  Removed separate initialization/apply-result storage and repeated execution
  wiring. Explicit Apply retains its Dirty bit; clean drafts adopt external
  updates, rejected drafts remain retryable, and Reload discards edits immediately.
  Successful apply reads back canonical config before execution and visualization.
- Run remains visible but disabled without a suitable mesh, with selection and
  topology reasons distinguished. Show controls require a matching entity.
  Other domain-window controls retain their own availability guards; removed the
  redundant common selection warning. No new helper, file, public surface,
  dependency or geometry validation; explicit source IDs and output buttons remain.
- Claude reviewed the plan and two fixed diffs. Adopted dirty-draft protection,
  immediate Reload, selection messaging and disabled Show findings. Confirmed
  canonical read-back is correct because execution reads the same active config.
- Real-widget regression reproduced stale external output names on the old panel.
  It covers explicit Apply, rejected Apply/Run, retry without another edit,
  NoChange re-execution, external refresh, dirty-draft conflicts, Reload and
  visible-disabled Run. The focused panel suite passed all 55 tests.
- Production footprint: 43 lines removed from one implementation, including its
  redundant GLM umbrella include. No compile-time improvement measurement or
  sanitizer/Vulkan runtime claim. Full numerical readiness and remaining
  action/backend controls keep UI-037 open.
- Final canonical `ci` configure / `IntrinsicTests` build / exclusion-only CPU gate:
  **4,678 passes, one expected ASan-only GLFW skip, zero failures** (4,679 selected,
  141.83 s). A provisional run detected source newer than the dependency scan after
  final review edits; rebuilding and rerunning against fixed source resolved it.
  Layering, test layout, task policy/state links, docs links/sync, skill freshness,
  session brief, root hygiene and diff checks pass. No module inventory change.


## Topology execution reuse checkpoint — 2026-09-16

- Operator-directed processing cleanup: denoise, remesh, subdivide and simplify
  now share four private typed computation functions between direct and queued
  commands in `Runtime.MeshTopologyOperations.Topology.cpp`. Parameter mapping,
  kernel dispatch, counters and failures have one implementation per operation.
  Validation/source capture, UV preparation, stale/cancel guards, history and
  result delivery retain their existing owners. No new production file, public
  surface, dependency, algorithm or framework; 230 production lines removed.
- Claude reviewed the plan, diff and fix. Preserved source-owned deleted-vertex
  counts, exhaustive worker dispatch and subdivision's before/output ownership.
  The proposed empty-failure fallback was unnecessary: every compute failure sets
  a message; the observed empty result came from publication rejecting NoChange.
  An attempted panel import removal was reverted after compilation proved the
  common parameterization action-result declaration still requires it.
- New direct/queued comparisons cover all operation modes, geometry/connectivity,
  diagnostics/counters, NoChange, kernel failure, single delivery and undo/redo.
  These tests found and fixed [BUG-200](../done/BUG-200-queued-denoise-nochange-completion.md):
  queued denoise now publishes valid NoChange with the same explanation as direct
  execution, unchanged geometry and no history entry. Existing failure/stale,
  topology and UV/seam coverage remains in the focused suite.
- The comparison helper reuses `EditorJobHarness` with one worker; its existing
  callers retain their two-worker default. Focused operation suite: 30 passes.
  Final canonical `ci` configure / `IntrinsicTests` build / exclusion-only CPU
  gate: **4,682 passes, one expected ASan-only GLFW skip, zero failures** (4,683
  selected; 143.67 s). Layering, test layout, task policy/state links, docs
  links/sync, skill freshness, root hygiene and diff checks pass.
- UI-037 remains open for remaining action/backend controls and full numerical
  readiness. No module inventory change, compile-time measurement, sanitizer
  execution or Vulkan runtime claim in this slice.


## Denoise/simplify admission reuse — plan

- Operator-directed continuation of processing reuse and compilation-locality work.
  Denoise/simplify command validation is the canonical owner of scene, parameter,
  kernel and entity checks. Move those checks into private typed overloads in the
  existing compiled family owner and share them with typed admission previews.
  Reuse `ValidateMeshSoupSourceMetadata` for cheap source checks, and the existing
  shared action button/tooltip. Preserve each command's diagnostic priority.
- Panels build one request for preview and execution; remove hidden actions,
  simplify's duplicate stop predicate, and the two redundant common-model flags.
  No new file, module, config lane or dependency; no measured compilation claim.
- Preview certifies admission only. Full-buffer finite/connectivity checks and
  deleted-slot feasibility still occur in the source builder; complete cached
  numerical readiness and remesh/subdivide options remain open.


## Denoise/simplify admission reuse — verified checkpoint

- Typed previews and apply share their existing scene/parameter/kernel/entity
  validation, including per-command reason priority and denoise-specific failure
  status. Preview reuses source metadata checks from the mesh builder. Panels
  use the exact previewed command and shared disabled-reason button; missing
  inputs no longer hide the action, and simplify has no app-owned stop predicate.
  Deleted the two common-model availability fields and their derivations.
- Claude reviewed the plan, diff and supporting source. Confirmed editable-surface
  capability means the same provenance/component presence, with no separate
  shared/read-only gate. Every metadata rejection supplies a diagnostic; no
  speculative fallback was added. Ready-default tests cover denoise's existing
  positive epsilon. Source access reads pointers, counts and property handles.
- New runtime tests cover validation priority/reason parity, missing scene/entity,
  kernel/parameter rejection, metadata changes, stale targets, and non-mutating
  admission over non-finite data with fail-closed execution. Actual widgets cover
  visible blocked actions, no submission without an entity or simplify stop
  criterion, then one queued job and terminal delivery for each method.
- Fixed four overlooked legacy model assertions during the first build. Corrected
  the new non-finite test's invented failure-enum expectation: the two existing
  execution paths reject by different routes, so it asserts no success, unchanged
  invalid input and no history entry. Final focused run: 28 passes (1.09 s).
- Canonical `ci` configure / `IntrinsicTests` build / full CPU selector pass:
  **4,685 passes, one expected ASan-only GLFW lifecycle skip, zero failures**
  (4,686 selected; 137.39 s). Layering, test layout, task policy/state links,
  docs links/sync, skill freshness, session brief, root hygiene and diff checks
  pass. Module inventory regenerated unchanged. Source documentation audit has
  zero objective errors; existing broad README/interface review hints are outside
  the changed paragraphs/declarations.
- Scope/layering/tests/docs sweep and automated workshop checks pass. Manual
  workshop rows 1–3 pass; renderer/pass/recipe rows 4–6 are unchanged; row 7 is
  partial task progress; row 8 has no exception. No new module, source file,
  dependency or service. Production C++ is **27 lines larger** overall to expose
  missing previews while removing duplicated model/UI decisions. No compilation
  improvement measurement, sanitizer execution or Vulkan runtime claim.
- UI-037 stays active: remesh/subdivide options, the remaining action/backend
  inventory and full cached numerical/deleted-slot readiness still need work.


## Remesh/subdivide admission and options — plan

- Continue operator-directed cleanup with the existing topology-family preview
  mechanism. Share the command's validation through two more private typed
  overloads; remove ten common-model availability fields and their derivations.
  Run uses one complete request; option probes use valid default requests with
  only the candidate variant, so unrelated draft errors cannot trap editing.
- Options and toggles reuse runtime reasons and the existing tooltip. Probe the
  toggled checkbox value to allow recovery from unsupported enabled features.
  Read mode/operator after widgets; preserve the existing clear-on-leaving-Loop
  feature policy. Adaptive sizing may be preselected while uniform is active.
- Correct the uniform remesher's unrelated error-bounded sizing capability gate:
  only adaptive mode consumes that capability. Keep parameter rules, including
  valid enum/approximation values, and all numerical/kernel/publication checks.
  No new algorithm, config lane, file, module, service or compatibility path.
  Full cached numerical/deleted-slot readiness remains outside this slice.


## Remesh/subdivide admission and options — verified checkpoint

- All four topology methods now share the existing compiled family admission
  pattern. Remesh/subdivide preview and apply reuse their original validation
  order and failures; no copy of command rules lives in the panel. Deleted ten
  shared-model availability flags and their derivations. Main Run validates the
  full request, while option probes remain independent of unrelated draft errors.
- Remesh mode/sizing/projection and subdivision operator/Loop-feature controls
  obtain disabled reasons from runtime and use the existing tooltip. Numeric
  controls stay editable. Read mode/operator after edits and probe the proposed
  checkbox value; unsupported enabled features can be turned off when their
  method is available. Preserve existing clear-on-leaving-Loop semantics.
- Corrected uniform remeshing's unrelated adaptive-sizing capability gate. A
  valid uniform request remains runnable if error-bounded adaptive sizing is
  unavailable; adaptive requests still reject with their canonical reason.
  Sizing can be preselected and is explicitly labeled as adaptive-only.
- Claude reviewed plan, diff, fix and source proof. Adopted current-mode sizing
  probing so the combo and Run agree. The reviewer withdrew invalid-default and
  OFF-rejection concerns after checking actual initializers/predicates; compiler
  verification also disproved an overload ambiguity. No speculative fallback,
  extra default constant, automatic state repair or generic option framework.
- Runtime tests cover every remesh/subdivide capability flag, default candidate
  validity, exact preview/apply rejection reasons, priority, metadata changes,
  missing scene/entity, recovery and the uniform/adaptive guard distinction.
  Real widgets cover all four blocked/queued actions plus adaptive/error-bounded
  projection and Catmull-Clark routing. Logged checked Loop preservation before
  switching proves the cleared result is not just its initial false default.
- Final focused run: 44 passes. Canonical `ci` configure / `IntrinsicTests`
  build / exclusion-only CPU gate: **4,688 passes, one expected ASan-only GLFW
  lifecycle skip, zero failures** (4,689 selected; 141.44 s). Layering, layout,
  task policy/state links, docs links/sync, skills, session brief, root hygiene
  and diff checks pass. Module inventory regenerated unchanged. Source-doc audit
  reports zero objective errors; existing broad review hints remain outside scope.
- Scope/layering/tests/docs and automated workshop sweep pass. Manual rows 1–3
  pass, 4–6 unchanged, 7 records partial progress, 8 has no exception. Production
  C++ shrinks by **109 lines** across five existing files; no new file/module,
  dependency, service or compatibility layer. No compilation-speed measurement
  or sanitizer/Vulkan runtime claim.
- UI-037 remains active for other families/options and full cached numerical,
  topology and deleted-slot readiness. This is admission and control reuse, not
  whole-task Operational closure.


## Progressive Poisson admission — plan

- Continue operator-directed duplication/compile-locality work outside the standing
  convergence selection priority. Replace the common-model Poisson readiness
  fields and hard-coded `v:position` scan with a typed family-owned preview.
- Reuse the command's numeric, scene/entity/domain validation and typed property
  access. Extract property-binding validation in the existing compiled config
  codec owner for parser, preview and apply; avoid a JSON roundtrip in admission.
  Preserve codec numeric fallback, method parameter rules and backend fallback.
- Keep input controls visible so missing default positions can be replaced by a
  compatible custom binding. Manual and debounced runs use the same current
  request and preview. Full finite validation remains at execution; generation-
  cached numerical readiness is still an open UI-037 follow-up.
- No new module, file, service, wrapper or compatibility path. Existing config,
  queue/publication/history and disabled-tooltip owners remain authoritative.
  Verify runtime rejection parity, custom domains and real chooser/run behavior;
  review the fixed diff with Claude and run focused plus full CPU checks.


## Progressive Poisson admission and draft reuse — verified checkpoint

- Preview and apply now share scene, method-parameter, typed binding,
  entity/domain and property-metadata admission in the existing point-set owner.
  Config parsing and typed commands share one compiled binding predicate.
  Removed the common-model availability/reason fields, default `v:position`
  provenance switch and full position-buffer scan (73 model-builder lines).
  Numerical validation still fails closed before any execution or submission.
- Input controls stay visible without a selection or default position property.
  Run and auto-run use the same current request and runtime readiness. Custom
  compatible property domains remain selectable; backend fallback, publication,
  history, config retry and terminal callback paths are preserved.
- Claude reviewed the plan, fixed diff and supporting definitions. Numeric codec
  warnings remain usable fallbacks; typed command rules still permit method-side
  clamping. The reviewer withdrew speculative null/lifetime/unused-helper
  blockers after checking the canonical resolver, const capture and remaining
  caller. No redundant defensive paths were added.
- The actual widget regression exposed a second outer window gate hiding controls
  without selection; removed it. Claude's final pass identified rejected drafts
  being overwritten each frame. Reused `ProcessingDraftState` and removed the
  panel's duplicate binding/result/visualization storage. Rejected edits survive
  retry; accepted external config changes refresh the widgets. Claude accepted
  this fix. Immediate Pending display plus one queued terminal callback is the
  existing API contract; the manual/debounced test covers failed auto-run without
  recurring retries.
- Runtime coverage verifies preview/apply rejection-reason parity, missing scene,
  stale entity, invalid parameters/bindings, type/cardinality/empty sources,
  deferred finite checks, retained config fallback and direct/queued face inputs.
  Actual widgets verify visible blocked actions, missing default positions,
  rejected custom-property selection and successful retry without re-editing.
  The first build's missing method-owner imports in two test files were fixed.
  Final focused runs: 32 runtime/source-presentation cases and three UI cases
  (34 distinct tests), all pass.
- Canonical `ci` configure / `IntrinsicTests` build / full exclusion-only CPU gate:
  **4,690 passes, one expected ASan-only GLFW lifecycle skip, zero failures**
  (4,691 selected; 151.53 s). An earlier full run was stopped to fix the reviewed
  draft defect; only the complete final run is verification evidence. Layering,
  test layout, task policy/state links, docs links/sync, skill freshness, session
  brief, root hygiene and diff checks pass. Inventory regenerated unchanged;
  source-doc audit reports zero objective errors and 145 existing broad hints.
- Scope/layering/tests/docs and workshop sweep pass. Manual rows 1–3 pass, 4–6
  unchanged, 7 records partial task progress, 8 has no exception. Production C++
  shrinks by **37 lines** across seven existing files. No new source file/module,
  dependency, service or compatibility layer; no measured compile-time speedup
  or sanitizer/Vulkan runtime claim.
- UI-037 stays active. Remaining candidates include curvature/segmentation
  admission, unused shared-model normal/direction flags, other family/backend
  options and generation-cached full numerical/deleted-slot readiness.


## Curvature/segmentation admission and common-model cleanup — plan

- Continue operator-directed duplication/compilation cleanup using the existing
  mesh-field, mesh-source and config owners. Share scene/kernel/config/entity
  admission through private typed overloads and expose two family previews.
  Curvature config parsing and typed execution share binding validation without
  a JSON roundtrip in preview; retain separate invalid output-mode diagnostics.
- Reuse `ValidateMeshSoupSourceMetadata`; extract segmentation's face/edge
  presence and endpoint-count checks for reconstruction and admission. These
  metadata checks precede execution-time numerical/topology checks. Preserve
  front-end reason priority, scalar direction fallback and publication/history.
- Reuse the existing mesh reconstruction result for curvature instead of a
  specialized copy that only renames fields. Keep curvature-specific diagnostics;
  capture the vertex count before moving source positions into a queued job.
- Panels preview the current request. Remove both method availability flags and
  four unused normal/direction flags from the common model. Keep the underlying
  runtime direction capability and actual normal-estimation preflight unchanged.
- No new file, module, service, dependency or compatibility path. Verify runtime
  rejection parity, typed/config binding agreement, metadata failures, custom
  inputs and real blocked/recovered widgets with Claude and the CPU gate. Full
  numerical/deleted-slot/output-conflict readiness remains follow-up work.


## Curvature/segmentation admission and common-model cleanup — verified checkpoint

- Added family-owned metadata previews for curvature and segmentation. Private
  typed command resolvers now share scene/kernel/config/entity checks with Apply,
  using the existing mesh-source metadata validator. Segmentation reconstruction
  and admission share face/edge presence and endpoint-count validation. Curvature
  config parsing and typed commands share binding rules without JSON in preview.
- Deleted all six remaining method-specific availability flags and their model
  derivations, including four unused normal/direction flags. Updated consumers to
  use canonical previews; actual normal-estimation validation and runtime direction
  capability remain unchanged. Scalar-only fallback retains explicit result and
  output-property coverage. Panels preview the current input/output configuration.
- Removed the specialized curvature reconstruction record that copied/renamed an
  existing mesh-source result. Direct and queued execution now use that existing
  record; curvature diagnostics remain explicit and vertex count is captured before
  moving positions into the job. Segmentation retains its distinct face/edge maps.
- Claude reviewed plan, fixed diff and source proof. Fixed its valid empty-selection
  diagnostic finding in runtime, also caught by the unchanged real-widget test.
  Preserved vertex-count diagnostics on early curvature metadata rejection. Exact
  Encode and existing fallback tests disproved proposed serialization/coverage
  defects; no extra guards, enum facade or duplicate panel predicates were added.
  Final review has no blocking findings.
- Runtime regressions cover failure priority/reason parity, missing scene/entity,
  invalid config/output enums, typed/serialized binding agreement, missing/custom
  positions, corrupt halfedge/edge counts, source removal, deferred finite checks
  and scalar fallback. Real widgets cover both visible blocked actions and recovery
  by choosing a custom property; existing config retry/draft and queued curvature
  cases pass. Fixed a new test's nonexistent segmentation result-slot assumption:
  inline segmentation is verified through published properties, without adding a
  result channel. Final focused run: **27 passes** (1.47 s).
- Canonical `ci` configure / `IntrinsicTests` build / exclusion-only full CPU gate:
  **4,693 passes, one expected ASan-only GLFW lifecycle skip, zero failures**
  (4,694 selected; 144.04 s). Layering, test layout, task policy/state links,
  docs links/sync, skills, session brief, root hygiene and diff checks pass.
  Module inventory regenerated unchanged. Source-doc audit reports zero objective
  errors and 144 existing broad hints outside the touched declarations/paragraphs.
- Scope/layering/tests/docs and automated workshop sweep pass. Manual rows 1–3
  pass, 4–6 unchanged, 7 remains partial readiness coverage, 8 has no exception.
  Production C++ is **six lines larger** overall across seven existing files:
  new admission entry points replace duplicate state and reconstruction records.
  No new production file/module, dependency, service or compatibility path. No measured
  compilation-speed improvement or sanitizer/Vulkan execution claim; the changed
  GPU-smoke assertion was compiled as part of the full target.
- UI-037 remains active for other families (including K-Means), backend/variant
  controls, bake readiness and cached numerical/topology/deleted-slot/publication
  conflict checks. All method-specific availability flags are now absent from the
  common processing model; this does not close whole-task numerical readiness.


## K-Means admission and shared-model cleanup — implementation plan

- Operator explicitly continues duplication/compile-locality cleanup with Claude,
  outside the standing convergence selection preference. Reuse the existing
  clustering Types implementation, service operation wrapper, property catalog,
  config codec and shared action/tooltip presentation; add no file or service.
- Move metadata admission from the clustering snapshot builder into one compiled
  validator consumed by service execution and editor preview/submission. Return
  an optional existing typed rejection record; nullopt admits. Scene/entity,
  parameter/binding, property kind/count and output conflicts are metadata checks.
  Keep finite scans, snapshot capture, asynchronous publication/history and
  staleness validation in execution, preserving non-finite failure status.
- Config and runtime share the binding predicate. ReadPropertyRef already checks
  canonical value kinds; preserve all known config domains while execution keeps
  the existing three-domain limit. RUNTIME-211/UI-043 still own generalization.
- Remove the common model's KMeansDomains vector, its producer/helper and the
  app conversion switch. Use catalog rows for default binding and keep entity,
  input/output controls and the disabled action visible even without a selection.
  Preview the exact draft; preserve explicit Apply/Reload and CPU/Vulkan fallback.
- Claude plan review identified finite-status and correlation preservation risks.
  Its proposed null-scene admission and value-kind compatibility objections do
  not match the existing contract: absent scenes reject, and ReadPropertyRef
  already rejects kind changes. Prove those paths in focused tests and review.


## K-Means admission and shared-model cleanup — verification checkpoint

- Implementation commit: `a174b4f8d`. On resuming, the operator's checkout
  already contained that commit on main and origin/main. A requested fast-forward
  pull then brought main to `5cf2dcd5c`; the additional commit changes backlog
  documents only. Verification below uses that checkout's identical C++ source.
  This checkpoint is recorded in a separate worktree to preserve the clean main
  checkout and the one-writer rule.
- The existing clustering Types implementation owns metadata admission reused
  by runtime snapshot capture and editor preview/submission. Rejections preserve
  typed status, backend and request identity; queued failures retain their world
  and correlation, while immediate editor rejection queues/publishes nothing.
  Finite scans, exact output capture, source staleness, publication and history
  stay in execution. The finite-input rejection status is preserved.
- Config parsing and admission share typed binding rules. All eight known config
  domains remain accepted; the current three execution domains are unchanged.
  RUNTIME-211/UI-043 now describe extending the existing validator and catalog
  controls instead of adding another readiness path.
- Removed KMeansDomains, its shared-model producer, its helper and the app domain
  conversion switch. Shared processing models contain no per-method availability
  flags or domain inventories. K-Means keeps entity/input/output controls and Run
  visible, uses catalog defaults, and previews the exact draft with the shared
  disabled-action tooltip. Explicit Apply/Reload, config rejection/retry, output
  Show actions and requested/actual backend reporting remain intact.
- Claude reviewed the plan, fixed diff and exact supporting source. Addressed its
  finite-status/correlation concerns. Source proof resolved its speculative
  import-cycle, invalid-ID, provenance and config-kind objections without new
  facades or duplicate guards; its final review has no blockers.
- Focused verification: **30 passes**, zero failures (1.82 s). Runtime tests cover
  rejection priority, custom/missing/count-mismatched input, output-kind conflict,
  non-finite execution, event identity and detached service frames. Config tests
  cover eight domains and nine malformed bindings. Real ImGui coverage proves
  visible blocked controls, property selection, rejected-draft retry and one
  successful queued run. Existing history/fallback/Show tests also pass.
- Fixed two remaining test references to the deleted domain list. The new widget
  test initially assumed the catalog would prefer its custom input over an
  existing v:point property; it now explicitly selects the custom row. A Clang
  lexer crash during an overlapping test-file correction did not recur with
  stabilized sources and CCACHE_DISABLE=1. No speculative source workaround was
  added, and this does not establish a cache-defect diagnosis.
- Production C++ is **69 lines smaller** across ten existing files. No new
  production file/module, compatibility path or exported dependency edge.
  The existing implementation owner now performs admission; this is structural
  cleanup, not a measured compilation-speed claim.
- UI-037 remains active for other families, backend/variant controls, bake
  readiness and cached numerical/topology/deleted-slot/publication checks.
  RUNTIME-211/UI-043 retain the broader K-Means domain integration. No task is
  retired by this bounded checkpoint.

- Canonical ci configure and IntrinsicTests build passed. The full exclusion-only
  CPU gate selected **4,697 tests: 4,696 passes, one expected ASan-only GLFW
  lifecycle skip, zero failures** (148.96 s). This run did not execute sanitizer
  or Vulkan/GPU tests. The changed common interface and affected GPU-smoke
  consumers were compiled by the full target.
- Layering, test layout, task policy/state links, docs links/sync, skills,
  session-brief freshness, root hygiene and diff checks pass. Inventory refreshed
  unchanged at 419 modules. The touched source-doc audit reports zero objective
  errors and 145 review hints; declaration lifetime/admission comments are
  intentional, with existing broad README debt left scoped to its owners.
- Scope/layering/tests/docs and automated workshop sweep pass. Manual rows 1–3
  pass, 4–6 are unchanged, 7 remains partial readiness coverage, 8 has no exception.
  Verification logs and fixed Claude packets are in
  `/tmp/intrinsic-kmeans-admission/` on the verification host.


## Property comparison reuse and compile locality — 2026-09-17

Operator explicitly continues duplicate-code and compilation cleanup with Claude,
outside the standing convergence work-selection preference. Baseline: `5cf2dcd5c`
with a clean checkout. This supporting slice leaves readiness acceptance open.

- Owner search found `SameTypedPropertyValues` has one caller, in the compiled
  mesh-support owner. Move its unchanged template into that source's anonymous
  namespace and remove the resulting six unused comparator includes. Keep the
  shared header's actually shared templates and declarations.
- Normals history and parameterization UV/source snapshots duplicate the existing
  `GeometryValueComparison::BitEqual` component-bit checks. Reuse that owner;
  retain sequence cardinality checks, NaN payloads and signed-zero distinctions.
  Keep `SameGeometryPositions` numeric equality separate: its contract differs.
- No new file, public module, dependency edge, tuning surface or compatibility
  wrapper. Existing scalar/vector bit-comparison and history/staleness regressions
  cover the unchanged behavior. Ten production files have a net reduction of
  18 lines, including the relocated helper. This is no measured compile-speed claim.
- Verification: canonical `ci` configure and `IntrinsicTests` build pass with
  Clang 23; focused comparator/history/locality run passes all 66 cases. Full CPU
  gate passes 4,696 tests with one expected ASan-only GLFW lifecycle skip
  (4,697 selected, 159.63 s). Layering, test layout, task policy/state links,
  session brief, docs sync over explicit changed files, doc links, root hygiene
  and diff checks pass. No sanitizer or Vulkan execution in this slice.
- Claude reviewed the fixed source and found no blocking defects. Existing helper
  tests pin signed zero/NaN payloads; normal history tests cover preserved NaNs.
  No public surface changed, so no inventory regeneration was required.


## Registration compilation locality — 2026-09-17

- Second operator-directed iteration, suggested by Claude and verified against
  source: registration used only three additional family-neutral mesh-support
  helpers, yet included all by-value mesh snapshots and two full mesh modules.
- Move those declarations to the existing `PointFields.hpp` block; definitions
  remain compiled once in `MeshSupport.cpp`. Registration now includes the narrow
  header, with its two used entity/status aliases explicit in the source.
  Four existing narrow-header consumers gain the leaf `Core.Error` import for
  the moved error-result declaration. No new module or helper body is introduced.
- Extend `ProcessingCompilationLocality.Registration` to forbid `Geometry.HalfedgeMesh`
  and `Geometry.MeshSoup`. The exact check rejected both imports before the edit.
  Existing registration semantics, numeric snapshot comparison and result mapping
  remain unchanged. Update the architecture paragraph and canonical reuse route.
- Verification: canonical `ci` configure and `IntrinsicTests` build pass;
  all 70 focused registration/comparison/history/locality cases pass. Combined
  full CPU gate: 4,696 passes and one expected ASan-only lifecycle skip
  (4,697 selected, 153.66 s). The new registration compiler-closure guard passes.
  Layering, test layout, task policy/state links, session brief, skill mirrors,
  explicit-file docs sync, doc links, diff and automated workshop checks pass.
  Module inventory regenerated unchanged. Manual workshop rows 1–3 pass;
  renderer/recipe/maturity rows are unchanged and no exception is introduced.
- Seven production C++ files are one line larger overall: four explicit leaf
  imports replace two heavy imports and one unused dirty-tags import, and aliases become explicit at their user.
  Shared declarations moved without extra definitions or wrappers. No measured
  compile-speed, sanitizer or Vulkan-execution claim; UI-037 remains active.
- Claude found no blocking issues in the second fixed-source review. Its valid
  unused dirty-tags import finding was removed from registration; the transform
  component still owns the dirty tag this operation actually uses. The requested
  post-edit verification note was already recorded while review was running.
- After removing that final unused import, rebuilt `IntrinsicTests`, reran all
  72 focused cases (including registration config), and repeated strict layering;
  all pass. The full CPU run above preceded this import-only review correction.


## Point-catalog metadata reuse and mesh-soup locality — 2026-09-17

Operator continues the duplication/compilation cleanup with Claude from clean
`e18d006d7`, explicitly outside the standing convergence selection preference.

- Share generation folding, vec3 candidate filtering and per-property revisions
  in `BuildPointInputCandidateCatalog`, compiled in the existing point-property
  owner. Generic discovery, density, spacing and bilateral retain distinct
  numerical, deletion and sample-count filters. No new service, policy flags or
  lifecycle template. Five production files shrink by nine lines overall.
- Add public-entry regression coverage across all eight domains for source versus
  property revision invalidation, read-only query behavior and the deliberate
  one-sample generic versus two-sample spacing/density eligibility distinction.
  Existing family tests retain output-name collisions and deleted-slot coverage.
- Isolate the triangle-soup result/builder declarations in one private header for
  their two current consumers: reconstruction and UV generation. Their definitions
  stay in the existing compiled owner. Remove five unused direct mesh-soup imports;
  parameterization still legitimately reaches it through its UV API.
- Strengthen compiler-closure guards for discovery, mesh fields and mesh topology.
  The pre-edit probe rejected the three inspected curvature/geodesics/topology
  producers for reaching `Geometry.MeshSoup`; post-build guards must pass.
  No public API or algorithm changes, and no measured compilation-speed claim.
- The first soup-locality run found an additional unused `Geometry.Mesh.Conversion`
  import in topology. Its re-export retained the forbidden soup dependency despite
  removing the direct import. Removed it after confirming no conversion caller;
  the compiler guard remains strict. Other 255 focused cases passed that run.
- Claude's independent plan identified the discovery unit's mesh-support include
  as serving only its geometry-source namespace alias. Use an explicit local
  alias and remove the helper includes, nine unused module imports and unused
  standard/GLM includes. Keep processing access and its real config/selection
  dependencies. Extend discovery's guard to halfedge mesh, spatial cache and
  transform components; the before probe rejected all three.
- Claude found no catalog blockers. Add bilateral revision coverage in its own
  existing test file, preserving test-family compilation boundaries. The suggested
  attachment-check concern is already guarded by command-handle resolution, and
  the requested shared-catalog docs were updated while review was running.
- Claude also identified topology's unused `MeshSurfaceTopology` import as a
  second indirect soup dependency. Confirmed the exact compiler trail, removed
  that import and kept the guard unchanged. Added the new private soup header
  to the existing no-duplicate-property-vocabulary scan.
- Final canonical `ci` configure and `IntrinsicTests` build pass. All 302 focused
  catalog, mesh/UV/history, vocabulary and compilation-locality tests pass,
  including the new bilateral revision case and all strengthened guards.
  Layering, test layout, task policy/state links, docs sync, strict doc links,
  skill mirrors, session brief, root hygiene and workshop checks pass. Module
  inventory regenerated unchanged. Manual workshop rows 1–3 pass; other rows
  are unchanged with no new exception or maturity closure.
- Full production accounting, including the new private header and CMake entry:
  15 files, 11,948 to 11,929 physical lines (19 fewer). The declaration split is
  justified by two real soup consumers and removes unrelated compiler dependencies;
  it is not counted as duplicate implementation removal. Shared catalog metadata
  replaces four copies while preserving distinct admission rules. UI-037 stays open.
- Final fixed-source Claude review found no blockers after the two topology
  import corrections. A separate immutable-diff review found no verified defect;
  its context-limited reachability questions are resolved by the source review,
  complete build and strengthened compiler-closure tests. Harmless formatting/
  include nits were left outside this verified source revision.
- Full exclusion-only CPU gate on the final combined source: 4,699 passes,
  one expected ASan-only GLFW lifecycle skip, zero failures (4,700 selected,
  148.60 s). No sanitizer or Vulkan execution, or compile-time benchmark, is
  claimed. Existing readiness and full-buffer-cache acceptance remains open.

## Private helper ownership and transform locality — plan, 2026-09-17

The operator continues to direct duplicate-code and compilation-dependency
cleanup with Claude. Isolate the two shared transform-mutation declarations
from the common entity/signature header, retaining their existing compiled
definitions and C++ linkage. Only registration and scene actions consume them;
18 other implementation units import the transform module solely for the
shared declaration surface. Preserve real transform dependencies in registration,
construction, scene actions, workspace model assembly and the definition owner.
Pin the removed family dependencies with compiler-closure tests.

Move the single-caller changed-value template into curvature's implementation
and the positive-finite predicate into topology's implementation. Replace UV
view token byte mixing with the existing editor signature helper: identical
offset, prime, eight-byte order and float bit patterns; the distinct diagnostic
fingerprint stays separate. No new public API or behavior is intended. Existing
family, undo/redo and UV-token contracts plus the full CPU gate cover the slice.

Reuse decision: keep compiled transform/signature definitions in
`Runtime.EditorFeatureContextAdapters.cpp`; a declaration-only transform header
is necessary because the existing command header would expose unrelated
render-hint/file types to registration. No new implementation owner or wrapper.
Claude's separate candidate to unify mesh-builder diagnostics remains deferred
to a semantic slice. No compilation-time speedup is claimed without a comparison.

### Implementation and review checkpoint

The two transform declarations now live in `Runtime.EditorTransformHelpers.hpp`,
included by registration, scene actions and the existing definition owner. All
18 unused direct imports are removed. A before/after check of the configured
compiler records rejected all 18 producers before the edit and passes all 18
after it; eight existing processing-family guards now forbid the transform
module. Registration/construction/scene edits retain their real dependencies.

`CountChangedValues` and `IsPositiveFinite` now have implementation-local
anonymous-namespace ownership in curvature and topology respectively. UV view
request tokens reuse `MixSignature` and `kEditorSignatureOffset`; both mixers
use the same eight little-endian bytes, XOR-then-multiply prime and offset.
Token field order and float bit casts are unchanged. The existing semantic
token test also checks that signed zero changes the request and resubmitting
the original restores its token. Tokens remain transient renderer request/cache
identity, with no persisted-format or cross-process contract.

Claude reviewed the plan and fixed diff: no verified blocking findings. Review
uncertainties about constant visibility, include completeness, remaining helper
callers, transitive imports and fixture behavior are closed by the full build,
the compiler-closure tests and the focused token test. No review-driven source
changes were required. The distinct diagnostic/sandbox hash algorithms and
polygon-preserving normal reconstruction remain separate.

Production delta, including the new declaration-only header: 25 source/build
files, 23,435 to 23,424 lines (11 removed). The fan-out is mostly one removed
import per consumer; there is no new implementation or public module.
Architecture/workshop rows 1–3 pass: fewer imports, unchanged target links and
public type ownership. Rows 4–6 are unaffected (no renderer state/pass/recipe
changes); whole-task retirement and exceptions do not apply (rows 7–8). Source
documentation audit: zero objective errors; seven existing review hints in the
shared mesh header. Module inventory remains unchanged at 419 modules.

Canonical `ci` configure and `IntrinsicTests` build pass with Clang 23. The
focused processing/editor boundaries, parameterization, registration, mesh-field,
point-field and runtime-layering selection passes 131 tests. Strict layering,
test layout, task policy/state links, docs sync/links, skill mirrors, root hygiene,
clean-workshop automation, session-brief freshness and diff checks pass.
Local logs and fixed Claude packet: `/tmp/intrinsic-third-*`. This continues
UI-037's operator-directed support work; its broad readiness acceptance stays open.

Final full CPU verification: **4,699 passed plus one expected ASan-only GLFW
skip**, zero failures (4,700 selected, 149.83 s), using
`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60`.
Sanitizer and GPU/Vulkan execution were not repeated for this private-helper
refactor. Compilation dependency reduction is verified; build-time speedup is
not measured.

## Shared mesh preparation and diagnostic locality — plan, 2026-09-17

The operator continues to direct duplication/compile-locality work with Claude.
Use one operation-neutral mesh preparation function and result declaration in
`MeshSources.hpp`, beside the existing source snapshots. Geodesics and
parameterization can then stop including `MeshSupport.hpp` and its point/job
helper declarations. Keep the ordinary compiled implementation owner and the
separate polygon-preserving normal builder. No public modules are added.

Consolidate the identical domain/position-presence checks privately in that
compiled owner; retain their order relative to position snapshots, deletion-mask
checks, topology metadata, soup construction and conversion. A full metadata
check before the snapshots would change failure counters and precedence, so do
not substitute that larger validator. Prefix every preparation failure with the
calling operation and report the actual bound position property. Remove the
curvature rewriting wrapper and dead fallback wording; topology previews use
the same source-defect text as execution. Keep the compact topology result
projection: it drops unused position/deletion/source-face buffers before the
whole-mesh kernels and queued preparation, so widening it would extend those
buffers' lifetimes. This narrows Claude's broader deletion suggestion.

Two new public-operation regressions fail on the baseline exactly as intended:
geodesics errors name denoise and `v:position` for a missing/malformed `v:rest`
source; parameterization conversion errors name denoise. Their status, output
retention and history assertions already pass. Add preview/apply equality and
malformed-mask coverage, retain stale-job/undo/topology tests, then run the full
CPU gate. No numerical method or deletion/polygon interpretation changes.

Initial focused verification passed all behavior tests and exposed an overbroad
new dependency assertion: UV regeneration legitimately calls
`Geometry::Mesh::Conversion::ToHalfedgeMesh` when applying its generated soup.
The new `ProcessingCompilationLocality.ParameterizationSolve` guard therefore
checks the solver interface/implementation only; the existing family guard
remains unchanged. This preserves the real conversion owner and verifies the
removed unused solver dependency without adding a forwarding helper.

### Verified implementation and review checkpoint

`BuildHalfedgeMeshForProcessing`/`MeshProcessingSourceResult` now own the shared
preparation contract. A private domain/position gate replaces two copies in the
compiled owner. The curvature error-rewriting wrapper is removed; the topology
wrapper only projects the compact result. Error status/code, source-buffer
counts, mask-before-topology precedence, triangulation, source-face mapping and
separate normal reconstruction are retained. Missing position and malformed
mask errors identify the actual bound property. Topology metadata failures have
identical preview/apply messages for denoise, simplify, remesh and subdivide.

Source result, builder, position extraction and stored-topology signature
declarations live in `MeshSources.hpp`. Geodesics still needs the signature for
undo validation, a use missed by the initial planning suggestion; it is included
in the narrower surface. Geodesics/parameterization no longer parse the broad
mesh-publication and point/job helper header and remove nine unused imports.
Job types remain reachable through the shared processing context where required.
The new solver boundary proves that its conversion dependency is gone; UV
regeneration keeps its real conversion call. No new implementation file, public
module, alias or forwarding API was introduced.

Claude reviewed the plan, fixed production diff and final boundary correction:
no verified functional regression or remaining blocker. Build and focused tests
closed the review's include, linkage, prefix-equality, mask-order and existing
message-test questions. The two new diagnostic regressions first failed on the
unchanged source; after the fix all **337 focused tests pass**. The third new
behavior test covers combined malformed mask/topology, preserved snapshot
counts, unchanged positions and empty history. Existing queued/stale/undo and
normal tests pass. Canonical `ci` configure and `IntrinsicTests` build pass with
Clang 23. Logs and immutable Claude packets: `/tmp/intrinsic-fourth-*`.

Production delta: eight source/header files, 8,653 to 8,587 lines (66 removed).
Architecture/workshop rows 1–3 pass: fewer imports, unchanged target links, no
public ownership change. Rows 4–6 are unaffected; whole-task retirement and
exceptions do not apply (7–8). Strict layering, test layout, task policy/state
links, docs sync/links, skill mirrors, root hygiene, clean-workshop automation,
session-brief freshness and diff checks pass. Source-documentation audit has
zero objective errors and seven retained review hints; the 419-module inventory
is unchanged. UI-037 remains open for broader readiness/cache acceptance.
Compilation speedup is not claimed without a matched timing comparison.

Final CPU gate: **4,703 passed plus one expected ASan-only GLFW skip**, zero
failures (4,704 selected, 164.90 s), using
`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60`.
Sanitizer and GPU/Vulkan execution were not repeated for this runtime
preparation/diagnostic refactor. Final source was rebuilt after review comments;
no production logic changed after the passing focused run.

## Exact buffer comparison and point-analysis locality — plan, 2026-09-17

The operator continues to direct duplication/compile-locality work with Claude.
Consolidate repeated exact-value buffer loops in the existing
`Runtime.GeometryValueComparison.hpp` owner. The present consumers all pass
matching `std::vector<T>` storage, including packed Booleans; use that narrow
overload instead of the proposed unrestricted two-range template, which could
convert one property's value type to another. No generic predicate, new module
or implementation file is needed. Keep element-wise component comparison,
length checks, NaN payloads and signed zero. Preserve each caller's property
existence, provenance, state flags and unsupported-type policy.

Replace loops/wrappers in parameterization, normals, mesh snapshots, clustering
and consolidation. Numeric `SameGeometryPositions` and the Poisson connectivity
comparators retain their different contracts. Extend the existing helper tests
for empty/mismatched buffers, float/double/vector bits, packed Boolean storage and
wide integer values; rerun method history and stale-publication tests.

Independent source inspection found unused `Geometry.HalfedgeMesh` imports in
density weights, keypoints, outliers and descriptors. Compiler-closure checks
currently reject all four producers against a proposed no-full-mesh contract.
Remove those edges and extend the existing point-analysis boundary to forbid
full halfedge mesh and mesh soup. The family operates on resolved properties;
mesh-domain eligibility and point-cloud-only destructive operations stay intact.
Claude agrees the repeated comparison loops are a bounded reuse candidate; the
additional dependency cut is based on the recorded compiler evidence. No
compile-time speedup is claimed without matched measurements.

### Implementation and verification checkpoint

The existing private `BitEqual` owner now compares matching typed buffers;
parameterization, normals, mesh snapshots, clustering and consolidation reuse
it. Packed Boolean values are materialized before scalar comparison. Presence,
unknown-type, state-flag and topology policies stay with the callers. The numeric
mesh-position predicate and connectivity-specific comparisons are unchanged.
Ten production files lose 105 net lines. No target, public module, dependency
exception or compatibility path was added.

Density weights, keypoints, outliers and descriptors no longer import the full
halfedge mesh. The strengthened `ProcessingCompilationLocality.PointAnalysis`
check passes across all ten family/shared producers, forbidding both full mesh
and mesh soup. The four-producer before check failed on the removed imports;
this is compiler-dependency evidence, not a compile-time measurement.

Canonical `ci` configure and the runtime contract build pass with Clang 23.
All **487 focused tests pass**, covering exact buffer edge cases, operation
history/stale guards, point analysis and processing boundaries. The three added
helper cases cover empty/unequal lengths, float/double/GLM NaN payloads and signed
zero, packed Boolean rows and integer values that cannot survive conversion
through floating-point storage. Existing deleted-row normal history coverage
also passes. Logs and immutable review packet: `/tmp/intrinsic-fifth-*`.

The architecture entry and reuse route identify the common owner and its
contract limits. The source-documentation audit has zero errors or review hints;
the 419-module inventory is unchanged. Architecture/workshop rows 1–3 pass:
unchanged layer/target ownership and fewer implementation imports. Rows 4–6
are unaffected; whole-task retirement and exceptions do not apply (7–8).
UI-037 remains open for its broader readiness/cache acceptance. No research
claim or compilation-speed claim is made by this refactor.

Claude's immutable-diff review reports no verified blocking defects. Successful
compilation resolves its buffer-type and overload-visibility uncertainties; the
passing ten-producer boundary resolves its import-closure/coverage questions.
Suggested header removals do not apply: normals still uses span, count and
min/max, and parameterization still uses spans. The helper stays scoped to the
present scalar/vector storage types; no heterogeneous range API, new predicate
parameter or speculative heavy-element specialization was introduced.

Final verification: `IntrinsicTests` builds and the full CPU exclusion gate
passes **4,706 tests plus one expected ASan-only GLFW skip** (4,707 selected,
zero failures, 156.75 s). Sanitizer and GPU/Vulkan execution were not repeated
for this comparison/dependency refactor. Strict layering, test layout, task
policy/state links, docs sync/links, skill mirrors, root hygiene, clean-workshop
automation, session-brief freshness and diff checks pass. The touched-scope
planner correctly selects broad feedback for the private header; the canonical
full CPU run supplies the broader evidence here.

## Shared finite-position capture — plan, 2026-09-17

The operator continues to direct duplication and compilation-locality work with
Claude. GEOM-073 discovery confirms that Cloud adapters have distinct slot and
scale semantics; no broad utility rewrite is included here. The concrete reuse
candidate is the byte-for-byte equivalent finite-position capture in clustering
and mesh processing: both require a nonempty vec3 property matching domain size,
reject any non-finite row and copy every row without interpreting deletion masks.

Reuse `CollectFiniteGeometryPositions` through a narrow private declaration
header, move its implementation and finite predicate from the mesh-support
translation unit into the existing point-property owner, and delete clustering's
copy. No new module or implementation file, public API, alias or wrapper is
needed. Keep existing namespace/function identity to limit caller churn. Compiler
closure currently confirms both clustering and point properties exclude full
halfedge mesh and soup; preserve that boundary while sharing capture. Add direct
contract cases for storage rejection, row order, retained deleted rows, and owned
snapshot independence, then verify existing clustering/history/mesh behavior.

### Implementation checkpoint

Clustering's three capture sites now reuse the original compiled function through
`Runtime.GeometryPositionCapture.hpp`. The original body and function identity
are unchanged; the ordinary `PointProperties.cpp` owns the definition and finite
predicate. Existing mesh/UV callers keep the same declaration through their
point-field include. The new declaration header is justified by a current
clustering caller that must not parse editor job/context records. No public
module surface or target edge changes. Including the new header, production
source shrinks by 12 lines; the boundary adds six CMake test-registration lines.

Three new contract cases verify missing/wrong-kind/empty/mis-sized rejection,
all component NaN/infinity rejection (including deleted rows), retained row order
and subnormal inputs, and snapshot ownership after source mutation. All **366
focused tests pass**, including clustering/history, mesh/UV/parameterization and
compiler boundaries. Canonical Clang 23 `ci` configure/build passes; an unused
variable warning in the test was fixed and the focused build rerun cleanly.
The two-producer PositionCapture boundary passes before and after reuse: it
proves the shared owner and clustering still exclude full mesh/soup modules,
not that a new dependency edge was removed. No timing improvement is claimed.

Claude's GEOM-073 discovery recommends a broader Gaussian-noise kernel but its
proposed adapter switch changes submesh-view handling. That proposal is not
implemented: a later GEOM-073 slice must preserve view offsets, absolute seed
indices, deletion semantics and adapter-specific scale resolution. No geometry
method or compatibility commitment changes in this slice.

Architecture/workshop rows 1–3 pass: unchanged ownership/targets, one compiled
capture and a private declaration surface. Rows 4–6 are unaffected; task
retirement and exceptions do not apply (7–8). Source-documentation has zero
objective errors; retained hints cover correctness and include-order comments.
The generated 419-module inventory is unchanged. Logs/review packets live at
`/tmp/intrinsic-sixth-*`. UI-037 remains open for readiness/cache acceptance.

Claude reviewed the fixed production/test diff and identified the already-fixed
unused test variable plus one contract question. `PropertyRegistry::Storage<T>`
returns null on a type-ID mismatch; `PropertyRegistry::Get<T>` returns nullopt and
`PropertySet::Get<T>` returns an invalid handle. Thus wrong-kind rejection is an
existing contract, including assertions-enabled builds. The new tests and the
successful compile/link close the review's header, linkage and caller questions.

Final verification: `IntrinsicTests` builds without warnings. The full CPU
exclusion gate passes **4,710 tests plus one expected ASan-only GLFW skip**
(4,711 selected, zero failures, 165.12 s). Claude's follow-up review confirms
no remaining blockers after the warning fix and property-type evidence.
Strict layering, test layout, task policy/state links, documentation sync/links,
skill mirrors, root hygiene, clean-workshop automation, session-brief freshness
and diff checks pass. Sanitizer and GPU/Vulkan execution were not repeated for
this unchanged capture implementation. Compilation speed remains unmeasured.


## Point deletion-domain mapping reuse — plan, 2026-09-17

The operator continues duplicate-code and compilation-locality cleanup with
Claude, outside the standing convergence selection preference, from clean
`0feadc835`. Five processing captures independently map point/property domains
to deletion storage. Share only that exact mapping in the existing compiled
point-property owner: vertex/node/cloud rows use `v:deleted`, faces use
`f:deleted`, edges use `e:deleted`, and halfedges use their edge domain with
a divisor of two. The private record has three ordinary fields and five
present callers; no policy flags, lifecycle template, module or file is needed.

Preserve each caller's validation order, diagnostic text, missing-mask policy,
revision watches, live-row filtering and readiness allocation behavior. Normal
capture continues owning its copied masks; the other families borrow properties.
Extend public-entry rejection coverage for malformed masks and halfedge storage,
then run focused processing/history tests and the canonical CPU gate.

### Mapping implementation checkpoint

The five captures now use one compiled mapping; all validation and storage
handling stays at the original call sites. Six production files shrink from
2,891 to 2,866 physical lines (25 fewer), including the private declaration.
No new file, module, target, interface dependency or behavior was introduced.

Canonical Clang 23 `ci` configure, runtime contract build and `IntrinsicTests`
build pass without warnings. All 175 focused processing/history/compiler tests
pass, including the new public preview/apply rejection matrix across eight
domains. Wrong mask type, short/long mask storage, edge/halfedge mismatch and
odd halfedge counts preserve exact diagnostics, output absence and empty history.
Layering, test layout, task policy/state links, docs sync/links, skill mirrors,
session brief, root hygiene and diff checks pass. Source documentation has zero
objective errors; eight retained hints cover the include-order synopsis and
non-obvious job/capture/deletion contracts. No public inventory change.

Verification commands:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointSpacing|KernelDensity|BilateralFilter|DescriptorAnalysis|NormalEstimation|PointConstruction|DensityWeight|Keypoint|Outlier|ProcessingCompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Claude's fixed-source review found no blockers and verified unchanged validation
order, watch order, diagnostics, halfedge remapping and name lifetime. A second
fixed-packet review also found no blockers; its context questions are resolved
by the contiguous domain enum, compiled contract target and executed new test.
The spatial cache has a related mapping with different mask validation; it is
outside this processing slice and does not import editor helpers.
Workshop automation passes. Manual rows 1–3 pass with unchanged layer/target
ownership and no public API changes; rows 4–8 are unaffected or not applicable.

Final CPU gate: 4,711 passed plus one expected ASan-only GLFW lifecycle skip
(4,712 selected, zero failures, 149.84 s). No sanitizer or GPU execution in
this mapping slice; no compilation timing claim. UI-037 remains open for its
broader readiness/cache acceptance. Logs: `/tmp/intrinsic-seventh-*`.


## Property-capture spatial-cache boundary — plan, 2026-09-17

Second operator-directed iteration from `bc0a260c2`: move the unchanged
`AdvancePointKnnRows` definition from the property-capture translation unit
into the existing radius/kNN paging owner `RadiusRows.cpp`. The declaration
already lives in `RadiusRows.hpp` and both density and spacing consume it.
Property capture needs only `Geometry.PointLBVH::ValidPoint`; replace the
spatial-cache service import with that leaf algorithm module, and remove the
now-unused paging header and chrono include. No new file, target or public API.

The compiler graph rejects the proposed no-SpatialIndexCache boundary before
the move. Pin it with a new property-capture compiler-closure test. Preserve
exact kNN body bytes, Euclidean/self-candidate membership, original-slot remap,
batch ownership, error handling and caller-owned stale/cancellation gates;
radius completeness and lowest-ID policy remain distinct. Verify focused CPU
and existing density/spacing Vulkan smoke tests on the combined source.
This is a dependency/ownership correction, not a measured speedup.

Claude's planning pass identified an unused `Geometry.PointCloud.Utils` import
in normal processing. Source search confirms no exported utility is used, and
the compiler graph rejects the proposed boundary before removal. Remove that
import and extend the existing normals closure guard. Claude's broader
companion-input proposal would change diagnostics/watch order and add optional
policy fields; the completed deletion-mapping slice keeps those contracts local.

### Dependency implementation and review checkpoint

The kNN body is byte-for-byte unchanged in its existing paging owner.
Property capture no longer imports `SpatialIndexCache`; normal processing
no longer imports `Geometry.PointCloud.Utils`. The new one-producer property
closure and expanded seven-producer normal closure pass. `IntrinsicTests`
builds and all 67 focused density/spacing/normal/compiler cases pass.

Claude reviewed the initial and final fixed packets and found no correctness
blockers. The complete compile/link and transitive closure tests resolve its
target-ownership and leaf-export questions. Both implementation files remain
in `ExtrinsicRuntime`; no duplicate definition, wrapper or module was added.
A harmless blank-line nit is left outside the verified source.

Three production sources shrink by two lines; five CMake guard-registration
lines make the source/build delta +3. This is code relocation plus dependency
removal, not duplicate-body removal or a compilation-speed measurement.
Layering, test layout, task policy/state links, docs sync/links, skill mirrors,
session brief and diff checks pass. Workshop rows 1–3 pass; renderer/recipe,
retirement and exception rows are unaffected. Public module inventory unchanged.

Canonical `ci-vulkan` configure and `IntrinsicPointLBVHGpuTests` build pass
with Clang 23. All six selected GPU/Vulkan tests execute and pass: normal
neighborhoods, kernel density, spacing, bilateral moving passes, descriptors,
and point-construction generated geometry. No skips or failures (98.92 s).
The combined two-slice production source delta is 27 fewer lines.

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'PointLBVHGpuSmoke\.(KernelDensityPublishesAcrossDomainsAndPreservesCandidatePolicy|PointSpacingPublishesAcrossDomainsAndPreservesCandidatePolicy|NormalNeighborhoodsPublishAcrossDomainsAndRejectIncompleteSupport|BilateralPublishesMovingPassesAcrossDomains|DescriptorPublishesAcrossDomainsWithCompleteSupport)$|PointConstructionGpuSmoke.QueriesMatchReferenceAcrossDomainsAndGeneratedGeometryRenders' --no-tests=error --timeout 120
```

CPU and structural verification use the commands from the preceding mapping
checkpoint, with focused selection
`PointSpacing|KernelDensity|NormalEstimation|ProcessingCompilationLocality`.
Logs and fixed review packets: `/tmp/intrinsic-eighth-*`.

Final combined CPU gate: 4,712 passed plus one expected ASan-only GLFW
lifecycle skip (4,713 selected, zero failures, 160.13 s). All source remained
fixed through final Claude review, builds and test runs. The full CPU sanitizer
suites were not repeated; the six GPU tests used the canonical instrumented
`ci-vulkan` preset. UI-037 remains active for broader readiness/cache work.


## Normal-input selector reuse — plan, 2026-09-17

The operator again requested continued duplication and compilation cleanup with
Claude. Reuse the existing compiled `DrawProcessingPointInput` for descriptor,
bilateral and construction normal selectors. All three query their existing
catalog lazily while open. Keep descriptor/bilateral's strict position-domain
filter, including Unknown, and construction's unrestricted catalog while its
position domain is Unknown. The shared presentation adds domain headings and
sample counts; canonical property identity and validated config application stay
unchanged. No new helper, interface, target or runtime semantics.

Verify the actual panel selectors with mixed vertex/face catalogs, including
selection persisted through config, and run the existing panel and CPU gates.
Claude is reviewing bounded candidates read-only; this checkout remains the sole
writer. A separate follow-up removes unused population-module imports from UV
processing, with before/after compiler-closure evidence. No compile-time speedup
claim is intended. UI-037's broader readiness/cache acceptance remains open.


### Selector implementation and review checkpoint

The three callers now use the existing helper with unchanged catalog sources,
widget IDs and domain filters. Production code shrinks by 28 lines, including
all call sites; no helper or build entry was added. The new actual-panel test
passes all six descriptor/bilateral/construction × resolved/unresolved cases:
strict filters hide other domains, construction's unresolved filter lists both,
and resolved selections persist via config without changing selection or
submitting jobs. Unknown construction normals cannot persist independently of
positions because the existing config validator requires matching domains; the
unresolved test deliberately checks discovery, not a new persistence contract.

Claude's fixed-packet review found no blockers. Its catalog-type and Unknown
validation questions are resolved by the compiled integration target and the
executed six-case test. The shared labels now include domain and cardinality;
no runtime numerical, revision, history or backend behavior changes.


`cmake --preset ci` and the `IntrinsicSandboxEditorIntegrationTests` build pass.
All 60 focused panel/presentation tests pass (8.43 s). Task policy, layering,
test layout, docs sync/links, task links and skill mirrors pass. Manual workshop
rows 1–3 pass (existing app owner, no dependency edges or duplicated selector);
rows 4–8 are unaffected. The full CPU gate will run on the combined source
following the next compile-locality slice. Logs: `/tmp/intrinsic-ninth-*`.


## Copied spacing-result and UV dependency boundaries — plan, 2026-09-17

Claude identified the point-field interface's `CloudStatistics` member as the
reason its interface, config forwarding and frame units depend transitively on
`Geometry.PointCloud.Utils` and the owning point-cloud container. Replace it with
the five editor-consumed summary fields directly in `EditorPointSpacingResult`:
centroid, mean/min/max nearest spacing and bounds diagonal. The existing live
count already supplies point count; unused AABB metadata is removed under the
operator's API-simplification direction and no-external-consumer contract.
Update all app, CPU and GPU callers together. Keep the geometry kernel/result
unchanged and copy values at the runtime result boundary. No wrapper, new module,
compatibility path or runtime algorithm change.

Also remove unused `GeometrySourcesPopulate` imports from parameterization and
UV regeneration, which use view/property access and the existing mesh-soup
publication owner. Before-change compiler probes reject both intended cuts.
Add a three-producer PointFieldResults closure test and extend the three-producer
Parameterization guard. Verify all copied summary fields against a fixed live
rectangle with a deleted NaN row, existing all-domain CPU comparisons, and the
existing Vulkan spacing test. Run full CPU and structural gates on the combined
source, with a fixed-packet Claude review. This is compile-dependency evidence,
not a measured compilation-speed claim. No new layer edge or target is planned.


### Compilation implementation and review checkpoint

The result now owns the five consumed summary values; every in-tree app, CPU
and GPU consumer is updated. Geometry kernels and their result records are
unchanged. Both unused UV imports are removed. The new three-producer result
closure and expanded three-producer UV closure pass against Clang's dependency
graph; both failed at baseline. Module inventory regeneration reports 419 modules
and no generated diff. No new public module or engine dependency edge.

Claude's fixed-packet review found no blockers. The full `IntrinsicTests` build
settles UV symbol reachability and copied result type compatibility; the boundary
tests settle transitive dependencies. The contract test includes algorithm/cmath
and executes the fixed rectangle/deleted-NaN fixture successfully. The panel
already has no direct PointCloud.Utils import to remove. Source-documentation
audit has zero errors; its one review hint is the existing required explanation
of immediate versus deferred result delivery.

All 163 focused spacing, density, UV, panel and compilation cases pass (16.92 s).
The slice adds three production source lines overall (five copied values replace
one structure assignment, minus the unused imports); eight CMake lines register
and extend guards. Combined with selector reuse, production source is 25 lines
smaller. This is a dependency cut, not duplicate-kernel removal or measured speedup.
Manual architecture/workshop rows 1–3 pass: same family ownership and runtime
boundary; no wrappers or new targets. Rows 4–8 remain unaffected. Structural,
layering and documentation checks pass. CPU and Vulkan gates follow below.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointSpacing|KernelDensity|Parameterization|UvRegeneration|ProcessingCompilationLocality|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^PointLBVHGpuSmoke.PointSpacingPublishesAcrossDomainsAndPreservesCandidatePolicy$' --no-tests=error --timeout 120
```

Logs and fixed Claude review packet: `/tmp/intrinsic-tenth-*`.


The canonical Clang 23 `ci-vulkan` configure/build and spacing GPU test pass
(11.16 s test, no skips), with ASan+UBSan enabled by that preset. The extended
comparison covers centroid and bounds diagonal as well as all nearest-spacing
values across eight input domains. No full CPU sanitizer suite was repeated.


Final combined CPU gate: 4,715 passed plus one expected ASan-only GLFW lifecycle
skip (4,716 selected, zero failures, 182.14 s). Source stayed fixed throughout
Claude review, both builds and both execution gates. Both builds are warning-free.
Workshop automation, docs sync/links, task links/policy, skill mirrors, session
brief, layering, test layout, root hygiene and diff checks pass. UI-037 remains
open for the broader readiness/cache acceptance; these are supporting reuse and
compilation-boundary slices, not retirement or a timing result.


## Bilateral config/result compilation boundary — plan, 2026-09-17

The operator again directs continued duplication/compilation cleanup with Claude,
from clean baseline `a7f8f0b8c`. Remove the unused PointCloud.Utils re-export from
BilateralFilterConfig. Replace the editor result's geometry-owned diagnostics
record with its four plain values: points filtered, degenerate normals, average
and maximum displacement. These describe the last completed pass, with zeros
before any pass. Keep every value and update all in-tree UI/CPU/GPU consumers;
keep the numerical kernel and private work data unchanged. Existing config
serialization and apply behavior remain unchanged. No new wrapper, module or
target; the result boundary is the present owner for copied display values.

Pin the no-Utils/no-Cloud boundary across config interface/implementation and
operation interface/progressive implementation/frame with a five-producer test.
The initial probe incorrectly named a nonexistent PointSetOperations.cpp and
failed before inspecting dependencies; it is not baseline dependency evidence.
The focused run caught the same guard registration mistake. The corrected guard
names the existing Runtime.GeometryProcessingOperations.cpp producer. Baseline
source has the two removed direct imports; use the corrected compiler closure
for after-change evidence. Verify last-pass
reporting, degenerate/deleted rows and zero iterations, existing all-domain CPU
comparison and moving-pass Vulkan coverage. Claude separately examines the next
reuse candidate: nine acquisition/snapshot guards. Do not add an acquisition
record or policy framework unless it actually reduces the repeated mechanism.


### Bilateral implementation and correction checkpoint

All four last-pass values are retained with identical types (two size_t counts,
two floats). The new CPU case executes both backends with zero and three passes,
one zero normal and one deleted NaN row; all expected counts/defaults pass.
All-domain CPU and Vulkan comparisons now cover all four copied fields.

The first build exposed the numerical implementation's reliance on the old
config re-export; adding its explicit private Utils import fixes that error.
The full IntrinsicTests rebuild then passes. Of 117 focused cases, 116 passed;
the only failure was the new guard's nonexistent source path. Correcting that
path to the actual progressive processing producer makes the five-producer
compiler guard pass. Neither initial failure is pre-existing or environmental.
The initial before-probe is invalid evidence, as documented in the plan above.

Claude's initial fixed-packet review accepted ownership and last-pass behavior
but required execution evidence, complete-record confirmation and a full build.
The source record has exactly the four copied members; the compiled full target,
executed CPU cases and corrected compiler guard settle those questions. The
public source-doc audit has no errors and four pre-existing comment/synopsis
review hints; module inventory remains 419 with no generated diff. Layering,
task policy, test layout and documentation synchronization pass.


The Vulkan bilateral test passes with all four diagnostic comparisons (14.69 s,
no skip, canonical ASan+UBSan preset). Claude's final code review has no blockers
and asks for the standalone Sandbox build. The edited panel already compiled in
IntrinsicTests as part of ExtrinsicSandboxEditor (initial build log step 34),
but the standalone executable is intentionally disabled in ci. Its attempted
ci target invocation was a command-selection error; use the canonical ci-vulkan
preset, where that target is enabled, for the additional link check.


The ci-vulkan standalone ExtrinsicSandbox build and link pass, including the
edited panel, resolving the final reviewer verification request. Production
source adds five lines (explicit four-field copying and import placement);
the compiler guard adds nine CMake lines. This slice cuts dependencies without
claiming duplicate-body removal or measured compilation speed. Workshop automation
passes; manual ownership/reuse/layering rows 1–3 pass and rows 4–8 are unaffected.
The full CPU gate will run on the combined source after the next reuse slice.
Logs and review packets: `/tmp/intrinsic-eleventh-*`.


## Shared spatial-snapshot comparison — plan, 2026-09-17

Continue from `a44207ea0` with the nine matching sample guards identified by
Claude: density, spacing, normals, bilateral, outliers, density weights,
keypoints, descriptors and construction. The cache owns immutable snapshot
positions and original-slot identity, so place one compiled free predicate in
SpatialIndexCache, with a declaration in its existing interface. No private
processing wrapper, new module, record, policy flag or acquisition lifecycle.

Narrow Claude's proposal to the predicate. Adding snapshot ownership to every
acquisition result is unnecessary here and changes lease lifetime; keep Acquire,
Snapshot and Ready calls and result assignments exactly where they are. Also do
not count compressing three assignments onto one line as a reduction. Share only
null rejection plus same-size/order/value checks using numeric vec3 equality:
positive and negative zero match, NaNs do not. Keep every family mismatch string,
backend gate, status and IndexReused assignment-before-rejection unchanged.

Nine present callers justify the small public function; direct snapshot tests
will exercise null/empty data, cardinality, row identity/order, one-ULP drift,
signed zero and NaNs. Existing all-domain entry-point tests and actual Vulkan
execution exercise the callers. Keep SpatialCompilationLocality.QueryInterface
and family closure guards green, regenerate inventory, and rerun the full CPU
gate on the combined source. This is shared validation and compile ownership,
not a timing claim. The broader acquire wrapper would require a future caller
needing a distinct acquisition contract before reconsideration.


The pre-build Vulkan `ctest -N` discovery timed out in the existing BUG-091
harness path before any selected tests ran. The occurrence is recorded in
`tasks/backlog/bugs/BUG-091-gtest-pretest-discovery-cold-timeout.md`; keep the
normal budgets and diagnose after the target rebuild. This is distinct from
the introduced-and-fixed guard source-path error in the preceding slice.


### Snapshot comparison review and CPU checkpoint

All nine callers share `SpatialIndexSnapshotMatches`; their surrounding code,
acquisition, snapshot leases, reuse reporting and mismatch diagnostics are
unchanged. The helper adds ten production lines including its declaration and
comment; the callers remove fifteen, for five fewer production lines without
format compression. There are no new target entries or files. Across the two
slices the production line count is unchanged; duplicated comparisons are now
compiled once and the bilateral config/results have a narrower import closure.

Claude's fixed final diff review found no blockers and confirmed equality,
ownership and line accounting. Full IntrinsicTests build and all 211 focused
CPU/compilation tests pass (44.31 s), including the new null/order/cardinality/
ULP/zero/NaN test and the unchanged spatial interface boundary. Both interface
inventory generation and task policy pass; the inventory remains 419. Source
documentation has zero errors; the numeric-equality comment is retained because
it distinguishes this predicate from bitwise undo-buffer comparisons.

Manual architecture/workshop rows 1–3 pass: existing spatial owner, nine real
callers and no new dependency edges or lease changes. Renderer/recipe,
retirement and exception rows are unaffected. No performance timing claim.
The full CPU gate and nine GPU publication paths are the final combined checks.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SpatialIndex|SpatialCompilationLocality|PointSpacing|KernelDensity|BilateralFilter|DescriptorAnalysis|NormalEstimation|PointConstruction|DensityWeight|Keypoint|Outlier|ProcessingCompilationLocality|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests ExtrinsicSandbox
```

Logs/review packets: `/tmp/intrinsic-twelfth-*`. Claude also identified unused
Utils/Features re-exports in OutlierAnalysisConfig, KeypointAnalysisConfig and
DescriptorAnalysisConfig. A valid six-producer baseline probe confirms those
config dependencies (`/tmp/intrinsic-thirteenth-boundary-before.log`); that remains
a separate next candidate, with no edits in this slice.


Recording the BUG-091 recurrence enrolls that previously grandfathered task in
the current micro workflow and explicit contract-review schema. The structural
gate caught the missing enrollment; the original contract-baseline hash is now
moved unchanged to `consumed`, and task policy/session-brief checks pass. No gate
or discovery budget was changed. The CPU build also reports existing warnings
in unchanged test fixtures (ignored AddTriangle result and constructor member
order); these are not test failures or new warnings from the touched sources.


Final combined CPU gate: 4,718 passed plus one expected ASan-only GLFW lifecycle
skip (4,719 selected, zero failures, 179.58 s). All nine GPU/Vulkan publication
tests passed (123.13 s total, no skips): normal estimation, outliers, density,
spacing, bilateral, keypoints, descriptors, density weights and construction.
The canonical instrumented ci-vulkan build includes a successful standalone
Sandbox link. No full CPU sanitizer suite was repeated. Source stayed fixed
through the final Claude code review and both execution gates.

```bash
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^PointLBVHGpuSmoke\.(NormalNeighborhoodsPublishAcrossDomainsAndRejectIncompleteSupport|OutlierNeighborhoodsPublishAcrossDomainsAndCountDenseSupport|KernelDensityPublishesAcrossDomainsAndPreservesCandidatePolicy|PointSpacingPublishesAcrossDomainsAndPreservesCandidatePolicy|BilateralPublishesMovingPassesAcrossDomains|KeypointPublishesAcrossDomainsWithCompleteSupport|DescriptorPublishesAcrossDomainsWithCompleteSupport|DensityWeightPublishesAllKernelsAcrossDomains)$|^PointConstructionGpuSmoke.QueriesMatchReferenceAcrossDomainsAndGeneratedGeometryRenders$' --no-tests=error --timeout 120
```

All final structural/workshop, layering, docs/link, task-policy/state-link,
source-documentation, root-hygiene and diff checks pass. UI-037 retains its
broader readiness/cache acceptance; BUG-091 retains its cold-start root-cause
and distribution work. No compilation-speed measurement or retirement claim.


## Point-analysis config and result boundaries — plan, 2026-09-17

The operator continues compilation-locality/reuse work with Claude. Keep the
existing config, operation and geometry owners; add no module, service, wrapper
or compatibility path. Remove the unused Utils export from OutlierAnalysisConfig
and Features exports from KeypointAnalysisConfig/DescriptorAnalysisConfig.
A fresh compiler-derived baseline finds the expected dependencies in all six
config producers and the three point-analysis interface/config/frame producers.
Logs and fixed read-only Claude packets live at `/tmp/intrinsic-next-locality/`.

The second bounded candidate replaces the two geometry-owned result scale
records with their five consumed scalar values on the existing runtime result
records. Keep scale resolution and supplied-neighborhood algorithms in Features;
copy all values at the current success/prepare/GPU completion boundaries and
construct the geometry scale only at its numerical call. Density-weight Kernels
is a real API dependency and stays. Preserve failure-state diagnostics, deleted
slot mapping, radius values, job sequencing and all publication/undo semantics.

Extend the compiler-derived boundary checks over actual config/interface/frame
producers, compare copied diagnostics through existing cross-domain CPU and GPU
entry-point tests, rebuild the app and full CPU target, and run the CPU gate plus
the affected Vulkan paths. Review the fixed final diff and refresh architecture
docs/module inventory. No compilation-speed claim follows without matched timing.
UI-037's broader readiness/cache acceptance stays open.

### Point-analysis implementation and review checkpoint

Both boundaries are implemented. The nine-producer compiler guard excludes
Features, Utils and the owning PointCloud from the config/interface/frame
closure; the baseline showed 21 forbidden paths and the rebuilt guard passes.
The geometry scales contain exactly the five zero-initialized floats now copied
onto runtime results. Numerical adapters import Features directly and rebuild
its scale with designated fields at the supplied-neighborhood call. Keypoints'
unused Utils import is removed. No new production file, type wrapper or target.

The existing eight-domain CPU tests now compare every copied diagnostic; two
new public-command fixtures check automatic/explicit radii against analytic
rectangle spacing for KD-tree and LBVH while ignoring a deleted NaN sample.
Existing GPU comparisons cover the same fields. All 78 focused CPU/dependency
tests pass (10.34 s). Canonical ci configuration and full IntrinsicTests build
pass with Clang 23. A mistaken initial request to build ExtrinsicSandbox under
ci failed because that preset disables the app; the corrected CPU build passes
and the app is built under its intended ci-vulkan preset. This was an invocation
error, not a source/build-system failure.

Claude's plan review accepted both slices. Its import concerns were checked
against source: SpatialIndexCache exports PointLBVH; runtime tests do not call
Features; numerical adapters now import it explicitly. Its fixed-diff review
found no correctness or layering blocker and confirmed success, pending and
failure copies. The analytic test expectations match the existing nearest-
neighbor estimator and automatic-radius rules and all pass. Broader failure
paths remain covered by the existing stale/cancel/overflow GPU fixtures.

Production delta: 11 added physical lines across seven existing production
files, for explicit boundary copies/comments and designated reconstruction;
this slice narrows compilation dependencies rather than claiming line reduction.
Architecture/workshop rows 1–3 pass: existing owners and downward edges only.
Rows 4–8 are unchanged or not applicable; no task retirement or exception.
Strict layering, workshop, task policy, test layout, root hygiene, docs links
and sync pass. Module inventory remains 419 after regeneration. Source-doc
audit reports zero objective errors; two interface-comment review hints do not
change the field units contract. Full CPU and affected Vulkan execution follow.

Final combined verification: 4,721 CPU tests passed plus one expected ASan-only
GLFW lifecycle skip (4,722 selected, zero failures, 151.34 s). All eight affected
Vulkan tests passed with ASan+UBSan and no skips (57.01 s), covering keypoint
neighborhood/full-compute publication, stale/cancel/overflow rejection, numerical
edge cases, descriptor publication and dense lowest-ID caps. The standalone
Sandbox links under ci-vulkan. Full CPU sanitizer variants were not rerun.
No source/test edits followed the fixed Claude diff review or successful gates;
source hashes are recorded with the local review packet. UI-037 remains open
for its broader readiness/cache acceptance, with no compilation timing claim.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ProcessingCompilationLocality|KeypointAnalysis|DescriptorAnalysis|OutlierAnalysis|DensityWeight' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^PointLBVHGpuSmoke\.(Keypoint|Descriptor)' --no-tests=error --timeout 120
```


## Point-config serialization reuse and visualization boundary — 2026-09-17

Operator-directed continuation with Claude. Reuse search found seven identical
Kind/Ref JSON encoders in the bilateral, density, spacing, outlier, keypoint,
descriptor and density-weight config implementations. Share one compiled
`ConfigDetail::EncodePointPropertyRef` in the existing ordinary shared codec TU,
with one private declaration header and C++ linkage across module units. No new
module, implementation file, target, registry or policy parameter. Retain the
three-kind string vocabulary, domain bounds and invalid sentinel, and name bytes.
The existing five-family numeric-kind codec and registration/construction's
vec3-only encoders have different contracts and stay separate.

The new public-serializer fixture passes against the original code for all seven
families, including Unknown/invalid domains, supported/unsupported kinds and
NUL/quote/backslash/newline names. Extend existing round-trip/application coverage
after replacement. This header exists because the proven common implementation
serves seven module owners; a header-only body would compile the duplicate again.

Remove the unused Geometry.UvAtlas import from visualization operations. The
baseline compiler graph shows the interface and Public/Debug/Actions producers
all inherit it; guard those four actual producers after rebuilding. Curvature
config's public parameter-conversion function was separately inspected with
Claude but is deferred, with no edits to its validator or conversion.

Review the fixed diff, run canonical CPU builds/tests and the Sandbox build,
refresh the module inventory and boundary docs, and preserve broader UI-037
readiness/cache acceptance. Logs/packets: `/tmp/intrinsic-config-locality/`.
No compilation-time claim follows from these source/dependency changes.

### Serialization implementation and correction checkpoint

Claude's reuse review found that validators also call the duplicated Kind helper.
The first build caught those unresolved calls and an overbroad textual replacement
of `validRef` names. Fixed both: `PointPropertyKindToken` and
`EncodePointPropertyRef` now compile once, serializers and validators use the same
three-kind vocabulary, and the family-local validRef lambdas retain their exact
validation. The failed build is retained in the local packet, not hidden.

All seven callers and the ordinary shared implementation are private sources of
one ExtrinsicRuntime target with common compile definitions; no target-link edge
was added. The private header declares only global C++ linkage functions, imports
nothing, and is included after JSON/type declarations. The shared implementation
includes that header before its definitions and explicitly imports the canonical
GeometryAvailability owner. Include-order errors already fail compilation; no
third-party header-guard macro policy is needed. The source-documentation contract
is declared for the new private header.

### Fixed-diff review and focused verification

The corrected focused build and all 165 selected CPU/dependency tests pass
(17.30 s). Claude's fixed-diff review found no blocker and confirmed encoder,
validator-token and linkage equivalence. Its follow-ups were checked against
source: both enums have fixed uint8_t bases; the boundary helper sets no labels
to override; compile_hotspots walks transitive CMake usages. Clarified the header
prerequisite comment and added explicit non-vec3 position rejection for all seven
validators; the final serializer/validator fixture and UV dependency guard pass.
The full IntrinsicTests target builds with Clang 23.

Symbol inspection of libExtrinsicRuntime.a finds one definition of each shared
function and seven referencing object files. The production delta, including the
new private header and unchanged-format caller replacements, is 14 fewer physical
lines across ten production files. Existing numeric-kind and vec3-only encoders
remain because their contracts differ. No JSON implementation moved into a public
module or a repeatedly compiled header.

Manual architecture/workshop rows 1–3 pass: current runtime owners, same CMake
target and no downward type leak. Rows 4–8 are unchanged/not applicable; no new
renderer/pass/recipe, task closure or exception. Strict structural checks, docs
links/sync, task policy, test layout, root hygiene and skill-mirror checks pass.
Module inventory remains 419. Source-documentation audit has zero errors and one
pre-existing large-shared-codec-file hint; sharing a small encoding mechanism does
not require another implementation target. Existing ignored-AddTriangle warnings
in unchanged parameterization/mesh test fixtures are not new source warnings.

### Combined verification and bounded follow-up

The final canonical CPU build passes. The first full CPU run exposed the remaining
fixed-frame duplicate dropped-import test: no mesh/result while decode was still
pending. Recorded and corrected separately as
[BUG-202](../done/BUG-202-duplicate-drop-frame-budget.md), using the existing
condition waiter and decode barrier. The unchanged test passed 100 isolated runs;
a controlled 128-frame probe reproduced the failure, then the corrected waiter
passed 100 delayed-worker repetitions and all ten neighboring cases. Claude
approved that fixed diff; terminal publication ordering and sub-deadline completion
were verified against source and run evidence. No production import code changed.

The combined full CPU selector passes 4,723 tests, with one expected GLFW/LSan
capability skip (4,724 selected, 146.28 s). The ci-vulkan preset builds
ExtrinsicSandbox, IntrinsicPointLBVHGpuTests and
IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests. All nine selected Vulkan tests
pass (89.02 s), covering each of the seven point families plus generated-UV/albedo
and exact normal-bake readback integration. That preset uses combined ASan/UBSan;
this is focused Vulkan sanitizer evidence, not a new full ci-asan/ci-ubsan sweep.

The source hashes match the reviewed and built implementations. No measured
compilation-time improvement is claimed. UI-037's broader readiness inventory,
shared authoritative predicates, cached derivations and all-control tooltip proof
remain open. Curvature config's parameter conversion is a deferred, separately
reviewed candidate; this slice does not alter it.


## Point-property decoding and curvature config boundary — 2026-09-17

Operator-directed continuation with Claude, two bounded slices. Reuse the seven
point-config families' exact name/domain decode mechanism in the existing
compiled ConfigDetail owner; preserve typed default kinds and keep differing
validators/diagnostics local. A public Set/Get/Serialize regression exercises
all nine domain tokens, distinct names containing embedded NUL/quote/backslash/
newline bytes, and all 33 descriptor outputs before and after replacement.

The curvature config's public parameter converter has only two implementation
consumers: its own validator and the curvature operation. Give those files one
private declaration, retaining the existing compiled definition and numerical
validator. Remove the resulting geometry algorithm dependency from the config
interface and shared JSON codec producer; cover actual Sandbox config producers
in the compiler-graph guard too. Mesh-field result types still need their geometry
imports and are not changed. One declaration header is justified by the two
current consumers; no new wrapper, module, target or copied conversion is needed.

Baseline compiler paths already confirm both config interface and shared codec
reach Geometry.HalfedgeMesh.CurvatureSegmentation. Review packets and logs are in
`/tmp/intrinsic-config-decode-boundary/`. Plan/fixed review with Claude, focused
config/curvature tests, full canonical CPU gate, Sandbox build, structural/docs
checks and inventory refresh will verify the slices. No compilation-time claim.


### Implementation and review checkpoint

The initial build rejected a global-C++ converter declaration followed by a
named-module definition. Added the same explicit C++ linkage to the existing
definition; its body and the numerical validator are unchanged. Claude's plan
review identified the same issue, and its fixed-diff review accepts the correction.
The decoder preserves each old lambda's assignment/order, comparison semantics,
domain bounds and untouched ValueKind. Both reader shapes were checked across all
seven families; no validators were merged.

The new characterization case passes both before and after refactoring. All 131
focused config/curvature/point-method cases and the four-producer boundary check
pass (6.49 s). The canonical full IntrinsicTests aggregate and the ci-vulkan
ExtrinsicSandbox target build. The existing invalid min/max component-range case
still exercises the unchanged geometry numerical validator. Repository-wide
references find only the expected converter definition and its three calls in
two implementation consumers; no app or test used the removed public declaration.
Symbol inspection finds one converter definition and the curvature consumer's
matching reference, plus one shared property decoder definition.

Claude's fixed review follow-ups are verification/bookkeeping: include the owner
route/task delta, report the post-refactor tests and all-target build, and check
Clang 20 linkage. Its optional unknown-domain-through-public-config test would
contradict current validation, which rejects that token; direct private tests are
unnecessary for this exact body extraction. The roundtrip retains the default
vec3/uint32/float kinds across different fields; it does not claim arbitrary kinds
are accepted. Source-documentation audit has zero errors and one reviewed comment
finding: the decoder's validated-input/retained-kind precondition is non-obvious
and remains documented. Net production size, counting the new private header,
is 11 fewer physical lines across 13 files (decoding -25, boundary declarations +14).
Workshop rows 1–3 pass; rows 4–8 are unchanged/not applicable. No new target,
policy exception, renderer pass or task retirement. Strict structural/docs checks
pass; refreshed module inventory remains 419. Full CPU and Clang 20 checks follow.


### Final verification

The canonical Clang 23 full CPU gate passes 4,725 cases with one expected
GLFW/LeakSanitizer capability skip (4,726 selected, 174.53 s). The ci-vulkan
Sandbox build passes; no GPU continuation or numerical implementation changed,
so no new backend/performance claim follows. Full sanitizer CPU suites were not
rerun for this extraction.

A dedicated ci-derived Clang 20 Null/headless configuration with matching
clang-scan-deps and ccache disabled compiles the ten affected implementation
objects and their module dependencies. A relocatable link of those objects
resolves both shared functions to exactly one definition with no unresolved
references to them. The config-interface/shared-codec forbidden-import check
also passes in that tree. This is focused Clang 20 compile/link evidence, not a
full Clang 20 executable/test gate. Review/build source hashes are unchanged.
Broader readiness/cache acceptance remains open; the deferred curvature config
converter boundary from the previous slice is now addressed.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxConfigSections|CurvatureSegmentation|ConfigCompilationLocality|BilateralFilter|KernelDensity|PointSpacing|OutlierAnalysis|KeypointAnalysis|DescriptorAnalysis|DensityWeight' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox
cmake --preset ci -B build/ci-clang20 -DCMAKE_C_COMPILER=/usr/bin/clang-20 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-20 -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-20 -DINTRINSIC_ENABLE_CCACHE=OFF -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_PLATFORM_BACKEND=Null
```
The focused Clang 20 Ninja object targets are the config converter, curvature
operation, shared feature codec and the seven point config implementations,
under `src/runtime/CMakeFiles/ExtrinsicRuntime.dir/` in that build tree.

Claude's final evidence/route review approves with no blockers. Its optional
linkage reminder is included in the converter owner route; full sanitizer and
Clang 20 runtime-suite limitations remain explicit above.


## Point-property validation consolidation (2026-09-17)

Operator-directed continuation of duplication/compile-locality cleanup with
Claude. This bounded slice extends the existing private PointConfigJson owner:
seven point-config families now call one compiled property-reference validator.
Its three outcomes preserve the five families' separate malformed-reference and
unknown-domain diagnostics and the other two families' combined diagnostics.
The exact three-field grammar, string kind tokens, nonempty names, guard order,
valid `Unknown` token and family-owned property relationships remain unchanged.
Numeric-kind codecs and vec3-only encoders have different contracts and stay
separate. No public module surface, dependency edge, target or tuning changes.

Claude reviewed the plan; its shape/domain ordering and global C++ linkage
conditions are preserved. A new public-API fixture covers 20 malformed references
across all seven families, checking exact diagnostic text, subject and code;
it and the serializer/full-domain roundtrip fixtures pass against the original
implementation before extraction (3 cases). After extraction, IntrinsicTests
builds and all 109 focused config/point tests
pass. The canonical Clang 23 CPU gate passes 4,726 cases with one expected
GLFW/LeakSanitizer capability skip (4,727 selected, 169.39 s). The ci-vulkan
ExtrinsicSandbox target builds and links. No GPU runtime or full sanitizer CPU
suite was rerun for this grammar extraction. Claude's fixed-diff review approves
without blockers; its full-CPU/app-link conditions are satisfied. Reviewed source
and test hashes are unchanged. Scope/layering/tests/docs review and task policy,
doc links, layering, test layout, root hygiene and skill-mirror checks pass.
Source-documentation audit has no errors; the one retained decoder comment
explains its non-obvious validated-input/value-kind precondition. No measured
compilation-speed claim is made.
Broader readiness/cache acceptance remains open. The operator requested a fresh
session recommendation to limit context costs; stop after this verified slice
and leave a temporary handoff rather than beginning another slice.


## Geodesic result compilation boundary (2026-09-17)

Operator-directed continuation of duplication and compilation cleanup with
Claude, starting at `5b02fc803`. This is a bounded UI-037 compilation slice;
readiness acceptance remains open. The initial compiler graph contains 25
mesh-field consumer producers. EditorProcessing, EditorCommon and
EditorWorkspaceAttachment do not import the full halfedge mesh; the other
mesh-field algorithm diagnostics still do.

Reuse/right-sizing decision: move the existing `VirtualSourceStatus` and complete
`VirtualSourceResult` verbatim into `Geometry.Geodesic.Types`, used directly by
geometry and runtime. This avoids a duplicate runtime record and field-by-field
conversion while retaining the distance field, counters and success predicate.
One new data-only module separates two present consumers; no factory, facade,
backend or parameter axis is added. Parameters and `ToString` stay in the
algorithm module, so their implementation attachment does not change. The
original algorithm re-exports its result types for its own public result API.

Claude recommended a geodesic pilot, gated on actual consumer reachability,
before a broader segmentation extraction. Its proposed extra parameter/string
move is unnecessary for runtime consumers and is deliberately omitted. The
baseline scanner guard detects the algorithm dependency in all three selected
mesh-field producers. The new CTest uses the same compiler metadata and also
checks the new types module. No algorithm, publication/history, diagnostic value
or existing behavior test is changed. Source-documentation and module-inventory
checks apply to the new interface. No compile-time performance claim is made.

The focused geometry/runtime/editor build passes with canonical ci Clang 23;
all 64 selected geodesic, segmentation, mesh-field and boundary tests pass.
`ProcessingCompilationLocality.GeodesicResults` passes for all four producers.
The mesh-field closure replaces `Geometry.Geodesic` with
`Geometry.Geodesic.Types` (55 modules before and after); this proves separation
from algorithm-interface edits, not a reduction in total modules or measured
wall-clock compilation time. The production change is five files including the
new module and CMake registration, 802 to 816 physical lines (+14 for the new
module boundary); it introduces no duplicate record or conversion implementation.

Claude's fixed-diff review has no code blockers. Both validation conditions are
satisfied by the boundary test and full IntrinsicTests plus ci-vulkan
ExtrinsicSandbox builds. Its possible prefix-match/label-overwrite concerns do
not apply: the boundary checker compares exact names and the registration helper
sets no labels. Optional include/order/blank-line suggestions require no change.
Source documentation reports zero errors and six reviewed declaration-comment
hints: numerical/lifetime comments stay; the unchanged heat-method overview is
outside this mechanical record extraction. Inventory refreshed to 420 modules.
Strict task policy/state links, layering, test layout, docs links, skill mirrors,
root hygiene, ARA structure and workshop checks pass. Manual workshop rows 1-3
pass, rows 4-6 do not change, UI-037 retains readiness follow-up and there are no
layer exceptions. Source and test hashes match the fixed review packet.

The canonical CPU gate passes: 4,728 selected, 4,727 passed, one expected
ASan-only GLFW lifecycle skip, zero failures (154.33 seconds). GPU runtime and
full sanitizer CPU suites are not run for this record move. Evidence and review packets are in
`/tmp/intrinsic-meshfield-locality/` on the verification host.


## Segmentation diagnostic compilation boundary — plan (2026-09-17)

Operator-directed reuse/compilation continuation with Claude at `b39161de4`.
The compiler-derived baseline rejects all five intended cuts (the four
segmentation algorithms and owning halfedge mesh) in each of the mesh-field
interface, config adapter and prepared-frame producers. Reuse the existing
records verbatim in one `Geometry.CurvatureSegmentation.Diagnostics` module;
all four algorithms and runtime consume it. Keep parameters, full result arrays
and status-string functions with their algorithm owners. This is a present
compile boundary, without duplicate runtime records, conversions or new behavior.
One writer owns this checkout; Claude reviews bounded read-only packets.

Extend the scanner-derived CTest guard to those three runtime producers and
the diagnostic module, then build `IntrinsicTests`, run focused segmentation/
mesh-field/panel tests and the exclusion-only CPU gate, and link the ci-vulkan
Sandbox. Refresh module inventory and architecture docs; run scope/layering/
tests/docs review and structural checks. UI-037 readiness acceptance remains
open. These dependency changes alone establish no compilation-time speedup.
Evidence: `/tmp/intrinsic-segmentation-locality/`.

The 13 moved status/diagnostic/timing records are byte-identical to baseline.
Production scope is eight files including the new module and CMake registration,
3,899 to 3,922 physical lines (+23 for the module boundary/imports). This is
dependency isolation rather than duplicate-body removal. The new module has no
imports; the mesh-field interface now reaches 47 modules. The baseline guard
rejected all five forbidden modules in each runtime producer; the new guard passes
all four producers. No runtime copies or field-conversion bodies were added.

The focused build first exposed an existing test's incidental `PatchBoundaryRole`
re-export dependency. Its direct Patches import fixes that without altering its
assertions; the repeat focused build and IntrinsicTests build pass. All 102 focused
segmentation, patch/boundary, geodesic, runtime/panel and locality cases pass
(8.59 seconds). Claude's fixed-diff review finds no code defects, conditioned on
the full gates. Config/ToString concerns from planning are resolved in source:
config has no algorithm imports; only execution uses geometry status strings.
The module inventory includes the new owner (421 modules). Source-documentation
audit: zero errors, 11 reviewed hints; retained comments describe numerical,
ownership, synchronous/asynchronous or failure contracts. The history heuristic
on “Iterations remains the deterministic work counter” is a false positive.

Canonical ci/Clang 23 CPU verification passes: 4,729 selected, 4,728 passed,
one expected ASan-only GLFW lifecycle skip, zero failures (144.25 seconds).
All locality CTests pass, including the new segmentation boundary. Strict
layering, test layout, task policy/state links, doc links, docs sync, skill
mirrors, root hygiene and workshop checks pass. Manual workshop rows 1–3 pass;
rows 4–6 are unchanged; UI-037 retains its readiness follow-up; there are no
layering exceptions. The full CPU/test build also resolves Claude's dropped
re-export concern beyond status strings. Reviewed source/test hashes are unchanged.

The ci-vulkan/Clang 23 `ExtrinsicSandbox` target builds and links after the
canonical CPU gate; Claude's remaining build/verification conditions are met.
No GPU runtime or full sanitizer CPU suite was run for this record move.
The operator requested session-boundary advice to conserve context; this
verified slice is the checkpoint, with a temporary handoff before further work.

## Strict point-config field validation reuse (2026-09-17)

Operator-directed continuation with Claude at `7d76d7111`. Ten point-config
validators duplicate the adjacent shallow default-field merge and ordered
unsigned-32 validation. Share that exact mechanism through the existing private
`Runtime.PointConfigJson.hpp` and compiled `Runtime.FeatureConfigCodecs.Detail.cpp`
owner. Preserve family diagnostics, field order, numeric constraints, parsed
counts and nested replacement. The five fallback-oriented codecs in that owner
have different warning/merge semantics and remain separate. No new public
module, record, template, policy switch or layer edge is needed.

Claude's planning review requires an adjacency/text audit of all ten families
and pre-edit public tests for exact diagnostics and their priority. Both are
part of this slice. Verify the regression before and after consolidation, then
build `IntrinsicTests`, run config/processing focused tests and the canonical
CPU gate. Review a fixed diff with Claude and run the structural/source-doc
checks. Evidence lives in `/tmp/intrinsic-point-config-reuse/`; readiness/cache
acceptance remains open, and this slice makes no measured compile-speed claim.

The pre-edit public-validator regression passes against the original code, then
all 45 config/tooltip/compilation-locality tests pass after consolidation.
Canonical ci uses Clang 23; `IntrinsicTests` builds and the exclusion-only CPU
gate passes: 4,730 selected, 4,729 passed, one expected ASan-only GLFW lifecycle
skip, zero failures (154.67 seconds). The archive has one compiled helper
definition and ten caller references. Across all 12 affected production files,
physical lines change from 4,029 to 4,026, including helper declarations and
explicit standard-library includes. This removes duplicate control flow, with
only a small net line reduction; it adds no files or public module surface.

Claude approves the fixed diff and identifies two test gaps: an unknown key that
sorts after an invalid integer, and ordering beyond the first integer field.
Both assertions are added; Claude approves their follow-up review. Production
hashes are unchanged from the reviewed/full-gate source. Strict layering,
test layout, task policy/state links, docs sync/links, skill mirrors, root
hygiene, session brief and workshop checks pass. Source-doc audit has zero
errors and three reviewed hints: both header comments express preconditions;
the existing large codec implementation remains the appropriate compiled owner.
Manual workshop rows 1–3 pass; renderer/recipe rows 4–6 are unchanged, UI-037
retains readiness follow-up, and there are no layer exceptions. GPU execution
and sanitizer CPU suites are not run for this validation-only consolidation.
After the review fixes, rebuild `IntrinsicSandboxEditorIntegrationTests` and
run `ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.|^SandboxEditorPresentation.DisabledActionReasonTooltipAppearsAfterTwoFrames$' --no-tests=error --timeout 120`:
all 15 affected cases pass (1.15 seconds). The full CPU gate above used the same
production source; only these additional assertions changed afterward.


## Normal config/result locality and numeric-validation reuse — 2026-09-17

Operator-directed continuation with Claude at `b92f0aab7`. One writer owns the
checkout/build; Claude reviews bounded read-only packets. UI-037 readiness
acceptance remains open. Evidence: `/tmp/intrinsic-normal-config-locality/`.

The compiler baseline reaches both normal algorithms, mesh/point-cloud owners
and three spatial indices from each of five config/normal interface, codec and
frame producers. Move the two enums and diagnostics verbatim into import-free
`Geometry.NormalEstimation.Types`; geometry and runtime share that owner, without
copies/adapters. Parameters, property results and DebugName stay with algorithms.
The new locality CTest passes all six producers. Full test/Sandbox builds resolve
Claude's transitive-export concerns; config uses integer tokens and execution
already imports both algorithms. Production scope is five files including CMake
and the new module: 555 to 572 physical lines (+17 for the compilation boundary).

Five numeric validators share one compiled non-template helper through existing
`Runtime.PointConfigJson.hpp`/`Runtime.FeatureConfigCodecs.Detail.cpp`. Point
spacing, bilateral filtering, kernel density, outlier analysis and construction
keep field order, exact diagnostics, underflow and positivity rules. The distinct
keypoint/descriptor representability and double-valued density checks stay local.
Seven existing production files change from 3,380 to 3,386 physical lines: six
net lines for the shared declaration/function while removing four duplicate
control-flow bodies and ten unused includes. No new numeric policy axis or file.

The public regression passes before consolidation. Claude's review prompts
integer-extreme and preceding-backend-error cases. Those expose an existing bug:
JSON comparison treats unsigned numbers above INT64_MAX as negative. A standalone
probe reproduces it. The shared float lower bound and radius-method positivity
check now compare as double, consistent with the upper bound. Tests accept
INT64_MAX, INT64_MAX+1 and UINT64_MAX (including radius mode), reject INT64_MIN,
and preserve float max, subnormals, signed zero, underflow, zero-radius and error
ordering. Claude accepts the shared comparison correction. Existing explicit
cmath/limits includes resolve its include question; the installed parser rejects
overflow before validation, so +/-1e400 retain the object-error diagnostic.

Verification and review:
- Canonical ci/Clang 23 `IntrinsicTests` and ci-vulkan `ExtrinsicSandbox` build
  and link. Initial 61 normal/config tests pass; after numeric correction all
  33 focused config/outlier/locality tests pass (2.37 s).
- The first full gate passes 4,731 cases plus one expected ASan-only GLFW skip.
  A later full gate discovers the independent fixed-frame fixture failure now
  tracked as [BUG-203](../done/BUG-203-manual-import-frame-budget.md). Controlled reproduction and the test-only correction are
  recorded in that task; cancelled intermediate runs are not passing evidence.
- Final combined source: 4,731 CPU tests passed, one expected ASan-only GLFW
  lifecycle skip, zero failures (4,732 selected; 152.55 s). BUG-203 controlled
  regression passes 25/25 times; all four neighboring import tests pass.
- Strict layering, test layout, task policy/state links, doc links/sync, mirrors,
  root hygiene, ARA structure and workshop checks pass. Inventory: 422 modules.
  Source-doc audit: zero errors and six reviewed hints; retained comments specify
  numerical/index-layout/key-presence contracts and the existing large codec
  unit remains the compiled owner. Workshop rows 1–3 pass, 4–6 unchanged;
  readiness remains open and there are no layer exceptions.
- No measured compilation-time speedup, GPU execution or full sanitizer CPU-suite
  claim follows from these dependency cuts and CPU/compile-link checks.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.|^ProcessingCompilationLocality.NormalContracts$|^OutlierAnalysis' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j4
```

## Density config/result locality — 2026-09-18

Operator-directed continuation with Claude from `0e7f7d65d`; Codex owns the
checkout/build and Claude reviews fixed read-only packets. UI-037's wider
readiness acceptance remains open. Logs/reviews: `/tmp/intrinsic-density-locality/`.

The configured compiler baseline reaches `Geometry.PointCloud.Kernels` and
`Geometry.KDTree` from all five density config/interface/codec/frame producers.
Move the kernel/mode enums, copied diagnostics and their two token-spelling
functions verbatim into `Geometry.PointCloud.Kernels.Types` and its compiled
implementation. The algorithm re-exports the sole type owner; execution imports
its kernel API explicitly. Seven producers pass the new dependency-boundary test,
which excludes the kernel algorithm, spatial queries and all three point indices.
All six kernel/mode combinations round-trip across the nine domain tokens.
Eight production files, including new files and CMake: 1,306 to 1,330 physical
lines; the 24-line increase pays for the compilation boundary, not new behavior.

Claude approves the fixed diff, conditioned on full consumer compilation and
separate commits for the codec tests. The first focused build identified the
execution unit's missing direct kernel import; the corrected build passes.
Before codec consolidation, all 16 config cases plus the new boundary test pass.
The final combined `IntrinsicTests` build passes on canonical ci/Clang 23; the
41 focused config/kernel/density-operation/boundary tests pass after consolidation.
No measured build-speed, GPU-execution or full-sanitizer-suite claim follows.

Architecture sweep: layer/CMake/type-ownership rows 1–3 pass, renderer/pass/recipe
rows 4–6 are not applicable, row 7 retains the open readiness task, row 8 has no
exceptions. Source documentation audit: zero errors; ten reviewed hints retain
kernel numerical/index contracts and the existing point-analysis family synopsis.
Inventory regenerated: 423 modules. Final combined full CPU gate: 4,733 passed,
one expected ASan-only GLFW lifecycle skip, zero failures (4,734 selected;
153.06 seconds). Canonical ci-vulkan/Clang 23 `ExtrinsicSandbox` also compiles
and links with ASan+UBSan instrumentation; no GPU or sanitizer test run is implied.

## Normal/construction property-codec reuse — 2026-09-18

Following density locality commit `6d5cd32a9`, normal estimation and point
construction now call the existing compiled `DecodePointPropertyRef` and
`ValidatePointPropertyRef` owner. Four duplicate bodies disappear; the two
production files shrink from 409 to 383 physical lines. No new helper, file or
policy switch. Normal's separate malformed-reference/unknown-domain messages and
construction's combined message remain; ordering, method/domain rules and each
vec3-only serializer's `invalid` sentinel remain local. Registration's distinct
non-string-domain diagnostic is outside this slice.

Existing public diagnostic and all-domain/name-byte round-trip tables cover both
families. Added cases pin the second property, first-error priority, face-normal
and supplied-normal domains, and wrong-kind serialization. These tests pass
before and after reuse. Claude approves the fixed diff; its optional wrong-kind
category check is already covered by the expanded malformed-reference table.
Combined-source verification is the 41 focused tests, full CPU gate and Vulkan
Sandbox compile/link recorded above. Strict layering/test-layout, task/state,
doc links/sync, source documentation, mirrors, root hygiene, ARA structure and
workshop checks pass. UI-037 remains open; no compilation-time claim is made.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.|^ProcessingCompilationLocality.DensityWeightContracts$|^DensityWeightOperations\.|^PointCloudKernels\.' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j4
```

## Registration property reuse and smoothing result locality — plan, 2026-09-18

Operator-directed duplication/compilation cleanup with Claude from `de8011d45`;
Codex owns this checkout and builds, Claude reviews fixed read-only packets.
The existing source-documentation and processing-locality contracts apply; the
broader readiness task remains open. Logs: `/tmp/intrinsic-registration-smoothing/`.

- Reuse the existing compiled `DecodePointPropertyRef` and
  `ValidatePointPropertyRef` in registration. Keep its non-string-domain
  malformed-reference diagnostic and local vec3-only serializer, whose invalid
  kind handling differs from the generic serializer. No new helper or policy flag.
- Move only `DenoiseStatus` to `Geometry.Smoothing.Types`, retaining the existing
  namespace, values and fail-closed contract. The algorithm re-exports that sole
  owner; topology results import it directly. This tiny interface is justified by
  the compiler boundary: all three topology contract/display-name/frame producers
  currently reach `Geometry.Smoothing`, `Geometry.HalfedgeMesh` and `Geometry.DEC`.
  Algorithms and `DebugName` keep their present compiled owner.
- Verify registration diagnostics for all three bindings and domain/name-byte
  round-trips before/after reuse; add the compiler-closure regression, then run
  the full CPU gate and Vulkan Sandbox compile/link on combined source. Keep
  locality and duplicate-body removal in separate commits. No elapsed build-time
  claim follows from the dependency cut.

Registration reuse is implemented: both duplicated bodies are removed and the
production file shrinks from 134 to 125 physical lines. All three property
bindings retain malformed versus unknown-domain categories, first-error order,
and all nine domain tokens/name bytes. The extended 17-case config suite passed
before the refactor; the combined 78-case focused suite passes after it. Claude
finds no blockers in the fixed registration diff; the existing header declarations
are confirmed by the canonical ci/Clang 23 full `IntrinsicTests` build. The final
combined CPU gate passes 4,735 tests plus one expected ASan-only GLFW lifecycle
skip (4,736 selected, 153.83 s). Vulkan Sandbox compile/link is still in progress
at this checkpoint. The vec3-only serializer remains local by contract, not as
deferred duplicate cleanup.

Smoothing locality is complete. The four-producer compiler guard passes; its
three original producers failed all smoothing/mesh/DEC boundaries before the
edit. Five production files, including the new enum owner and CMake, change from
3,806 to 3,817 physical lines. The 11-line increase establishes a compilation
boundary; it is not duplicate-body removal or a measured compile-time speedup.
The first build found the execution unit's missing direct smoothing import;
the explicit import fixes it. Claude accepts the fixed locality diff, and the
full ci build plus ci-vulkan/Clang 23 `ExtrinsicSandbox` compile/link satisfy
its consumer-visibility condition. The source is unchanged since fixed review
and the final 78 focused / full CPU results above. No Vulkan execution or
full sanitizer-suite run is implied by compile/link success.

Strict layering/test-layout, task policy/state, doc links/sync, skill mirrors,
session brief, root hygiene, ARA structure and workshop checks pass. Inventory
refreshed: 424 modules. Source-doc audit: zero errors, 17 reviewed hints; the
new enum retains the fail-closed contract, existing smoothing comments remain
outside the moved declaration. Workshop rows 1–3 pass, rows 4–6 not applicable,
row 7 retains UI-037's open readiness work, row 8 introduces no exceptions.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.|^Smoothing\.|^RegistrationDomains\.|^ProcessingCompilationLocality\.(MeshTopology|Registration)|^SandboxEditorUi\.(MeshDenoise|MeshTopology|MeshProcessing)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j4
```


## Property validation reuse and parameterization result locality — plan, 2026-09-18

Operator-directed duplication/compilation continuation from `bc6b1cc8a`, with
Codex owning this checkout/build and Claude reviewing fixed read-only packets.
UI-037's readiness acceptance remains open. Existing declared source-documentation
and processing-compilation-locality contracts cover these slices.

- Five point config validators repeat the same ordered property validation and
  diagnostic assembly. Reuse `ValidatePointPropertyRef` through one compiled
  non-template loop in the existing `Runtime.FeatureConfigCodecs.Detail.cpp` /
  `Runtime.PointConfigJson.hpp` owner. Keep all family-specific relationship
  rules and other families' different messages. Public tests add all-field kind
  and first-error coverage, run before and after the refactor.
- Parameterization's copied records and atlas status enums need no owning mesh
  or solver imports. Compiler baseline rejects eight intended cuts across the
  runtime interface and prepared frame. Move the sole canonical definitions to
  `Geometry.Parameterization.Types` and `Geometry.UvAtlas.Types`; original
  algorithms re-export them, runtime result producers import only types.
  No adapters, duplicate records or new runtime behavior. Keep evaluation options,
  algorithms and string functions with their existing owners.
- Claude approves the plan. Use checked JSON access with a documented merged-key
  precondition. Verify actual compiler closure and direct imports, full consumer
  build, focused and default CPU tests, Vulkan Sandbox compile/link, source/docs
  checks and final fixed-diff review. Keep reuse and locality in separate commits.
  Dependency removal alone establishes no elapsed compilation-time improvement.


Both slices are complete. Five diagnostic loops now share one compiled owner;
all seven affected production files, including the existing header/helper,
change from 3,288 to 3,282 physical lines. Six config tests pass before the
refactor and all 18 config-section tests pass after it. The added public cases
pin every field's kind and first-error order across all five callers.

The canonical parameterization records and atlas enums moved without field,
enumerator or default changes. Four compiler producers pass the new boundary
check. Seven production files including both new modules and CMake change from
977 to 995 physical lines; the increase buys a compilation boundary. Removed
one duplicate stable-token declaration. Algorithm implementations are unchanged.

Claude's fixed-diff review finds no blockers. All 11 private JSON-header
consumers already include `<utility>` before the header; adding a standard
header inside their named-module purview would violate its existing include
contract. Rejection's failure-path cost rationale remains at the existing
`SummarizeRejection` implementation, and the moved record retains its unevaluated
contract. The surviving stable-token declaration and definition are in the same
runtime module; algorithm re-exports and all consumer imports compile correctly.

Final combined-source verification: canonical ci/Clang 23 `IntrinsicTests`
build passes; 116 focused tests pass; full CPU gate passes 4,737 tests plus one
expected ASan-only GLFW lifecycle skip (4,738 selected; zero failures,
167.95 seconds). Canonical ci-vulkan/Clang 23 `ExtrinsicSandbox` compiles and
links. No GPU execution, full sanitizer-suite execution or measured build-time
speedup is claimed. Source hashes match the reviewed and tested snapshot.

Strict layering/test layout, task policy/state, doc links/sync (explicit changed
files), mirrors/session brief, root hygiene and workshop checks pass. Module
inventory refreshed to 426. Source-doc audit: zero errors, 11 inspected comment
hints; retained comments describe ordering, publication, copied/borrowed lifetime,
rejection state or existing solver contracts. Workshop rows 1–3 pass, rows 4–6
not applicable, row 7 retains UI-037 readiness work, row 8 adds no exceptions.
Logs/review packets: `/tmp/intrinsic-parameterization-reuse/`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.|Parameterization|UvAtlas' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j4
```


## Canonical config property vocabulary locality — plan, 2026-09-18

Operator-directed compilation/duplication continuation from `985c7671e`.
Codex owns this checkout and build; Claude reviews bounded read-only packets.
The existing processing-compilation-locality and source-documentation contracts
apply. UI-037 readiness remains open.

Fourteen config interfaces import live GeometryAvailability only to name
canonical property identities. Existing compiler records show 56 prohibited
paths across those interfaces to availability, property containers, ECS sources
and render components. Move the sole canonical value-kind enum to
Geometry.Properties.Types and domain/ref/filter declarations plus their compiled
pure functions to Runtime.GeometryProperty.Types. Configs import the small owner;
GeometryAvailability re-exports it for its existing live consumers. No duplicated
records, conversion adapters, algorithm changes, or token/default changes.

The new files establish concrete compilation boundaries for fourteen current
consumers. Claude's plan review highlights lost accidental transitive exports:
add direct imports where live consumers actually need them and build all consumers.
Keep property traits/storage in Geometry.Properties; preserve the domain ToString
and equality function bodies exactly. Add the compiler-record guard, run focused
and default CPU tests, build the Vulkan Sandbox, then review the fixed diff.
Dependency evidence alone does not establish an elapsed build-time improvement.


### Vec3-only serializer reuse — plan

Normal estimation, point construction and registration have seven current
bindings backed by three equivalent local JSON serializers. Their contract is
stricter than the existing generic encoder: only Vec3 emits a valid kind token.
Reuse the existing compiled config-codec owner with an explicitly named
EncodeVec3PointPropertyRef; keep generic serialization and all family validation
messages unchanged. Remove the three local bodies. Claude approves this bounded
reuse subject to public serialization regressions and existing owner/link checks.
The new test exercises every binding, every valid domain/kind, invalid enum values
and embedded NUL/quote/backslash/newline name bytes before and after consolidation.
Keep this semantic reuse separate from the property-vocabulary move commit.


The first compiler check found the shared codec still reached live availability
through ClusteringConfig -> ClusteringTypes. The latter also uses only property
identities, so its import now uses the canonical Types owner; the guard includes
ClusteringTypes and ClusteringConfig (32 producers total, fifteen config interfaces).
This fixes the observed dependency instead of excluding the shared codec.

ClusteringTypes' compiled command preflight and ClusteringModule execution still
need live resolution and now import GeometryAvailability directly; their public
records and config do not. The guard checks the type interface, not that preflight.


The expanded guard also found CurvatureSegmentationConfig.cpp intentionally
constructs algorithm parameters and calls IsValidSegmentationParams. That existing
adapter is outside the authoring-only boundary; retain its authoritative validator
rather than copy rules or split another algorithm in this slice. Guard its interface
and the eleven authoring-only config implementations (31 producers total). This
corrects the initial overly broad test selection; no existing gate is changed.


## Property vocabulary locality — verified, 2026-09-18

The sole canonical enum and runtime property identity now live in the two Types
interfaces. All six pure function bodies and enumerators are unchanged; live
availability/catalog/preflight owners retain their original responsibilities.
Fifteen config interfaces avoid the live source/property/render owners. The
31-producer compiler guard passes; its baseline and intermediate failures drove
the clustering correction and the documented algorithm-adapter boundary.

Claude accepts the fixed diff and final boundary correction. All combined-source
verification passes: canonical ci/Clang 23 IntrinsicTests build, 60 focused tests,
and 4,739 passed CPU tests plus the expected ASan-only GLFW lifecycle skip (4,740
selected, zero failures, 169.60 seconds). Final ci-vulkan/Clang 23 ExtrinsicSandbox
compile/link passes after all edits. This is not GPU execution or a full sanitizer
suite. Strict layering, test layout, task policy/state, doc links/sync, mirrors,
session brief, root hygiene and clean-workshop checks pass. Inventory: 428 modules.
Source-doc audit: 20 interfaces/headers, zero errors; 62 hints primarily flag
unchanged storage/availability comments. New declarations retain authoring and
optional-kind contracts. Workshop rows 1–3 pass, 4–6 n/a, row 7 keeps UI-037 open,
row 8 has no exceptions.

The locality slice spans 26 production files including three new units and CMake:
8,079 -> 8,076 physical lines. The small decrease comes from scoped comment
shortening alongside definition moves; it is not duplicate-body removal or a
measured compilation-time result. Source hashes match the reviewed/tested snapshot.
Logs and fixed Claude packets: `/tmp/intrinsic-property-contracts/`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.|^RuntimeGeometryAvailability\.|^ProcessingCompilationLocality\.|^RuntimeEngineLayering.NoDuplicateGeometryPropertyVocabularyRemains$|^SandboxEditorPresentation.DisabledActionReasonTooltipAppearsAfterTwoFrames$' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j4
```

## Vec3-only serializer reuse — verified, 2026-09-18

Normal estimation, point construction and registration now use the compiled
EncodeVec3PointPropertyRef in the existing private config helper. Three local
bodies are deleted; seven bindings keep exact domain/name/kind JSON and all
family-specific validation messages. The generic three-kind encoder stays
separate. Five production files, including the shared header/helper, change from
3,343 to 3,331 physical lines.

All 19 config tests passed before consolidation, including the new public test
of all seven bindings, valid/invalid domain and kind tokens, and embedded name
bytes. The combined final 60 focused tests and CPU/Vulkan compile checks above
pass afterward. Existing positive roundtrips cover all seven bindings across all
nine domains including Unknown, with complete serialized equality. Claude's
suggested positive coverage is already present; no redundant test was added.
The shared ordinary translation unit imports none of these three config modules,
and all callers already use its private declarations, so reuse adds no module
cycle or new layer edge.


## Consolidation result locality — verified, 2026-09-18

Operator-directed duplication/compilation continuation from `bc6da168d`;
Codex owns the checkout/build and Claude supplies read-only planning/review.
UI-037 readiness remains open. Existing source-documentation and
processing-compilation-locality contracts cover this slice.

The sole canonical consolidation status enum moves unchanged to
`Geometry.PointCloud.Consolidation.Types`. The algorithm re-exports it; runtime
records use it and the existing `Runtime.GeometryProperty.Types`. Parameters,
projection state, result arrays and status spelling retain their algorithm owner.
Four production files, including the new module and CMake, change from 747 to
758 physical lines; the increase buys a compilation boundary, not deduplication.

Compiler metadata exposed 28 candidate dependency paths across four existing
producers. Twenty are cut. Eight remain because EditorCommon/presentation and
private frame composition still consume live availability. The two new checks
therefore guard three record producers against both live sources and algorithms,
and two editor consumers against the consolidation algorithm and point/index
owners. Existing guards are unchanged; the initial overbroad check is retained
in the logs. No compatibility shim, duplicate status or new layer edge is added.

The locality-only source passed 116 focused tests. Claude found no code blockers;
canonical ci/Clang 23 IntrinsicTests and ci-vulkan ExtrinsicSandbox compile/link
pass on the final combined source. Inventory refresh produces 429 modules.
Logs and fixed review packets: `/tmp/intrinsic-consolidation-locality/`.


## Config getter reuse — verified, 2026-09-18

Eleven point/mesh config getters now reuse the compiled
`ConfigDetail::FindValidatedCanonicalPayload` in the existing private config
helper. Together with its five original callers, sixteen getters share one
lookup/schema/validation gate. Typed decoding, error text and schema ownership
stay local. A null serializer preserves the eleven validators' empty reference
argument; the original five retain lazy default serialization and fallback
rejection. The eleven validators only emit Invalid or Valid, so the strict Valid
gate preserves current behavior. The private header documents that future
fallback handling belongs to preview/apply, not getter decoding.

Claude endorsed the plan and fixed code diff. His two completion conditions are
satisfied: canonical full-consumer build passes, and Codex reviewed the omitted
429-module generated inventory and these intentional task notes. The first getter
build found curvature lacked the shared header; its include and explicit standard
prerequisites now follow the existing named-module pattern. No new helper file,
module, template, factory, compatibility path or cross-layer dependency is added.
Thirteen production files change from 4,094 to 4,097 physical lines (+3 for shared
declarations/includes and nullable reference support); the benefit is one owner
for repeated lookup logic, not fewer physical lines.

All 20 config tests, including the new eleven-family schema/error/recovery test,
passed before and after consolidation. Existing authored-value roundtrips and
strict shared-family fallback rejection remain. The combined source passes 117
focused tests and the default CPU gate: 4,742 passed, one expected ASan-only GLFW
lifecycle skip, zero failures (4,743 selected, 164.78 seconds). Canonical ci/Clang
23 IntrinsicTests and ci-vulkan/Clang 23 ExtrinsicSandbox compile/link pass. No GPU
execution, full sanitizer-suite run or elapsed compilation-time improvement is
claimed. Reviewed source/test/tool hashes match the tested source.

Strict layering, test layout, task policy/state, docs sync/links, skill mirrors,
session brief, root hygiene and workshop checks pass. Source-doc audit: four
interfaces/headers, zero errors, 17 inspected contract comments retained for
lifetime, validation, field ordering, fallback and numerical semantics. Workshop
rows 1–3 pass, 4–7 n/a (UI-037 remains open), 8 pass with no temporary exceptions.
Logs/reviews: `/tmp/intrinsic-consolidation-locality/`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxConfigSections\.|Consolidation|^ProcessingCompilationLocality\.|^SandboxEditorPresentation\.DisabledActionReasonTooltipAppearsAfterTwoFrames$' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j4
```

## Shared readiness and compiled test support — plan, 2026-09-18

Operator-directed duplication/compilation continuation from `731084f66`, with
Codex as sole checkout/build writer and Claude providing bounded read-only
planning and fixed-diff review. Existing source-documentation and processing
locality contracts apply; broad UI-037 readiness acceptance remains open.

- Replace eight unused resolved-config readiness records with the existing
  `ActionReadiness`. Keep the exact shared capture predicates and config-lane
  priority; construction retains its consumed resolved request. This removes
  duplicate records and panel conversions without another wrapper or service.
- Compile the existing `SandboxEditorJobHarness` implementation once in the
  runtime contract object target. Its 12 consumers retain the same fixture,
  scheduler teardown order, job identity, callbacks and drain behavior. Only
  the small generic context-to-command-surface adapter remains templated.
- Keep these as separate commits. Build runtime and panel consumers, run their
  focused tests then the default CPU gate, compile all tests and the Vulkan
  Sandbox, and run source/docs/structural checks. No elapsed compile-time claim
  follows from moving bodies or removing repeated records.

## Shared point-method readiness — verified, 2026-09-18

Eight previews now return the existing `ActionReadiness` directly: normal,
outlier, keypoint, descriptor, density-weight, kernel-density, spacing and
bilateral. Their unused resolved-config copies and eight duplicate records
are removed. Construction keeps its resolved request because its panel consumes
it. The panel passes the shared value directly into the existing config-lane
resolver; preflight, command validation and reason priority are unchanged.
Thirteen production files change from 6,558 to 6,508 physical lines. No new
module, dependency, compatibility wrapper or configuration format is introduced.

The existing normal-config test now exercises actual preview results through
the config-lane resolver and compares the invalid-neighborhood diagnostic with
command rejection. The spacing diagnostic assertions and seven GPU-smoke sites
use the shared fields. The first build caught two old spacing test member names;
the corrected rebuild passes. All 446 focused tests passed before and after the
separate job-harness move.

Claude's corrected fixed-packet review found no blockers; its compilation and
test conditions are satisfied by the recorded runs. The initial review mixed
in nonexistent source names and was superseded, not accepted as evidence.
Module inventory regeneration remains at 429 modules with no content change.
UI-037's broad readiness/action-inventory acceptance remains open.

Final combined verification: canonical ci/Clang 23 `IntrinsicTests` builds,
including the updated GPU smoke TU; all 446 focused tests pass, then the default
CPU gate selects 4,743 tests: 4,742 pass, one expected ASan-only GLFW lifecycle
skip, zero failures (148.23 s). Canonical ci-vulkan/Clang 23 `ExtrinsicSandbox`
compiles and links. This is not GPU execution or a full sanitizer-suite run.

Strict layering, test layout, task policy/state, docs sync/links, skill mirrors,
session brief, root hygiene and workshop checks pass. Source-doc audit has zero
errors: four production interfaces retain eight inspected synopsis/lifetime
comments; the new support source/header and README have zero review findings.
Workshop rows 1–3 pass, 4–7 n/a, 8 pass with no exceptions. Source/test hashes
match the reviewed/tested source, apart from the explicitly reviewed scheduler
import fix. Logs and review packets: `/tmp/intrinsic-normal-readiness/`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(NormalEstimation|OutlierAnalysis|KeypointAnalysis|DescriptorAnalysis|DensityWeight|KernelDensity|PointSpacing|BilateralFilter|PointConstruction|RegistrationDomains|SandboxEditor|SandboxProcessingPanels|ProcessingCompilationLocality)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j4
```

## Compiled editor-job test harness — verified, 2026-09-18

The existing `SandboxEditorJobHarness` now compiles its identity join, command
callbacks, snapshot/drain loops and scheduler lifecycle once in
`tests/support/SandboxEditorJobHarness.cpp`, added to `RuntimeContractTestObjs`.
All 12 existing consumers use the same fixture. Its context adapter remains a
one-line template, while scheduler teardown remains first and keeps borrowed
scene/context state alive. Six moved bodies match the original after whitespace
and parameter-name normalization. Symbol inspection confirms the implementation
comes from the support object instead of each consumer.

The smaller header exposed one previously implicit scheduler import in
`Test.SandboxEditorMeshMethods.cpp`; that caller now imports `Core.Tasks`
explicitly. Four affected source/build files, including the new helper and
CMake registration, change from 9,086 to 9,080 physical lines. The benefit is
compiling shared bodies once; source movement is not deduplication of distinct
algorithms, and no elapsed build-time improvement was measured.

Claude approved the fixed main diff; Codex reviewed the one-line explicit-import
fix and confirmed all 12 consumers compile/link. The runtime and panel tests
exercise publication, dedup, cancellation and attachment expiry before and after
the move. The routing tool's 19 synthetic tests pass.

The final combined builds, 446 focused tests and 4,742-passing CPU gate recorded
above include this slice. Live gate routing also passes for `IntrinsicTests`: 41
targets, 4,750 registered cases and 363 source files. No broad UI-037 acceptance
checkbox is closed by test-support locality.

```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```


## Registration readiness and compiled context support — plan, 2026-09-18

Operator-directed continuation from `0201e2709` to reduce duplication and
repeated compilation; Codex owns this checkout/build and Claude supplies
read-only planning and fixed-diff review. The existing source-documentation
and processing-locality contracts cover this slice. UI-037 stays open.

- Registration preview reuses `ActionReadiness` from `Runtime.EditorProcessing`.
  Its extra command status has no consumer. Keep `ApplyRegistrationChecked`
  as the validation owner and preserve config-lane priority; test empty success
  reasons, rejection-message agreement and side-effect-free preview.
- Move the non-template conversions and six geometry builders from
  `tests/support/EditorFeatureTestContext.hpp` to one compiled source. The
  existing test object-library helper supplies module scanning and matching
  flags; four existing runtime-dependent executables link that object. Leave
  header imports intact. Core-only `TestSupportObjs` gains no runtime edge.
  One small support target serves 23 direct consumer TUs; no production
  abstraction is added. Definitions would return inline only if required by
  templates or constant evaluation.
- Keep separate commits. Build canonical ci consumers, run focused registration/
  editor tests and the CPU gate, compile all tests plus Vulkan Sandbox and the
  GPU support consumer. Verify source docs, inventory, layering and test routing.
  Moving bodies establishes one compiled owner; it does not measure elapsed
  compilation improvement. Logs: `/tmp/intrinsic-registration-context/`.


## Shared registration readiness — verified, 2026-09-18

Registration now returns the existing `ActionReadiness` directly. Its unused
status record and panel conversion are removed; `ApplyRegistrationChecked`
still owns admission. Successful preview clears the disabled reason; invalid
parameter diagnostics match command rejection. The existing canonical-operands
test checks both cases through the config-lane resolver and verifies preview
does not change the source transform or invoke config callbacks. Three
production files change from 4,092 to 4,085 physical lines. No module, layer
edge, compatibility wrapper or config format is added.

Claude's fixed-diff review found no code blockers. Its documentation separator
fix is applied. The same 325 focused registration/editor/locality tests pass
before and after the separate context-support move. Canonical ci/Clang 23
`IntrinsicTests` builds, and the final combined default CPU gate selects 4,743
tests: 4,742 pass, one expected ASan-only GLFW lifecycle skip, zero failures
(148.27 seconds). Canonical ci-vulkan/Clang 23 `ExtrinsicSandbox` and
`IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` compile and link. This is
compile/link coverage, not GPU execution or a full sanitizer-suite run.

Module inventory regeneration remains at 429 modules without a content change.
Strict layering, test layout, task policy/state, docs sync/links, skill mirrors,
brief freshness, root hygiene and workshop checks pass. Source-doc audit has
zero errors; three retained comments state the narrowed ICP catalog, terminal
callback lifetime and explicit low-level test seam contracts. Workshop rows
1–3 pass, 4–7 n/a, 8 pass with no temporary exceptions. UI-037's broad
action-inventory and scan-free readiness acceptance remains open. Source/test
hashes match the reviewed/tested surface. Logs/reviews:
`/tmp/intrinsic-registration-context/`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j4
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(RegistrationDomains|RegistrationConfig|SandboxEditor|SandboxProcessingPanels|ProcessingCompilationLocality)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j4
```


## Compiled editor context support — verified, 2026-09-18

Seven non-template context methods and six geometry builders now compile once
in `tests/support/EditorFeatureTestContext.cpp`, using the existing object-library
helper as `EditorFeatureTestSupportObjs`. The header retains its data, imports,
constant and declarations. All 13 bodies match their originals after whitespace
normalization, including borrowed-cache invalidation, command binding and
property/topology initialization.

The source registry confirms 23 direct consumers: 19 runtime contract, one
editor integration, two runtime graphics and one Sandbox GPU smoke source. All
four executables compile/link in ci; the GPU smoke also links in ci-vulkan.
Compiler metadata has one support action, and symbol inspection finds all 13
definitions in that object and none in the 23 consumer objects. The header
shrinks from 342 to 162 lines; the complete source/build set, including the new
source and CMake, grows from 3,051 to 3,098 lines. The extra declarations and
build entry buy compilation locality, not a physical-line reduction or a
measured elapsed speedup. Core-only `TestSupportObjs` remains runtime-free.

Claude's fixed-packet review found no blockers conditional on the four links;
those links passed. Codex also reviewed the final CMake comment placement. The
325 focused tests, full 4,742-passing CPU gate and structural checks recorded
above cover the final combined source. Test routing passes its 19 synthetic
tests and the live aggregate: 41 targets, 4,750 cases, 363 test sources. No
UI-037 acceptance checkbox is closed by this test-only compilation change.

```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

## Compiled RHI mock support — verified, 2026-09-18

The operator explicitly continued duplication and compilation-locality work with
Claude. This companion test-support slice follows the compiled editor context;
UI-037's action inventory and operational acceptance remain open. The standing
Framework24 focus is unchanged. Catalog review adds no contract ID: the existing
`repo.source-documentation` declaration covers this header/documentation change;
no production geometry, runtime, config or backend contract changes.

Reuse discovery found one existing mock owner, `tests/support/MockRHI.hpp`,
with 53 direct source consumers across nine executable targets. No support
header, named module, or additional conditional source includes it. The new
`MockRHI.cpp` compiles 56 existing method bodies through the existing
`intrinsic_test_object_lib` helper, with `ExtrinsicRHI` and `ExtrinsicCore`
dependencies. Each of the nine executables explicitly links
`MockRhiTestSupportObjs`; core-only `TestSupportObjs` stays independent of RHI.
No generic helper, wrapper, alternate mock implementation or production surface
is introduced. Special members, layout, includes/imports, default arguments,
constant evaluation, small accessors and no-op methods remain unchanged.

Claude reviewed the plan and fixed diff read-only. Its conditional-consumer
check is satisfied by the repository-wide include audit. Its routing concern
revealed an imprecise README sentence, now corrected: support sources do not
register test cases, and touched-scope planning conservatively selects the broad
route. A trailing-space finding is fixed. Removed historical comments are
replaced by the non-obvious payload indexing, failure-control and buffer-read
contracts; picking-specific byte layout remains the consumer's responsibility.

All 56 moved bodies match after whitespace/comment normalization; reinserting
them reconstructs the original header tokens. Compiler metadata contains one
support compilation, and symbol inspection finds the 56 definitions and four
vtables in that object and none in the 53 consumer objects. The header changes
from 1,179 to 575 lines; the complete header/source/CMake set grows from 3,893
to 4,005 lines. Added declarations/build wiring buy compilation locality;
this is not source-line reduction or a measured elapsed build-speed claim.
Logs, body comparisons, consumer mapping and reviews: `/tmp/intrinsic-mock-rhi/`.

Verification uses canonical ci with Clang 23. The aggregate builds and all nine
consumer executables link. Before and after the move, the default CPU gate
selects 4,743 tests: 4,742 pass, one expected ASan-only GLFW lifecycle skip,
zero failures (149.50 seconds before, 150.31 after; these are test execution
observations, not compilation measurements). The final whitespace correction
also rebuilds and relinks all nine consumers successfully. No GPU execution,
full sanitizer-suite run or elapsed compilation comparison was performed.
Routing passes 19 synthetic checks and live reconciliation (41 targets,
4,750 cases, 363 test sources). Strict layering, test layout, task policy/state,
docs sync, links, root hygiene, skill mirrors and session-brief checks pass.
The touched source-documentation audit has zero errors and zero review findings.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
python3 tools/ci/touched_scope.py --root . --changed-file tests/support/MockRHI.cpp --preset ci --preset-build-dir build/ci --build-dir build/ci --print
```

After the whitespace-only correction, all 1,906 focused cases pass (1,903
consumer-executable cases plus three cases sharing those suites, 51.42 seconds).
The pattern is retained in `/tmp/intrinsic-mock-rhi/focused-pattern.txt`.

## Compiled point-neighborhood and job-lookup reuse — 2026-09-18

Operator-directed continuation with Claude from clean `d341af47a` on
`codex/mesh-field-diagnostics-locality`. Codex owns this checkout/build; Claude
provides read-only planning and fixed-diff review through the configured CLI.
The standing Framework24 focus and UI-037's remaining acceptance criteria stay
unchanged. Catalog review retains the existing source-documentation, element-
domain and processing-compilation-locality contracts; no new contract or
public module surface is introduced.

Reuse discovery found nine point-method submissions repeating the optional
`JobCommands.FindActive` callback guard already owned by the compiled
`MeshSupport::FindActiveEditorJob`. They now call that helper while retaining
`IsActiveEditorJobState`, their typed output identities and original messages.
Registration and mesh-family callers keep their existing semantics. A public
kernel-density command test covers all eleven job states, an absent callback
and an empty record; existing per-family queued-job tests cover duplicates,
staleness, cancellation and delivery.

Density, spacing and local-distance-ratio outliers now share `AppendPointKnnRows`
in the existing ordinary `RadiusRows.cpp` owner, declared privately with C++
linkage. The three adapters keep their width floors, config gates, kernels,
publication and requested/actual backend reporting. The helper uses each
caller's existing immutable spatial-index snapshot, complete Euclidean point
kNN rows with self candidates, query order and compact indices. It preserves
the diagnostic and partial-row failure behavior. Statistical outliers exclude
self and construction has batching/cancellation/reference differences, so both
stay separate. Radius support and GPU continuation code are unchanged. Review
of the spatial consumer inventory found no missing query capability for this
slice. No new module, source file, target or lifecycle template is needed.

Claude found no implementation blocker and identified insufficient wide-row,
coincident-point and nonidentity source-row coverage. The revised reference
comparisons cover k=1/2/63, all eight domains for density/spacing, small-input
clamping, and 70-row point clouds with coincident samples and interior deletion.
The latter retain 68 live rows for density/spacing and 69 for outliers, so k=63
actually exercises 64-candidate rows. The optional additional construction
terminal-state matrix is deferred; its production substitution retains its
existing state predicate and identity, with the current job tests rerun.

Compiler metadata has one `RadiusRows.cpp` action. Symbol inspection finds one
`AppendPointKnnRows` definition in that object and references from the three
consumer objects. Across all 11 changed production files, physical lines are
4,338 before and 4,339 after: the new declaration and readable compiled owner
offset deleted duplicate bodies. This is mechanism consolidation and compilation
locality, not total line reduction or a measured elapsed build-speed claim.

Verification: canonical ci/Clang 23 builds the focused runtime target and
`IntrinsicTests`; all 162 focused tests pass after the review fixes. The full CPU
gate selects 4,744 tests: 4,743 pass, one expected ASan-only GLFW lifecycle skip,
zero failures (147.93 seconds of test execution, not compilation). Claude's final
read-only test review accepts the coverage repairs and has no blockers. Strict
layering, test layout, task policy/state, docs sync/links, root hygiene, skill
mirrors and brief checks pass. Live routing reconciles 41 targets, 4,751 cases
and 363 sources. The source-doc audit has zero errors; its three declaration-
comment prompts were reviewed and retained for self/row completeness, partial
failure and main-thread/cancellation contracts. Architecture review preserves
layer/target ownership, existing C++ linkage and immutable query borrows;
renderer/recipe/scaffold changes are not applicable, with no new exceptions.
Logs and fixed review packets are in `/tmp/intrinsic-reuse-next/`. No sanitizer-
suite or GPU-execution claim; UI-037 remains active.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(KernelDensity|PointSpacing|OutlierAnalysis|DensityWeight|Keypoint|Descriptor|Bilateral|NormalEstimation|PointConstruction|ProcessingCompilationLocality)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

## Compiled geometry test fixtures — 2026-09-18

Operator-directed duplication/compile-locality continuation with Claude from
clean `eb3a6ba2a`; Codex owns this checkout and build. UI-037 remains active and
its readiness acceptance criteria are unchanged. Catalog review retains the
existing source-documentation contract; this companion slice changes only tests.

Reuse discovery identified `tests/support/geometry/Test_MeshBuilders.h` as the
existing owner used by 29 test sources. Move its 15 non-template bodies unchanged
to `geometry/MeshBuilders.cpp`, compiled by `GeometryMeshBuilderTestSupportObjs`
and explicitly linked into the three consumer executables. Preserve signatures,
defaults and global-module attachment. The existing production tetrahedron and
icosahedron builders remain the two fixture wrappers' owners. No geometry
algorithm or production module changes. Keep geometric contracts in the header;
correct its false interior-vertex and five-vertex quad-pair descriptions.

The initial narrow-import build required an explicit `Geometry.Properties`
import for vertex/face handles; adding it fixes the compilation error. All 15
bodies then compare byte-for-byte against the baseline. Compiler/symbol inspection
finds one support action and 15 definitions, with none in the 29 consumer objects.
The header shrinks from 291 to 52 lines; the new implementation has 232 lines
and CMake adds six net lines. This relocates compilation; it does not establish
an elapsed build-time improvement.

Verification: all three executables compile/link with canonical ci/Clang 23;
all 1,578 consumer CTest cases pass, including the four opted-in slow cases.
The full exclusion-only CPU gate passes 4,743 tests with one expected ASan-only
GLFW lifecycle skip (4,744 selected, 148.87 seconds of test execution). Claude's
fixed-diff review and resolution have no remaining concerns. The wider ODR check
covers all 106 objects in the three executable groups plus both support groups:
only the new support object defines these 15 global functions. Final review also
clarified the diamond's diagonal and restored the two alias equivalence comments.
The source-doc audit has zero errors; 15 reviewed declaration comments describe
fixture geometry/equivalence. No production layering, runtime wiring, config,
GPU path or public module changes; no sanitizer or GPU evidence is claimed.
Logs, exact test selection, body/symbol comparisons and Claude packets:
`/tmp/intrinsic-construction-mesh-reuse/`.

Final comment-only rebuild of `IntrinsicTests` and all 1,578 focused cases pass
again. Test routing passes 19 self-tests and reconciles 41 targets, 4,751 cases
and 363 test sources; support-source edits retain the conservative broad route.
Test layout, task policy/state and docs sync pass in strict mode; links, root
hygiene, skill mirrors, session-brief and diff checks also pass. Scope is one fixture-compilation
move; layer/public-surface/renderer/maturity/exception changes are not applicable.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

## Shared point-index admission — 2026-09-18

Operator-directed continuation with Claude from clean `8d48f3759`; Codex owns
this checkout/build and Claude reviews read-only. The Framework24 focus and
UI-037 readiness acceptance remain unchanged. Existing declared source-doc,
processing-locality and element-domain contracts cover this slice.

Reuse/right-sizing review identifies nine identical cache acquire/lease/match
sequences. One private free function, `AcquirePointIndex` in the existing
`RadiusRows.cpp`, replaces those mechanisms without a new module, source, target,
context wrapper or lifecycle template. Adapters keep typed backend predicates,
status mapping, exact mismatch text and publication freshness. Bilateral's
zero-iteration bypass, topology-normal bypass, outlier removal and registration's
transformed-target contract stay explicit. Acquisition failure preserves lease
outputs; mismatch retains the acquired handle, immutable snapshot and reuse flag.
No neighborhood semantics or missing spatial-query capability changes.

The helper's small tri-state return avoids a result wrapper plus repeated lease
assignments. Public density command regressions cover acquisition failure,
coordinate mismatch and equal-count source-row mismatch, including cache reuse
and unchanged output values/revisions. Existing family and Vulkan workflows
remain the integration checks.

Claude's fixed-diff review found no blockers. Its optional move/copy nit retains
the prior acquired-diagnostic copy semantics; callers ignore diagnostics on ready
or mismatched outcomes, so no expanded comment is needed. Reviewed source-doc
prompts retain only the lease-output, self/row-completeness and main-thread
contracts. Every adapter reconstructs the baseline exactly outside the replacement
block and three includes; all mismatch strings byte-match. One compiler action
and symbol inspection show one helper definition and nine consumer references.
The 11 production files grow from 4,339 to 4,374 physical lines: the declaration,
compiled owner and readable call sites consolidate the mechanism but do not reduce
total source lines. No elapsed compilation speedup is measured or claimed.

Architecture review preserves runtime ownership, C++ linkage and existing cache
leases. The ordinary owner explicitly imports the narrow WorldHandle type and
includes entt's entity declaration. Layer and target checks pass; public surface,
renderer/pass/recipe, maturity-closure and exception changes are not applicable.
Logs and the fixed review packet are in `/tmp/intrinsic-index-admission/`.


Verification uses canonical ci/Clang 23: `IntrinsicTests` builds, all 164 focused
cases pass, and the full exclusion-only CPU gate selects 4,746 cases (4,745 pass,
one expected ASan-only GLFW lifecycle skip, zero failures). The ci-vulkan target
builds with the preset's ASan+UBSan instrumentation; all 20 selected GPU/Vulkan
integration cases execute and pass, including all nine adapters and both
construction workflows. This is focused sanitizer-backed Vulkan coverage, not
full CPU sanitizer-suite coverage. No capability maturity or benchmark claim is
added. Test routing reconciles 41 targets, 4,753 cases and 363 sources. Strict
layering, test layout, task policy/state and docs sync pass; doc links, root
hygiene, skill mirrors, brief and diff checks pass. Source-doc audit has no errors;
its four declaration-comment prompts retain the contracts described above.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(KernelDensity|PointSpacing|OutlierAnalysis|DensityWeight|Keypoint|Descriptor|Bilateral|NormalEstimation|PointConstruction|ProcessingCompilationLocality)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests -j4
ctest --test-dir build/ci-vulkan --output-on-failure -R '^(PointLBVHGpuSmoke\.(Normal|Outlier|LocalDistance|KernelDensity|PointSpacing|Bilateral|Keypoint|Descriptor|DensityWeight)|PointConstructionGpuSmoke\.)' -L gpu -L vulkan --no-tests=error --timeout 180
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

## Compiled runtime test lifecycle — plan, 2026-09-18

Operator continuation with Claude from clean `85d5331d7`. Codex owns edits
and builds; Claude reviews read-only. The source-documentation contract already
declared above applies. UI-037 remains open; this supports its existing test
harness compilation cleanup without changing readiness or product scope.

`RuntimeTestModule.hpp` is included by 47 test sources. Move non-template
registration, companion lifecycle and kernel shutdown bodies into one
`RuntimeTestModule.cpp`, following the existing test-support object pattern.
Keep templates, trivial accessors and required module imports in the header.
The generated source registry identifies ten executable consumers; list the
support object explicitly in each and define it before the early LBVH target.
No production change, new wrapper, generic build helper or runtime API change.
Reintroduction of inline bodies would require a demonstrated need at a caller.

Preserve lexical hook/resolve ordering, per-boot shutdown-latch reset, virtual
fixture dispatch, quiescence before fixture teardown and production-service
availability during teardown. Existing `RuntimeModule` and kernel-event
contracts cover these behaviors; all five baseline lifecycle cases pass.
Claude endorses this extraction and recommends leaving the seven-consumer
graphics leaf helpers inline because their extra wiring adds little value.
No elapsed compilation speedup is inferred from this source change.

### Runtime lifecycle support — verified checkpoint

Claude's fixed-diff review found no blockers. Preserve current ordering rationale
in the implementation and README; removed historical comments are not needed
in the declaration surface. All moved control-flow bodies retain their existing
statements. No tests or production behavior were changed. The compiler database
contains one support-source action; symbol inspection finds both shared module
vtables and registration in that object and none in the 47 consumer objects.
Each of the ten real Ninja link commands contains the support object exactly
once. The test-case registry intentionally excludes support sources.

The header shrinks from 158 to 94 physical lines; its new implementation has
84 lines. Together with five added CMake lines, the source/build total grows by
25 lines to provide declarations and one compiled owner. Production source is
unchanged. This removes repeated compilation, not source-line count, and makes
no elapsed build-speed claim. Tests README documents the required object link.

Canonical `ci` configure and `IntrinsicTests` build pass with Clang 23. All 11
focused runtime lifecycle/kernel-event tests pass. The full exclusion-only CPU
gate selects 4,746 cases: 4,745 pass, one expected ASan-only GLFW lifecycle
skip, zero failures (155.27 s). All five Vulkan-consuming executables build in
`ci-vulkan`; one integration case per executable executes and passes under
ASan+UBSan, no skips (43.88 s). This is focused sanitizer-backed Vulkan coverage,
not full CPU sanitizer-suite evidence.

Routing reconciles 41 targets, 4,753 cases and 363 case-owning sources. Strict
layering, test layout, task policy/state, docs sync, doc links, root hygiene,
skill mirrors, session brief, diff and clean-workshop automation pass. The
source-documentation audit reports zero errors/review findings for the two
support files. Manual architecture/workshop rows preserve downward test
dependencies and global-module type ownership; renderer state, pass/recipe
order, maturity closure and exceptions are unchanged/not applicable.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(RuntimeModule|RuntimeKernelEvents|KernelEvents)\.' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicRuntimeClusteringServiceGpuSmokeTests IntrinsicRuntimePointCloudConsolidationGpuParityTests IntrinsicPointLBVHGpuTests -j4
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^(DefaultRecipeSurfaceGpuSmoke\.RecipeSelectorReachesOperationalVulkanCommandStream|RuntimeSandboxAcceptanceGpuSmoke\.ExtrinsicSandboxDefaultConfigPresentsReferenceTriangleAtFrameCenter|ClusteringServiceGpuSmoke\.VulkanExecutionMatchesCpuReferenceAndCommitsCanonicalProperties|PointCloudConsolidationGpuParity\.VulkanAutoProcessesChildMeshPositionsAndPublishesDisplacement|PointLBVHGpuSmoke\.FramedKNearestReusesBuffersAndRejectsStaleTarget)$' --no-tests=error --timeout 180 --parallel 1
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

Logs, fixed Claude packets and compiled-owner inspection are retained under
`/tmp/intrinsic-compiled-test-support/`. UI-037's broader readiness inventory
remains open. Start the next session from this checkpoint, not the full history.

## Compiled config parser — plan, 2026-09-18

Operator-directed compilation/reuse continuation with Claude from `b95993ced`.
Codex owns edits/builds; Claude provides read-only planning and fixed-diff review.
The existing source-documentation and processing-compilation-locality contracts
apply. This slice leaves the broader UI-037 readiness inventory open.

Eleven config implementations already using `Runtime.PointConfigJson.hpp` each
instantiate JSON parsers for string and string-view inputs. Reuse that private
header and its existing compiled owner, `Runtime.FeatureConfigCodecs.Detail.cpp`,
for `ConfigDetail::ParseConfigJson`. The same parser now consumes a string view;
its failure policy is explicit at every call (false for untrusted validation,
true for generated defaults and validated payloads). Keep schema, validation,
merge order, diagnostics and typed decoding with their current owners. There
are no new modules, targets, templates or public APIs. Geodesics remains separate
because it does not consume the property helper; avoid widening its imports.

This private function is justified by eleven existing consumers and repeated
parser-template compilation, not by reducing forwarding lines. Baseline objects
each define sixteen parser symbols across two input-adapter instantiations.
Verify removal from the consumers and one compiled owner after the build; do not
infer elapsed compilation speedup. Existing public config contracts gain exact
input-range and malformed/trailing/embedded-NUL coverage. Run config integration,
compilation-locality, full canonical CPU and touched structural checks. Planning
review rejected shared prepared-frame templates and bound-context wrappers:
those candidates would increase coupling for little or no compilation benefit.

### Config parser — verified checkpoint

Claude approved the fixed production diff without blockers. Its coverage notes
led to an additional public regression for mesh-curvature rejection and
clustering fallback, including bounded input views. All eleven consumer edits
invert exactly to their baseline outside the parser-call substitutions. Object
inspection finds no parser definitions in any consumer; the existing shared
owner contains one parser adapter and one strong `ParseConfigJson` definition.
The thirteen production files total 4,097 -> 4,106 lines (+9); this consolidates
compilation work, not source-line count. No elapsed speedup or final-binary-size
claim is made. Docs and the reuse route identify the compiled owner.

Canonical `ci` configures with Clang 23 and `IntrinsicTests` builds. The full
exclusion-only CPU run selects 4,746 cases: 4,745 pass, one expected ASan-only
GLFW lifecycle skip, zero failures (149.59 s). After adding the review-requested
test, the affected integration executable rebuilds and all 55 config/locality
cases pass. Production code is identical across these runs. Routing reconciles
41 targets, 4,754 cases and 363 test sources. No GPU continuation, backend or
numerical algorithm changed; no Vulkan or sanitizer-suite evidence is claimed.

Strict layering, test layout, task policy/state, docs sync (explicit changed
files), doc links, root hygiene, skill mirrors, session brief and diff checks
pass. Source-documentation audit has zero errors; its five pre-existing comment
prompts retain required default/validation order and decoding contracts. Review
preserves runtime ownership and global C++ linkage. No new dependency edge,
public surface, recipe/pass, maturity closure or policy exception is involved.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(SandboxConfigSections|ProcessingCompilationLocality)\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

Review packets, parser-object inspection and logs are retained under
`/tmp/intrinsic-reuse-continuation/`. This is a clean session boundary; read this
checkpoint rather than the full task history. UI-037 remains open.

## Compiled config serialization — verified, 2026-09-18

Operator-directed reuse/compilation continuation with Claude from `83c1326aa`.
Codex owns edits/builds; Claude supplied read-only planning and fixed-diff review.
The existing source-documentation and processing-compilation-locality contracts
apply; broader readiness acceptance stays open.

Reuse discovery found twelve default JSON dump calls in eleven consumers of
`Runtime.PointConfigJson.hpp`, plus five calls in its compiled codec owner.
All now use `ConfigDetail::SerializeConfigJson(const nlohmann::json&)` beside
the parser in `Runtime.FeatureConfigCodecs.Detail.cpp`. Family-owned schema
construction, typed decoding and validation remain unchanged. No new target,
module, dependency edge or public API is needed. Other serializers retain their
owners and formatting contracts.

Object inspection finds zero serializer/dump definitions and one helper
reference in each consumer. The shared owner contains twelve serializer-related
definitions and one strong helper definition. Consumer edits invert to baseline
tokens; the owner inverts byte-for-byte after removing the helper and its five
call substitutions. Thirteen production files total 4,106 -> 4,111 lines (+5).
This consolidates repeated compilation; no elapsed-speed or binary-size claim.

Claude found no blockers; its whitespace note is fixed. Public regressions pin
compact raw UTF-8 and slash/quote/backslash/newline/NUL encoding in all eleven
families, plus invalid-UTF-8 termination through one consumer. All three encoding
tests pass before and after the production change. The first test draft expected
exceptions, but these sources use `-fno-exceptions`. A redundant 33-death draft
then timed out while Apport processed crashes. One shared-policy death check
retains the required coverage without changing any timeout or runtime policy.

Canonical `ci`/Clang 23 configures and `IntrinsicTests` builds. All 59 focused
config/locality cases pass. Full CPU: 4,748 selected, 4,747 passed, one expected
ASan-only GLFW lifecycle skip, zero failures (151.73 s of test execution).
After the whitespace fix, all 22 config cases pass again. Routing reconciles
41 targets, 4,755 cases and 363 sources. No GPU or sanitizer-suite run is claimed.
Strict layering, test layout, task policy/state, docs sync, doc links, root
hygiene, skill mirrors, session brief and diff checks pass. Source-doc audit has
zero errors; five existing declaration comments retain required validation and
decoding contracts, and its large-file prompt covers the shared codec owner.
No module inventory, recipe change, policy exception or maturity closure is owed.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(SandboxConfigSections|ProcessingCompilationLocality|ConfigCompilationLocality)\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

Logs, symbol inspection and Claude packets: `/tmp/intrinsic-config-serialization/`.
This is a clean session boundary; continue from this checkpoint. UI-037 remains open.


## Shared config rejection — plan, 2026-09-18

Operator-directed reuse/compilation continuation from `ef97deb04`. Codex owns
this checkout and builds; Claude supplies bounded read-only planning and review.
The existing source-documentation and processing-compilation-locality contracts
cover this slice; UI-037's broader readiness acceptance remains open.

Eleven consumers of `Runtime.PointConfigJson.hpp` repeat a capturing rejection
lambda: append one `InvalidValue` diagnostic to a fresh result, then copy that
result. Reuse discovery found no existing helper for this exact failure shape;
the shared codec owner's fallback/merge diagnostics have different semantics.
Add `ConfigDetail::RejectConfigSection(subject, message)` in the existing private
header and compiled codec owner; replace all eleven lambdas with direct calls.
Keep family messages, validation order, success fields and fallback codecs intact.
This needs no new target, file, module, dependency or public API. The concrete
eleven callers justify a free function; no factory or generic policy is needed.

Claude verified every rejection precedes any result mutation. Extend existing
public tests to pin explicit Invalid state and the full curvature failure shape,
then run them before and after the change. Build the focused integration target
and all `IntrinsicTests`; run config/locality cases and the full CPU selector,
plus applicable structural checks. Inspect consumer objects for the removed
diagnostic-vector insertion/copy definitions and the shared owner's definition.
This verifies a compiled owner, not elapsed compilation speedup. Evidence and
fixed review packets: `/tmp/intrinsic-config-rejection/`.


## Shared config rejection — verified, 2026-09-18

All 91 rejection paths across the eleven codecs now call the existing compiled
owner's `RejectConfigSection`; duplicate capturing lambdas are removed. Consumer
changes invert to baseline tokens with every message unchanged. The thirteen
production files total 4,111 -> 4,098 physical lines (-13), including the helper,
declaration and local using declarations. Each consumer object has one shared
helper reference and no diagnostic-vector insertion or copy-constructor definition;
the shared owner has one strong helper definition. No elapsed-build-speed or
binary-size improvement is claimed.

Claude's fixed-diff review found no blockers; its local-using and string-alignment
readability suggestions are applied. Three strengthened public tests passed on
baseline production, and all 59 focused config/locality cases passed after the
extraction. The first full run rejected five stale dependency scans because the
readability edits happened during CTest. This was an agent verification-order
error, resolved by rebuilding all `IntrinsicTests` and rerunning against frozen
source; no gate was changed. Final canonical ci/Clang 23 CPU run: 4,748 selected,
4,747 passed, one expected ASan-only GLFW lifecycle skip, zero failures (150.70 s
of test execution). No GPU or sanitizer-suite execution is claimed.

Strict layering, test layout, task policy/state, doc links, docs sync, root
hygiene, skill mirrors, session brief and diff checks pass. Routing reconciles
41 targets, 4,755 cases and 363 sources. Source-doc audit has zero errors; its
five existing declaration comments document required contracts and its size
prompt names the coherent shared codec owner. No public module surface changed.
The architecture doc and reuse route identify the common failure owner.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(SandboxConfigSections|ProcessingCompilationLocality|ConfigCompilationLocality)\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

Evidence: `/tmp/intrinsic-config-rejection/`. This is a verified session boundary;
UI-037 stays open and none of its broader readiness checkboxes is closed here.


## Canonical editor job snapshots — plan, 2026-09-18

Operator direction continues duplicate-code and compilation cleanup with Claude.
This companion slice removes the duplicate `EditorJobModel` and
`EditorJobDependencyModel`, using the existing value-owned `EditorJobRecord` and
`EditorJobDependency` in presentation and UV snapshots. The compiled
`ToEditorJobModel` adapter copies every field without adding validation, ownership
or filtering; delete it and update runtime/app consumers to the canonical field
names. No compatibility alias, new file, dependency edge or scheduler change.

Reuse/right-sizing: the owner is `Runtime.EditorJobProjection.cppm`. The two
producer paths construct models only from actual records, so the old model's
unused `Queued` default does not replace the record's `Invalid` state. Empty
collections and absent UV jobs remain empty; active-job selection order and
independent copied strings/dependencies remain unchanged. A separate projection
would be justified only by a real difference in payload or presentation semantics.
This changes existing runtime/app surfaces only; applicable catalog contracts
remain declared in front-matter. No broader readiness checkbox closes here.

Claude Sonnet reviewed the bounded plan. Resolve its default-state concern with
the caller audit and baseline public snapshot coverage; verify all field renames
by compiling both runtime and app consumers. Extend the existing snapshot test
for all fields, invalid state and copy independence; retain the real UV-job/cache
lifecycle test. Configure canonical ci/Clang, run focused tests then full CPU
verification, strict structural checks, module inventory and a fixed-diff review.
No elapsed compilation-speed claim is made.

The first fixed-diff implementation passed 34 focused UI, UV lifecycle and
editor-locality tests. A second, separately reviewed refinement deletes the
single-use append adapter: the newly constructed presentation model now takes
the callback's owned vector directly, preserving one callback call, ordering
and the absent-callback case. Claude Sonnet found both changes safe. Its
suggested empty-vector comment/assert is unnecessary beside the freshly created
model; the required module synopsis is retained. Final combined-source gates
follow before commit.


### Canonical editor job snapshots — verified checkpoint

Removed both duplicate model records, the compiled field-copy adapter and the
single-caller append helper. Nine affected production files total 13,777 ->
13,712 physical lines (-65); no replacement file or alias. The canonical record
now serves queue, inspector and UV snapshots. Public tests preserve every field,
invalid state, nested dependency/identity copy independence and real queued UV
state transitions. The producer's returned vector transfers directly into the
fresh presentation model. No module import, layer policy, scheduler or backend
behavior changed. No elapsed compilation-speed or binary-size claim is made.

Claude Sonnet's fixed-diff review and final vector-transfer review found no
blockers. Two public tests passed on baseline production; 34 focused UI/UV and
editor-locality cases passed both before and after the last refinement. Final
canonical ci/Clang 23 IntrinsicTests build and CPU gate: 4,748 selected, 4,747
passed, one expected ASan-only GLFW lifecycle skip, zero failures. No GPU or
sanitizer-suite execution is claimed. Source remained frozen during verification.

Strict layering/test layout/task policy and state links, doc links, docs sync,
root hygiene, skill mirrors and session brief checks pass. Module inventory was
regenerated with no content change. Routing reconciles 41 targets, 4,755 cases
and 363 sources. Source-documentation audit: zero errors; four existing file-size
review prompts and one unrelated historical-comment prompt remain outside this
bounded deletion. Architecture documentation names the shared record owner.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.(GeometryPresentation|UvRegeneration|ActionReadinessDerivesDomainPrerequisiteReasons)|^SandboxEditorPresentation\.DisabledActionReasonTooltipAppearsAfterTwoFrames$|^EditorCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
```

Evidence: `/tmp/intrinsic-job-record/`. UI-037 remains open; no readiness
acceptance criterion is closed by this companion cleanup. Start a fresh session
after this checkpoint to keep subsequent context bounded.


## Canonical property-option snapshots — verified, 2026-09-18

Operator-directed reuse/compilation continuation from `7c28a7dff`; Codex owns
source/builds and Claude Sonnet reviewed the plan and fixed diff. Both editor
option vectors now reuse `GeometryPresentationPropertyOption` from the already
imported `Runtime.GeometryPresentation` owner. Removed the editor-only record,
redundant `ActualValueKind`, converter and two conversion loops; app readers use
`Property`. The value-owned record preserves ordering, compatibility, disabled
reasons and the enumerator's source-generation field (default zero here).
No new file, dependency, alias, config state or policy exception. A separate
record would require distinct presentation or ownership semantics.

Four production files total 9,104 -> 9,059 physical lines (-45). The rebuilt
snapshot object contains none of the duplicate type's 73 baseline symbol entries.
No elapsed compilation-speed or binary-size improvement is claimed. The existing
source-documentation contract covers the interface; UI-037's broader readiness
acceptance remains open. Architecture documentation and its owner mapping are
updated; regenerated module inventory has no content change.

The public snapshot regression passed on baseline production and now checks the
canonical fields in both option vectors, ordering and independent copied strings.
Final review added an empty-copy assertion before element access. Claude's import
concern is resolved by the existing direct import and successful module/app builds.
Canonical ci/Clang 23 builds both focused targets and `IntrinsicTests`: 29 focused
cases pass; full CPU selects 4,749, with 4,748 passed, one expected ASan-only GLFW
lifecycle skip, zero failures (165.41 s). No GPU or sanitizer-suite run is claimed.
Source/tests stayed frozen during final gates; evidence includes their hashes.

The task's Verification commands pass. The focused run additionally selects
`PropertyOptions`, `PropertyCatalog`, `GeometryPresentation`,
`SurfacePropertySelector`, `SelectedModelCacheInvalidatesOnGeometryPresentationRecipeGeneration`
and `EditorCompilationLocality` cases. Strict layering/test layout/task policy
and state, docs sync/links, root hygiene, skill mirrors and session brief checks
pass. Routing reconciles 41 targets, 4,756 cases and 363 sources. Source-doc audit:
zero errors, three existing large-file prompts. Exact commands, logs, symbols and
Claude packets: `/tmp/intrinsic-property-options/`. Continue in a fresh session;
do not repeat this completed option-record slice.


## Shared property-selector requirements — 2026-09-18

Operator-directed reuse continuation from `29b8a0138`. Claude Sonnet and source
review found identical domain/type fallback resolution in the property catalog
and presentation-slot builders. Both now use the file-local
`ResolveGeometryPresentationSlotSelector`; the redundant options wrapper is
removed. Authored domain/kind take precedence; unresolved domains keep empty
options. No public API, import, ownership, CMake or algorithm change. Separate
rules would be warranted only if the two views acquire different semantics.
The production implementation shrinks from 3,250 to 3,233 lines (-17), without an
elapsed build-speed claim. The canonical enumerator and owned option records stay
unchanged. Architecture documentation describes the shared rule.

A public snapshot regression covers explicit and partially inferred domain/kind,
vertex/face/edge defaults and absent geometry; its first version passed baseline
production, and final fixtures include face scalars/normals and edge colors.
Claude approved the fixed diff. The focused ci/Clang 23 build and 30 selected
runtime/presentation/compilation-locality cases pass. Evidence and the plan:
`/tmp/intrinsic-readiness-reuse/`. Broader UI-037 acceptance stays open.

Full canonical `IntrinsicTests` build and CPU selector pass: 4,750 selected,
4,749 passed, one expected ASan-only GLFW lifecycle skip, zero failures
(150.16 s). Layering, test layout, task policy/state, links, root hygiene and
explicit-file docs-sync checks pass. Source-doc audit: zero errors and one
existing large-file review prompt. No module inventory change is required for
this implementation-only slice; no GPU or sanitizer-suite run is claimed.


## Editor test diagnostics dependencies — verified, 2026-09-18

Continuation from `31796b85d`, authorized compilation cleanup. The shared
`EditorFeatureTestContext.hpp` now imports the canonical `Graphics.RenderDiagnostics`
owner for `RenderGraphFrameStats`. Models, mesh-method, clustering-method and
presentation tests drop their duplicate renderer-facade dependency; the model
test directly imports `RenderCommandRouter` for its command-status enum. This
explicit enum import fixes the missing re-export exposed by the first rebuild.
Renderer-method callers retain their existing imports. No runtime behavior,
public API, new helper/file or ownership change; a direct facade import is
appropriate only when a consumer actually needs its complete API.

The rebuilt Clang/CMake graphs show 17 test producers no longer reach
`Graphics.Renderer`. The shared helper's transitive module set drops 122 -> 94;
method consumers lose 27–28 modules and the large editor model/mesh/clustering
consumers lose 15 each. `EditorCompilationLocality.TestContext` guards all 17
producers through the existing compiler-metadata checker. These are dependency
counts, not an elapsed compilation-speed claim. Production sources are unchanged;
four test import edits net -1 physical line, the shared header is unchanged in
length, and the new CMake guard adds 23 lines. Runtime/test docs explain the owner.

Claude Sonnet reviewed the plan and corrected fixed diff. Canonical ci/Clang 23
focused builds and 63 tests pass; the `IntrinsicTests` aggregate builds every
consumer, including GPU-smoke sources. Final full CPU gate: 4,751 selected,
4,750 passed, one expected ASan-only GLFW lifecycle skip, zero failures (150.43 s).
No GPU execution or sanitizer-suite run is claimed. Compiler-hotspot tooling's
26 tests pass; routing reconciles 41 targets, 4,757 cases and 363 sources.
Layering/test layout, task policy/state, docs sync/links, root hygiene, skill
mirrors and session brief checks pass. Inventory regenerated without changes.
Source-doc audit: zero errors, 17 existing review prompts outside edited prose.
Sources/tests stayed frozen during the final gates; hashes, compiler metadata,
exact prompts/diffs, commands and logs are in `/tmp/intrinsic-readiness-reuse/`.
UI-037's broader readiness acceptance remains open. Start the next slice in a
fresh session instead of repeating either completed cleanup.


## Unused editor asset-service wiring — 2026-09-18

Operator-directed reuse and compilation cleanup from `04d9b86bb`, with Codex as
sole writer and Claude Sonnet as bounded read-only reviewer. Source search found
that `EditorSceneEditingContext`, private `EditorFeatureBindings` and the shared
test context only forwarded `AssetService*`; no editor operation consumed it.
Imports already execute through asset-workflow command callbacks, and texture
baking uses the attached service. Remove the dead fields, adapter copies, service
lookup, redundant imports and fixture assignments; retain actual service users.
This uses the existing owners without a replacement wrapper or linkage change.
The existing source-documentation and editor compilation contracts apply.

Verify with canonical ci/Clang 23 focused editor/runtime builds, the scene/import,
bake and readiness contracts, and the compiler-metadata boundary guard. The final
combined cleanup checkpoint also owes the full `IntrinsicTests` build/CPU gate
and task Verification commands above. Dependency counts establish reachability,
not elapsed compilation speed. Broader UI-037 acceptance remains open.

The focused build passes; 79 selected scene/import, texture-bake, lifecycle,
readiness and editor-locality cases pass. Clang/CMake metadata shows all 35
inspected affected producers no longer reach `Asset.Service`: most drop six
transitive modules, four drop five. The scene interface is 54 -> 48, workspace
interface 87 -> 81, and shared test support 94 -> 88. The new production guard
covers eight producers; the existing test-context guard covers another 17.
Four production files shrink 6,088 -> 6,079 lines (-9); test setup/imports remove
12 lines, and dependency guards add 15 net lines. No runtime-speed claim.
Claude reviewed the fixed production/test diff with no findings. Layering, test
layout, task policy, inventory and session-brief checks pass. Source-doc audit
reports zero errors and five pre-existing contract-comment review prompts;
inspection retains the lifetime/linkage/test-seam comments. Final full CPU and
combined diff review follow the dependency-record slice. Evidence is under
`/tmp/intrinsic-editor-reuse-next/`.


## Canonical scheduler dependencies in editor job records — 2026-09-18

Continuation after `cd9450d23`, preserving the user's duplication/compilation
cleanup direction. `EditorJobDependency` exactly repeats the scheduler's
`JobDependency` (token plus owned reason string), whose module is already
imported. Reuse that canonical value in `EditorJobRecord`, delete the duplicate
record without an alias, and update the current presentation snapshot regression
to verify two ordered dependencies and independently copied reasons. Runtime's
current job projection leaves dependencies empty; this slice does not add queue
publication behavior. The existing source-documentation contract applies; no new
module, import, config state, backend or API compatibility requirement.

Plan: focused snapshot/job/locality tests, all task Verification commands, and
one combined full CPU run; fixed final diff reviewed with Claude Sonnet. Count
removed duplicate symbols and source lines without an elapsed-speed claim.

Verified combined checkpoint: canonical ci/Clang 23 builds both focused targets
and `IntrinsicTests`, including GPU-smoke consumers. All 89 focused tests pass;
full CPU selects 4,752, with 4,751 passed, one expected ASan-only GLFW lifecycle
skip, zero failures (150.78 s test execution). No GPU execution or sanitizer-suite
run is claimed. Source/test hashes stayed fixed through final verification.

The job interface shrinks 91 -> 86 lines; its removed duplicate type has zero
symbols in rebuilt snapshot and model-test objects (44 and 60 entries before).
Across both slices, five production files shrink 6,179 -> 6,165 lines. Final
compiler metadata extends the first slice's affected set to 38 producers, each
losing five or six transitive dependencies. These are source/dependency facts;
no elapsed compile-time or final binary-size improvement was measured.

Claude Sonnet reviewed both slices. Its final diff-only review raised a missing
header guard and a possibly unused GPU fixture member; full CMake/source context
resolved both: the compiling `.cpp` already guards the shared header, and the GPU
fixture legitimately uses `m_Assets` for direct asset assertions. No findings
remain. Architecture sweep: existing ownership and layer directions retained;
no added dependency, facade, config state, frame recipe, backend or exception.
Clean-workshop rows 1–3 and 8 pass; renderer/pass/recipe/maturity rows 4–7 do not
apply. The live scheduler-to-editor projection's empty dependency list is unchanged.

Strict layering/test layout/task policy/state, task validation, docs-sync/links,
root hygiene, skill mirrors and session-brief checks pass. Module inventory is
regenerated unchanged. Source-doc audits: zero errors; the job interface has zero
review prompts. Compiler-hotspot tooling passes all 26 tests; touched-route
reconciliation passes against the configured ci registry. Commands, logs,
compiler closures, source hashes and Claude review packets are retained at
`/tmp/intrinsic-editor-reuse-next/`. Start a fresh session from this checkpoint to
avoid reloading completed-slice history. Broader UI-037 acceptance remains open.


## 2026-09-18 continuation — shared visualization lookup

Operator direction remains duplication reduction and compilation locality.
Baseline: `e03f17b40`; clean checkout and one writer. Reuse search found identical
stored/effective visualization lookup in workspace models and visualization
commands. Move the shared read-only mechanism into the existing compiled
context-adapter owner and declare it in a narrow private visualization header;
keep mutation local to visualization commands. No public module, facade or
new compiled source file is needed. Reintroduce separate logic only if the contracts
actually diverge. Stored absence must remain distinct from entity fallback for
undo, and snapshots remain owned values. Only the three consumers import the visualization component type. Claude Sonnet reviewed the bounded plan; its claim that a
C++-linked owner would itself widen module imports was not adopted.

Extend the existing lane override command/model/history regression across
surface, edge and point lanes, including absent config, entity inheritance,
other-lane isolation, undo, redo and disabling. Remove the unused asset-service import from visualization actions; extend the existing compiler
asset-service boundary check to cover that producer. Final evidence follows.
Broader readiness acceptance remains open.

First build exposed two declaration prerequisites: `Asset.ImportRouter` is
required by the existing command-helper header and stays; the shared properties
header reaches a workspace session without a visible visualization component.
A dedicated `Runtime.EditorVisualizationHelpers.hpp` keeps the new declarations
out of unrelated consumers instead of widening their imports. This is the
concrete compilation reason for one small private header. The first build's
missing-type failures were caused by this slice, diagnosed and corrected here.

Canonical ci/Clang 23 focused builds pass after correction, with no new compiler
warnings. All 64 focused runtime/editor/locality tests pass. `IntrinsicTests`
also builds all consumers. Compiler-produced module maps show visualization
actions' closure falling from 73 to 67 modules, removing `Asset.Service` and
five dependencies; none were added there. Across the three changed production
implementations and the new private header, physical lines fall 8,486 -> 8,453.
These are structural facts, not elapsed compilation-speed claims.

Claude Sonnet's corrected fixed-diff review found no concrete bugs. Its initial
hypothetical missing lane-override type was resolved against the actual exported
`VisualizationLaneOverrides` in the visualization component module; the new
header documents its EnTT prerequisite. Architecture review retains runtime
ownership, owned optional results, existing command/history semantics and C++
linkage. No public module surface, config lane, backend, recipe or exception
changes. Clean-workshop rows 1–3 and 8 pass; rows 4–7 do not apply.

Strict layering/test layout/task policy/state/docs-sync, doc links, root hygiene,
skill mirrors and session-brief checks pass. The inventory regenerates unchanged;
the new-header source-doc audit has no findings. Hotspot tooling passes 26 tests,
and touched-scope reconciliation passes against the configured ci registry.
Logs, exact commands, source hashes and Claude review packets are in
`/tmp/intrinsic-visualization-reuse/`. Full CPU outcome is recorded below.

Full CPU gate: 4,752 selected, 4,751 passed, one expected ASan-only GLFW lifecycle
skip, zero failures (150.92 s test execution). Source hashes remained unchanged
through final verification and review. No GPU execution or sanitizer-suite run
is claimed. This is a verified session checkpoint; broader UI-037 stays open.

## 2026-09-18 continuation — shared render-hint components

Operator direction: continue duplicate-code and compilation-locality work with
Claude Fable 5.1. This bounded slice remains under UI-037; broader readiness
acceptance stays open. The existing `runtime.editor-prepared-frame-locality`
and `repo.source-documentation` declarations cover the changed private helpers.

Reuse review found matching surface/edge/point capture, exact comparison and
application in scene primitive-view and visualization commands. Both now use
`EditorRenderHintComponents` and three compiled free functions in the existing
`Runtime.EditorFeatureContextAdapters.cpp` owner. No new module or target is
needed. A narrow private header replaces the broad command-helper dependency
in visualization actions, allowing its scene-editing and asset-import imports
to be removed. Per-component comparisons are private to their sole owner.

Retained differences: scene and visualization history transactions and guards
remain separate; only visualization owns surface visualization, which is still
applied before the render components. Float-bit and string-variant comparisons,
optional absence, command validation and cache invalidation remain unchanged.
Poisson's visualization equality has different float semantics and is not merged.

Claude Fable 5.1 reviewed the plan. Keep implementation bodies compiled, retain
update order and check the two history contracts through public commands. The
repository's supported Clang preset supplies build evidence; no GCC evidence or
elapsed compilation-speed improvement is claimed. The new regression exercises
both command families with finite, signed-zero, NaN and property-name sources;
the existing render-hint test additionally checks surface-visualization undo
and intervening-edit rejection. Verification results follow below.

Canonical ci/Clang 23 focused build and all 65 selected editor/session/locality
tests passed. `IntrinsicTests` also builds. Both builds emit no warnings.
Compiler-produced module maps show visualization actions' module closure falling
67 -> 55, with 12 removed and none added. All affected production implementations
and private headers together fall 6,008 -> 5,984 physical lines. These are
structural measurements, not elapsed build-time results.

Fable's fixed-diff review found no production bug. Its test feedback led to
optional-existence assertions, both width/size round-trip checks and distinct
string/variant stale-state checks. The indirect-import concern was resolved
against the compiler dependency closure used by the locality tool. Architecture
review retains runtime ownership, owned snapshots, exact comparisons, mutation
order and family history guards. Clean-workshop rows 1–3 and 8 pass; rows 4–7
are not applicable. Strict layering, test layout, task policy/state, docs sync,
links, root hygiene, skill mirrors and session-brief checks pass. The inventory
regenerates unchanged, touched-scope reconciliation passes, the changed-header
audit has no findings and hotspot tooling passes 26 tests.

Exact commands, module maps, source hashes and Fable packets/results are retained
in `/tmp/intrinsic-render-hint-reuse/`. Final corrected-test and CPU outcomes
are recorded below.

Verification for this slice:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi\.(.*Visualization.*|.*Appearance.*|.*RenderHint.*|.*PrimitiveView.*|.*GeometryPresentation.*)|SandboxEditorSession|EditorCompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Final verification: the full CPU gate selected 4,754 tests: 4,753 passed, one
expected ASan-only GLFW lifecycle skip, zero failures (151.23 s). Subsequent
changes only strengthened the test assertions; the final `IntrinsicTests`
build and all 65 focused tests pass again (7.81 s). Production and architecture
hashes still match Fable's reviewed diff. Fable's test re-review found no blocker;
final self-review also checked both undo/redo source restoration and point-source
staleness. No GPU execution, sanitizer-suite run or compilation speedup is
claimed. This is a completed checkpoint within the still-open UI-037 task.

## 2026-09-18 continuation — shared unpublished-job failures

Operator direction: continue duplicate-code/compilation cleanup with Claude
Fable 5.1; broader readiness acceptance stays open. Reuse review found five
finalizers repeating status/error mapping and diagnostic construction across
Poisson, registration, UV, curvature and mesh topology operations. Extend the
existing compiled `MeshSupport.cpp` owner with a plain failure value declared
in a small private `JobFailure.hpp`; no template, target or module is needed.
Keep typed result copies, delivery flags, callbacks and registration's stored
result local. UV retains `BackendRejectedInput`. Only `Current` may append
worker detail; Poisson requires `GeometryProcessingFailed`, while registration
uses its broader `!Succeeded()` predicate. A shared lifecycle abstraction would
need a demonstrated identical delivery contract and is outside this slice.

Fable's plan review confirmed the factoring and highlighted owned-message
construction before stored-result replacement, header prerequisites and the
allocation-bearing helper's lack of `noexcept`. Existing tests are strengthened
through public commands. The first baseline check rejected a new, incorrect
expectation: cancellation before apply validation leaves `Current` and emits
the generic unpublished reason. Preserve that behavior; this is not a scheduler
semantics change. Structural consolidation alone makes no build-time claim.

The first implementation build found `EditorProcessing.cpp` also consumes
`PointFields.hpp` without command-status visibility. A separate private failure
header keeps that discovery consumer's imports unchanged. This is the concrete
compilation reason for the new header; the missing-type failure belongs to this
slice and was corrected without widening the generic context.

Fable's fixed-diff review found no concrete defect. The copied worker detail is
owned before registration replaces its stored result, and the two caller
predicates retain their original behavior. Its concern about header placement
was resolved: standard headers are supplied in each module's global fragment;
private declarations follow imports. `MeshSupport.cpp` already imports
`EditorCommon`. Dedicated dropped-worker detail scenarios were not added;
equivalence of those gates was reviewed against the original functions.

Canonical ci/Clang 23 focused and aggregate `IntrinsicTests` builds pass after
the header correction. All 121 selected lifecycle/method/locality tests pass.
Compiler module maps for the six changed implementations are unchanged, with
zero added dependencies. Across all eight affected production files, including
the new narrow header, physical lines fall 10,480 -> 10,467. The main gain is
one compiled status/diagnostic mechanism replacing five bodies; no elapsed
compile-time result is inferred from this small net reduction.

Architecture/workshop review: runtime ownership, value-owned messages, thread
and delivery semantics remain intact; scorecard rows 1–3 and 8 pass, rows 4–7
are not applicable. Strict layering, task policy/state links, test layout,
root hygiene, docs links/sync (explicit changed paths), skill mirrors and session
brief checks pass. Inventory regeneration is unchanged. Touched-scope producer
reconciliation and all 26 compile-hotspot tooling tests pass. Header source-doc
review has zero objective errors; the new declaration comment records the
non-obvious detail gate, while existing point-field lifetime/preflight comments
remain relevant. Logs, fixed Fable packets and source/module-map hashes are in
`/tmp/intrinsic-editor-reuse-next/`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi\.(.*ProgressivePoisson.*|.*Registration.*|.*Mesh(Denoise|Remesh|Subdivide|Simplify|Curvature).*|.*Queued.*|.*UvRegeneration.*)|ProcessingCompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Final CPU gate: 4,754 selected, 4,753 passed and one expected ASan-only GLFW
lifecycle skip, zero failures (157.54 s test execution). Source/test hashes
match Fable's fixed diff. No GPU execution, sanitizer-suite run or elapsed
compilation-speed improvement is claimed. This is a completed session checkpoint
inside the still-open UI-037 task.

## Continuation — selection-controller borrows (2026-09-18)

Operator direction: continue duplication and compilation-locality work with
Claude Fable 5.1. This slice preserves UI-037's open readiness inventory.
Reuse decision: `EditorCommandHistory` and `EditorProcessing` each need only a
`SelectionController*`; reuse the existing borrowed-service C++ linkage pattern
and the sole controller owner instead of creating a facade or a types module.
Its config, pick and primitive records retain their module attachment. No fields,
algorithms, ownership, command behavior or dependency-layer rules change.

The baseline compiler-metadata guard fails for both interfaces. Fable reviewed
the plan and requested reverse-dependency accounting plus minimum-Clang
verification because the class linkage changes. Keep complete API imports only
at actual callers, guard the transitive closure, and rebuild all callers.
The first build caught member definitions needing matching C++ linkage; both
implementation units now wrap only their member definitions. An incorrectly
placed closing brace in the primitive unit was corrected before verification.

Verification for this slice: canonical ci configure; focused runtime/editor
targets, selection/history/editor/locality CTest cases; IntrinsicTests and the
full exclusion-only CPU gate; fresh cache-off Clang20 Null/headless runtime/editor
closure; strict layering/task/docs/workshop checks and module inventory.
Exact logs and fixed review packets live in `/tmp/intrinsic-locality-next/`.
No elapsed compilation improvement is inferred from dependency counts.

Fixed-source review: Fable approved subject to green builds/CPU tests. The
review's transitivity concern is covered by `compile_hotspots.py`'s recursive
CMake usages traversal; its suggestion to restore a historical task comment
conflicts with current source-documentation policy and was not adopted.
Selection/history and parameterization are the only newly explicit callers.
All 151 focused CTest cases pass, including the six-producer selection-borrow
guard and the two-frame disabled-tooltip integration. Canonical focused targets
build with Clang23. Existing unrelated nodiscard warnings remain in test code.

Compiler metadata for the captured editor/test cohort removes SelectionController
from 38 producers; command history's transitive module count drops 17 -> 1 and
generic processing's 39 -> 36. This isolates edits to controller details; it
does not measure elapsed rebuild time. Across all seven affected production
files, physical source lines are 3,420 -> 3,429 (+9); no new module, source file,
compiled target, wrapper, state owner or allocation is introduced.

Scope/layer/tests/docs sweep: one dependency cut, unchanged layouts and method
bodies, one real compiler-boundary regression. Strict layering (zero allowlist
entries), test layout, task policy, doc links, root hygiene and skill freshness
pass; generated inventory is unchanged (429 modules). Source-doc audit finds
zero objective errors in the three touched interfaces; existing declaration
comments remain outside this change. All 26 compile-hotspot tooling tests pass.
Clean-workshop rows 1-3 and 8 pass; 4-7 are not applicable (no renderer/pass,
recipe, maturity-closure or exception changes). Full CPU and Clang20 evidence
follow below.

Minimum-compiler check: fresh cache-off Clang20 ci-derived Null/headless
`ExtrinsicSandboxEditor` runtime/library closure builds, as do the four
selection/history/primitive/session-lifecycle test objects. Reconciliation is
a no-op and the six-producer compiler-boundary guard passes. This is compiler
compatibility evidence, not Clang20 test execution. Owned tmpfs build removed
after retaining logs and compiler metadata; existing user builds are untouched.
Canonical Clang23 `IntrinsicTests` builds successfully.

Final CPU gate: 4,755 selected, 4,754 passed and one expected ASan-only GLFW
lifecycle skip; zero failures (157.72 s). Reviewed source hashes remain fixed.
Fable's build/test conditions are satisfied. No sanitizer-suite or GPU execution
and no elapsed compilation-speed improvement is claimed. UI-037 remains open;
this is a complete compilation-locality checkpoint.


## Continuation — shared appearance drawing (2026-09-18)

Operator direction continues duplicate-code and compilation cleanup with Claude
Fable 5.1 from `b185f7810`; the broader readiness acceptance remains open.
Reuse search verified the inspector and domain appearance implementations of
`DrawBoundRenderStateRows` are token-identical and `DrawTextureBakeControls`
differ only in formatting and optional braces. Both now reuse compiled free
functions in the existing `Sandbox.PanelSupport.cpp`, with declarations in its
app-private header. No new file, module, service or dependency layer is needed.

Fable approved the plan with state-lifetime, linkage, ImGui-ID and dependency
checks. A plain mutation-state record is justified by two real callers: each
keeps one process-lifetime static, preserving the original separation and
persistence of rename target/buffer and diagnostic. Borrowed bake/UV state,
callbacks, defaults, IDs and command order remain unchanged. Bake-only constants
and two helpers move out of the shared header into the implementation. Re-split
the drawing bodies only if the callers acquire different behavior contracts.
The existing source-documentation contract applies; canonical runtime ownership
and readiness contracts are unchanged.

Verification plan: real ImGui checks for row diagnostic priority/empty rows,
rename-state isolation/persistence/truncation and bakeable-source clamping;
existing presentation, domain, UV/bake and compilation-locality cases; canonical
ci focused targets followed by IntrinsicTests and the full CPU selector. Update
existing source-location checks for the shared owner without dropping their UV
callback, dismissal or padding assertions. Review a frozen diff with Fable and
run task/layer/docs checks. Evidence: `/tmp/intrinsic-panel-reuse/`.

Implementation/review: the first build caught missing braces around the domain
callsite's new static plus draw call. A later script selected a helper use instead
of its definition while moving private helpers; rebuilding from the exact baseline
and signature-anchored extraction fixed it. Both were local edit errors, not engine
regressions. The rename test initially queued ImGui activation too late; queueing
in frame N and checking in N+1 exercises the real button and now passes.

All 82 focused tests pass. Canonical ci/Clang23 IntrinsicTests builds; the initial
full CPU run selected 4,758 cases: 4,757 passed, one expected ASan-only GLFW
lifecycle skip, zero failures (156.21 s). Fable's fixed-source review found no
blocking production defect. Its refinements replace order-sensitive structured
binding with named field references, strengthen the UV call-binding check, and
drop two weak log-substring assertions while retaining source-index checks.
The tests exercise actual ImGui drawing, diagnostic priority, empty state, rename
isolation/persistence and 127-byte NUL-terminated truncation. No extra source-shape
checks or pre-existing behavior changes were added. Final gates follow below.

Across all four affected production files, physical source lines fall 6,237 ->
5,616 (-621), including the new state record and declarations. The shared header
falls 370 -> 340 lines. Module-map closure sets remain 117/124/115 for domain,
shell and support respectively, with zero added or removed modules; the removed
direct imports remain transitively reachable. Object symbols confirm both
consumers reference exactly one compiled definition of each drawing helper in
PanelSupport. Body comparisons match baseline after accounting only for the state
parameter, equivalent local type alias, and named state references. No elapsed
compilation-speed or binary-size gain is claimed.

Scope/layer/tests/docs sweep: app presentation only; runtime APIs, command
contracts and GPU continuations unchanged. No new module, file, library, layering
edge or policy exception. Workshop rows 1–3 and 8 pass; 4–7 are not applicable.
Strict layering, test layout, task policy/state links, task validation, root
hygiene, docs links/sync, skill mirrors and session-brief checks pass. Module
inventory regeneration is unchanged (429 modules). Source-doc audit has zero
objective errors; existing declaration/README heuristic findings remain outside
this slice. Fable review packets, source hashes, symbol/module-map evidence and
all logs are retained in `/tmp/intrinsic-panel-reuse/`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorPresentation\.|^SandboxDomainPanels\.|^SandboxEditorUi\.(.*TextureBake.*|.*UvRegeneration.*|ActionReadinessDerivesDomainPrerequisiteReasons)|^EditorCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Final verification after review refinements: IntrinsicTests builds; all 82
focused cases pass again. The final CPU gate selected 4,758 cases: 4,757 passed,
one expected ASan-only GLFW lifecycle skip, zero failures (151.79 s). Fable's
delta review found no new concern. Final C++/test hashes remained unchanged
through verification. No sanitizer-suite/GPU execution or elapsed compilation
speedup is claimed. UI-037 remains open; this is a complete reuse checkpoint.


## Continuation — shared scalar styling controls (2026-09-18)

Operator-directed duplication/compilation cleanup with Claude Fable 5.1 from
`d0315afc6`; UI-037's broader readiness acceptance remains open. Reuse search
found matching uniform-color and scalar drawing, except for the domain-only
baked-texture return between range and bins. The matching bodies now compile
once in existing PanelSupport; callers retain visibility checks and ImGui IDs.
Separate color/range and bin/isoline functions preserve that difference without
a policy flag. The existing colormap-name array is reused. Two real callers
justify plain functions; no new file/module/state owner/layer edge. Re-split
only if their behavior contracts diverge further.

Fable reviewed the plan and fixed diff with no production regression found.
Applied its naming, shared submission, formatting and stronger test feedback.
Real ImGui activation through bound runtime commands covers entity/surface
routing, full styling/non-default colormap preservation, edit suppression,
add seeds, removal order and capacity. The unchanged caller baked-visibility
branch was reviewed directly; no new end-to-end visibility test was added
because the existing public panel seam exposes registration, not direct drawing.

All affected production files: 5,616 -> 5,533 lines (-83). Compiler module-map
sets remain 117/124/115 for domain/shell/support, with no dependency changes.
Symbols confirm one compiled definition per shared helper and two consumers;
body tokens match after the declared extraction. No elapsed compile-speed claim.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorPresentation\.|^SandboxDomainPanels\.|^SandboxEditorUi\..*Visualization.*|^EditorCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Final canonical ci/Clang23 build passes; all 79 focused tests pass. CPU gate:
4,759 selected, 4,758 passed, one expected ASan-only GLFW skip, zero failures
(152.55 s). Reviewed source/test hashes stayed fixed. Strict layering, test
layout, task policy/state links, docs links/sync, root hygiene, skill mirrors
and session brief pass; source-doc audit has zero objective errors. No public
module/inventory change. Architecture review retains runtime validation and
lifetimes; workshop rows 1–3/8 pass, 4–7 n/a. No GPU/sanitizer-suite execution.
Packets, hashes, comparisons and logs: `/tmp/intrinsic-scalar-controls/`.

## Continuation — local position chooser and compiled setup (2026-09-18)

Operator directs continued duplication/compile-locality cleanup with Claude
Fable 5.1 from `5a321e24e`. UI-037's broad readiness inventory remains open.
K-Means and Progressive Poisson now share `DrawPointSetPositionInput` locally
in `Sandbox.MethodPanels.cpp`. The existing catalog filter, labels, widget IDs
and within-frame selected-row tracking are preserved; each caller still owns
its output-domain changes. Consolidation stays separate because it uses row
identity and default focus. A shared public header or policy-flagged selector
would add unnecessary scope. The two loops become one, with nine net added
source lines for the function boundary and borrowed-result handling.

The independent runtime slice moves `EngineSetup` construction and its two
non-template hook-registration bodies from `Runtime.Module.cppm` into the
matching private implementation unit. Keep C++ linkage, template definitions,
small accessors, field layout, callback moves and error precedence unchanged.
The extra source file is justified by compile locality; there is no new module,
state owner, wrapper or layer dependency. Including CMake registration, this
slice adds 29 physical lines while removing 29 interface lines. Neither source
count nor code motion establishes an elapsed build-time improvement.

Claude's fixed-packet Fable 5.1 review found no blocking defect. Existing real
ImGui K-Means/Poisson retry tests cover the unchanged selector IDs and binding
paths; `EngineSetup.RetainsOnlyRegistrationPhaseFrameAndViewportRegistrars`
covers invalid-hook priority, unavailable registrars and successful callbacks.
Verification results follow after the canonical focused and full CPU gates.
Review packets and logs: `/tmp/intrinsic-reuse-round2/`.

Final verification: canonical ci/Clang23 focused targets and `IntrinsicTests`
build; 104 focused cases pass. Full exclusion-only CPU gate: 4,760 selected,
4,759 passed, one expected ASan-only GLFW lifecycle skip, zero failures
(151.84 s). The final implementation-only header edit rebuilt only
`Runtime.Module.cpp.o` before relinking the focused executables. Symbol output
confirms the constructor and registration bodies in that object. Final source
hashes remained fixed through all verification. No GPU execution, sanitizer
suite or elapsed compilation-speed claim.

Scope/layer/tests/docs review passes. Layering has zero violations/allowlist
entries; test layout, task policy/state/validation, doc links/sync, root hygiene,
skill mirrors and session brief pass. Source-doc audit has zero findings in the
touched interface; module inventory regenerates unchanged (429 modules).
Architecture/workshop rows 1–3 and 8 pass; rows 4–7 are not applicable. No
ownership, config-lane, renderer recipe or lifetime contract changes.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(EngineSetup|RuntimeModule|SandboxProcessingPanels|SandboxPointCloudConsolidationPanel|SandboxCurvatureSegmentationPanel|SandboxDescriptorAnalysisPanel|EditorCompilationLocality|ProcessingCompilationLocality|KernelCompilationLocality)\.|^SandboxEditorUi\.ActionReadinessDerivesDomainPrerequisiteReasons$|^SandboxEditorPresentation\.DisabledActionReasonTooltipAppearsAfterTwoFrames$' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

## Continuation — borrow-only kernel registry dependencies (2026-09-18)

Operator-directed compilation/reuse cleanup with Claude Fable 5.1 from
`10ec14fc6`. UI-037's remaining readiness acceptance is unchanged. The existing
C++ linkage on `ECS::Scene::Registry` and `WorldRegistry` permits the command,
setup and input-action contexts to borrow those exact types without importing
their complete owners. Reuse the established non-exported declaration pattern;
no wrapper, new source, ownership change or compatibility path is needed.
Concrete scene/world users retain explicit owner imports. Command/event/job
signature helpers and envelopes remain separate because their diagnostics,
access and ownership contracts differ; no queue helper is justified to save
three lines. This slice reduces compile dependencies, not duplicate algorithms.

Claude reviewed the bounded plan and fixed diff. Its doc-route finding was
corrected; delta review found no blocker. Compiler guards traverse all listed
producers and their transitive dependency graph. The Engine-only convergence
policy remains unchanged and passes. Source-doc audit: zero errors; seven
existing command-contract comments reviewed. No ownership, layout, sequencing,
config-lane, frame-recipe or failure-state behavior changes. Workshop rows
1–3/8 pass, 4–7 n/a; strict layering has no exceptions or violations.

All three affected production interfaces total 550 -> 563 physical lines: the
extra declarations/comments remove imports without adding an abstraction. The
full aggregate build caught one benchmark consumer missing its direct scene
owner import; `Bench_KMeansGpuVulkanSmoke.cpp` now imports it (+1 line).
The benchmark parameters, algorithms and reporting are unchanged. All 101
benchmark manifests validate. No GPU execution or performance result is claimed.

Canonical ci/Clang23 configuration, focused targets and `IntrinsicTests` build
pass; 174 focused cases pass. The sandbox editor library is built by these
targets; standalone `ExtrinsicSandbox` is disabled in the ci preset. Compiler
module maps exclude the scene registry from all three touched interfaces:
CommandBus 15 -> 12 entries, Module 30 -> 27, InputActions 12 -> 10. Engine's
transitive map changes 40 -> 38. This is dependency evidence, not elapsed build
time evidence. No additional Codex subagents were needed.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(RuntimeCommandBus|RuntimeKernelEvents|RuntimeJobService|RuntimeWorldRegistry|RuntimeModule|RuntimeInputActions|RuntimeSceneLifecycle|EngineSetup|CoreHash|CoreFrameGraph|KernelCompilationLocality|RenderCompilationLocality|EditorCompilationLocality|SpatialCompilationLocality)\.|^SandboxEditorUi\.ActionReadinessDerivesDomainPrerequisiteReasons$|^SandboxEditorPresentation\.DisabledActionReasonTooltipAppearsAfterTwoFrames$' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Logs, fixed review packets, source hashes and baseline module maps:
`/tmp/intrinsic-kernel-imports/`.

Final checkpoint (2026-09-19): full exclusion-only CPU gate selected 4,761
cases; 4,760 passed, one expected ASan-only GLFW lifecycle skip, zero failures
(161.31 s). Reviewed source/test hashes stayed fixed through verification.
Strict task policy/state links, doc links/sync, root hygiene, source/test layout,
kernel convergence, skill mirrors and session-brief freshness pass. Module
inventory regenerates unchanged at 429 modules. No sanitizer-suite or GPU run.
UI-037 remains open; all code in this compilation-dependency slice is complete.


## Continuation — processing menu redirects (2026-09-19)

Operator-directed duplicate-code cleanup with Claude Fable 5.1 from
`4d3fef75b`. The ten redirect descriptor/callback bodies in
`Sandbox.MeshProcessingPanels.cpp` now use one private
`Impl::RegisterRedirectWindow` member. IDs, menu paths, titles, registration
order, default closed state and callback order stay explicit and unchanged.
The face-normal alias retains its additional preset action. The existing
`RegisterWindow` helper draws content and resets the model cache, so its
contract does not fit redirect aliases. One local helper is sufficient; no
shared header, new module or configurable redirect framework is needed.

Affected production source: 2,808 -> 2,771 physical lines (-37). Existing
presentation tests cover all ten alias families and structured menu paths;
the face-output integration test covers the distinct face-normal preset.
No elapsed compilation improvement is inferred from the consolidation.
Fixed review packet and verification logs: `/tmp/intrinsic-reuse-round3/`.
Claude's fixed-diff review found no blocker; its declaration grouping and
literal-lifetime comments were applied and reviewed. All 156 registration
literals retain their order, and the panel module imports are unchanged.
The four-point scope/layer/tests/docs sweep passes; no state ownership,
validation, frame composition or control-surface contract changes.

Combined verification with the independent input-action locality slice:
canonical ci/Clang23 configure, focused targets and `IntrinsicTests` build pass.
All 101 focused cases pass. After the final declaration/comment changes, the
full exclusion-only CPU gate selects 4,762 cases: 4,761 pass, one expected
ASan-only GLFW lifecycle skip, zero failures (151.59 s). Source/test hashes
remain fixed through final verification. No sanitizer-suite or GPU run.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(SandboxEditorPresentation|SandboxProcessingPanels|RuntimeInputActions|EditorWindowRegistry|KernelCompilationLocality|EditorCompilationLocality)\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 120
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Strict layering, test layout, task policy/state links, docs links/sync, root
hygiene, kernel convergence, skill mirrors and session brief pass. Source-doc
audit has zero findings in the touched interface; the module inventory
regenerates unchanged (429 modules). UI-037's broader readiness acceptance
remains open.


## Continuation — input-action viewport dependency (2026-09-19)

Independent compile-locality slice alongside `d360ae0d0`. Reuse decision:
`Platform::Extent2D` is exactly an alias of `Core::Extent2D`; the input-action
interface already imports the core owner and uses that type in its context.
Dispatch now names the core type directly in declaration and definition,
removing `Platform.Window` without changing type identity, linkage, callers,
input behavior or object layout. The two production files total 209 -> 208
lines. No wrapper, new file or extra import is needed.

`KernelCompilationLocality.InputActions` rejects the original compiler graph
and passes on the final source. The interface's module map drops Window
(10 -> 9 dependencies). This is dependency evidence, not an elapsed compile-time
result. Canonical runtime documentation and the test guide explain the boundary;
the module inventory remains unchanged. Fable's plan and fixed-diff review
agree; its transitivity concern is settled by the actual recursive compiler
metadata check. Workshop rows 1–3/8 pass, 4–7 n/a.

The exact combined verification commands and CPU results are in the preceding
checkpoint. All 26 compile-hotspot tool tests also pass. Strict structural
checks pass and source/test hashes remain fixed. Evidence and fixed review
packets: `/tmp/intrinsic-reuse-round3/`. UI-037 remains open; both session
slices are complete. No additional Codex subagents were used.


## Continuation — compiled graphics test support (2026-09-19)

Operator-directed duplication/compile-locality work with Claude Fable 5.1 from
`2a81b9db0`. UI-037 remains open; this continues its shared test-support cleanup
and does not close the remaining action/readiness acceptance matrix.

Reuse decision: `tests/support/GraphicsTestSupport.hpp` already owned first-match
command-pass lookup and byte-preserving RGBA/sRGB conversion. Renderer lifecycle,
ImGui smoke and UV-view smoke now reuse its lookup; default-recipe smoke reuses
its matching conversion functions. The ImGui-specific pixel record/converter
stays local. Snapshot pointers remain borrowed, duplicate names return the first
pass, unknown formats retain their passthrough behavior and alpha stays linear.

Non-constexpr helper bodies compile in `GraphicsTestSupport.cpp`, linked through
one object target into graphics CPU contract and Vulkan smoke executables only.
The header imports the canonical render-diagnostics owner and uses `string_view`
for allocation-free lookup. No production source, module surface, backend, label
or GPU execution behavior changes. The compile database has one helper action;
this is compilation-structure evidence, not an elapsed compilation-speed claim.

Existing helper/caller files plus the new helper and build entries total
14,093 -> 14,054 physical lines (-39); the new regression test is additional
coverage. Three CPU tests passed before extraction and after it, checking first
match/missing lookup, four pixel formats, unknown-format fallback, sRGB values,
monotonicity and alpha preservation. The final tests also cover a bounded
non-null-terminated name view and constexpr format checks. All 108 focused
cases pass. Canonical ci uses Clang 23 without sanitizers; both consumer
executables and the full `IntrinsicTests` target build successfully.

Fable's plan and fixed-diff reviews found no blocking defect. Conditional import,
link-scope and call-site concerns are settled by successful builds of all affected
consumers; `Format` is uint32_t and the default-recipe test still needs cmath for
isfinite. The four-point review passes. Source-documentation audit: zero errors,
one retained first-match/borrow-lifetime comment. No Codex subagents were needed.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicGraphicsVulkanSmokeTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(GraphicsTestSupport|RendererFrameLifecycle|TransientDebugSurfacePass|VisualizationOverlayPass|ImGuiPass|PresentPass|DebugViewPass|MinimalTriangleReadbackHarness)\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Fixed review packets, source hashes and verification logs:
`/tmp/intrinsic-graphics-support/`. No GPU execution or sanitizer-suite result
is claimed.

Final checkpoint: the full exclusion-only CPU gate selected 4,765 cases;
4,764 passed, one expected ASan-only GLFW lifecycle check skipped, zero
failures (151.89 s). Reviewed source/build hashes stayed fixed. Strict test
layout, layering, task policy/state links, doc links, root hygiene, skill mirrors
and session-brief freshness pass. Module inventory regenerates unchanged
at 429 modules. All work in this support slice is complete.


## Continuation — K-Means config-backed action readiness (2026-09-19)

Operator-directed duplication/compilation continuation with Claude Fable 5.1,
starting from `888d8f36b`. UI-037 remains open. The K-Means panel now composes
`PreviewEditorKMeansRun` with the existing compiled
`ResolveEditorProcessingActionReadiness` owner instead of merging a copied
config-availability flag and inventing its own disabled reason. Missing config
controls take the shared reason priority. Apply/Reload retain their distinct
local draft policy. No new abstraction, file or production dependency is added.

Fable's plan review correctly distinguished the composite config-backed action
from the config-independent method preview/submit pair; those runtime contracts
remain unchanged. New runtime coverage exercises the composed action with ready,
missing-config, missing-service, invalid-request and expired-attachment states,
unchanged canonical rejection messages, metadata-only preview without callbacks
or publication, and a positive queued submission without config state. Existing
ImGui tests cover disabled hover/click and enabled command dispatch; this slice
does not add a dedicated real K-Means panel interaction test. The production
composition is checked in fixed-diff review, not a duplicate source-string test.

Initial focused verification passed 16 tests; the combined locality slice passed
77 focused cases. Fable's fixed-diff review prompted isolated alias tests for that
slice and the positive config-independent submission case; both refined tests
pass, and the final delta review has no blocking finding. The source/build hashes
and review packets are retained in `/tmp/intrinsic-kmeans-readiness/`.

Verification uses canonical ci/Clang 23 without sanitizers. The following
checkpoint records the combined full CPU gate and structural results. No
elapsed compilation-speed, GPU execution or sanitizer-suite result is claimed.


## Continuation — consolidation property-validator locality (2026-09-19)

The second operator-directed slice moves the unchanged property-reference
predicate into the existing `Runtime.PointCloudConsolidationTypes` owner.
The lifecycle module re-exports that declaration and uses it for source
preflight; the method panel imports Types directly. The former private
`HasValidPropertyRefs` body and exported forwarding wrapper are gone. No new
module, header, service, config or dependency is introduced. Function body
tokens match baseline after the declared name/attribute/whitespace changes;
optional-normal, alias and domain semantics are unchanged.

Clang's method-panel module map drops from 156 to 123 dependencies, removing
33 entries including the lifecycle, consolidation algorithm, point LBVH,
spatial-cache and RHI-device modules. The new
`EditorCompilationLocality.MethodPanelServices` checks the recursive compiler
dependency closure; it rejects the original lifecycle import and passes after
the move. Five affected production files total 6,563 -> 6,556 physical lines
(-7); the compiler guard adds seven CMake lines, leaving that combined scope
unchanged. New behavior/boundary coverage is additional test code. These counts
are structural evidence, not a measured compilation-speed improvement.

Fable reviewed the plan and fixed diff, then accepted the isolated alias-test
corrections. The pre-merge sweep passes: one intent per commit, unchanged layer
policy, validated behavior, current docs. Workshop rows 1–3/8 pass; 4–7 n/a.
Strict layering has zero exceptions. Source-documentation audit for both
interfaces: zero errors/findings. The module inventory remains 429 modules.
The build retains a pre-existing initializer-order warning in the consolidation
test fixture; it is unrelated to the moved validator.

Exact combined verification:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(ClusteringModule\.|PointCloudConsolidationModule\.|PointCloudConsolidationConfig\.|SandboxPointCloudConsolidationPanel\.|SandboxEditorPresentation\.|NormalEstimationConfig\.RoundTripAndSharedPreviewApplyRun|ProcessingCompilationLocality\.Consolidation|ProcessingCompilationLocality\.PointCloudService|EditorCompilationLocality\.(PointCloudServices|MethodPanelServices))' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

77 focused cases pass. Both refined tests pass after review corrections.
Final exclusion-only CPU gate: 4,768 selected, 4,767 passed, one expected
ASan-only GLFW lifecycle check skipped, zero failures (152.41 seconds).
Strict task policy/state links, test layout, doc links/sync, root hygiene,
skill mirrors and session-brief freshness pass. Source/build hashes remain
fixed through final verification; logs and review packets are in
`/tmp/intrinsic-kmeans-readiness/`. No GPU execution, sanitizer-suite or elapsed
compile-time result is claimed. UI-037 remains open; these two slices are complete.


## Continuation — texture-bake property type locality (2026-09-19)

Operator-directed compilation/duplication cleanup with Claude Fable 5.1 from
`ff7e8098d`. The bake interface now imports the existing
`Runtime.GeometryProperty.Types` and `Geometry.Properties.Types` owners;
presentation and live property storage stay in its implementation. All
declarations and bodies are unchanged. No new module, helper or dependency
edge outside the existing layer policy is introduced.

The recursive `EditorCompilationLocality.TextureBake` guard rejects the
original interface and passes the narrowed imports. Clang's module map drops
25 -> 19 dependencies, removing presentation, geometry availability, ECS
geometry sources, render geometry, linear algebra and live property storage.
The production file remains 296 lines; four guard lines and three current-state
architecture lines document/protect the boundary. This is structural evidence,
not an elapsed compilation-speed claim.

Committed as `b96c3eb0f`. Fable reviewed the plan and fixed diff. The implementation imports its
required presentation/storage owners; rebuilding callers checks accidental
transitive-import reliance. Source documentation audit: zero findings/errors.
Inventory remains 429 modules. Strict layering and test layout pass.
Combined verification is recorded below; logs and packets are retained in
`/tmp/intrinsic-bake-locality/`. UI-037 remains open.


## Continuation — consolidation strategy-validation reuse (2026-09-19)

The panel's request builder and config-valid flag now reuse
`IsValidEditorPointCloudConsolidationConfig` for strategy admission. Deleted
the helper that rebuilt/scanned menu options and the duplicate stable-token
check. All four menu choices remain available; backend capability admission
stays with runtime preflight. A future unavailable strategy needs a runtime
admission rule rather than a menu-only validation policy.

Fable's plan review identified the important equivalence checks: unknown
strategies serialize to an empty stable token, enum parsing marks fallback,
and editor validation requires exactly `Valid`. Source inspection confirmed
each. The existing public panel-request test now directly checks canonical
validation and request preservation for all four strategies, plus isolated
unknown values 4, 999 and UINT32_MAX. Invalid entity, property and radius
cases remain. No new helper, module or production file was added. The two
production files total 3,603 -> 3,581 lines (-22); additional
test code protects the shared admission boundary. The app-private header's
source-documentation audit has no findings/errors.

Fable's fixed-diff review has no blockers. Its two conditional concerns are
resolved: the builder still calls the canonical validator, the request owns
both config copies, and all added cases compile and pass. Reviewed source/build
hashes stayed fixed through verification. Pre-merge scope/layer/tests/docs
sweep passes; workshop rows 1–3/8 pass and 4–7 are not applicable.

Exact combined verification (canonical ci, Clang 23, unsanitized):

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(RuntimeTextureBakeModule\.|PointCloudConsolidationConfig\.|PointCloudConsolidationModule\.|SandboxPointCloudConsolidationPanel\.|SandboxEditorPresentation\.|EditorCompilationLocality\.)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

All 88 focused cases pass. Full CPU gate: 4,768 selected, 4,767 passed, one
expected ASan-only GLFW lifecycle check skipped, zero failures (153.05 s).
The build retains an unrelated ignored-nodiscard warning in the existing mesh
fixture. Strict layering (zero exceptions), test layout, task policy/state links,
doc links/sync, root hygiene, skill mirrors and session-brief freshness pass.
All 26 compile-hotspot tooling tests pass. No GPU execution, sanitizer-suite
result or elapsed compilation-speed improvement is claimed.

UI-037's broader readiness/cache work remains open; these slices are complete.
The consolidation slice does not change per-frame property scans or Run-button
behavior. Evidence: `/tmp/intrinsic-bake-locality/`.


### 2026-09-19 continuation — consolidation lifecycle and option lookup

Operator direction continues duplication and compilation cleanup outside the
standing Framework24 work-selection focus. The checkout starts clean at
`5ca6c3cf1`; Codex is sole writer, Claude Fable 5.1 reviews bounded packets.

Plan/reuse decisions:

- Remove four implementation-only imports from the consolidation lifecycle
  interface. Reuse existing globally attached C++ owners for the three borrowed
  service pointers; keep module-attached history/job types and value records.
  The existing implementation already imports all four full owners. Add the
  configured-compiler boundary guard, with a recorded baseline failure.
- Replace the six duplicated private K-Means/Progressive-Poisson option mapping
  functions with two file-private array helpers, used directly by three current
  option families. Preserve array order, clamped indices, first-match lookup and
  unknown-value fallback to zero. Search of app/editor/core found no matching
  shared owner. No public API, file, wrapper or policy flag is needed.
- Keep consolidation Run readiness/cache work open: its current preflight scans
  full position/normal buffers. Correct per-frame readiness needs generation-
  keyed results, not a new wrapper around those scans.

Fable reviewed the plan without blockers. Right-sizing: three present callers
justify the two local helpers; borrowed declarations avoid a new lifecycle
facade or owning record. Existing contracts cover source documentation,
processing/kernel compilation locality and unchanged property semantics.
Both implementation slices are complete. Lifecycle commit: `cd9a9b2b8`.
The configured Clang module closure
for the lifecycle interface is 72 -> 32. The new boundary guard failed on all
four forbidden modules against the built clean parent and passes after the
change. Interface size is 69 -> 76 lines (borrow declarations); the existing
implementation already explicitly imports all required owners. The test build
entry adds 10 lines. No new production file or API was introduced.

The method-panel helper change is 3,475 -> 3,427 lines: six private mapping
functions become two local templates, with nine direct uses across three
option arrays. Other `find_if` paths return property pointers or search strategy
records and have different contracts. Their behavior remains separate.

Fable's fixed-diff review found no blockers. The omitted owner excerpt was
checked directly: `Runtime.WorldRegistry.cppm` lines 51–53 put the class itself
inside `export extern "C++"`. Both implementation units and external importers
built. Its optional test-name change was declined to retain the existing
editor-locality naming family; the retained state clamps normalize UI state
and are not redundant with the read-only mapping helpers. Source/build hashes
remain fixed through review and verification.

Scope/layering/tests/docs sweep passes. Workshop rows 1–3/8 pass; 4–7 are not
applicable. Strict layering (zero exceptions), test layout, task policy/state
links, docs sync/links, root hygiene, skill mirrors and session-brief freshness
pass. Inventory regenerated, unchanged at 429 modules. All 26 compile-hotspot
tooling tests pass. Source documentation: zero errors, one pre-existing file-
size review hint for MethodPanels; this slice reduces it without fragmenting
its implementation. No correctness finding remains.

Exact verification (canonical ci, Clang 23, unsanitized):

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(PointCloudConsolidationModule\.|PointCloudConsolidationConfig\.|SandboxPointCloudConsolidationPanel\.|EditorCompilationLocality\.ConsolidationLifecycle$|EditorCompilationLocality\.PointCloudServices$)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(SandboxEditorUi\..*(Clustering|KMeans|ProgressivePoisson)|SandboxEditorPresentation\.|SandboxPointCloudConsolidationPanel\.|PointCloudConsolidationModule\.|PointCloudConsolidationConfig\.|EditorCompilationLocality\.)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

All 26 first-slice checks and 106 combined focused tests pass. Full CPU gate:
4,769 selected, 4,768 passed, one expected ASan-only GLFW lifecycle skip, zero
failures (153.02 s). The focused build reports one pre-existing member-
initialization-order warning in the consolidation test fixture. No GPU execution, sanitizer-suite result or elapsed compilation-speed
improvement is claimed. UI-037's broader readiness acceptance remains open.
Evidence and review packets: `/tmp/intrinsic-consolidation-locality/`.


## Continuation — clustering lifecycle dependencies (2026-09-19)

Operator-directed compilation/duplication cleanup continues from `939828561`,
with Codex as sole writer and Claude Fable 5.1 reviewing bounded packets.
Reuse: `ClusteringModule` only borrows `RHI::IDevice` and `WorldRegistry`;
matching non-exported C++ declarations reuse their existing globally attached
owners, as consolidation already does. Its implementations retain explicit
owner imports. No API, class layout, algorithm or lifetime behavior changes.

`EditorCompilationLocality.ClusteringLifecycle` fails on both forbidden owners
against the built baseline, then passes. The recursive configured Clang module
closure is 39 -> 25; this is dependency evidence, not measured compile speed.
The interface is 57 -> 62 lines and test registration adds seven lines; no new
production file, wrapper or dependency is introduced. Runtime architecture docs
are synchronized; the regenerated inventory remains at 429 modules.

Fable's plan and fixed-diff reviews found no blocker. Its app-importer concern
was checked by compiling/linking `ExtrinsicSandbox`, including `main.cpp`, with
a temporary ci preset override, then restoring the normal ci configuration.
The original focused gate passed all 13 checks. Exact commands:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(ClusteringModule\.|EditorCompilationLocality\.ClusteringLifecycle$|EditorCompilationLocality\.ConsolidationLifecycle$)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target ExtrinsicSandbox -j4
cmake --preset ci
```

Evidence, baseline failure, compiler closures and immutable review packet:
`/tmp/intrinsic-clustering-locality/`. Combined CPU verification is recorded in
the following parameterization checkpoint; UI-037 remains open.


## Continuation — shared parameterization dropdowns (2026-09-19)

Alongside clustering commit `e9e5afc74`, replace four repeated enum dropdown
loops in `Sandbox.MethodPanels.cpp` with file-private
`DrawParameterizationChoice`. Reuse search found no matching shared owner:
property selectors consume catalog records, while domain controls map integer
indices. Four present callers justify this local template; no header, public
API, policy flags or broader widget framework is added. Strategy-record drawing
stays separate. BFF's boundary-data clearing remains in its caller, including
reselection of AutomaticConformal. Labels, IDs, option order, unknown-value
preview, focus ordering and boolean accumulation are unchanged.

Production size is 3,427 -> 3,385 lines including the helper and all callers.
Fable's fixed-diff review found no correctness defect; reviewed source/build
hashes remained unchanged through verification. Existing real-window and typed
config/action tests pass; no new source-string UI assertion was introduced.
No elapsed compilation-speed improvement is inferred from this consolidation.

Combined verification, canonical ci / Clang 23 / unsanitized:

```bash
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(ClusteringModule\.|SandboxParameterizationPanel\.|ParameterizationOperations\.|SandboxEditorPresentation\.|EditorCompilationLocality\.)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

All 104 combined focused tests pass. Full CPU gate: 4,770 selected, 4,769 passed,
one expected ASan-only GLFW lifecycle skip, zero failures (153.22 s). Standalone
Sandbox compilation/linking also passed as recorded above. No GPU execution or
sanitizer-suite result is claimed. Scope/layering/tests/docs sweep passes;
workshop rows 1–3/8 pass, 4–7 are not applicable. Strict layering (zero
exceptions), test layout, task policy/state links, docs sync/links, root hygiene,
skill mirrors/session-brief freshness and all 26 compile-hotspot tooling tests
pass. Source documentation has zero errors and one pre-existing MethodPanels
size hint, reduced by this deletion. UI-037's broader readiness work remains
open. Logs and review packets: `/tmp/intrinsic-clustering-locality/`.


## Continuation — consolidation readiness cache (2026-09-19)

Operator-directed continuation from `c225b2135` prioritizes the previously
identified per-draw consolidation scans. Codex is the sole writer; Claude Fable
reviews bounded packets. Reuse: canonical property revisions, finite predicate,
consolidation admission rules, command drain and shared action button/tooltip.

Right-sizing decision: two feature-owned property-verdict slots replace repeated
position/normal scans. Missing verdicts queue a private command; preparation
reads metadata only and pending disables Run. The drain validates the captured
world epoch/entity/domain/name/revision/count before a finite scan. Normal-only
edits reuse position results; config, output and deleted-slot metadata stays live.
`FindPropertyRevision` observes the edit epoch, so repeated edits invalidate.
The direct side-effect-free preflight and command submission still revalidate.

Fable recommended the existing main-thread command drain over copying up to two
whole buffers into a worker job. This concrete bounded derivation uses no worker,
new module, service or snapshot allocation; the scan happens once per requested
revision outside model construction. Explicit `PrepareAvailability` owns the
queue side effect. A future background path requires evidence that this deferred
scan causes a hitch, and must preserve these keys and pending behavior. This is
a documented implementation choice for this slice, not a relaxation of the
no-scans-in-model-construction or stale-result requirements.

The editor now imports the service Types owner instead of the lifecycle and
reuses the shared action button. The existing PointCloudServices compiler guard
also covers the operation implementation. UI-037 remains open for other families.
Fable's fixed-diff review caught pending results masking metadata failures.
The corrected shared inspector checks all metadata first, then requests both
finite verdicts for one drain; a metadata failure drops unfinished checks.
Invalid counts now take the metadata `TypeMismatch` reason before finite checks,
including multiple-failure inputs. The cache deliberately serves one active
preview, with old requests superseded. Its command scan remains on the main
thread; no elapsed frame-time/compile-time improvement is claimed.

Nine new runtime tests cover steady requests with zero additional scans/queues,
repeated edits before/after drain, retained mutable borrows, independent normal
invalidation, removal/replacement, world epochs, recycled entities, every element
domain and strategy, metadata/count rejection without work, changing previews,
shutdown/reinitialization and command-time revalidation. Editor domain tests now
assert pending then readiness after the drain. Existing disabled-tooltip/click
and detached-frame tests remain in the gate. Fable's follow-up found no blocker;
the two omitted-source questions were checked directly: property revisions use
a global atomic counter and the handler clears `Queued` before stale validation.

Seven affected production files total 6,769 -> 6,898 lines (+129), justified by
the bounded cache and preparation behavior; no new production file/module/service
is added. The editor wrapper loses duplicated entity/preflight wiring and a
lifecycle import. The strengthened compiler boundary guard passes. Inventory
remains 429 modules. Scope/layering/tests/docs sweep passes; workshop rows 1–3/8
pass, 4–7 n/a. Source documentation has zero errors; its remaining review hints
are the two existing large implementations and necessary borrow/preparation
contract comments.

The first focused run exposed a new test's missing event pump and callback
cleanup; those were fixed. An interim CPU run overlapped a review edit and its
compiler dependency-freshness check correctly rejected the changed interface.
Final verification rebuilds and runs with all source/build files frozen.

Verification (canonical ci, Clang 23, unsanitized):

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^(ConsolidationReadiness\.|PointCloudConsolidationModule\.|PointCloudConsolidationConfig\.|SandboxPointCloudConsolidationPanel\.|SandboxEditorPresentation\.|SandboxEditorSessionLifecycle\.|EditorCompilationLocality\.|ProcessingCompilationLocality\.PointCloudService$)' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

All 102 focused checks and 26 compile-hotspot tooling tests pass. Final CPU
gate: 4,779 selected, 4,778 passed, one expected ASan-only GLFW lifecycle skip,
zero failures (157.21 seconds). Source/build hashes stayed fixed through this
final build and run. Strict task policy/state links, layering (zero
exceptions), test layout, doc links/sync, root hygiene, skill mirrors and brief
freshness pass. The existing consolidation fixture initializer-order warning
is unchanged. No GPU execution or sanitizer-suite result is claimed. Logs and
review packets: `/tmp/intrinsic-consolidation-readiness/`.

## Continuation — deferred shared point-input readiness (2026-09-19)

Operator directs continued duplicate-code/compilation work with Claude Fable 5.1,
starting at `62cb78abb`. This slice follows the handoff's substantive shared
point-scan gap. It covers the common point-input catalog, outlier readiness and
keypoint readiness. Normal PCA/topology readiness remains a subsequent UI-037
slice; the broad task checklist stays open.

Reuse/right-sizing decision: `PointProperties.cpp::CapturePointInput` already
owns live-row filtering, finite/LBVH/subnormal classification and compact counts.
Split its metadata/row work in that compiled owner and reuse the row function
for execution and deferred readiness. Consolidation's whole-buffer finite
predicate is not substitutable: point methods exclude deleted samples and map
halfedge pairs to edge deletion flags. There is no new algorithm, service class,
module, worker job, or public cache template. The existing engine `CommandBus`
is discoverable through the existing service registry so session-owned state can
request work at the established main-thread command drain; a dummy worker job
would add scheduling without any safe worker-side ECS work.

The shared processing interface gains only an opaque session state pointer and
copied diagnostic counters. Cache keys include scene/world/epoch, versioned
entity, canonical positions and both input-property watches. Requests replace
superseded revisions; entries unused in the previous prepared frame expire, so
closed panels cannot accumulate historical sources and large catalogs do not
starve behind a fixed entry cap. Queue payloads hold weak references. Metadata
validation precedes lookup, negative row verdicts are reusable, and commands
still recapture rather than trusting readiness. Catalog generations include
accepted membership; the combo keeps its binding when a pending entry is omitted.
Prepared sessions never fall back to scans when command wiring is unavailable;
explicit standalone processing contexts retain synchronous validation.

- [x] Plan review with Claude Fable 5.1 and bounded Codex Sol ownership review.
- [x] Implement one shared deferred verdict for catalog/outlier/keypoint queries.
- [x] Verify lifecycle, revision/deletion invalidation, negative caching,
      supersession, full command revalidation and unchanged direct contracts.
- [x] Finish fixed-diff review and strongest relevant CPU/structural checks.

Development evidence and immutable review packets: `/tmp/intrinsic-point-readiness/`.
No elapsed compilation/frame-time speedup, GPU execution or sanitizer-suite
result is claimed. The implementation adds cache behavior; it is not a net
source-line reduction.

Review closure: Fable's revision/handler questions were checked against the
process-monotonic property token contract and replace-on-registration command
bus. Catalog hashes include domain/name as well as revisions; attached discard
paths invalidate snapshots, and queue failure is distinct from pending.
The shell prepares once per `DrawFrame` and then borrows the prepared frame for
all families. Twelve new runtime regressions include a real four-frame engine
run with selected-model construction (one queued check/one scan), an 81-property
catalog without starvation, absent command wiring, paired halfedge masks,
negative verdicts, property/remove-readd revisions, retained borrows, superseded
work, detach/world changes, expiry and apply-time revalidation. Retained-world
statistics assertions first prove the handle is bound.

Initial focused verification passed 176 cases. The first complete CPU run
caught two integration failures: standalone catalog generations no longer
matched sibling input catalogs, and the built-in-service test expected the
command bus to be unpublished. The fix restricts asynchronous membership hashing
to prepared session state, preserving the existing standalone contract, and
pins the consumed built-in bus to `Engine::Commands()` plus null after shutdown.
The 14 affected/new tests pass after these fixes. Neither failure was pre-existing;
no gate was weakened. Development compile fixes also supplied a private forward
declaration, an explicit Graph test import and the existing public
`RuntimeTestModule` frame seam instead of calling private `Engine::RunFrame`.

Pre-merge sweep: one UI-037 readiness/cache intent, runtime-only ownership,
no new layer or link edge, command revalidation preserved. Clean-workshop rows
1–3 pass; renderer/pass/recipe rows 4–6 and closure row 7 are not applicable;
row 8 passes with no exceptions. Automated workshop, strict layering, task,
test-layout and doc-link checks pass. Source-doc audit: zero errors, eleven
review hints (ten lifetime/ordering/capture contract comments and the existing
four-line PointFields synopsis). Module inventory remains 429; all 26
compile-hotspot tooling tests pass. The strengthened processing-service borrow
guard also forbids the command-bus module from shared editor interfaces.

Final verification: canonical ci/Clang 23, unsanitized. `IntrinsicTests` builds;
standalone `ExtrinsicSandbox` compiles and links with a temporary ci app override,
then the normal ci configuration is restored and `IntrinsicTests` rebuilt.
The final CPU gate selects 4,791 tests: 4,790 pass, one expected ASan-only GLFW
lifecycle skip, zero failures (154.12 seconds). Source hashes remained fixed
through final build/test. Final Sol source review found no blocker after the
Fable review closure and integration fixes.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -R '^EditorPointReadiness\.|^KeypointAnalysis|^OutlierAnalysis|^SandboxEditorSession|^SandboxProcessingPanels\.|^SandboxEditorPresentation\.|^ProcessingCompilationLocality\.|^EditorCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^EditorPointReadiness\.|^PointSpacingOperations.InputCatalogsShareRevisionMetadataWithoutChangingEligibility$|^RuntimeModule.EnginePublishesOnlyConsumedBuiltInServices$' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target ExtrinsicSandbox -j4
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Production scope is nine files, 3,688 -> 3,943 physical lines (+255), including
the new 15-line private declaration header; test registration adds one CMake
line. This adds the required cache lifetime/validation behavior while sharing
one compiled scanner, rather than reducing net lines. No elapsed compile-time
or frame-time speedup is inferred. A changed source is scanned once during
command drain, so this is not worker parallelism or a per-drain time budget.
UI-037 stays open for remaining per-frame method readiness scans and its broad
action inventory. This is a completed session checkpoint.


## Continuation — shared normal point capture (2026-09-19)

Operator-directed duplication/compilation continuation from `338e162c7`, with
Codex as sole writer, Claude Fable 5.1 plan/fixed-diff review, and Codex Sol for
one bounded read-only import inspection. UI-037 remains active.

Reuse decision: normal execution and preview now consume the existing compiled
`CapturePointInput` / `PreparePointInput` owner in `PointProperties.cpp`.
Positions/deletion watches, finite checks, live counts, LBVH limits and ascending
source slots match. Execution moves captured inputs/points/slots and reconstructs
its full deletion vector from live slots before topology reconstruction. Commands
still synchronously recapture current inputs. No new production file, helper,
module, cache, job or dependency is added; the normal implementation loses six
physical lines (666 -> 660). No elapsed compilation or frame-time improvement is
claimed. Removing the duplicate loop is source consolidation, not benchmark evidence.

Normal output/config metadata remains ahead of capture. Prepared normal previews
now share pending/negative verdicts with catalogs, outliers and keypoints, including
all topology variants. Missing command wiring fails closed. Standalone contexts
retain synchronous validation. Deletion diagnostics adopt the shared owner's
specific type/cardinality reasons. Backend/topology checks follow the accepted
point verdict; topology deletion-mask copies remain synchronous and explicitly open.

The proposed attachment forward declaration was rejected during source review:
`EditorWorkspaceAttachment` and `RuntimeEngineConfigApplyResult` are attached to
named modules without matching global C++ linkage. Introducing an unmatched
forward declaration would change type identity. No interface dependency is removed
by this slice; a future owner-level locality change needs its own bounded review.

Five new runtime cases cover normal-only pending and negative verdict reuse,
retained-borrow invalidation, output priority, PCA zero/minimum counts and LBVH
coordinate gates, topology variants with live mask metadata, and backend reason
order, plus executed graph/mesh vertex and paired-halfedge output preservation.
Existing fixture coverage now includes normals in shared-cache, paired
halfedge-mask, unavailable command queue and stale-command tests. The first
focused run passed 157/158; its sole failure was a new test expecting a later
preflight message instead of the existing config validator's earlier message.
The expectation was corrected against the validator; production behavior and
assertion strength were preserved.

Fable fixed-diff review prompted stronger execution coverage, a positive CPU-LBVH
control with an actual spatial-cache module, and an exact stale-command rejection
assertion through the normal prepared frame. The topology fixture now retains a
live face rather than pinning the pre-existing all-deleted-face readiness behavior.
Fable accepted the follow-up fixed diff with no blocker. Existing
`EveryCanonicalDomainPublishesNamedNormalsAndSupportsUndoRedo` already executes
CPU-LBVH with deleted point rows, and the dedicated backend-reason test pins
capability failure after point validation. Remaining optional interior-row
coverage and all-deleted-face semantics are listed below.
The frame-accessor concern was checked directly: `VisitPreparedFrame` only visits
stored context; it does not advance frames or evict cache entries. The duplicated
metadata checks deliberately retain normal diagnostic priority. Source is unchanged
since the reviewed implementation; later edits strengthen tests/documentation.

An intermediate full CPU run passed all 4,794 executed tests (one expected ASan-only
skip among 4,795 selected; 155.57 s). The final stronger-test gate is recorded below.
Logs/immutable review packet: `/tmp/intrinsic-normal-readiness/`.

### Final verification for shared normal capture

Canonical ci / Clang 23, unsanitized: focused targets and `IntrinsicTests` build
pass. After review fixes, all 38 focused normal/readiness tests pass. Final
exclusion-only CPU gate selects 4,796 tests: 4,795 pass, one expected ASan-only
GLFW lifecycle skip, zero failures (154.28 seconds). SHA-256 checks confirm the
production/test sources remained fixed through the final build and run.

Strict layering (zero exceptions), test layout, task policy/state links, root
hygiene, doc links/sync, skill mirrors and session-brief freshness pass. Source
annotation audit: zero errors and zero review hints in the changed production
unit. Module inventory regenerated unchanged at 429. No new module interface,
CMake producer, layer/link edge or compatibility path. Scope/layering/tests/docs
sweep passes; workshop rows 1–3/8 pass, renderer/pass/recipe/closure rows 4–7 n/a.
This is CPU evidence; no new GPU execution or sanitizer-suite run is claimed.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^EditorPointReadiness\.|^NormalEstimation' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

### Open points at the preceding checkpoint

- [ ] Remove remaining synchronous topology deletion-mask copies/counts from
      normal readiness without changing graph/mesh/face execution, reason order,
      deletion semantics or command-time revalidation.
- [ ] Add an interior-deletion/nonuniform-normal regression if extending normal
      slot mapping; current execution tests pin prefix deletion, paired halfedges,
      preserved output and existing CPU-LBVH behavior. During the topology-readiness
      slice decide the intended all-deleted-face availability against command
      behavior; this continuation preserves the existing enabled preview.
- [ ] Continue feature-by-feature scan/copy inventory and cache adoption for
      density, density weights, spacing, bilateral filtering, descriptors,
      construction, registration and mesh/UV/bake/parameterization readiness.
      Reuse the point verdict only where contracts match; keep normal/topology/
      paired-source predicates with their owners. Prove steady-frame zero scans,
      negative caching, revision/deletion invalidation, lifecycle and supersession
      for every newly cached predicate.
- [ ] Complete the initial action/backend/variant readiness matrix and common
      `ActionReadiness` representation in every family-owned prepared frame;
      remove remaining duplicate app validation and action-hiding early returns.
      Keep disabled reasons deterministic, actionable and shared with agents.
- [ ] Finish per-family disabled/enabled button and selectable-option tooltip /
      command/no-command coverage. The shared two-frame tooltip test is already
      complete; it does not prove the entire action inventory.
- [ ] Complete the named table-driven
      `SandboxEditorUi.ActionReadinessDerivesDomainPrerequisiteReasons` closure
      test, including ICP distinct compatible sources and finite count-matched
      point-to-plane normals, parameterization strategy prerequisites, UV/bake
      prerequisites and unavailable backend choices.
- [ ] Continue bounded compilation-owner inspection; preserve named-module type
      identity. Re-run configured compiler boundary guards for each accepted
      change. A compilation-speed claim still requires matched measurements;
      this slice supplies none. BUILD-006/CI-012 remain separate task owners.
- [ ] Review whether deferred main-thread scans need a measured per-drain budget
      or worker path; no worker parallelism or hitch-free frame claim is made.
      Do not introduce a second cache/service without evidence.
- [ ] Close broad acceptance only after the complete action inventory, stale
      apply/async completion guards, co-equal control surfaces, no-scan readiness
      and app-linked operational checks pass. This checkpoint does not retire
      UI-037 or claim GPU/sanitizer execution.

Session boundary: after this verified commit, start a fresh session using this
checkpoint and initial task scope. Avoid rereading the full historical task note;
its accumulated history is much larger than the remaining slice context.


## Continuation — shared scalar readiness (2026-09-19)

Operator-directed duplication/compilation continuation from `47e446184`, with
Codex as sole writer and Claude Fable 5.1 providing plan and fixed-diff review.
This closes estimated slice 1; UI-037 remains active. The original planning
estimate now has eleven remaining slices (rough range 9–13), not promised sessions.

Reuse: the existing compiled `CapturePointScalarField` now routes previews through
`PreparePointInput` and execution through fresh `CapturePointInput`. Density,
spacing and density weights share pending/negative/revision-keyed input verdicts
with other point families. Output validation remains after input validation;
method minimum counts and backend/radius/subnormal gates retain their owners.
Commands can submit while readiness is pending and always recapture current rows.
No new service, cache, module, template, worker or production file was added.

The density/spacing catalog loops and synthetic output/config validation are
replaced by `BuildPointInputCatalog(context, id, 2)`. Config inspection confirms
no lost name-length/character predicate; empty names fail canonical resolution.
The generic catalog still requires one live sample. Pending entries remain absent,
accepted membership changes its generation, and the combo's current binding is
independent of membership. Catalog eligibility stays backend-independent.

Five production files total 1,411 -> 1,380 physical lines (-31). Four redundant
direct imports, three array includes, two unused numeric includes and two unused
domain aliases were removed. The imports remain in the transitive closure; this
is duplicate-source/unused-import cleanup, not a measured compilation speedup or
compiler-closure reduction. BUILD-006/CI-012 remain separate owners.

Seven new runtime cases cover shared catalogs/verdicts, steady-frame scan counts,
negative caching and retained-borrow invalidation, output metadata, one/two/zero
sample boundaries, backend/LBVH/subnormal gates, diagnostic order, pending-command
submission and stale-command rejection. Existing paired-halfedge, supersession
and missing-command-queue cases now exercise scalar consumers too. Existing family
tests retain synchronous standalone validation and execution/history coverage.

Claude Fable 5.1 accepted the fixed production/test diff with no blockers. Plan
review prompted explicit pending-output-order and pending-submit tests. Its final
optional coverage suggestions (scalar catalog checks with missing queue, and
mixed-count multi-domain catalogs) remain below; existing public-command, generic
catalog and per-domain execution tests cover the underlying paths. Documentation
reflow and the task checkpoint followed review; production/test source stayed fixed.

Canonical ci / Clang 23, unsanitized: focused targets build without warnings;
all 151 focused tests pass, including 34 processing compilation-boundary guards.
The `IntrinsicTests` build passes. The final exclusion-only CPU gate selects
4,803 tests: 4,802 pass, one expected ASan-only GLFW lifecycle skip, zero failures
(155.38 seconds). The reviewed production/test diff stayed unchanged through
the final build and test run (SHA-256 `a674a93d63572968d98d2357abdee12fc564f87edd0d987d62e4b50eb31f5079`).
Strict layering (zero exceptions), test layout, task policy/state links, root
hygiene, docs sync, doc links, skill mirrors and session-brief freshness pass.
Module inventory regenerates unchanged at 429. Source-documentation audit finds
zero errors and eight advisory hints in the existing private header; reviewed
comments describe required include/capture/lifetime contracts. Scope/layering/
tests/docs review passes. Workshop rows 1–3/8 pass; renderer/recipe/closure rows
4–7 do not apply. No public module surface, CMake edge or compatibility path changes.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^EditorPointReadiness\.|^PointSpacingOperations\.|^KernelDensity|^DensityWeight|^SandboxProcessingPanels\.|^SandboxEditorPresentation\.|^ProcessingCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
Logs and immutable review packet: `/tmp/intrinsic-scalar-readiness/`.

### Current open points

- [ ] Bilateral/descriptor readiness: reuse the point verdict where it fits and
      cache their position-plus-normal predicates with revision/deletion guards.
- [ ] Point-construction readiness: eliminate per-preview point capture through
      the existing owner, preserving method-specific prerequisites.
- [ ] Normal topology readiness: remove deletion-mask copies/count scans; decide
      all-deleted-face availability against apply. Add interior-deletion/nonuniform
      normal coverage if changing the mapping; existing mapping is unchanged here.
- [ ] ICP: cache paired-source and point-to-plane normal readiness; preserve
      distinct compatible sources, finite/count-matched normals and stale guards.
- [ ] Mesh/curvature/UV: close admission gaps against existing metadata validators.
      Parameterization still needs runtime-owned strategy/pin/boundary prerequisites;
      texture bake needs request-specific property/UV/device/range readiness.
- [ ] Complete action/backend/variant readiness for service actions (K-Means,
      Progressive Poisson, consolidation, outliers), use the common prepared-frame
      representation everywhere, and remove remaining duplicate app prerequisites
      and hidden actions while retaining co-equal config/UI/agent validation.
- [ ] Finish the named table-driven
      `SandboxEditorUi.ActionReadinessDerivesDomainPrerequisiteReasons` matrix,
      full-family invalidation/lifecycle/supersession and zero-scan coverage, plus
      per-action/option enabled-command and disabled-tooltip/no-command ImGui tests.
- [ ] Run final task-wide operational closure only after the full action inventory,
      stale apply/completion guards and app-linked checks pass; this checkpoint
      neither retires UI-037 nor supplies new GPU or sanitizer execution evidence.
- [ ] Optional scalar-catalog coverage: explicitly assert both scalar catalogs
      are empty without a command queue, and cover mixed live counts across
      multiple candidate domains. These are review suggestions, not blockers.
- [ ] Non-gating: bounded compilation-owner inspection and matched compile-time
      measurements remain separate work; preserve named-module type identity.
      Consider a per-drain budget or worker path only if measured latency warrants
      it. This shared deferred scan is still main-thread work.

Session boundary: after the verified checkpoint, start a fresh session from this
section and the initial scope. Next recommended slice is bilateral/descriptor
position-plus-normal readiness; do not reread the accumulated historical slices.


## Continuation — shared point/normal readiness (2026-09-19)

Operator-directed duplication/compilation continuation from `b22ad36c7`, with
Codex as sole writer and Claude Fable 5.1 providing plan and fixed-diff review.
Closes estimated slice 2; UI-037 remains active, with ten estimated slices left
(rough planning range 8–12). Earlier checkpoint open-point lists are historical;
the current list is below.

Reuse: bilateral and descriptor capture now compose `CapturePointInput` /
`PreparePointInput` through the compiled `CapturePointNormalInput` in
`Runtime.GeometryProcessingOperations.PointProperties.cpp`. Position and normal
properties reuse independent entries of the existing cache, including catalog
entries, negative verdicts, revision/count/deletion keys and attachment/world
lifetime guards. Both requests enqueue in one preview, so one command drain can
resolve the pair. There is no pair cache, additional service, module or worker.
The two work records inherit the ordinary shared capture record, as scalar work
already does. Bilateral's catalog uses `BuildPointInputCatalog(context, id, 2)`;
its synthetic configuration and repeated scan are removed.

The existing row scan also records zero/nonfinite vector flags. Zero uses masked
float bits, preserving signed-zero and subnormal semantics of the previous double
norm test. Bilateral accepts zero normals; descriptors reject them. Normal LBVH
bounds and subnormal flags do not become position/backend limits. Nonfinite
failures now name the position or normal role; output and normal metadata retain
priority over deferred input checks. Each family retains its output aliasing,
minimum count and backend predicates. Apply always recaptures current values,
uses the same ascending live slots, and retains revision-guarded publication and
undo. Execution now makes two independent property captures instead of one fused
loop; redundant normal slots are temporary. No execution-time speedup is claimed.

Four production files total 1,470 -> 1,456 physical lines (-14), including the
shared helper and capture records. Four unused direct imports and five standard
includes are removed; two GLM umbrella includes narrow to `glm/vec3.hpp`. Existing
named-module identity and public interfaces stay unchanged. These are source and
include reductions, not measured compile-time or transitive-module-closure gains.
Compile-time measurement and broader compiler-owner work retain their own owners.

Eight added runtime tests cover deferred pair reuse across frames/catalog/scalar
consumers, negative normal verdicts and retained-borrow invalidation, signed zero
and subnormal normals, same-property aliasing, metadata priority and zero/one live
count gates, position-only LBVH limits, supersession/deletion invalidation,
submission during pending readiness and fresh command-time normal validation.
Existing halfedge and missing-command-queue cases also cover both consumers.
Existing bilateral/descriptor family tests retain all-domain execution, history,
output aliasing and stale completion coverage. The focused ImGui normal-selector
regression passes; source review of `DrawProcessingPointInput` confirms an empty
pending catalog cannot clear the current normal binding: only selecting a row
assigns the bound property. The first focused run exposed
incorrect new descriptor fixture setup and diagnostic assumptions; those fixtures
were corrected without weakening production validation.

Verification: canonical ci / Clang 23, unsanitized focused build passes;
183/183 focused tests pass, including 34 processing compilation-boundary guards.
Claude Fable 5.1 reviewed the fixed production/test diff and found no blockers.
Its two documentation findings were checked and corrected: normal generation
retains additional topology-mask checks (point capture is shared), and the reuse
route now links the new oriented-input coverage. Optional follow-ups are listed
below. The full `IntrinsicTests` build passes without warnings. The exclusion-only
CPU gate selects 4,811 tests: 4,810 pass, one expected ASan-only GLFW lifecycle
skip, zero failures (156.53 seconds).
Strict layering (zero exceptions), test layout, root hygiene, task policy/state
links, docs sync, doc links, skill mirrors and session-brief freshness pass. Module inventory regenerates unchanged at 429. Source documentation
has zero errors and eight existing advisory comments/include-contract hints.
Scope/layering/tests/docs sweep and architecture review retain the existing runtime
owner and named-module surfaces; workshop rows 1–3/8 pass, rows 4–7 are inapplicable.
No new GPU or sanitizer evidence is supplied by this slice.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^EditorPointReadiness\.|^BilateralFilter|^DescriptorAnalysis|^PointSpacingOperations\.|^KernelDensity|^DensityWeight|^SandboxProcessingPanels\.|^SandboxEditorPresentation\.|^ProcessingCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
Current evidence files in `/tmp/intrinsic-normal-readiness/`: `configure.log`,
`build-focused.log`, `focused.log`, `build-all.log`, `cpu.log`, `plan.json`,
`review.diff` and `review.json`. Other files in that directory predate this slice
and are not evidence for it. Production/test source stayed fixed through review;
only the task record and the two small documentation corrections followed.
Fixed production/test diff SHA-256:
`e7e5687d1a2b7093d2ed7660ca61b7684a08a41cdb9d3a354885669071f6ebb2`.

### Current open points

- [ ] Point-construction readiness: eliminate per-preview point capture through
      the existing owner, preserving method-specific prerequisites.
- [ ] Normal topology readiness: remove deletion-mask copies/count scans; decide
      all-deleted-face availability against apply. Add interior-deletion/nonuniform
      normal coverage if changing the mapping; mapping is unchanged here.
- [ ] ICP: cache paired-source and point-to-plane normal readiness; preserve
      distinct compatible sources, finite/count-matched normals and stale guards.
- [ ] Mesh/curvature/UV: close admission gaps against existing metadata validators.
      Parameterization needs runtime-owned strategy/pin/boundary prerequisites;
      texture bake needs request-specific property/UV/device/range readiness.
- [ ] Complete action/backend/variant readiness for service actions (K-Means,
      Progressive Poisson, consolidation, outliers), use the common prepared-frame
      representation everywhere, and remove remaining duplicate app prerequisites
      and hidden actions while retaining co-equal config/UI/agent validation.
- [ ] Finish the named table-driven
      `SandboxEditorUi.ActionReadinessDerivesDomainPrerequisiteReasons` matrix,
      full-family invalidation/lifecycle/supersession and zero-scan coverage, plus
      per-action/option enabled-command and disabled-tooltip/no-command ImGui tests.
- [ ] Run final task-wide operational closure only after the full action inventory,
      stale apply/completion guards and app-linked checks pass; this checkpoint
      does not retire UI-037.
- [ ] Optional scalar-catalog coverage: explicitly assert both scalar catalogs
      are empty without a command queue, and cover mixed live counts across
      multiple candidate domains. Prior review suggestions, not blockers.
- [ ] Optional point/normal follow-up: avoid the temporary second slot-vector
      allocation during fresh execution if profiling justifies a wider capture
      API; explicit paired-size assertions would document an already enforced
      same-domain/deletion-order invariant. Neither blocks this slice.
- [ ] Non-gating: bounded compilation-owner inspection and matched compile-time
      measurements remain separate work; preserve named-module type identity.
      Consider per-drain budgeting or a worker only if measured latency warrants
      it. The shared deferred scan is still main-thread work.

Session boundary: after this verified checkpoint, start a fresh session from
this section and the initial scope. Next recommended slice: point-construction
readiness. Do not reread the accumulated historical slices.


## Continuation — shared construction readiness (2026-09-19)

Operator-directed deduplication/compilation continuation from `98b9c7554` on
`codex/mesh-field-diagnostics-locality`; root is the sole writer. Claude Fable 5.1
reviewed a bounded plan packet and the fixed implementation packet under
the standing authorization. This completes slice 3 of the current closure estimate;
UI-037 remains open, with nine planned closure slices remaining. No new algorithm,
config schema, dependency edge or public named-module surface was introduced.

Reuse decision: construction's position/normal/deletion loop duplicates
`CapturePointInput`, `PreparePointInput` and `CapturePointNormalInput` in the
compiled `PointProperties.cpp` owner. `ConstructionWork` now reuses the existing
capture record. Graph construction and Hoppe with estimated normals request only
positions; supplied-normal Hoppe reuses independent position/normal cache entries.
Execution always recaptures data and retains the source/transform watches,
compact source-row ordering, backend queries, publication and history guards.

The concrete contract mismatch is Hoppe's stricter supplied-normal admission:
float squared lengths must be finite and above `1e-16`. The common scanner now
caches minimum/maximum squared norms of live finite vectors and carries the normal
range through the existing oriented capture. Family thresholds stay in construction;
bilateral and descriptor zero-normal semantics remain unchanged. Infinity/zero
initial aggregates preserve empty/overflow distinctions; live-count checks still
reject empty inputs. All cache write/read and normal-transfer sites carry the new
summaries, with warm-path threshold and overflow tests.

Config, source/property metadata and finite nonsingular world transforms precede
input requests. Missing input verdicts disable construction until command drain.
Settled family gates retain coordinate bounds, Vulkan subnormal rejection, normal
length, live-count/neighborhood budgets and backend availability. Nonfinite and
malformed-mask diagnostics now come from the shared capture. Failures are ordered
by property then family prerequisite rather than whichever bad row was visited
first. The mixed-normal/position regression pins position priority.

Across all three production files, physical lines change from 1,554 to 1,548:
construction 887→866, point-field declarations 132→135, compiled capture 535→547.
The additional summaries and explicit GLM includes account for the shared-owner
increase; the duplicated scan and capture storage are removed. Two broad GLM
includes were narrowed. No measured compile-time or runtime speedup is claimed.
Named-module type identity and build edges remain unchanged.

Tests add eight `EditorPointReadiness.Construction*` cases and extend paired-halfedge
readiness coverage; two construction tests verify interior-deletion output ordering
for all eight canonical domains and both methods on both CPU backends, plus the
live-count/neighborhood budgets. Supplied-normal Hoppe output is compared with an
independently compacted property source carrying nonuniform normals; deleted
positions and normals contain NaNs. Fresh execution rejects tiny/overflowing normals
as well as exact zero after a cached success. Coverage includes cold/warm cross-family reuse, no normal
request for graph/estimated-normal paths, zero/tiny/overflowing normals, normal-only
supersession, deletion invalidation, metadata/transform priority, pending apply,
fresh rejection after cached success, and submission after cached rejection is fixed.
Existing every-domain output/history and queued stale-transform/input tests remain.

Verification checkpoint: canonical ci configured with Clang 23, unsanitized;
focused build and 207/207 focused tests pass, including 34 compilation-boundary
guards. The full `IntrinsicTests` build passes; the exclusion-only CPU gate selects
4,821 tests: 4,820 pass, one expected ASan-only GLFW lifecycle skip, zero failures
(156.04 seconds). Production source stayed fixed; test-only coverage additions from Claude's review
were rebuilt and the final 207/207 focused tests pass (21.94 seconds). Strict layering
(zero exceptions), test layout, root hygiene and source documentation pass;
source documentation has zero errors and eight existing advisory hints.
Module inventory regenerates unchanged at 429.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^EditorPointReadiness\.|^PointConstruction|^BilateralFilter|^DescriptorAnalysis|^PointSpacingOperations\.|^KernelDensity|^DensityWeight|^SandboxProcessingPanels\.|^SandboxEditorPresentation\.|^ProcessingCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Current local evidence: `/tmp/intrinsic-construction-readiness/` contains
`configure.log`, `build-focused.log`, `focused.log`, `build-all.log`, `cpu.log`,
`touched-plan.json`, `plan.json`, `review.diff` and `review.json`. Test-only review
follow-up uses `build-review-tests.log`, `focused-review-tests.log`,
`review-final.diff` and `review-followup.json`.
Claude Fable 5.1 found no production correctness defect. Its requested
interior-deletion Hoppe test and optional execution-threshold checks were added;
the follow-up review found no behavioral blocker. The subsequent compiler check
caught a test-only enum spelling (`GraphVertex` → canonical `GraphNode`), which
was fixed before the final focused run. Review cautions about an unused `GpuPoint`
and diagnostic dependents were checked against the actual generated-query caller
and repository searches; no production change was needed. Existing publication
requires a nonempty surface, and current source fixtures intentionally lack
StableId/Transform components until their tests add them.

Scope/layering/tests/docs sweep and architecture review retain the existing
runtime owner, named-module surfaces and config/app control paths. Workshop rows
1–3/8 pass; rows 4–7 are inapplicable. Strict task policy/state links, docs sync,
doc links, skill mirrors and session-brief freshness pass.

Final fixed source/test/docs review diff SHA-256:
`28babc37e21416b080d2743e06e322e18cc4aa0e32ab1eb0b6d0f597073e8eb9`.
No GPU or sanitizer execution is claimed for this checkpoint.

### Current open points

- [ ] Normal topology readiness: remove deletion-mask copies/count scans; decide
      all-deleted-face availability against apply. Add interior-deletion/nonuniform
      normal coverage if changing the mapping.
- [ ] ICP: cache paired-source and point-to-plane normal readiness; preserve
      distinct compatible sources, finite/count-matched normals and stale guards.
- [ ] Mesh/curvature/UV: close admission gaps against existing metadata validators.
      Parameterization needs runtime-owned strategy/pin/boundary prerequisites;
      texture bake needs request-specific property/UV/device/range readiness.
- [ ] Complete action/backend/variant readiness for service actions (K-Means,
      Progressive Poisson, consolidation, outliers), use the common prepared-frame
      representation everywhere, and remove remaining duplicate app prerequisites
      and hidden actions while retaining co-equal config/UI/agent validation.
- [ ] Finish the named table-driven
      `SandboxEditorUi.ActionReadinessDerivesDomainPrerequisiteReasons` matrix,
      full-family invalidation/lifecycle/supersession and zero-scan coverage, plus
      per-action/option enabled-command and disabled-tooltip/no-command ImGui tests.
- [ ] Run final task-wide operational closure only after the full action inventory,
      stale apply/completion guards and app-linked checks pass; this checkpoint
      does not retire UI-037.
- [ ] Optional scalar-catalog coverage: explicitly assert both scalar catalogs
      are empty without a command queue, and cover mixed live counts across
      multiple candidate domains. Prior review suggestions, not blockers.
- [ ] Optional point/normal follow-up: avoid the temporary second slot-vector
      allocation during fresh execution if profiling justifies a wider capture
      API; explicit paired-size assertions would document an already enforced
      same-domain/deletion-order invariant. Neither blocks this slice.
- [ ] Non-gating: bounded compilation-owner inspection and matched compile-time
      measurements remain separate work; preserve named-module type identity.
      Consider per-drain budgeting or a worker only if measured latency warrants
      it. The shared deferred scan is still main-thread work.

Session boundary: start a fresh session after this checkpoint is verified and
pushed. Read this section and initial scope, without reloading the historical
slices. Next recommended slice: normal topology readiness and all-deleted-face
semantics. Construction-specific readiness work is complete for this slice.


## Continuation — normal topology readiness (2026-09-20)

Operator-directed duplication/compilation cleanup from `d664c14a3`, continuing
with Claude Fable 5.1 and one writer. Slice 4 is complete; eight estimated closure
slices remain. This is ordinary runtime refactoring, without a timing claim,
new algorithm, public API, module, cache or control surface. UI-037 remains open.

Reuse decision: the existing normal capture owner already shares point admission
through `PreparePointInput`/`CapturePointInput`, topology reconstruction through
`BuildHalfedgeMeshForVertexNormalRecompute`, and revision watches through
`ObserveGeometryProperty`. Its local `ReadMask` additionally allocated/copied
face and edge masks for preview and execution, although mesh execution reads
those masks in the reconstruction owner. Replace that helper with metadata-only
`CaptureDeletionMetadata`, used by both preview and execution. Keep absent-mask
watches, bool/cardinality checks, halfedge pairing and diagnostic priority.
Graph execution copies the edge mask directly into its owned property set after
the preview return; absent masks retain count-matched all-false storage.

Delete the readiness-only face count: preview returns only enabled/reason,
and command-time reconstruction supplies the actual surviving-face count.
With live finite input vertices and no surviving faces, face normals remain a
successful `NoChange` without publication/history; weighted vertex normals
retain normalized fallback output and preserve deleted output slots. This covers
all-deleted face masks, all-deleted edge masks, and zero face slots. Invalid live
face-ring content remains command-time validation; this slice does not promise
full connectivity admissibility from metadata alone.

The sole production implementation changes from 660 to 655 physical lines,
including explicit GLM geometric/vec3 includes in place of the umbrella header.
Its direct `Geometry.Graph` import now names the existing `Geometry.Graph.Fwd`
owner of `HalfedgeConnectivity`. `Geometry.Graph.Vertex.Normals` still imports
`Geometry.Graph` transitively: this is narrower direct dependency spelling,
not an eliminated transitive closure or a measured compile-time improvement.

Tests add six cases: metadata preview/apply agreement for missing, valid,
wrong-type and wrong-length face/edge masks; empty-topology output/revision/history
semantics; graph edge-mask consumption on mesh and graph sources; queued empty-face
no-op; mask creation/mutation/removal/type replacement during queued work for all
three topology normal methods (including resized masks); and prepared-frame metadata refresh/cardinality
rejection without additional shared point scans. Initial versions of the first two tests passed
against the original implementation before refactoring. Existing deleted-row,
nonuniform publication, history and cancellation coverage remains in place.
No output mapping changes were made.

Claude Fable 5.1's bounded plan and fixed-diff reviews found no production
correctness blocker. Its test suggestions were incorporated: partial edge masks
with nonuniform normal/fallback rows, post-submit mask resizing, halfedge
cardinality preview/apply agreement, and an explicit optional include. The
retained mutable-mask handle is intentional: `PropertyBuffer::operator[]` and
`Vector()` mark storage modified on access, confirmed in the canonical owner and
by the stale-result tests. No reacquisition workaround is required.

The pre-merge sweep keeps one runtime intent, existing layer ownership, unchanged
config/UI/agent paths and immutable worker snapshots. No named-module definition
moves. Workshop rows 1–3/8 pass; 4–7 are inapplicable. The shared-property scan
counter covers point verdict reuse; mask-copy removal itself is established by
the bounded source diff, not an added allocation/timing instrument.

### Verification

Canonical `ci` configured with Clang 23, unsanitized. `IntrinsicTests` built
successfully. The focused checkpoint passed 163/163 tests, including all 34
`ProcessingCompilationLocality` guards. After the test-only review additions,
`IntrinsicTests` was rebuilt and the full exclusion-only CPU gate selected 4,827:
4,826 passed, one expected ASan-only GLFW lifecycle skip, zero failures (158.15 s).
This final run includes all focused cases and compilation guards. No GPU or
sanitizer execution is claimed.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j4
ctest --test-dir build/ci --output-on-failure -R '^EditorPointReadiness\.|^NormalEstimation\.|^NormalEstimationConfig\.|^SandboxProcessingPanels\.|^SandboxEditorPresentation\.|^ProcessingCompilationLocality\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Iteration fixes: corrected a test-only const property handle passed to `Remove`.
An intermediate locality run rejected source metadata after test edits overtook
its build; rebuilding the fixed source resolved it. Neither was a pre-existing
repository defect and no guard was weakened.

Strict layering (zero exceptions), test layout, root hygiene, task policy/state
links, docs synchronization, doc links, skill mirrors and session-brief freshness
pass. The module inventory regenerates unchanged at 429 modules. Touched source
documentation has zero errors; 136 pre-existing runtime README advisories are
outside the changed paragraph. Final source/test/docs self-review is clean.

Evidence directory: `/tmp/intrinsic-normal-topology/` contains `plan.json`,
`review.json`, fixed `review.diff`, baseline/focused/full-build logs,
`focused-final.log`, `build-review-tests.log`, `cpu.log` and structural-check logs.
The final complete patch is saved as `final.diff`; the reviewed production source
was unchanged by the test-only follow-up.

### Current open points

- [ ] Slice 5 — ICP paired-source and point-to-plane normal readiness: preserve
      distinct compatible sources, finite/count-matched normals and stale guards.
- [ ] Slice 6 — Mesh/curvature/UV admission gaps against the existing metadata
      validators. Inventory command-only connectivity checks and keep preview
      limits explicit; do not introduce synchronous topology scans in drawing.
- [ ] Slice 7 — Runtime-owned parameterization strategy, pin and boundary
      prerequisites, shared with config/apply.
- [ ] Slice 8 — Texture-bake request-specific property/UV/device/range readiness
      and shared presentation.
- [ ] Slice 9 — Service action/backend/variant readiness for K-Means, Progressive
      Poisson, consolidation and outliers, preserving config/UI/agent validation.
- [ ] Slice 10 — Common prepared-frame readiness everywhere; remove remaining
      duplicate app prerequisites and hidden actions.
- [ ] Slice 11 — Complete the named table-driven
      `SandboxEditorUi.ActionReadinessDerivesDomainPrerequisiteReasons` matrix,
      full-family invalidation/lifecycle/supersession and zero-scan coverage.
- [ ] Slice 12 — Per-action/option enabled-command and disabled-tooltip/no-command
      ImGui coverage, final stale apply/completion guards, full task-wide checks,
      review and operational closure. Do not retire UI-037 before these pass.
- [ ] Optional scalar-catalog tests: both catalogs empty without a command queue;
      mixed live counts across multiple candidate domains.
- [ ] Optional point/normal follow-up: avoid the second temporary slot-vector
      allocation if profiling justifies a wider capture API; explicit paired-size
      assertions may document the enforced mapping invariant.
- [ ] Non-gating compilation work: inspect bounded owners and obtain matched
      compile-time measurements under a build task; preserve named-module type
      identity and do not reopen completed BUILD-009/RUNTIME-266/267/268 slices.
- [ ] Non-gating scheduling work: per-drain budgeting/worker only if measured
      latency warrants it; shared deferred point scanning remains main-thread work.

Session boundary: after this verified checkpoint is committed and pushed, start
a fresh session to save context and credits. Read this section and the initial
scope only. Next recommended slice: ICP paired-source readiness (slice 5).
