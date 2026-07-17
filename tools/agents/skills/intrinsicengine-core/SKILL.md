---
name: intrinsicengine-core
description: Authoritative repository contract for IntrinsicEngine — a modular C++23 Vulkan-based research engine for graphics and geometry processing. Defines layering invariants (core/geometry/assets/ecs/physics/graphics/platform/runtime/app), C++23 module rules with clang-20 + clang-scan-deps-20, the CMake preset workflow, the task-driven slice discipline, and the default CPU correctness gate. Use this skill whenever working in any clone of `IntrinsicEngine`, touching any file under `src/`, `tasks/`, `methods/`, `benchmarks/`, `docs/`, or `tools/`, or when asked about layer ownership, module conventions, build commands, the CI preset, the agentic workflow, or anything that mentions IntrinsicEngine, `AGENTS.md`, or repository policy — even if the user does not explicitly ask for the contract.
---

# IntrinsicEngine Core Contract

This skill is the agent-facing entry point for working in the IntrinsicEngine
repository. The authoritative on-disk contract is `AGENTS.md` at the repo root;
this skill mirrors it and routes to expanded procedures.

If `AGENTS.md` in the checkout ever disagrees with this skill, **`AGENTS.md` wins**.
Always re-read `AGENTS.md` at the start of a session — this skill is a routing
aid, not a replacement.

## Session start sequence

Read in this order, only as deep as the touched scope requires:

1. `/AGENTS.md` — authoritative contract. Re-read every session.
2. `tasks/SESSION-BRIEF.md` — generated current state: active tasks plus
   per-theme unblocked/blocked backlog. Authoritative for what is open and
   unblocked; regenerate with
   `python3 tools/agents/generate_session_brief.py` after opening, retiring,
   or re-gating any task.
3. The chosen task file — read completely before touching code.
4. `tasks/active/README.md` / `tasks/backlog/README.md` — on demand only,
   for theme priorities, rationale, and the promotion checklist.
5. The specialist procedure for your touched scope (see "Routing" below).

Then inspect repo state before choosing work:

```bash
git status --short --branch
git log --oneline -10
ls tasks/active/
```

For the full default session onboarding (work selection, slice picking, anti-patterns
to refuse), read `references/session-onboarding.md`.

## Architecture invariants (non-negotiable)

Dependency boundaries — lower layers never import higher layers:

- `core` → nothing
- `geometry` → `core`
- `assets` → `core`
- `ecs` → `core`; geometry handles/types only when explicitly required
- `physics` → `core`, `geometry`; no live ECS/runtime/graphics/platform/app ownership
- `graphics/rhi` → `core`
- `graphics/assets` → `core`, asset IDs (`Asset.Registry` types only), `graphics/rhi`; no live `AssetService` traffic
- `graphics/vulkan` → `core`, `graphics/rhi`, backend-local Vulkan deps (`Vulkan::Vulkan`, `volk`, `VulkanMemoryAllocator`, `glfw`); **no ECS, runtime, or live asset-service knowledge**, and no `Vk*` types through RHI/renderer APIs
- `graphics/*` → `core`, asset IDs, `graphics/rhi`, geometry GPU views; **no live ECS knowledge**
- `platform` → `core`
- `runtime` → all lower layers; owns composition/wiring, including physics bridge ownership
- `app` → `runtime` only
- `methods` → public method API + declared backend integration only
- `benchmarks` → public method APIs only
- `tests` → explicit test seams only

Cross-layer convenience imports that violate this table are prohibited.

## Coding rules

- Use C++23.
- Preserve existing module names during mechanical directory moves.
- Do not mix mechanical moves with semantic refactors.
- Avoid introducing new engine features during reorganization tasks.
- Keep patches small and scoped to one task unless explicitly batched.
- Prefer deterministic, testable APIs with explicit ownership and failure states.
- Out-of-source CMake presets only; `CMakeLists.txt` rejects in-source configure.
- Presets require Clang 20 as the minimum supported major version and auto-select the highest
  complete installed Clang toolchain (`clang`, `clang++`, and matching `clang-scan-deps`) at
  version 20 or newer. **GCC and stale non-preset build trees are not valid verification for
  module changes.**
- Declare module libraries with `intrinsic_add_module_library(...)` from
  `cmake/IntrinsicModule.cmake`, and module interfaces via
  `target_sources(... FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ...)`.
