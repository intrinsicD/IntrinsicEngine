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
- [ ] Create the named follow-up manifest and freeze source revisions, scenarios,
      toolchain/preset, target, job count, warmup/cache state, repeat count and
      reporting limits before validation and timing. This task owns the source
      identity, result sealing/validation and evidence report; none is deferred.
- [ ] Measure clean engine libraries, settled no-op, representative implementation
      edits, and consolidation-config, editor-snapshot and renderer interface edits.
      Include actual changed compiler units, dominating producers, weighted critical
      paths, CPU work, peak-process RSS and elapsed time. Separate target scopes.
- [ ] Use at least three retained samples per comparison arm in a balanced order;
      preserve raw samples, negative/null results and rejected attempts. Do not
      run competing builds/tests or install/mutate shared dependencies during timing.
- [ ] Preflight storage and use owned disposable source/build trees; disable package
      installation against the fingerprinted installed dependencies. Preserve all
      user builds and clean up only owned scratch storage. Freeze the storage
      choice across arms; record tmpfs capacity and host memory pressure alongside
      process RSS rather than treating tmpfs pages as measured process memory.
- [ ] Validate machine-readable results and publish a concise comparison/ranking
      bound to exact sources. Record claim eligibility honestly; any performance
      statement follows AGENTS §8/8b and cites its matched evidence.
- [ ] Link the baseline from the three implementation tasks and state the selected
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
