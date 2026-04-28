# Method Dataset Policy

Datasets used by method tests/benchmarks must be declared and reproducible.

## Rules

- Use manifest-driven dataset IDs, not ad-hoc file paths.
- Keep large binaries out of the repository unless explicitly approved.
- Prefer generated/builtin datasets for smoke tests.
- Document licensing and provenance for external datasets.

## Required metadata

- Stable dataset identifier.
- Source/provenance.
- Scale/profile (smoke vs heavy).
- Known numerical pathologies relevant to validation.
