---
id: RUNTIME-215
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T07:09:07Z"
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this task changes only physical source paths while preserving every module name, dependency, public contract, data domain, publication rule, and control surface."
---
# RUNTIME-215 — Organize runtime sources by cohesive ownership

## Goal

- Replace the crowded flat `src/runtime` root with broad, cohesive ownership
  directories while preserving every C++23 module name, implementation body,
  dependency edge, public contract, and runtime behavior.

## Non-goals

- No module merge, split, rename, export change, implementation refactor, or
  file-count reduction in this task.
- No change to runtime lifecycle, frame order, configuration, editor behavior,
  method behavior, rendering, GPU/Vulkan execution, or ownership boundaries.
- No new per-file directory, CMake subdirectory hierarchy, umbrella module, or
  compatibility forwarding path.
- No implementation of `RUNTIME-216`; that separate task owns the first
  semantic right-sizing deletion after this move is independently reviewable.

## Context

- Owner/layer: `runtime`, the engine composition root.
- The 2026-08-05 census found 170 files under `src/runtime`, including 131
  direct root files and 77,069 root-level C++ lines. The layer already uses
  cohesive `Cameras/`, `Editor/`, `Gizmos/`, `ImGui/`, `Scene/`,
  `Visualization/`, and `Modules/Clustering/` directories, but most sources
  still predate that layout.
- The same census found 56 of 74 named runtime modules in the ordinary
  `.cppm` interface plus `.cpp` implementation shape required by repository
  module hygiene. This task improves physical locality; it does not use file
  count as an architectural proxy.
- Physical directories are navigational cohorts, not a permanent module-family
  taxonomy. ADR-0026 continues to decide logical responsibility cohesion and
  ADR-0027 continues to decide whether an abstraction earns its keep.
- The exact target cohorts are `Kernel/`, `AssetWorkflow/`, `Config/`,
  `Editor/`, `Editor/Operations/`, `Editor/internal/`,
  `GeometryIntegration/`, `Rendering/`, `Scene/`, and
  `Modules/<feature>/`, alongside the already-cohesive camera, gizmo, ImGui,
  and visualization directories. Integration cohorts deliberately avoid names
  identical to lower-layer roots so physical ownership stays unambiguous to
  both readers and the layering checker.

## Right-sizing decision

- **Element:** the 131-file runtime root fails source-locality discovery, while
  one-folder-per-source-pair would add path ceremony without new ownership.
- **Simpler alternative:** broad owner/lifecycle folders inside the existing
  single `ExtrinsicRuntime` target and its single `CMakeLists.txt`.
- **Blast radius:** runtime paths in CMake, include directives, source-scanning
  tests, documentation links/current-state prose, and the generated module
  inventory. Module imports and target links remain unchanged.
- **Reintroduction trigger:** root placement is reserved for the target
  `CMakeLists.txt` and layer `README.md`; a future source belongs at root only
  if no existing cohesive owner can truthfully contain it and a reviewed task
  records why.

## Required changes

- [x] Move runtime kernel/composition sources into `src/runtime/Kernel/`.
- [x] Move asset workflow sources into `src/runtime/AssetWorkflow/`.
- [x] Move boot/live configuration and shared config-codec sources into
      `src/runtime/Config/` and `src/runtime/Config/internal/`.
- [x] Co-locate editor common/workspace sources under `src/runtime/Editor/`,
      feature operation implementations under `Editor/Operations/`, and shared
      private editor detail under `Editor/internal/`.
- [x] Move geometry availability, plan-building, presentation, topology,
      refinement, and channel-binding sources into
      `src/runtime/GeometryIntegration/`.
- [x] Move extraction, render-world, recipe/artifact handoff, and Engine-private
      extraction glue into `src/runtime/Rendering/`.
- [x] Co-locate reference content, serialization, selection, and stable lookup
      with the existing scene modules under `src/runtime/Scene/`.
- [x] Co-locate physics, point-cloud consolidation, Progressive Poisson, and
      texture-bake owners beneath `src/runtime/Modules/<feature>/`; retain the
      existing clustering cohort.
- [x] Update `src/runtime/CMakeLists.txt`, private include directives, and every
      repository path reference required by the move without changing module
      declarations or import statements.
- [x] Leave only `CMakeLists.txt` and `README.md` as direct files in
      `src/runtime/`.

## Tests

- [x] Source-contract tests that read runtime files use the new canonical paths
      and retain their existing behavioral expectations.
- [x] A fresh `ci` configure and `IntrinsicTests` build succeed with Clang 20+
      module scanning after every moved interface/implementation unit is
      registered at its new path.
- [x] The complete default CPU-supported selector passes.
- [x] Strict layering, test-layout, task-policy, docs-sync, ARA, root-hygiene,
      clean-workshop, and link gates pass.
- [x] Repository-global task-state and workflow-evidence validation are clean.
      `BUG-133` repaired the unrelated lifecycle link, and RUNTIME-214 received
      its required independent acceptance before this completion report was
      sealed.

## Docs

- [x] Add the factual cohesive directory map to `src/runtime/README.md` and
      update affected architecture/current-state source paths.
- [x] Regenerate `docs/api/generated/module_inventory.md` from the moved source
      tree.
- [x] Add `RUNTIME-216` to the runtime backlog as the separately reviewed first
      semantic file-reduction task.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening or retiring tasks.

## Acceptance criteria

- [x] `find src/runtime -maxdepth 1 -type f` reports exactly
      `src/runtime/CMakeLists.txt` and `src/runtime/README.md`.
- [x] Every pre-move runtime module name still has exactly one defining
      interface and the same implementation-unit membership.
- [x] No C++ behavior body, exported declaration, import, target link, config
      schema, or runtime control path changes in the move.
- [x] Source paths and documentation agree with the new layout, and generated
      inventories are current.
- [x] Focused structural checks, a fresh meaningful build, and the default CPU
      gate pass.

## Status

- Completed on 2026-08-06 as a behavior-preserving mechanical source-layout
  task. Runtime capability maturity is unchanged.
- Implementation commit: `61fec5c0` (with the corresponding ARA integration
  record at `aec75527`).
- The final Clang 23 build succeeded, all 4,103 selected CPU tests passed (with the
  expected environment-gated GLFW/LSan skip), and the focused tooling suites
  passed 22/22 and 27/27 cases.
- Hash-bound completion receipts additionally cover strict layering, test
  layout, task policy/format/state, docs sync/links, ARA, root hygiene, exact
  runtime-root layout, preserved module declarations, and repository-global
  workflow evidence.
- The standard completion report fixes source revision `200870ac` and content
  digest `3ba4d3cf36cb4565308530024e2cb33a5f141ab22a8f0f228d0714e71daf8d49`.

## Verification

```bash
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes

- Mixing any semantic refactor, abstraction deletion, or API change into the
  path move.
- Renaming C++ modules, namespaces, public types, targets, tests, or runtime
  concepts to mirror directory names.
- Adding CMake targets/subtargets or dependency edges for physical folders.
- Folding implementation units into interfaces merely to reduce file count.
- Editing method mathematics, shaders, benchmark evidence, or runtime feature
  behavior.