- Keep `.cppm` module interfaces focused on exported types, declarations, small
  inline accessors, and templates that must be visible to importers. Put
  non-trivial implementations in matching `.cpp` module implementation units and
  add them as private target sources. Treat an implementation as non-trivial when
  it owns algorithm/control-flow bodies, allocation-heavy work, topology/container
  traversal, backend calls, diagnostics assembly, file/IO handling, or imports
  other modules only needed by the implementation rather than the public API.
- Third-party C/C++ deps go through `vcpkg.json` and the repository-local vcpkg
  toolchain (`external/vcpkg/scripts/buildsystems/vcpkg.cmake`) chainloaded with
  `cmake/IntrinsicClangToolchain.cmake`. Run `tools/setup/bootstrap_vcpkg.sh` on
  fresh checkouts; use `VCPKG_BINARY_SOURCES` for local/CI binary caching.
  `cmake/Dependencies.cmake` is vcpkg-manifest-only; new dependency traffic must
  go through `vcpkg.json`, `vcpkg-configuration.json`, or repository overlay
  ports.

## Default build and test commands

```bash
# Configure
cmake --preset ci

# Build tests
cmake --build --preset ci --target IntrinsicTests

# Default CPU-supported correctness gate
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Note: `CMakePresets.json` defines configure/build presets but no CTest presets;
invoke CTest with `--test-dir build/ci` rather than `ctest --preset ci`.

For local iteration on changed paths, prefer the touched-scope helper before the
full gate:

```bash
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --print
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --run
```

The helper drives the staged `pr-fast` feedback lane: structural-only changes
skip C++ setup, focused source changes use the unsanitized Null/headless
`ci-fast` preset, and ambiguous or graph-affecting changes broaden. It is not a
replacement for the full CPU, sanitizer, or capability-specific PR/merge
gates.

Touched-scope structural checks (run when relevant):

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Platform and runtime configuration

- Platform backend is explicit: `INTRINSIC_PLATFORM_BACKEND=Auto|Null|Glfw`.
  Use `Null` / `INTRINSIC_HEADLESS_NO_GLFW=ON` for headless work unless a task
  specifically needs GLFW/Vulkan surface coverage.
- Runtime owns graphics backend selection. Promoted Vulkan is opt-in only when
  both `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` and
  `RenderConfig::EnablePromotedVulkanDevice` are enabled; otherwise Vulkan
  requests fall back to the Null device.
- Renderer/runtime code must gate on `RHI::IDevice::IsOperational()`, not on
  Vulkan diagnostics.

## Routing — specialist skills and references

Load the specialist skill for the touched scope rather than reading every guide:

| Touched scope | Skill to consult |
| --- | --- |
| Creating, promoting, retiring, or materially updating files under `tasks/` | `intrinsicengine-task-workflow` |
| Before committing or reporting completion for a non-trivial change | `intrinsicengine-review` |
| Changing dependency boundaries, module ownership, source layout, runtime wiring | `intrinsicengine-review` (architecture-review checklist) |
| Planning or reviewing new abstraction surface (interfaces, service/bridge/registry facades, module frameworks, event/command indirection), a small change fanning out across many files, or suspected over-engineering/glue | `intrinsicengine-right-sizing` |
| Proposing novel, unconventional, cross-domain, or potentially publishable research directions (the ideation front end that feeds the method track) | `intrinsicengine-research-ideation` |
| Auditing method, benchmark, backend-parity, capability-maturity, or quantitative claims after evidence exists | `intrinsicengine-results-audit` (then the method/benchmark/review specialists it routes) |
| Implementing or modifying paper/method work under `methods/` | `intrinsicengine-method` |
| Adding, changing, or running benchmark harnesses/manifests/baselines | `intrinsicengine-benchmark` |
| Moving files, changing public APIs/module surfaces, refreshing inventories | `intrinsicengine-docs-sync` |
| Adding or changing an asset import/materialization path, or an import that "succeeds" but is not visible/selectable in the sandbox | `intrinsicengine-import-visibility-contract` |
| Adding or changing a geometry importer/exporter (OBJ/OFF/PLY/STL/PCD/XYZ/TGF), parsing an untrusted header count, or defining IO diagnostics/fixtures | `intrinsicengine-geometry-io-format` |
| Diagnosing a hard bug, validation-layer error, parity mismatch, or perf regression | `intrinsicengine-diagnose` |
| Sandbox input capture, window-close/exit, edit-flush ordering, camera/cursor sign, or drag-drop poll-thread wiring in `Engine::RunFrame` | `intrinsicengine-sandbox-input-lifecycle` |
| Debugging a black/wrong frame, VUID cascade, or driver crash on the promoted Vulkan path | `intrinsicengine-vulkan-frame-triage` |
| Authoring or changing an opt-in `gpu;vulkan` readback smoke, or proving a fix `Operational` | `intrinsicengine-gpu-smoke-authoring` |
| Unexplained SEGV/ASan/vtable/ICE failure, especially after `.cppm` module changes | `intrinsicengine-stale-build-triage` |
| Getting a layer-cake map of an unfamiliar file before editing | read `tools/agents/skills/intrinsicengine-zoom-out/SKILL.md` directly (user-invoked slash skill, not model-invocable) |
| Navigating module deps, change impact, or paper→method→code links | Knowledge-graph discovery aid (below) |
| Compacting a long session into a handoff doc for the next agent | `intrinsicengine-handoff` |

## Knowledge-graph discovery aid (use it before manual tracing)

A `knowledge-graph` MCP server (registered in `.mcp.json`, provisioned by session
setup) serves a merged graph of the whole-repo C++23 module DAG **and** the
paper→method→code chain. Its dependency edges come **only from C++23 module
`import` statements** — the adapters do not parse `#include`, so header include
dependencies are absent from the graph. Reach for it in these cases:

