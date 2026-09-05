---
id: BUG-168
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "interactive CI baseline repair; evidence is the reviewed metadata diff, focused regression, and hosted full-CPU gate"
owner: Codex
branch: codex/bug-166-historical-input-seals
worktree: /tmp/intrinsic-bug166
claimed_at: "2026-09-05T03:18:00+02:00"
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog; this repair updates two existing compile-hotspot physical identities after a mechanical source move and adds metadata-integrity coverage. It changes no reusable task, source-documentation, engine, geometry, method, or cross-layer contract."
---
# BUG-168 — Runtime source move left compile-hotspot physical identities stale

## Status

- Completed and retired on 2026-09-05. The two baseline rows now name the
  object outputs emitted from the current `Kernel/` and `Rendering/`
  directories, and their physical edge IDs are recomputed from the unchanged
  source/kind/output identity contract. Timing ceilings are unchanged.
- Implementation commit: `3ee6a343d`; pull request
  [#1037](https://github.com/intrinsicD/IntrinsicEngine/pull/1037).
- Hosted `ci-linux-clang` run
  [`33936262942`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/33936262942),
  job `101224743953`, passed all 4,263 CPU-supported tests and resolved all
  four measured compile edges before the hotspot comparison passed.

## Goal
- Restore the hosted full-CPU compile-hotspot gate after the runtime source
  organization moved two measured module interfaces into ownership
  subdirectories without updating their object output paths and physical edge
  IDs.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` and `Runtime.RenderExtraction.cppm` baseline rows
      name the object outputs emitted from their current `Kernel/` and
      `Rendering/` paths, with edge IDs recomputed from the unchanged
      source/kind/output identity contract and timing budgets left unchanged.
- [x] A focused regression proves every checked-in baseline edge ID is the
      SHA-256 of its declared source, edge kind, and sorted physical outputs.
- [x] The current Clang build report resolves all four measured physical edges
      and hosted full CPU passes the compile-hotspot comparison after all 4,263
      CPU-supported tests remain green.

## Verification
```bash
python3 tests/regression/tooling/Test.CompileHotspots.py -v
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40 \
  --json-out build/ci/compile_hotspots_report.json \
  --baseline-json tools/analysis/compile_hotspot_baseline.json
python3 tools/agents/check_task_policy.py --root . --strict
# Hosted evidence: ci-linux-clang / full-cpu concludes green.
```

## Context
- PR #1037 run `33934397943`, job `101219377234`, compiled all 2,177 edges
  and passed all 4,263 selected CPU tests, then failed only the fail-closed
  compile-hotspot comparison. The missing IDs were
  `58ec1170b599cb974764ce6459b83c3f8090403d6455d5555f162baf402ac970`
  and `86f41c84f63ce2736408ccd2c99e84fd43fca32ea17d9e65aed6bf40c38c398d`.
- Commit `61fec5c06` moved the sources to
  `src/runtime/Kernel/Runtime.Engine.cppm` and
  `src/runtime/Rendering/Runtime.RenderExtraction.cppm`. It updated the
  baseline `source` strings but retained object outputs without `Kernel/` and
  `Rendering/`; because edge identity hashes source, kind, and outputs, the
  retained IDs no longer describe even the checked-in tuples.
- A current local Clang/Ninja report resolves the moved physical edges as
  `b0cb4aade2f211bfaece2294f1be395d41b6d96e9e3852f3795f75ba9cf36248`
  and `97ec2241f9bfc2e791739e0191241ba2f624e03c8cd081cf6c2713ca598dddcd`.
  The existing 165,000 ms and 167,000 ms measured ceilings are retained.
- On 2026-09-05, hosted run `33936262942`, job `101224743953`,
  passed the complete 4,263-case CPU-supported selector and the compile-hotspot
  gate with all four measured physical edges present.
