---
id: ARCH-022
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: program tracker; evidence lives in each slice task and the target's measured rules
contract_schema: 1
contracts: []
contract_review: tracker only; each slice declares its own contracts
---
# ARCH-022 — Engine and feature modules: the architecture target

## Operator questions (verbatim, 2026-10-30)
34. "Ok ich bin etwas verwirrt… Ich habe das Gefühl dass wir seit ewigkeiten an der Architektur
    herumschrauben. Eigentlich hatte ich immer versucht IntrinsicEngine so zu bauen dass Änderungen
    leicht sind, die Architektur modular und die Abhängigkeiten gering. Nun scheint es so dass alles
    wieder total kompliziert zugewuchert ist? Die Engine soll eigentlich das Fundament bilden und
    Module/Plugins fügen Features und Methoden hinzu. Ich weiß nicht an welcher Stelle wir falsch
    abgebogen sind."
35. "Ich will 2. machen. Kannst du hier etwas vorschlagen und ich schaue mir das dann an?" (point 2:
    a one-page target picture and an inventory, no code)
36. "Also ich hätte gerne alle alten zieldokumente archiviert. Dann nehmen wir nur dieses hier als
    aktuelles und das wird erst ersetzt wenn die Ziele erfüllt sind. Ja, jedes Modul/Plugin meldet
    sich selbst an. Ich will auch Laufzeit Kompilierung damit Agenten etwas ändern kannst du die
    sandbox/engine weiterlaufen kann. Außerdem sollen Agenten die Engine komplett per mcp steuern
    können. Wenn wir für dieses Ziel Dinge umbauen müssen dann ist das ok. Am Ende zählt das
    Änderungen günstig sind, die Engine modular und erweiterbar ist und dass der Build und ci nicht
    so lange dauert."
37. "Bitte räum erst auf. Danach erklärst du mir wie die Plugins genau aussehen sollten und wir
    müssen irgendwie regeln dass zb. Plugin Features Wiederverwendung werden. Sagen wir wir bauen
    marching cubes als Plugin. Nach einer Weile brauchen aber vllt andere Plugins darauf Zugriff.
    Dann sollte marching cubes ein Feature Modul werden welches zum Engine kern gehört damit es von
    anderen Plugins verwendet werden kann. Plugins sollten auch für ihre eigene ui zuständig sein
    und diese registrieren… oder?"
38. "Passt 1-3. evidence gerne löschen" (1: the three kinds of code, promotion instead of plugin
    dependencies, and "the UI only calls commands" go into the target; 2: the 13 older active tasks
    move to the backlog; 3: the reviewed evidence folders are deleted)
39. "Macht es Sinn alle Algorithmen die über die ui ausgeführt werden (per Button Click) als
    asynchrone tasks auszuführen? Ansonsten hätte ich lieber interaktivität falls sich das irgendwie
    mit zb slidern beißt… Wie macht man das besonders gut?"
40. "Ok das klingt ziemlich gut. Außerdem hätte ich bei bestimmten Algorithmen, zb Registrierung oder
    iterative Optimierung/fitting gerne interaktive Updates, so dass man live verfolgen kann was
    passiert (observability). Ich habe gemerkt dass ich so besonders schnell Fehler in den
    Algorithmen finden kann und dass das auch die Wartezeit verkürzt. Wir könnten das als preview
    checkbox bei den ui Algorithmen einbauen dass man das auch ausstellen kann?"
41. "- eine step Funktion sollte es überall, wo möglich, geben - für die Beobachtung der Algorithmen
    würde ich gerne eine Art „Double buffering" verwenden, sodass diese lockless funktionieren und
    ich zu jeder Zeit lesen kann ohne die Berechnung zu stören. Oder ist das keine gute Idee und
    wiederspricht dem „Briefkasten"? - wenn ein Algorithmus fertig ist können die temporären buffer
    zerstört werden oder in einen reuse cache geparkt werden damit diese nicht neu instanziert werden
    müssen und recycelt werden können. - ja Agenten sollten das ebenfalls bekommen. Wie macht man das
    alles „gut"?"
42. "Klingt gut, mir ist nur wichtig dass wenn step aufgerufen wird (außer bei der ersten iteration)
    der „gpu-State" den der Algorithmus braucht bereits existiert. Also dass nicht zwischen drin immer
    wieder teuer gpu io oder so passieren muss (außer es geht garnicht anders) damit step einfach nur
    ausführt und sonst quasi nix leistet. Als Beispiel hier bei kmeans, sollen die gpu bidder on die
    die Zwischenergebnisse und Ergebnisse geschrieben werden so lange existieren bis „accept oder
    reset" geklickt wird oder per Konvergenz akzeptiert wird oder der buffer cache aus budget Gründen
    wieder Platz braucht. Dann sollten die am längsten ungenutzten gecacheten buffer wieder zerstört
    werden"
43. "Ja bitte. Das soll aber für alle gpu Algorithmen gelten und neue sollen auch genau so direkt
    implementiert werden." (yes to the first plan; the job form applies to every GPU algorithm, and
    new ones are built that way from the start)
44. "Ja bitte, slice 1"
45. "ok then Option 1" (the R6 timing baselines measure the workload the plan names: build tree on
    disk, one clean build, real edits, default parallelism, ccache as configured)
46. (2026-10-31) "Ja bitte dann mach 1 und 2. danach gehen wir 3. an" (yes to the proposed targets:
    R4 under 10 s, R6 Sandbox rebuilt after one plugin edit in under 10 s and CI median under 15 min,
    R7 under 4 ms; next a CI time breakdown (1), then a measured linker comparison (2), then slice 2
    (3))
47. (2026-10-31) "Du darfst lld installieren. Ich bin gerade nicht an dem Rechner."
48. (2026-10-31) "Für 2. würde ich auch die Konzert nehmen" (read as "Jobzeit", agreeing with the
    recommendation: the CI target means the `full-cpu` job time; this reading was stated back to
    the operator)
49. (2026-10-31) "Lld übernehmen ja, installieren kann ich das erst am Montag oder so. Was machen
    wir mit den 3 Befunden? Können wir den 1. und 2. Punkt irgendwie korrigieren? 3. lässt sich nix
    machen oder?"
50. (2026-10-31) "Ja bitte, mach 1. - 5." (1: the NVIDIA leak in the spike driver, by preload and a
    narrow suppression; 2: prepare lld as the dev linker; 3: late config registration through
    `RegisterSection`/`UnregisterSection`; 4: the window types in a contract module outside the
    editor; 5: S2)

## Goal
- Reach the target in
  [`docs/architecture/engine-and-feature-modules.md`](../../docs/architecture/engine-and-feature-modules.md):
  a foundation, a small set of module services, and self-registering feature plugins that are
  rebuilt and reloaded while the Sandbox runs and are fully controllable over MCP, with interactive
  and observable algorithms. Measured by its rules R1–R7. This note says where the work is and what comes next; each slice has its own task.

## State
- 2026-10-30: the target was written and accepted by the operator.
  - All earlier target documents are archived (`docs/archive/README.md`).
  - ADR 0031 is superseded; the moonshot program (`ARCH-020`) is closed by this decision.
- **Next:** the first plan, for the operator's review. It fixes the order of steps (the target's
  open points) and the numeric targets for R4 and R6 once their baselines are measured. The
  proposed first slices are:
  1. baselines plus an R1/R2 checker;
  2. the hot-reload spike;
  3. registries;
  4. moving the features out of the editor.

## Plan (first plan, 2026-10-30; revised after Codex plan review `plan022`)

Serves Q36 (the target's goals R1–R6: self-registering plugins, run-time compilation, full MCP
control, short build and CI) and Q39–Q43 (jobs, observation, resident GPU state, all GPU
algorithms; R7).

**Why this order is the shortest path.**
- Measuring first is cheap, and a ratchet locks in the direction from the first commit.
- The riskiest assumption comes next, before any large rebuild: whether C++23 plugins can be built
  as shared libraries and reloaded inside the running Sandbox. Its answer shapes the services, the
  registries and every migration after it.
- Today all engine libraries are static (`intrinsic_add_module_library` → `add_library` without a
  type), the whole runtime is one library (`ExtrinsicRuntime`), and the undo history stores
  `std::function` callbacks (`Runtime.EditorCommandHistory.cppm:76`).
- After the spike, one real feature goes through every layer before anything is migrated broadly.

Each slice runs plan → Codex plan review → implementation with red-before tests and mutations →
Codex code review and ponytail → commit → CI. After each slice I stop and report (drift budget).
Results-bearing measurements (timings) get the **full** experiment pre-flight of `AGENTS.md`
"Experiments": two independent reviews of a frozen packet, generated HTML reports. Decisions rest on
them: the operator's numeric targets and the choice of link variant. Deterministic checks are tests,
not runs.

**Slice 1: the boundary ratchet and the baselines.**
- **The checker** `tools/repo/check_feature_boundaries.py`, with an executable counting contract:
  - **Operation names:** the `enum` of `properties.operation` of the `run_operation` tool in
    `tests/data/agent_declared_tool_schemas.json`. A missing, empty or malformed enum is an error,
    never a pass. Each name matches as snake_case or as its PascalCase form, on whole-word
    boundaries.
  - **R1 scope:** files with suffix `.cpp`, `.cppm`, `.hpp`, `.h` and `CMakeLists.txt` under
    `src/runtime` and `src/app`, excluding plugin folders (`src/runtime/Modules/<name>/`, later
    `plugins/<name>/`). A file counts when it names two or more operations.
  - **R2 scope:** per plugin folder, the distinct modules it imports, other than its own and its
    partitions, from two classes:
    - *editor internals:* modules whose interface unit lives under `src/runtime/Editor/`;
    - *other plugins:* modules whose interface unit lives in another plugin folder.

    Module names are resolved to folders by scanning `export module` declarations.
  - **Baseline** `tools/repo/feature_boundaries_baseline.json`: the R1 count and each folder's R2
    counts. Strict mode fails when:
    - a value rises above its ceiling;
    - a new folder appears without an entry (new plugins start at 0);
    - an entry is stale (its folder is gone);
    - a value falls below its ceiling while the ceiling is not lowered in the same change;
    - a ceiling is higher than in the last accepted baseline. As built (after the code review
      `review022s1`), the last accepted baseline is the **lowest value of each ceiling in the
      baseline file's Git history**. A key absent from a version counts as 0 there, so a new
      plugin starts at 0. Because the ratchet only falls, that lowest value is the last legitimate
      one. A raise in an earlier commit of the same push, or one that landed with a failed push,
      therefore cannot become the new baseline. A missing or shallow history fails with a reason.
      This replaces the comparison with one change-base revision of the first plan.
  - Wired into `ci-docs` (static Python).
  - Tests in `tests/regression/tooling/`, written before the checker, on a fixture tree: an added
    hub file fails R1; an editor import and a cross-plugin import each fail R2; a new folder without
    an entry fails; a stale entry fails; a fall without a lowered ceiling fails; a ceiling raised
    above the previous baseline fails, even when the count rose with it, also when one commit of a
    two-commit push raises it and the second leaves it unchanged; a missing comparison revision
    fails; a malformed or empty enum
    fails. Mutations, each red: each of these checks removed in turn, including the comparison with
    the previous baseline.
- **Inventories** (deterministic, read from the code; no run):
  - **R5:** both categories, with the method stated. *Editor actions that are not commands:*
    action-button sites, shortcuts and gestures that change state outside the command path.
    *Commands without an MCP tool.*
  - **R7:** one row per GPU or iterative algorithm and per backend, with:
    - step form (yes or no);
    - resident state;
    - the bytes moved per iteration, as a formula in the input sizes;
    - buffer lifetime;
    - preview interval.

    Unknowns are stated, not guessed. This is the migration list Q43 asks for.
- **Timing baselines** (full pre-flight, frozen before any measurement):
  - **R6, incremental rebuild:**
    - preset `dev` (it builds the Sandbox; `ci` does not), the existing `build/dev` cache, `ccache`
      as configured;
    - the parallelism of the machine's default;
    - three named files with one fixed, compile-relevant edit each (an operation unit, a config
      interface, a geometry interface), plus a no-op rebuild as control;
    - five repetitions each, alternating;
    - built on the checks of `tools/analysis/benchmark_compile_iteration.py`.
  - **R6, clean build:** one clean build of `dev` (wall time and peak memory).
  - **R6, CI:** wall time from workflow start to end of the last 20 green `ci-linux-clang` runs on
    `main` (not stage sums), as a stated cohort.
