# Benchmark Dataset Policy

Datasets used by benchmarks must be explicit, versionable, and reproducible.

## Rules

- Declare dataset identifiers through manifests under `benchmarks/datasets/manifests/`.
- Keep in-repo datasets small and deterministic for smoke checks.
- Do not commit large binary datasets directly into the repository.
- Record provenance and licensing for non-generated datasets.
- Keep preprocessing deterministic and documented.

## Storage guidance

- **In-repo:** tiny built-in meshes/scenes used for smoke tests.
- **External/cache:** larger datasets fetched in optional workflows.
- **Nightly-only:** heavyweight datasets and stress suites.
