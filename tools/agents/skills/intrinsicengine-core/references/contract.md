# Agent Contract (Expanded)

Detailed engineering requirements referenced by [AGENTS.md](../../../../../AGENTS.md).
These requirements remain mandatory when their scope applies; the root contract
wins in a conflict. Read the named section for the touched scope, not this whole
file at startup. Section numbers in prose refer to `AGENTS.md`.

## Mission

Build and maintain IntrinsicEngine as a modular, high-performance, scientifically rigorous engine for graphics, geometry
processing, and method-driven research integration.

The immediate product objective is full user-facing feature and workflow
parity with Framework24, delivered as a demonstrably better replacement in
modularity, extensibility, reliability, usability, and performance.
Framework24 is the behavioral baseline, not an
architecture or implementation template: IntrinsicEngine need not reproduce
its APIs, algorithms, source organization, or OpenGL design. Its own
architecture and implementations may be redesigned through the normal reviewed
architecture workflow whenever that produces the better engine. The current
C++23/Vulkan, layering, and reliability contracts are quality floors, not a
frozen component diagram; they remain authoritative until an explicit reviewed
change replaces them. “Better” never excuses a missing Framework24 feature or
user outcome. The authoritative feature inventory, scorecard, and golden
workflows live in `docs/product/framework24-convergence.md`.

Until `REVIEW-004` retires with an accepted convergence verdict, Framework24
product convergence is the standing P0 focus for work selection. All
agent-chosen work — suggestions in pair posture, delegated selection, and
unattended overnight runs — defaults to Theme J tasks, explicit unsatisfied
`REVIEW-004` dependencies, reproducible regressions, and
correctness/reliability work that a golden workflow requires; a pre-existing
method task qualifies only when the Framework24 feature inventory makes it an
explicit product dependency. This focus is a strong recommendation the agent
surfaces, not a refusal rule: the human operator may explicitly direct work
outside it, in which case the agent records that direction in the task note
and proceeds. Preserve paused research evidence and resume the research track
after the gate retires.

All agent work must preserve:

- Buildability.
- Testability.
- Layer ownership.
- Documentation synchronization.
- Reviewability of mechanical vs semantic changes.
- Truthful requested/actual/fallback reporting for every selectable algorithm
  backend; a backend token exists only for a real implementation with parity
  and benchmark evidence appropriate to its claim.

## Architecture invariants

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

## Source tree map

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

## Layering rules

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

## Coding and change-scope rules

- Use C++23.
- During the current simplification and compile-locality migration, public C++
  APIs and engine-owned scene/config formats have no backward-compatibility
  commitment: the operator confirms there are no external API consumers or
  persisted user data to migrate. Update in-tree callers, codecs, fixtures and
  tests together and remove superseded paths directly. Do not add compatibility
  wrappers, aliases, dual readers or format migrations solely to retain those
  old representations. Preserve user-facing capabilities and current-format
  save/load and config round-tripping.
- Before adding or copying a non-trivial implementation, helper, or source file, use
  `intrinsicengine-reuse` to find existing owners and consumers and compare their contracts.
  Record the chosen reuse path or concrete mismatch briefly in the existing task/review or response.
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
- Keep source documentation concise and current-state. Every project-owned `.cppm` and header begins with a short
  synopsis of what the file contains and why its surface exists. Comment declarations only when a correctness,
  ownership, lifetime, ordering, units, numerical, or similarly non-obvious contract cannot be expressed clearly in
  code. Put implementation rationale in the matching `.cpp` near the affected implementation (or beside an inline/
  template implementation that must remain in the interface). Source comments do not narrate task history. The
  canonical rules and audit workflow live in `docs/agent/source-documentation-policy.md`.
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

Method/paper work must follow this order:

1. Query and review the original paper plus relevant extensions and subsequent
   improvements; record stable citations, select the exact formulation, and
   define the method contract.
2. Implement CPU reference backend first.
3. Add correctness tests.
4. Add benchmark harness/manifests.
5. Add optimized CPU backend.
6. Add GPU backend only after reference parity exists.
7. Document numerical limitations and diagnostics.

