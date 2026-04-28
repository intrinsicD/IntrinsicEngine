# Target Repository Layout (Migration Contract)

This document defines the explicit target directory layout for the IntrinsicEngine reorganization effort.

> Migration note: this file is a planning/migration contract and not a permanent architecture deep dive.

## Exact target tree

```text
IntrinsicEngine/
  AGENTS.md
  README.md
  CMakeLists.txt
  CMakePresets.json

  src/
    legacy/
    core/
    assets/
    ecs/
    geometry/
    graphics/
      rhi/
      vulkan/
      framegraph/
      renderer/
    runtime/
    platform/
    app/

  methods/
    _template/
    geometry/
    rendering/
    physics/
    papers/

  benchmarks/
    geometry/
    rendering/
    datasets/
    baselines/
    reports/
    runners/

  tests/
    unit/
    contract/
    integration/
    regression/
    gpu/
    benchmark/
    support/

  docs/
    index.md
    architecture/
    adr/
    methods/
    benchmarking/
    agent/
    migration/
    api/

  tasks/
    active/
    backlog/
    done/
    templates/

  tools/
    repo/
    docs/
    ci/
    benchmark/
    agents/
    analysis/

  .github/
    workflows/
      pr-fast.yml
      ci-linux-clang.yml
      ci-sanitizers.yml
      ci-docs.yml
      ci-bench-smoke.yml
      nightly-deep.yml
    pull_request_template.md
    ISSUE_TEMPLATE/

  .codex/
    config.yaml

  .claude/
    settings.json
```

## Top-level directory intent

- `src/`: Engine source organized by explicit architecture layers and subsystem ownership.
- `methods/`: Scientific paper/method packages, manifests, references, tests, and benchmark integration points.
- `benchmarks/`: Benchmark manifests, datasets metadata, baselines, runners, and reports.
- `tests/`: Purpose-based test taxonomy (unit/contract/integration/regression/gpu/benchmark/support).
- `docs/`: Canonical docs package and indices.
- `tasks/`: Structured actionable work items and templates.
- `tools/`: Repository/documentation/CI/analysis/task/benchmark automation scripts.
- `.github/`: PR workflow, CI workflows, templates.
- `.codex/`, `.claude/`: Tool-specific configuration only.

## Migration notes and constraints

- `src/legacy/` is **temporary** and should shrink over time as subsystems are promoted into canonical roots.
- `methods/` is reserved for method packages and their implementation lifecycle artifacts (manifest, references, tests, benchmarking, reports).
- `docs/`, `tasks/`, `tools/`, and CI workflows are part of the architecture contract and are not optional repository extras.
- Source-tree movement during this migration must be mechanical and reviewed separately from semantic code changes.

## Linkage requirements

- This target-layout document must stay linked from `README.md` during migration.
- The canonical root `AGENTS.md` (to be added by RORG-010) must also link to this document.
