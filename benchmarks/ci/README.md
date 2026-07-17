# CI Gate Benchmarks

This package contains infrastructure-performance contracts for GitHub-hosted CI
gates. These measurements observe the repository build/test system rather than
an engine method.

`ci.gate-latency.github-ubuntu-24.04.v1` records configure, build, test, and
measured-total wall time for one gate invocation. Results use the canonical
benchmark JSON schema with `backend: external_baseline`; gate, preset, compiler,
sanitizer, cache state, selected test count, Ninja command-edge count, and cache
statistics are diagnostics. The configured target platform, graphics backend,
requested and selected platform backend, and headless flag identify the exact
generated graph behind each timing sample.

The measured total is the sum of the three instrumented phases. Dependency
installation, queue time, artifact upload, and unrelated structural checks are
not included. Cold and warm samples are separate populations and must never be
combined in one statistic.

The pre-optimization baseline is
[`../baselines/ci_gate_latency_github_ubuntu_24_04_v1.json`](../baselines/ci_gate_latency_github_ubuntu_24_04_v1.json).
It retains five API-sourced samples for each required gate plus both sanitizer
matrix legs. All populations use the same five pull-request commits; the
artifact retains every run/job ID and records the completed 30-job/25-run API
source audit. The baseline validator locks the gate context and recomputes
median and nearest-rank p95:

```bash
python3 tools/ci/validate_gate_timing_baseline.py
```

The per-run profile keeps the ID
`ci.gate-latency.github-ubuntu-24.04.v1` and its four direct phase metrics. The
multi-run report has the distinct manifest-backed ID
`ci.gate-latency.github-ubuntu-24.04.v1.aggregate-baseline`; its diagnostics
link back to the source profile so aggregate statistics cannot be mistaken for
one gate invocation.
