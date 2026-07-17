# tools/analysis

Static analysis and performance analysis tooling.

## Current scripts

- `tools/analysis/compile_hotspots.py`
- `tools/analysis/module_fanout.py`: reports import/include/export fan-out for
  selected files or `--root src`. Nightly runs it in report-only mode;
  `--fail-on-regression` is an opt-in local comparison against files present in
  the historical baseline, not a current CI gate.
- `tools/analysis/compile_hotspot_baseline.json`
- `tools/analysis/module_fanout_baseline_2026-04-03.md`
- `tools/analysis/build_time_baseline_2026-04-05.md`

## Migration status

- RORG-072 completed: analysis scripts/baselines now live under `tools/analysis/` and CI/docs references use the new paths.
- BUG-004 refreshed `compile_hotspot_baseline.json` to use canonical source
  paths under the then-current layout; stale `src/Runtime/...` migration aliases
  are not accepted by the gate.
