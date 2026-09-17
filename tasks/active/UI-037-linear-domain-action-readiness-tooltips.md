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
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, runtime.editor-prepared-frame-locality, runtime.processing-compilation-locality, runtime.spatial-query-locality]
---
# UI-037 — Linear domain-action readiness and disabled-reason tooltips

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
