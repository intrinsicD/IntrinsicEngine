# Benchmark Dataset Policy

Datasets used by benchmarks must be explicit, versionable, and reproducible.

## Rules

- Declare dataset identifiers through manifests under `benchmarks/datasets/manifests/`.
- Keep in-repo datasets small and deterministic for smoke checks.
- Do not commit large binary datasets directly into the repository.
- Record provenance and licensing for non-generated datasets.
- Keep preprocessing deterministic and documented.
- Claim-grade protocols seal every dataset byte path with SHA-256, declare
  split names, and state how screening and confirmation splits are disjoint.
- A changed dataset hash invalidates initialized runs. Create a new run
  identity; never rewrite the old result to match new bytes.
- Protected datasets are never touched during prospective review. The review
  records zero protected interactions/private draws before separate launch
  authorization.

## Storage guidance

- **In-repo:** tiny built-in meshes/scenes used for smoke tests.
- **External/cache:** larger datasets fetched in optional workflows.
- **Nightly-only:** heavyweight datasets and stress suites.
- **Protected/external:** remain outside Git; protocols and bundles retain
  opaque path/hash/provenance bindings without exposing private contents.
