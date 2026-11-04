# Engine and feature modules: the architecture target

- **Status:** The **only** current architecture target (operator, 2026-10-30). It is replaced only
  when its goals are met. Earlier target, vision and plan documents are archived in
  [`docs/archive/`](../archive/README.md); [ADR 0031](../adr/0031-declared-operations.md) is
  superseded.
- **Tracking:** [`ARCH-022`](../../tasks/active/ARCH-022-engine-and-feature-modules-target.md).
- **Origin:** after ADR 0031's phase 2 gate failed
  ([report](../reports/2026-10-30-phase-2-gate.md)), the operator stated the intent and decided
  the goals below.

## Goals (operator, 2026-10-30)

In the operator's words: *"Am Ende zählt das Änderungen günstig sind, die Engine modular und
erweiterbar ist und dass der Build und ci nicht so lange dauert."* Rebuilding things for these goals
is allowed.

1. **Changes are cheap.**
2. **The engine is the foundation; modules (plugins) add features and methods.** Each module
   registers itself.
3. **Run-time compilation:** an agent changes a module's code, and the Sandbox keeps running and picks
   up the change.
4. **Agents can control the engine completely over MCP.**
5. **Build and CI are fast.**

## The target in three rings

```
 ┌───────────────────────────────────────────────────────────────────┐
 │ Feature modules (plugins): one folder and one library each        │
 │ (outliers, normals, remesh, ICP, k-means, texture bake, …)        │
 │ Each one: its algorithm glue, config rows, panel and commands.    │
 │ It registers itself, can be rebuilt and reloaded while the        │
 │ Sandbox runs, and knows only the ring below.                      │
 ├───────────────────────────────────────────────────────────────────┤
 │ Module services: the small, stable surface plugins build against  │
 │ 1. scene and property access, with one undo transaction           │
 │ 2. jobs (snapshot in, commit on the main thread)                  │
 │ 3. GPU resources (property residency, spatial index cache)        │
 │ 4. config sections (declared once as rows)                        │
 │ 5. UI host (register a window and a menu entry)                   │
 │ 6. command registry: every action, for the UI and for MCP alike   │
 │ 7. plugin host: load, register, unload, reload                    │
 ├───────────────────────────────────────────────────────────────────┤
 │ Foundation: core, ecs, geometry and graphics libraries, rhi,      │
 │ assets, platform, the runtime kernel (frame loop, jobs, commands, │
 │ events, worlds)                                                   │
 └───────────────────────────────────────────────────────────────────┘
   Dependencies point down only. The two lower rings name no feature.
```

**What this implies:**
- **Plugins are separate libraries loaded by the running Sandbox.** A change to one rebuilds only
  that library, which serves goal 3 and goal 5 alike. The foundation and the services are the host
  and stay loaded.
- **Everything a plugin adds goes through the services' registries** (config sections, windows,
  commands), and the registries take it out again on unload. Nothing else may keep a pointer into a
  plugin's code across a reload; this includes undo entries, which therefore hold data or a
  registered command, not plugin closures.
- **The kernel stays as ADR 0024/0027 decided.** `tools/repo/check_kernel_convergence.py` keeps
  enforcing its closed surface. The archived `kernel-target-state.md` is history; its knob table no
  longer guides placement, this page does.
- **State that must survive a reload lives in the host:** the scene, the config and the undo
  history. A plugin rebuilds its own state on load.
- **Commands are the one way to act.** The UI and the MCP lane both call registered commands, and
  the MCP tool list is generated from the command registry at run time. A plugin's command
  therefore reaches agents without editing any central list. Today's MCP lane is described in
  [Agent control lane](agent-control-lane.md) (ADR 0029).

## Plugins: what goes where (operator, 2026-10-30)

**Three kinds of code, three places:**

| kind | example | where it lives |
|---|---|---|
| **algorithm**: stateless, input to output | marching cubes: grid to mesh (`Geometry.MarchingCubes`) | the foundation: `src/geometry` or `src/graphics` |
| **shared service with state** | the spatial index cache; a GPU pipeline with its buffers | the module services |
| **feature**: workflow, UI, commands, config | "extract an isosurface" in the editor | a plugin |

