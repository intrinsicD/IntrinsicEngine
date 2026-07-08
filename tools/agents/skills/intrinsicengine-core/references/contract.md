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
- `physics` -> `core`, `geometry`; no live ECS/runtime/graphics/platform/app ownership
- `graphics/rhi` -> `core`
- `graphics/assets` -> `core`, asset IDs (`Asset.Registry` types only), `graphics/rhi`; no live `AssetService` traffic
- `graphics/vulkan` -> `core`, `graphics/rhi`, backend-local Vulkan dependencies (`Vulkan::Vulkan`, `volk`, `VulkanMemoryAllocator`, `glfw`); no ECS, runtime, or live asset-service knowledge, and no `Vk*` types through RHI/renderer APIs
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views; no live ECS knowledge
- `platform` -> `core`
- `runtime` -> lower layers; owns composition/wiring, including physics bridge ownership
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

## Shared optional session setup

Agents and humans may use shared setup entrypoints under `tools/setup/`:

- `tools/setup/agent_session_setup.sh` provisions the Clang 20+ module toolchain
  plus windowing/Vulkan development headers, then optionally pre-builds core
  library targets. On Debian/Ubuntu hosts it may install system packages with
  `sudo`; use it intentionally. It is a setup convenience, not a substitute for
  preset-based verification.
- `tools/setup/wait_for_agent_setup.sh` waits for the session setup marker or a
  visible complete Clang 20+ toolchain before build/test gates.
- `tools/setup/provision_knowledge_graph.sh` optionally installs graphify's MCP
  extra and rebuilds the `.mcp.json` graph artifact at
  `build/knowledge-graph/graphify-out/graph.json`.

Agent-specific hooks should be thin adapters over these scripts. Knowledge
Graph provisioning is an optional, non-authoritative discovery aid and must not
block normal build/test work.

### Knowledge-graph discovery aid (optional)

When the `knowledge-graph` MCP server from `.mcp.json` is available, agents *may*
query the merged code + paper/method graph (`query_graph`, `get_neighbors`,
`shortest_path`, `god_nodes`, `graph_stats`) to navigate faster. It is a
discovery aid only: it never gates anything, and any finding must be confirmed
against the real authority before acting on it. Its dependency edges are derived
**only from C++23 module `import` statements** — the adapters do not parse
`#include`, so header include dependencies are absent from the graph; for units
that still use C/C++ headers, fall back to source search and to
`check_layering.py` (which covers both `import` and `#include` edges). Fitting
use cases:

- **Navigation before edits** — `get_neighbors` on a module to see which modules
  it imports and who imports it before touching a `.cppm` interface.
- **Impact / blast-radius analysis (module imports)** — `shortest_path` and
  reverse-dependency walks to find downstream consumers reachable through module
  `import` edges (useful for review and docs-sync scoping); pair with source
  search for `#include` dependencies, which the graph does not capture.
- **Layering review aid** — edges are pre-tagged `same-layer`/`allowed`/
  `violation`, so a suspected boundary problem can be spotted quickly, then
  **confirmed with `check_layering.py`**, which remains the sole layering gate.
- **Architecture hot-spots** — `god_nodes`/`graph_stats` to surface
  over-connected modules worth refactoring.
- **Paper-claim ↔ code traceability** — trace which paper claim a method
  implements and which modules realize it, from `method.yaml` + `paper.md`
  headings + import edges. The authoritative record of paper claims stays the
  method contract (`method.yaml` + `docs/methods/*`), not the graph.

If the server is absent (graph not built, or `--skip-knowledge-graph`), proceed
normally — no task depends on it.

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
- Use explicit test categories: `unit`, `contract`, `integration`, `regression`, `benchmark`, `slo`. `gpu`/`vulkan`/`glfw` are capability labels and `slow`/`flaky-quarantine` are opt-in labels, not categories (see `AGENTS.md` §7 and `tests/README.md` for the full label allow-list).
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
- Structural checks run strict in `ci-docs.yml`; a new check may start in
  warning mode only while a referenced task ID owns its tightening.
- Workflows remain split by purpose and readable.

## Temporary migration exceptions

There are currently no active legacy-tree exceptions. Future temporary
exceptions are allowed only if:

- recorded in a current task under `tasks/active/`,
- linked to a removal task ID,
- time-bounded,
- and isolated so they do not create new promoted-layer violations.

## Weekly agent-output review cadence

The per-PR `docs/agent/review-checklist.md` catches single-slice
defects. A weekly human-led sweep — driven by
[`REVIEW-001`](../../../../../tasks/done/REVIEW-001-human-led-agent-week-review-cadence.md)
and run from [`docs/agent/agent-output-review-checklist.md`](../../../../../docs/agent/agent-output-review-checklist.md)
— audits roughly one week of agent-authored commits for patterns the
per-PR view misses (multi-PR scope drift, decorative comments,
documented-but-not-tested claims, ceremony-without-shipped-value). The
cadence is *additive*: it does not gate PR merges, does not impose
per-commit reviewer load, and either silently passes or files specific
follow-up tasks. Reviewer ownership rotates; see
[`docs/agent/roles.md`](../../../../../docs/agent/roles.md).

A second, *state-scoped* audit complements the window-scoped weekly sweep:
the repo-state drift audit driven by
[`REVIEW-002`](../../../../../tasks/done/REVIEW-002-recurring-drift-and-inconsistency-audit.md)
and run from [`docs/agent/drift-audit-checklist.md`](../../../../../docs/agent/drift-audit-checklist.md).
Where the weekly sweep reviews roughly one week of *commits*, the drift audit
inspects the *whole current tree* for accumulated drift between code, docs,
tasks, generated inventories, and tracked migration exceptions (inventory
drift, retired allowlist owners, stale `(planned)` markers, dead seams,
untracked TODO/shim markers, naming/cross-doc rot). It is also additive — on
demand or every 2–4 weeks, not CI-enforced — and writes a dated report to
`docs/reports/<YYYY-MM-DD>-drift-audit.md`. It rotates through the same
reviewer pool as the weekly sweep.
