---
name: intrinsicengine-source-documentation
description: Audit and improve IntrinsicEngine source documentation without generating boilerplate. Use when creating or reviewing C++23 `.cppm` module interfaces, headers, source comments, or README files; when comments feel excessive, stale, historical, or duplicative; when a source directory is hard to navigate; or when running a recurring repository-wide documentation-compliance inventory.
---

# IntrinsicEngine Source Documentation

Use code structure as the primary documentation. Apply this skill to find
documentation debt, decide which comments are genuinely necessary, and make
bounded cleanup changes that improve discovery.

## Authority

Read `/AGENTS.md` first. Then read
[`references/source-documentation-policy.md`](references/source-documentation-policy.md)
completely before changing source documentation. The reference is generated
from `docs/agent/source-documentation-policy.md`; edit the canonical document
and run `python3 tools/agents/sync_skills.py --write` when the policy changes.

## Audit workflow

1. Choose the smallest useful scope. Use selected files or one subsystem during
   feature work; use the whole tree for a recurring debt inventory.
2. Run the deterministic scanner.
3. Fix objective errors in newly created or materially changed files.
4. Inspect every review finding in the touched scope. Keep a declaration
   comment only when it conveys a mandatory non-obvious contract.
5. Improve names, types, helpers, ordering, or file boundaries before adding
   explanatory prose.
6. Move implementation rationale to the matching `.cpp` and replace historical
   narratives with a current invariant plus, when useful, a short link to the
   authoritative record.
7. Rerun the selected audit and the repository's normal touched-scope checks.

Do not bulk-generate synopses, auto-delete comments, or treat heuristic review
findings as proven violations.

## Commands

Audit the whole project while existing migration debt remains non-blocking:

```bash
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py \
  --root . --summary --no-fail
```

Audit one subsystem or repeat `--path` for selected files:

```bash
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py \
  --root . --path src/runtime
```

Produce stable machine-readable output:

```bash
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py \
  --root . --format json --no-fail
```

The command exits nonzero for objective errors unless `--no-fail` is supplied.
Review findings never determine the exit code.

## Interpret findings

- `error` means an objective contract condition is absent: for example, a
  module/header has no leading synopsis or a README explicitly contains a
  history/roadmap section.
- `review` identifies a likely hotspot: a weak or long synopsis, a declaration
  comment, historical wording, a large manual README inventory, or an unusually
  large file. Read the surrounding code before changing anything.
- File size is a discovery signal, not a mandate to split a coherent unit.
- A task/ADR citation is acceptable only after the comment states the current
  invariant in its own words.

## Report results

Report the command and scope, objective-error count, review-finding count, and
the highest-value files to clean next. Separate findings fixed in the current
task from untouched migration debt. When cleanup is too broad for the active
task, propose a bounded task by subsystem or rule family rather than mixing a
repository-wide mechanical sweep into semantic work.