**Promotion instead of plugin dependencies.**
- A plugin never depends on another plugin, only on the two rings below.
- When a second plugin needs something from a plugin, exactly the part it needs moves down: an
  algorithm into the foundation, a stateful service into the module services, each with its own
  tests. The original plugin keeps its UI, commands and config.
- A new algorithm may start inside a plugin, where hot reload makes iteration fast, and is promoted
  when a second consumer appears. Promoted code no longer reloads while the Sandbox runs: the core
  stays stable, the plugins stay fluid.

**A plugin owns its UI, and its UI only calls commands.**
- A plugin registers its windows and menu entries itself.
- A button never changes the scene directly; it calls a registered command. The same command is an
  MCP tool, so an agent can do whatever the UI can (R5) without anyone maintaining a central list.

**The shape of a plugin** (settled by the hot-reload spike and slice 3 step 1, `ARCH-022`):

```
plugins/marching_cubes/
  CMakeLists.txt               intrinsic_add_feature_plugin(MarchingCubes <sources>): the objects, the MODULE with
                               generated v1 entry points (shared builds), a startup plugin of the Sandbox
  MarchingCubes.cppm           module Intrinsic.Plugin.MarchingCubes: config rows, the section, Register, Shutdown
  MarchingCubes.Plugin.cpp     Register adds everything through PluginRegistrations
  MarchingCubes.Run.cpp        its runs (PluginRun, SubmitPluginRunStage), on the module services
  MarchingCubes.Tools.cpp      its MCP tools (a waiting tool: WaitForPluginRun)
  MarchingCubes.Window.cpp     its window; buttons start runs as the tools do
```
(the two feature plugins, `plugins/bilateral_filter` and `plugins/descriptor_analysis`, have this shape; their
tests share `tests/support/PluginHarness.hpp`)

- **The contract** is `Extrinsic.Runtime.PluginContract` (`src/runtime/Services/`), the only module a plugin needs
  to reach the host:
  - three `extern "C"` entry points: `intrinsic_plugin_register_v1` fills `PluginRegistrations` (config sections,
    MCP operations, windows, history commands); `intrinsic_plugin_shutdown_v1`; `intrinsic_plugin_build_id_v1`
    returns `INTRINSIC_PLUGIN_BUILD_ID` from the generated `PluginBuildId.h`;
  - host functions for CPU jobs, GPU work (a queue participant), undo entries (`RecordPluginCommand`) and the
    engine's services;
  - a plugin hands callables to the host through these APIs only, so the host holds every one of them and
    removes them all before it closes the image;
  - a deferred result (a continuation, progress or cancel) is held by the host, so an unload drains it; the
    contract's run protocol (`PluginRun`, `SubmitPluginRunStage`, `CancelPluginRun`, `WaitForPluginRun`) gives a
    plugin's multi-stage runs and its waiting MCP tools;
- **v1 means the same build:** the build id is a sha256 over the toolchain, standard library, compile flags and
  definitions, build type, sanitizers, shared-engine setting and every engine module interface (`src/**/*.cppm`). The host refuses another
  id (`abi_mismatch`) and a missing entry point (`dlsym_failed`). There is no compatibility layer beyond that; a
  plugin is rebuilt with the engine.
