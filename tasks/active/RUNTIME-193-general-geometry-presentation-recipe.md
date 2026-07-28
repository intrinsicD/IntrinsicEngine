---
id: RUNTIME-193
theme: F
depends_on: [RUNTIME-192]
maturity_target: Retired
---
# RUNTIME-193 — General geometry-presentation recipe

## Status

- Promoted to active on 2026-07-28 after `RUNTIME-192` retired the duplicate
  property vocabularies and left `GeometryPropertyRef` as the canonical stable
  property identity.
- Intake census found two public modules with 44 direct module imports across
  24 production and 20 test files. The generally used
  `ProgressivePresentationBindings` component currently combines authored
  choices with readiness, diagnostics, generated outputs, expected counts,
  and source/binding generations; scene serialization persists several of
  those runtime observations.
- Slice A is in progress: introduce the neutral authored recipe and copied,
  generation-qualified operational snapshot in one cohesive runtime module
  before migrating or deleting the proven path.

## Goal

- Replace the progressive-named but generally used render-data and extraction
  path with one data-driven `GeometryPresentationRecipe` and copied runtime
  status snapshot that works for mesh, graph, point-cloud, and procedural
  geometry without feature-specific presentation pipelines.

## Non-goals

- No merge of structural geometry channels, PBR material channels, or
  scientific visualization output semantics.
- No renderer pass-order change, new render pass, material system, or live ECS
  ownership in graphics.
- No job, texture-bake, or visualization adapter registry hidden inside the
  recipe.

## Context

- `Runtime.ProgressiveRenderData` and
  `Runtime.ProgressivePresentationExtraction` carry general property,
  material, visualization, generated-asset, and readiness data despite their
  names. Their duplicate property vocabulary is removed by `RUNTIME-192`.
- Scene serialization needs desired presentation values only. Pending/ready/
  failed state, GPU handles, jobs, and generations are runtime observations and
  must not leak into the serialized recipe.
- The general recipe is the glue between authored scene intent and the
  renderer-facing copied snapshot; graphics remains unaware of ECS/runtime.

## Right-sizing decision

- **Elements:** the two `Progressive*` modules trigger the misleading-name,
  role-fragmentation, and mixed-persistence heuristics. Their data is consumed
  by every geometry domain, while one ECS record mixes serializable intent
  with transient result state and a second public module only projects that
  record into another copy.
- **Simpler alternative:** replace both modules with one
  `Extrinsic.Runtime.GeometryPresentation` module. Keep plain recipe, runtime
  status, and copied snapshot records plus free validation/projection helpers;
  use `GeometryPropertyRef` directly and add no interface, registry, service,
  factory, or feature-specific pipeline. Scene documents write only the recipe
  and accept the legacy wire object on read for compatibility.
- **Blast radius:** 24 production and 20 test files spanning scene
  serialization, asset/model handoff, texture-bake result binding, render
  extraction, Sandbox models/commands, the promoted Vulkan acceptance smoke,
  runtime/test CMake registration, architecture docs, and the generated module
  inventory. Mechanical naming migration stays separate from the semantic
  recipe/status split in reviewable commits.
- **Reintroduction trigger:** split projection into another public module only
  if a second independently versioned target needs the projection ABI without
  the recipe vocabulary. A second geometry domain or UI remains data handled
  by the same free projection functions.

## Slice plan

- **Slice A — recipe and projection.** Define the plain desired-state recipe,
  runtime status snapshot, and pure projection using `GeometryPropertyRef`.
- **Slice B — workflow migration.** Migrate serialization, render extraction,
  editor models, material binding, texture-bake result handling, and every
  geometry domain with parity tests.
- **Slice C — retirement.** Delete the progressive-named modules, aliases,
  re-exports, and duplicate extraction branches after the new path is the sole
  production consumer path.

## Required changes

- [ ] Add one serializable `GeometryPresentationRecipe` containing stable
      property/asset identities and explicit material/visualization choices,
      without borrowed pointers, ECS entities, job handles, GPU handles, or
      operational readiness.
- [ ] Add one copied `GeometryPresentationSnapshot` for effective state,
      readiness, fallback, diagnostics, and exact generations.
- [ ] Project the recipe into renderer snapshots through pure/runtime-owned
      extraction functions and the existing render-world submission boundary.
- [ ] Migrate scene save/load, selected-entity models, generated-texture result
      processing, material source selection, and mesh/graph/point-cloud/
      procedural extraction to the new vocabulary.
- [ ] Preserve stale-generation rejection, fallback behavior, per-renderable
      material isolation, and scene round-trip compatibility.
- [ ] Delete `Runtime.ProgressiveRenderData`,
      `Runtime.ProgressivePresentationExtraction`, old component/binding names,
      compatibility aliases, and duplicate production branches only after
      parity and round-trip coverage passes.

## Tests

- [ ] Round-trip tests prove only desired authoring state is serialized and
      runtime-only readiness/handles never persist.
- [ ] Domain matrix tests prove mesh, graph, point-cloud, and procedural
      presentation projects through the same recipe path.
- [ ] Generation/fallback tests cover stale properties, generated assets,
      per-renderable material isolation, and scene reload.
- [ ] Structural tests prove no production import or old progressive
      presentation symbol remains after cleanup.

## Docs

- [ ] Update runtime, scene-serialization, and renderer extraction docs with
      the recipe/snapshot ownership split.
- [ ] Update migration references and regenerate the module inventory.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] One geometry-presentation recipe expresses desired source/material/
      visualization state for every supported geometry domain.
- [ ] Runtime status remains copied and generation-qualified; graphics consumes
      snapshots only.
- [ ] The progressive-named general modules and compatibility path are deleted
      after every workflow uses the new recipe.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Presentation|SceneSerialization|RenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- A universal enum that conflates property source, material channel, and
  visualization meaning.
- Persisting operational state or component references in scene data.
- Leaving the progressive modules as permanent forwarding wrappers.

## Maturity

- Target: `Retired`; closure requires production migration, round-trip and
  extraction parity, and removal of the old public modules.
