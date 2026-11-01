# ARCH-022 R4 baseline: from saving a plugin edit to the reloaded plugin running

Date: 2026-11-01. Task: `ARCH-022` (S2d). Status: measured under the full pre-flight. This is a spike-fixture
baseline. Whether a typical plugin meets R4 stays open.

## Question
The operator's questions (verbatim):
- Q36: "Ich will auch Laufzeit Kompilierung damit Agenten etwas ändern kannst du die sandbox/engine weiterlaufen
  kann."
- Q46 accepted the target "R4 under 10 s".
- Q50 asked for S2, whose last pass criterion is this measurement.

Rule R4 of `docs/architecture/engine-and-feature-modules.md` measures the time from saving an edit to the reloaded
plugin running.

## What ran
- Packet `tasks/evidence/ARCH-022/preflight-r4/`: revision 4, digest
  `1445006114b8afb798dc106b0d52d8ca5389b9537824cd8ecb5a23a1a5fb1f35`, commit `2b26c6d4f`.
  - Claude and Codex each gave PASS on that digest after four rounds; the reviews and the start gate are in
    `reviews/README.md`.
  - Run: `tasks/evidence/ARCH-022/r4_run.sh tasks/evidence/ARCH-022/r4`, 2026-11-01 01:28 to 01:32 UTC, exit 0.
- Fixture:
  - the spike plugin: one translation unit; the edit rewrites its generated version header;
  - the `dev` build (Debug, ASan and UBSan, lld 24, no compiler cache);
  - one host: Ryzen 9 7950X, 32 logical CPUs; RTX 4090.
- A sample: save the edit; `cmake --build --target IntrinsicSpikePlugin`; `plugin_reload` (drain, unload, load a
  private copy, register, commit, config reapplied); the plugin answers the new generation. Each sample had to show
  plugin edges only (no host rebuild), a clean close of the previous generation, the config restored, and the new
  generation.
- Variants: (a) the engine static inside the executable; (b) the engine as shared libraries. Two sessions each, in
  the order a, b, b, a, with 1 warmup and 5 measured samples per session.

## Result
All four sessions passed every check, none failed:
- 120 checks on the 20 measured samples (six predicates each) and 24 on the 4 warmups;
- 24 probes;
- 52 session-level checks.

Machine: PSI CPU 0.00 before every sample, load average at most 2.02.

| seconds, n = 10 per variant | (a) static, exporting executable | (b) shared libraries |
|---|---|---|
| total: median (min to max) | **9.74** (9.68 to 9.85) | **4.49** (4.46 to 4.50) |
| build | 9.50 (9.46 to 9.61) | 4.24 (4.22 to 4.26) |
| of it: plugin compile | 3.92 | 3.92 |
| of it: plugin link (lld) | 0.04 | 0.09 |
| reload | 0.20 (0.20 to 0.22) | 0.22 (0.20 to 0.22) |
| answer | 0.02 (0.02 to 0.04) | 0.02 (0.02 to 0.04) |
| warmup totals (not summarized) | 9.67, 9.72 | 4.48, 4.48 |
| probe: rebuild with nothing changed, n = 6 | 5.55 | 0.14 |
| probe: reload without an edit, n = 6 | 0.20 | 0.22 |
| peak memory: Sandbox VmHWM / largest build process | 1.04 GiB / 0.97 GiB | 1.07 GiB / 0.97 GiB |

Verdict by the frozen rule (every total under 10 s): **met** in both variants. Variant (a) meets it by 0.15 s.

## Against the predictions (written before any timing was read)

| prediction | measured | |
|---|---|---|
| build 2 to 8 s | (a) 9.50, (b) 4.24 | (a) outside |
| reload 0.05 to 1 s | 0.20, 0.22 | inside |
| answer under 0.1 s | 0.02 | inside |
| compiling the one TU is most of the build | (a) 41 %, (b) 92 % | (a) not supported, (b) supported |
| total median 2.5 to 9 s, verdict met | (a) 9.74, (b) 4.49; met in both | (a) outside the range, the verdict as predicted |
| rebuild with nothing changed under 1 s | (a) 5.55, (b) 0.14 | (a) outside |
| reload without an edit close to the measured reload | 0.20 / 0.20 (a), 0.22 / 0.22 (b) | supported |

The surprise is variant (a)'s fixed cost. This attribution is a post-hoc analysis; the frozen plan attributed no
difference to a single cause.
- Its build runs `CollectHostModules.cmake` every time: 5.40 s median (5.37 to 5.44 s) over the ten measured builds,
  from the tree's `.ninja_log`. The script
  reads all of the host's `.modmap` files to pass the host's module files to the plugin compile, because CMake 4.3's
  `$<COMPILE_ONLY:...>` does not carry module sets (S1).
- Without it, (a) would come near (b)'s time (3.92 s compile, 0.04 s link, 0.2 s reload). That is an unmeasured
  estimate.
- Variant (b) needs no such step, since CMake carries the module sets of linked shared libraries.

The surprise does not change the verdict. It does say where (a)'s time goes.

## What this does and does not show
- A one-file plugin edit reaches the running Sandbox in about 4.5 s with shared engine libraries, and in about 9.7 s
  with the static engine, of which 5.4 s is the spike's host-module collection.
- Compiling the plugin's one translation unit (3.9 s, Debug and sanitizers) is the largest cost in (b); in (a) the
  collection is. The reload (0.2 s) and the answer (0.02 to 0.04 s, frame-pump latency included) are small.
- Not shown:
  - a typical plugin with several translation units;
  - edits that change the host;
  - a file watcher's or an editor's delay (the build starts at the save).

  The reload and answer phases include the Sandbox's frame pump (calls are dispatched once per frame). The numbers
  are warm-loop numbers.
- The variants are two build arrangements as a whole, not two linkers.

## Raw outputs
`tasks/evidence/ARCH-022/r4/`:
- per session: `samples.jsonl`, `summary.json`, `report.html` and the Sandbox logs;
- per variant: `a/` and `b/`, with the pooled `summary.json` and `report.html`.

The summaries above were recomputed from `samples.jsonl` and equal the pooled ones.

## Follow-ups (not done; for the operator)
- If the static arrangement (a) is kept for plugins, collecting the host's module files only when the host relinks,
  instead of on every build, could remove up to about 5.4 s per edit (estimated, not measured).
- R4 for a typical plugin needs a fixture with several translation units.
