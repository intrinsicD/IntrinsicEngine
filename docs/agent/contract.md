# Agent Contract (Expanded)

This document expands the concise contract in `/AGENTS.md`. If this file and `/AGENTS.md` ever disagree, `/AGENTS.md` is authoritative.

## Mission

Deliver a modular, high-performance, scientifically rigorous engine for graphics and geometry processing while preserving:

- buildability,
- testability,
- layer ownership,
- documentation synchronization,
- and reviewability.

## Architecture invariants

Required dependency boundaries:

- `core` -> nothing
- `geometry` -> `core`
- `assets` -> `core`
- `ecs` -> `core`; geometry handles/types only when explicitly required
- `graphics/rhi` -> `core`
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views; no live ECS knowledge
- `runtime` -> lower layers; owns composition/wiring
- `app` -> `runtime` only
- `methods` -> public method API + declared backend integration only
- `benchmarks` -> public method APIs only
- `tests` -> explicit test seams only

## Coding and change-scope rules

- Use C++23.
- Preserve module names during mechanical moves.
- Do not mix mechanical moves and semantic refactors in one task.
- Avoid introducing new engine features during reorganization.
- Keep patches scoped to one task unless explicitly batched.
- Keep `.cppm` module interfaces focused on exported types, declarations, small
  inline accessors, and templates that must be visible to importers. Put
  non-trivial implementations in matching `.cpp` module implementation units and
  add them as private target sources. Treat an implementation as non-trivial when
  it owns algorithm/control-flow bodies, allocation-heavy work, topology/container
  traversal, backend calls, diagnostics assembly, file/IO handling, or imports
  other modules only needed by the implementation rather than the public API.

## Method implementation protocol

1. Intake paper and define method contract.
2. Implement CPU reference backend.
3. Add correctness tests.
4. Add benchmark harness/manifests.
5. Add optimized CPU backend.
6. Add GPU backend after reference parity.
7. Document numerical limitations and diagnostics.

## Testing and verification protocol

- Run strongest relevant verification subset for touched scope.
- Add/update tests for behavior changes.
- Keep pass rate stable or improved unless temporary shim is explicitly documented.
- Use explicit test categories: `unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`.
- Verification hygiene:
  - Prefer configured presets over ad-hoc build directories. If a non-default build tree is needed, first confirm it uses a compiler/toolchain that satisfies the repository C++23 requirements; stale trees using older toolchains are not valid evidence.
  - When a task needs a non-headless backend sanity check, prefer the smallest direct target that proves the touched seam. For Vulkan renderer integration, use focused CPU contract tests plus a direct `ExtrinsicBackendsVulkan` build before attempting broad runtime-test executables.
  - Treat `Testing/Temporary/LastTestsFailed.log` as historical state only. A failure is current only when reproduced by the CTest command just run.
  - For noisy or long builds, preserve the full log with `tee` and display only the tail, for example `2>&1 | tee /tmp/intrinsic-build.log | tail -n 120`. Use `set -o pipefail` so failures are not hidden by filtering.
  - Do not use long-running broad targets as the first verification step. Run focused build/test targets first, then broaden only when the focused gate passes and the task requires it.
  - For local iteration on changed paths, `python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir <configured-build> --print` can plan conservative affected build targets, CTest labels, and structural checks. Use `--run` only when the selected build tree is current and toolchain-compatible. This helper is not a substitute for the full PR/merge gate.
- The default CPU-supported correctness gate is:

  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

- Codex verification must configure the `ci` preset, build a real target such as `IntrinsicTests`, and run CTest. It must not use build-only or `--target help` verification as a substitute for tests.

## Documentation sync protocol

When structure, policy, or behavior changes:

- update relevant docs and task records in the same PR,
- update links for moved files,
- regenerate inventories/manifests when required,
- keep docs factual and current-state.

## CI expectations

- PR checks remain green for touched areas.
- Structural checks can start in warning mode and later tighten.
- Workflows remain split by purpose and readable.

## Temporary migration exceptions

Exceptions are allowed only if:

- recorded in a current task under `tasks/active/`,
- linked to a removal task ID,
- time-bounded,
- and isolated so they do not create new promoted-layer violations.

## Weekly agent-output review cadence

The per-PR `docs/agent/review-checklist.md` catches single-slice
defects. A weekly human-led sweep — driven by
[`REVIEW-001`](../../tasks/done/REVIEW-001-human-led-agent-week-review-cadence.md)
and run from [`docs/agent/agent-output-review-checklist.md`](agent-output-review-checklist.md)
— audits roughly one week of agent-authored commits for patterns the
per-PR view misses (multi-PR scope drift, decorative comments,
documented-but-not-tested claims, ceremony-without-shipped-value). The
cadence is *additive*: it does not gate PR merges, does not impose
per-commit reviewer load, and either silently passes or files specific
follow-up tasks. Reviewer ownership rotates; see
[`docs/agent/roles.md`](roles.md).
