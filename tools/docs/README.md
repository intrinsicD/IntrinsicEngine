# tools/docs

Documentation validation and synchronization tooling.

## Current scripts

- `check_doc_links.py`: validates relative markdown links from a repository root while excluding local build/cache/vendor artifact trees. In a Git work tree it scans tracked and untracked `*.md` files but skips Git-ignored ones (local evidence, review copies), which never exist in a CI checkout; a `path:line` or `path:start-end` target is valid when `path` is an existing file. The checker ignores fenced code blocks and whole inline-code snippets, but still validates links whose labels contain inline-code markup such as ``[`TASK-ID`](path.md)``.
- `check_docs_sync.py`: validates docs-update requirements from `docs_sync_rules.yaml` in warning/strict mode and supports diff-mode between explicit git base/head refs.

## Enforcement status

- Docs link checks are strict in CI after RORG-121.
