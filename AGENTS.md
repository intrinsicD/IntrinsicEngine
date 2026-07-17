# IntrinsicEngine Agent Contract

This file is the authoritative repository contract for all coding agents operating in this repository.

It **supersedes policy text** in `CLAUDE.md`, `.github/copilot-instructions.md`, and `.codex/config.yaml` when those
files disagree.

## Agent skills

This repository ships agent skills at `tools/agents/skills/` (also surfaced
via `.claude/skills/` and `.codex/skills/`). At the start of every session,
read the skill manifests by running:

    cat tools/agents/skills/intrinsicengine-core/SKILL.md

This skill is the master entry point and routes to specialist skills for
task workflow, review, methods, benchmarks, and docs sync. Load specialist
skills on demand per the routing table inside `intrinsicengine-core`.

Skill `references/` files are generated from `docs/agent/*` by
`python3 tools/agents/sync_skills.py --write` and verified in CI
(`ci-docs.yml`); edit the canonical doc and re-run the sync, never the
mirror copy.

If you are operating in an environment without auto-discovery of agent
skills (e.g. Claude Code on the web, or any bare API client), treat the
files under `tools/agents/skills/` as required reading mirroring
`docs/agent/*` — the SKILL.md bodies summarize, and `references/` files
contain the authoritative source.

## Shared optional session setup

All agents may use the shared setup entrypoints under `tools/setup/`:

- `tools/setup/agent_session_setup.sh` provisions the Clang 20+ module toolchain
  and windowing/Vulkan development headers used by repository builds, then
  optionally pre-builds core library targets. On Debian/Ubuntu hosts this may
  install system packages with `sudo`; invoke it intentionally, and remember it
  is a convenience setup helper, not a replacement for preset-based
  verification. It always runs a vcpkg egress preflight and records
  `ready`/`reachable`/`blocked`/`unknown` to `/tmp/intrinsic-session-setup.vcpkg`;
  on a blocked host it prints an actionable diagnosis (`BUG-065`) instead of a
  later cryptic preset `403`. Pass `--bootstrap-vcpkg` (or
  `INTRINSIC_SESSION_BOOTSTRAP_VCPKG=1`) to pre-bake the vcpkg tool when the
  download host is reachable.
- `tools/setup/bootstrap_vcpkg.sh` bootstraps the repository-local vcpkg tool
  the `ci`/`dev` presets chainload. It gates on `tools/setup/vcpkg_preflight.sh`
  and fails closed with an actionable diagnosis when the environment egress
  policy blocks the tool download (`BUG-065`); set `INTRINSIC_VCPKG_FORCE=1` to
  attempt regardless.
- `tools/setup/wait_for_agent_setup.sh` blocks until the session setup marker is
  written or a complete Clang 20+ toolchain is visible. Use it before CMake
  gates if setup is running in the background.
- `tools/setup/provision_knowledge_graph.sh` installs graphify's MCP extra when
  possible and rebuilds `build/knowledge-graph/graphify-out/graph.json`, the
  artifact served by `.mcp.json`. This is optional, non-authoritative discovery
  tooling; failures must not block normal build/test work.

Agent-specific hooks should wrap these shared scripts instead of duplicating
their implementation. For example, `.claude/setup.sh` is only a Claude
`SessionStart` adapter that calls `tools/setup/agent_session_setup.sh
--async-json`.

## 1. Mission

Build and maintain IntrinsicEngine as a modular, high-performance, scientifically rigorous engine for graphics, geometry
processing, and method-driven research integration.

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
- `physics` -> `core`, `geometry`; owns simulation world/state, never live ECS/runtime/graphics/platform/app.
- `graphics/rhi` -> `core`.
- `graphics/assets` -> `core`, asset IDs (`Asset.Registry` types only), `graphics/rhi`; no live `AssetService` traffic.
- `graphics/vulkan` -> `core`, `graphics/rhi`, backend-local Vulkan dependencies (`Vulkan::Vulkan`, `volk`,
  `VulkanMemoryAllocator`, `glfw`); no ECS, runtime, or live asset-service knowledge, and no `Vk*` types through
  RHI/renderer APIs.
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views; **no live ECS knowledge**.
- `platform` -> `core`.
- `runtime` -> all lower layers; owns composition/wiring, including ECS-to-physics synchronization and physics-to-ECS writeback.
- `app` -> `runtime` only.
- `methods` -> public method API + declared backend integration only.
- `benchmarks` -> public method APIs only.
- `tests` -> explicit test seams only.

