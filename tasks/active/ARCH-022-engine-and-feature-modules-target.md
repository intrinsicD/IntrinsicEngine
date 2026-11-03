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
51. (2026-11-01) "Zu 1. welche vor und Nachteile hat statisch oder Shared? Was empfiehlst du hier?
    2. ja ich würde gerne anfangen mit refactoring und Plugins bauen, damit die Engine aufgeräumt und
    sauber wird, runtime und editor auch. 3. ist das sinnvoll?" (1: the link variant; 2: the next
    step; 3: lld in CI)
52. (2026-11-01) "Ja, ja uns ja, das nächste Mal wenn ci eh gemacht werden muss, nicht extra
    vorher." (yes to shared libraries for the dev builds, CI and release static for now; yes to the
    order: 1. the plugin host as a module service, 2. a first real feature as a plugin, 3. feature by
    feature, 4. S3 before the GPU-heavy features; yes to measuring CI's link share, the next time CI
    has to change anyway, not as a separate step)
53. (2026-11-01) "Ja bitte. Plane bilateral Filter als erstes Plugin. Außerdem fixe die offenen Sachen
    die du fixen kannst. Wenn der Plan steht implementier bitte das Plugin und teste es via Live
    sandbox und auch mcp" (after the report on slice 3 step 1)
54. (2026-11-01, answers to two questions on the step 2 plan) "Volle Parität (Recommended)" (the plugin's
    runs keep today's behaviour: rows of the Jobs window and cancellable there, several runs on different outputs,
    MCP waits for the result and can cancel; the contract grows for it) and "Eigene Plugin-Tools (Recommended)"
    (the plugin registers its own MCP tools; the `run_operation` row goes)
55. (2026-11-02) "Kannst du bitte weiter machen?"
56. (2026-11-02) "Kannst du bitte weiter machen?" (again, after a reset of the workstation)
57. (2026-11-02) "kannst du mir kurz zwischendrin nochmal sagen welche terminal commandos ich heute (montag) an
    meinem rechner hier ausführen sollte?" (answered: `sudo apt update`, `sudo apt install lld-24`, Q49)
58. (2026-11-02) "btw lld-24 ist nun installiert"
59. (2026-11-02) "Der Rechner hängt, kannst du die Tests beenden und rausfinden was da los ist?"
60. (2026-11-02) "ja bitte setze 1. um" (1: a Ninja compile pool sized from the RAM)
61. (2026-11-02) "ja, bitte mach 1 - 3." (1: amend the plugin commit with the two pinned references, including
    `ara/logic/claims.md`; 2: the compile pool as its own commit; 3: replay ci-docs and push)
62. (2026-11-02) "während du baust: wenn ich in der Apperance UI Faces:Color auf v:normal setze bekomme ich bei
    Color interpretation Normal direction und Components. Was ist dieses Components? das sieht seltsam aus auf dem
    mesh..." (answered: Components uses the values as RGB in 0..1, so negative normal components turn black)
63. (2026-11-02) "danke. ist ci fertig?" / "Was gehen wir dann nach ci an?"
64. (2026-11-02) "ja bitte" (the cleanup block: the own-or-borrowed workspace logic once, one test harness, no
    extern "C" forwarders; then kernel density and point spacing as plugins; then S3)
65. (2026-11-02) "wenn du fertig bist bitte nach main mergen und pushen"
66. (2026-11-02) "Ok gut. Dann mach das bitte so" (next: kernel density and point spacing as plugins; the point-cloud
    editing service learns scalar outputs; the general plugin harness with the second plugin)
67. (2026-11-02, answer to: kernel density and point spacing run on the shared operation runtime with a GPU-resident
    preview and accept, so per Q52 they come after S3; how to go on?) "FPFH-Deskriptoren (Recommended)" (next plugin:
    FPFH descriptors, the same inputs as the bilateral filter, CPU and Vulkan neighbourhoods (radius, not kNN as
    stated then), no GPU preview; the service learns
    scalar outputs with several columns; the general plugin harness; then S3; then the kernel density, point
    spacing, density weight and outlier family on the job form)
68. (2026-11-02, answer to: the plugin host holds one plugin, the bilateral filter takes it at startup; for FPFH as
    the second plugin it must hold several; how to go on?) "Erst Multi-Plugin-Host (Recommended)" (a step of its
    own before FPFH: several plugins by name, load, unload, reload and list; drain, jobs and deferred results per
    plugin; several startup plugins; tools take a plugin name and need none while one is loaded; then FPFH)
69. (2026-11-03) "Bildschirm kann ich erst später wieder anschalten. Beim Template, was empfiehlst du wegen des
    shutdown hooks?" (answered: every successful Register gets exactly one shutdown hook before the image closes,
    committed or not; a Register that returns false cleans up itself)
70. (2026-11-03) "Die Regel finde ich gut. Dann fixe auch den Spike Treiber. Setze den Template Schritt so um."
71. (2026-11-03) "Ja, bitte S3. Und was ist mit ponytail Vorschlag?" (the open ponytail item: the picker test's
    ImGui setup through the repository's frame scope; done first, then S3)
72. (2026-11-03) "Warum wird zwischen Spike und nicht Spike unterscheiden?" (answered: the slice-2 plan from before
    Q51/Q52 defined S1 to S3 as one spike, ending in a report and keep-or-delete; after Q51/Q52 the host and the
    feature plugins became ordinary work, only S3 kept the old definition) "Ja bitte, Spike soll raus. Ich will einfach
    nur Plugins. Was war jetzt das nächste was ansteht?" (decided: no spike category for S3; its answers are recorded
    as results here, the CPD plugin is an ordinary plugin)
73. (2026-11-03) "ja bitte, 1 - 3. ausserdem ist der bildschirm nun eingeschaltet. plugins/spike soll eigentlich
    verschwinden, von mir aus plugins/fixture" (1: the CPD plugin without spike, world-space capture, an optional
    normal input; 2: S3c, k-means as a step run and the stall measurement; 3: kernel density and point spacing as
    plugins, then density weight and outliers; the S1 test plugin becomes plugins/fixture; S2 and R4 can run now)

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
- 2026-11-01: Operator decisions Q51/Q52: shared libraries for the dev builds; the order of the next steps
  (plugin host, a first feature, features one by one, S3 before the GPU-heavy features); CI's link share
  measured at the next CI change. Recorded in the target ("Decisions and open points").
