---
id: REVIEW-004
theme: J
depends_on:
  - ARCH-017
  - ASSETIO-012
  - BENCH-001
  - BUG-154
  - BUG-156
  - BUG-158
  - BUG-159
  - BUG-160
  - GRAPHICS-135
  - METHOD-015
  - RUNTIME-218
  - UI-046
  - UI-047
  - UI-048
  - UI-049
  - UI-050
  - UI-051
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog. This one-shot audit evaluates existing product, architecture, geometry, publication, method, and control-surface contracts but changes none; each blocking implementation finding receives a separate contract-enrolled task."
---
# REVIEW-004 — Framework24 product-convergence audit

## Goal

- Decide, on one clean exact revision with matched sealed evidence, whether
  IntrinsicEngine has become a comparable-or-better Framework24 replacement
  across all golden workflows while retaining its modular Vulkan architecture
  and reliability guarantees.

## Non-goals

- No production, benchmark, task-tool, or validator implementation changes.
- No absorption of a discovered fix into this audit.
- No permanent claim that later revisions cannot regress.
- No research restart from a partial or mixed-source result.

## Context

- Owner: one-shot product/architecture/results audit under Theme J.
- The workflow and thresholds are authoritative in
  `docs/product/framework24-convergence.md`.
- Static dependencies are the currently known blockers. If the audit finds a
  new blocker, it opens one scoped task, adds that ID here, regenerates the
  session brief, and stops. Remediation tasks must not depend on `REVIEW-004`.
- The complete audit restarts on a fresh clean revision after every blocker
  retires; rejected partial evidence is diagnostic only.

## Required changes

- [ ] Confirm every static dependency is retired before beginning the audit.
- [ ] Record exact IntrinsicEngine and Framework24 source identity, clean/dirty
      state, host, driver, toolchain, build mode, config, fixtures, and display
      conditions.
- [ ] Execute W1-W6 exactly as specified and record each as pass or finding.
- [ ] Audit the full Framework24 feature inventory: each in-scope feature is
      accepted, deliberately superseded with a tested workflow, or assigned one
      concrete owner; no “architecture exists” row counts as product parity.
- [ ] Audit every method manifest and integrated config/agent/UI selector for
      CPU-reference truth, real optimized/parallel/Vulkan implementations, and
      requested/actual/fallback honesty.
- [ ] Run the architecture, right-sizing, source-documentation, results, and
      docs/evidence review checklists on the same frozen surface.
- [ ] For every blocking finding, open a scoped remediation task, add it to
      `depends_on`, regenerate `tasks/SESSION-BRIEF.md`, and stop.
- [ ] Publish the final accepted or rejected product verdict with evidence
      limits; resume Theme I only for an accepted verdict.

## Tests

- [ ] Pass the complete default CPU, isolated ASan, isolated UBSan, and promoted
      Vulkan gates on the audited revision.
- [ ] Pass all strict structural, task, documentation, manifest, benchmark,
      result, ARA, layering, and source-documentation checks.
- [ ] Validate the claim-grade experiment bundle and independent fixed-surface
      review.

## Docs

- [ ] Update every scorecard row with accepted evidence or its new blocker.
- [ ] Add ARA claim rows before publishing performance, parity, or capability
      statements.
- [ ] Record the exact revision-bound audit report under `docs/reports/`.

## Acceptance criteria

- [ ] W1-W6 are all `Accepted` with no known unowned in-scope gap.
- [ ] Matched comparisons meet the declared time and memory gates or carry a
      specifically accepted, evidence-backed product tradeoff.
- [ ] Correctness, visibility, fallback, validation, sanitizer, and
      architecture non-regression gates all pass.
- [ ] The independent reviewer accepts the exact frozen surface and claim
      bundle.

## Verification

```bash
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -L gpu -L vulkan --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Forbidden changes

- No implementation repair inside the audit.
- No reuse of rejected partial evidence as the final verdict.
- No research restart while any scorecard row remains open or candidate.
- No weakening, skipping, retrying away, or quarantining a gate to obtain an
  accepted result.