Geometry method binding follows the canonical property-domain substitutability
contract in `docs/architecture/geometry-api-style.md`:

- Method inputs and outputs are semantic slots bound by full canonical property
  references (element domain + property name + value kind). A slot named
  `Position` does not require a property named `v:position`; for example it may
  bind `f:centroid` on mesh faces when that property satisfies the typed
  contract. Do not create compatibility aliases merely to satisfy a slot name.
- Property binding is name- and provenance-independent across all methods,
  config/agent paths, and UI pickers. Equal-dimensional numeric properties are
  interchangeable inputs when their element correspondence and the method's
  stated numerical constraints match. Resolve storage differences through
  checked numeric conversion at the binding boundary, never by reinterpreting
  storage, truncating channels, or silently substituting a named default.
  Property names do not imply curvature, normals, colors, or other algorithm
  semantics; such interpretation and derived-input generation are explicit
  method/config choices. Output mutation still enforces the declared storage
  type, aliasing rules, and structural-property ownership.
- A point-set method consumes the compatible typed `Property<T>` /
  `ConstProperty<T>` (or span) it names on any resolved element domain. It must
  not require point-cloud provenance, a `Vertices` component, or a
  `VertexProperty` wrapper merely because points were used during development;
  mesh face centers, edge samples, and halfedge samples are valid when they
  satisfy the typed input contract.
- A graph method adds only the node/edge/halfedge adjacency and properties its
  algorithm actually needs. A mesh satisfying those sources must be accepted
  without mesh-to-graph conversion. Mesh-only eligibility is valid only when
  faces or surface topology are semantic inputs.
- Same-cardinality results publish only named output properties on the
  originating element domain and preserve unrelated properties/topology.
  Topology/cardinality changes require an explicit owning operation and must
  never silently replace a richer entity.
- Runtime, config/agent, and UI availability must reuse the same canonical
  property/topology preflight. Every appropriate provenance menu must expose
  compatible property domains; handle-specific property aliases are
  conveniences, not discovery filters.
- Public, persisted geometry vector properties use float `glm::vec*` storage.
  Kernels may use `double`/`glm::dvec*` for conditioning, accumulation,
  predicates, and other precision-sensitive internal work, but convert at the
  publication boundary instead of exposing double-vector properties by
  default. A deliberate public double-vector contract requires its own scoped
  API decision and typed catalog support.

Every new or materially changed geometry-method task must declare
`geometry.element-domain-sources` and `method.engine-integration` when
applicable, spell out these decisions in its `## Engine integration` matrix,
and name follow-up tasks for deferred runtime/config/UI/publication rows.

For spatial-query work, review `docs/architecture/spatial-index-consumers.md`
and record the query semantics, index ownership/reuse and missing capabilities
using `docs/agent/method-workflow.md#spatial-acceleration-review`. Keep relevant
open-task reminders synchronized; proximity acceleration must preserve the
method's primitive, metric, topology and neighborhood contract.

Method manifests live at `methods/**/method.yaml` and are validated by
`python3 tools/agents/validate_method_manifests.py`.

## Testing and verification protocol

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
    - For local iteration on changed paths, use `python3 tools/ci/touched_scope.py --root . --local --base-ref origin/main
      --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --print` (or `--run`). Local mode
      includes committed changes through `HEAD`, staged and unstaged changes, and non-ignored untracked files.
      CI omits `--local` to compare only its supplied base/head revisions. The same staged planner drives `pr-fast`:
      it runs structural-only changes before C++ setup, configures
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

  GPU/Vulkan, slow, and explicitly quarantined tests are opt-in and must be justified by label policy. The required
  hosted full-CPU workflow explicitly enables replacement-only grouped registration and invokes the same selector with
  `--parallel 4`. This is a fixed workflow budget, not a host-derived local default; the command above retains
  individual registration and does not infer a parallel budget from the host.
