# tools/docs

Documentation validation and synchronization tooling.

## Current scripts

- `check_doc_links.py`: validates relative markdown links from a repository root while excluding local build/cache/vendor artifact trees. The checker ignores fenced code blocks and whole inline-code snippets, but still validates links whose labels contain inline-code markup such as ``[`TASK-ID`](path.md)``.
- `check_docs_sync.py`: validates docs-update requirements from `docs_sync_rules.yaml` in warning/strict mode and supports diff-mode between explicit git base/head refs.

## Enforcement status

- Docs link checks are strict in CI after RORG-121.
