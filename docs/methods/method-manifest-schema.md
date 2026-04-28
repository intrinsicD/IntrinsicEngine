# Method Manifest Schema (`method.yaml`)

This document defines the required schema for method manifests under `methods/**/method.yaml`.

## Purpose

Method manifests make paper-method packages machine-checkable for CI and agent workflows.

Validation script:

```bash
python3 tools/agents/validate_method_manifests.py --root methods
python3 tools/agents/validate_method_manifests.py --root methods --strict
```

## Required top-level fields

Every `method.yaml` must define:

- `id` (string)
- `name` (string)
- `domain` (string)
- `status` (enum)
- `paper` (mapping)
- `inputs` (non-empty list)
- `outputs` (non-empty list)
- `backends` (non-empty list)
- `metrics` (non-empty list)
- `correctness_tests` (non-empty list)
- `benchmarks` (non-empty list)
- `known_limitations` (non-empty list)

## ID rules

In strict mode, `id` must be namespaced with one of:

- `geometry.*`
- `rendering.*`
- `physics.*`

## Status enum

`status` must be one of:

- `proposed`
- `reference`
- `optimized`
- `gpu`
- `validated`
- `deprecated`

## Backend enum

Each value in `backends` must be one of:

- `cpu_reference`
- `cpu_optimized`
- `gpu_vulkan_compute`
- `gpu_vulkan_graphics`
- `cuda_optional`
- `external_baseline`

## Paper metadata

`paper` must contain these keys:

- `title`
- `authors`
- `year`
- `doi`
- `url`

## Correctness test and benchmark references

Entries in `correctness_tests` and `benchmarks` must either:

1. Resolve to existing files/directories relative to the method package root, or
2. Be explicit placeholders using one of these prefixes:
   - `TODO:`
   - `TBD`
   - `PLACEHOLDER:`

This allows incremental scaffolding while still being explicit about missing artifacts.
