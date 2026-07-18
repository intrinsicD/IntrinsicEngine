# tools/analysis

Static analysis and performance analysis tooling.

## Current scripts

- `tools/analysis/compile_hotspots.py`: normalizes the latest Ninja records
  into physical compiler invocations, resolves their sources through the
  configured `compile_commands.json`, and optionally checks stable edge
  identities against `compile_hotspot_baseline.json`. The parser explicitly
  supports Ninja log v4-v7. Versions 5-v7 share the five-field hashed-command
  layout; command hashes are opaque identities and record mtimes do not
  participate in physical-edge grouping. Unverified versions fail closed.
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
- BUILD-004 migrates that historical target to a stable normalized identity.
  Its 92-second budget remains historical until a comparable five-sample
  cohort justifies replacing the target set and thresholds.
