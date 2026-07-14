---
id: RUNTIME-127
theme: B
depends_on: [GRAPHICS-099, GRAPHICS-101]
maturity_target: CPUContracted
---
# RUNTIME-127 — Render artifact publication and apply semantics

## Goal
- Add runtime-owned render artifact tracking and explicit publish/apply
  semantics for renderer outputs, preserving the rule that renderers do not
  mutate project data implicitly.

## Non-goals
- No renderer contract definitions (owned by `GRAPHICS-099`).
- No Vulkan output production changes.
- No UI editing surface (owned by `UI-023`).
- No broad project persistence overhaul beyond artifact metadata and explicit
  publish/apply hooks.

## Context
- Owning subsystem/layer: `src/runtime`. Runtime owns composition, job
  publication, undo/audit, source-of-truth project data, and UI-facing status.
- Accepted design: declared render outputs become runtime-owned artifacts first.
  Project data changes require explicit publish/apply or declared recipe target
  semantics with provenance and clear status.

## Required changes
- [x] Add a runtime render-artifact registry keyed by renderer, snapshot,
      view/output recipe, source revisions, artifact purpose, status,
      diagnostics, and lifetime.
- [x] Add artifact lifecycle states for transient, cached, saved-to-file,
      preview-only, dataset/batch output, readback/metric artifact, and
      candidate project result.
- [x] Add explicit publish/apply command surfaces for candidate outputs, with
      provenance and undo/audit integration.
- [x] Add diagnostics for unpublished, stale, canceled, failed, and superseded
      artifacts.

## Tests
- [x] Add runtime contract tests for artifact registration, lifecycle updates,
      stale/superseded handling, explicit publish/apply, undo/audit metadata,
      and no implicit project mutation.
- [x] Cover canceled and failed artifact publication paths.

## Docs
- [x] Document render artifact lifecycle and publish/apply semantics in runtime
      docs.
- [x] Add this task to `tasks/backlog/runtime/README.md`.

## Acceptance criteria
- [x] Renderer outputs are observable as runtime artifacts before any project
      mutation occurs.
- [x] Publish/apply is explicit, provenance-carrying, undo/audit-aware, and
      test-covered.
- [x] UI-facing status can distinguish unpublished, stale, canceled, failed,
      superseded, and published artifacts.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'RenderArtifact|ArtifactPublication|EditorCommandHistory' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Letting graphics or renderers mutate runtime project data directly.
- Publishing render outputs without provenance or status.

## Maturity
- Target: `CPUContracted`. Runtime artifact publication is CPU/headless tested.
- `Operational` owned by `GRAPHICS-103` for image-producing backend proof.

## Completion
- Retired on 2026-06-24 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Added `Extrinsic.Runtime.RenderArtifactPublication`, a CPU-only
  runtime registry and command surface for renderer-produced artifacts. The
  registry records renderer/snapshot/view recipe/source revision/output purpose
  identity, lifecycle kind, UI status, diagnostics, provenance, undo metadata,
  and audit entries. Publish/apply are explicit and provenance-gated; applying
  a candidate authorizes caller-owned project mutation but the registry performs
  no ECS, UI, renderer, RHI, or persistence mutation itself.
- Evidence:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests -- -j16`
  - `ctest --test-dir build/ci --output-on-failure -R 'RenderArtifact|ArtifactPublication|EditorCommandHistory' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `python3 tools/repo/check_layering.py --root src --strict`
  - `python3 tools/agents/check_task_policy.py --root . --strict`
