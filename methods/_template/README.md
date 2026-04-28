# Method Package Template

Use this template as the starting point for a new method package.

## How to use

1. Copy `_template` to `methods/<domain>/<method_name>/`.
2. Update `method.yaml` with a stable namespaced ID.
3. Fill in `paper.md` from the target publication.
4. Implement CPU reference backend first.
5. Add correctness tests before any optimization work.
6. Add benchmarks and report outputs.

## Required package contents

- `method.yaml`: canonical method manifest.
- `paper.md`: bibliography, assumptions, and algorithm notes.
- `include/`, `src/`: implementation surface and internals.
- `tests/`: correctness and regression cases.
- `benchmarks/`: benchmark manifests/runners for smoke and deeper runs.
- `reports/`: performance and numerical validation artifacts.
