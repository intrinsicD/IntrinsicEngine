# IntrinsicEngine Agent Contract

This file is the authoritative repository contract for all coding agents operating in this repository.

It **supersedes policy text** in `CLAUDE.md`, `.github/copilot-instructions.md`, and `.codex/config.yaml` when those files disagree.

## 1. Mission

Build and maintain IntrinsicEngine as a modular, high-performance, scientifically rigorous engine for graphics, geometry processing, and method-driven research integration.

All agent work must preserve:

- Buildability.
- Testability.
- Layer ownership.
- Documentation synchronization.
- Reviewability of mechanical vs semantic changes.

## 2. Non-negotiable architecture invariants

The following dependency boundaries are mandatory:

- `core` -> nothing.
- `geometry` -> `core`.
- `assets` -> `core`.
- `ecs` -> `core`; may use geometry handles/types only when explicitly required.
- `graphics/rhi` -> `core`.
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views; **no live ECS knowledge**.
- `runtime` -> all lower layers; owns composition/wiring.
- `app` -> `runtime` only.
- `methods` -> public method API + declared backend integration only.
- `benchmarks` -> public method APIs only.
- `tests` -> explicit test seams only.

Cross-layer convenience imports that violate this table are prohibited.

## 3. Source tree map

Target source layout:

- `src/legacy/` (temporary migration area; must shrink over time).
- `src/core/`.
- `src/assets/`.
- `src/ecs/`.
- `src/geometry/`.
- `src/graphics/rhi/`, `src/graphics/vulkan/`, `src/graphics/framegraph/`, `src/graphics/renderer/`.
- `src/runtime/`.
- `src/platform/`.
- `src/app/`.

Supporting architecture roots are mandatory parts of the system contract:

- `methods/`, `benchmarks/`, `tests/`, `docs/`, `tasks/`, `tools/`, `.github/workflows/`.

## 4. Layering rules

Agents must enforce ownership and dependency flow:

- Lower layers never import higher layers.
- Runtime wiring remains in `runtime`; lower subsystems remain reusable.
- Graphics subsystems operate on snapshots/views, not live gameplay ownership.
- `src/legacy` may contain transitional exceptions only when tracked in migration docs/tasks.

Every new dependency edge must be justifiable by layer policy and reflected in docs when architectural.

## 5. Coding rules

- Use C++23.
- Preserve existing module names during mechanical directory moves.
- Do not mix mechanical moves with semantic refactors.
- Avoid introducing new engine features during reorganization tasks.
- Keep patches small and scoped to one task when possible.
- Prefer deterministic, testable APIs with explicit ownership and failure states.

## 6. Method implementation protocol

Method/paper work must follow this order:

1. Intake paper + define method contract.
2. Implement CPU reference backend first.
3. Add correctness tests.
4. Add benchmark harness/manifests.
5. Add optimized CPU backend.
6. Add GPU backend only after reference parity exists.
7. Document numerical limitations and diagnostics.

## 7. Testing protocol

For each change:

- Run the strongest relevant subset of repository verification commands.
- Add/update tests for behavior changes.
- Preserve or improve pass rate unless a temporary shim is documented.
- Label tests by category as layout migration progresses (`unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`).
- The default CPU-supported correctness gate is:

  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

  GPU/Vulkan, slow, and explicitly quarantined tests are opt-in and must be justified by label policy.

## 8. Benchmarking protocol

- Benchmarks must use declared manifests and stable IDs.
- Distinguish smoke checks from heavy/nightly runs.
- Record metrics and diagnostics in machine-readable output.
- Do not claim performance wins without baseline comparison.

## 9. Documentation sync protocol

When code, structure, or policy changes:

- Update relevant architecture/migration/task docs in the same PR.
- Update references and links for moved files.
- Regenerate inventories when required by tooling.
- Keep docs factual (current state), not aspirational unless clearly labeled.

## 10. CI expectations

- PR checks must remain green for touched areas.
- Structural checks (tasks/docs/layering/manifests) should run in warning mode until explicitly tightened.
- Workflow definitions must stay readable and split by purpose.
- Agent/Codex verification must configure the `ci` preset, build a meaningful target such as `IntrinsicTests` (never `help` as a stand-in), and run CTest. The current Codex verification command mirrors the default CPU-supported gate from the testing protocol.

## 11. Task execution workflow

Every task execution should follow this sequence:

1. Inspect existing code and docs.
2. Identify owning subsystem and layer.
3. Write or update task file.
4. Implement the smallest useful patch.
5. Add or update tests.
6. Add or update docs.
7. Run verification.
8. Update generated inventories.
9. Self-review against PR checklist.

## 12. Review checklist

Before commit/PR, verify:

- Scope matches exactly one task unless batching is explicitly allowed.
- Layering invariants are preserved.
- Tests are updated and pass for touched scope.
- Docs and task records are synchronized.
- Temporary compatibility shims are tracked with removal follow-up.
- Mechanical moves and semantic edits are not mixed.

## 13. Temporary migration exceptions

Temporary exceptions are allowed only when all of the following are true:

- Exception is documented in `tasks/active/0000-repo-reorganization-tracker.md`.
- Exception has a specific removal task ID.
- Exception is time-bounded and reviewed.
- Exception does not create new violations in promoted final layers.

Undocumented exceptions are policy violations.

## Related expanded docs

Use the expanded procedure docs under `docs/agent/`:

- `docs/agent/contract.md`
- `docs/agent/task-format.md`
- `docs/agent/review-checklist.md`
- `docs/agent/method-workflow.md`
- `docs/agent/benchmark-workflow.md`
- `docs/agent/docs-sync-policy.md`
- `docs/agent/roles.md`
