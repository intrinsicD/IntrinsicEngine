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
