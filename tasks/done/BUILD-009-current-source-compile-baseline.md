---
id: BUILD-009
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive task; unattended workflow completion reports are exempt, but this task owns its benchmark manifests, results and source identities alongside review and test evidence.
contract_schema: 1
contracts: []
contract_review: Existing source measurement only; no catalog contract, benchmark schema, method, or engine ownership rule is changed. Benchmark evidence requirements still apply.
---
# BUILD-009 — Remeasure current engine compile costs

## Goal
Establish a fresh, source-bound compile-cost baseline and rank the remaining
editor, processing-config and renderer work before choosing more structural changes.

## Scope and starting point
- The operator explicitly requests actionable follow-ups to the compilation/reuse
  cleanup. This is engine-source measurement, independent of BUILD-006's
  build/cache-backend comparison; its CI infrastructure prerequisites do not apply.
- BUILD-007, RUNTIME-265 and GRAPHICS-138 are completed historical experiments.
  GRAPHICS-139–143 changed dependencies afterward without matched elapsed timings.
  Use `45d5a4f1f` as the known implementation checkpoint; resolve exact baseline
  and current commits at execution and freeze them before running. Never treat
  the historical 18.8/15/33.6-second values as current measurements.
- Reuse `tools/analysis/benchmark_compile_iteration.py`, `compile_hotspots.py`,
  the BUILD-007 protocol and schema-v2 manifest/result validators. Preserve
  original reports/manifests/results. Create the new follow-up manifest at
  `benchmarks/ci/manifests/engine_compile_iteration_followup.yaml` with stable ID
  `build.engine_compile_iteration.followup.v1`: this cohort adds editor/renderer
  probes beyond the frozen overnight scenario set. Freeze exact parameters and
  source revisions before validation or execution; do not rename historical IDs.
- Produce a baseline for RUNTIME-266, RUNTIME-267 and GRAPHICS-144. Each later
  source task still owes a matched immediate-before/after comparison; this
  task does not claim benefits for changes that have not been implemented.

## Acceptance criteria
- [x] Create the named follow-up manifest and freeze source revisions, scenarios,
      toolchain/preset, target, job count, warmup/cache state, repeat count and
      reporting limits before validation and timing. This task owns the source
      identity, result sealing/validation and evidence report; none is deferred.
- [x] Measure clean engine libraries, settled no-op, representative implementation
      edits, and consolidation-config, editor-snapshot and renderer interface edits.
      Include actual changed compiler units, dominating producers, weighted critical
      paths, CPU work, peak-process RSS and elapsed time. Separate target scopes.
- [x] Use at least three retained samples per comparison arm in a balanced order;
      preserve raw samples, negative/null results and rejected attempts. Do not
      run competing builds/tests or install/mutate shared dependencies during timing.
- [x] Preflight storage and use owned disposable source/build trees; disable package
      installation against the fingerprinted installed dependencies. Preserve all
      user builds and clean up only owned scratch storage. Freeze the storage
      choice across arms; record tmpfs capacity and host memory pressure alongside
      process RSS rather than treating tmpfs pages as measured process memory.
- [x] Validate machine-readable results and publish a concise comparison/ranking
      bound to exact sources. Record claim eligibility honestly; any performance
      statement follows AGENTS §8/8b and cites its matched evidence.
- [x] Link the baseline from the three implementation tasks and state the selected
      next candidate or evidence that no further split is justified. Preserve
      BUILD-006 as a separate backend decision.

