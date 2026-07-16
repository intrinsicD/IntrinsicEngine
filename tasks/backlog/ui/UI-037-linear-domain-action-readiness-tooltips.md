---
id: UI-037
theme: F
depends_on: [BUG-093, BUG-096, RUNTIME-138]
maturity_target: Operational
---
# UI-037 — Linear domain-action readiness and disabled-reason tooltips

## Goal
- Keep every action in the Sandbox's linear mesh, UV, bake, point-cloud, registration, and parameterization workflow visible while making its current readiness explicit: runtime supplies one authoritative `ActionReadiness { Enabled, DisabledReason }` value per action, and the app disables unavailable controls and explains the missing prerequisite on hover.

## Non-goals
- No new geometry algorithm, method backend, processing parameter, or automatic prerequisite repair.
- No app-side inspection of geometry properties, selection cardinality, device state, or method configuration to rediscover whether an action is valid.
- No replacement of command-time validation. Readiness is a side-effect-free preview for presentation and automation; every runtime command still revalidates immediately before apply.
- No redesign of Sandbox navigation, input capture, window registration, or panel layout beyond keeping the existing linear controls present and understandable.

## Context
- Owner/layers: `src/runtime/` owns selection/domain/config/capability validation and constructs the readiness model; `src/app/Sandbox/Editor/` consumes that model and owns only ImGui presentation. The dependency remains `app -> runtime`.
- Today the domain panels mix boolean availability, early returns, inline `TextDisabled`, and buttons that remain enabled until their command fails. That makes the next step in the linear workflow difficult to discover and risks app validation drifting from the command contract.
- The readiness inventory covers mesh processing actions (denoise, curvature, remesh, subdivide, simplify, and recompute normals), selected-mesh UV regeneration, texture bake, point/graph/mesh normal generation where offered, point-cloud outlier removal, K-Means, Progressive Poisson, ICP, and parameterization.
- ICP readiness must distinguish two distinct selected point clouds from point-to-plane's additional finite, count-matched target-normal requirement. `BUG-096` first makes those normals authoritative so the UI cannot advertise a variant that silently executes point-to-point.
- Parameterization readiness includes the selected editable mesh, validated strategy/config, and strategy-specific pin or boundary prerequisites. Texture-bake readiness includes an operational device, compatible source property, finite UVs, valid resolution/range, and an eligible output binding. Backend choices report their own capability readiness without changing requested-versus-actual fallback policy.
- Control surfaces remain co-equal: the runtime facade exposes the same plain
  readiness values to UI and agent/controller callers. Actions that already
  have a config lane keep config-file/UI/agent parity through their typed
  preview/apply path; this task does not invent config state for commands that
  are not currently config-backed.
- `RUNTIME-138` is a prerequisite because finite/count/property readiness must
  consume generation-keyed cached or asynchronous selected-analysis results;
  it may not reintroduce full-buffer scans in the per-frame ImGui model build.

## Slice plan
- **Slice A — Runtime readiness contract.** Add the plain readiness records,
  stable reason priority, validator reuse, generation keys, and table-driven
  model tests while consuming `RUNTIME-138` cached/async analysis results.
- **Slice B — App presentation.** Add the private disabled-reason item helper,
  reusing the exact app-internal free-function/hover-flag convention from
  `BUG-093`; keep controls visible in linear order, remove duplicated app
  validation, and pin command/no-command behavior without changing algorithms.
- **Slice C — Inventory and operational proof.** Cover every named workflow and
  backend/variant option, add the two-frame real ImGui hover integration, and
  cite that run before claiming `Operational`.

## Required changes
- [ ] Export one right-sized runtime value record, `SandboxEditorActionReadiness` (the Sandbox `ActionReadiness { Enabled, DisabledReason }` contract), and include a readiness value for every listed action and selectable backend/variant in the existing domain-window model.
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
- [ ] Consume generation-keyed cached/async finite-data and property-analysis
      results from `RUNTIME-138`; pending analysis disables the affected action
      with a reason, and steady per-frame model construction performs no
      full-buffer geometry/property scan.

## Tests
- [ ] Add a pure runtime/model test named
      `SandboxEditorUi.ActionReadinessDerivesDomainPrerequisiteReasons` that
      table-drives every listed action through ready and representative blocked
      states, asserts `Enabled` parity with the authoritative validation result,
      and asserts a stable non-empty unlock reason when disabled.
- [ ] Cover ICP separately: one selection, duplicate source/target, missing or invalid target normals, and valid point-to-plane normals. The valid case is enabled only after `BUG-096`; no case may report point-to-plane ready while selecting or executing point-to-point semantics.
- [ ] Cover parameterization strategy prerequisites, UV regeneration, texture-bake property/device requirements, and unavailable GPU/backend options without invoking ImGui.
- [ ] Add an app integration test named
      `SandboxEditorPresentation.DisabledActionReasonTooltipAppearsAfterTwoFrames`.
      Frame one establishes the disabled item's rectangle; frame two positions
      the mouse over it and asserts the tooltip window/text. The test must
      exercise `AllowWhenDisabled` and the exact runtime-provided reason rather
      than a duplicated app literal.
- [ ] Assert enabled controls emit their existing typed command and show no disabled-reason tooltip; disabled controls emit no command on click. Command-level stale-state tests continue to prove apply-time revalidation.
- [ ] Add a steady-selection regression proving readiness cache hits perform
      zero full-buffer finite/property scans, while a changed generation
      invalidates the old result and reports pending until fresh analysis
      applies.

## Docs
- [ ] Update `src/runtime/README.md` with the readiness record, authoritative-validation reuse, deterministic reason policy, and the distinction between preview readiness and apply-time validation.
- [ ] Update `src/app/Sandbox/README.md` with the linear disabled-control convention, `AllowWhenDisabled` hover behavior, and config/agent parity.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the exported runtime module surface changes.

## Acceptance criteria
- [ ] Mesh/UV/bake/normal/outlier/K-Means/Progressive-Poisson/ICP/parameterization controls remain visible in their linear workflow and cannot be invoked while their runtime readiness is disabled.
- [ ] Hovering every disabled action or unavailable option presents its runtime-supplied prerequisite reason, including in the two-frame ImGui integration test.
- [ ] Runtime is the sole owner of domain/action readiness and app code contains no duplicate geometry, config, capability, or selection validation for the affected actions.
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
- No per-action service/interface/registry hierarchy; use the plain record and existing domain-window/facade model.
- No synchronous full-buffer finite/property scan from the per-frame readiness
  or ImGui path; pending cached/async analysis remains an explicit disabled
  state.
- No unrelated algorithm, renderer, input-lifecycle, import, scene-management, or navigation changes.

## Maturity
- Target: `Operational` through the app-linked two-frame ImGui integration
  test plus runtime contracts covering every listed action. No Vulkan-specific
  follow-up is owed because readiness and tooltip presentation are
  backend-neutral; backend availability remains an input to the model.
