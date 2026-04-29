# tools/docs

Documentation validation and synchronization tooling.

## Current scripts

- `check_doc_links.py`: validates relative markdown links from a repository root while excluding local build/cache/vendor artifact trees.
- `check_docs_sync.py`: validates docs-update requirements from `docs_sync_rules.yaml` in warning/strict mode and supports diff-mode against a git base ref.

## Enforcement status

- Docs link checks are strict in CI after RORG-121.
