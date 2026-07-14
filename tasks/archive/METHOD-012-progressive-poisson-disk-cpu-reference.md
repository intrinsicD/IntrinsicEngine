---
id: METHOD-012
theme: none
depends_on: []
---
# METHOD-012 — Progressive Poisson-disk sampling: paper intake + CPU reference backend

## Goal
- Intake the paper "GPU-Accelerated Progressive Poisson Disk Sampling via Phase-Parallel Spatial Hashing" and implement a hermetic CPU **reference** backend as a method package under `methods/geometry/`, producing a progressive ordering of an accepted subset of an input point cloud such that every prefix `[0,k)` is a valid Poisson-disk sampling, with per-point splat radii and per-level offsets.

## Non-goals
- No GPU backend in this task (that is METHOD-013, gated on this reference + GRAPHICS-108).
- No optimized CPU backend yet; correctness and clarity over speed.
- No renderer/runtime/ECS/UI integration (interactive playground is RUNTIME-134).
- No new engine-layer geometry features; the method is a hermetic package consuming public geometry types only.

## Context
- Status: done.
- Owner/agent: Claude + Codex verification/retirement.
- Completed: 2026-06-28.
- Commit: this commit (`Retire verified geometry, method, and runtime tasks`).
- Maturity: `CPUContracted`.
- Verification status: CPU reference + smoke benchmark compile through the
  repository CMake wiring, manifest validation, and the default CPU gate are now
  green in the local `ci` preset build.
- Owning subsystem/layer: `methods` (hermetic package; method packages import only public method APIs + declared geometry types, no ECS/runtime/graphics/platform/app — see `AGENTS.md` §2/§6 and the `methods/_template` + `methods/physics/particle_spring_reference` exemplar).
- Reference source: the sibling repo `GPU-Accelerated-Progressive-Poisson-Disk-Sampling-via-Phase-Parallel-Spatial-Hashing` (`code/progressive_poisson.h` defines the contract: `SamplerConfig{dimension, grid_width, max_levels, hash_load_factor, radius_alpha, randomize_grid_origin, grid_origin_seed, shuffle_within_levels, shuffle_seed}` → `ProgressivePoissonResult{order, level_offsets, splat_radii, base_radius}`). The CUDA `.cu` is the optimized parallel form; the CPU reference must reproduce the same accept/order semantics serially. Honor the splat-radius semantics caveat recorded in that repo's `OPEN_DECISIONS.md` (OD1: introduction-level radii).
- Must follow the method workflow: intake → CPU reference → correctness tests → benchmark harness (this task), with optimized/GPU backends in follow-ups.

## Required changes
- [x] Scaffold `methods/geometry/progressive_poisson/` from `methods/_template/` with `method.yaml` (`id: geometry.progressive_poisson`, domain `geometry`, status `reference`, backends `[cpu_reference]`, inputs/outputs/metrics/known_limitations populated), `paper.md` (citation, core claim, formulation, inputs/outputs, degenerate cases, implementation notes incl. the splat-radius caveat), and `README.md`.
- [x] Implement the CPU reference in `include/` + `src/` as pure free functions: config/result structs mirroring `progressive_poisson.h`, a serial multi-level grid/hash accept pass (radius `r_L = base_radius / 2^L`, `radius_alpha` default `sqrt(d)/2`), per-level grid-origin randomization, optional within-level shuffle, and per-point introduction-level splat radii. Support 2D and 3D inputs; the public entry takes `std::span<const glm::vec3>` (z ignored when Dimension==2), not a container.
- [x] Report deterministic output for fixed `(points, config)` and expose diagnostics: accepted count, per-level counts, measured min-distance per level, and the actual `base_radius`.
- [x] Define degenerate handling for empty/one-point inputs, duplicate/coincident points, non-finite coordinates, `dimension` not in `{2,3}`, and `radius_alpha` outside `(0,1)` (falls back to default).

## Tests
- [x] Add `unit;geometry` correctness tests asserting the Poisson guarantee: for every prefix ending at a level boundary, measured `min_dist >= r_L` (ratio `>= 1`); and that `order`/`level_offsets`/`splat_radii` lengths and invariants hold.
- [x] Add a determinism/regression test: identical `(points, config, seeds)` reproduce identical `order` and `level_offsets`; changing `shuffle_seed` only permutes within levels and preserves the guarantee.
- [x] Add edge-case tests for empty/one-point/duplicate/non-finite inputs and invalid `dimension`/`radius_alpha`.
- [x] Add a benchmark manifest + smoke runner under `benchmarks/geometry/` (metrics `runtime_ms`, plus a quality metric such as min-distance ratio or NN-CV), distinguishing smoke from heavy, with no performance-win claims.

## Docs
- [x] Populate `methods/geometry/progressive_poisson/paper.md` and `README.md` (backend status: `cpu_reference`; known limitations incl. "subsampling, not generation" and the splat-radius semantics).
- [x] Add the method to `docs/methods/index.md`.
- [x] Validate the manifest with `tools/agents/validate_method_manifests.py --strict`.

## Acceptance criteria
- [x] A hermetic CPU reference method package exists, builds via its tests/benchmarks, and produces a progressive ordering whose every level-boundary prefix satisfies the Poisson-disk guarantee.
- [x] Output is deterministic for fixed inputs/seeds; diagnostics report per-level counts and measured spacing.
- [x] Correctness, determinism, and edge-case tests pass on the default CPU gate; a CPU smoke benchmark exists without perf claims.
- [x] `method.yaml` validates strict; the method imports no ECS/runtime/graphics/platform/app code.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Implementing a GPU or optimized backend in this reference task.
- Importing ECS/runtime/graphics/platform/app code into the method package.
- Claiming performance improvements without a baseline comparison.

## Maturity
- Target: `CPUContracted` — a correctness-first CPU reference with parity-ready contract for METHOD-013.
- No `Operational` follow-up is owed by this task; the GPU `Operational` milestone is owned by METHOD-013.
