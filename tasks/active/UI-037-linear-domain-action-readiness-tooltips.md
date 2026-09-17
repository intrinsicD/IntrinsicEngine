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
contracts: [geometry.element-domain-sources, geometry.property-coherence, runtime.editor-prepared-frame-locality]
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
