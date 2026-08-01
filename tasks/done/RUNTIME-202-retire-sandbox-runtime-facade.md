---
id: RUNTIME-202
theme: F
depends_on:
  - BUG-125
  - RUNTIME-191
  - RUNTIME-193
  - RUNTIME-196
  - RUNTIME-198
  - RUNTIME-199
  - RUNTIME-200
  - RUNTIME-201
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-RuntimeCleanup"
branch: "codex/runtime-202-retire-sandbox-facade"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-01T01:27:35Z"
maturity_target: Retired
---
# RUNTIME-202 — Retire the Sandbox runtime facade and localize feature models

## Status

- Completed and retired on 2026-08-01 at `Retired`.
  `BUG-125` retired the two diagnosed queued-import scheduling races. The
  corrected implementation physically owns scene, geometry, visualization,
  render-recipe, and workspace-model bodies in their feature units; the
  private session is limited to attachment epochs, retained job results, and
  prepared-frame lifecycle. The aggregate `IntrinsicTests` build and all
  4,010 default CPU tests pass, as do 2,664/2,664 ASan and 2,664/2,664 UBSan
  tests and every strict structural check. The default CPU and UBSan selectors
  contain the policy-defined GLFW/LSan self-skip; ASan executes that control.
  An initial ASan pass reproduced the already-tracked `BUG-123` terminal-event
  race once on an untouched scene-document path; the exact test then passed
  20/20 and the complete ASan selector passed on rerun. The first independent
  review rejected the replacement all-feature aggregate and misplaced private
  operation bodies; the corrected implementation is `d09d6893`. The second
  review closed every code finding and requested only factual ownership-record
  corrections. This retirement source plus the generated report and final
  append-only review record provide the exact completion binding.
- Commit: corrected implementation `d09d6893`; this retirement-state commit
  plus the generated report provide the final exact task-state binding.
- Re-gated on 2026-07-30: `RUNTIME-138` is no longer a prerequisite. Facade
  retirement must preserve the current selected-model cache and diagnostic
  behavior, migrate existing selection/property behavior to its feature
  owner, and avoid waiting for or introducing a replacement selected-analysis
  service. Any concrete expensive derivation discovered during migration
  becomes a scoped feature-owner follow-up.

## Goal

- Replace the monolithic `Runtime.SandboxEditorFacades` and
  `Runtime.SandboxConfigSections` / `SandboxDefaultPolicies` surfaces with the
  general runtime services/operations/snapshots delivered by their feature
  owners, move
  Sandbox-only view models and config registration into `app`, migrate every
  real workflow, and delete the old facade family.

## Non-goals

- No replacement `EditorService`, universal command facade, panel registry in
  runtime, or feature-erased request/result variant.
- No app ownership of ECS, assets, jobs, renderer state, validation, or
  mutations.
- No simultaneous algorithm redesign; prerequisite tasks establish the common
  property, work, presentation, visualization, import, and mutation paths.

## Context

- `Runtime.SandboxEditorFacades.cppm` exports thousands of lines of
  Sandbox-prefixed enums, records, commands, session state, and service
  attachment; its implementation is split across several large files and
  imports nearly every runtime feature.
- That facade was useful during migration from an Engine god object, but it now
  hides duplicate property/backend/result vocabularies and makes unrelated
  features depend on one BMI.
- The endpoint is not one more generic wrapper: durable modules publish their
  narrow services, stateless features expose typed operations/snapshots, and
  `src/app/Sandbox` aggregates those records into view models and registered
  config sections.

## Slice plan

- **Slice A — app context/model boundary.** Define an app-owned
  `SandboxEditorContext` over narrow runtime service/snapshot handles and move
  pure presentation records/config registration out of runtime.
- **Slice B — feature migration.** Move each existing panel/agent/test workflow
  to clustering, texture bake, scene, config, asset, presentation,
  visualization, spatial-debug, and mutation owners.
- **Slice C — thin-path cleanup.** Push registration alignment and other
  pass-through algorithms to their owning typed operation; remove duplicate
  Sandbox enums/converters/results.
- **Slice D — facade deletion.** After all workflows and real widget tests use
  the new paths, delete the facade/config modules, split implementation files,
  compatibility accessors, and obsolete tests/CMake entries in a separate
  mechanical commit.

## Required changes

- [x] Inventory every exported Sandbox facade/config type and map it to:
      feature-owned runtime contract, app-owned view/config record, or
      deletion; record no unowned compatibility bucket.
- [x] Make `src/app/Sandbox` compose panel models from copied narrow service
      snapshots and submit the corresponding typed feature operation.
- [x] Move feature config schemas/codecs beside their runtime feature owner;
      keep app-owned registration/default aggregation and one generic config
      preview/validate/apply lane.
- [x] Move Sandbox default-policy descriptor aggregation/installation into
      `src/app/Sandbox`; keep only reusable typed policy records/operations
      with their runtime feature owner.
- [x] Migrate existing selection/property analysis, texture baking,
      clustering, progressive Poisson, registration, parameterization, asset
      import, visualization/spatial debug, presentation/material edits, and
      scene commands without app-to-lower-layer imports or a replacement
      selected-analysis service.
- [x] Remove Sandbox-specific backend/domain/value/result duplicates in favor
      of each feature's typed request/result and the canonical property record.
- [x] Inline or privatize thin `RegistrationAlignment` pass-through logic in
      the typed registration operation; geometry remains the algorithm owner.
- [x] Delete `Runtime.SandboxEditorFacades`,
      `Runtime.SandboxConfigSections`,
      `Runtime.SandboxDefaultPolicies`,
      `Runtime.SandboxMethodFacade`,
      `Runtime.SandboxParameterizationFacade`,
      `Runtime.SandboxEditorRenderRecipeFacade`, their internal header,
      compatibility accessors/re-exports, and old direct tests after migration.

## Tests

- [x] A workflow matrix covers every existing Sandbox action through the
      app-owned context and narrow runtime operation, including validation,
      pending/ready/failure, mutation, undo, and stale completion.
- [x] Config-file, UI, and agent/CLI requests remain co-equal for every
      config-backed feature.
- [x] App layering tests prove panels import runtime only and runtime modules
      contain no ImGui/panel models.
- [x] Compile/source ratchets prove the deleted facade/config symbols and
      compatibility aliases are absent from all production targets.

## Docs

- [x] Update runtime and Sandbox architecture docs with the app aggregation /
      feature-owner split and a mapping from every removed facade area.
- [x] Regenerate module inventory and remove all old facade/config examples.
- [x] Refresh task indexes, session brief, and retirement records on the exact
      frozen surface used by the final independent review.

## Acceptance criteria

- [x] No production panel, agent command, config application, or runtime test
      uses the Sandbox facade family.
- [x] Runtime exposes only feature-owned operations/services/snapshots; app
      owns view composition without live lower-layer state.
- [x] The facade/config modules and all compatibility routes are deleted after
      the workflow/config/layering matrix passes.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditor|RuntimeContract|EngineConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Replacing the facade with another all-feature facade/service/variant.
- Moving validation, mutation, jobs, ECS, assets, or renderer ownership into
  app.
- Deleting the old facade before every production workflow and control surface
  has parity on its owning path.

## Maturity

- Target: `Retired`; all prerequisite general paths, workflow/control-surface
  parity, and app/runtime layering proof precede final facade deletion.
