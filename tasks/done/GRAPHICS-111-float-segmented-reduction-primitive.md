---
id: GRAPHICS-111
theme: B
depends_on: []
completed: 2026-07-04
---
# GRAPHICS-111 — Float segmented/per-key reduction primitive in ComputeParallelPrimitives

## Completion
- Completed: 2026-07-04. Commit/PR: pending current change.
- Maturity: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.
- Summary: added the shared CPU/RHI segmented float reduction contract,
  deterministic GPU shader path, parity tolerance, contract coverage, and
  opt-in Vulkan smoke coverage for per-segment sums, counts, and means.
- Clean-workshop review: automated scorecard passed. Manual rows had no
  finding: no higher-layer type is exposed in the public API, no renderer
  member/subsystem or frame-graph pass was added, no string-routed pass or
  frame-recipe dependency was introduced, and the task closes at its intended
  maturity with no follow-up owed by the scorecard.

## Goal
- Add a reusable GPU float segmented reduction (sum, and mean via sum/count) over
  a key→value stream to `Extrinsic.Graphics.ComputeParallelPrimitives`, with a
  deterministic CPU reference, so per-cluster/per-segment accumulation
  (e.g. k-means centroid `Σx / count`) is a shared primitive rather than each
  backend hand-rolling float atomics.

## Non-goals
- No change to the existing prefix-scan / stream-compaction contracts.
- No k-means backend wiring here; KMeans GPU execution still uses its portable
  per-cluster scan until a separate consumer task switches to this primitive.
  This task ships the primitive + its parity contract only.
- No arbitrary-associative-operator reduction framework; scope is float sum and
  count-normalized mean over a bounded key range (segment count `k`).

## Context
- Owning subsystem/layer: `graphics/renderer`
  (`Extrinsic.Graphics.ComputeParallelPrimitives`); imports RHI contracts only,
  no ECS/runtime/app/Vulkan-native handles (per
  `docs/architecture/compute-parallel-primitives.md`).
- Origin: `docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 3.
  The module was a good substrate for the assignment-compaction and
  count→dispatch-indirect half of a clustering iteration, but had no float
  segmented/per-key reduction — exactly the centroid accumulate-and-divide
  step.
- Implementation: this slice ships the deterministic missing-feature fallback as
  the production path. One 256-lane workgroup owns one segment and scans the
  key/value stream in fixed order, so no optional float-atomic Vulkan feature or
  shared-memory `k` budget is required. A future float-atomic fast path can be
  added behind capability-gated pipeline creation without changing the public
  record surface.
- Consumes the same caller-provided-scratch + owned-lease-fallback pattern the
  existing primitives already use, so callers reuse buffers across dispatches.

## Required changes
- [x] Add a segmented-reduction record API (planning + RHI recording) that takes
      a per-element segment key and float value stream and produces per-segment
      sums and counts, plus a count-normalized mean, into caller-owned buffers.
- [x] Add the shader asset(s) (BDA/scalar-push convention, `local_size_x=256`,
      deterministic one-workgroup-per-segment fallback for large `k` or missing
      optional float-atomic support).
- [x] Add a deterministic CPU reference (`...ReduceBySegmentCpu`) mirroring the
      existing CPU reference helpers, and a declared parity tolerance for the
      float-atomic GPU path.
- [x] Reuse the caller-provided-scratch + owned-`BufferLease`-fallback pattern;
      no per-call allocation churn when scratch is supplied.

## Tests
- [x] Default CPU-gate contract test: CPU reference sums/means match a hand
      computed fixture; empty segments report count 0 and a defined mean.
- [x] Default CPU-gate contract test: fail-closed GPU record status on
      non-operational device / invalid handles (mirroring the existing
      primitives' fail-closed contract).
- [x] Opt-in `gpu;vulkan` smoke: GPU segmented sum/mean matches the CPU reference
      within the declared tolerance; deterministic mode is bit-stable across runs.
- [x] Default CPU gate stays green.

## Docs
- [x] Extend `docs/architecture/compute-parallel-primitives.md` with the
      segmented-reduction contract, scratch layout, and parity tolerance.
- [x] Cross-link `docs/migration/kmeans-gpu-vulkan-compute-proposal.md` §5 and the
      audit Finding 3.
- [x] Regenerate `docs/api/generated/module_inventory.md` for the new surface.

## Acceptance criteria
- [x] A backend can compute per-segment float sums and count-normalized means via
      one shared primitive with reused scratch buffers.
- [x] GPU path matches the CPU reference within a declared tolerance; a
      deterministic mode is available and bit-stable.
- [x] Large-`k` / missing-feature fallback path is covered when shared-memory
      privatization or optional float atomics do not fit.
- [x] No RHI/Vulkan-native leakage; layering holds; default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
tools/ci/run_clean_workshop_review.sh . --strict
git diff --check
# Vulkan-capable host:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitivesGpuSmoke' -L 'gpu' --timeout 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Leaking `Vk*`/Vulkan-native handles through the graphics API.
- Claiming parity without a declared tolerance and a CPU reference.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (opt-in `gpu;vulkan` parity
  smoke); `CPUContracted` everywhere else via the CPU reference + fail-closed
  contract tests.