- The canonical `ci` preset is unsanitized. Required address and undefined-behavior sanitizer coverage uses the
  isolated `ci-asan` and `ci-ubsan` presets, their matching `build/ci-asan` and `build/ci-ubsan` trees, and the same
  exclusion-only CPU selector:

  ```bash
  cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
  cmake --build --preset ci-asan --target IntrinsicCpuTests
  ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
  cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
  cmake --build --preset ci-ubsan --target IntrinsicCpuTests
  ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
  ```

  Required CPU variants use replacement-only grouped registration for the audited pure cohort. Sanitizer CTest
  execution remains explicitly serial with `--parallel 1`; do not raise that budget without matched sanitizer
  evidence. Tests that intentionally create multiple scheduler workers carry case-specific CTest `PROCESSORS`
  reservations.
- Promoted Vulkan opt-in verification uses the `ci-vulkan` preset plus GPU/Vulkan label intersection. Among required
  CI gates, this is the only one that retains combined ASan+UBSan instrumentation because the Vulkan shutdown contract
  owns explicit LeakSanitizer evidence:

  ```bash
  cmake --preset ci-vulkan
  cmake --build --preset ci-vulkan --target IntrinsicTests
  ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
  ```
- `CMakePresets.json` defines configure/build presets but no CTest presets; invoke CTest with `--test-dir build/ci`
  rather than `ctest --preset ci`.

### Focused verification guidance

When a task needs a non-headless backend sanity check, prefer the smallest direct
target that proves the touched seam. For Vulkan renderer integration, use focused
CPU contract tests plus a direct `ExtrinsicBackendsVulkan` build before attempting
broad runtime-test executables. Run focused targets first, then broaden when they
pass and the task requires it; the touched-scope route does not replace required
full CPU, sanitizer, or capability-specific gates.

## Benchmarking protocol

- Benchmarks must use declared manifests and stable IDs.
- Canonical result schema v2 keeps stable `benchmark_id` separate from
  append-only `run_id`/`attempt_id`, binds the exact manifest hash, resolved
  params/warmup/thresholds, and source state, and recomputes gate disposition.
- `"local-dev"`, dirty, historical, or unverified source is non-claim-eligible;
  claim eligibility is explicit and requires a clean exact commit or an
  approved sealed snapshot/diff identity.
- Distinguish smoke checks from heavy/nightly runs.
- Record metrics and diagnostics in machine-readable output.
- Do not claim performance wins without baseline comparison.
- Validate manifests and result payloads with `python3 tools/benchmark/validate_benchmark_manifests.py` and
  `python3 tools/benchmark/validate_benchmark_results.py`.
- A benchmark result that becomes a repeatable claim also owes an `ara/logic/claims.md` row (§8b).

## Research claim and evidence protocol

Research, performance, parity, and capability statements are tracked in the Agent-Native Research
Artifact under `ara/`. `ara/logic/claims.md` is the claim ledger: each `C<NN>` row carries a
statement, a disposition, a falsification criterion, and a `Proof` binding to artifacts that exist
in the tree. `ara/staging/observations.yaml` holds `O<NN>` observations awaiting closure;
`ara/logic/solution/` crystallizes constraints (`K<NN>`), architecture statements (`A<NN>`), and
heuristics (`H<NN>`).

- The ledger tracks **research results**: method, benchmark, parity, and capability claims produced
  by research work (`methods/`, `benchmarks/`, method reports, and research-derived statements
  entering `README.md`, `docs/`, or a task status line). Such a statement needs a claim row first.
  A commit message is not a claim record.
