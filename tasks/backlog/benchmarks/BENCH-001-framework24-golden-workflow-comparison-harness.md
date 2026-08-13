---
id: BENCH-001
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog. This task adds benchmark manifests, process-level adapters, and result evidence without changing a method, engine API, geometry data domain, publication contract, or control surface."
---
# BENCH-001 — Framework24 golden-workflow comparison harness

## Goal

- Produce reproducible, claim-eligible matched measurements for the Framework24
  product-convergence workflows: startup/first frame, import-to-visible,
  import-to-method-ready, optional enrichment, resident curvature, steady-state
  frame phases, peak memory, and implemented method backends.

## Non-goals

- No production-engine optimization or behavior change.
- No benchmark that links IntrinsicEngine code against Framework24.
- No claim from a dirty comparison checkout, a sanitizer/debug
  build, unmatched output semantics, kernel-only timing, or one warm run.
- No requirement to check a large external mesh into the repository.

## Context

- Owner: `benchmarks/` manifests and public/process-level runners. Engine and
  Framework24 adapters execute independent binaries and write the shared result
  schema; neither becomes a dependency of the other.
- `docs/product/framework24-convergence.md` defines the comparison thresholds
  and golden workflow boundaries. Existing diagnostic runs identified import
  enrichment, FastStaged atlas construction, resident topology rebuilding,
  curvature publication, and render preparation as boundaries that must be
  measured independently.
- The checked-in `tests/data/sculpt.obj` and `assets/models/child.obj` provide
  bounded smoke/medium inputs. Full claim runs may consume a declared local
  large-mesh dataset under the existing dataset policy.

## Control surfaces

- Config: runners record the exact engine config and method/backend request.
- UI: no benchmark-only UI state; app workflows are driven through supported
  command-line/report seams or deterministic automation.
- Agent/CLI: one documented runner emits schema-v2 result JSON and diagnostics.

## Backends

- Backend axis: record requested, actual, and fallback for every method run;
  compare only real implementations declared by the corresponding manifest.

## Required changes

- [ ] Add stable benchmark manifests for the required product timing boundaries
      and representative small/medium inputs.
- [ ] Add process adapters that run optimized IntrinsicEngine and Framework24
      binaries independently against the same asset and requested output.
- [ ] Separate parse/topology, visible/selectable readiness, method readiness,
      UV/texture enrichment, resident curvature kernel, publication/
      visualization, RenderPrep, render execution, and complete-frame timing.
- [ ] Record median, p95, paired ratios/confidence bounds, peak resident memory,
      output cardinality/finite-value diagnostics, source state, hardware,
      config, and exact comparison revision.
- [ ] Provide a PR-fast smoke using checked-in small data and an opt-in full
      protocol without requiring Framework24 or large data in default CI.
- [ ] Freeze and seal the claim protocol/result bundle before any scorecard row
      is accepted.

## Tests

- [ ] Validate manifests and result JSON with the repository validators.
- [ ] Test adapter failure propagation, timeout, missing comparison binary,
      mismatched output, dirty source, and requested/actual backend reporting.
- [ ] Prove the smoke is deterministic and never treats a skipped external
      comparison as a pass.

## Docs

- [ ] Document exact build modes, fixtures, invocation, metrics, and evidence
      limits in the benchmark README/report.
- [ ] Link accepted results from the product scorecard and add ARA claim rows
      before stating any performance conclusion.

## Acceptance criteria

- [ ] One command can produce a validated matched result bundle on a host with
      both optimized binaries available.
- [ ] Cold, resident, enrichment, render-phase, and end-to-end measurements are
      not conflated.
- [ ] A wrong, invisible, fallback, or diagnostically incompatible result is
      rejected before timing comparison.
- [ ] The default repository gate has no external Framework24 or large-dataset
      dependency.

## Verification

```bash
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/agents/check_ara_claims.py --root . --strict
ctest --test-dir build/ci --output-on-failure -L benchmark \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

## Forbidden changes

- No production dependency on `experimental/framework24`.
- No speedup claim without a baseline comparison on a clean exact or approved
  sealed source identity.
- No omission of transfer, publication, visualization, or fallback cost from
  a result described as end to end.