- **Output:** a baseline report generated from the raw outputs; the target's "today" column; the
  operator sets the numeric targets of R4, R6 and R7.

**Slice 2: the plugin-host spike.**
- **Containment:** behind the CMake option `INTRINSIC_PLUGIN_SPIKE` (off by default), code under
  `plugins/spike/`. A test asserts that with the option off, no spike target or test is configured.
  Not in CI until the operator decides.
- **Pass checks:** each is written as an assertion before the spike runs.
- **S1, the boundary.** A spike plugin is built as a separate target after the host, from the host's
  existing module BMIs, with the same compiler and flags and **without rebuilding the host**. It is
  loaded into the running Sandbox and registers a config section, a command and a window. Pass:
  - every symbol resolves at load time;
  - shared state has **one identity** across the boundary (the address of a host registry and of
    a static seen from host and plugin are equal, so no duplicated engine state);
  - static initialization and destruction run once, in a defined order;
  - the ImGui context and allocator are the host's;
  - the command is callable over MCP;
  - the window draws.

  Both link variants are tried **on this one fixture**, without converting the whole engine:
  (a) the static engine with an exporting executable (`-rdynamic` or an export list), (b) the
  libraries the fixture needs, built as shared. They are compared on build time, binary size and
  symbol problems.
- **S2, reload without a restart.** The plugin is edited and only its target rebuilt. The host is
  triggered by an MCP tool or a file watcher.
  - **The reload sequence:**
    1. draining is bounded: queued jobs are cancelled; running jobs finish or are cancelled within a
       timeout; pending completion callbacks and GPU fences complete;
    2. the plugin is unregistered and unloaded;
    3. the new build is loaded and registered.
  - **Pass:**
    - scene and config survive;
    - an undo entry **and a redo entry** made through the plugin before the reload still work after
      it (today's `std::function` history must not call unloaded code, so this is expected to fail
      until history entries hold data or a registered command);
    - a failed build, a failed load and a failed `Register` leave the old plugin running, or none,
      with a reason and no partial registration;
    - the registries hold no stale entry;
    - 20 reload cycles run clean with the host and the plugin both ASan-instrumented;
    - the time from saving to the reloaded plugin running is measured (the R4 baseline, under the
      full pre-flight).
- **S3, the job form on CPU and GPU.** Inputs and backend are fixed before bitwise comparisons.
  - **CPU case:** CPD's CPU backend (it already has `Step()`) through
    `Init`/`Step`/`Metrics`/`Preview`/`Finish`, driven by a minimal loop with a lock-free triple
    buffer and a metric ring. Pause, single step and stop work from the UI and over MCP.
  - **GPU case:** GPU k-means with its resident buffers. Steps move no full arrays; the run's
    buffers live until Accept, Reset or accept on convergence; observation is fence-safe.
  - **Assertions:**
    - a run equals its step sequence, bit for bit;
    - preview on equals preview off;
    - pause and resume equals an uninterrupted run;
    - latest wins: a newer preview supersedes an older one, and the older result is never shown
      after the newer;
    - cancel commits nothing;
    - previews commit nothing, also when they complete;
    - Accept or Apply commits exactly once;
    - a converged run commits only when accept on convergence is enabled, and otherwise waits for
      Accept or Reset;
    - a stalled reader never sees a torn snapshot and never blocks the writer (a separate TSan
      harness, with progress asserted, not only cleanliness);
    - the GPU case moves zero full-array bytes per step after `Init`;
    - the main-thread stall during a slider preview is measured against the proposed 4 ms.
- **Done:** a spike report with yes or no per question, the numbers, and a recommended plugin
  boundary, link variant and history-entry form, for the operator to decide. The spike code then
  either becomes the start of slice 3 or is deleted.

**Slice 2, S1 implementation plan (revision 4, 2026-10-31, after Codex plan review `plan022s1`).**
Serves Q36 (plugins that register themselves, run-time compilation, full MCP) through R3, R4 and R5.
Smallest end-to-end slice: one plugin loaded once into the running Sandbox. Reload (S2) and the job form
(S3) come after.
- **Facts it rests on** (read 2026-10-31):
  - Everything links statically; there is no shared library, `-fPIC`, `-rdynamic` or dlopen code.
  - Registration is by hand-written lists. `EditorUiHost::RegisterWindow` / `UnregisterWindow`
    can unregister; the config-section registry and `AgentOperationRegistry` cannot. The
    agent registry is exposed read-only, and `Register` grows the vector whose `std::function`s
    dispatch runs.
  - ImGui and ImPlot are static archives inside the executable.
  - A toy check (clang 23, ASan and UBSan) showed: a `-fPIC` shared object built against non-PIC module
    BMIs loads into a `-rdynamic` host, and a host function-local static and an inline-function
    static have one address on both sides. This does not show that CMake can do the same.
- **Toolchain and ABI, fixed for the spike.**
  - CMake 4.3.2, Ninja, `/bin/clang++-23` with libstdc++, preset `dev`. The plugin gets the host's
    effective compile and link flags: `-fno-rtti`, `-fno-exceptions`, `-fsanitize=address,undefined`.
  - The entry point is `extern "C"` and `noexcept`; nothing throws across it.
  - The host executable supplies the sanitizer runtimes; the plugin links none.
  - UBSan's vptr check is off with `-fno-rtti`, so UBSan cleanliness says nothing about vptr
    correctness.
- **Step 1, containment.**
  - `option(INTRINSIC_PLUGIN_SPIKE OFF)`; with it on, `plugins/spike/` is added and `ExtrinsicSandbox`
    gets `ENABLE_EXPORTS` (variant (a)).
  - Check: in a configured build with the option off (`build/dev`), `ninja -t targets all` lists no
    spike target and `ctest -N` no spike test. This runs in the spike driver; it is not in CI.
- **Step 2, the plugin target, the build recipe first.**
  - `IntrinsicSpikePlugin` is a `MODULE` library with `POSITION_INDEPENDENT_CODE`, linking no engine
    archive and no ImGui.
  - Its first question is whether `$<COMPILE_ONLY:ExtrinsicSandboxEditor>` carries the module sets
    (`CXX_MODULE_SETS`) so the plugin compiles against the host's BMIs. `COMPILE_ONLY` promises
    compile requirements, not BMI reuse, so this is tested, not assumed.
  - Named fallback: module scanning off for the plugin, with explicit `-fmodule-file=` flags taken
    from the host's `.modmap` files.
  - Checks:
    - building only the plugin after the host runs only plugin build actions (Ninja's log window);
    - host outputs and BMIs are byte-identical before and after;
    - `nm` of the `.so` defines only names on a narrow allowlist: the plugin's own names plus the
      vague-linkage instantiations listed by name.
- **Step 3, the host side.** Spike-only, compiled only with the option.
  - `Sandbox.PluginSpike.cpp` loads with `dlopen(RTLD_NOW | RTLD_LOCAL)` and resolves
    `intrinsic_plugin_register_v0`.
  - It passes a plain `PluginHostContext`: `EditorUiHost` (`RegisterWindow`) for windows, the config-section registry,
    mutable access to the `AgentOperationRegistry` (added under the option), and the ImGui context.
  - Registration is staged. The plugin fills a staging list; the host commits it after the MCP
    dispatch that asked for the load has returned (on the next frame). The vector holding the
    running callback therefore never grows during that call.
  - The commit is atomic by construction:
    1. the original config-section and agent-operation registries are kept;
    2. the staged entries are added to copies of both, and the copies are validated;
    3. the staged windows are registered, and each window that registered is remembered;
    4. the two copies replace their registries.

    Any failure in steps 2 to 4, including an injected one between the two replacements, restores
    both registries from the kept originals and unregisters every window remembered in step 3. A
    window whose registration failed has no handle and needs none. The originals are dropped only
    after step 4 succeeded.
  - Every failure (`dlopen`, `dlsym`, `Register`, a commit stage) returns a reason, with the
    registries unchanged. A spike-only switch injects a failure after each stage, and each case is
    asserted.
  - The library stays loaded until the process exits. In S1 nothing calls `dlclose`, so no stored
    callback can outlive its code.
  - MCP tools: `plugin.load` (deferred as above), `plugin.host_identity`, and `app.quit` (for a
    graceful exit), if none exists.
- **Step 4, the plugin.** It registers:
  - a config section `spike`, after boot;
  - MCP tools `spike.echo` and `spike.identity`;
  - a window `view.spike` that draws a fixed-colour rectangle as a pixel marker.

  Identity is shown by state, not only by pointers. Through imported host code the plugin mutates:
  - a host counter;
  - a host inline static;
  - a host `thread_local`, on the main thread.

  The host then reads the values back. The plugin also reports, and the driver compares with the
  host:
  - its ImGui context;
  - its ImGui allocator functions and user data;
  - its ImPlot context.

  Lifecycle: an `extern "C"` recorder exported by the executable, `spike_lifecycle_event(const char*)`,
  is usable before `dlopen` and through process teardown, because it holds no host state. Each call
  opens the driver-owned log file (path from an environment variable), appends one line and closes it.
  The plugin records, in order:
  1. its static construction (during `dlopen`);
  2. `Register`;
  3. the commit;
  4. the first draw;
  5. the shutdown hook `intrinsic_plugin_shutdown_v0`, which the host calls on `app.quit` before
     the engine's teardown;
  6. its static destruction (at process exit).
- **Step 5, the driver and the pass assertions**, written before the first run.
  - Environment: an X display (`DISPLAY`, checked first), the promoted Vulkan path with a working
    device (checked through the existing diagnostics tool), and an isolated agent socket and config
    directory. A readiness deadline applies to every wait, and the driver ends with `app.quit`.
  - Assertions:
    - the plugin loads;
    - every identity and state check matches;
    - `spike.echo` answers over MCP;
    - the config section `spike` (one field, `marker_size`, default 8) is visible after the load,
      with schema and default. A change to 12 through the existing config-apply path results in an
      active value of 12, the plugin's change callback is called exactly once, and the marker is
      drawn at 12;
    - the explicitly opened window shows its marker in a `region="window"` view capture (the
      default capture excludes panels);
    - the lifecycle events come once each, in the expected order;
    - ASan and UBSan are clean at exit.

  Late config registration may fail, because defaults bind at boot and the change callback needs a
  previous section. That is recorded as a spike finding, not fixed by a config redesign.
  - None of these is a timing result, so no pre-flight is needed for S1's assertions.
- **Step 6, variant (b), attempted whatever (a) shows.**
  - The fixture's dependency closure (the libraries the plugin imports, and theirs) is built `SHARED`
    with position-independent code, under the option, plus the runtime search path. No engine-wide
    conversion.
  - The same assertions run against it.
  - The build-time and binary-size comparison of (a) and (b) is a measurement and gets its own
    experiment pre-flight.
- **Done for S1:** each S1 pass check answered yes or no in the spike report for (a) and (b), with the
  evidence, including negative results.
- **Not in S1:** unregister and reload (S2), the job form (S3), CI (the operator decides), and
  converting any real module.

**Later slices** (each gets its own plan after the spike decision):
3. **One vertical slice:** one real feature (proposal: k-means, closest to the job form)
   end-to-end as a plugin. It has its own build target, registers config, commands, windows and its
   MCP tools, runs through an extended **existing** job service (Step loop, preview and commit,
   latest wins, triple buffer, metric ring, buffer cache with budget and LRU, MCP job controls), and
   reloads in the running Sandbox. Generic registries are allowed (the target asks for them);
   central dispatch shaped by operations is not.
4. **The services generalized** from what the vertical slice needed; the hub files disappear
   (R1, R3, R5).
5. **Migrations, one algorithm per slice:** every GPU algorithm (Q43) and the CPU iterative ones
   into the job form, each with R7's acceptance (step form, zero full-array bytes per step, buffer
   lifetime, the five plugin tests).
6. **Features out of the editor into plugins**, one at a time, with R1–R3 ratcheting down.
7. **Build and CI:** one target per plugin, and dependency-aware CI selection (a change to a shared
   service rebuilds and tests its dependents) (R6).

## Acceptance criteria
- [ ] Rules R1–R7 of the target meet their targets, as measured by the checker and the baselines
      (the operator decides; this replaces the target page only then).