- **The host** is `Extrinsic.Runtime.PluginHost`, a runtime module the Sandbox composes. Its MCP tools are
  `plugin_load`, `plugin_unload`, `plugin_reload`, `plugin_list` and `app_quit`.
  - A load opens a private copy, calls Register and commits everything atomically on the next frame. Every
    successful Register gets exactly one shutdown hook before the image closes, committed or not (a taken name, a
    failed commit, a reload to another name, a dropped pending load, a startup plugin dropped or not committed);
    a Register that returns false gets none and cleans up itself.
  - An unload drains first: the plugin's jobs are cancelled and reaped and its GPU work retired, within 5 s
    (else `drain_timeout`, and the plugin stays). Then the shutdown hook runs and every registration goes, with
    the config payloads saved. The image is closed only when none of its callables is alive.
  - A reload is an unload and a load; the saved payloads are validated again and reapplied.
  - Several plugins load side by side, each under the name its Register sets; a second plugin of a loaded name is
    refused (`already_loaded`). With more than one loaded, `plugin_unload` and `plugin_reload` need `plugin`
    (else `invalid_params`), and a reload whose build carries another name leaves the plugin unloaded
    (`name_mismatch`). `plugin_list` returns `{"loaded": [...]}`, one entry per plugin.
  - Each drain, unload and close concerns one plugin; the others keep running. A contract call (jobs, cancel,
    report, GPU work) acts for the plugin whose code the host is running, else for the only loaded one; it runs
    on the main thread, and with several loaded a call from outside any plugin is refused. A plugin cancels,
    reports and joins only its own jobs. Close accounting counts callables by build generation, so two plugins
    from one build share it: a live callable of one keeps the other's image mapped too (safe, never a close too
    early).
  - A cancelled job that has not started is reaped only when a worker reaches it, so another plugin's long jobs
    on every worker can delay a drain to `drain_timeout`; the plugin stays loaded and the unload can be retried.
  - A load, unload or reload waits while any plugin's code runs on the main thread (the registries it changes may
    hold the running callable), so one polled from inside plugin code completes only after that code returned.
    While its Register or shutdown hook runs a plugin is not loaded, and its contract calls are refused.
  - At the ordinary exit the engine settles GPU work and jobs; the host, which shuts down first, runs the
    shutdown hook and removes the registrations, and leaves the image mapped.
  - Fault injection and probes, for the fixture plugin's drivers, need `--plugin-test-hooks`.
  - Plugins need the shared engine libraries (the `dev` presets). In a static build `plugin_load` answers
    `plugins_unsupported`.
- **The spike's three questions, answered** ([spike report](../reports/2026-11-01-arch-022-plugin-host-spike.md)):
  - C++23 modules across the boundary with clang work with the shared engine. The plugin links the engine
    libraries like any target, CMake carries their module sets, and every symbol (ImGui and ImPlot included)
    binds to the one copy the Sandbox loaded. The static engine needed a 5.4 s collection of the host's
    module files per build; that arrangement is removed.
  - Registration needs four registries with unregistration (config sections, operations, windows, history
    commands), an atomic commit, and a host that holds every plugin callable. The ImGui context is the host's.
  - State that survives a reload: the scene, the config (saved payloads, validated again), the undo history
    and host state. Undo entries hold data and call a registered command, so they find the new plugin's code;
    an entry that holds the plugin's closures crashes after the unload (the S2b control). Jobs still running
    at an unload are cancelled and reaped, and GPU work retired, before the old image goes.

## Jobs, preview and observation (operator, 2026-10-30)

Every algorithm a plugin offers runs as a job; the main thread never waits for it.

**One way to run.**
- The inputs are snapshotted on the main thread and computed on a worker or the GPU. The result is
  committed on the main thread only if the inputs are still current, as one undo step.
- There is no second inline path with other semantics; the scheduler may run a tiny job within
  the same frame.
- **Preview and commit are separate.** While a slider moves, only previews run, into a display
  buffer without undo entries; releasing it or Apply commits once.
- **The latest wins.** A newer preview of the same command on the same output supersedes the older
  one (cancelled, or its result dropped) instead of being refused.
- **Coarse while dragging, full on release**, where an algorithm offers a coarse level.

**Step functions, wherever possible.** An algorithm is a state machine:
- `Init` (the state owns all buffers);
- `Step` (one bounded iteration);
- `Metrics` (a few numbers);
- `Preview` (what the viewer shows);
- `Finish`.

The job service owns the loop and checks between steps for stop, pause and single step; then it
takes metrics and preview on the same worker thread. Every algorithm therefore gets these, without
code of its own:
- pause;
- single step (n steps);
- stop and accept the current state;
- cancel.

A run without UI is the same loop. A non-iterative algorithm steps in chunks, or in one step. Where
an external solver owns the inner loop, the step boundary is the outer loop.

**Resident state.**
- After `Init`, the state a `Step` needs already exists where the step runs. A GPU algorithm keeps
  its inputs and working buffers resident; `Step` only records and dispatches work.