- 2026-11-01: Plan for slice 3, step 1: the plugin host as a module service, revision 2 (amended after round 2:
  GPU shutdown order, deferred operation results, the contract digest). Codex plan review 1
  (fresh session `01a19679-254f-7371-95a2-03dbac6ac621`) returned REVISE with four blocking findings, answered
  in "Revision 2" below:
  1. ordinary shutdown was unspecified;
  2. a static Sandbox's behaviour was undefined;
  3. test instruments and lifetime enforcement were conflated;
  4. "v1" needed a bounded compatibility contract.
  - Questions served:
    - Q51: "ja ich würde gerne anfangen mit refactoring und Plugins bauen, damit die Engine aufgeräumt
      und sauber wird, runtime und editor auch";
    - Q52: "Ja, ja uns ja" (shared dev builds, the order, the deferred CI measurement);
    - through them Q36: plugins that register themselves, run-time compilation, changes are cheap.
  - Goal item: the target's module service 7 ("plugin host: load, register, unload, reload"). It
    answers the open point 3, "which process hosts the plugins": the engine runtime, wired by the
    Sandbox.
  - Why first: every later step moves a feature into a plugin, and a plugin needs a host that is not
    a spike behind an option. The spike's host code is reviewed and tested (S1 to S3 of slice 2's
    reload part), so this step moves it into place rather than writing it anew. The slice 2 plan said
    the spike code "either becomes the start of slice 3 or is deleted".
  - **1a, dev presets shared (its own commit, first).**
    - `dev` and `dev-cuda` set `BUILD_SHARED_LIBS=ON` and `CMAKE_POSITION_INDEPENDENT_CODE=ON`; the
      `ci*` presets do not.
    - `Test.SanitizerPresets.py` pins both: the dev presets shared, no CI preset shared.
    - Verification: a fresh tree with `--preset dev` builds every default target (Sandbox and tests)
      and passes the CPU test set (`ctest`), and the Sandbox starts (the S1 driver). The operator's
      `build/dev` needs one fresh `cmake --preset dev` and a full build.
  - **1b, the plugin host in the runtime.** The spike's host code moves from
    `src/app/Sandbox/Sandbox.PluginSpike.*` to `src/runtime/Services/`, split by job:
    - `Extrinsic.Runtime.PluginContract`: what a plugin may use. That is the registrations (config
      sections, operations, windows, history commands), the entry symbols with a version
      (`intrinsic_plugin_register_v1`, `intrinsic_plugin_shutdown_v1`), and the host functions for
      jobs, GPU work and history records. The lifecycle-event and callable-accounting hooks stay;
      they are test instruments a plugin may call.
    - `Extrinsic.Runtime.PluginHost`: an `IRuntimeModule` that provides the service and holds the
      host's behaviour from S2:
      - private copies;
      - the drain with its deadline and GPU retirement;
      - the close guard;
      - saved config payloads;
      - registered history commands;
      - the reservation that serializes lifecycle calls, quit included.

      Its MCP tools are `plugin_load`, `plugin_unload`, `plugin_reload` and `plugin_list`, plus the
      test hooks the drivers use, registered through a function the Sandbox calls (as the spike
      does today).
    - The host core takes an opened image through a small interface (register, shutdown, close).
      Contract tests can then drive commit, rollback, unload, the close guard, the drain (with the real
      `JobService`), config restore and history commands with in-process fixtures, without `dlopen`.
      These run in CI, which stays static. `dlopen` itself stays covered by the drivers on the shared
      dev build.
    - The Sandbox always composes the plugin host; `INTRINSIC_PLUGIN_SPIKE` and
      `Sandbox.PluginSpike` are deleted.
    - `plugins/` builds when the engine is shared (dev). Static builds (CI) build no plugin, and the
      variant (a) module-collection path (`CollectHostModules.cmake`, 5.4 s per build) is deleted.
    - `plugins/spike` stays as the fixture plugin and its drivers stay its tests. It links `ExtrinsicRuntime`
      directly instead of `ExtrinsicSandboxEditor`. R2 stays 0/0.
    - R2 counts a plugin's direct imports only; `AgentOperations` still exposes editor types through its
      context. 0/0 is the boundary this step reaches, not full editor independence; later steps narrow it.
  - **Revision 2:**
    - **Ordinary exit (finding 1).** `Engine::BeginShutdown` already shuts the GPU queue participants down,
      the plugin's included. `Engine::Shutdown` then runs `JobService::CancelAndDrain`, so every job
      completion and unpublished finalizer runs, the plugin's included. It then runs each module's
      `OnShutdown` in reverse composition order, and only then shuts the renderer and device down. The
      document history belongs to `SceneDocumentModule`; the shutdown announcement clears its entries, and
      registered handlers stay until removed.
      - The Sandbox composes the plugin host last, so it shuts down first, while the agent server, config
        control, UI module and document history it unregisters from are still alive.
      - Its `OnShutdown`: calls the plugin's shutdown hook; removes every registration; releases the GPU
        work entry (its participant was already shut down) and the finalized job entries; and leaves the
        image mapped. The process ends, so a callable destroyed later still finds its code.
      - `app_quit` only requests the exit; window close and quit take the same path. Pending lifecycle
        continuations are dropped by the agent server, and their reservation restores admission.
      - Tests:
        - a contract test composes the host with an in-process fixture that holds registrations, a
          running job and GPU work, shuts the engine down, and checks the order: the GPU participant's
          shutdown first, then the job completion, then the shutdown hook; no fixture callable invoked
          after it; every registration removed;
        - the drivers' exit checks run on the shared build.
    - **Static Sandbox (finding 2).** In a static build, `plugin_load` and `plugin_reload` answer
      `plugins_unsupported` ("plugins need the shared engine build"); the capability is a compile-time
      definition from `BUILD_SHARED_LIBS`. A CI contract test checks that answer. Deleting the variant (a)
      path drops plugin loading into a static executable, which no agreed workflow uses; a release that
      wants plugins builds shared. Recorded in the target.
    - **Lifetime versus diagnostics (finding 3).**
      - Required, enforced by the host: every plugin callable the host holds lives in a host registry
        entry (registrations, jobs, GPU work, history commands). The host removes and destroys all of
        them before a close, and the contract states that a plugin hands callables to the host through
        these APIs only.
      - Deferred operation results: the agent server moves an outcome's `Continuation`, `Progress` and
        `Cancel` into its own pending-call storage, beyond the reach of an unregistration. v1 therefore
        rejects deferred plugin operations. The host wraps each plugin operation, and an outcome carrying
        any of the three is answered `deferred_unsupported`, with those callables destroyed at once while
        the image is mapped. A contract test checks this without diagnostic counts: an in-process fixture
        operation returns a continuation that holds a shared object, and the test checks the reply and
        that the object was released.
      - Diagnostic: the callable counts. A plugin may report them; the host then also refuses a close
        while one is alive. The fixture always reports and tests the structural rule with them.
      - Fault injection stays fixture-only. `fail_at`, `simulate_drop`, `retain_callable`,
        `drain_deadline_ms`, `simulate_gpu_stall` and the closure-form history record (the S2b negative
        control) are active only when the Sandbox starts with `--plugin-test-hooks`, which the drivers
        pass. Without the flag the hook arguments are rejected and the closure form returns false.
    - **Compatibility (finding 4).** v1 means the same build and the same exchanged contract.
      - The same build: the same toolchain, standard library, build type, sanitizers and shared-engine
        setting.
      - The same contract: a sha256 over every module interface file (`.cppm`) under `src/`, sorted by
        path. A hand-kept list of boundary files misses transitive layouts: `GpuQueueParticipantDesc`
        holds a `WorldHandle`, defined through `Runtime.WorldHandle.cppm` and `Core.StrongHandle.cppm`.
        Hashing every interface is stricter than needed: any engine interface edit changes the id. But a
        plugin compiled against older interfaces is unsafe anyway, and the dev build recompiles it after
        such an edit.
      - A build-time custom command (a `cmake -P` script) computes the hash into a generated header. It
        depends on the files from a `CONFIGURE_DEPENDS` glob, so an edit or a new interface file changes
        the id and rebuilds both sides.
      - A tooling test runs the script on a temporary copy before and after changing a nested file (a
        `StrongHandle`-like layout), and the id must change.
      - The plugin exports `intrinsic_plugin_build_id_v1`. The host refuses a different id
        (`abi_mismatch`) or missing v1 entry points (`dlsym_failed`, naming the symbol).
      - Fixture builds check both refusals, among them a plugin built with a stale id (an older interface
        state) under otherwise identical settings. There is no compatibility layer beyond that.
    - **Named risks for 1a:**
      - imported static vcpkg archives may need PIC;
      - singletons (ImGui, ImPlot and others) must keep one identity across the shared libraries
        (S1's identity probes cover ImGui, ImPlot and three host statics);
      - sanitizer linkage and the private copy's run path (the plugin's `RUNPATH` is absolute in the
        build tree, which S2's copies in `/tmp` relied on);
      - `dev-cuda` is not built on this machine; its preset gets the same settings, unverified.
      - The operator's `build/dev` is kept; one fresh `cmake --preset dev` reconfigures it.
  - **Done when:**
    - the three drivers pass against the real host on the shared dev build: S1, S2 with its
      control, and a technical R4 run (pass/fail only);
    - the new contract tests pass and are mutation-checked;
    - the ratchet, layering, module inventory and the whole ci-docs replay pass;
    - the target's "shape of a plugin" names the real contract and answers the spike's three open
      questions from S1 and S2;
    - a short spike report records the answers, the link variant and the history-entry form (slice
      2's done criterion).
  - Not in step 1:
    - moving any feature (step 2);
    - buttons calling commands;
    - S3;
    - CI changes;
    - renaming `plugins/spike`.
  - Codex plan review (thread `01a19679-254f-7371-95a2-03dbac6ac621`):
    - round 2: findings 1 and 2 resolved;
    - round 3: the digest missed transitive layouts;
    - round 4: "**Verdict: APPROVE.** The amended plan resolves the remaining digest-coverage finding."
- 2026-11-01: Slice 3 step 1a, the dev presets shared.
  - `dev` and `dev-cuda` set `BUILD_SHARED_LIBS`, `CMAKE_POSITION_INDEPENDENT_CODE` and
    `CMAKE_EXPORT_COMPILE_COMMANDS`; no `ci*` preset is shared (`Test.SanitizerPresets.py`).
  - The first CPU run on a fresh shared tree had 97 failures, from two causes:
    - 96 `*CompilationLocality` tests read `compile_commands.json`, which no dev preset wrote (since
      2026-07-18 only the `ci*` presets export it). They fail on a static dev tree too. The dev presets
      now export it; `base` stays without it, as `Test.WorkflowConcurrency.py` requires.
    - `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` counts `glfwTerminate` through
      `--wrap`, which reaches only calls linked into its executable; a shared `ExtrinsicPlatform`
      calls it from its own library (`terminate_calls=0`). The test is registered in static builds only
      (checked: registered in a static configure, absent in the shared one). CI is static and keeps it.
  - Verification on a fresh `build/dev-shared` (`--preset dev`, lld 24): every default target builds
    (`cmake --build`, 13 engine libraries, 54 executables; a second build has no work); the CPU set
    (`-LE "gpu|vulkan|slow|flaky-quarantine"`) passes 6060 of 6060 in 4 min 43 s. The Sandbox starts on
    the shared build and answers `diagnostics_read`. The S1 driver needs the plugin host's `app_quit` and
    runs in 1b.
  - Unverified in shared builds: GLFW's exactly-once static teardown (the gated test). The Vulkan shutdown
    contract keeps the synthetic leak control but is a GPU test.
  - Codex review (thread `01a196af-9473-7932-8322-e834a5e8e8ad`): REVISE, one blocking finding: every
    default target must build, not only the CPU tests and the Sandbox (23 were unbuilt). Answered above.
    Non-blocking: the preset test read only a literal "ON"; it now also reads a JSON bool, TRUE, YES, Y and
    non-zero numbers (a CI preset with JSON `true` fails it). Delta: "Verdict: APPROVE", bound to the diff
    sha256 `62dbd624…40b3e3`, with two notes answered here (54, not 55, executables; non-zero numbers).
  - The operator's `build/dev` needs one fresh `cmake --preset dev` and a full build.
- 2026-11-01: Slice 3 step 1b, the plugin host in the runtime (as planned, revision 2), except:
  - The module's name is `~Runtime.PluginHost`. The engine registers modules in name order and shuts them down
    in reverse (`Engine::RegisterRuntimeModulesForBoot` sorts them), so the plan's "composed last" is a name
    that sorts after every `Runtime.` module. The shutdown-order contract test found it; the drivers could not
    (the services stay alive as objects until the engine is destroyed).
  - `plugin_host_identity` is a test-hook tool; `app_quit` only requests the exit.
  - The test instruments are `intrinsic_plugin_event`, `intrinsic_plugin_callable`, `intrinsic_plugin_invoked`
    and `INTRINSIC_PLUGIN_EVENT_LOG`; the probes live in `Extrinsic::Runtime::PluginProbes`.
  - Code: `src/runtime/Services/Runtime.PluginContract.*` and `Runtime.PluginHost.*`; `cmake/PluginBuildId.cmake`
    (the build id); the fixture plugin links `ExtrinsicRuntime`; deleted: `INTRINSIC_PLUGIN_SPIKE`,
    `Sandbox.PluginSpike.*`, `plugins/spike/CollectHostModules.cmake`.
  - Reviews of the staged diff `cbad98e4…8739253c`:
    - ponytail (fresh Claude subagent): "Verdict: Ship", no must-fix; applied: the report no longer repeats the
      verification (it links here), the default opener is no longer exported, the shared image is built in one
      step, the rollback reuses `Unregister`, redundant lines and the fixture's duplicate deferred-result check
      (the contract test covers it) are gone, a misplaced CMake comment moved;
    - Codex (fresh session `01a196e8-886c-7492-b7c5-e0c4f904a888`): REVISE, four blocking findings, answered:
      1. the id lacked the configuration's flags, the definitions and the standard library: it now hashes the
         compiler (id, version, path), the libstdc++ file the compiler resolves, `CMAKE_CXX_FLAGS` with the
         configuration's flags, `INTRINSIC_COMPILE_FLAGS` and the runtime target's compile definitions;
      2. removing an interface left the id stale: the sorted interface list is written at configure time (only
         when it changes) and is a dependency of the id. The wiring is the function `intrinsic_add_plugin_build_id`
         in `cmake/PluginBuildId.cmake`, tested in a small Ninja project (removal, `_GLIBCXX_DEBUG` in the Debug
         flags, a compile definition; each test fails with the old wiring);
      3. a static `plugin_reload` answered `not_loaded`: load and reload now answer `plugins_unsupported` first
         (the contract test covers both; the mutation fails it in the static tree);
      4. evidence: the closure control and the check outputs, below.
      Non-blocking, answered: the deferred-result test covers continuation, progress and cancel (a mutation of
      each predicate fails it); the stale-id fixture's artificial id is recorded as a limitation in the report;
      the R4 runs on stale trees were three (below).
  - Verification after the fixes, on `build/dev-shared` after a full build: S1 60 of 60, S2 116 of 116, the S2b
    closure control 17 of 17, a technical R4 run 13 of 13 (pass/fail only); 10 contract tests and
    `PluginBuildId.Script` (8 cases); 12 host mutations each fail their test; the CPU set passes (below).
  - A static tree (`--preset dev -DBUILD_SHARED_LIBS=OFF`) builds the Sandbox and the runtime contract tests; the
    plugin tests pass there, load and reload answer `plugins_unsupported`, no plugin is built.
  - Technical R4 runs on stale trees failed (`r4-tech1`, `r4-tech3`, `r4-tech4`); on current trees they pass
    (`r4-tech2`, `r4-r2`). Cause and safeguard: the lesson "A build tree a driver uses holds only what the driver
    measures" in `docs/lessons/engineering.md`; the driver ends a session after a failed sample without `app_quit`
    (recorded in the spike report).
  - Docs: the target's "shape of a plugin" names the contract and answers the spike's three questions; the
    [spike report](../../docs/reports/2026-11-01-arch-022-plugin-host-spike.md); the module inventory.
  - Checks on HEAD plus the staged change, on a clean worktree: the whole ci-docs replay (23 `run:` blocks, the
    module inventory's regeneration among them; 2 skipped need the GitHub context or package installs), the R1/R2
    ratchet (`plugins/spike` 0/0), `check_layering.py --root src --strict` and `check_compiler_hazards.py`: all
    pass.
  - The CPU set after the fixes: 6070 of 6071; the one failure is an unrelated timing flake, filed as `BUG-256`.
  - Codex delta (same thread): "Verdict: APPROVE", bound to the staged diff sha256 `a35a7e72…0effc41a`. Ponytail
    delta: "Verdict: Ship"; its two optional code cuts (a second static check in `Load`, a settings test the wiring
    test covers) are not applied, so the approved code stays as reviewed; its log note is applied here.
- 2026-11-01: Open points of step 1 fixed (Q53, "fixe die offenen Sachen die du fixen kannst"):
  - `BUG-256` (the Jobs window test's race), retired;
  - GLFW's exactly-once teardown is tested in shared builds again: the probe interposes `glfwTerminate` (the shared
    `ExtrinsicPlatform` calls it through its PLT) instead of `--wrap`, which static builds keep; it passes in the
    shared and the static tree, and fails in the shared tree without the counter;
  - the R4 driver sends `app_quit` after a failed sample, so the exit checks still judge a clean exit;
  - the ponytail review's optional cuts: `plugin_load` no longer checks for a static build before the opener
    does; the settings-only build-id test is gone (the wiring test covers it).
  - Not fixed here, decisions for the operator: dlopen in CI (CI is static; a shared CI job is a CI change); R4
    for a typical plugin (a measurement with its own pre-flight); the dev presets' `compile_commands.json`
    (reverses the decision of 2026-07-18; kept unless the operator objects).
  - Codex review of the fix block (session `01a1973c-80b4-7881-8c35-e6573a58ff0e`): REVISE on `f47ccb8c…cc415d84`
    for `BUG-256`'s record only; delta "Verdict: APPROVE" on `1e23b24d…3d46d5c26` (the record, and an early-failure
    guard in the Jobs window test).
  - `main` was red after `bfb76bb39`: the CPU test selection refused the new Python CTest test. Fixed in
    `e2813a899` (allowlisted; reviewed by Codex, APPROVE), and `ci-linux-clang` is green again.
- 2026-11-01: Plan for slice 3, step 2: the bilateral point filter as the first feature plugin, revision 1.
  - Questions served:
    - Q53: "Ja bitte. Plane bilateral Filter als erstes Plugin. Außerdem fixe die offenen Sachen die du fixen
      kannst. Wenn der Plan steht implementier bitte das Plugin und teste es via Live sandbox und auch mcp";
    - Q52's order, item 2: "a first real feature as a plugin, through every layer";
    - Q51: "Plugins bauen, damit die Engine aufgeräumt und sauber wird, runtime und editor auch".
  - Goal items: the target's "Plugins: what goes where" and R1, R2, R3, R5. Measured here: R2 for
    `plugins/bilateral_filter` is 0/0; R1 loses the bilateral filter in its hub files; R5 keeps the feature
    reachable over MCP.
  - Today (map of 2026-11-01): about 940 lines of runtime and app code are bilateral-only. They are spread over
    10 hub files: the editor operation `Runtime.PointSetOperations.Bilateral.cpp` (656 lines), its interface part,
    a result slot and a catalog entry; the config module and part of a shared codec; one row of the agent's
    `run_operation` table; and a Sandbox panel. The algorithm stays in `Geometry.PointCloud.Utils`. The operation
    uses no shared transaction runtime, but it depends on editor helpers that 24 files share: point capture,
    output-revision guards and history rebasing, the spatial index and the GPU kNN paging. The plugin contract
    lacks four things it needs:
    1. a job gate for GPU paging;
    2. undo data for large arrays;
    3. a result an agent can wait for;
    4. editor context for the window.
  - Shape: the plugin holds everything bilateral-specific. One new module service, the point-cloud editing
    service, wraps the shared editor helpers for plugins, so the plugin imports no editor module. It is the
    service the next point plugins need too (KDE, spacing, outliers, normals).
  - **2a, the host: startup plugins and a job gate.**
    - The Sandbox opens its built-in plugins before the config boot:
      - a shared build opens `plugins/lib<Name>Plugin.so` of its own build tree;
      - a static build (CI, release) registers the same code linked into the executable, through an
        in-process image (the `PluginImage` seam of step 1).
    - `StagePlugin(image)` calls Register. The plugin's config sections join the boot section registry, so a
      saved `engine.json` payload loads as today. The host commits the rest at `OnResolve` (operations, windows,
      history commands) and owns the sections, so unload and reload work as in step 1.
    - The host holds one plugin at a time. The fixture's drivers start the Sandbox with `--no-startup-plugins`.
      Several plugins at once come with the second feature plugin.
    - `PluginJob` gains an optional `IsReadyToApply`, a main-thread gate the host forwards like the GPU
      forwarders, before Completion. A chain submits its next job from a completion.
    - Tests: startup staging (the section is active at boot with a file payload; a startup plugin unloads and
      reloads); the gate (Completion waits for it; a drain cancels a gated job).
  - **2b, the service `Extrinsic.Runtime.PointCloudEditing`** (`src/runtime/Services/`): a runtime module that
    provides `PointCloudEditingService`.
    - Its interface names only foundation and service types. Its implementation wraps today's editor helpers
      unchanged (`CapturePointNormalInput`, the property watches and history rebasing, `AcquirePointIndex`,
      `GpuRowPages`, `SpatialIndexWorkspace`), through its own workspace attachment, as the agent server does.
    - Calls, with data in and data out; it holds no plugin callable:
      - `Capture`: an entity, and position, normal and output refs. The live points, normals and slot count, or
        a diagnostic. Resolves unknown domains and checks the refs, as today's capture does.
      - `Current`: inputs and output unchanged since the capture.
      - `Publish`: writes new positions into the output for the live slots, records one undo entry, rebases the
        watches and marks the entity dirty. The entry holds the before and after arrays in host code, so undo
        and redo work after the plugin is unloaded or reloaded.
      - `InputCatalog`: the point property catalog for the window.
      - kNN rows: CPU rows from the cached LBVH; GPU rows paged (Begin, then Advance until Ready or Failed),
        including the private index over moving points from the second pass on.
    - Tests: the cases of `Test.BilateralFilterOperations` that concern input, staleness and history, through
      the service (every domain, copy and in place, a changed input refused, undo and redo).
  - **2c, the plugin `plugins/bilateral_filter/`:**
    - Config: the section keeps its name and schema id (`sandbox.bilateral_filter`), so saved configs load. The
      codec is derived from rows (`Extrinsic.Runtime.DerivedConfigSections`) instead of the hand-written codec.
    - Run: capture, then CPU octree, CPU LBVH or Vulkan LBVH rows through the service, then the algorithm (stays in
      `Geometry.PointCloud.Utils`), then publish. One run at a time; jobs through `SubmitPluginJob`, GPU pages
      behind the gate; the last result is kept by the plugin.
    - MCP tools (operations answer at once):
      - `bilateral_filter_run`: submits a run with the section's settings and answers with its id;
      - `bilateral_filter_preview`: readiness and the reason;
      - `bilateral_filter_status`: the run's state and result.
    - Window `view.bilateral_filter`, plain ImGui:
      - the entity from the selection service;
      - property pickers from the service catalog;
      - the section's fields;
      - Run calls the run command;
      - the last result.
    - Build: one object library of the plugin's sources. Shared builds wrap it in a MODULE with the v1 entry
      points; static builds link it into the Sandbox. Tests link it in both.
    - Deleted:
      - the editor operation and its interface, result slot and catalog entry;
      - the `run_operation` row (`bilateral_filter`) and its schema fixture entries;
      - the panel and its windows;
      - the central config registration;
      - the runtime config module and the codec part.
      Tests move with the code.
  - **2d, live verification (Q53),** a driver `plugins/bilateral_filter/live_driver.py` on the shared dev
    build, over MCP:
    - the Sandbox starts with the plugin: `plugin_list`, its tools, its window and its section with the
      defaults;
    - `import_file` of a point cloud with normals; `config_apply` sets the entity and settings; preview; a run
      on each backend; status until done;
    - `property_values` and `property_stats` show the output moved by the reported displacement. The three
      backends agree, since they compute the same neighborhoods;
    - undo restores the output and redo reapplies it, also after a `plugin_reload`;
    - a capture of the window shows the panel;
    - a clean exit (sanitizers).
    Plus the static build: the contract tests pass through the in-process image.
  - Gaps kept for later (named, not hidden):
    - plugin runs are not rows of the Jobs window (job observation is S3);
    - MCP waits by polling `bilateral_filter_status` (no deferred plugin results);
    - several plugins at once;
    - R4 for this plugin (a measurement with its own pre-flight, for the operator).
  - Done when:
    - 2a to 2c pass their contract tests in the shared and static trees, and the CPU set;
    - the live driver passes on the shared dev build;
    - R2 for `plugins/bilateral_filter` is 0/0, and R1 has dropped by the bilateral filter;
    - the reviews pass;
    - the target and `docs/architecture/bilateral-point-filter.md` are updated.
- 2026-11-02: Plan for slice 3, step 2, revision 2. Codex plan review 1 (fresh session
  `01a1973c-80af-7353-83fc-08edff1eac21`): REVISE, five blocking findings, answered here together with the
  operator's decisions Q54. Revision 1 stands except:
  - **Parity (finding 1, Q54 "Volle Parität").** The contract grows by what parity needs, and no more:
    - `PluginJob` gains:
      - `Label`;
      - an optional `Output` (stable entity id and property ref);
      - `Run`, the handle of an earlier job to join its run;
      - `IsReadyToApply`.

      `SubmitPluginJob` returns a `PluginJobHandle` (invalid when refused). The host submits through the editor's
      job surface with an editor job identity built from these fields. So a run is a row of the Jobs window,
      cancellable there, and admission refuses a second run on the same output only.
    - Deferred plugin results: the host keeps an operation outcome's `Continuation`, `Progress` and `Cancel` in a
      host entry and gives the agent server host forwarders. An unload or a drain destroys the plugin's
      callables, and a pending call then answers `plugin_unloaded`. This replaces step 1's
      `deferred_unsupported` and keeps the rule that every plugin callable lives in a host entry.
    - MCP (Q54 "Eigene Plugin-Tools"): `bilateral_filter` runs and waits for the result (cancellable, with
      progress), as `run_operation` did. `bilateral_filter_preview` gives readiness. The `run_operation` row goes.
  - **Without an agent server (finding 2).** A commit without an operation registry registers no tools; config,
    windows and history commands are unaffected. The window's Run calls the plugin's own command function with a
    context built from the services. Tests: a startup plugin composed without an agent server (shared harness and
    static tree), with the window's Run pressed through ImGui (as the Jobs window test does).
  - **The run's failure contract (finding 3).** A run is one state machine:
    - each stage checks staleness and cancellation;
    - a failed gate (failed GPU rows), a stale capture, a cancellation or a refused next submission stops the
      run and publishes nothing;
    - the run ends with exactly one terminal status (published, failed with a reason, cancelled), which the
      waiting MCP call and the window read.

    Tests: a cancellation between stages, a refused continuation (a drain during the chain), a gated job drained,
    a failed GPU page.
  - **Live verification with known answers (finding 4).** The driver writes a fixed fixture: a seeded noisy
    plane of 400 points with normals. It computes the expected answer independently in Python: a brute-force kNN
    including self, and the update rule of `Geometry.PointCloud.Utils` (written out in the driver from the
    implementation). The tolerance is 1e-4.
    - Controls:
      - zero iterations leaves the output equal to the input;
      - one iteration matches the reference, with the reported displacement compared per pass;
      - three iterations match a three-pass reference on each backend, and the GPU runs use 64-query pages, so
        several pages;
      - each run reports the requested and actual backend, the completed iterations and GPU batches above zero
        for Vulkan;
      - unrelated properties are unchanged;
      - undo and redo restore the values exactly, while the plugin is unloaded and after a reload;
      - a cancelled run publishes nothing.
    - The polling of every wait is bounded. The window is checked in a capture; its Run is covered by the ImGui
      test of finding 2.
  - **R1 measured honestly (finding 5).** `check_feature_boundaries.py` adds the features that moved into
    plugins (the plugin folder names under `plugins/`, the fixture excluded) to its vocabulary, so a bilateral
    reference left in `src/runtime` or `src/app` still counts. A regression test plants one. The report gives the
    bilateral references removed per hub file apart from R1's file count. The R2 baseline gains
    `plugins/bilateral_filter` (0/0) and loses `src/runtime/Modules/BilateralFilter`.
  - Non-blocking, taken:
    - the service's readiness capture copies no values (as today's preview);
    - undo and redo are tested while the plugin is unloaded;
    - the bilateral assertions of `Test.SandboxEditorSessionLifecycle.cpp`
      (readiness, queued jobs, cancellation) move to the plugin's tests;
    - static builds keep the feature through the in-process registration, while loading and reloading stay
      `plugins_unsupported` there.
  - Cuts, taken: the service carries only what the bilateral filter needs; the live driver reuses the fixture
    driver's process and socket helpers; `--no-startup-plugins` goes into the common launcher.
- 2026-11-02: Plan for slice 3, step 2, revision 3. Codex delta on revision 2 (same session): REVISE, three
  findings, answered:
  1. **One job owner for every entry point.** Editor job identities, outcomes and cancel records live per
     workspace attachment, and the Jobs window shows the Sandbox shell's. The Sandbox therefore gives the plugin
     host and the point-cloud editing service its shell's attachment once it is attached (a host and a service
     setter). Every plugin run, whether from its window or over MCP, is submitted, admitted and cancelled
     through that one session, and it is used for the service's captures and history too, as the panel does
     today. Without a shell (tests, a headless engine) the host and the service attach their own.
     - New over today: a run started over MCP is a Jobs window row too (today MCP runs live in the agent
       server's own session).
     - Tests (an integration test composing the shell's Jobs window and the plugin's static form):
       - start over the plugin's command and cancel in the Jobs window;
       - start from the window and cancel over the MCP call;
       - the same output refused across both, different outputs concurrent.
  2. **Outcomes reach the Jobs window.**
     - `PluginJob::Completion` returns the publication verdict (`bool(bool applied)`). The host's
       `PublishCompletion` returns it, so a stale or failed publication is not reported as published.
     - `PluginJob` names its requested domain (`PluginJobDomain`: CPU, GPU); the host maps it to the editor job
       identity.
     - `ReportPluginJobOutcome(handle, resolved domain, diagnostic)` forwards to the surface's `ReportOutcome`.
     - `CancelPluginJob(handle)` cancels through the same surface.
     - Tests: a failed GPU page and a stale publication show failed or stale, with the backend and the reason,
       in the Jobs window's records and in the MCP reply.
  3. **Deferred results' lifetime.**
     - Each forwarder (continuation, progress, cancel) shares one host entry holding the plugin's callables. The
       host keeps only a weak reference per plugin.
     - When the agent server drops the call (completion, disconnect, reconnect, a minimized-window failure,
       `DropPending`), the forwarders and with them the entry and the plugin's callables are destroyed, then
       and there, while the image is mapped.
     - An unload clears every live entry of the plugin before its close, so its callables die first. A
       surviving forwarder then answers `plugin_unloaded` (continuation), an empty progress and an empty
       cancel, and never reaches a later generation, whose entries are new. A drain timeout keeps the plugin
       and its entries. The ordinary exit clears them like an unload.
     - Tests, replacing step 1's `deferred_unsupported` test (all three forwarders alive in each):
       - completion releases the callables;
       - a cancel runs the plugin's cancel once;
       - a dropped call releases them;
       - an unload with a pending call destroys them before the close, and the forwarder answers
         `plugin_unloaded`;
       - a reload: the old forwarder does not reach the new generation;
       - a drain timeout keeps the call working.
  - Codex delta on revision 3 (session `01a1973c-80af-7353-83fc-08edff1eac21`): "Verdict: APPROVE for revision 3,
    together with revisions 1 and 2 as amended. Blocking findings: none."
- 2026-11-02: Step 2a implemented (the host: parity contract, held deferred results, startup plugins), as planned
  in revision 3, except:
  - the host's own workspace is attached in `OnResolve` and detached in `OnShutdown` (the agent server's pattern);
    detaching in its destructor touched the already destroyed scene document module;
  - the host prepares a workspace frame per call only for its own workspace; the app's shell prepares one every
    frame;
  - the contract's job output carries no presentation semantic (the module would pull heavy imports into every
    plugin); the host uses the displacement semantic, as the bilateral filter does today.
  - Verification on `build/dev-shared`: 20 plugin host contract tests (the deferred-result lifecycle, job rows and
    verdicts, admission per output, run cancel, the gate, startup plugins with and without an agent server);
    8 mutations of the new mechanisms each fail their test; S1 60 of 60, S2 116 of 116.
  - Codex review of step 2a (fresh session `01a19b2d-b290-70b1-a8c9-8cc86323aaef`): REVISE, five blocking findings,
    answered (each test fails with its fix removed):
    1. a refused publication was finalized a second time: the finalizer returns when Completion already ran;
    2. a deferred call that unloads its own plugin could destroy the running callable: the host counts the plugin
       code it is running, and an unload or reload waits a frame while it is above zero;
    3. a startup plugin whose commit failed without a UI host was closed with its boot sections registered: they
       are unregistered first, and without config control the image stays mapped;
    4. `CancelPluginJob` of a later stage cancelled that job only: the host resolves the stage's run first;
    5. the drain cancelled through `JobService` directly: it cancels the runs through the workspace (the session
       records the request), then the jobs.
    Non-blocking, taken: the harness shuts the engine down while its shell is attached; a test submits through
    the host's own workspace. 25 plugin host contract tests pass.
  - Codex delta 1 (same session): REVISE, one finding: a job's completion (or gate) could unload its own plugin while
    running. The host counts every main-thread plugin call it forwards (gate, completion, finalizer, deferred
    callables); a test unloads from a completion, and the unload waits. 26 plugin host tests pass.
  - Codex delta 2 (same session): "Verdict: APPROVE", bound to the staged diff sha256 `9b47b4cc…1d05775d`; gate and
    finalizer reentrancy checked by inspection, the publication guard by a test.