- Ordinary implementation and refactoring work owes no claim rows; its record is the PR, its tests,
  and its docs. The moment such work produces a claim-shaped statement ("2× faster", "parity with
  the reference", "operational on Vulkan"), that statement is a research result and follows this
  section.
- A gate that rejected a hypothesis gets a `refuted` row with the gate that killed it, not a
  silently dropped branch.
- A `supported` or `refuted` claim must cite at least one repository path that exists; moving a
  cited artifact means updating the claim in the same change.
- Benchmark-backed claims still owe the manifest and baseline comparison required by §8.
- Evidence must address the claim: performance improvements need matched benchmark comparisons;
  parity needs reference comparisons with stated tolerances; operational capability needs a run
  of the named backend or integration path. A capability or parity statement alone does not
  require a new performance benchmark. Method implementation still follows §6.
- CPU, GPU/Vulkan, and sanitizer results are distinct evidence classes; a claim must say which one
  it rests on.

`python3 tools/agents/check_ara_claims.py --root . --strict` validates the ledger structure and
runs strict in `ci-docs.yml`. The authoritative policy, record format, and anti-patterns live in
`docs/agent/ara-evidence-policy.md`.

## Documentation sync protocol

When code, structure, or policy changes:

- Update relevant architecture/migration/task docs in the same PR.
- Update references and links for moved files.
- Regenerate inventories when required by tooling.
- After module surface changes, refresh `docs/api/generated/module_inventory.md` with
  `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- After opening, retiring, or re-gating any task, regenerate `tasks/SESSION-BRIEF.md` with
  `python3 tools/agents/generate_session_brief.py`; after editing `docs/agent/*` or `tasks/templates/task.md`,
  re-run `python3 tools/agents/sync_skills.py --write`. CI (`ci-docs.yml`) enforces skill-mirror
  freshness; the session brief is a convenience view regenerated opportunistically.
- Keep docs factual (current state), not aspirational unless clearly labeled.
- Keep every `README.md` as a concise current-state entry point. Link to ADRs, migration docs, tasks, retirement records,
  reports, or Git for history and future work instead of copying those narratives into a README.

## CI expectations

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
  in `.github/pull_request_template.md` and `docs/agent/review.md`.

## Task execution workflow

Sessions follow the pair workflow defined in `docs/agent/prompt/prompt.md`:
the agent operates in one of three postures — Pair (default observant
copilot), Delegate (bounded hand-off), Advisor (direction, method selection,
literature research) — with review effort gated by risk signals (new
dependency edges, public module surfaces, research claims, destructive
actions) instead of applied uniformly to every change.

Authorization persists for the agreed task. Necessary implementation choices within its scope
and the existing layer policy do not require repeated approval. Obtain a new human decision
before landing a change to agreed scope, layer policy or its exceptions, compatibility commitments,
or a destructive/irreversible action that has not already been authorized. Complete authorized
preparation first so the decision concerns a concrete diff and its impact. Review and verification
remain required even when no new approval is needed.

Use one writer per checkout and build directory in every posture. Concurrent writing agents use
separate branches/worktrees and their own build directories; read-only review uses a fixed diff.
Before integrating, reconcile changes and re-run affected verification on the combined source.
Interactive work does not need the overnight claim/work-graph machinery to observe this rule.

Task files under `tasks/` are shared memory between sessions, not process
contracts. Single-session work needs no task file. Work that outlives the
session gets a note in `tasks/active/` seeded from
`tasks/templates/task-micro.md` — the interactive lane (`template: micro`,
`workflow_profile: micro`, `evidence: not_applicable`, concrete
`evidence_skip_reason`). The full `tasks/templates/task.md` and the
`standard`/`high-risk` profiles are the unattended overnight lane;
`claim-grade` and `protected` custody remain opt-in for publication-bound
claims (`docs/agent/workflow-evidence.md`). Do not create long-lived
root-level planning checklists once work belongs in `tasks/backlog/`,
`tasks/active/`, or `tasks/done/`.

When creating or materially changing a task file, consult
`docs/architecture/contract-catalog.yaml` and declare applicable stable
contract IDs in front-matter (or record a justified-empty `contract_review`).
Task wording may not silently narrow a canonical contract; a new reusable
contract or an intentional contract change updates its canonical source,
catalog entry, and executable proof in the same reviewed change.

Delegated and unattended work follows this sequence:

1. Inspect existing code and docs; identify the owning subsystem and layer.
2. Read or write the task note; ask clarifying questions once, up front, then
   record chosen defaults.
3. Implement the smallest robust slice; add or update tests with it.
4. Update docs when a surface or structure actually changed (§9).
5. Run the strongest relevant verification (§7), touched-scope first; update
   generated inventories when module surfaces changed.
6. Sweep the diff (scope, layering, tests, docs — §12), commit, push, and
   report what changed, how it was verified, and what remains uncertain.

Claim (`task_claim.py`), live work-graph (`agent_work_graph.py`), and
completion-evidence (`workflow_evidence.py`) machinery is scoped to unattended
overnight runs — where it substitutes for the absent human — and to opt-in
custody profiles; interactive sessions owe none of it. Unattended runs accept
only night-ready tasks (complete goal, checkbox acceptance criteria, exact
verification commands, no open questions or loose ends) and retire their
enrolled tasks with the completion evidence their profile requires, including
the `seal.yaml` step for dirty-source reports and independent fixed-surface
review for `high-risk` and higher (`docs/agent/workflow-evidence.md`).
Historical tasks are not backfilled; the prospective inventory
deterministically enrolls new or changed open work.

Retire finished tasks to `tasks/done/` with completion date and commit/PR
reference, append the narrative to the append-only
`tasks/done/RETIREMENT-LOG.md`, and regenerate `tasks/SESSION-BRIEF.md` — the
generated open/unblocked view consulted when picking backlog work. Theme
rationale lives in `tasks/backlog/README.md`; older retired tasks are swept
from `tasks/done/` to `tasks/archive/` (frozen read-only history; IDs stay
authoritative for dependency resolution). Keep roadmap details in those files
rather than expanding this contract with task-specific plans.

### Standing Claude Code authorization

The operator authorizes Codex to delegate IntrinsicEngine planning, implementation,
refactoring, review, debugging and testing to Claude Code CLI through the operator's
configured Anthropic account. This includes transmitting private repository source
files, complete diffs, documentation and relevant build/test output needed for that
work. Authorization covers this repository, its worktrees and future tasks until
the operator revokes it. Do not request per-packet confirmation merely because the
files, diff size or amount of source context change.

Exclude credentials, secrets, authentication/session material and unrelated personal
data from shared payloads. Delegated work follows the same scope, layer, verification
and one-writer rules above; reconcile and verify any changes before integration.
This authorization does not grant permission to publish repository contents to other
destinations or override platform or managed restrictions. When an approval review
is required, cite this standing authorization with the actual destination, payload
scope and purpose.

## Review checklist

Before commit/PR, verify:

- Scope matches exactly one task unless batching is explicitly allowed.
- Layering invariants are preserved.
- Tests are updated and pass for touched scope.
- Docs and task records are synchronized.
- Temporary compatibility shims are tracked with removal follow-up.
- Mechanical moves and semantic edits are not mixed.
- For unattended overnight work: enrolled completion evidence matches the
  final source surface and profile, and high-risk acceptance is independent
  and revision-bound. Interactive work owes the sweep above, not evidence
  artifacts.

## Temporary migration exceptions

Temporary exceptions are allowed only when all of the following are true:

- Exception is documented in a current task under `tasks/active/`.
- Exception has a specific removal task ID.
- Exception is time-bounded and reviewed.
- Exception does not create new violations in promoted final layers.

Undocumented exceptions are policy violations.

## On-demand audit sweeps

The pre-merge sweep in [`docs/agent/review.md`](../../../../../docs/agent/review.md) catches
single-slice defects. Two deeper sweeps catch what the per-change view
misses, both defined in the same document and run **on demand — preferably
overnight — with no fixed cadence** (2026-08-14 decision; the earlier weekly
`REVIEW-001` and 2–4-week `REVIEW-002` cadences are retired):

- The **output audit** (`review.md` §"Output audit") covers a window of
  agent-authored commits for multi-PR scope drift, decorative comments,
  premature abstraction, documented-but-not-tested claims, and
  ceremony-without-shipped-value.
- The **drift audit** (`review.md` §"Drift audit") inspects the whole current
  tree for accumulated drift: inventory drift, retired allowlist owners,
  stale `(planned)` markers, dead seams, untracked TODO/shim markers,
  naming/cross-doc rot.

Neither gates PR merges. Findings land as `tasks/HINTS.md` entries or backlog
tasks; dated reports go to `docs/reports/<YYYY-MM-DD>-<sweep>-audit.md`, and
`python3 tools/agents/check_audit_cadence.py` reports last-report dates on
request. The `intrinsicengine-audit` skill is the operator entry point.