Cross-layer convenience imports that violate this table are prohibited.

## 3. Source tree map

Target source layout:

- `src/core/`.
- `src/assets/`.
- `src/ecs/`.
- `src/geometry/`.
- `src/physics/` (approved by ADR-0019; implementation lands only under scoped physics tasks).
- `src/graphics/rhi/`, `src/graphics/assets/`, `src/graphics/vulkan/`, `src/graphics/framegraph/`,
  `src/graphics/renderer/`.
- `src/runtime/`.
- `src/platform/`.
- `src/app/`.

Supporting architecture roots are mandatory parts of the system contract:

- `methods/`, `benchmarks/`, `tests/`, `docs/`, `tasks/`, `tools/`, `cmake/`, `.github/workflows/`.
- `assets/` contains checked-in shaders/models/fonts used by app, graphics, and tests; `external/vcpkg/`,
  `external/vcpkg-installed/`, `external/vcpkg-bincache/`, and `third_party/` contain dependency
  tool/cache state and are not engine layers.

## 4. Layering rules

Agents must enforce ownership and dependency flow:

- Lower layers never import higher layers.
- Runtime wiring remains in `runtime`; lower subsystems remain reusable.
- Graphics subsystems operate on snapshots/views, not live gameplay ownership.
- `physics` owns simulation world/state and may use geometry collision/math kernels, but must not import live ECS,
  runtime, graphics/RHI, platform, app, live asset services, or method packages.
- ECS physics authoring components store CPU descriptors only. Runtime owns any live sidecar that maps ECS identity to
  physics handles, fixed-step scheduling, and simulation writeback.
- `assets` is CPU-only and GPU-agnostic; GPU-side asset state lives in `src/graphics/assets/` and is wired by `runtime`
  from asset events.
- `platform` exposes window/input ports and explicit backends; it must not import `graphics`, `ecs`, or `runtime`.
- Platform backend selection is explicit: `INTRINSIC_PLATFORM_BACKEND=Auto|Null|Glfw`; use `Null`/
  `INTRINSIC_HEADLESS_NO_GLFW=ON` for headless work unless a task specifically needs GLFW/Vulkan surface coverage.
  Under `Auto`, `src/platform/CMakeLists.txt` resolves to `Glfw` only when `EXTRINSIC_PLATFORM=Linux` and
  `EXTRINSIC_BACKEND=Vulkan` (both default) and `INTRINSIC_HEADLESS_NO_GLFW=OFF`; otherwise it resolves to `Null`. The
  `ci-vulkan` preset pins `EXTRINSIC_BACKEND=Vulkan`.
- Runtime owns graphics backend selection. Promoted Vulkan is opt-in only when
  `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` and `RenderConfig::EnablePromotedVulkanDevice` are both enabled;
  otherwise Vulkan requests fall back to the Null device. Renderer/runtime code must gate on
  `RHI::IDevice::IsOperational()`, not Vulkan diagnostics.
- Temporary compatibility exceptions may exist only when tracked in migration docs/tasks and represented in the
  layering allowlist with a current removal owner. There are no active legacy-tree exceptions.

Every new dependency edge must be justifiable by layer policy and reflected in docs when architectural.

## 5. Coding rules

- Use C++23.
- Preserve existing module names during mechanical directory moves.
- Do not mix mechanical moves with semantic refactors.
- Avoid introducing new engine features during reorganization tasks.
- Keep patches small and scoped to one task when possible.
- Prefer deterministic, testable APIs with explicit ownership and failure states.
- Use out-of-source CMake presets only; `CMakeLists.txt` rejects in-source configure. Default agent build setup is
  `cmake --preset ci` followed by `cmake --build --preset ci --target IntrinsicTests`.
- Presets require Clang 20 as the minimum supported major version and auto-select the highest complete installed
  Clang toolchain (`clang`, `clang++`, and matching `clang-scan-deps`) at version 20 or newer. Do not treat GCC or
  stale non-preset build trees as valid verification for module changes.
- Add C++23 module libraries with `intrinsic_add_module_library(...)` from `cmake/IntrinsicModule.cmake` and declare
  module interfaces via `target_sources(... FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ...)`.