- **Before touching a `.cppm` interface** — `get_neighbors` to see which modules
  it imports and who imports it.
- **Scoping a change's module-level blast radius** (review, docs-sync) —
  `shortest_path` and reverse-dependency walks over module `import` edges to
  find downstream consumers.
- **Suspected layering problem** — edges are pre-tagged
  `same-layer`/`allowed`/`violation`; spot it on the graph, then **confirm with
  `tools/repo/check_layering.py --root src --strict`**, which stays the sole gate.
- **Architecture hot-spots** — `god_nodes`/`graph_stats` for over-connected modules.
- **Paper-claim ↔ code traceability** — trace which paper claim a method
  implements and which modules realize it (see `intrinsicengine-method`).

**For `#include`-based dependencies the graph is incomplete** — any unit still
using C/C++ headers will have include edges the graph does not show. For those
units, fall back to source search (Grep/Glob) and to
`tools/repo/check_layering.py`, which is the authority that covers **both**
`import` and `#include` edges. The graph is a module-import navigation aid, never
an authority: confirm every finding against the gate (`check_layering.py`) or the
method contract (`method.yaml` + `docs/methods/*`) before you act. If the server
is absent, proceed normally — no task depends on it. Full use-case detail:
`references/contract.md` §"Knowledge-graph discovery aid".

References bundled with this skill (read on demand):

- `references/contract.md` — expanded rationale for invariants, mission, and protocols.
  Read during onboarding, contract edits, or when you need the *why* behind a rule.
- `references/roles.md` — agent role definitions (Architect, Implementation, Test,
  Review, Paper) and the rotating weekly-review ownership.
- `references/session-onboarding.md` — the default generic session prompt: how to
  find work, scope it, verify it, and ship it; commit/PR hygiene; anti-patterns.

## Method implementation protocol (high-level)

Method/paper work must follow this order — see `intrinsicengine-method` for the
full procedure and review checklist:

1. Intake paper + define method contract.
2. Implement CPU reference backend first.
3. Add correctness tests.
4. Add benchmark harness/manifests.
5. Add optimized CPU backend.
6. Add GPU backend only after reference parity exists.
7. Document numerical limitations and diagnostics.

## Commit and PR hygiene

- One task per PR unless explicitly batched.
- Separate commits for independent slices and for non-trivial docs/task sync.
- Stage only intentional changes; never include editor/build artifacts.
- **Never use `--no-verify`, `--amend` on shared history, or force-push to `main`/`master`.**
- Commit messages: imperative subject ≤ 72 chars; body explains *why* and lists
  verification commands actually run.
- Retire completed active tasks to `tasks/done/` with completion date (`YYYY-MM-DD`)
  and commit/PR reference, append the narrative to `tasks/done/RETIREMENT-LOG.md`,
  and regenerate `tasks/SESSION-BRIEF.md`.

## Temporary migration exceptions

Allowed only when **all** of the following hold:

- documented in a current task under `tasks/active/`,
- linked to a specific removal task ID,
- time-bounded and reviewed,
- does not create new violations in promoted final layers.

Undocumented exceptions are policy violations.

## When stuck

- Add a nonblocking clarification question to the relevant task file rather
  than blocking; pick the more robust default and continue.
- Prefer the more deterministic, more testable, smaller-blast-radius option.
- If a task is too large for one slice, write the slice plan into the task file
  *before* implementing.
- If state on disk surprises you (unfamiliar files, branches, locks), investigate
  before deleting or overwriting — it may be in-progress work on another branch.