- 2026-11-02: Steps 2b and 2c implemented (uncommitted until review): the bilateral point filter is the first real
  plugin.
  - 2b, module service `PointCloudEditingService` (`src/runtime/Services/Runtime.PointCloudEditing.*`): capture of
    positions and normals on all eight point domains, the cached CPU index, paged GPU kNN rows, publication with one
    undo entry of host code and host data (so undo and redo work after an unload). The Sandbox shell hands its
    workspace to the host and the service (`UseEditorWorkspace`, `ReleaseEditorWorkspace` before it detaches).
  - 2c, `plugins/bilateral_filter`: section `sandbox.bilateral_filter` (name and schema id kept, saved configs load),
    tools `bilateral_filter` (waits, cancellable over MCP) and `bilateral_filter_preview`, window
    `view.bilateral_filter`, CPU runs as one plugin job and the Vulkan chain as stages of one run. The Sandbox stages
    it before the config boot (shared: the `.so` beside the executable, reloadable; static: linked, in-process);
    `--no-startup-plugins` skips it (the spike drivers pass it). The built-in filter is removed: its operation,
    config codec, window, editor session slot, catalog entry and `run_operation` row.
  - Deviations, recorded: the mesh/graph/point-cloud Processing menus no longer list it (their entries were Sandbox
    redirects); `bilateral_filter` sends no progress notifications; plugin rows belong to the shell's session, so
    the agent's `jobs_cancel` refuses them (MCP cancels through `notifications/cancelled`, the Jobs window through
    its Cancel); the built-in filter's eight-domain Vulkan smoke is not ported (the plugin's own Vulkan smoke
    replaces its stale/cancel cases), so benchmark `geometry.point_lbvh.bilateral_runtime_smoke` has no producer
    (open).
  - R1/R2: plugin folder names count as features for R1 (regression test, fails with the change removed); R1 ceiling
    31 -> 30; R2 `plugins/bilateral_filter` 0/0 (its tests live under `tests/`, as the layout requires).
  - Verification on `build/dev-shared`: `Test.BilateralFilterPlugin.cpp` (7 tests: section, MCP tool and Jobs row,
    all domains with undo and agreeing CPU backends, stale and cancelled runs, Jobs-window cancel, preflight, the
    window's Run button), `Test.PointCloudEditing.cpp` (4), the ported Sandbox tests; the CPU set (ctest, non-GPU)
    passes except a benchmark smoke that timed out under load and passes alone. Live MCP driver
    `plugins/bilateral_filter/live_driver.py`: see the run below.
  - Live MCP driver `plugins/bilateral_filter/live_driver.py` on the first revision (`build/live-bilateral-094630`):
    49 of 49 checks. Reference error 3e-8 (one pass) and 7e-8 (three passes) against a float64 reference; CPU LBVH
    and Vulkan LBVH equal to the octree run; 15 Vulkan query batches of 64 over three passes; MCP cancel ended a
    run within the pass in flight (18.0 s; one pass on 50k points takes 17.6 s in this ASan build); undo/redo while
    unloaded and after reload; clean, sanitizer-clean exit.
  - Ponytail review (Claude subagent, diff sha256 `634ca195…80ec05`): "Verdict: Ship after 1 and 2 (1 and 3 can also
    go into the next block)." Taken: 2 (a Vulkan test, below), 5 (backend names from one table), 6 (one
    `FindService`), 7 (unused view fields and stored pointers). Deferred to the next block, as the reviewer allows:
    1 (the own-or-borrowed workspace logic copied in host and service), 3 (the two test harnesses), 4 (the
    extern "C" forwarders).
  - Codex review (fresh session `01a19b72-c851-70e3-9f97-65815573276b`, same diff): "Verdict: REVISE", four blocking
    findings, answered (each new test fails with its fix removed, except 1):
    1. finished runs kept their buffers, index lease and GPU workspace: `Finish` releases them; the callbacks that
       could still run afterwards return early (by inspection, no observable signal without a test hook);
    2. the window allowed one run at a time: it keeps one row per running run, the host refuses the same output
       (contract test with two outputs and a refused third; window test runs twice while the first is pending);
    3. the Vulkan chain had no failure tests: `BilateralFilterPluginGpuSmoke` (agreement and paging, stale before
       GPU recording, cancel between stages). It found a bug: inputs changed under the gated stage ended the run
       `failed`; the chain now ends `stale`;
    4. (and its non-blocking 5) the history rebasing case was dropped: the service guards the output with the
       shared history slot (`CapturePointPropertyHistory`, stamped on every write); successive in-place runs undo
       in turn (test; it fails without the stamp).
  - CI on 4b1202da1 was red: `PluginHost.ADrainTimeoutKeepsTheLoadedPluginsDeferredCallWorking` unloaded before its
    stubborn job had started (a queued job is cancelled cleanly, nothing to drain). The test now waits for the job
    to run (100 repeats pass; the race was not reproduced locally, the CI log is the evidence).
  - Codex delta 1 (same session, diff sha256 `d928e687…4237d5`): "Verdict: REVISE", two findings, answered:
    1. `= {}` kept the vectors' capacity: `Finish` move-assigns empty vectors;
    2. the GPU cancel test matched the stale run's finished neighbourhood job: it now waits for this run's active
       neighbourhood stage (`Identity.Run` equal to the run's token); with the neighbourhood completion ignoring
       the cancel, the test fails.
    Closed per the reviewer: window concurrency, the shared history guard, the drain-timeout test.
  - Live MCP driver on the revision (`build/live-bilateral-102856`): 49 of 49. One earlier attempt was cut short by
    a window close from the desktop ("Window close requested … source=platform-event"; exit 0, sanitizer clean).
  - Codex delta 2 (same session): "Verdict: APPROVE", bound to diff sha256 `c67d4af3…2132de`; "Tests and mutation
    checks were not rerun; those results remain author-reported. Previously disclosed fault-injection coverage gaps
    remain."
  - Open after 2c: port the eight-domain Vulkan benchmark (`geometry.point_lbvh.bilateral_runtime_smoke` has no
    producer); a failed GPU page with current inputs has no test (no fault hook); ponytail 1, 3, 4 (next block).
- 2026-11-02, Q58–Q60: `build/dev` and `build/dev-shared` link with `/usr/bin/ld.lld-24`. Reconfiguring `build/dev`
  with the `dev` preset made the static tree shared and started a full rebuild with 34 parallel ASan compiles; the
  workstation ran out of memory and hung (an OOM kill at 10:19 had already forced a reset earlier the same morning).
  Measured: a heavy compile peaks at 3.4 GB, the largest lld link at 1.7 GB. Fix (Q60): Ninja compile pool, one
  compile per 4 GiB of RAM (`cmake/CompilePool.cmake`, 15 here), `Test.CompilePool.py` (three mutations fail it),
  `docs/build-troubleshooting.md`, lesson in `docs/lessons/engineering.md`. `build/dev` is now shared; a static
  check needs its own tree.
  - Codex review of the compile pool (fresh session `01a19bb9-5f22-7a20-9414-108792efa51b`, diff sha256
    `cec370ab…729c84`): "Verdict: APPROVE", no blocking findings. Taken: the test's description limited to what the
    pool does; a `shortcut:` comment that a container's memory limit is not read. Open: GitHub's 16 GB runners report
    about 15,980 MiB, so CI gets 3 compiles (Ninja's default was 6 on 4 cores); the CI job time is compared after the
    push.
- 2026-11-02, CI on `399b3931d`: ci-docs and ci-linux-clang green. The runner reports 15,982 MiB, so 3 compiles;
  `full-cpu` 37.4 min (build 32.2 min) against 42.4 min (35.0 min) on `616cdf17e` without the pool: no slowdown
  seen (two runs, ccache hit rates vary).
- 2026-11-02, plan of the cleanup block (Q64), the three items the ponytail review of 2c deferred. Goal: the plugin
  template is copied without its duplicates. Done when the three are gone and the 2c tests, the GPU smoke and the
  live driver pass unchanged; this is a refactoring, so no behaviour changes and no new tests beyond moved ones.
  1. **The workspace slot once.** `PluginHostModule` and `PointCloudEditingService` each hold an own workspace, a
     current pointer, `Use`/`Release`, the per-call frame of the own workspace, attach at resolve and detach at
     shutdown. One small class `EditorWorkspaceSlot` (`src/runtime/Services/Runtime.EditorWorkspaceSlot.*`) holds
     it; both services keep their `UseEditorWorkspace`/`ReleaseEditorWorkspace` (one line each), so the shell is
     unchanged. Not chosen: the service asking the host for its commands (it would tie the service to the plugin
     host; the service tests compose it without one).
  2. **One test harness.** `tests/support/PointCloudEditingHarness.hpp` composes the engine as the Sandbox does
     (modules, shell, `Frame`, a generic `Settled`, the sample grid); `BilateralFilterHarness.hpp` adds the staged
     plugin and the host. `Test.PointCloudEditing.cpp` and the plugin tests use them.
  3. **No named extern "C" forwarders.** `BilateralFilter.Entry.cpp` calls `Register`/`Shutdown` of the module;
     the static Sandbox imports the module under `INTRINSIC_BILATERAL_FILTER_IN_PROCESS` and the test harness
     imports it, both passing `&Register`/`&Shutdown`.
  Verification: the plugin, service and host contract tests and `BilateralFilterPluginGpuSmoke` on `build/dev`
  (shared), the plugin tests and the in-process Sandbox on a static tree (`build/ci-vulkan`), the live driver
  (it unloads and loads the `.so`, so it runs the new entry points), layering, hazards, R1/R2, ci-docs replay.
- 2026-11-02, the cleanup block (Q64) implemented as planned:
  1. `EditorWorkspaceSlot` (`src/runtime/Services/Runtime.EditorWorkspaceSlot.*`) holds the own-or-lent workspace;
     the plugin host and the editing service use it (their `UseEditorWorkspace`/`ReleaseEditorWorkspace` forward).
  2. `tests/support/PointCloudEditingHarness.hpp` composes the engine; `BilateralFilterHarness.hpp` derives from
     `Boot` (registry, boot sections, staged plugin; built first, destroyed last) and from it.
     `Test.PointCloudEditing.cpp` and the plugin tests share the sample grid; the old copies are gone (jscpd: four
     clones of 6 to 11 lines remain inside test cases of the two suites, left as they test different layers).
  3. The named extern "C" forwarders are gone; `Entry.cpp`, the static Sandbox (import under
     `INTRINSIC_BILATERAL_FILTER_IN_PROCESS`) and the harness pass `Register`/`Shutdown` of the module.
  - Codex plan review (fresh session `01a19c70-129a-7d82-9262-47bf49dd3072`): "Verdict: REVISE"; answered: the
    destruction order through the `Boot` base, `Frame(60)` where the service tests advanced 60 frames, and the
    verification of both build kinds and the `ci` preset.
  - Verification: dev-shared 97 contract tests (plugin, service, host, point readiness, queued point jobs) and 2
    `BilateralFilterPluginGpuSmoke`; `ci-vulkan` (static, ASan, Sandbox) 94 of them and the static Sandbox
    registers the plugin in-process (`plugin_list` file "", tools, section, exit 0); `ci` (static, unsanitized) 97;
    live MCP driver 49 of 49 (unloads and loads the `.so`, so the new entry points ran); layering, hazards, R1/R2,
    module inventory clean. Every build ran under a memory guard; at least 30 GB stayed free.
  - Codex code review (same session, diff sha256 `611fc004…31c98b`): "Verdict: APPROVE"; its non-blocking note
    (the harness's `Settled` previewed twice) taken.
  - Ponytail review (Claude subagent, same diff): "Verdict: Ship." Taken: 1 (the unused `ui` option of the
    harnesses), 2 (a test of the slot's release fallback, `PointCloudEditing.AReleasedWorkspaceFallsBackToTheServicesOwn`;
    it fails when `Release` keeps the detached workspace), 3 (`Settled` without the optional round trip), 4 (the
    one-line `JobCommands`/`Commands` renames, the detach lambda, unused includes). Deferred: 5, a general plugin
    harness (`Boot`, the host hook, `Call`, `Jobs`), when the second plugin's harness is written. These fixes go
    into the next block's review.
  - After them: dev-shared 98 contract tests and 2 Vulkan smoke tests, `ci-vulkan` 98, `ci` 98, the static Sandbox
    still registers the plugin in-process.
- 2026-11-02, plan of step 2d (Q67): the FPFH descriptor analysis as the second plugin, `plugins/descriptor_analysis`.
  Goal: the second feature leaves the editor; the plugin template, the editing service and the test harness serve a
  second plugin. Done when the built-in descriptor analysis is gone, the plugin reproduces it (golden parity), and
  its tests, the GPU smoke and a live MCP run pass. Inventory: an Explore pass over every reference (operation,
  codec, session slots, agent row, Sandbox window and redirects, 7 R1 hub files, boundary tests, 8 test files, docs,
  ARA C87, method and benchmark manifests).
  - **Golden, recorded first** on `60de92325` (built-in, over MCP, `build/descriptor-golden-150556`): a 300-point
    cloud with normals, automatic radius, the 33 columns of `cpu_kdtree`, `cpu_lbvh` and `vulkan_lbvh` (identical to
    each other; every 11-bin block sums to 100). The plugin must reproduce them exactly.
  1. **Config: an array of property references in derived sections.** The section keeps its name, schema id and
     payload (`outputs`: exactly 33 scalar references). `Bind` binds a `std::array<GeometryPropertyRef, N>` member
     to an `Array` row whose item is a `PropertyRef`; the derived codec encodes, decodes and validates it with the
     existing array and reference checks; the cross-field rule (distinct names, not an input, the input domain)
     is the section's `Check`. Not chosen: keeping the runtime codec (a plugin may not import a feature module: R2
     starts at 0) or a prefix instead of 33 names (breaks saved configs).
  2. **The editing service learns** (all host code, reused by later point plugins):
     - several scalar outputs per capture (`ScalarOutputs` beside the vec3 `Output`): checked as the operation
       checks them today, snapshotted at capture, published as one undo entry through `PublishPointScalarField`
       (atomic over all columns, shared history guards);
     - zero normals reported in the view (FPFH refuses them, the bilateral filter keeps them);
     - GPU radius rows paged with a capacity and the lowest-ID cap (`AdvanceGpuRadiusRows` over
       `AdvancePointRadiusRows`); CPU radius rows come from the cached index the service already hands out;
     - `ShowProperty(entity, property)`: the Color binding `show_property` uses, for the window's "Show histogram
       bin".
  3. **The plugin** (as the bilateral filter): section, tools `descriptor_analysis` (waits, cancellable) and
     `descriptor_analysis_preview`, window `view.descriptor_analysis` (inputs, the 33 names with "Name all 33
     bins", backend and limits, Run and Cancel per running run, the display-bin slider and "Show histogram bin",
     the result). Runs: CPU k-d tree and CPU LBVH one job each; Vulkan: scale, then gated GPU radius rows, then the
     histograms; terminal state released at the end.
  4. **The built-in feature removed**: operation, config module and its codec block, result slot and sinks, agent
     row, Sandbox registration, window and redirects, CMake and boundary-test entries. Shared tests keep their
     intent: the point-readiness fixture's second oriented consumer becomes a service capture that refuses zero
     normals (the role-specific verdicts stay tested); the queued-job contract keeps its other operations; config,
     schema, panel, presentation and agent lists lose the feature. ARA C87 pins its removed proofs to `36a7b50`.
  5. **One plugin harness** (the deferred ponytail item): `tests/support/PluginHarness.hpp` holds the boot, the
     host, `Call` and `Jobs` for any plugin's `Register`/`Shutdown`; the bilateral and descriptor harnesses keep
     only their own helpers.
  6. **Tests**: the operation's contract tests ported to the plugin (round trip and refusals, every domain with
     history and deleted rows, all backends agree, stale, cancel and changed outputs, zero-normal and preflight
     refusals, double columns, atomic multi-output publication); the window (Run, the display-bin slider follows
     only after Show); a Vulkan smoke (agreement, stale before recording, cancel between stages, radius-capacity
     overflow) that writes the existing benchmark's JSON, so `geometry.point_lbvh.descriptor_runtime_smoke` keeps a
     producer; each new test fails with its mechanism removed. A live MCP driver: the golden for all three backends,
     MCP cancel, undo and redo across unload and reload.
  Verification: dev-shared, `ci-vulkan` (static, Sandbox), `ci`; GPU smoke; live driver; gates; ci-docs replay.
  Builds run one at a time under the memory guard.
  - Codex plan review (fresh session `01a19c9a-b996-7d53-9c94-7f00b1e330aa`): "Verdict: REVISE", four blocking
    findings; revision 2 of the plan answers them:
    1. (finding 2) The array row validates every item before decoding: its length equals the bound array's,
       each item is a canonical reference of the item's kind (`AnyScalar` kept: double and checked integer columns
       stay valid), an unknown domain is refused; the plugin's `Check` refuses duplicate names, an output named
       like an input and outputs or normals on another domain. Tests: 32 and 34 items, wrong kinds, unknown
       domains, duplicates, input names, a normal domain mismatch, and a **saved section of the built-in codec**
       (`tests/data/descriptor_analysis_saved_section.json`: `config_get` of the built-in at `60de92325`, custom
       names with spaces and slashes, float and double columns, non-default limits) that the plugin loads at boot
       unchanged.
    2. (finding 3) The built-in's Vulkan smoke cases move to the plugin's Vulkan smoke: eight domains with
       deleted slots, cold, warm and index reuse, the lowest-ID cap with a dense radius, radius-capacity overflow,
       stale inputs, cancel between stages, and a later stage refused (the host refuses submissions while an
       unload drains): the run fails or is cancelled once, outputs and history unchanged.
    3. (finding 4) Stages are sequential: a later stage is submitted from the previous stage's completion, so a
       refused submission leaves no earlier job running; the host's drain keeps the image mapped until accepted
       jobs settle, and `Finish` releases the run's state once. Test: unload during a running chain (the run ends
       once, nothing written, the unload completes), then reload and run again.
    4. (finding 7) The benchmark keeps its contract: the eight-domain requests through publication with the
       manifest's parameters, warmup and measured iteration, `INTRINSIC_DESCRIPTOR_BENCHMARK_OUTPUT`, its
       diagnostics, now written by the plugin's Vulkan smoke.
    Non-blocking taken: `PublishPointScalarField` reused as is with its preparation-failure and last-column guard
    tests ported; R1 lowered only to its measured value; the shared-library load/reload and the static Sandbox's
    registration checked for real (live driver, static check).
  - 2026-11-02, while planning the plugin: the host holds one plugin (`std::optional<LoadedPlugin>`, `already_loaded`
    on a second load); Q68 puts a multi-plugin host first. Done so far for 2d, uncommitted, kept for its block: the
    derived array row with its test, and the service's scalar outputs, radius rows, zero-normal flag and Show.