- Between steps there is no full-array transfer between CPU and GPU. Only a few bytes of metrics are
  read back, asynchronously, so convergence may be noticed one step late.
- A step that cannot avoid a CPU stage moves only small data (for example a matrix) and is marked as
  hybrid. The preview draws from the GPU buffers directly.
- Data crosses between CPU and GPU only at the start and at the end (the backlog task GRAPHICS-153
  has the same goal). GPU k-means already uploads once and previews at an interval.

**Lifetime and the buffer cache.**
- A run's buffers live until **Accept**, **Reset** or **accept on convergence** (an option per
  algorithm).
- Then they are parked in the host's buffer cache, keyed by size class and usage, instead of being
  destroyed.
- When the cache needs room for its memory budget, the least recently used parked buffers are
  destroyed first. Buffers of an active run are pinned and never evicted; if the budget cannot be
  met, a new run is refused with a reason.
- A GPU buffer is reused only after its fence has signalled.
- The cache holds raw buffers, never types from a plugin, so it survives a plugin reload.

**Observation.** Each algorithm has a **live preview** checkbox, on by default for iterative ones.
Off means no intermediate copies at all; the result is identical.
- **The hand-over** from the computing thread to its readers is a lock-free **triple buffer**. The
  writer never waits, and a reader can read at any time and always gets the newest complete state.
  The worker copies only when the last state was taken, and at most about 30 times per second.
- **Metrics** per step go to a preallocated log sized to the iteration cap (`Core.AppendLog`), kept in full,
  so the curves have no gaps.
- **The UI:**
  - the preview in the viewport, drawn from the buffers;
  - metric curves in the panel;
  - stop, pause and single-step buttons.

  The services provide these; no panel rebuilds them.
- **Agents get the same over MCP:**
  - a status with the phase, the iteration, the latest metrics and their history;
  - pause, n steps, resume, stop and accept, and cancel;
  - waiting until a condition holds (an iteration, a metric threshold).

**Current state (ARCH-022 S3b-1).** `Extrinsic.Runtime.StepRuns` (`src/runtime/Services/Runtime.StepRuns.cppm`)
is the run driver: the owner gives `Step` (a worker, one iteration), `Preview`, `Show`, `Valid`, `Commit` and
`Discard`, and its job submission (a plugin passes `PluginStepRunJobs()`). A run is carried by the owner's jobs: a
holder job that stays parked by its readiness gate until the run ends, so a paused run keeps its output, has its Jobs
row and is cancelled by the Jobs window and an unload drain, and whose gate is the run's main-thread pump; and segment
jobs joined to it, the worker loop that checks the controls between `Step` calls. The transition table is at the top
of the module. MCP: `run_control` (run, pause, step n, stop, accept, discard) and `run_observe`; waiting for a
condition is not there yet. Its contract tests are `tests/contract/runtime/Test.StepRuns.cpp`.
The CPU case is the plugin `plugins/coherent_point_drift`: coherent point drift (the CPU reference solver, rigid)
moves a source cloud onto a target one step per `Step` in world space (each unparented cloud mapped by its local
Transform, captured with `CaptureTransform`; the source's result mapped back by the full affine inverse), shows the
moving source through the point-cloud editing service's `PreviewPositions` (a CPU-made front of the positions'
residency ring, which the renderer draws; released by `DiscardPreview` and before `Publish`), and Accept publishes the
positions with one undo entry. It runs beside the editor's built-in CPD until that feature migrates. The editing
service's `Current` and publications also require the capture's world to be the active one and, with
`CaptureTransform`, the Transform unchanged and the entity unparented; an empty normal name takes the positions alone.
Tests: `tests/contract/runtime/Test.CoherentPointDriftPlugin.cpp` and the Vulkan smoke
`tests/integration/graphics/Test.CoherentPointDriftPluginGpuSmoke.cpp`.