## Verification
```bash
python3 tools/docs/check_doc_links.py --root . --strict
```

## Log
- 2026-10-30: Q34 answered with a diagnosis: the kernel is slim and enforced, but the methods never
  got a boundary. Q35: a proposal written (the three rings, rules R1–R3 with today's numbers, the
  inventory). Q36: decisions recorded in the target. Rules R4–R6 were added with today's values:
  - CI `ci-linux-clang` median 40 min over 36 runs;
  - 48 MCP tools from a hand-written list;
  - hot reload not possible.

  13 target documents were archived, with links rewritten across 52 files (strict link check
  clean). A cleanup of the 45 active tasks, many of them finished M2 and ADR 0031 slices, is
  proposed as its own step.
- 2026-10-30: Frozen task records may not be edited, so the link rewrite there was undone. Four
  archived documents keep a forwarding page at their old path. The docs CI checks pass locally in
  strict mode: links, task policy, task-state links, docs sync, root hygiene, skill mirrors and claims.
- 2026-10-30: Codex review of the staged change (`review022`, fresh session): revise.
  - R1's measure covered only the 13 files naming five or more features. It is now scoped to
    `src/runtime` and `src/app`: 31 files name two or more (13 of them five or more), and 4 files in
    `src/geometry` are counted apart.
  - Three import counts were corrected: 42, 38 and 25.
  - `roadmap.md`'s architecture sequencing was archived, and ADR 0024's "living scorecard" line
    corrected.
  - The no-framework, no-golden and three-plugin rules are marked as proposals, not operator
    decisions.
  - The kernel stays under its convergence checker.
- 2026-10-30: Operator (Q37): "Bitte räum erst auf." The 31 tasks of the moonshot M2 and ADR 0031
  programs were retired to `tasks/done/`. Each has a retirement line with its commit reference and
  maturity, and an entry in `RETIREMENT-LOG.md`.
  - RUNTIME-354's three criteria were recorded as met, with their review sources.
  - ARCH-020's two criteria were dropped, unmet, by the operator's decision.
  - ARCH-021 and ADR 0031 are superseded.

  The session brief was regenerated, and the links were retargeted (frozen records only in their
  directory part). Evidence:
  - RUNTIME-354's scripts move to `tasks/evidence/ARCH-022/` (this task still uses them);
  - RUNTIME-326's logs and the RUNTIME-351 and RUNTIME-353 scripts are proposed for deletion, since
    the reports reproduce them.

  13 older active tasks with open points remain (September and October; three with stale `codex`
  claims). Whether they move to the backlog is put to the operator.
- 2026-10-30: Operator (Q38), decisions 1-3 carried out.
  1. The target gained "Plugins: what goes where":
     - three kinds of code (algorithm to the foundation, stateful shared service to the module
       services, feature to a plugin);
     - promotion of exactly the needed part instead of dependencies between plugins;
     - a plugin owns its UI, and the UI only calls commands, which are MCP tools;
     - the plugin layout sketch, with the spike's open points.

     The no-shape-framework and no-golden rules stay proposals. The three-plugin rule for shared
     helpers is replaced by promotion at the second consumer.
  2. 11 of the 13 older active tasks moved to the backlog (`ui`, `methods`, `rendering`, `runtime`,
     `workshop` for HARDEN-090). Their stale claim fields were cleared; the shared claim registry
     held no claims. Two stay in `tasks/active/`, because records that may not be edited name them
     as active:
     - **UI-037:** the frozen record RUNTIME-277 says "UI-037 (active)", and the strict task-state
       check enforces it;
     - **METHOD-040:** ARA claim C45 cites its active path as proof, and `ara/` is not committed here
       (operator rule). The first push moved it; `ci-docs` failed on `813fd16dc` and `579e8e8cf`, and
       the move was undone.
  3. The reviewed evidence folders of RUNTIME-326, 351, 353 and 354 were deleted; the phase 2
     scripts remain in `tasks/evidence/ARCH-022/`.

  The strict checks pass (links, task policy, task-state links). The one local link finding is in
  the uncommitted `docs/research/geometry-methods-2026-10-23.md`, which links the old METHOD-083
  path; it is the operator's file and stays untouched.
- 2026-10-30: Q39-Q42 recorded in the target as "Jobs, preview and observation":
  - one way to run, with preview and commit separate, the latest preview winning, and a coarse level
    while dragging;
  - step functions with the loop owned by the job service;
  - resident state, with data transfer only at start and end;
  - buffer lifetime until Accept, Reset or convergence, then a budgeted LRU cache (active runs
    pinned);
  - a live preview checkbox, with a lock-free triple buffer as the hand-over (the operator's double
    buffering, with a third buffer so neither side waits), a full metric ring, and the same controls
    for agents over MCP;
  - five cheap tests per plugin.

  New rule R7 (interactive and observable) with today's values. GPU k-means already uploads once
  and previews at an interval; CPD has `Step()`.
- 2026-10-30: Q43: the job form applies to every GPU algorithm, and new ones are built that way. It is
  recorded in the target (scope), in AGENTS.md §6 and in `docs/agent/contract.md` (the method
  protocol), with the skill mirrors synced. The first plan was written for review.