- Keep `.cppm` module interfaces focused on exported types, declarations, small inline accessors, and templates that
  must be visible to importers. Put non-trivial implementations in matching `.cpp` module implementation units and add
  them as private target sources. Treat an implementation as non-trivial when it owns algorithm/control-flow bodies,
  allocation-heavy work, topology/container traversal, backend calls, diagnostics assembly, file/IO handling, or imports
  other modules only needed by the implementation rather than the public API.
- Third-party C/C++ dependencies are declared in `vcpkg.json` and resolved by presets through the repository-local
  vcpkg toolchain at `external/vcpkg/scripts/buildsystems/vcpkg.cmake`, chainloaded with
  `cmake/IntrinsicClangToolchain.cmake` so Clang module scanning remains enforced. Run
  `tools/setup/bootstrap_vcpkg.sh` on fresh checkouts; use `VCPKG_BINARY_SOURCES` for local/CI binary caching.
  `cmake/Dependencies.cmake` is vcpkg-manifest-only; new dependency traffic must go through `vcpkg.json`,
  `vcpkg-configuration.json`, or repository overlay ports.
- CUDA compute support is optional and off by default (`INTRINSIC_ENABLE_CUDA=OFF` in `ci`/`dev` presets); enable it
  only for tasks that explicitly require CUDA seams, using `dev-cuda` or an equivalent configure with a valid
  `CUDAToolkit` install.
- **Research pragmatism (P1).** This is research-driving software: prefer the smallest construct that does the job.
  Plain `struct`s and free functions are the default for data-driven code (configs, params/result records, CPU/GPU
  descriptors, packed buffers). Introduce an interface, factory, wrapper, builder, or backend seam only when a *present*
  second caller, a layering boundary, a test-double surface, or a config/UI/agent-controllable variant axis requires it
  — one implementation is not a seam. Robustness means fail-closed and deterministic, not defensive ceremony.
- **Config lane is a first-class control surface (P3).** Engine-tunable behavior must be reachable through the config
  tree by config files, agents/CLI, **and** the UI as co-equal surfaces — never UI-only. New tuning state is expressed
  as serializable config that round-trips to a file and is applied through a side-effect-free preview/validate-then-apply
  path (the `RenderRecipeConfig` schema-id + version + diagnostics shape is the reference model). UI panels and agents
  drive the same validated apply path; a UI handler must not poke a subsystem through a private path the config lane
  cannot reproduce.
- **Recipe-driven frames and a readable main loop (P5).** Frame composition is data-driven: passes/resources are
  described by recipe data (`FrameRecipe*`), default recipes are derived/loaded at init, and the engine update loop
  reads as an ordered list of named phases (see `docs/architecture/frame-graph.md`). Do not hardcode pass order or
  composition behind imperative branches that the recipe data cannot express or introspect.

## 6. Method implementation protocol

Method/paper work must follow this order:

1. Intake paper + define method contract.
2. Implement CPU reference backend first.
3. Add correctness tests.
4. Add benchmark harness/manifests.
5. Add optimized CPU backend.
6. Add GPU backend only after reference parity exists.
7. Document numerical limitations and diagnostics.

Method manifests live at `methods/**/method.yaml` and are validated by
`python3 tools/agents/validate_method_manifests.py`.

## 7. Testing protocol

For each change:

- Run the strongest relevant subset of repository verification commands.
- Add/update tests for behavior changes.
- Preserve or improve pass rate unless a temporary shim is documented.
- Label tests using the documented CTest allow-list in `tests/README.md` and `tests/CMakeLists.txt` (categories:
  `unit`, `contract`, `integration`, `regression`, `benchmark`, `slo`; ownership labels:
  `assets`, `build`, `core`, `ecs`, `geometry`, `graphics`, `headless`, `physics`, `platform`, `runtime`; capabilities:
  `glfw`, `gpu`, `vulkan`; opt-in labels: `slow`, `flaky-quarantine`). New labels must update both files in the same
  change.
- New C++ test files use `Test.<Name>.cpp`; existing `Test_*.cpp` files are compatibility carryover and should only be
  renamed by explicit mechanical cleanup tasks.