**Scope (operator, 2026-10-30):** this applies to **every GPU algorithm**, and to every iterative
algorithm. Existing ones are brought into this form (inventoried in `ARCH-022`'s plan); new ones are
built this way from the start ([AGENTS.md](../../AGENTS.md) §6). Until the job service offers the
loop, a new algorithm is still written as `Init`/`Step`/`Finish` with resident state, and the
existing job path drives it.

**Tests every plugin gets cheaply:**
- a run equals its step sequence;
- preview on equals preview off;
- pause and resume equals an uninterrupted run;
- cancel leaves the scene unchanged;
- no data transfer per step after `Init`, apart from the metrics.

**Still proposed, not decided:** no central framework that knows the *shapes* of operations (as ADR
0031 built), and no generator or golden that every new field must pass through.

## Rules, measured

| rule | measure | today (2026-10-30) | target |
|---|---|---|---|
| **R1** The foundation and the services name no feature. | files under `src/runtime` and `src/app`, outside plugin folders, that name two or more of the `run_operation` tool's operations (snake_case, or PascalCase inside identifiers); the algorithm libraries `src/geometry` and `src/graphics` are counted apart. Enforced by `tools/repo/check_feature_boundaries.py` (ratchet in `ci-docs`) | 31 files: 22 in `src/runtime` (18 of them in the editor), 9 in `src/app`; 13 name five or more (below) | 0 |
| **R2** A plugin imports only the services and the foundation. | per plugin folder: the distinct modules it imports from the editor (`src/runtime/Editor/`) and from other plugin folders. Same checker and ratchet | editor modules: Clustering 5, OutlierAnalysis 5, DensityWeight, KernelDensity, PointSpacing and PointCloudConsolidation 4 each, TextureBake 1 (27 in all); other plugins: PointCloudConsolidation 2, Registration 1 | 0 |
| **R3** Adding a method is a new plugin folder; removing it is deleting the folder. | files outside the folder touched to add an operation, tests included | 9 to 13 files | 0 |
| **R4** A plugin change reaches the running Sandbox without a restart. | time from saving an edit to the reloaded plugin running, for a typical plugin | fixture baseline (one-file plugin, `dev` Debug with sanitizers, lld, warm loop): shared engine libraries 4.49 s median (max 4.50; compile 3.9 s, reload 0.2 s); static engine 9.74 s median (max 9.85), of which 5.4 s is the fixture's host-module collection; both under 10 s ([R4 baseline](../reports/2026-11-01-arch-022-r4-baseline.md)). A typical plugin is not measured yet | under 10 s |
| **R5** Everything the editor can do, an agent can do over MCP. | (A) editor actions no MCP tool reaches; (B) state-changing editor entry points without an MCP tool; by static reachability, counting rule in the [inventories](../reports/2026-10-30-arch-022-inventories.md) | (A) 62 of 193 action rows not reached, 28 partly (55 of the 62 change scene, config, selection or history); (B) 45 of 111 entry points not reached (42 not at all, 3 partly); the 48 MCP tools come from a hand-written list | 0 and 0 |
| **R6** Build and CI are fast. | incremental rebuild after editing one plugin; CI wall time | Sandbox rebuild after one edit (`dev`, Ninja default jobs, one host): operation unit 19.1 s, plugin config interface 48.6 s, geometry interface 18.7 s, each including a 12.8 s Sandbox link; no-op 0.14 s; clean build 2.0 min plus 8.2 s configure. CI `ci-linux-clang` median 41.1 min (20 green runs, 21 to 71; the job alone 38.2 min, of which 32.1 min is a cold build) ([baseline](../reports/2026-10-30-arch-022-r6-baseline.md), [CI breakdown](../reports/2026-10-31-arch-022-ci-breakdown.md)). With lld instead of GNU ld (adopted for the `dev` presets once lld is installed; CI unchanged): plugin operation unit 7.1 s, plugin config interface 36.7 s, core geometry interface 6.7 s, the link 0.84 s ([linker comparison](../reports/2026-10-31-arch-022-linker-comparison.md)) | Sandbox rebuilt after one plugin edit in under 10 s; CI median under 15 min, measured as the `full-cpu` job on a push to `main` (waiting for the previous run is reported separately) |
| **R7** Algorithms stay interactive and observable. | longest main-thread stall caused by an algorithm run; CPU-GPU bytes per step after `Init` (metrics apart); algorithms with step functions | 53 table rows (21 GPU, 32 CPU): 11 with a step API (8 driven by the runtime), 4 GPU paths resident between steps, 4 moving full arrays per iteration; 6 CPU algorithms run synchronously in their command ([inventories](../reports/2026-10-30-arch-022-inventories.md)); stall of GPU k-means under a slider (Release, one host, render refresh after the accept not measured): 5.1 ms at 100 000 points, 62 ms at 1 048 576, the longest being the accept's publication; almost every slider frame pays a full capture and start, median 1.4 ms and 16.5 ms ([R7 k-means baseline](../reports/2026-11-03-arch-022-r7-kmeans-stall.md)); after Q75 (2026-11-04): the accept's publication 2.1 ms and 31 ms, a start 1.0 ms and 10.6–10.8 ms (process medians); the frozen verdict is still not met at both sizes, the maxima set by single untraced stalls in the platform phase or present; without those frames (exploratory) 2.9 ms at 100 000 points (met) and 32 ms at 1 048 576 (the accept) ([Q75 before/after](../reports/2026-11-04-arch-022-r7-kmeans-q75.md)); after steps 6 and 7 (2026-11-04): a replacing request's start 0.06 ms at both sizes, hold frames under 0.6 ms (the renderer's execute under 0.3 ms); the frozen verdict met at 100 000 points (3.0 ms, the cold first start) and not met at 1 048 576 (the accept's frame 32.6–32.7 ms with its publication 31.8–31.9 ms, the frame before it 16.6–16.8 ms with transfer collection, as before these changes, the cold first start 21.3–21.5 ms) ([Q79 before/after](../reports/2026-11-04-arch-022-r7-kmeans-q79.md)); other algorithms not measured | under 4 ms; 0 bytes; every iterative algorithm |

The operator set the targets of R4, R6 and R7 on 2026-10-31, after the R6 baseline (`ARCH-022`, Q46).

## Inventory (measured 2026-10-30)

**Where the code is** (lines of C++, tests apart):

| area | lines | note |
|---|---:|---|
| `src/geometry` | 88,566 | algorithm library: this is where the methods are |
| `src/graphics` | 75,050 | renderer, GPU kernels |
| `src/runtime/Editor` | 51,014 | 39,635 of it in `Editor/Operations`: the operations' glue |
| `src/runtime/Modules` | 21,314 | 20 folders. Four hold an operation (the ADR 0031 migrations); 9 hold only a config interface; the rest are services (clustering, consolidation, texture bake, physics, GPU backends). All are built into one runtime library listed in `src/runtime/CMakeLists.txt` |
| `src/app` (Sandbox) | 19,816 | all processing panels in two files: `Sandbox.MeshProcessingPanels.cpp` 5,478 and `Sandbox.MethodPanels.cpp` 3,987 |
| `src/runtime/Kernel` | 5,582 | the slim kernel (ADR 0024/0027, enforced by its convergence checker) |

A survey on 2026-10-28 found about 520 lines of real algorithm code in the editor. The rest is glue:
- execution 24 %;
- undo and commit 19 %;
- validation and config 18 %;
- kernel adaptation 14 %;
- view models 13 %.

**Hub files that name five or more features** (the 13 largest of R1's 31). Every method is entered
in each of these:

| file | features named | lines |
|---|---:|---:|
| `Agent/Runtime.AgentOperations.Operations.cpp` | 22 | 1,176 |
| `app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp` | 21 | 5,478 |
| `app/Sandbox/Sandbox.ConfigSections.cpp` | 16 | 73 |
| `runtime/CMakeLists.txt` | 15 | 363 |
| `Editor/internal/Runtime.EditorWorkspaceSession.cpp` | 11 | 1,113 |
| `Editor/Operations/Runtime.GeometryProcessingOperations.{Mesh.cpp,cppm}` | 8 | 727 |
| `Editor/internal/Runtime.EditorFeatureContextAdapters.cpp`, `Runtime.EditorFeatures.Internal.hpp` | 7 | 2,410 |
| `Editor/Runtime.EditorProcessing.cppm`, `Editor/Operations/Runtime.MeshFieldOperations.cppm` | 6 | 631 |
| `Editor/Operations/Runtime.MeshTopologyOperations.{Topology.cpp,cppm}` | 5 | 3,604 |

**What a module imports** (R2): distinct named modules imported from outside the folder (its own
partitions not counted).

| module | lines | imports | of them runtime |
|---|---:|---:|---:|
| PointCloudConsolidation | 5,750 | 50 | 19 |
| TextureBake | 4,489 | 42 | 14 |
| Clustering | 2,774 | 38 | 17 |
| OutlierAnalysis | 1,246 | 25 | 13 |
| PhysicsIntegration | 1,442 | 21 | 8 |
| DensityWeight, KernelDensity, PointSpacing | 333-355 | 14-15 | 7-8 |
| ProgressivePoisson | 2,186 | 12 | 1 |

## How we got here (short)

- The kernel work (ADR 0024/0027) succeeded and is enforced.
- The methods never got a boundary: each was added as editor operations and panels and entered
  itself in the hub files.
- Later programs measured *files per added field* and *duplicated glue*, and answered with central
  machinery: tables, generators, then ADR 0031's operation runtime. That made the centre larger
  instead of making the features independent.
- Keeping every message and quirk byte-identical meant the machinery had to absorb every special
  case.

## Decisions and open points

**Decided by the operator (2026-10-30):**
- This page is the only architecture target, and the old targets are archived.
- ADR 0031 is superseded.
- Every module registers itself.
- Plugins are compiled and reloaded at run time, so agents can change code while the Sandbox keeps
  running.
- Agents control the engine completely over MCP.
- Rebuilding things for these goals is allowed.

**Decided by the operator (2026-11-01, `ARCH-022` Q51 and Q52), after the hot-reload spike:**
- Dev builds link the engine as shared libraries. Plugins then build like any target. The measured one-file
  fixture plugin ran its edit in 4.5 s ([R4 baseline](../reports/2026-11-01-arch-022-r4-baseline.md)); that is not
  a guarantee for every plugin. CI and release stay static for now.
- The order of the next steps:
  1. the plugin host as a module service;
  2. a first real feature as a plugin, through every layer;
  3. the features one by one;
  4. the job form (S3) before the GPU-heavy features.
- lld in CI: its link share is measured the next time CI changes anyway.

**State (2026-11-02):** step 1 is done: the plugin host is a module service. Step 2's feature is the bilateral point
filter ([plugin](bilateral-point-filter.md), `plugins/bilateral_filter`): it owns its config section, its MCP tools
and its window, and runs as plugin jobs of the editor's Jobs window. What it needs from the editor (capture,
indices, GPU kNN pages, publication with undo) is the module service `PointCloudEditingService`, which later point
plugins reuse. The Sandbox loads it at startup (static builds link it). The domain Processing menus' entries for it
are gone: they were Sandbox redirects, which a plugin cannot register. The second plugin is the FPFH descriptor
analysis ([feature](descriptor-analysis.md), `plugins/descriptor_analysis`), loaded beside it: the editing service
learned radius rows and the publication of several scalar columns as one undo entry; what both plugins need moved
into the services ring (`PluginService`, `Extrinsic.Runtime.PluginSections`), and their tests share one plugin
harness (`tests/support/PluginHarness.hpp`).

**Open, for the first plan** (`ARCH-022`):
1. **Numeric targets for R4 and R6**: set on 2026-10-31 (table above).
2. **Order of steps.** The proposal starts with the riskiest assumption:
   1. **Baselines and a checker:** the incremental rebuild per module, CI time by stage, and MCP
      coverage; a CI checker for R1 and R2, with today's numbers as a ceiling that may only fall.
   2. **A spike:** one small feature module, built as a shared library, is loaded into the running
      Sandbox, edited, rebuilt and reloaded without a restart, with a command reachable over MCP.
      It answers three questions:
      - whether C++23 modules work across that boundary with clang;
      - what registration and unregistration need;
      - which state survives a reload.
   3. **Registries for self-registration** (config sections, windows, commands and their MCP
      tools), removing the hub files.
   4. **Features move out of the editor into plugins**, one at a time.
3. **Which process hosts the plugins:** the engine runtime (the plugin host module above), wired by the
   Sandbox (slice 3 step 1).