- 2026-10-30: Codex plan review of the first plan (`plan022`, fresh session): revise, with eight
  findings:
  1. an executable counting contract for R1/R2;
  2. the full pre-flight for timings, since decisions rest on them;
  3. precise R5/R6/R7 measures (`ci` does not build the Sandbox);
  4. S1 must prove one identity of shared state with a separately built plugin;
  5. S2 must cover undo and redo across a reload (today's history holds `std::function`), bounded
     draining and failed loads;
  6. S3 needs a resident-GPU case and explicit commit semantics;
  7. a vertical slice before broad migration;
  8. the policy wording must keep the metrics and hybrid exceptions and the optional accept on
     convergence.

  Folded. Delta reviews:
  - `plan022b`: revise (a ceiling could rise along with a regression; commit semantics; R7 in the
    acceptance criterion);
  - `plan022c`: revise (`HEAD~1` misses a two-commit push);
  - `plan022d`: approve.

  The measurement runs still need their frozen packets and pre-flight reviews.
- 2026-10-30: Operator (Q44): "Ja bitte, slice 1". The ratchet was implemented:
  - `tools/repo/check_feature_boundaries.py`, its baseline `feature_boundaries_baseline.json`
    (R1 31 files; R2 27 editor imports and 3 cross-plugin imports, per folder) and its tests
    `Test.CheckFeatureBoundaries.py` (14), written first. They were red without the checker.
  - Wiring: in `ci-docs`, the docs-sync step passes its change base through `GITHUB_ENV` to a new
    strict ratchet step. `Test.WorkflowRouting.py`'s harness now provides `GITHUB_ENV` and asserts
    the base per event.
  - Name matching uses token boundaries instead of the planned whole words. The snake_case form must
    not touch letters or digits; the PascalCase form must not be preceded by an uppercase letter or
    followed by a lowercase one. Whole words would have missed names inside identifiers such as
    `ApplyEditorKernelDensityConfig`.
  - Mutations, each red:
    - M1 the R1 ceiling;
    - M2 editor imports;
    - M3 cross-plugin imports;
    - M4 a new folder without an entry;
    - M5 a stale entry;
    - M6 a fall below the ceiling;
    - M7 the comparison with the previous baseline;
    - M8 a new entry above 0;
    - M9 a missing revision;
    - M10 an empty enum;
    - M11 the name boundaries;
    - M12 the plugin-folder exclusion;
    - M13 the workflow step;
    - M14 a hard-coded change base.
- 2026-10-30: Ratchet code reviews (diff `0f015760…`):
  - **Codex (`review022s1`, fresh session): revise.**
    - P1: plugin folders under both roots shared one name.
    - P1: declarations were read as lines, so comments, strings and one-line forms could fool R2.
    - P2: a plugin folder without sources went unnoticed.
    - P2: duplicate or blank names were accepted.
    - At plan level: a failed push leaves its raise as the next push's base.
  - **Ponytail: ship**, with three savings: a literal prefilter (R1 2.9 s → 0.1 s), a test that
    duplicated the CI step, and a README copy of the contract.
  - **All applied:**
    - folders are keyed by path and discovered as directories;
    - declarations are read with comments, strings and raw strings removed, without line anchors;
    - the names must be unique snake_case;
    - the comparison uses the lowest ceiling in the baseline's Git history instead of one change
      base, which also removes the `GITHUB_ENV` hand-over and the routing-test change;
    - the duplicate test and the README copy are gone.
  - **Verification:** 16 tests pass, and the repository passes in 0.4 s. The baseline is unchanged
    in value: R1 31, the R2 counts identical, now keyed by path.
  - **Mutations, each red:** M1–M12 (M9 is now missing history, and the enum checks are split in
    two), M13 the workflow step, and the new M15–M18 (comments and strings, basename identity,
    folders only from sources, line-anchored imports).
- 2026-10-30: Codex delta (`review022s1b`): revise.
  - P1: the history was simplified at merges, so a lowering on a merged side branch could be lost.
  - P1: C++ tokenization: spaces or comments between the dots of a module name, and line splicing
    before comments.
  - P2: an unreadable baseline version was skipped as if deleted.

  Applied:
  - the history is read with `--full-history`, and `ls-tree` tells a deleting commit from an
    unreadable one, which now fails;
  - names allow whitespace between tokens and are normalized;
  - backslash-newline is spliced before comments are removed.

  The test helper now looks for the expected text only in finding lines; before, a case could pass
  on a report line. Tests: 20 pass, among them a merge with `-s ours` over a lowering side branch,
  a removed blob, spaced and commented names, a continued line comment, and a spaced declaration.
  Mutations, each red: M1–M22, plus M20b (import spacing). M20 (declaration spacing) first survived
  and is red with the spaced-declaration test.
- 2026-10-30: Codex second delta (`review022s1c`): revise. Splicing before raw-string recognition
  turned a raw string's contents into declarations.
  - Replaced by a one-pass lexer (`code_only`). It recognizes raw strings on the original text;
    outside them it splices backslash-newline first, then blanks comments (a continued `//` comment
    continues) and literals.
  - Only module interface units and plugin files are lexed: 0.8 s on the repository.
  - New tests: a raw string with a backslash-newline that hides a fake declaration, and a name split
    by backslash-newline.
  - 20 tests pass. Mutations M1–M24 are each red: M23 removes raw-string recognition, M24 skips
    interface units outside plugins. M21 (splicing outside raw strings) first survived and is red
    with the split-name test.
- 2026-10-30: Codex third delta (`review022s1d`): revise.
  - The lexical limits (trigraphs, digraphs, macro tricks) are accepted for this ratchet.
  - P1: the `.cppm`-only filter lost editor modules declared in headers.

  The checker now lexes every file under `src/runtime/Editor/` (for owners) and every plugin file
  (for imports). New test: an editor module declared in a `.hpp`, red before the fix. Mutations
  M24 and M24b, each red. 21 tests pass; 0.9 s on the repository.
- 2026-10-30: Codex fourth delta (`review022s1e`, diff `5e2e9e13…`): approve, with no remaining
  findings within the agreed scope and lexical limits.
- 2026-10-30: Inventories for R5 and R7 (two read-only agents; I spot-checked three R7 claims) in
  `docs/reports/2026-10-30-arch-022-inventories.md`, with the target's R5/R7 cells updated.
  - Codex claims review (`review022inv`, fresh session; 13 rows of Appendix A and samples of B
    checked against the code): revise, on four points:
    1. a false 128-byte readback ceiling (implicit smoothing reads 80 bytes per solve);
    2. a step-form count inconsistent with its rule (11, not 10);
    3. the counting rule for controls shared by windows;
    4. the 53 are table rows, and R5 is static reachability.
  - All corrected. The sampled source supports:
    - CPU work in the main-thread `IsReadyToApply`;
    - the six synchronous commands;
    - progressive Poisson's hard-coded GPU availability of false;
    - the workspace pool without a budget;
    - the transfer formulas of G4–G7.
- 2026-10-30: Codex delta on the inventories (`review022inv2`): findings 1, 2 and 4 resolved. My
  "once per window" correction contradicted the single texture-bake row.
  - The rule is now one row per **distinct action**, a control together with the state it changes.
    Look-alike controls that act on different operations count separately (the four scalar
    Accept/Discard pairs); one control that changes the same state from several windows counts once
    (texture bake in the Inspector and in Appearance).
  - The census already follows this rule, so the totals stand.
- 2026-10-30: Codex second delta on the inventories (`review022inv3`): approve, within the sampled
  static scope. Its editorial point is applied: the counting rule lives only in Appendix B, item 4.
- 2026-10-30: R6 timing baselines, preparation for the full pre-flight.
  - The manifest `benchmarks/ci/manifests/arch022_r6_dev_loop_baseline.yaml` runs the existing
    compile-iteration runner on the Sandbox target with preset `dev`, at `adf7e0b3c`. A sample is
    configure, clean, no-op and three touches: an operation unit, a plugin config interface and a
    geometry interface. There are five samples plus one warmup.
  - The runner gains `params.dependency_set` (default `ci`, the previous constant), so it guards the
    vcpkg tree the preset builds against.
  - Technical test (one sample, numbers not read): the first attempt found that `dev` does not
    export compile commands, so the frozen configuration adds `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
    The second attempt passed every runner assertion; its snapshot hashed the `dev` tree (1,728
    files, against 1,731 in `ci`), and the report script rendered.
  - The packet (Layer A, Layer B, `r6_report.py`) is in `tasks/evidence/ARCH-022/preflight-r6/`, to
    be frozen after this commit and reviewed by a fresh Claude and a fresh Codex session.
- 2026-10-30: R6 pre-flight, round 1 and 2 on packet `7b0c1426…361b9`: Claude REVISE, Codex BLOCK.
  - Both: the command chain was not fail-fast; the report summarized partial populations; outputs
    could be overwritten; the load average cannot separate the runner's own load from other load.
  - Both: the packet measured a controlled proxy (fresh tmpfs per sample, ccache off, 32 jobs, mtime
    touches, six clean builds), not the plan's workload, and recorded no deviation. Further: CI
    re-runs inflate created-to-updated; the 3 h timeout did not fit the predictions; noop could
    relink unseen; the 10 % resolution claim was unsupported.
  - Operator (Q45): "ok then Option 1", the plan's workload.
  - Revision 2: the runner gains `build_once`, unique appended probe edits with an exact source check,
    optional `jobs`, per-command host counters (other-process CPU, swap, major faults), empty-window
    checks for noop and a settle build after every scenario; three new tests, each failing with its
    mechanism removed. The manifest (`.v2`) names the importers each interface edit must rebuild
    (static count: 2 direct importers for the config interface, closure 19; 1 for the geometry
    interface). `r6_run.sh` holds the fail-fast chain; `r6_report.py` refuses incomplete populations
    (self-check with mutations). The installed gh 2.4.0 lacks `run list --branch/--status`; the CI
    query uses the REST API (found by the first technical-test attempt, which the fail-fast chain
    stopped).
  - Technical test of revision 2 (two samples, `warmup` and `baseline`, through `r6_run.sh` with a
    temporary test manifest; numbers not read): chain exit 0, both samples sealed, the second sample
    reused the build tree without `clean`, the probe files held exactly their probe lines, every
    settle build was empty, host counters were recorded, the CI cohort had 20 runs with all fields,
    and the report rendered. The packet goes to delta reviews.
- 2026-10-30: R6 delta reviews on `c41e77dd…bd9da`: Claude REVISE, Codex REVISE.
  - Codex: the report could crash instead of printing UNEVALUATED on early failures or malformed
    inputs; `source_clean_before_after` stayed true despite probe edits; the CPU residual was presented
    as exact attribution; flags were missing per measurement; the settle wording said "every scenario".
  - Claude: with the 2 GiB swap full, the swap flag cannot see memory pressure (page-cache reclaim
    shows only as faults).
  - Revision 3: the report turns every missing or malformed input into UNEVALUATED, flags each
    measurement row, adds a "memory stall" flag (PSI `some` above 1 % of wall time; idle host 0 µs in
    5 s), labels the CPU residual an estimate, and shows the CI median over single-attempt runs. The
    runner records the PSI stall, removes `CMAKE_BUILD_PARALLEL_LEVEL` when `jobs` is absent, and
    records the probe hashes with honest source diagnostics. Self-check and runner tests extended,
    each new check mutation-checked.
  - Technical test of revision 3 (two samples through `r6_run.sh`, numbers not read): chain exit 0,
    both samples sealed, the new host and source fields present, report rendered.
- 2026-10-30: R6 second delta on `54ec2561…d76f`: Claude PASS (optional: `AttributeError` not
  caught), Codex REVISE (a malformed value such as a null `startedAt`, a zero wall time or a string
  number could still crash the report outside its handler; the CPU residual was described as only
  over-flagging, but its error can have either sign). Revision 4 changes only the report and the
  packet text: the page is rendered completely before printing and any parse or render error makes it
  UNEVALUATED; wall times must be positive and no CI run may end before it starts; the residual is
  described as able to over- or underestimate. Self-check extended and each new check
  mutation-checked; the report renders the revision-3 technical test's outputs (exit 0, numbers not
  read). The runner is unchanged since revision 3, so its technical test stands.
- 2026-10-30: R6 pre-flight third delta on `c77edd2e…3fcb`: Claude PASS, Codex PASS (records and
  verbatim verdicts in `tasks/evidence/ARCH-022/preflight-r6/reviews/`). The start gate rebuilt the
  manifest from the live state at `d6824e47f` (digest matched) and the run started 23:25 local; the
  chain exited 0, all six samples sealed, the report rendered, and no measurement was flagged.
  - Audit: every summary recomputed from the raw outputs, independently of the report, and agrees.
  - Result ([R6 baseline](../../docs/reports/2026-10-30-arch-022-r6-baseline.md)): Sandbox rebuild
    after one edit 19.1 s (operation unit), 48.6 s (plugin config interface, 20 units), 18.7 s
    (geometry interface); each includes a 12.8 s Sandbox link (GNU ld); no-op 0.14 s; clean build
    2.0 min plus 8.2 s configure; CI median 41.1 min (20 green runs, 21 to 71). The target's R4 and R6
    cells are updated.
  - Claims check of the report by a separate agent: changes required (link 12.9 → 12.8 s median,
    geometry max 18.7, no-op spread 2.3 %, "fourth packet version", the 20 units are the interface
    plus its 19 importers in the Sandbox target, R1 not R2, audit wording); all applied.
  - Surprises against the written predictions: the clean build (predicted 10 to 45 min) and the
    config interface (predicted 1 to 8 min) were faster; the coordinator's earlier exposure to a
    technical-test clean build is recorded in the packet. The config edit's chain runs through the
    runtime's `PointAnalysisOperations` interface into a Sandbox editor panel.
  - Slice 1 output is complete; the numeric R4, R6 and R7 targets are the operator's.
- 2026-10-31: Operator (Q46) set the R4, R6 and R7 targets as proposed (target table updated) and
  chose the order: a CI time breakdown, then a measured linker comparison, then slice 2. The linker
  comparison needs lld: the agent cannot install packages (sudo needs a password); apt.llvm.org now
  offers `lld-24` (the `clang-23` in use is an older snapshot); `ld.gold` is already installed.
- 2026-10-31: Step 1 of Q46, the CI time breakdown
  ([report](../../docs/reports/2026-10-31-arch-022-ci-breakdown.md)), over the R6 baseline's 20 green
  push runs, from GitHub's step timings (an analysis of logged data, no run).
  - On a push, `ci-linux-clang` runs only `full-cpu` (`ci-docs` runs in parallel, median 4.2 min).
  - Job median 38.2 min: the cold build is 32.1 min (84 %), the tests 4.4 min (12 %).
  - Every push builds without a compiler cache. The measured CI-007 ccache policy exists, but only
    in `pr-fast.yml`, which never runs on a push.
  - 8 of 20 runs first waited 6.5 to 34.2 min for the previous run (the concurrency group); there
    was no runner queueing.
  - Build times fall into two groups (17.6–27.1 and 30.4–35.2 min); the cause is unknown.
  - Claims check by a separate agent: changes required (ci-docs also runs on push; image and step
    count only from two logs; the no-cache claim rests on the workflow; script scope; lever wording),
    applied, then approved. The target's R6 cell links the breakdown.
  - Open for the operator: whether the 15-minute target means job time or run time including the
    wait.
- 2026-10-31: Step 2 of Q46, the linker comparison, prepared for a full pre-flight.
  - lld (Q47): sudo needs a password, so `lld-24` and `libllvm24` were fetched from apt.llvm.org
    with `apt-get download`, checked against the apt index's sha256, and extracted to
    `~/.local/opt/lld-24`. A wrapper provides `libLLVM.so.24.0` to lld only; the system is
    unchanged.
  - The runner gains a linker identity check: the final link command (`ninja -t commands -s`) and
    the binary's `.comment` must match the arm's declared linker in every sample. Test included,
    each check mutation-checked.
  - Manifests `arch022_linker_bfd.yaml` and `arch022_linker_lld.yaml` are the R6 workload with only
    the linker changed. `ld_run.sh` runs bfd, lld, bfd, lld in one worktree path; `ld_report.py`
    compares them and refuses inconsistent arms (self-check with mutations).
  - Technical test (all four runs with two samples each, numbers not read): every run exited 0,
    every sample had its arm's linker, and the report rendered, so its consistency gates held.
- 2026-10-31: Operator (Q48): the 15-minute CI target means the job time (as read back to the
  operator); the target table says so.
- 2026-10-31: Linker pre-flight rounds 1 and 2 on `96d8dc7b…dc60f`: Claude REVISE, Codex REVISE.
  - Both: arm equivalence was not enforced where the arms could differ: link commands, CMake
    cache, dependencies, executed compile edges, and binding of each run to the reviewed manifest
    and runner. Codex also: sealed contents unchecked, and the R6 Layer A was outside the manifest.
    Claude also: the rule looked at wall time only.
  - Layer B overstated the report's link-command check and misstated the baseline spread (0.6 s on
    the config interface, not 0.3 s).
  - Revision 2:
    - the report gates all of the above;
    - the decision also needs a link-step saving of at least 2 s, a non-link difference under 1 s,
      and no flagged edit measurement;
    - the run order is balanced (bfd, lld, lld, bfd);
    - the chain records both linkers' identities, and the runner records the binary size;
    - the dead gold filter is removed;
    - every new gate is mutation-checked.
  - Technical test of revision 2 (all four runs with two samples each, numbers not read): every run
    exited 0, every sample had its arm's linker and a recorded binary size, `linkers.txt` names
    `ld.bfd` 2.38 and LLD 24.0.0, and the report rendered, so every arm-equivalence gate held.
- 2026-10-31: Linker pre-flight delta on `87df2ae5…14deb7`: Claude PASS, Codex REVISE.
  - Codex's blocker: summaries selected samples by the raw `arm` label, which was never checked
    against the frozen order, so a relabelled baseline sample could leave the decision.
  - Revision 3:
    - the report gates every sample's label against its position;
    - it requires a complete `linkers.txt`, which the chain now writes from separate assignments,
      also recording clang's ld (`/usr/bin/ld`);
    - it shows the manifest prefix and hashes, and counts binary sizes over measured samples only;
    - any repeat after "no decision" is outside the frozen plan.
  - Each new check is mutation-checked.
  - Technical test of revision 3 (four runs, two samples each, numbers not read): every run exited
    0, `linkers.txt` complete, and the report rendered.
- 2026-10-31: Linker comparison: both reviewers PASS on `e758d333…48156`.
  - The start gate matched the live digest, after green CI on the measured runner. Records are in
    `tasks/evidence/ARCH-022/preflight-ld/reviews/`.
  - The four runs (bfd, lld, lld, bfd) all exited 0, and every report gate held.
  - Audit: the coordinator recomputed every summary from the raw outputs; it agrees.
  - Result ([report](../../docs/reports/2026-10-31-arch-022-linker-comparison.md)): lld links the
    Sandbox in 0.84 s instead of 12.7 s. The rebuilds after one edit fall from 18.9 to 7.1 s
    (operation unit), from 18.6 to 6.7 s (geometry interface) and from 48.8 to 36.7 s (plugin
    config interface). The non-link time is unchanged (at most 0.19 s apart), and no measurement
    was flagged.
  - The frozen rule says lld is a candidate default for dev builds. Adoption is a separate change
    with its own tests (the CPU suite linked with lld, the Sandbox started, packaged lld).
  - Surprise: the lld link (predicted 1–4 s) and the lld geometry rebuild (predicted 7–11 s) were
    faster than predicted.
  - Against the R6 target (plugin edits):
    - With lld, the plugin's operation-unit edit meets it.
    - The plugin config interface does not; its serial module chain is R1's problem.
    - The geometry edit is a core-library edit; it is also under 10 s.
  - Claims check of the report by a separate agent: changes required (clean-build drift 0.41 s, the
    geometry edit is not a plugin edit, a held prediction missing, where the records are); applied.
- 2026-10-31: Slice 2 started (Q46: "danach gehen wir 3. an").
  - An exploration of the code confirmed: static linking only; hand-written registration lists; no
    unregister for config sections and MCP tools; ImGui statically inside the executable.
  - A toy check (clang 23) showed one identity of host statics across a `-rdynamic` boundary.
  - The S1 implementation plan was reviewed by a fresh Codex session (`plan022s1`, thread
    `01a19266`): REVISE (7 blocking), REVISE (3), REVISE (1), then APPROVE on revision 4.
  - What the revisions changed:
    - both link variants are always attempted;
    - the CMake module recipe is tested, with a named fallback;
    - the load is deferred past the MCP dispatch, and the commit is atomic, with the originals
      kept;
    - identity is shown by state, with ImGui, ImPlot and thread-local probes and an exported
      lifecycle recorder;
    - an expected first config change is specified;
    - the GUI checks are deterministic;
    - the ABI and sanitizer limits are stated.
- 2026-10-31: S1 steps 1 and 2 (containment, build recipe), in `build/spike` (preset `dev`,
  `-DINTRINSIC_PLUGIN_SPIKE=ON`).
  - Containment: with the option off, a fresh configure has no `IntrinsicSpike*` or
    `plugins/spike` target, no spike test and no `--export-dynamic`.
  - CMake 4.3.2's `$<COMPILE_ONLY:ExtrinsicSandboxEditor>` passes include paths and definitions but
    no module sets. The plugin failed with "module 'Extrinsic.Core.Logging' not found".
  - The named fallback works: `CollectHostModules.cmake` gathers the `-fmodule-file=` entries of
    the host's `.modmap` files (466) into a response file, and the plugin compiles with module
    scanning off against the host's BMIs.
  - The host's 460 artifacts and BMIs were byte-identical before and after both plugin builds.
  - The plugin `.so` defines only `intrinsic_plugin_probe_v0`. Its engine references (a function
    and a module initializer) are undefined and exported by the executable.
  - With `ENABLE_EXPORTS` the executable exports 173,917 dynamic symbols. Its size (738 MB) is noted,
    not compared.
  - `plugins/` is added to the repository root allowlist (`tools/repo/root_allowlist.yaml`), the
    plugin location the target names (`plugins/<name>/`).
- 2026-10-31: S1 steps 3 to 5, variant (a): host loader, plugin and driver.
  - Code:
    - `AgentServerOptions::RegisterExtraOperations`, a plain optional hook; only the spike sets it.
    - `Extrinsic.Sandbox.PluginSpike`, in the editor library only with the option: the plugin
      contract, the identity probes, the lifecycle recorder, the deferred atomic commit with failure
      injection, and the tools `plugin_load`, `plugin_host_identity` and `app_quit`.
    - The plugin: config section `spike`, tools `spike_echo` and `spike_identity`, window
      `view.spike` with a magenta marker.
    - `plugins/spike/spike_driver.py`.
  - The tool names follow the repository's snake_case convention, not the plan's dotted names.
  - Driver run 2 (`tasks/evidence/ARCH-022/spike-s1/run2/summary.json`): 41 of 46 checks passed.
    - Yes:
      - the plugin loads into the running Sandbox;
      - an injected failure at each of the 7 stages leaves tools, config sections and windows
        unchanged;
      - the counter, inline static and thread-local have one address, and the plugin's mutations
        are seen by the host;
      - the ImGui context, allocator and user data and the ImPlot context are the host's;
      - `spike_echo` answers over MCP;
      - the config section is visible with schema and default 8;
      - the window draws its 64×64 marker exactly;
      - the lifecycle events come in order, with no repeated static init or destruction.
    - No, finding 1 (late config registration): `config_apply` accepts 12, but `OnChanged` runs only
      when the active config holds the section before and after the change
      (`Runtime.EngineConfigControl.cpp`). A section registered after boot is not in the active
      config, so the first change never reaches the plugin. That is 3 of the failed checks: the
      callback, the redrawn marker, and the `config_changed` event. Recorded, not fixed (plan).
    - No, finding 2 (host exit): the Sandbox ends with status 1 and a LeakSanitizer report (116,642
      bytes in 24 allocations, from the graphics driver, X11 and dbus), identically without the
      plugin. The plan's "ASan clean at exit" cannot hold for this host. The driver adds a session
      without the plugin and asserts the same status and no report beyond it; both pass.
  - Symbols:
    - the plugin defines 2 strong symbols (its entry points), the ASan section bounds and 813 weak
      vague-linkage symbols, so the allowlist is by kind (only entry points and ASan bounds strong),
      not by name;
    - with the option off, the Sandbox builds and exports 3,217 dynamic symbols (173,917 with it).
- 2026-10-31: S1 block review.
  - Codex (thread `01a19286`): CHANGES REQUIRED.
    - Must fix: concurrent loads bypass the single-plugin limit; the config validator accepts -12,
      "12" and 1.5; the plugin is not rebuilt when the host changes.
    - Should fix: stage-specific failure checks with registry contents; marker dimensions with an
      absent-marker baseline; schema, default and active state checked separately; the leak
      comparison with frames; isolated config and a Vulkan check; quit racing the socket.
  - Ponytail review: the repository's LSan recipe for a Vulkan exit (BUG-083: one ICD through
    `VK_DRIVER_FILES`, `fast_unwind_on_malloc=0`); Pillow instead of a hand-written PNG decoder; and
    smaller simplifications (-75 lines).
  - Fixed:
    - a load reservation released when the continuation is destroyed;
    - a JSON validator: one integer field in [1, 64], nothing else;
    - `OBJECT_DEPENDS` of the plugin on the host executable and the module list (verified: touching
      the host makes the plugin object dirty);
    - the driver as above, plus pipelined double-load and invalid-payload tests;
    - no `PluginHostContext`, integer addresses, no no-op registry copy, no redundant import or CMake
      line.
  - The BUG-083 recipe was tried before adopting it:
    - without a plugin the Sandbox still leaked 240 bytes (NVIDIA, pipeline creation) or 368 bytes
      (lavapipe, one-time driver initialization);
    - the NVIDIA leak appears in some runs only (run 3: in the plugin session but not the no-plugin
      one).
  - So finding 2 is narrowed: with the repository's settings the Sandbox's exit is not LSan-clean on
    this host because of driver allocations. The driver now attributes records by their frames: no
    record touches the plugin or the spike host code.
  - Driver run 4 (`tasks/evidence/ARCH-022/spike-s1/run4/summary.json`): 52 of 58 passed. The 6
    failures are finding 1 (4: the section is not active after a late registration, so no change
    callback, no redrawn marker, no `config_changed` event) and finding 2 (2: exit status 1 and the
    driver's leak records).
- 2026-10-31: S1 review delta. Ponytail: Ship (optional cuts applied). Codex: CHANGES REQUIRED.
  - Codex's remaining points:
    - section contents in the rollback comparison;
    - the Vulkan check read the wrong case;
    - the socket double-load is not guaranteed to land in one frame;
    - non-leak sanitizer errors could pass.
  - Fixed:
    - the comparison holds each section's `config_get`;
    - `device_status.actual_backend == "vulkan"` and `is_operational` are checked directly;
    - a spike hook (`probe_pending`) makes a second load in the same dispatch, which must be refused
      as `load_pending`, while a socket load right behind it may be refused as pending or already
      loaded;
    - any sanitizer line other than the leak report fails;
    - the no-plugin session is dropped, since `baseline-bug083*` already shows the driver's own leaks.
  - Driver run 7 (`tasks/evidence/ARCH-022/spike-s1/run7/summary.json`): 54 of 60 passed.
  - The 6 failures:
    - Finding 1 (4 failures): after the first change the section is active with 12, but the plugin
      was not told of that change.
    - Finding 2 (2 failures): status 1 and the NVIDIA driver's 240 bytes. No other sanitizer line, and
      no leak frame in the spike code.
- 2026-10-31: S1 review, second delta (Codex): CHANGES REQUIRED.
  - A failed registry read could pass as unchanged, and the section listing's `schema_id` and
    `schema_version` had dropped out of the comparison.
  - Fixed: every registry read must succeed or the session fails, and each section keeps its
    listing entry next to its `config_get`.
  - Driver run 8 (`tasks/evidence/ARCH-022/spike-s1/run8/summary.json`): 54 of 60 passed. The same
    two findings remain.
- 2026-10-31: S1 step 6, variant (b): `build/spike-shared` (preset `dev`,
  `-DINTRINSIC_PLUGIN_SPIKE=ON -DBUILD_SHARED_LIBS=ON`).
  - The module libraries are added without a type, so the configure switch alone makes the engine
    shared (13 `.so`); there is no source or CMake conversion. The plugin links
    `ExtrinsicSandboxEditor` like any target, and CMake carries its module sets: no response file,
    no `COMPILE_ONLY`.
  - The first build failed: the project's static `libtinygltf.a` was not position-independent and
    cannot go into the shared runtime. With `-DCMAKE_POSITION_INDEPENDENT_CODE=ON` it builds.
  - Driver run b1 (`tasks/evidence/ARCH-022/spike-s1/run-b1/summary.json`): 54 of 60 passed, the
    same checks and the same two findings as variant (a). Identity holds with shared libraries too:
    one ImGui context and allocator, one ImPlot context, one counter, inline static and
    thread-local.
  - Observed, not compared (the comparison of build time and size needs its own pre-flight):
    - variant (a): executable 738 MB, plugin 43 KB;
    - variant (b): executable 7.4 MB plus 13 shared engine libraries, plugin 4.8 MB.
- 2026-10-31: ci-docs failed on `01095d834`: the R2 ratchet treated `plugins/spike` as a new plugin
  above 0.
  - The spike imports `Extrinsic.Runtime.EditorWindowRegistry` for `EditorWindowDescriptor`, an
    editor-internal module under the ratchet's definition. That is a spike finding: today a plugin
    cannot register a window without an editor-internal import; R2 needs the descriptor in a service
    module.
  - Fix: `check_feature_boundaries.py` excludes exactly `plugins/spike` (`NOT_PLUGINS`), with a test
    that every other folder under `plugins/` still counts; real plugins are unaffected.
  - Why it slipped through locally: the local replay of ci-docs took only lines starting with
    `python3` and missed 14 single-line `run: python3 ...` steps, among them this check. The lesson
    in `docs/lessons/engineering.md` now names that trap.
  - The next ci-docs step would also have failed: the source rule
    `ProductionSourceHasNoApplicationCallbackLifecycle/app` forbids `Runtime::Engine&` under
    `src/app`. The spike now takes the `ServiceRegistry` and an exit callback instead. After the
    change, driver runs 9 (variant a) and b2 (variant b) passed 54 of 60 each, with the same two
    findings. All 56 ci-docs commands, single-line `run:` steps included, pass on a clean copy.
- 2026-10-31: ci-linux-clang also failed on `01095d834`, at "Validate layering (strict mode)".
  - The spike host module imported `Extrinsic.Core.Config.EngineLoad`, and app code may not import
    core directly. It now imports `Extrinsic.Runtime.DerivedConfigSections`, which re-exports it, as
    other app code does.
  - `check_layering` and `check_compiler_hazards` pass; both variants build; driver run 10: 54 of
    60.
  - The failed "Aggregate gate timing result" step followed from the first failure.
  - The lesson now names ci-linux-clang's repository checks too.
- 2026-10-31: ci-docs also failed on `ff93e28a6`, at the module inventory freshness check: the new
  module `Extrinsic.Sandbox.PluginSpike` was missing from `docs/api/generated/module_inventory.md`
  (regenerated: 455 modules). The local replay now runs every `run:` block of ci-docs with
  `bash -e` on a clean copy (23 run, 2 skipped for the GitHub context or package installation, 0
  failed), plus `check_layering` and `check_compiler_hazards`. The lesson says so.
- 2026-10-31: Codex review of `01095d834..30881f958` (variant (b) and the CI fixes, thread
  `01a19286`): APPROVE, no findings.
  - In (b), host and plugin use the same engine `.so` files, and the (a) branch is unchanged.
  - The ratchet exclusion is exact and tested, and the exit callback keeps the shutdown order.
  - S1 is complete: both variants pass 54 of 60. Three findings for the plugin design:
    1. a config section registered after boot is not active, and its first change does not
       reach the plugin;
    2. registering a window needs an editor-internal module (R2);
    3. the Sandbox's exit is not LSan-clean on this host because of the NVIDIA driver, plugin or
       not.
- 2026-10-31: Q50 step 1, finding 3 (the NVIDIA leak at exit).
  - With the ICD library preloaded (`libGLX_nvidia.so.0`, from `nvidia_icd.json`), the driver stays
    mapped until LSan runs. Its leaks are then named: `libnvidia-glcore.so` of driver 580.159.04,
    114,000 bytes in 21 allocations.
  - One suppression, `leak:libnvidia-glcore.so` (`plugins/spike/lsan-driver.supp`, appended to
    `lsan.supp` by the driver), makes the exit clean: status 0, two of two no-plugin runs
    (`baseline-nvidia-*`).
  - Driver runs 11 (variant a) and b3 (variant b): 56 of 60 each. Exit status 0, sanitizers clean,
    lifecycle order as before; the 4 failures are finding 1.
  - The repository's `lsan.supp` is unchanged; whether it gets the line is the operator's call (CI
    uses lavapipe).
- 2026-10-31: Plan for Q50 steps 2 to 4, revision 2 (Codex plan review 1: REVISE, thread
  `01a19341-7b01-7742-8e3e-5a9765793450`; three blocking findings on step 3, answered below).
  Serves Q49/Q50 and through them Q36 (cheap changes, short builds, plugins that register
  themselves): R2 (a plugin uses no editor internals), R3, R4, R6, and the S1 findings 1 and 2.
  - **Step 2, lld as the dev linker.**
    - `option(INTRINSIC_USE_LLD OFF)`; the `dev` and `dev-cuda` presets set it ON. `ci` inherits
      `base`, not `dev`, so CI is unchanged until the operator decides.
    - With it on: `find_program(INTRINSIC_LLD NAMES ld.lld-<clang major> ld.lld-24 ld.lld
      HINTS /usr/lib/llvm-<clang major>/bin)`. The compiler is clang 23, but the apt sources
      offer no `lld-23` (the `lld` metapackage depends on it), so the operator installs
      `lld-24` (`/usr/bin/ld.lld-24`), the version the comparison measured. If found, `--ld-path=` is appended to the existing
      `INTRINSIC_LINK_FLAGS`, which modules, tests, the Sandbox and the spike plugin already
      use, and a status line names it. If not found, a status line says GNU ld stays. A
      NOTFOUND result is searched again at the next configure, so the operator's install on
      Monday is picked up by `cmake --preset dev`.
    - Test: `Test.SanitizerPresets.py` pins `INTRINSIC_USE_LLD` ON in `dev` and `dev-cuda`
      and not ON in any preset that a `ci*` preset resolves to (mutation: remove it from
      `dev`).
    - Real path, in a separate build directory configured with the `dev` preset and its
      sanitizers, with the extracted lld wrapper on `PATH` only as `ld.lld-24`, the name the
      package installs (so discovery itself is tested): the CPU contract tests and the Sandbox link with lld
      (`.comment`), the tests pass, and the spike driver passes in both variants. Fallback:
      a configure without lld prints the status line and the link commands carry no
      `--ld-path`.
  - **Step 3, finding 1: config sections registered after boot.**
    - `EngineConfigControl` gains `[[nodiscard]] bool RegisterSection(registration)` and
      `[[nodiscard]] bool UnregisterSection(std::string_view name)`.
    - Both views change together: the engine's config that hot apply and previews read, and
      the control's `ActiveConfig` copy that `config_get` reads.
    - Register: `EngineConfigSectionRegistry::Register` validates the default and rejects a
      duplicate. The canonical default then goes into both views. A section of that name
      already in the config is rejected without change. Register calls no callback: the plugin starts from its own default,
      so nothing differs.
    - Unregister: the registration (new `EngineConfigSectionRegistry::Unregister`) and the
      section leave both views. No value is retained here: the target requires config to survive
      a reload, and S2 implements that.
    - Both return false and change nothing when the control is unbound, and when called
      from an `OnChanged` callback during hot apply (a flag set for the dispatch loop and
      restored to its previous value after it; the loop holds pointers into the registry and
      the config). Callers are on the main thread,
      as `config_apply` is today.
    - Spike: the commit registers its section first and unregisters it when a later stage
      fails. No callback runs in between, so the rollback is complete.
    - Tests in `tests/contract/runtime/Test.RuntimeConfigControl.cpp`, each failing without
      the change:
      - a late registration is in both views with its default;
      - the first hot apply after it calls `OnChanged` once, with the default as previous (the
        S1 failure);
      - an unregister removes the registration and the section from both views; a later apply
        naming the section changes nothing and calls nothing;
      - a duplicate, an already present name, an unknown unregister, an unbound control, and a
        call from inside `OnChanged` return false and change nothing.
    - The four S1 checks of finding 1 are expected to pass in the spike driver.
    - Limitation, recorded and not changed here: a section in `engine.json` for a plugin that
      is not loaded at boot is discarded at boot (`ParseAppSections` warns "not registered").
      The late registration starts from the default. Keeping such sections for a later
      registration is a separate decision for the operator.
  - **Step 4, finding 2: windows without editor internals.**
    - The target names "UI host (register a window and a menu entry)" as a module service.
    - A new module `Extrinsic.Runtime.UiHostTypes` in `src/runtime/Services/` is the first
      module of that ring. It holds `EditorWindowHandle`, `EditorWindowDescriptor` and
      `EditorWindowMenuEntry`, unchanged. `Extrinsic.Runtime.EditorWindowRegistry` re-exports
      it (`export import`), so its 21 importers compile unchanged.
    - The spike plugin imports `UiHostTypes` instead of the registry; it only fills
      descriptors, and the host commits them. Its R2 count is then 0/0.
    - `NOT_PLUGINS` is deleted. `plugins/spike` gets an ordinary baseline entry `0/0`, which
      the ratchet allows because an absent entry counts as zero.
    - Its exclusion test is replaced by two tests: the spike folder is an ordinary plugin
      folder, and an editor import in it is rejected.
    - Checks:
      - both builds and both spike variants, with consumers of the registry rebuilt;
      - the ratchet, layering, module inventory, source rules and the whole ci-docs replay;
      - the window-registry tests unchanged.
  - Codex plan review 2 (delta, same thread): findings 1-3 resolved; new blocker: the named
    package `lld-24` does not match clang 23's search name. Answered with its two wording corrections.
    Review 3 (delta): `lld` depends on `lld-23`, which has no candidate here; the reviewer's
    smallest fix applied (install `lld-24`, search `ld.lld-24`, test that name through `PATH`).
  - Not in these steps: CI adoption of lld; a repository `lsan.supp`; moving other editor
    types; keeping config values across a reload (S2 implements it); boot-document values for sections
    registered later.
  - Codex plan review 4 (delta): APPROVE, task-diff SHA-256
    `a70174160ec3105a7b67d941c428af420c3e5fcce8c22affcb0426be2d13cf53`.
- 2026-10-31: Q50 steps 2 to 4 implemented.
  - Step 3 tests, mutation-checked (each mutation fails its own test):
    - no copy sync on register fails the late-registration and rejection tests;
    - a value kept on unregister fails the unregister test;
    - a guard never set gives an ASan heap-use-after-free in the dispatch test (the guard is
      needed, not only tidy);
    - a guard not restored, a present name not checked, and an unbound control not checked
      (UBSan null access) each fail their test.
  - Step 2: the presets test fails with lld removed from `dev`. The configure of `build/dev`
    without lld prints `Linker: lld not found (sudo apt install lld-24); GNU ld stays`, and
    no link carries `--ld-path`. `build/spike` with only `ld.lld-24` on `PATH` finds it, and
    58 link commands carry it.
  - Step 4: `Extrinsic.Runtime.UiHostTypes` (`src/runtime/Services/`), re-exported by the
    window registry. `plugins/spike` counts 0/0, `NOT_PLUGINS` is gone, and the replacement
    ratchet test fails with the exclusion restored.
  - Real path with lld (`build/spike`, `build/spike-shared`; dev preset with sanitizers; only
    `ld.lld-24` on `PATH`): the Sandbox, the runtime contract tests, the engine libraries and the
    plugin carry `Linker: Ubuntu LLD 24.0.0`. All 1870 runtime contract tests pass.
  - Spike driver: the finding-1 checks now pass. One new failure in run12 was a fixture defect:
    the plugin's window kept its first auto-fit size and clipped the 96 px marker after the
    change to 12 (the image shows a scrollbar). The window now auto-resizes. Runs 13 (variant
    a) and b4 (variant b), both linked with lld: 60 of 60 checks each. S1 has no open finding
    left.
  - Code review of `dec3be1b5`:
    - Codex (fresh session `01a19362-8a98-7ec3-a804-68ff22d5899d`, diff SHA-256 prefix
      `ff2c012fbeb4c892`): REVISE with two Musts and one Should:
      1. `UnregisterSection` given a name owned by the registry erased the next section from the
         config;
      2. a preview held across a registration change dropped or brought back a section on apply;
      3. nested dispatch lacked a test.
    - Fresh Claude ponytail review: Ship, with lean findings:
      - the non-const `SectionRegistry()` still let callers register without either view or the
        guard;
      - the spike's `replace` stage now equals `windows`;
      - a 16-line `Unregister`;
      - a `HINTS` line that never changes the result;
      - two unused includes.
  - Fixes, each with a regression test that failed before it and a mutation that fails it after:
    - the name is copied before the registry erases its storage;
    - apply rejects a preview whose sections no longer match the registry (diagnostic: preview
      again);
    - the nested apply keeps the outer guard;
    - the non-const `SectionRegistry()` is deleted (every other caller only reads);
    - the `replace` stage is gone, and the driver has 58 checks;
    - the lean findings are applied.
  - After the fixes (`build/spike`, lld): 1873 runtime contract and 292 Sandbox editor integration
    tests pass. The spike driver passes 58 of 58 in both variants (runs 15 and b6); its lifecycle
    check now derives the expected `register` count from the failure stages.
  - Codex delta review of `5cc90698e` (same thread): the three findings resolved; REVISE for one
    new Must from the lean cleanup. The one-statement core `Unregister` compared against a
    borrowed name while compaction overwrote it: with "a", a 122-character name and "z", it also
    erased "z". A core regression test reproduced this (one entry left instead of two). The
    predicate now owns the name, and 280 core, 17 config-control and 58 of 58 driver checks pass
    (run 16).
  - Codex delta review of `3ba77576a` (same thread): "APPROVE — reviewed `3ba77576a`." Pushed;
    the CI result is pending.
- 2026-10-31: Plan for Q50 step 5, S2, revision 2. Codex plan review 1 (fresh session
  `01a19388-e743-7a00-9786-5498ee09b6e6`) returned REVISE with six blocking findings, all folded
  in below:
  1. draining must release callables;
  2. a redo entry must cross the reload;
  3. the Register failure bypassed Register;
  4. registry equality cannot see stale callbacks;
  5. the closure control needs attribution;
  6. scene contents, host identity and GPU fences.
  - Questions served:
    - Q36: "Ich will auch Laufzeit Kompilierung damit Agenten etwas ändern kannst du die
      sandbox/engine weiterlaufen kann."
    - Q50: "Ja bitte, mach 1. - 5." (5: S2)
  - Goal item: the target's plugin host ("load, register, unload, reload"), and its rule that the
    registries take out on unload whatever a plugin added, with undo entries holding data or a
    registered command, not plugin closures. S2 is the slice the approved plan names for this. It
    is the shortest path because S1 already loads the spike once; S2 adds the reverse direction
    and the failure paths on the same fixture, without a general plugin host.
  - Pass criteria: the approved S2 list (section "Slices"), unchanged, including the GPU fences.
    Four blocks, each with its own code review; this plan review covers all four.
  - **Callable accounting, used by every block.** Each plugin build has a generation (the version
    from `SpikeVersion.inc`, a generated header in the build directory, so the driver's edits never
    touch repository sources). Every callable the plugin hands to the host captures a token whose
    constructors and destructor report to the host. The kinds are operation, validator, change
    callback, window draw, window state, history handler, job work, job completion, and GPU
    participant.
    - The host keeps live counts per generation and kind.
    - The host refuses to `dlclose` a generation whose counts are not all 0. The unload fails
      with `callables_alive`, and the plugin stays loaded. The counts are recorded at every
      close.
    - Every invocation logs `<kind>:<generation>`, so the driver can assert that after a reload
      only the new generation is called.
  - **S2a, unload and reload (spike host only).**
    - Order: reload = unload, then load. The approved criteria allow "the old plugin running, or
      none", so the candidate is never opened beside the old image.
    - Every load opens a private copy (`<tmp>/spike-<n>.so`), so a rebuilt file loads even if an
      old image stays mapped; the copy is deleted after `dlclose`.
    - `plugin_unload`:
      1. drain (S2c);
      2. the shutdown hook;
      3. the config sections' active payloads are saved in the host, then the operations, sections,
         windows and history handlers are unregistered;
      4. the callable counts are recorded, then `dlclose`.

      Whether the image left `/proc/self/maps` is reported.
    - `plugin_load` restores the saved payloads after its commit through preview and apply, so the
      new registration validates them; a rejected payload keeps the default and is reported.
    - Apply also validates every changed section with its current registration, so a preview held
      across a reload cannot carry a payload the new validator rejects. Its regression test keeps
      the schema id and version and swaps in a stricter validator; the held preview is rejected
      before anything changes.
    - Driver checks, asserted before the first run, in both link variants:
      - Unload: tools, sections and windows equal the pre-load baseline; the plugin's tools
        answer as unknown; the closing generation's live counts are all 0.
      - Reload after an edit:
        - the version is the new one, the registries equal the post-load content, and only the
          new generation is invoked;
        - the marker size 12 set before the reload is active, the new plugin got one
          `OnChanged` with 12, and the marker is 96 px;
        - the scene is seeded with a translated, renamed copy of the reference geometry before
          the first load, and `save_scene` files written before and after are byte-identical
          (values and transforms, not only property metadata);
        - the Sandbox PID and the sha256 of its binary are unchanged across the plugin rebuild;
        - the identity probes still have one address.
      - Failed build (a syntax error in the version header): the build fails, the `.so` is
        unchanged (sha256), and the old version answers.
      - Failed load (a truncated `.so`): `dlopen_failed`, no plugin, a reason, the registries
        equal the baseline.
      - Failed `Register`: a build whose `Register` stages everything and then returns false.
        Result: `register_failed`, no plugin, the staging's live counts reach 0 before its
        `dlclose`, and the registries equal the baseline. The host's `fail_at register`
        short-circuit is removed.
      - Failed commit (`fail_at windows`): no plugin, a reason, the registries equal the
        baseline, and the counts are 0.
      - 20 reload cycles alternating two prebuilt versions, under ASan, UBSan and LSan: each
        cycle checks the version, the registries and the counts. At exit, no sanitizer report
        and no unsuppressed leak (the leak-frame-name check is dropped: unloaded images lose
        attribution).
  - **S2b, undo and redo through the plugin across a reload.**
    - Change: `EditorCommandHistory::RegisterCommand(name, handler)` and
      `UnregisterCommand(name)`, plus `MakeRegisteredCommandRecord(label, name, payload)`.
      - It is defined in the history's implementation unit, so the record's closures and their
        payload strings are host code and host data.
      - Undo and redo look the handler up when called. With no handler they return
        `UnsupportedOperation`, and the entry stays (today's failure path).
      - The plugin's handlers are staged and committed or unregistered like its other
        registrations.
    - Contract tests:
      - a data entry calls the registered handler with its payload;
      - with no handler, `UnsupportedOperation`, and the stacks are unchanged;
      - a handler registered again makes it work;
      - closure entries behave as before.
    - Driver (command form): `spike_counter_add` 5, then 3, then `undo`, so the undo stack holds
      5 and the redo stack holds 3. Reload. Then `undo`, `redo`, `redo`. The host counter takes
      the expected values (5 → after the reload 0, 5, 8), the new generation's handler is the
      one invoked, and no sanitizer report.
    - Control (closure form, a separate driver mode): the same sequence with today's closure
      entries. They carry no token, as today's closures don't, so they neither block the close nor
      enter the zero-count assertion; the control asserts instead that the history still holds
      them. It counts only if:
      - the setup succeeded;
      - the undo entry is listed by `history`;
      - the old image left `/proc/self/maps`;
      - the process then dies with a sanitizer report whose stack contains
        `EditorCommandHistory::Undo`.

      A still-mapped image makes the control inconclusive, and any other failure fails it.
  - **S2c, bounded draining (CPU and GPU).**
    - CPU: `spike_start_job {seconds, ignore_cancel}` submits through a host function that
      records the token and refuses new submissions while draining.
      - Plugin callables stay owned on the main thread: the job's work is a host lambda holding a
        raw pointer to a host-owned entry with the plugin's work and completion. The worker sets
        the entry's `returned` flag in host code after the plugin call has returned. The main
        thread destroys the entry (and with it the plugin callables) only when the job is
        terminal and either it never ran or `returned` is set. Reaping is not relied on: a
        worker may still own the job record, which then holds only host code.
      - Draining cancels the recorded tokens, then waits frame by frame until each entry is
        destroyed that way, its completion or unpublished finalizer having run on the main
        thread; their live counts reach 0.
      - The deadline is 5 s, enforced in code. On timeout, the unload fails with
        `drain_timeout`, submissions are admitted again, and the plugin stays loaded and
        registered.
    - GPU: `spike_gpu_work {frames}` registers a GPU queue participant that records a small
      buffer fill each frame and reports in-flight work until its frames have completed.
      In-flight is computed as the cache does: the last recorded frame has not retired
      (`GlobalFrameNumber < SubmittedFrame + FramesInFlight + 1`). Draining stops the recording
      and polls `HasInFlightWork` frame by frame. Only once it is false is the participant
      unregistered. Its buffer is freed in `ShutdownAfterDeviceIdle`.
    - Wording agreed in the review exchange: CPU draining enforces its deadline while the host
      frame loop progresses. GPU resources and plugin code remain alive until their submissions
      retire. GPU retirement has no plugin-level deadline: a hung GPU can stall the engine through
      its existing frame-fence wait. Bounding that wait or changing device-loss handling is
      outside S2.
    - Driver:
      - a reload with one running job and one queued behind it (more jobs than workers), the
        states asserted before: both end cancelled, their completion events come before
        `shutdown`, the counts are 0, and the reload succeeds;
      - a job that ignores cancellation for 10 s: `drain_timeout`, the old version answers, and a
        submission works again; after the job ends, a reload succeeds;
      - a reload while GPU work is in flight (asserted before): the participant's shutdown event
        comes before `dlclose`, the counts are 0, and the Sandbox log has no Vulkan validation
        error.
  - **S2d, the R4 baseline (full pre-flight, its own packet).**
    - Measured: from writing `SpikeVersion.inc` through
      `cmake --build --target IntrinsicSpikePlugin` and `plugin_reload` to the plugin answering
      the new version.
    - Reported per phase, both variants, repeated runs. Target: under 10 s.
    - The packet fixes the exact commands, repetitions and stop rules: Layer A and B, a manifest,
      a fresh Claude and a fresh Codex review, a start gate, and an HTML report.
  - Codex plan review (thread `01a19388-e743-7a00-9786-5498ee09b6e6`):
    - round 2: findings 2 to 4 resolved, 1 and 6 partly;
    - round 3: one blocker, the GPU deadline (`BeginFrame` waits on frame fences without a
      timeout);
    - one exchange: the reviewer withdrew it. Verbatim: "Yes. I withdraw the blocker. APPROVE the
      plan with the proposed clarification." The clarification is the wording above.
  - Not in S2: a file watcher (the MCP tool is the trigger); a general plugin host outside the
    spike; CI; moving editor operations to registered commands; agent continuations created by
    plugin operations (the spike's operations create none; a general host must drain them).
- 2026-10-31: S2a implemented (unload and reload, spike host only).
  - Runtime:
    - `AgentOperationRegistry::Unregister`, with its name owned (the registry-compaction lesson of
      `3ba77576a`).
    - Apply validates a late section's change again with its current registration
      (stricter-validator regression test).
  - Host:
    - callable accounting per generation and kind (`spike_callable`, `spike_invoked`);
    - private copies;
    - a close only at zero live callables;
    - `plugin_unload` and `plugin_reload`;
    - saved section payloads reapplied through preview and apply;
    - the `fail_at register` short-circuit removed;
    - test hook `retain_callable`.
  - Plugin:
    - a token in every callable;
    - `spike_version`;
    - builds from a generated `SpikeVersion.inc` (generation, a failing Register, an unresolved
      symbol).
  - Driver `reload_driver.py`, written before the first run, results:
    - run 1: 34 of 40, two findings:
      - The planned failed-load fixture, a truncated `.so`, crashes glibc's `dlopen` with SIGBUS
        while it maps the segments, instead of failing. A partially written plugin file would
        therefore kill the host. lld writes its output to a temporary file and renames it, so a
        reload never sees a partial file from lld; the host's private copy is taken from the
        finished file.
      - The failed load now uses a realistic fixture: a build that needs a symbol the host does
        not export (`undefined symbol`, `dlopen_failed`).
      - Unknown tools answer with a JSON-RPC error ("Unknown tool"), not a tool result; the
        check reads that.
    - runs 2 and b1: 57 of 57 in both variants;
    - run 3 adds the guard check: 60 of 60.

    Every closed image left the process (no GNU-unique pinning with clang): 28 static
    constructions, 28 destructions.
  - The S1 driver's lifecycle check is now strict: every failed attempt's image is closed with no
    live callable and unmapped. S1 regression: 57 of 57.
  - Plan deviation: the scene is seeded with an imported test mesh (`import_file`), since no
    rename or translate tool exists. The comparison is `save_scene` byte equality, with a control
    of two saves.
  - S2a mutations (host source mutated, rebuilt, the driver run; each caught by its intended check):
    - close guard disabled: the guard checks;
    - windows kept on unload: the unload checks;
    - restore skipped: the reapplied-payload checks;
    - windows kept on a rollback: the failed-commit checks;
    - staging kept after a failed commit: the S1 strict lifecycle check;
    - staging kept after a failed Register: the failed-Register close check.

    The first batch had four invalid runs. Their output paths made the agent socket path longer
    than AF_UNIX allows (111 to 113 bytes), and the Sandbox logged `InvalidPath` and never
    listened. A timeout looked like a catch. The driver now checks the socket path length as an
    environment condition, and those four mutations were rerun with short paths.
  - Final S2a state (`build/spike` and `build/spike-shared`, lld, dev sanitizers):
    - 280 core, 292 Sandbox editor integration and 1875 runtime contract tests pass;
    - S1 driver: 58 of 58 in both variants;
    - S2a driver: 61 of 61 in both variants.
- 2026-10-31: S2a code review of `43042ab82`.
  - Codex (fresh session `01a193d9-abc1-7782-8896-3a11de669a01`, diff SHA-256 prefix
    `5cd0461b25eaa51e`): REVISE.
    - Musts:
      1. a dropped load continuation left its image open;
      2. apply's re-validation checked only the state, not the canonical payload;
      3. Restore could apply a fallback-changed payload that it reported as rejected.
    - Shoulds:
      4. the scene fixture did not protect transforms;
      5. a missing token passed as zero.
  - Fresh Claude ponytail review: Ship.
    - Lean findings: `Usable()` like preview; the late-section list; a docstring claim; an unused
      field; two driver near-copies; the prebuild order.
    - Noted for the correctness review: on `callables_alive` the host unregisters everything and
      forgets the plugin, while the plan said "the plugin stays loaded".
  - Fixes (they go into the S2b review, per the review rule):
    1. A pending candidate owns its image (`Own`): a dropped or unrunnable continuation closes it.
       Test hook `simulate_drop`: closed, nothing alive, unmapped, copy removed.
    2. Design change for review instead of re-validation: every preview the control makes carries
       its section registry's generation, which `RegisterSection`/`UnregisterSection` advance.
       Apply rejects any other generation. This replaces `SectionsMatchRegistry` and the
       re-validation; unstamped previews count as generation 0. All production previews for
       apply come from the control.
    3. Restore applies only when the preview keeps the saved section unchanged. Fixture: a
       build whose validator clamps 12 to 10 with a fallback; the saved 12 is reported rejected,
       8 stays, and no change callback runs.
    4. The scene is seeded with the imported mesh renamed, moved and scaled through `load_scene`.
       The check asserts the seeded transform, then compares byte-equal saves.
    5. Every callable kind must be counted while loaded and appear at every close (and the copy
       must be gone).

    Ponytail's lean findings are applied; the top-level `live` field stays, because 5 now reads
    it.
  - Deviation, recorded: on `callables_alive` the plugin is unregistered and forgotten, but its
    image stays mapped (leaked), so no callable outlives its code. The plan's "stays loaded" is
    met for the image, not for its registrations.
- 2026-10-31: S2b implemented (undo and redo across a reload).
  - `EditorCommandHistory::RegisterCommand`/`UnregisterCommand`/`MakeRegisteredCommandRecord`
    (contract tests: a data entry calls its handler; without a handler `UnsupportedOperation` and
    unchanged stacks; a handler registered again serves both entries).
  - Spike: history commands staged and committed like other registrations; `RecordCommand` and
    `RecordClosureCommand` let the plugin record entries without naming editor types (R2 stays
    0/0); host state `HostUndoValue`.
  - Driver: add 5, add 3, undo, reload to generation 3, then undo, redo, redo give 0, 5, 8; only
    generation 3's handler ran (three times).
  - Closure control (`--closure-control`): the entry is listed and the image unmapped; then undo
    kills the Sandbox. The control's criterion as first written (pc equal to the fault address)
    did not hold: with UBSan's function check, the caller reads the callee's type hash before the
    jump, so the fault is a READ in the host frame. The criterion is now the plan's (Undo on the
    stack), plus the fault address inside the closed image's former range, recorded by the host
    before `dlclose`. Run ctl2: 12 of 12.
  - Variant (a), run 2: 73 of 73.
  - Variant (b): 73 of 73, control 13 of 13. S1 regression: 58 of 58 in both variants. Suites:
    280 core, 292 integration, 1877 runtime contract tests.
  - Mutations (each caught by its intended check):
    - the history handler cached at record time: the reload fails `callables_alive`, and the
      history checks fail;
    - the validator's token removed: the counted-kind and close checks;
    - Restore without its preview guard: the clamp check;
    - the generation check removed: both held-preview contract tests.

    The dropped-load mutation first went unnoticed: the check read the last close record, which
    was an earlier clean one. It now requires a new record, and the mutation is caught.
- 2026-10-31: S2b review of `6bca0539a`.
  - Codex (delta in thread `01a193d9-…`, diff SHA-256 prefix `b32354b0d15cb4d6`): all five earlier
    findings resolved. The generation stamp, the `callables_alive` deviation (for this spike) and
    the revised control criterion are accepted. REVISE, with three Musts:
    1. the counter accepted deltas whose undo overflows (`-INT_MIN`);
    2. a staged history command with an empty handler passed registration and would terminate
       when called;
    3. the closure control skipped the approved sequence (both stacks, then a reload).
  - Fresh Claude ponytail review: Ship, with lean findings (a comment said three times, a test
    repeating another, `MappedRange` parsing and printing again, a redundant exception type). All
    applied.
  - Fixes:
    1. deltas are bounded to ±1,000,000 and the host value is 64-bit; out-of-range deltas
       (2147483648, -1000001) are rejected with value and history unchanged;
    2. an empty `Apply` fails the commit ("history: command rejected"); build 8 stages a valid
       and an empty command, and the rollback leaves the baseline with nothing alive;
    3. the control now runs the approved sequence.
  - Finding from the full control (run ctl3): reloaded to the same-size build B, the loader mapped B
    into the range A had just left. Undo of today's closure entry then ran B's code in A's place
    and produced the expected values without any error. Only the per-generation accounting showed
    that no handler had run. Value checks alone cannot detect a stale closure; on a reload, today's
    closure entries silently execute whatever code now sits at their address.
  - To make the crash case deterministic, the control reloads to a 16 MiB larger build (generation
    9), which cannot fit the freed range. It asserts that the ranges do not overlap, then undoes:
    a SEGV inside A's former range with `Undo` on the stack (run ctl4: 15 of 15).
  - After the fixes, run in both variants: 82 of 82 driver checks; control 15 of 15 (variant b
    too); S1 58 of 58; 1877 runtime contract tests.
  - Mutations: the delta bound removed fails the range check and the undo/redo values; the
    empty-handler guard removed fails every empty-handler check.
  - Codex delta of `dd49e901a`: "APPROVE — commit `dd49e901a`." One wording note: "cannot
    overflow" overstated; it now says an overflow is impractical. S2a and S2b are pushed.
- 2026-11-01: S2c implemented (bounded draining, CPU and GPU), as planned, except:
  - The contract takes a `Runtime::GpuQueueParticipantDesc` for GPU work. The host forwards its
    callbacks and gates recording on draining. The plugin finds the device through `Services()`
    and reports its own retirement by the cache's rule; it imports RHI directly (`plugins/` is
    outside `src` layering), and R2 stays 0/0.
  - The ignoring job runs 12 s, not 10 s.
  - Test hooks: `drain_deadline_ms` and `simulate_gpu_stall` on unload and reload.
  - A job entry is released once finalized. The planned `returned` flag was dropped, since the
    service finalizes only after `Work` returned or for a job that never started. Codex confirmed
    this, and the ponytail review found the flag could not change the result.
  - `app_quit` drains too, before the shutdown hook.
  - Driver, variant (a), run 2: 94 of 94.
    - 66 jobs (31 running, 35 queued) are all cancelled, each completion before the shutdown hook.
    - A job that ignores cancellation: `drain_timeout` after 5.04 s; the old version answers and
      admission is restored; after the job ended, a reload succeeds.
    - GPU work in flight: its shutdown ran before the plugin's; Vulkan validation enabled, 0 errors.
    - Run 1 failed one check on a stale reference (the snapshot predated the clamp step's 8),
      not on the code.
  - Variant (b): 94 of 94; control 15 of 15 in both variants; S1 58 of 58 in both.
  - Mutations, each caught by its intended check:
    - no cancellation: the drain checks;
    - no deadline: the timeout checks;
    - admission not restored: the admission check;
    - entries reaped before their completion: a use-after-free kills the Sandbox.
  - Finding, from the mutation "unregister the GPU work without waiting for retirement": it first
    went unnoticed. `VulkanDevice::DestroyBuffer` already defers the release (`DeferDelete`)
    until the frame retires, so an early free is harmless here. The wait guards callbacks that
    fire at retirement (readbacks into plugin code), which the spike's GPU work does not have.
    The host now logs whether the work was still in flight when it unregistered, and the driver
    requires "retired". With that check the mutation is caught; run 3: 95 of 95.
- 2026-11-01: S2c review of `4e3a00ea5`.
  - Codex (fresh session `01a1944d-c439-7ed3-98a7-254980416870`, diff SHA-256 prefix
    `0bcce28041eddcaf`): REVISE.
    - Musts:
      1. a dropped drain continuation left admission blocked;
      2. `app_quit` ran the shutdown hook before outstanding work settled;
      3. a drain could succeed after its deadline.
    - Shoulds:
      4. the GPU checks trusted the same predicate they test, and missed the timeout restoration;
      5. the planned `{frames}` argument was missing.
  - Fresh Claude ponytail review: fix 2 and 3 first:
    - the `Started`/`Returned` flags cannot change the result;
    - `Work`'s bool was thrown away;
    - `Stopping` duplicated `g_Draining`;
    - `PluginGpuWork` duplicated `GpuQueueParticipantDesc`;
    - two shared values for one fact;
    - the log restated the plan.
  - Fixes:
    1. the reservation's end restores admission; checked by a disconnect during a drain and a
       reconnect;
    2. `app_quit` drains first; a quit with a job and GPU work outstanding settles both before the
       shutdown hook;
    3. the deadline is checked before success; checked deterministically with a 0 ms budget;
    4. retirement is checked from the plugin's own frame numbers, logged at its GPU shutdown;
       a simulated stall times out, and recording resumes;
    5. `{frames}` records exactly that many frames.

    All ponytail findings are applied. Variant (a), run 5: 102 of 102.
  - Variant (b): 102 of 102. Control 15 of 15 and S1 58 of 58 in both variants.
  - Mutations, each caught by its intended check:
    - the reservation not restoring admission: the dropped-drain check;
    - success checked before the deadline: the 0 ms check;
    - quit without a drain: the quit-order check;
    - a retirement predicate one frame short: the frame-number check;
    - `frames` ignored: the three-frames check.

    Not mutation-checkable: removing the explicit admission reset on timeout, since the
    reservation's end restores it as well.
  - Codex delta of `caea4cc34` (same thread): findings 1, 3, 4 and 5 resolved and the
    simplifications sound; REVISE on finding 2. On a timeout the quit still ran the shutdown hook,
    and after a successful quit a later request could still reach the plugin. Fix: a quit whose
    drain times out fails with `drain_timeout` and does not exit. A drained quit unloads the
    plugin like `plugin_unload` (shutdown hook, all registrations out, close) before the exit.
  - Checks:
    - a quit with a job that ignores cancellation fails, and the plugin keeps running;
    - a job request sent right behind a quit was dispatched in the quit's frame, before its drain
      began, so it was admitted and then drained before the shutdown hook. After the hook, no job
      event follows. A request dispatched after the quit finds the plugin unloaded.
  - Run 8: 104 of 104 (variant b, run b3: 104 of 104); S1 (now expecting `unload` at quit): 58 of
    58 in both variants.
  - Mutations: quitting on timeout is caught by the quit checks; quitting without the unload is
    caught by the quit-order check (and by S1).
  - Codex delta of `afec3a26e`: the quit fix holds; REVISE on one new Must. `app_quit` reserved
    without checking `g_LoadPending`, so a quit overlapping a reload could unload twice. Fix:
    `app_quit` refuses with `load_pending` like the other lifecycle operations. Check: a quit sent
    while a reload drains is refused, and the reload completes. Run 9: 105 of 105 (variant b:
    105 of 105); S1 58 of 58 in both. The mutation without the check fails the overlap check.
  - Codex delta of `b43916988`: "APPROVE — commit `b43916988`." S2c is approved.
- 2026-11-01: S2d packet (R4 baseline, full pre-flight) prepared in
  `tasks/evidence/ARCH-022/preflight-r4/`. Layer A and Layer B are there; Layer B opens with Q36,
  Q46 and Q50 verbatim.
  - Code:
    - `plugins/spike/r4_driver.py`: a sample is the save, the plugin build, `plugin_reload`, and
      `spike_version` answering the new generation; plus null probes, load records, a host-binary
      validity check, and a report;
    - `r4_report.py`: pools the sessions per variant;
    - `r4_selftest.py`: known answers; each of its four mechanisms is shown to fail the test when
      removed;
    - `tasks/evidence/ARCH-022/r4_run.sh`: the plan, ABBA over the two link variants, 1 warmup and
      5 measured samples per session.
  - Technical tests t1 to t3 passed their checks. No timing was read.
- 2026-11-01: S2d, the R4 baseline, measured under the full pre-flight.
  - Packet digest `1445006114b8…a5fb1f35` at `2b26c6d4f`: Claude and Codex PASS after four rounds
    (`tasks/evidence/ARCH-022/preflight-r4/reviews/README.md`).
  - Run 01:28 to 01:32 UTC, exit 0. All 40 sample checks and 24 probes passed.
  - Results (median total, n = 10 per variant):
    - shared engine libraries: 4.49 s (max 4.50);
    - static engine: 9.74 s (max 9.85);
    - both `met` (under 10 s).
  - Surprise: in the static variant, the spike's host-module collection (`CollectHostModules.cmake`) costs 5.4 s on
    every build; it was predicted under 1 s. The plugin compile is 3.9 s, the reload 0.2 s.
  - The audit recomputed every summary from the raw samples.
  - Report: `docs/reports/2026-11-01-arch-022-r4-baseline.md`. The target's R4 cell holds it as the spike-fixture
    baseline; a typical plugin stays open.
  - S2 is complete: S2a to S2c approved, S2d measured.
