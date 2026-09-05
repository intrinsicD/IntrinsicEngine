---
id: UI-037
theme: F
depends_on: [BUG-093, BUG-096, RUNTIME-202]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
maturity_target: Operational
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence]
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
- Owner/layers: runtime feature owners own selection/domain/config/capability
  validation and expose copied readiness with their operation snapshots;
  `src/app/Sandbox/Editor/` aggregates those values into its view model and
  owns only ImGui presentation. The dependency remains `app -> runtime`.
- Today the domain panels mix boolean availability, early returns, inline `TextDisabled`, and buttons that remain enabled until their command fails. That makes the next step in the linear workflow difficult to discover and risks app validation drifting from the command contract.
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
- **Slice B — App presentation.** Add the private disabled-reason item helper,
  reusing the exact app-internal free-function/hover-flag convention from
  `BUG-093`; keep controls visible in linear order, remove duplicated app
  validation, and pin command/no-command behavior without changing algorithms.
- **Slice C — Inventory and operational proof.** Cover every named workflow and
  backend/variant option, add the two-frame real ImGui hover integration, and
  cite that run before claiming `Operational`.

## Required changes
- [ ] Export one right-sized runtime value record,
      `ActionReadiness { Enabled, DisabledReason }`, reuse it in each typed
      feature-operation snapshot, and include a value for every listed action
      and selectable backend/variant in the app-owned domain-window model. Do
      not create a monolithic Sandbox readiness service/facade.
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
- [ ] Add an app integration test named
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
- [ ] Update `src/runtime/README.md` with the readiness record, authoritative-validation reuse, deterministic reason policy, and the distinction between preview readiness and apply-time validation.
- [ ] Update `src/app/Sandbox/README.md` with the linear disabled-control convention, `AllowWhenDisabled` hover behavior, and config/agent parity.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the exported runtime module surface changes.

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