- Verification hygiene:
    - Prefer configured presets and task-specific focused targets before broad or long-running targets.
    - Treat non-default build trees as valid evidence only after confirming their compiler/toolchain satisfies the
      repository C++23 requirements; stale trees with older compilers are not valid verification.
    - For local iteration on changed paths, use `python3 tools/ci/touched_scope.py --root . --base-ref origin/main
      --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --print` (or
      `--run`). The same staged planner drives `pr-fast`: it runs structural-only changes before C++ setup, configures
      the unsanitized Null/headless `ci-fast` identity for source routes, and reconciles selected producers against the
      fresh test registry before build. Missing/ambiguous diffs, module interfaces, headers, build/dependency inputs,
      and unknown paths fail closed to the bounded broad feedback route. This helper and workflow are feedback aids,
      not substitutes for the full CPU, sanitizer, or capability-specific PR/merge gates.
    - Treat `Testing/Temporary/LastTestsFailed.log` as historical state only; current pass/fail state comes from the
      CTest command just run.
    - For noisy commands, capture full output with `tee`, display a bounded tail, and use `set -o pipefail` so filtering
      does not hide failures.
- The default CPU-supported correctness gate is:

  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

  GPU/Vulkan, slow, and explicitly quarantined tests are opt-in and must be justified by label policy.
- Promoted Vulkan opt-in verification uses the `ci-vulkan` preset plus GPU/Vulkan label intersection, for example:

  ```bash
  cmake --preset ci-vulkan
  cmake --build --preset ci-vulkan --target IntrinsicTests
  ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
  ```
- `CMakePresets.json` defines configure/build presets but no CTest presets; invoke CTest with `--test-dir build/ci`
  rather than `ctest --preset ci`.

## 8. Benchmarking protocol

- Benchmarks must use declared manifests and stable IDs.
- Distinguish smoke checks from heavy/nightly runs.
- Record metrics and diagnostics in machine-readable output.
- Do not claim performance wins without baseline comparison.
- Validate manifests and result payloads with `python3 tools/benchmark/validate_benchmark_manifests.py` and
  `python3 tools/benchmark/validate_benchmark_results.py`.

## 9. Documentation sync protocol

When code, structure, or policy changes:

- Update relevant architecture/migration/task docs in the same PR.
- Update references and links for moved files.
- Regenerate inventories when required by tooling.
- After module surface changes, refresh `docs/api/generated/module_inventory.md` with
  `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- After opening, retiring, or re-gating any task, regenerate `tasks/SESSION-BRIEF.md` with
  `python3 tools/agents/generate_session_brief.py`; after editing `docs/agent/*` or `tasks/templates/task.md`,
  re-run `python3 tools/agents/sync_skills.py --write`. CI (`ci-docs.yml`) enforces freshness of both.
- Keep docs factual (current state), not aspirational unless clearly labeled.

## 10. CI expectations

- PR checks must remain green for touched areas.
- Structural checks (tasks/docs/layering/manifests/skill mirrors) run strict in `ci-docs.yml`; treat their failures as
  merge blockers, not advisories. A newly introduced check may run in warning mode only while a referenced task ID owns
  its tightening (same idiom as §13 temporary exceptions); an untracked warning-mode check is a policy violation.
- Workflow definitions must stay readable and split by purpose.
- A failing check that is pre-existing or environmental (flake, runner variance, harness defect) is converted into a
  `BUG-` task under `tasks/backlog/bugs/` in the same session it is observed, with evidence, and referenced from the PR
  (see `docs/agent/prompt/prompt.md` §"When CI fails"). Gates are never weakened, skipped, or quarantined to reach
  green without a diagnosis.
- Agent/Codex verification must configure the `ci` preset, build a meaningful target such as `IntrinsicTests` (never
  `help` as a stand-in), and run CTest. The current Codex verification command mirrors the default CPU-supported gate
  from the testing protocol.
- Touched-scope structural checks use the repository tools, for example
  `python3 tools/agents/check_task_policy.py --root . --strict`, `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/repo/check_layering.py --root src --strict`, and
  `python3 tools/repo/check_test_layout.py --root . --strict`. Repository-root hygiene is enforced by
  `python3 tools/repo/check_root_hygiene.py --root .`; PR review structure lives
  in `.github/pull_request_template.md` and `docs/agent/review-checklist.md`.

## 11. Task execution workflow

Every task execution should follow this sequence:

1. Inspect existing code and docs.
2. Identify owning subsystem and layer.
3. Write or update task file.
    - Base new task files on `tasks/templates/`; do not create long-lived root-level planning checklists once work
      belongs in `tasks/backlog/`, `tasks/active/`, or `tasks/done/`.
4. Implement the smallest useful patch.
5. Add or update tests.
6. Add or update docs.
7. Run verification.
8. Update generated inventories.
9. Self-review against PR checklist.

The generated `tasks/SESSION-BRIEF.md` is the authoritative open/unblocked
view of the task tree (derived from task front-matter); consult it before
picking work. The cross-domain convergence themes and their rationale are
tracked in `tasks/backlog/README.md`; retirement narratives live in the
append-only `tasks/done/RETIREMENT-LOG.md`. Older retired tasks are swept
from `tasks/done/` to `tasks/archive/` (frozen read-only history; IDs stay
authoritative for dependency resolution). Keep roadmap details in those
files rather than expanding this contract with task-specific plans.

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

- Exception is documented in a current task under `tasks/active/`.
- Exception has a specific removal task ID.
- Exception is time-bounded and reviewed.
- Exception does not create new violations in promoted final layers.

Undocumented exceptions are policy violations.

## Related expanded docs

Read this `AGENTS.md` file at the start of every session/task; it is the authoritative contract. Read expanded
`docs/agent/` procedures when their trigger applies rather than reading every specialized guide for every task:

| Document                                      | Read when                                                                                                                                                                                                               |
|-----------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `docs/agent/how-this-repo-is-built.md`        | You need an outside-reader overview of the task lifecycle, convergence themes, skills, validators, CI gates, and the evidence trail for a retired task.                                                                  |
| `docs/agent/contract.md`                      | You need expanded rationale or detail for this contract, especially during onboarding or policy edits.                                                                                                                  |
| `docs/agent/prompt/prompt.md`                 | Starting a generic (non-task-specific) session: work selection, slice picking, verification bundles, commit/PR hygiene, anti-patterns, and multi-task loop-mode policy (default stop condition, checkpoint pushes).      |
| `docs/architecture/index.md`                  | Designing subsystem architecture, algorithm backend splits, config/command control surfaces, recipe/frame composition, or deciding which canonical architecture doc or ADR governs a change.                           |
| `docs/agent/task-format.md`                   | Creating, promoting, retiring, or materially updating files under `tasks/`.                                                                                                                                             |
| `docs/agent/review-checklist.md`              | Before committing or reporting completion for any non-trivial change.                                                                                                                                                   |
| `docs/agent/architecture-review-checklist.md` | Changing dependency boundaries, module ownership, source layout, runtime wiring, or architecture docs.                                                                                                                  |
| `docs/agent/method-workflow.md`               | Implementing or modifying paper/method work under `methods/` or method-backed integrations.                                                                                                                             |
| `docs/agent/method-review-checklist.md`       | Reviewing method/paper changes before commit or PR.                                                                                                                                                                     |
| `docs/agent/benchmark-workflow.md`            | Adding, changing, or running benchmark harnesses, manifests, datasets, baselines, or reports.                                                                                                                           |
| `docs/agent/benchmark-review-checklist.md`    | Reviewing benchmark changes or performance claims before commit or PR.                                                                                                                                                  |
| `docs/agent/docs-sync-policy.md`              | Moving files, changing public APIs/module surfaces, updating docs, or deciding whether generated inventories/manifests must be refreshed.                                                                               |
| `docs/agent/roles.md`                         | Clarifying agent responsibilities, handoff expectations, or role-specific workflow questions.                                                                                                                           |
| `docs/agent/agent-output-review-checklist.md` | Running the weekly human-led audit of recent agent-authored slices for cross-PR scope drift, documented-but-untested claims, and process anti-patterns not visible in a single PR review.                               |
| `docs/agent/drift-audit-checklist.md`         | Running the recurring whole-tree repo-state drift audit (inventory drift, retired allowlist owners, stale `(planned)` markers, dead seams, untracked TODO/shim markers, naming/cross-doc rot). State-scoped; additive to the weekly sweep. |
| `docs/agent/clean-workshop-review.md`         | Reviewing a change that touches a dependency boundary, adds a renderer subsystem/pass, changes RHI/platform/runtime wiring, closes a scaffold/parity task, or edits the layering allowlist — the drift/decomposition scorecard. |
| `docs/agent/task-maturity.md`                 | Closing a task whose stop-state is ambiguous, especially rendering, Vulkan, asset ingest, hot reload, pass command bodies, runtime composition, or legacy retirement; reviewers checking the `Scaffolded` closure rule. |