## Verification
```bash
python3 tests/regression/tooling/Test.CompileHotspots.py
python3 tests/regression/tooling/Test_BenchmarkResultValidator.py
# Create/freeze the manifest and owned disposable checkout before these commands.
test -f benchmarks/ci/manifests/engine_compile_iteration_followup.yaml
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/analysis/benchmark_compile_iteration.py --source /tmp/intrinsic-build009/source --build /dev/shm/intrinsic-build009 --output ara/evidence/diagnostics/build009_compile_followup --manifest benchmarks/ci/manifests/engine_compile_iteration_followup.yaml
python3 tools/benchmark/validate_benchmark_results.py --root ara/evidence/diagnostics/build009_compile_followup/results --manifests-root benchmarks --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Execution plan — 2026-09-15
- Compare post-overnight `07a8b29147ccd642fcf827c6a6361ba3e1c29f13` with
  implementation checkpoint `45d5a4f1fc0c7e51172a0e4a0edbca2f675448ea`.
  Task-only HEAD `ea1d39507` has the same engine source as the latter.
- Frozen follow-up manifest: three samples per arm in BAABBA order (before,
  after, after, before, before, after), zero discarded warmup pilots, identical
  source/dependency pre-read. Eight scenarios: clean, no-op, workspace/renderer/
  config implementations, then config/snapshot/renderer interfaces.
- Reuse the existing runner unchanged; Clang 23 Debug ci, Null/headless,
  ExtrinsicRuntime closure, four jobs, caches/launchers disabled. No package
  installation; installed dependency hashes must remain identical after every
  configure/build. Do not run other compilers or tests during timing.
- Owned source worktree: `/tmp/intrinsic-build009/source`; owned disposable
  build: `/dev/shm/intrinsic-build009`. Preflight finds roughly 3.5 GiB disk,
  20 GiB tmpfs and 47 GiB available host memory; tracked source is about 343 MiB.
  Record exact storage/memory state in the run bundle; peak process RSS does
  not count tmpfs storage. Preserve unrelated worktrees/builds.
- Outputs first go to an append-only temporary population, then validated
  portable results and a report enter the repository. No current speedup,
  statistical guarantee, GPU or sanitizer claim is inferred from this plan.

## Plan review resolutions
- Claude reviewed the fixed manifest and existing runner. Pin C, C++ and matching
  scanner paths explicitly; both arms use Clang 23. Preflight successfully hashes
  the borrowed ignored dependency root, so no copy or traversal failure occurs.
- A synthetic nested-metric seal validates without being retained as measurement
  evidence. Existing tooling tests pass (26 hotspot, 15 result-validator cases).
  Exact task-head/checkpoint diff contains only task and trace files.
- Initial configure is the only configure-time metric by design; reconfigure was
  already measured by BUILD-007/008. Retain the reusable runner's other scenarios.
  Report actual compiler-unit counts and per-probe invalidation, retaining the
  minimal one-importer validity floor rather than requiring a positive benefit.
- Record normal desktop noise, lack of affinity/governor control and matched
  storage; local descriptive results imply no statistical/cross-host guarantee.
  Final owned build cleanup occurs after complete evidence capture; unrelated
  trees and any failed population remain intact.

## Completion — 2026-09-15
- Commit reference: protocol `6a000ce9d`; measured source checkpoints `07a8b29147`
  and `45d5a4f1fc`. The report/evidence archive accompanies this retirement.
- Completed at CPUContracted, the intended measurement/tooling endpoint. Frozen
  protocol commit `6a000ce9d`; exact source checkpoints are recorded above.
- Six retained samples, three per arm, pass source/command/dependency checks and
  canonical result validation. All 775 shared compiler commands and all installed
  dependency fingerprints match. No failed measured population was discarded.
- [Report](../../ara/evidence/tables/build009_current_compile_measurement.md) and
  [evidence index](../../ara/evidence/diagnostics/build009_compile_followup/evidence-index.json)
  contain raw samples, producer ranking, accounting, scope and controls. C97
  records the bounded observations; implementation/no-op overlap is retained.
- Claude's results and accounting reviews are resolved. Recomputed 24 raw Ninja
  critical-path durations/counts; tied path witnesses preserve equal durations.
- Pre-timing canonical ci build, 90 focused checks, 26 hotspot-tool tests and 15
  result-validator tests pass. Six measured library builds succeed. This task
  makes no Vulkan or sanitizer execution claim.
- Removed the owned timed build; retain the detached source only for RUNTIME-266's
  immediately following experiment. User build trees remain intact.
- RUNTIME-266 is selected first using the remaining snapshot/scene-edit cost;
  RUNTIME-267 follows its shared session boundary. GRAPHICS-144 owns the renderer
  follow-up. BUILD-006 remains the independent gated build/cache decision.
