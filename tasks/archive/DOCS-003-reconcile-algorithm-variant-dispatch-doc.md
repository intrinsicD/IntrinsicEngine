---
id: DOCS-003
theme: F
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---
# DOCS-003 — Reconcile algorithm-variant-dispatch.md with reality and define the backend-seam template

## Goal
- [x] Make `docs/architecture/algorithm-variant-dispatch.md` describe the *real*
  Strategy×Backend seam (CPU-only free function + GPU-capable device overload +
  honest `ActualBackend` diagnostic) as the canonical target template, so the
  contract can cite it and `GEOM-052` can implement against it (P4).

## Non-goals
- Implementing any backend code (that is `GEOM-052`).
- Inventing a global backend-preference map (premature with one dispatch family).
- Promoting unimplemented strategy tables as if they ship.

## Context
- The doc describes a Strategy×Backend pattern that is almost entirely
  unimplemented: only `src/geometry/Geometry.KMeans.cppm` carries a `Backend`
  axis, and it is a phantom `{CPU, CUDA}` whose `.cpp` always sets
  `ActualBackend = Backend::CPU`. The doc therefore drifts from live code.
- The doc is classified legacy-background in `docs/architecture/index.md`, so the
  contract cannot cite it as canonical until it matches reality.
- The doc's GPU overload signature uses `RHI::VulkanDevice&`, which violates the
  current layering rule "no `Vk*` types through RHI/renderer APIs" — it should be
  `RHI::IDevice&`.
- Backend enum tokens must match `docs/methods/backend-policy.md` (`GPU` /
  `gpu_vulkan_compute`, never `CUDA` for the Vulkan path).
- Status: retired 2026-06-28. The dispatch doc is now explicitly labelled as a
  target template pending `GEOM-052`, uses `Extrinsic::RHI::IDevice&` for the
  GPU-capable overload boundary, aligns `Backend::CPU`/`Backend::GPU` with the
  method backend-policy tokens, and documents the config/agent backend override
  lane plus honest requested-vs-actual fallback telemetry.
- Commit: this commit (`Reconcile algorithm variant dispatch docs`).

## Required changes
- [x] Rewrite the worked example to match a real shared seam: CPU-only free
      function (no RHI dependency) + GPU-capable `RHI::IDevice&` overload with
      automatic CPU fallback + a `Result.ActualBackend` diagnostic.
- [x] Replace the `RHI::VulkanDevice&` reference with `RHI::IDevice&` and align
      the `Backend` enum naming with `docs/methods/backend-policy.md` tokens.
- [x] Add a short config/agent-lane backend-selection note: a dispatch family's
      `Backend` field is the supported override read from config (per `CORE-003`),
      not a hardcoded constant.
- [x] Either label the doc explicitly as the TARGET template, or (once `GEOM-052`
      makes it true) reclassify it canonical in `docs/architecture/index.md`.

## Tests
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.
- [x] Skill mirrors regenerate clean (`tools/agents/sync_skills.py --write` is a
      no-op against committed mirrors) if this doc is mirrored.

## Docs
- [x] This task is docs-only; it edits `docs/architecture/algorithm-variant-dispatch.md`
      (and `index.md` classification if reclassified).

## Acceptance criteria
- [x] The doc no longer contradicts live `Geometry.KMeans` code.
- [x] Enum tokens match `backend-policy.md`; the GPU overload uses `RHI::IDevice&`.
- [x] The doc is either canonical or explicitly labelled target-template; links
      and skill mirrors are green.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --quiet -- tools/agents/skills docs/agent || echo "mirror/doc drift to review"
```

## Forbidden changes
- Implementing backend code or fabricating a GPU path.
- Adding a global backend-preference registry.
- Leaving `RHI::VulkanDevice&` or `CUDA` tokens in the canonical example.

## Maturity
- Docs-only reconcile. No engine maturity level applies; the implementation gate
  is owned by `GEOM-052`.
