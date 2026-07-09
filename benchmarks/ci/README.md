# CI Gate Benchmarks

This package contains infrastructure-performance contracts for GitHub-hosted CI
gates. These measurements observe the repository build/test system rather than
an engine method.

`ci.gate-latency.github-ubuntu-24.04.v1` records configure, build, test, and
measured-total wall time for one gate invocation. Results use the canonical
benchmark JSON schema with `backend: external_baseline`; gate, preset, compiler,
sanitizer, cache state, selected test count, Ninja command-edge count, and cache
statistics are diagnostics.

The measured total is the sum of the three instrumented phases. Dependency
installation, queue time, artifact upload, and unrelated structural checks are
not included. Cold and warm samples are separate populations and must never be
combined in one statistic.
