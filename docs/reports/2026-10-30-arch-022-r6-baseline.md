# ARCH-022 slice 1: timing baselines for rule R6

Rule R6 of the architecture target ([Engine and feature modules](../architecture/engine-and-feature-modules.md)):
"Build and CI are fast". Measured on 2026-10-30 at source revision `adf7e0b3c`, tracked in
[`ARCH-022`](../../tasks/active/ARCH-022-engine-and-feature-modules-target.md), slice 1. This is a baseline from which the
operator sets the R6 targets; it compares nothing.

## Summary

| What | Wall time | Spread (5 samples) | Compiled units |
|---|---|---|---|
| No-op rebuild | 0.14 s | 0.14 to 0.14 | 0 |
| Edit an operation unit (`Runtime.OutlierAnalysisOperation.cpp`) | **19.1 s** | 18.9 to 19.2 | 1 |
| Edit a plugin config interface (`Runtime.OutlierAnalysisConfig.cppm`) | **48.6 s** | 48.5 to 49.1 | 20 |
| Edit a geometry interface (`Geometry.Registration.cppm`) | **18.7 s** | 18.5 to 18.7 | 3 |
| Clean build of the Sandbox (one measurement) | **2.0 min** build (119.6 s), plus 8.2 s configure | n/a | 900 |
| CI `ci-linux-clang`, last 20 green runs on `main` | **41.1 min** median | 21.4 to 70.8 min | n/a |

- **Every edit pays one Sandbox link of about 12.8 s** (median over all 15 measured edits; 12.65 to 13.07). It is the
  last step on every critical path: 67 % of the operation-unit rebuild, 69 % of the geometry-interface rebuild and 26 %
  of the config-interface rebuild. The linker is GNU `ld`, clang's default here: `CMAKE_LINKER` is `/usr/bin/ld` and the
  build sets no `-fuse-ld`. The exact link command was not recorded. On this host at writing, lld and mold are not
  installed; gold is.
- **The plugin config edit runs down a serial chain of module interfaces into the Sandbox editor.** Each step waits for
  the one before:
  1. `Extrinsic.Runtime.OutlierAnalysisOperation.pcm` (5.3 s);
  2. `Extrinsic.Runtime.PointAnalysisOperations.pcm` (4.5 s);
  3. `Extrinsic.Sandbox.Editor.MeshProcessingPanels.pcm` (8.5 s);
  4. `Sandbox.MeshProcessingPanels.cpp.o` (16.7 s);
  5. the link (12.8 s).

  It reaches 20 units in all, in every sample: the interface and all 19 of its transitive importers in the Sandbox
  target. Its test importers are not part of that target and were not built. The path runs through the runtime's shared
  `PointAnalysisOperations` interface and an app editor panel. By reading of the target, not by measurement, that is the
  kind of hub dependency rule R1 removes.
- The geometry interface has a single importer outside geometry, so its rebuild costs about as much as an operation
  unit.
- CI varies more than threefold across the cohort (21 to 71 min). The edit rebuilds varied by at most 1.6 % (no-op
  2.3 %).

## What was measured

- **Workload** (the approved slice 1 plan, as the operator decided in Q45: "ok then Option 1"):
  - preset `dev`: Debug, ASan and UBSan, tests ON, target `ExtrinsicSandbox`;
  - a build tree on disk, configured and clean-built once;
  - Ninja's default parallelism (34 jobs on 32 logical CPUs);
  - ccache as configured: it is not installed on this host, so there is no compiler cache.
- **Edits:** each one appends a unique declaration to the file, then rebuilds. Build completion is the proxy for "the
  Sandbox can run again"; start-up was not timed.
- **Order and samples:** one warmup and five measured samples, each running noop, operation, config and geometry in
  that order. The warmup's incremental scenarios are shown in the raw outputs and excluded from the table.
- **Host:** AMD Ryzen 9 7950X (16 cores, 32 threads), 62 GiB RAM, NVMe, compiler `/bin/clang++-23`.
- **Peak memory:** maximum single-process RSS 2.99 GiB, the same in every scenario that links. Aggregate memory was not
  measured.
- **CI:** minutes from the start of the latest attempt to the end of the run. The cohort is 20 push runs between
  2026-10-29 18:13 and 2026-10-30 21:14 UTC, none re-run, queried at 2026-10-30T22:25:48Z.

## Validity

- **Pre-flight:** full tier, packet digest `c77edd2e278f06fc197b1fdb72342b4951eb1da9f08808a0c7e3f7e76e6d3fcb`. Both a
  fresh Claude reviewer and a fresh Codex reviewer gave PASS on the fourth packet version. The first version measured a
  controlled proxy instead of the plan's workload, and the reviews caught that. The start gate rebuilt the manifest from
  the live state, and it matched.
- **Runner assertions, all passed:**
  - the source was exactly the commit plus the probe lines before and after every step;
  - every no-op build and every settle build after an edit produced no output except CMake's glob re-check;
  - each edited file and each named importer compiled;
  - the dependency tree was unchanged;
  - no compiler cache was used;
  - every result sealed and validated.
- **Host interference:** no measurement was flagged.
  - The largest estimated CPU residual was 0.36 CPU on average; the flag is at 1.
  - The largest memory stall was 0.003 % of wall time, during the clean build; the flag is at 1 %.
  - No page went to swap. One measurement saw one page swapped in and 16 major faults; the others had 0 to 3 major
    faults.
- **Predictions written before the run:**
  - noop under 2 s, operation unit under 2 min, geometry interface under 3 min, CI median 30 to 50 min: all held.
  - Config interface, predicted at 1 to 8 min, took 48.6 s, below the range.
  - The clean build, predicted at 10 to 45 min, took 2.0 min.

  Before the run, the coordinator had seen a technical test's clean build of about 2 minutes, measured in RAM with 32
  jobs. That exposure is recorded in the packet, and the prediction was left unchanged.
- **Audit:** the coordinator recomputed the summaries from the raw outputs, independently of the report script, and they
  agreed with the HTML report. A separate agent then checked every number in this document against the raw outputs; its
  corrections (rounding, the link median, the importer count wording) are applied.

## Limits

- One host, one revision, one preset and one linker.
- A Sandbox restart after the rebuild is not included.
- These numbers do not say what a faster linker, shared libraries or loading plugins at run time would save. Each is a
  hypothesis for a later slice, to be measured against this baseline with the same runner and manifest
  (`benchmarks/ci/manifests/arch022_r6_dev_loop_baseline.yaml`).
- The raw outputs and the HTML report are kept outside git in `tasks/evidence/ARCH-022/r6-baseline/`.
