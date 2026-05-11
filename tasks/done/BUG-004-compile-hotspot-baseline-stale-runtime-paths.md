# BUG-004 — Compile-hotspot gate baseline references stale runtime source paths

## Goal
- Refresh the compile-hotspot baseline so `tools/analysis/compile_hotspots.py` validates current source paths after the runtime/geometry/ECS layout migration.

## Non-goals
- No compile-performance optimization claims.
- No semantic C++ refactors.
- No broad rewrite of the hotspot analysis tool unless needed to handle migrated paths robustly.

## Context
- Status: done 2026-05-09.
- Owner/agent: Copilot.
- Observed: 2026-05-09 in `build/ci-full-logs/compile_hotspot_gate.log`.
- Symptom: `python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40 --json-out build/ci/compile_hotspots_report.json --baseline-json tools/analysis/compile_hotspot_baseline.json` exits with status 2 because the baseline requires sources that no longer appear in compile entries:
  - `src/Runtime/Geometry/Geometry.Octree.cppm`
  - `src/Runtime/ECS/Systems/ECS.Systems.Transform.cpp`
- Expected behavior: the hotspot gate should compare against current canonical source paths or explicitly carry migration aliases.
- Impact: structural/performance CI checks fail even when the compile log can be parsed and the report is emitted.

## Required changes
- [x] Inspect `tools/analysis/compile_hotspot_baseline.json` and map stale paths to the current owning source files or remove entries that no longer represent valid targets.
- [x] If source migration aliases are intentional, teach `compile_hotspots.py` to resolve them explicitly and document the mapping.
- [x] Regenerate or manually update the baseline with current paths only, keeping the change mechanical and reviewable.

## Tests
- [x] Add/update tool coverage for stale-baseline diagnostics if the analyzer behavior changes.
- [x] Re-run the compile-hotspot command against a successfully built `build/ci` tree.

## Docs
- [x] Update analysis/tool docs if baseline refresh procedure or migration alias policy changes.
- [x] Mention the refreshed baseline in the task completion record when fixed.

## Acceptance criteria
- [x] The compile-hotspot gate no longer reports missing `src/Runtime/...` sources after a normal CI build.
- [x] The refreshed baseline references source paths that exist in the current repository or are documented aliases handled by the tool.
- [x] The generated JSON report remains machine-readable and the gate still fails on genuine hotspot regressions.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40 --json-out build/ci/compile_hotspots_report.json --baseline-json tools/analysis/compile_hotspot_baseline.json
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not lower or remove hotspot thresholds solely to make the gate pass.
- Do not mix baseline path refresh with C++ compile-time refactors.
- Do not treat a stale source path as a performance improvement.

## Captured evidence
- `build/ci-full-logs/compile_hotspot_gate.log` shows the analyzer successfully writing `build/ci/compile_hotspots_report.json` before failing on the two missing baseline source entries.

## Completion
- Completed: 2026-05-09.
- Commit reference: pending.
- Notes: `tools/analysis/compile_hotspot_baseline.json` now references `src/geometry/Geometry.Octree.cppm` and `src/legacy/ECS/Systems/ECS.Systems.Transform.cpp`.

