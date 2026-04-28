# tools/docs

Documentation validation and synchronization tooling.

## Current scripts

- `check_doc_links.py`: validates relative markdown links from a repository root.

## Planned moves

- `check_docs_sync.py`: validates docs-update requirements from `docs_sync_rules.yaml` in warning/strict mode and supports diff-mode against a git base ref.
- Tighten docs checks from warning mode to strict in CI after cleanup (RORG-121).