- 2026-11-02, plan of step 2d-0 (Q68): the plugin host holds several plugins. Goal: a second feature plugin can be
  loaded beside the bilateral filter, each loaded, unloaded and reloaded on its own. Done when two in-process
  plugins and a shared plugin beside a startup plugin load, run jobs, drain and unload independently; S1, S2 and R4
  pass; the 2c tests and the live driver pass.
  1. **A name per plugin.** `PluginRegistrations` gains `Name` (required, non-empty, unique among loaded plugins;
     a second load of a loaded name answers `already_loaded`). The bilateral filter is `bilateral_filter`, the spike
     `spike`, the fixtures name themselves. It changes the contract's layout, so the build id changes with it.
  2. **A slot per plugin** holds what the single plugin held: the loaded image and its registrations, its jobs, its
     GPU work, its drain state, its deferred results and the count of its code running on the main thread. Slots
     are held by pointer, so the host's callbacks into a slot stay valid; a slot is removed with its plugin.
  3. **The calling plugin is known.** Every host entry into plugin code enters the plugin's slot (counts it as
     running and makes it the caller): tool invocations, deferred continuations, progress and cancel, job gates,
     completions and finalizers, GPU recording, and newly window drawing, history commands and config-section
     callbacks (validate, changed). `SubmitPluginJob`, `CancelPluginJob`, `ReportPluginJobOutcome` and
     `StartPluginGpuWork` act for the caller; called from outside any plugin call (a test's own code) they act for
     the only loaded plugin, and refuse when several are loaded.
  4. **Tools by name.** `plugin_load {path}`; `plugin_unload {plugin?}` and `plugin_reload {path, plugin?}` name the
     plugin, optional while one is loaded (`not_loaded` without, `invalid_params` naming the loaded ones when
     several are and none is named); one load, unload or reload at a time (`load_pending`), as today. A drain covers
     the named plugin's jobs and GPU work only; the other plugins keep running. `plugin_list` answers
     `{"loaded": [ {name, generation, file, operations, config_sections, windows, history_commands} ]}`;
     `plugin_host_identity` keeps `loaded` and `loaded_range` for the single loaded plugin (null otherwise) and adds
     `plugins`.
  5. **Several startup plugins.** `PluginHostOptions::Startup` is a list, committed in order at resolve; one that
     fails is logged and closed without touching the others. The Sandbox stages its built-in plugins from one list.
  6. Limitation kept and documented: the test hooks' callable accounting counts by generation number, so two loaded
     plugins with the same generation share counts; a shared count can only refuse a close (the image stays
     mapped), never allow one.
  Tests (each fails with its mechanism removed): two plugins registered at once, unloading one by name keeps the
  other's tools, sections, windows and jobs; unload without a name refused while two are loaded; a duplicate name
  refused; a drain of one plugin leaves the other's running job alone; a job submitted from a window draw belongs to
  that window's plugin; two startup plugins, one failing; deferred results of the remaining plugin survive the
  other's unload. The existing 27 host tests pass with named fixtures. Drivers: S1's `plugin_list` check reads the
  list.
  - Codex plan review (fresh session `01a19cc4-ff2c-75f0-a7ff-137605acea1e`): "Verdict: REVISE", six blocking findings.
    Revision 2 of the plan:
    1. **Slot lifetime.** A slot is a `shared_ptr` the host lists; every host forwarder into plugin code (tool
       wrapper, deferred entry, job callbacks, GPU participant, window, history command, section callbacks) shares
       the slot or a host entry and checks it before calling: an unload empties the plugin's callables in its
       entries (they die while the image is mapped) and marks the slot closed, so a copy the job service, agent
       server, UI host or history still holds calls nothing. The host's list drops the slot; forwarders keep it
       alive as inert host data. Test: after a plugin's unload its GPU and window forwarders, invoked again, call
       nothing.
    2. **Every entry into plugin code enters the slot** (counted as running, the slot is the caller): the above,
       all four GPU callbacks and the host's own `HasInFlightWork` probe, `Register` and the shutdown hook. A
       caller stack per thread restores the previous caller on return (A, then B, then A). Contract calls are
       main-thread only: from another thread (a job's `Work`) they are refused; during `Register` and the shutdown
       hook submissions are refused; from outside any plugin call (a test's own code) they act for the only loaded
       plugin and are refused when several are loaded.
    3. **Ownership.** `CancelPluginJob`, `ReportPluginJobOutcome` and a job joining a run (`PluginJob::Run`) accept
       only the caller's own jobs; the output exclusion stays global (two plugins cannot write one output at once).
    4. **Startup isolation before the boot.** One host function stages the startup images into the boot section
       registry: a duplicate plugin name, or a section that does not register, drops that plugin (its own inserted
       sections removed again, its image closed) and keeps the others; `SectionNames` lists only inserted sections;
       the section callbacks are wrapped at staging, before the boot calls them. The Sandbox and the tests use it.
       A later commit failure removes only that plugin's sections.
    5. **Drivers.** S1 reads an empty `loaded` list; the bilateral live driver selects `bilateral_filter` by name and
       gains a coexistence scenario: the shared spike plugin loaded beside the startup bilateral filter, its tool
       called, the spike unloaded and reloaded by name, the bilateral filter reloaded by name while the spike is
       loaded, both working after each step. S2 and R4 keep their single-plugin runs (no startup plugins).
    6. **Reload identity.** `plugin_reload {plugin, path}` requires the new build to carry the same name: a
       different one is refused (`name_mismatch`) and, as any failure after the unload, leaves that plugin unloaded;
       other plugins are untouched.
    Non-blocking taken: the build id needs no extra machinery; the generation limitation gets a test (two plugins
    with one generation: a live callable of one refuses the other's close) and keeps the retention control.
    Tests added for 1 to 4 and 6: forwarders inert after unload, nested attribution, a worker-thread call refused,
    cross-plugin cancel, report and run joining refused, a drain timeout of one plugin leaving the other's admission
    intact, duplicate startup names and section collisions, a name mismatch on reload.
- 2026-11-02, step 2d-0 implemented (plan revision 2): `PluginSlot` per plugin (shared, listed by the host); every
  host entry into plugin code goes through `Entered` (running count, thread-local caller restored on return);
  plugin callables sit in clearable host entries, so forwarder copies are inert after the unload; contract calls
  run on the main thread for the caller, else the only loaded plugin; cancel, report and run joining only for the
  caller's own jobs; `StageStartupPlugins` stages the Sandbox's and the tests' startup images; tools take
  `plugin`; `plugin_list` answers `{"loaded": [...]}`; a reload to another name is `name_mismatch`. Found while
  testing: a cancelled job that has not started is reaped only when a worker reaches it, so another plugin's long
  jobs on every worker can delay a drain to `drain_timeout` (the plugin stays; `shortcut:` note at `PollDrain`,
  documented in the target doc). Tests: 12 new host tests (two plugins by name, duplicate name, ownership of
  cancel/report/join, one plugin's drain timeout leaving the other's job, nested attribution, worker-thread call
  refused, a window draw's job owned by its plugin and an inert forwarder after the unload, reload name mismatch,
  startup duplicate name and section collision, a startup commit failure removing only its own sections, the
  shared-generation close guard, a deferred result surviving the other plugin's unload). Drivers: S1 reads an
  empty list; the bilateral live driver selects its plugin by name and gains the coexistence scenario.
  - Mutation checks of the new tests (source mutated, test rebuilt and run, source restored): 22 mechanisms, each
    killed by its test. Two survived at first and were fixed: the early name check in `plugin_load` duplicated the
    commit's (removed); the startup duplicate-name case was masked by the section collision (now its own section).
  - Codex code review (fresh session `01a19ce8-c358-7f81-856b-95d402a709cb`, diff sha256 e32a150f…): "Verdict:
    REVISE", five blocking: (1) a cross-plugin unload polled inside a completion could free a pending finalizer's
    entry; (2) a load polled inside a plugin tool replaces the registry entry that is running; (3) the identity
    probe called `HasInFlightWork` without entering the plugin; (4) the coexistence driver fails on a fresh build,
    the spike's generation (1) being the bilateral filter's; (5) no test of retained GPU forwarders. Fixed: a commit
    and an unload wait while any plugin's code runs (`AnyRunning`, closes 1 and 2); the probe enters the plugin;
    the driver builds the spike at generation 9; tests for a load and an unload polled from plugin code, the probe
    as its plugin, and GPU forwarders held by a second job service after the unload. Round 2 (same session, diff
    sha256 59397b82…): "Verdict: APPROVE", no remaining findings.
  - Ponytail review (fresh Claude subagent, diff sha256 6ea6f412…): "Verdict: fix 1 and 2 first", "Lean: -55 lines
    possible". Done: (1) the unread `Closed` flag removed; (2) `Admitting` removed, the ordinary exit taking each
    slot off the list before its hook (new test: Register and shutdown hook refused even with another plugin
    loaded); (3) the GPU forwarders use `Forward`; (5) `Held<F>` replaced by the `std::function` itself; (6) the
    stubborn-job test releases its job on any exit. Deferred, not a bug: (4) one type for `Candidate` and
    `StagedPlugin` and one discard path (five copies of "clear, close"; `Stage`'s failure path skips
    `ClearCallables`, safe while no forwarder exists at that point); it changes the exported `StagedPlugin`, so it
    goes with the next host change.
  - Codex round 3 on the ponytail delta (diff sha256 29d226bd…): "Verdict: REVISE", one blocking: the ordinary-exit
    case of the new refusal test passed for the job service's refusal too, and its mutation broke the loop's
    termination. Fixed: the test asserts the host's reason ("the calling plugin is not loaded"); the mutation keeps
    the slot listed through the iteration and pops it at its end (killed with exit 1, the exit call's refusal
    empty). Round 4 (diff sha256 be6f3026…): "Verdict: APPROVE", no remaining findings.
  - Verification in this session, on the final source: 56 tests (PluginHost, BilateralFilterPlugin,
    PointCloudEditing) in `build/dev-shared`; S1 60 of 60; the bilateral live driver 61 of 61 (11 coexistence
    checks); jscpd (src, tests, plugins; min 8 lines, 70 tokens): no clone touching the changed files. S2: 112 of 116;
    the four failed checks are timing checks (the 66 queued 60 s jobs, the 12 s job that ignores cancellation, the
    GPU frames' retirement) invalidated by the display: the monitor was off and the session locked, so
    presentation ran at the X server's 1 Hz fallback (frame pacing report without any plugin: 0.86 s mean per
    frame, 98 % in `present`); submitting 66 jobs then outlasted their 60 s, so 8 ran to completion before the
    reload. S2 and R4 are to be rerun with the display on; R4 was not run. Static trees (working tree with the
    later block's uncommitted files, which the 2d-0 files do not use): `build/ci` 56 tests pass; `build/ci-vulkan`
    builds the static Sandbox with the in-process bilateral filter.
- 2026-11-02, step 2d in progress (after 2d-0 landed in `aef5dc81c`, `41b85ebe0`, `c441a0bac`): `plugins/descriptor_analysis`
  (section `sandbox.descriptor_analysis` with the derived array row, tools `descriptor_analysis` and
  `descriptor_analysis_preview`, window `view.descriptor_analysis`; runs: CPU k-d tree and CPU LBVH one job each,
  Vulkan scale, gated GPU radius rows and histograms, each stage submitted from the previous one's completion).
  Promoted for both plugins: `PluginService<T>` (contract) and `ActivePluginSection`/`ApplyPluginSection`
  (`Extrinsic.Runtime.PluginSections`); `tests/support/PluginHarness.hpp` is the general harness, the bilateral and
  descriptor harnesses add only their helpers. 10 contract tests (`Test.DescriptorAnalysisPlugin.cpp`) pass with the
  built-in still present. Open: golden parity, removal of the built-in, Sandbox wiring, GPU smoke with the benchmark
  contract, live driver, mutation checks, reviews, docs.
  - The built-in removed (by a delegated session, reviewed here): config module and codec block, operation, result
    slot and sinks, agent row, Sandbox section, window and redirects, CMake, the two built-in-only test files and
    the three `PointLBVHGpuSmoke.Descriptor*` cases (their successor, `DescriptorAnalysisPluginGpuSmoke`, also writes
    the benchmark JSON of `geometry.point_lbvh.descriptor_runtime_smoke`). Shared tests keep their intent; the
    point-readiness fixture's second oriented consumer is a `PointCloudEditingService` capture with two scalar
    outputs that refuses zero normals (minimum live count 1, so the two consumers' minimums stay distinct). Lost:
    the Sandbox's only normals picker that hid unresolved-domain rows was the built-in's (construction's remains,
    which shows them). The Sandbox no longer names its plugins: each plugin's CMake adds itself to
    `INTRINSIC_STARTUP_PLUGINS`, and a generated `SandboxStartupPlugins.hpp` carries the list into `main.cpp`
    (R1 30 -> 29). ARA C87 pins its removed proofs to `36a7b50` (as C86's bilateral proof was pinned in
    `c9968295c`). Golden parity: `Test.DescriptorAnalysisPlugin.cpp` loads the 300-point cloud the built-in was
    recorded on (`tests/data/descriptor_golden_cloud.json`) and both CPU backends reproduce its 9900 values
    exactly; the live driver `plugins/descriptor_analysis/live_driver.py` checks all three backends over MCP.
  - Mutation checks of the new tests (source mutated, test rebuilt and run, source restored), each killed: zero
    normals accepted; stale reported as cancelled (killed after a new case: an input change, then a cancel);
    unrepresentable reported as failed; columns shifted; CPU LBVH without its scale; CPU LBVH rows capped;
    duplicate output names allowed; the window following the bin without a Show; PublishScalars accepting a wrong
    column count (killed after a new service test); the catalog's domain filter off; the array items unchecked.
  - Codex code review (fresh session `01a19de3-aa27-7010-8dfd-571da38988ca`, diff sha256 1a1a3549…): "Verdict:
    REVISE", three blocking: the Normals picker offered every domain and then overwrote the domain (the bilateral
    window too); CPU LBVH counted its scale time twice; no unload/reload test. Fixed: `InputCatalog` takes a domain
    and both windows offer the positions' domain for normals; Compute times itself after Prepare; a contract test
    unloads, undoes and redoes, reloads and runs again (the plugin harness reopens the plugin in-process for any
    load path), and the GPU smoke's unload case reloads and runs again. A deterministic later-stage refusal has no
    host path (the drain cancels every stage before it refuses submissions); argued in round 2.
  - Ponytail review (fresh Claude subagent, same diff): "Verdict: fix 1, 2 and 3 first", "Lean: -80 lines
    possible". Done: (2) the array row's length comes from the bound array (Bind sets MinItems/MaxItems, so schema
    and validator agree) and its items go through `CollectDeclaredFieldErrors`; (3) the CPU backends' stage is
    `SubmitHistograms`; (4) a no-op restore in `CpuRadiusRows` removed (deviation from the plan: CPU radius rows are a
    service method, the built-in's loop moved down for later point plugins, not rows made by the plugin from
    `CpuIndex`); (5) one LBVH refusal, after the zero-normal check as in the built-in. Deferred to one "plugin
    template" step before a third plugin (the review allows it): (1) the run protocol both plugins copy (`Run`,
    `Submit`, `Cancel`, the waiting tool, the property combo) into the services ring, and (6) one CMake function
    for a plugin's targets and its startup entry. (7) skipped (test code).
  - Codex round 2 (diff sha256 85da3cfc…): "Verdict: APPROVE" (the refusal analysis accepted, no host hook). Then the
    Vulkan smoke ran (its swapchain has VSync off, so the throttled display did not stop it) and found two test
    defects, fixed: the dense case's reference columns were not prefilled like the outputs; the unload case kept the
    answered unload's reservation alive. Live driver: the gated GPU stage is active as `awaiting-apply`.
  - Verification in this session: dev-shared full build; IntrinsicRuntimeContractTests 1935/1935 (exit 1 from a
    LeakSanitizer report in the Vulkan ICD at process exit), IntrinsicRuntimeIntegrationTests 80/80,
    IntrinsicSandboxEditorIntegrationTests 291/291; plugin tests 6 repeats under 40 busy loops; Vulkan smoke
    `DescriptorAnalysisPluginGpuSmoke` 4/4 and `BilateralFilterPluginGpuSmoke` 2/2; live driver 39/39 (all three
    backends equal the built-in golden over MCP, sha256 18f798d0…; MCP cancel during the GPU stage; undo and redo
    across unload and reload); static `build/ci` 173 filtered tests and `build/ci-vulkan` static Sandbox with both
    plugins in-process; jscpd (src, tests, plugins): no clone touching the changed files; R1/R2 checker strict.
  - Found by the bilateral live driver (which still unloaded without a name and expected one startup plugin; fixed):
    a load the host refuses after Register closes the image without the shutdown hook, so what Register allocated
    leaks (LSan: the window state, 256 bytes). Fixed in the plugins (window state made on the first draw) and
    documented in the target doc; open for the plugin template step: whether the host should call the shutdown
    hook for a candidate it discards after a successful Register (it changes the spike drivers' event order).
- 2026-11-03, plan of the plugin template step (Q69, Q70; the deferred ponytail findings of 2d-0 (4) and 2d (1, 6)).
  Goal: a third plugin is a folder with its module, Register/Shutdown and its own code, with no copied host
  protocol; the host pairs every successful Register with one shutdown hook. Done when both plugins use the shared
  pieces, the host's discard paths are one, the rule has a host test, and S1 passes with its updated lifecycle.
  1. **The shutdown rule.** Every successful Register is followed by exactly one shutdown hook before the image
     closes (or stays mapped): at unload and at the exit as today, and now also for a candidate the host discards
     (empty or taken name, failed commit, reload name mismatch, a pending load dropped before its commit, a startup
     plugin dropped at staging or not committed). A Register that returns false gets none (it cleans up itself). One
     discard path does it: shutdown hook (entered), staged callables destroyed, forwarders cleared, close.
     `Candidate` becomes `StagedPlugin` (one type for an uncommitted plugin). Test: the fixture logs shutdown and
     close; each discard path gives "shutdown", "close" in that order, a failed Register only "close"; tests that
     expected no shutdown on a failed commit are updated. S1's expected lifecycle gains "shutdown" after "register"
     for the commit failures (Q70); S2 checks only relative orders (unchanged, rerun when the display is on).
  2. **The run protocol in the contract.** `PluginRun<Result>` {Pending, Final, Handle}; `SubmitPluginRunStage(run,
     job, refusal)` (the first stage's handle is the run's, later stages join it); `CancelPluginRun(run)`;
     `WaitForPluginRun(run, pendingText, answer)` (the waiting tool's outcome: pending text, a continuation that
     answers once Final is set, the cancel hook). Both plugins use them; their Run, Submit, Cancel and tool bodies
     shrink. The vec3 property combo over `InputCatalog` moves into the editing service (`PointPropertyCombo`, ImGui
     compiled in the runtime, as the host's identity probe already is). Test: a host contract test of two stages
     joining one run through `SubmitPluginRunStage` and `CancelPluginRun` ending both; the plugin tests keep their
     coverage.
  3. **One CMake function.** `intrinsic_add_feature_plugin(<Name> SOURCES ...)` in `cmake/` creates
     `Intrinsic<Name>PluginObjects` (module `<Name>.cppm`), with shared libraries the MODULE
     `Intrinsic<Name>Plugin` from an entry file generated from one template (the three v1 symbols forwarding to
     `Intrinsic::Plugins::<Name>::Register/Shutdown`), and appends `<Name>` to `INTRINSIC_STARTUP_PLUGINS`. Each
     plugin's CMakeLists is one call; their `*.Entry.cpp` go. The spike keeps its own CMake (a fixture, not a
     startup plugin).
  Not in this step: S3, a host test hook to force refusals, changing the window state back to Register.
  Verification: dev-shared and static (`ci`, `ci-vulkan` Sandbox) builds, the three test binaries, the plugin GPU
  smokes, S1, both plugin live drivers, mutation checks, jscpd, ci-docs replay; S2 and R4 when the display is on.
  - Codex plan review (fresh session, `codex-plantpl`): "Verdict: REVISE", three blocking. Revision 2:
    1. Startup plugins: one discard for every uncommitted startup plugin, also without config control (hook entered,
       staged callables destroyed, forwarders cleared, so the boot sections' callbacks call nothing; the image stays
       mapped only there, as today) and when the host goes before `OnResolve` (its destructor and `OnShutdown`
       discard what is still staged). Test: one hook, no plugin callback afterwards.
    2. Tests prove the order and attribution: the fixture's staged callables log their destruction, so each discard
       path gives "shutdown", the callables' destruction, then "close" (a failed commit's rollback destroys its
       tools' copies before the hook; the hook still runs once, before the close); a discarded candidate's hook submitting work
       is refused as "the calling plugin is not loaded" with another plugin loaded; zero hooks for a failed Register
       and for failures before Register; one hook when the close guard keeps the image mapped.
    3. The combo returns whether the selection changed (the descriptor window moves its 33 outputs to the positions'
       domain on it); a window test selects a face property and checks the applied domains, and an unlisted
       selection is kept.
    Optional taken: `WaitForPluginRun` gets its own checks (already final, pending text and polling, the final
    answer, the cancel hook).
- 2026-11-03, the plugin template step implemented (plan revision 2):
  - The shutdown rule: one `Discard` in the host (hook entered, staged callables, forwarders, close; or the image
    kept mapped for a startup plugin without config control and for startup plugins of a host that never resolved,
    `DiscardStartup` in the destructor and `OnShutdown`); `Candidate` is gone (`StagedPlugin`). A failed Register
    gets no hook. Tests: every discard path (taken name, failed commit, reload to another name, a dropped pending
    load) gives shutdown, the callables' death, close (a failed commit's rollback destroys its tools' copies first);
    a failed Register only the close; a discard the close guard keeps mapped runs the hook once; startup plugins of
    an unresolved host run the hook once and their section validator calls nothing afterwards; a discarded
    candidate's hook is refused as not loaded with another plugin loaded. S1's lifecycle expects the hook after a
    failed commit's Register (60/60).
  - The run protocol in the contract: `PluginRun`, `SubmitPluginRunStage`, `CancelPluginRun`, `WaitForPluginRun`;
    `PointPropertyCombo` in the editing service (true when the choice changed `ref`). Both plugins use them (their
    `Run` is `PluginRun<Result>`, their `Cancel` is gone). Tests: stages join the first stage's run and the waiting
    tool's cancel hook ends them; the waiting tool answers at once or when the run ends; the descriptor window's
    positions picker moves the 33 outputs and the normals to the face domain and keeps an unlisted choice.
  - `intrinsic_add_feature_plugin` (cmake/IntrinsicFeaturePlugin.cmake) with the entry template
    `IntrinsicFeaturePluginEntry.cpp.in`; both plugins' CMakeLists are one call; their `*.Entry.cpp` are gone.
  - Codex code review (fresh session, diff sha256 95bf9ab8…): "Verdict: REVISE", three blocking: a hook run from the
    destructor could reach destroyed services; the hook did not always precede the death of the plugin's callables
    (a startup drop unregistered sections first, a failed commit's rollback dropped its tools); two doc statements
    contradicted the new rule. Fixed: the destructor clears the services and the current host before its hooks
    (module failures terminate the process, so only an exception reaches that path; no test can build a
    registered-but-unresolved host); the slot holds each plugin callable strongly until `ClearCallables`, so every
    discard path is hook, callables, close (the failed-commit test expects it; mutation of the strong reference
    killed); docs updated (the deferred-result line was stale since 2c).
  - Ponytail review (fresh Claude subagent, same diff): "Verdict: fix 1 and 3 first", "Lean: -40 lines possible".
    Done: (1) docs and two stale comments; (2) `RegisterInto` closes after a failed Register (one place);
    (3) the startup drop and cannot-commit tests check their hooks (mutations killed); (4) `WaitForPluginRun(run,
    text)` owns the answer (applied succeeds, any other status is the error code); (5) `Discard(StagedPlugin&)`
    and `ShutdownInto`; (6) `SubmitPluginRunStage` shorter; (7) the CMake function takes its sources directly and
    finds its template by `CMAKE_CURRENT_FUNCTION_LIST_DIR`; (8) leftovers removed. Skipped: (9) the picker test's
    ImGui setup (a third copy; the frame scope's docking flag was not checked against it).
  - Codex round 2 (diff sha256 d4fd0216…): "Verdict: APPROVE", no remaining findings. Verification on the final
    source: IntrinsicRuntimeContractTests 1941/1941 (exit 1 from the Vulkan ICD's LeakSanitizer report at process
    exit), IntrinsicRuntimeIntegrationTests 80/80, IntrinsicSandboxEditorIntegrationTests 291/291; Vulkan smokes 6/6;
    S1 60/60 (the hook after a failed commit's Register); live drivers 39/39 (descriptor analysis) and 62/62
    (bilateral filter, with a refused second load and a clean sanitizer exit); static `build/ci` 76 filtered tests,
    `build/ci-vulkan` static Sandbox and Vulkan tests built; jscpd: no clone touching the changed files. S2 and R4
    still wait for the display.
- 2026-11-03, plan of S3, the job form (Q71; Q52's order "S3 before the GPU-heavy features"; Q67: kernel density and
  point spacing come after S3). Goal: one run protocol that plugins use for iterative and preview/accept work, on
  CPU and GPU, proven by the S3 assertions (definition above, "S3, the job form on CPU and GPU"), so the
  kernel-density, spacing, density-weight and outlier family can move into plugins on it. Done: the assertions as
  tests, the stall measured (preflight), a spike report with yes or no per question, the numbers and a recommended
  boundary for the operator; the code stays as the start of that migration or is deleted.
  Inventory (Explore, 2026-11-03): no generic Init/Step/Preview/Finish; reusable: the CPD solver (deterministic
  `Initialize`/`Step`/`Current`, a unit test that step mode equals the full run), the CPD session's control logic,
  the GPU transaction core and residency rings (preview = ring front, Accept once, Discard), the resident k-means
  state machine (no full array per step already, byte counters, Stop/Accept/Discard, workspace reuse), JobService
  (`CoroutineWork`, gates, run joining), frame pacing diagnostics. Missing: a step loop with pause and single step,
  a lock-free snapshot hand-over and a metric ring (CPD keeps a mutex snapshot with a growing trace), latest-wins
  previews (editor and host refuse a second run on an output), MCP controls (every GPU run over MCP auto-accepts),
  accept-on-convergence as its own option, a TSan build, per-callback main-thread timing.
  Blocks (one code review each):
  - **S3a, two core primitives** (`src/core`, no dependencies): `TripleBuffer<T>` (one writer, one reader, latest
    wins, never blocks either side, a reader never sees a torn value) and `AppendLog<T>` (revision 2/3: a
    preallocated append-only log, one writer, readers read any published prefix without blocking it; it replaces
    the metric ring). Tests: unit (latest wins, no torn value under a deterministic
    interleaving) and a separate TSan harness: a standalone CMake project (`tests/tsan/`) that compiles the two
    headers and a stress test with `-fsanitize=thread` and asserts progress (the writer completes K publications
    while a reader is stalled; every observed value is one the writer wrote, never half), run by a CTest of label
    `build` (the main tree keeps its sanitizer modes).
  - **S3b, the step run and the CPU case.** `Extrinsic.Runtime.StepRun` in the services ring, used through the
    plugin contract: a run is `Init` (main thread: capture), `Step` (one iteration: a worker job on CPU), optional
    `Preview` (writes the latest snapshot into the triple buffer), `Metrics` (one record per step into the ring),
    `Commit` (main thread, one undo entry) and `Discard`. States: ready, running, paused, converged, failed,
    cancelled, committed. Controls: run, pause (after the step in flight), step(n), stop (= pause and end),
    accept (commit exactly once from paused, converged or stopped), discard; option accept-on-convergence. One
    step per job, the next submitted from the previous completion while running or while a step budget remains,
    so batching cannot change the sequence. Latest wins: a new run on the same output supersedes a ready, paused or
    converged one (its preview is dropped, it ends discarded) and is refused only while a step is in flight. MCP:
    `run_control {run, action}` and `run_observe {run, metrics}` (phase, step count, last metrics, preview
    revision); the UI: a spike window with Run, Pause, Step, Stop, Accept, Discard and a metric plot. CPU case: CPD
    (reference E-step, `Geometry::CoherentPointDrift::Solver`) in a spike plugin `plugins/job_form_spike` (kept or
    deleted per the report): Init captures source and target through the point-cloud editing service, Step is one
    solver step, Preview the transformed source, Commit publishes it into a vec3 output (one undo entry). Tests (each fails without its
    mechanism): a run equals its step sequence bit for bit (positions and trace) for step(1)*N, step(7)+run, run
    with pauses; preview on equals off; pause and resume equals uninterrupted; cancel and previews commit nothing
    (no undo entry, outputs unchanged); accept commits exactly once (a second accept refused); converged commits
    only with accept-on-convergence; latest wins (an older preview never shows after a newer one: revisions
    monotonic at the reader; a superseded run ends discarded once).
  - **S3c, the GPU case and the measurement.** GPU k-means with resident buffers as a step run, inside the
    clustering module (a plugin may not import it: R2), through the same StepRun: a step is one iteration of the
    existing paged state machine (a step budget at the iteration boundary), preview through the
    residency ring (latest wins as today), accept reads the labels once. Tests (Vulkan smoke): run equals step
    sequence (labels exact, centroids bitwise), pause/resume equals uninterrupted, zero full-array bytes per step
    after Init (the existing byte counters), cancel and previews commit nothing, accept once, accept on
    convergence. The main-thread stall during a slider preview (a parameter change that restarts the preview run
    every frame) measured against 4 ms: a frame-pacing field for the job completion drain, a pilot packet under the
    experiment pre-flight (light tier unless the report's recommendation rests on it, then full).
  Not in S3: moving CPD or k-means out of the editor; the kernel-density family; changing the shipping CPD or
  k-means panels and MCP tools (the spike runs beside them).
  - Codex plan review (fresh session, `codex-plans3`): "Verdict: REVISE", seven blocking. Revision 2:
    1. **Latest wins without loss.** A run keeps one pending newest request (a new parameter set or a new run on its
       output); it replaces the current computation at the next safe boundary (after the step in flight, never
       writing buffers in use), older pending requests are dropped. Test: A running, B then C requested, the slider
       released: C previews, and neither A nor B shows or commits afterwards.
    2. **One transition table** (in the code and the report): states ready, running, paused, accepting (GPU
       readback in flight), and the terminal states committed, discarded, failed; a run that stops computing records
       its end reason (converged, iteration cap, stopped, cancelled) and waits paused for Accept or Discard. Accept
       on convergence commits only for "converged" (never cap or stop). Precedence: cancel and discard over a
       pending accept; a replacement during accepting waits for it. Tests: cap, Stop, cancel during the last step,
       discard and replacement during the accept readback.
    3. **The run outlives its jobs.** A step-run service (`Extrinsic.Runtime.StepRuns`) owns each run until it is
       terminal: its output (admission asks it, so a paused run still holds its output), its controls, its owner
       (a plugin name or a runtime module), its callbacks; cancel reaches a paused run; a plugin's unload drains
       its runs (each ends discarded, once). The service takes an explicit owner; the plugin contract wraps it with
       the caller resolution, and the clustering module calls the service directly. Tests: paused cancel, a
       competing editor publication refused, unload during a paused run.
    4. **The CPU preview reaches the viewport and Commit checks every input.** The point-cloud editing service gains
       a position preview through the residency ring (as the GPU position transactions do): the latest snapshot is
       uploaded into the output's ring and shown, Discard releases it, Commit publishes canonically. A capture of
       several entities (CPD: source and target) is checked as a whole at Commit. Test: target changed or deleted
       after convergence: Accept fails stale, output and history unchanged.
    5. **Complete metrics, counted copies.** Metrics go into a preallocated append-only log sized to the run's
       iteration cap (one writer, an atomic count, readers read any prefix without blocking), not a last-N ring;
       the triple buffer carries only the latest preview. Preview off makes zero intermediate copies; preview on
       copies only after the reader took the previous one and at most every k steps. Tests: a stalled observer
       still reads the whole curve; copy counts with preview off, on, and with a stalled reader.
    6. **Tests that see the mechanism:** the exact step count after step(n), no advancement while paused,
       advancement after resume, through the window and over MCP; on GPU also preview on/off, latest wins, the
       fence-held buffers' lifetime and reuse, and byte counters split into init, per step, terminal centroids and
       accept.
    7. **The measurement:** frame-pacing fields for every main-thread path a run uses (completion drain, GPU
       transfer drain, capture and init, preview upload), with counts of accepted replacements and completed steps
       (a stream of refusals cannot pass); the R7 verdict and the recommendation rest on it, so full pre-flight
       (two fresh reviewers, a frozen packet: workload, controls, limits, command, HTML report from raw output).
    Optional taken: bitwise comparisons over the mathematical fields with executable, device, inputs, seeds and
    paging fixed (timings excluded); a worker loop that checks the controls between identical `Step()` calls
    (ordering kept, not limited to one step per frame) instead of one job per step; the standalone TSan project
    holds a reader across many publications and wraps, with a timeout and progress asserted in every build.
  - Codex round 2 on the plan: "Verdict: REVISE"; findings 1, 3, 4, 6 and 7 closed, two open. Revision 3:
    1. **Cancel ends the run.** Cancelled is terminal (discarded), not a paused end reason: Accept and Resume are
       refused after it, also after a late worker or GPU completion; its resources retire when safe. Paused for
       Accept or Discard remain only converged, iteration cap and stopped. Test: cancel, then a late completion,
       then Accept and Resume refused, nothing committed.
    2. **The preview rate is a time limit.** An intermediate preview copy needs both the reader to have taken the
       previous one (`TripleBuffer::Unread` false) and a minimum interval on a monotonic clock (about 30 per second,
       as the target states), injectable for tests. Test with a controlled clock: many cheap steps with a reader
       that always consumes give at most the clock's allowance of copies.
    3. (optional) S3a's metric storage is the preallocated append log (`Core.AppendLog`), which replaces the S3
       definition's metric ring (complete curves); the TSan project compiles the production modules
       (`Core.TripleBuffer`, `Core.AppendLog`) and has no ring-wrap expectation.
  - Codex round 3 on the plan: "Verdict: APPROVE", no findings open.
- 2026-11-03, S3a implemented: `Extrinsic.Core.TripleBuffer` (three slots, a fresh bit in the middle index; the
  writer's `Unread` tells whether the reader took the last value) and `Extrinsic.Core.AppendLog` (preallocated,
  release-published count). Tests: `Test.Core.TripleBuffer.cpp` (newest once, unread, a held value untouched while the
  writer publishes on, the log's prefixes and a full log) in IntrinsicCoreWrapperUnitTests; the standalone TSan
  project `tests/unit/core/tsan` (CTest `Core.TripleBufferThreadSanitizer`): a reader
  holding its value while the writer completes 200000 publications, a poller seeing whole and monotonic values, two
  log readers seeing complete records. Mutation checks: TSan reports a race for a relaxed publish, for a reader
  sharing the middle slot and for a relaxed log count; the unit tests fail for a publish without the fresh bit, for a
  reader taking the writer's slot, and for a log without its capacity check. Also (Q71): the descriptor picker test
  uses `TestSupport::ImGuiFrameScope` (both of its mutations still killed).
  - Reviews: Codex (fresh, `codex-review-s3a`, diff sha256 ea996906…): "Verdict: REVISE", one finding: the stress
    harness could pass without concurrent publication (a writer done before the readers ran skips their checks, and
    the poller's final read is ordered by the done flag). Fixed: the poller writer publishes until the poller took
    1000 values (counted through `Unread`) and the poller counts only reads made while the writer runs; the log writer
    starts once both readers run and waits halfway until each checked records (a signal from the readers to the
    writer, ordering none of the records). Ponytail (fresh agent): "fix 1 first; 2 and 3 are optional", all taken:
    the Python runner replaced by `ctest --build-and-test` with a configure-time TSan probe (`INTRINSIC_TSAN_AVAILABLE`,
    "Not Run (Disabled)" without it); the initial-value constructor, `Capacity()`, the deleted copies and the
    alignment removed, the log on a vector; a leftover include. Also the core README and the module inventory.
    Verification: the TSan CTest passes (160445 polled reads of 200000 publications); its three mutations each give a
    TSan data race through CTest; disabled when the probe is 0; unit tests 4/4, descriptor tests 13/13.
- 2026-11-03, S3b implementation plan (within plan revision 3; Q71 "Ja, bitte S3."). Two blocks, one code review each.
  Facts it rests on: a plugin job parked by its readiness gate stays `AwaitingApply`, which the editor's admission
  (`IsActiveEditorJobState`) and the host's admission count as active, the Jobs window lists and a run cancel or an
  unload drain cancels (its `Completion(false)` then runs once); the gate runs on the main thread entered in the
  plugin (so contract calls from it resolve the plugin), once per drain within the gate budget; jobs joined to a run
  skip admission and are cancelled with it; the host's drain waits for every job of the plugin.
  - **S3b-1, the run driver** (`Extrinsic.Runtime.StepRuns`, `src/runtime/Services`, no plugin host change):
    - A run's state is host data (created by engine code, so a plugin may hold it past its unload): phase, end
      reason, step count, controls, `AppendLog<StepRunMetric>` (sized to the iteration cap; a metric is the step
      number and four named doubles), `TripleBuffer<StepPreview>` (generation, step, `std::vector<float>`), copy
      counters, an injectable monotonic clock.
    - The run is carried by jobs of its owner (`Submit`/`Cancel` callables; a plugin passes the contract's
      `SubmitPluginJob`/`CancelPluginJob`, S3c the clustering module's): a **holder job** (the run's first job, with
      the output) finishes its work at once and stays parked by its gate until the run ends, so a paused run holds
      its output, has a Jobs row and is cancelled by the Jobs window and the unload drain; its gate is the run's
      main-thread pump (shows a newer preview, applies a pending restart, submits the next segment, starts Accept);
      its `Completion(true)` is Accept (`Commit` once), `Completion(false)` is the end of a run that does not commit
      (cancelled, discarded or failed: `Discard` once). **Segment jobs** (joined to the run) are the worker loop: steps
      until a pause, the step budget, the end, the cancel or a pending restart, checking the controls between
      `Step()` calls; a metric per step; an intermediate preview copy only when the reader took the previous one
      (`Unread` false) and the minimum interval passed (30/s by default), one copy at a segment's end.
    - Transitions (one table in code and report): ready -> running (run, step n) -> paused (pause, budget spent) or
      paused with an end reason (converged, iteration cap, stopped) -> committed (Accept, or at once when converged
      with accept-on-convergence) or discarded (Discard, cancel at any time, also during a step: the late segment
      completion changes nothing); failed when a step fails. Accept and Resume refused after a terminal phase.
      Controls (window, MCP) only set requests the pump acts on, so every action runs in the owner's context.
    - Latest wins: `Restart(init)` keeps one pending newest request (main thread); the pump applies it once no segment
      is in flight (asking the running one to stop after its step), calling `init` (the plugin re-captures or
      re-parameterises) and starting a new generation (new log; previews of older generations are not shown).
    - The module registers MCP `run_control {run, action, count}` (run, pause, step, stop, accept, discard) and
      `run_observe {run, metrics}` (phase, end, owner, step count, last or all metrics, preview generation and step,
      copies); runs are listed by id while their state lives.
    - Tests with a deterministic fake step, the real `JobService` and a test owner, each failing without its
      mechanism: run equals step(1)*N, step(7)+run and run with pauses, bitwise (state and metric log); preview on
      equals off, copy counts off/on/stalled reader with a controlled clock; the exact count after step(n), none
      while paused, advancing after resume; cancel then a late completion then Accept and Resume refused, nothing
      committed; Accept once (a second refused); converged commits only with accept-on-convergence, cap and Stop
      never; A running, restart B then C: C runs, B's init never called, nothing of A shown after C; MCP controls and
      observation.
  - **S3b-2, the CPD case** (`plugins/job_form_spike`, `intrinsic_add_feature_plugin`): Init captures source and
    target through the point-cloud editing service and initialises `Geometry::CoherentPointDrift::Solver` (reference
    E-step); Step is one `Solver::Step`, its metrics from the observer's trace; Preview copies the transformed source;
    Show uploads it into the source positions' residency ring (new editing-service calls `PreviewPositions` and
    `DiscardPreview`: back slot, transfer upload, publish; released by Discard and before Commit's canonical
    publication); Commit checks both captures (`Current`) and publishes the source positions (one undo entry), stale
    when either changed. A spike window (Run, Pause, Step, Stop, Accept, Discard, a metric plot). Tests through the
    plugin harness: run equals the solver's own step sequence bitwise (positions and trace); a target changed or
    deleted after convergence makes Accept fail stale (output and history unchanged); a paused run holds its output
    (a competing run refused); unload during a paused run ends it discarded once; MCP controls end to end.
  - Codex plan review (fresh session, `codex-plan-s3b`): "Verdict: REVISE", five blocking, one optional. Revision 2:
    1. **Terminal at once, retirement after the last segment.** A run's phase becomes terminal at once (cancel,
       discard, failed Accept); `Discard` (the owner's cleanup) and dropping every owner callable (step, preview,
       show, commit, a pending restart) happen only once no segment is in flight: at once when none is, else in that
       segment's completion (also a late or cancelled one), exactly once. Test: a step blocked inside the owner's code,
       the run cancelled and the holder finalised first, then the step released: cleanup once, after the step returned,
       no callable left; S3b-2 also unloads only afterwards.
    2. **One world.** The run records the active world at Start; the pump checks the owner's `Valid` (for the CPD
       plugin: `Current` of both captures) before Show, a restart, a segment and Commit, and ends the run discarded
       ("stale") when it fails. The point-cloud editing service's `Current` (and so `Publish`) also requires the
       capture's world to be the active one (`EditorProcessingContextWorldCurrent`; today it checks only the captured
       properties). Tests: the world switched while paused and while a segment runs: no preview shown, nothing
       published, no restart into the other world.
    3. **A failed Accept ends the run.** In the holder's true completion a refused `Commit` (stale) makes the run
       terminal (failed, "stale") and retires it there (the host suppresses a later false completion). Test: the
       target changed after convergence: Accept fails stale, the run is terminal and retired, a second Accept and a
       Resume are refused, and a new run on the output is admitted.
    4. **Copies are limited across segments.** A preview copy, wherever made, needs preview on, the reader to have taken
       the previous copy and the interval passed. The worker checks after each step; when a run stops computing (pause,
       end) with its newest step not copied, the pump makes that copy on the main thread (no segment in flight, so the
       owner's state is idle) once the same three conditions hold, so the paused or ended state still shows. Commit
       reads the owner's state, never a preview. Tests: repeated step(1) with preview off (zero copies), with a stalled
       reader (one copy) and with a frozen clock (one copy).
    5. **The viewport path is tested.** S3b-2's preview: `PreviewPositions` acquires a back slot of the source
       positions' ring, uploads with the transfer queue, records the upload as the slot's completion
       (`AddCompletion`) and publishes; the run keeps the ring's generation and releases only that ring
       (`Discard(key, generation)`), at retirement and before Commit's canonical publication. A Vulkan smoke (the
       plugin GPU smoke harness): while paused the ring front reads back as the preview positions while the canonical
       positions and the history are unchanged; after Discard the ring is gone (the canonical slot shows); a run
       discarded and a successor started on the same positions: the late cleanup of the first leaves the successor's
       ring. Also the window test (Run, Pause, Step, Stop, Accept, Discard pressed through ImGui's
       `ActivateItemByID`) and a competing editor publication on the held output refused.
    6. (optional, taken) S3b-1's tests also run with an apply and gate budget of one and an unrelated parked job:
       progress, no duplicate segment, controls kept. The 4 ms measurement stays in S3c.
  - Codex round 2 on the S3b plan: "Verdict: REVISE"; 1, 2, 4 and 6 closed; 3 and 5 open, 7 new. Revision 3:
    3. **Commit refused with valid captures.** S3b-1 test with the fake owner: `Valid` true, `Commit` returns false:
       Commit called once, the run failed and retired (Discard once, no callable left), Run, Step and Accept refused, a
       new run on the output admitted.
    5. **The renderer-facing positions and a pending upload.** The Vulkan smoke asserts what the renderer draws, as
       `Test.GpuPositionsAcceptSmoke` does: `RenderExtractionCache::ShowsUncommittedPositions` for the source and the
       drawn pixels moved while paused (canonical positions and history unchanged), and both back after Discard.
       `PreviewPositions` takes the residency and the transfer queue, so a contract test with `MockDevice` holds the
       upload incomplete (`TransferQueue.AlwaysComplete = false`): the slot is not handed out again and not freed by a
       Discard until the transfer completes; without `AddCompletion` this test fails.
    7. **No ring handed to a successor while its predecessor still holds it.** The run's `Discard` (it releases only
       what the run shows: the preview ring) runs once in the owner's context as soon as the run is terminal (in the
       pump or a completion, before the holder's run is cancelled), not at retirement: the preview is touched only by
       main-thread code (Show, Discard), never by a segment, so it need not wait for one. Retirement (after the last
       segment) drops the callables. So the output is admissible only after the predecessor's ring is released. Test
       (S3b-2): A cancelled while its step is blocked, B started on the same positions and previewing, A's step
       released: B's ring and preview stay.
  - Codex round 3 on the S3b plan: "Verdict: APPROVE. Findings 3, 5 and 7 are closed at plan level. No new blocking
    or optional findings." (S3b-1's fake owner has no output admission; the admitted successor of 3 is tested in S3b-2
    through the plugin host.)
- 2026-11-03, S3b-1 implemented: `Extrinsic.Runtime.StepRuns` (`src/runtime/Services/Runtime.StepRuns.{cppm,cpp}`;
  the Sandbox adds the module and registers `run_control` and `run_observe` beside the plugin host's tools). One
  deviation from revision 1, found while checking the mechanisms: the previews' generation filter could never act (the
  pump takes the older computation's last copy before it applies a restart), so `Show` drops every copy while a
  restart is pending instead; the generation stays as a counter. Tests `tests/contract/runtime/Test.StepRuns.cpp` (11,
  the real job service and scheduler, a test owner carrying the jobs as the plugin host does; 15 repetitions clean):
  run equals step(1)*N, step(7)+run and run with four or more pauses requested while a step is held (bitwise trace,
  one step at a time); step counts exact, none while paused; copies with preview off (0), a frozen clock (1), a moving
  clock (at most one per 33 ms) and a stalled reader (1, the writer never waits); cancel with a step held inside the
  owner's code (Discard at once, the owner's code kept until the late completion, then dropped; Accept and Run
  refused); Accept once; only convergence commits by itself and only when asked (not the cap, not Stop); a refused
  Commit with valid captures ends failed and retired; a restart replacing a pending one (never called) and nothing of
  the older computation shown after the request; invalid captures while paused and while computing; drains with apply
  and gate budgets of one and another parked job; MCP control and observation. Mutation checks (15, each one mechanism
  removed): all killed (one more step per segment, no pause between steps, a copy while the reader holds the last, no
  interval, copies with preview off, callables dropped while a step runs (sanitizer), Discard waiting for the segment,
  old previews after a restart, the cap committing, a refused commit counted as committed, no validity check, a second
  segment while one runs, a restart keeping the older pending, Accept twice, resuming after the end).
  - Reviews of S3b-1 (diff sha256 307303db…): ponytail (fresh agent): "fix 1 and 2 first (both are small); 3 and 4 are
    optional", all taken: a retired run frees its previews; the three flags that changed nothing (`ShownReleased`,
    `CancelRequested`, `HolderFinished`) are gone; `PreviewEnabled` is gone (no `Preview` callable is preview off);
    `Trim` shorter. Codex (fresh, `codex-review-s3b1`, after two capacity retries): "Verdict: REVISE", five findings,
    and "S3a finding: closed". Fixed: (1) an older generation's convergence no longer accepts the restart replacing it
    (no accept request while a restart is pending; test with the converging step held and a restart requested); (2)
    the owner's clock is dropped at retirement with its other callables (the cancel test checks it); (3) the callback
    contract states that Valid, Show and Discard may run while a step does; (4) `run_control` refuses a count beyond
    32 bits instead of narrowing it (test); (5) retired runs beyond the newest 32 go at every service call, not only at
    Start (test with 40 runs ended together). Not tested: that a retired run's preview memory is freed (nothing
    observes it). Mutation checks rerun (19, the 15 before plus the four fixes): all killed.
  - CI on 357977bd2 (S3a) failed: its CPU test selection maps a selected CTest test to a test binary or to a script of
    `SCRIPT_TESTS`, and the ponytail-proposed `ctest --build-and-test` command is neither. Fixed in 0a6fc4380 (pushed at
    once so main turns green): `tests/regression/tooling/Test.TsanHarness.py` runs the harness again and is
    allowlisted; the configure-time probe still disables the test without a TSan runtime. Goes into the next review.
- 2026-11-03, S3b-2 implemented: the spike plugin `plugins/job_form_spike` (`intrinsic_add_feature_plugin`; tool
  `job_form_spike_start`, a window following the run started last with Run, Pause, Step, Stop, Accept, Discard and an
  objective plot; rigid CPD with the reference E-step). Two shortcuts, commented: both clouds in their own object space,
  and the positions stand in for the editing service's always-required normal input. One finding while testing: the
  solver detects convergence in the next call's E-step, which ends without an iteration; that last step's metric holds
  that E-step's values. The point-cloud editing service: `Current` and `Publish` require the capture's world to be the
  active one (`PublishScalars`' shared publisher already did; a check added there was redundant, a mutation showed it,
  removed); `PreviewPositions` (a CPU front of the positions' ring through `UploadGpuPropertyFront` in
  `Runtime.GpuPropertyBinding`, which reuses `Graphics::SubmitBufferUpload` and records the upload as the slot's
  completion) and `DiscardPreview` (only the capture's ring generation; also before `Publish`). The plugin harness
  composes the step-run module and optional extra modules. Tests: `Test.JobFormSpikePlugin.cpp` (5: the run equals the
  solver's own calls bit for bit, metrics and written positions, Accept once with one undo entry; a changed target
  and another world end it discarded, stale, without a write, and the output is free again; a paused run holds its
  output (a second run refused, a Jobs row); an unload ends it discarded; the window's buttons through
  `ActivateItemByID`), `Test.PointCloudEditing.cpp` (the world case; a preview front with a pending upload neither
  reused nor freed, `MockDevice`), the Vulkan smoke `Test.JobFormSpikePluginGpuSmoke.cpp` (paused: the render block
  shows the front and the pixels move right while the canonical positions and the history stay, an editor GPU position
  run on the same positions refused; Discard: the ring gone and the CPU frame back; a successor previews in its own
  ring with the same frame; Accept: written once, the ring released, the accepted frame equals the preview). Full
  runtime contract suite 1960 passed (exit 1 from LeakSanitizer on Vulkan driver allocations in `vkCreateInstance`, as
  before). Mutation checks (8): 7 killed, the redundant `PublishScalars` world check survived and was removed. Not
  tested: `PreviewPositions` refusing a ring another result holds (the reverse, an editor run refused, is).
  - Reviews of S3b-2 (diff sha256 of review-s3b2.diff in the scratchpad; with the S3b-1 fixes and the CI fix):
    ponytail (fresh agent): "fix 1 first (small). 2 to 6 are optional"; taken 1 to 5: the tools' re-checks of what
    the dispatcher's schema already validates are gone and the MCP tests call through `InvokeAgentOperation`
    (`invalid_params`); a preview writes into the publication's scratch instead of a fresh copy; the one-use helper
    inlined; the ring generation a plain integer (0: none); a dead guard. Not taken: 6 (one pixel helper for four
    smokes), an unrelated clean-up unless the plugin stays. Codex (fresh, `codex-review-s3b2`): "Verdict: REVISE",
    three findings, and "The five previous S3b-1 findings are closed" and the CI fix "fits both
    cpu_test_selection.py and collect_test_timing.py". Fixed: (1) a copy `Show` refuses is offered again until shown
    or replaced, `ShownStep` counts only shown ones (`Show` returns whether it showed; `PreviewPositions` returns
    Shown, Busy or Unavailable and the plugin retries only Busy; test with three refusals); (2) the start tool bounds
    the entity ids to 32 bits (test through the dispatcher); (3) the late-completion order on a device:
    `ALateCompletionLeavesItsSuccessorsRing` (A held 1.5 s per step through the plugin's test hook `hold_step_ms`,
    discarded while a step is held: its ring goes at once, B is admitted and previews while A's step still runs,
    asserted, and A's late completion leaves B's ring generation). Also the window test presses Discard. Verification:
    28 focused contract tests, both Vulkan smokes.
- 2026-11-03, S3c implementation plan (within plan revision 3, S3c: "GPU k-means with resident buffers as a step run,
  inside the clustering module ... through the same StepRun"; Q71). Facts (Explore, 2026-11-03): the resident k-means
  (`Runtime.ClusteringGpuState.cpp`) advances only on the main thread, in frame maintenance (its GPU queue
  participant's `DrainCompletedTransfers` calls `Advance`, which consumes a finished page and records the next with
  `QueueGpuCompute(Immediate)`, at most one submission a frame); an iteration ends at the Update boundary (32-byte
  diagnostics, convergence, cap, Stop latch; a FinalAssignment pass when centroids moved); previews are its own label
  ring publications every `GpuPreviewInterval` iterations; Accept is an asynchronous front readback job; the run has
  no Jobs row and no job while it iterates; no step budget; MCP only `run_kmeans`/`preview_kmeans` (auto-accept). So
  S3b's worker step form does not fit: a worker `Step` would block for frames.
  - **S3c-1, the driver's second step form** (`StepRuns`): `StepRunCallbacks::Advance` (main thread, in the pump, while
    computing and no worker step form is given): the owner moves its GPU work on and reports nothing yet, a completed
    step (with its metric) or an end (converged, failed); the driver gives it `mayContinue` (false once paused, the
    step budget is spent or a restart waits), and the owner stops at its next iteration boundary. `Commit` may answer
    pending: the run stays `accepting` (the holder's gate keeps it parked and polls `Commit` each pump) until it
    answers committed or refused; discard and cancel during accepting end it discarded (the owner's `Discard` abandons
    the readback, as the transaction core does). Owner previews (the label ring) stay the owner's; `Preview`/`Show`
    unused. Tests with a fake main-thread owner: step counts and pauses at boundaries, no step after pause, the
    accepting phase (refused controls, discard during it, a late answer ignored), run equals step sequence.
  - **S3c-2, k-means as a step run** (clustering module, no plugin: R2): a step budget at the iteration boundary of
    the paged state machine (`mayContinue` false: stop there, Ready, resumable); the module owns a step run per GPU run
    (holder and its Jobs row through the editor job surface; its MCP control through `run_control`/`run_observe`; the
    panel's Stop/Accept/Discard map onto it), the label ring stays its preview (latest wins as today), Accept reads the
    labels once. Tests (contract, the fake compute device, plus the Vulkan smoke): run equals step sequence (labels
    exact, centroids bitwise) for step(1)*N, step(3)+run and run with pauses; zero full-array bytes per step after Init
    (the readback counters split: init, per step (32 B diagnostics), terminal centroids, accept); cancel and previews
    commit nothing; Accept once; accept-on-convergence only when converged.
  - **S3c-3, the stall measurement** (full pre-flight: two fresh reviewers, a frozen packet, an HTML report from raw
    output): the main-thread time of every path a run uses (completion drain, GPU transfer drain, capture and init,
    preview upload) as frame-pacing fields, under a slider preview (a parameter change restarting the preview run every
    frame), with counts of accepted replacements and completed steps; against the proposed 4 ms. Planned after S3c-2.
  Not in S3c: the editor's k-means panel beyond mapping its buttons, other GPU methods, the CPD editor session.
  - Codex plan review (fresh, `codex-plan-s3c`): "Verdict: REVISE"; it confirms the shape ("A main-thread `Advance`
    callback plus pending/committed/refused Commit is the smallest reasonable extension") and names six blocking
    gaps: (1) an iteration boundary is not an acceptable result (centroids moved at Update before FinalAssignment; a
    non-terminal preview is fused with the next Assign), so pause, resume and Accept from a pause need their own rules;
    (2) an asynchronous Commit needs a latched outcome before the pump's capture check (a successful publication
    changes the watched outputs); (3) batch auto-accept (`run_kmeans`, the one-iteration smoke case) must stay distinct
    from accept-on-convergence, and `preview_kmeans` only checks readiness; (4) Stop stays irreversible against a late
    convergence; (5) latest wins needs a real replacement path in clustering (it refuses a second run today) and owner
    previews do not get the driver's suppression or rate limit; (6) the Jobs row and cancel must go through the editor
    surface (today's clustering job adapter drops the identity). Scope: well beyond revision 3's "a step is one
    iteration of the existing paged state machine"; asked the operator before going on (AGENTS drift budget).
- 2026-11-03, with the display on (Q73; `build/dev-shared` at b980e76f9): S1 60 of 60, S2 116 of 116 (the four timing
  checks the 1 Hz presentation had failed now pass), the technical R4 run 13 of 13 (pass/fail only, no numbers read).
- 2026-11-03, plan of block A (Q72, Q73 item 1 and the fixture): the spike category goes.
  - `plugins/spike` becomes `plugins/fixture` (the host tests' and drivers' test plugin): its target, module, tools,
    window, generated version header, the CMake option (`INTRINSIC_PLUGIN_SPIKE` becomes `INTRINSIC_PLUGIN_FIXTURE`),
    the drivers, the probes namespace stays; current docs follow, dated reports and evidence keep their wording.
  - `plugins/job_form_spike` becomes `plugins/coherent_point_drift` (plugin `coherent_point_drift`, tool
    `coherent_point_drift_start`, window `view.coherent_point_drift`), an ordinary plugin beside the editor's
    built-in CPD until that feature migrates.
  - World space, as the editor's CPD does it: a request flag `CaptureTransform` in the point-cloud editing service
    captures the entity's local Transform (the model matrix in the view), refuses a parented entity, and `Current`
    then also requires the Transform unchanged (`SameTransformComponent`). The plugin registers model-mapped points
    and publishes the source's positions mapped back by the inverse of its model matrix; previews likewise.
  - An optional normal input: an empty `Normals` name captures the positions alone (no normals in the view).
  - Tests: the editing service (no normals: captured, no normals in the view; with `CaptureTransform`: the model
    matrix, a parented entity refused, a moved entity stale); the plugin's run equals the solver's own run on
    model-mapped points with a transformed source and target, and the written local positions map back.
  - Codex plan review (fresh, `codex-plan-a`): "Verdict: REVISE", seven findings. Revision 2:
    1. **No window collision:** the editor's CPD panel already registers `view.coherent_point_drift`; the plugin's
       window is `view.coherent_point_drift_steps` ("Coherent Point Drift (steps)"). Test (Sandbox editor integration,
       the real panels): every startup plugin's window id is free in the Sandbox shell.
    2. **Transform validity covers the hierarchy and publication:** with `CaptureTransform`, `Current` and `Publish`
       (and `PublishScalars`) require the entity still unparented, the Transform component still present (or still
       absent) and unchanged. Tests: the source or the target moved, given a parent, a Transform added or removed:
       stale, and a direct `Publish` writes nothing.
    3. **The inverse and the values are checked:** the plugin maps with the full affine model matrices (no TRS
       decomposition), refuses a source whose model matrix is not invertible or not finite at Start, and checks every
       mapped-back position finite before a preview or Accept; the service also refuses non-finite positions in
       `Publish` and `PreviewPositions` (nothing written). Tests: a rotated, non-uniformly scaled and shifted source
       and target against the solver's own run on the mapped points; a singular source refused; a non-finite value
       refused by the service.
    4. **The rename is complete:** root CMake, the plugin targets and module imports, the test sources and object
       links, the named GPU timeout, both keys of `feature_boundaries_baseline.json` (zero ceilings kept),
       `Test.CheckFeatureBoundaries.py`'s fixture paths, `PluginContract.cppm`'s comment, current docs and the lesson.
       Corrected inventory: there is no `INTRINSIC_PLUGIN_SPIKE` option at HEAD (the fixture builds with
       `BUILD_SHARED_LIBS` and the Sandbox target) and no fixture module; neither is introduced.
    5. **Drivers together, history kept:** the maintained drivers and their self-tests (`spike_driver.py` becomes
       `s1_driver.py`, `reload_driver.py`, `r4_driver.py`, `r4_report.py`, `r4_selftest.py`, the LSan suppression) move
       with the fixture's target, library, generated header, macros and tool names; the bilateral and descriptor live
       drivers import them from the new place. `tasks/evidence/ARCH-022/r4_run.sh` stays as it is: it runs only at its
       frozen commit (it checks HEAD) and its raw results stay. S1, S2 and the technical R4 run on the renamed fixture
       in a shared tree prove the rename (the in-process "fixture" of `Test.PluginHost.cpp` does not).
    6. **Transformed preview and history:** the Vulkan smoke uses a transformed source (rotated, scaled, shifted): the
       preview frame and the accepted frame agree; moving the source or the target while paused discards the run and
       its ring without a write. Contract: Accept, undo, redo with transformed inputs and a deleted source slot restore
       the stored local arrays.
    7. **Optional normals on both paths:** an empty `Normals` name takes the positions alone through
       `CapturePointInput` (copy) or `PreparePointInput` (readiness); tests: readiness without copy, a cloud without
       normals, a later normal edit does not stale such a capture; a given but invalid normal binding still fails and a
       given normal input stays watched.
  - Codex round 2 on the block A plan: "Verdict: APPROVE", all seven findings "closed at plan level".
- 2026-11-03, block A implemented: `plugins/fixture` (was `plugins/spike`: `IntrinsicFixturePlugin`,
  `FixturePlugin.cpp`, `FixtureVersion.inc` with `FIXTURE_*` macros, plugin name and tools `fixture`, `fixture_echo`,
  `fixture_identity`, `fixture_version`, the drivers `s1_driver.py`, `reload_driver.py`, `r4_driver.py`, `r4_report.py`,
  `r4_selftest.py`; the bilateral and descriptor live drivers import them from there; the boundary baseline and its
  test renamed; `tasks/evidence/ARCH-022/r4_run.sh` left as it is, it runs only at its frozen commit).
  `plugins/coherent_point_drift` (was `plugins/job_form_spike`: plugin `coherent_point_drift`, tool
  `coherent_point_drift_start`, window `view.coherent_point_drift_steps`), in world space: the point-cloud editing
  service's `CaptureTransform` (an unparented entity only; its local Transform's matrix in the view; `Current`,
  `Publish` and `PublishScalars` stale once the entity gets a parent or its Transform changes, appears or goes) and an
  empty normal name (the positions alone, copied or readiness-only); the plugin maps both clouds by their model
  matrices, refuses a source matrix that cannot be inverted, and maps the result back by the full affine inverse; the
  service refuses non-finite positions in `Publish` and `PreviewPositions`. Tests: the editing service (positions
  alone on two clouds and in readiness, a normal edit leaves such a capture current while a given normal input stays
  watched, an invalid normal input still fails; the captured Transform moved, parented, removed, rescaled, appearing:
  stale and neither publication writes; a parented entity refused; a non-finite position refused), the plugin
  (transformed clouds: the run equals the solver's own run on the mapped points and Accept writes the mapped-back
  values into the live slots, a deleted slot kept, undo and redo; a singular source refused; the source moved while
  paused ends the run discarded), the Sandbox panels (every startup plugin's window id free beside the built-in
  panels), the Vulkan smoke on rotated, scaled and shifted clouds (preview and accepted frames equal; moving the source
  while a later run is paused discards it and its ring without a write). Live, display on: S1 60 of 60; S2 112 of 116
  first (the four frame-rate checks: 8 of 66 queued 60 s jobs finished before the reload), then 116 of 116 on a rerun;
  technical R4 13 of 13; the bilateral live driver 62 of 62; the descriptor live driver 39 of 39.
- 2026-11-03, CI note: `RuntimeJobServiceEngineWiring.RunFrameAppliesSubmittedJob` failed once on 0a6fc4380 (its
  application asks to exit after 8 frames, before a loaded runner's worker ran the job) and passed on the rerun; not
  in ARCH-022's scope, recorded here as a flake.
  - Mutation checks (10): all killed (`Current`, `Publish`, `PublishScalars` without the Transform check; no parent
    check; non-finite positions published; no positions-only capture; no world mapping; no inverse mapping back; a
    singular source accepted; the plugin's window on the built-in panel's id).
  - Reviews of block A (diff sha256 a851a648…): ponytail (fresh agent): "fix 2 first. 1 is should-fix"; taken 1 to 7:
    the panel test reads every startup plugin from `INTRINSIC_STARTUP_PLUGINS` (a generated header, as the Sandbox
    builds its list) instead of naming three; rename leftovers (an evidence path the replacement broke, a comment,
    the R4 row, the tests' `JFS` alias); one mapping helper; one inverse check (`glm::inverse` of a singular or
    non-finite matrix is non-finite); the parent check in one place; `Geometry::Validation::IsFinite`; the test hook
    `hold_step_ms` marked as a shortcut until a plugin can see `--plugin-test-hooks`. Codex (fresh, `codex-review-a`):
    "Verdict: REVISE", three findings, fixed: (1) `PublishScalars` checks the world before the Transform, which reads
    the captured scene (test: the captured world destroyed, stale without reading it); (2) `Show` answers Shown, Later
    or Never, and only Shown counts (`StepRunShow`; a copy that cannot be shown, e.g. without a device, is neither
    retried nor counted; tests in StepRuns and the plugin); (3) the Vulkan smoke checks a non-finite preview refused on
    a real device (no ring) and the target moved while a run is paused (discarded, ring gone, no write).
- 2026-11-03, nightly-deep on b980e76f9 failed: `StepRuns.OnlyTheNewest32RetiredRunsStayObservable` kept 40 runs (it
  pumped ten frames without waiting, before a two-worker runner had run the 40 holder jobs' work, so none had
  retired). Fixed: it waits until all 40 Discards ran; 60 of 60 repetitions in six parallel processes.
