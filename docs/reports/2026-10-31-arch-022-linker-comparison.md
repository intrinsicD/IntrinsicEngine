# ARCH-022: GNU ld against lld on the R6 workload

Rule R6 of the architecture target ([Engine and feature modules](../architecture/engine-and-feature-modules.md)) sets
"Sandbox rebuilt after one plugin edit in under 10 s". In the [R6 baseline](2026-10-30-arch-022-r6-baseline.md) the
Sandbox link (GNU ld, about 12.8 s) was the last step of every edit's critical path. This run measured how much of it lld
removes. Tracked in [`ARCH-022`](../../tasks/active/ARCH-022-engine-and-feature-modules-target.md) (Q46, step 2).

## Summary

| Scenario | GNU ld | lld | Wall saving | Link step, GNU ld / lld | Non-link difference |
|---|---|---|---|---|---|
| Edit an operation unit | 18.92 s (18.85–19.05) | **7.05 s** (7.01–7.09) | 11.87 s | 12.72 / 0.84 s | 0.00 s |
| Edit a geometry interface | 18.59 s (18.47–18.67) | **6.66 s** (6.61–6.71) | 11.93 s | 12.76 / 0.84 s | 0.02 s |
| Edit a plugin config interface | 48.79 s (48.53–49.15) | **36.71 s** (36.41–36.98) | 12.08 s | 12.72 / 0.84 s | 0.19 s |
| No-op rebuild | 0.14 s | 0.14 s | 0.00 s | none | 0.00 s |
| Clean build (one per run) | 118.44, 118.85 s | 106.86, 106.73 s | about 11.9 s | 12.77 / 0.86 s | |

Medians over 10 measured samples per arm (two runs × five); ranges are min–max.

- **The saving is the link.** lld links the Debug ASan/UBSan Sandbox in 0.84 s instead of 12.7 s. Everything else on the
  rebuild stays the same: the non-link medians differ by at most 0.19 s. Every lld sample of every edit is faster than
  every GNU ld sample.
- **The frozen decision rule is met:** "lld is a candidate default for dev builds".
- **Against the R6 target** of under 10 s per plugin edit:
  - With lld, the plugin's operation-unit edit (7.05 s) meets it. The geometry-interface edit (6.66 s) is also under
    10 s, but it edits a core library, not a plugin.
  - The plugin config interface (36.71 s) does not. That rebuild is a serial chain of module interfaces from the plugin
    through the runtime's shared `PointAnalysisOperations` into a Sandbox editor panel (R6 baseline), which no linker
    removes.
- **No measurement was flagged**: no CPU residual above 1 CPU, no memory stall above 1 %, no swap-out.
- **No drift:** the two runs of each arm agree within 0.3 s per incremental scenario. The clean builds of the two runs
  are 0.41 s apart for GNU ld and 0.13 s for lld. The runs went GNU ld, lld, lld, GNU ld.
- The binary is about the same size: median 706,218,240 bytes with GNU ld, 708,682,216 with lld.

## What was measured

- **Workload:** the R6 workload, four times at revision `adf7e0b3c`, in one worktree path:
  - preset `dev`: Debug, ASan and UBSan, target `ExtrinsicSandbox`;
  - a build tree on disk, configured and clean-built once per run;
  - one warmup and five measured samples of a no-op and three edits, each edit appending a unique declaration;
  - Ninja's default parallelism; no compiler cache.
- **The only difference between the arms is the linker.**
  - GNU ld: binutils 2.38, `/usr/bin/x86_64-linux-gnu-ld.bfd`, clang's default.
  - lld: LLD 24.0.0, an apt.llvm.org snapshot. It was extracted without root and is called through a wrapper that
    loads its `libLLVM`.
  - lld was selected with `--ld-path` in the CMake linker flags.
- **Host:** AMD Ryzen 9 7950X, 32 threads, 62 GiB.

## Validity

- **Pre-flight:** full tier, packet digest `e758d333b3585c7e24ef6fb85a0c73062d070e7c4a1041e4b72c858d27848156`.
  - Two fresh reviewers, Claude and Codex, gave PASS on the third packet revision.
  - The reviews forced the gates on arm equivalence and the link-attribution rule.
  - The start gate rebuilt the manifest from the live state and matched the digest.
  - CI was green on the measured runner before the start.
- **The report's gates all held.** It was generated only after:
  - each run was checked against its frozen manifest and the repository's runner;
  - the complete populations and sealed results were checked;
  - every sample's binary was checked for its arm's linker, by the `.comment` linker line and by the link command;
  - all 24 link commands were checked to be identical except the lld selector;
  - the CMake caches were checked to be identical except the three linker-flag entries;
  - one dependency snapshot, identical compile commands and identical compiled edge sets per scenario were required;
  - every sample's warmup or baseline label was checked against its position.
- **Audit:** the coordinator recomputed every summary, separation and flag from the raw outputs independently of the
  report script, and they agree (recorded in the task log). A separate agent then checked every number in this
  document against the raw outputs.
- **Predictions written before the run:**
  - Held: GNU ld link 12–14 s; GNU ld rebuilds; GNU ld clean build 115–125 s; lld operation unit 7–11 s; lld config
    interface 36–41 s; lld clean build 100–115 s; no-op unchanged.
  - Below the range: the lld link (predicted 1–4 s, measured 0.84 s) and the lld geometry-interface rebuild (predicted
    7–11 s, measured 6.66 s).

## Not shown here

- Whether the lld-linked Sandbox runs correctly; the binary was not run.
- Adopting lld as the default is a separate change with its own tests:
  - the CPU test suite linked with lld;
  - the Sandbox started with lld;
  - how lld is provided (a packaged `lld-24` rather than the wrapper).
- CI: its build links test binaries, not the Sandbox, and was not measured with lld.
- mold, gold, other presets, revisions or hosts.
- The raw outputs and the HTML report are kept outside git in `tasks/evidence/ARCH-022/ld-compare/`.
- The start gate, the Codex answers and the verdict summary are in `tasks/evidence/ARCH-022/preflight-ld/reviews/`. The
  Claude reviewer's full answers are in the coordinator's session transcript.
