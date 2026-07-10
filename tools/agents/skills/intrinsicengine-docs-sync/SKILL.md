---
name: intrinsicengine-docs-sync
description: Documentation synchronization policy for IntrinsicEngine — what docs must update in the same PR when code, structure, or policy changes. Defines per-change-type required updates (architecture/layering → `AGENTS.md` + `docs/architecture/*`; migration → `docs/migration/*` + inventories; task/process → `tasks/*` + `docs/agent/*`; method/benchmark infra → `docs/methods/*` or `docs/benchmarking/*` + validators/manifests; CI/workflow → workflow docs), the factual-current-state quality gate, and the warning-mode/strict-mode docs-sync validator. Use this skill whenever the user is moving files, renaming modules, changing public API or module surfaces (`.cppm` interfaces), updating `AGENTS.md` or `docs/architecture/*`, regenerating module inventories, asking whether a doc update is needed, or asking about the `tools/docs/check_docs_sync.py` validator.
---

# IntrinsicEngine Documentation Sync Policy

This skill governs when and what documentation must update alongside code,
structure, or policy changes in IntrinsicEngine. The core principle:
**documentation updates ship in the same PR as the change that motivates them**,
not in a follow-up.

The full source policy is in `references/docs-sync-policy.md`. This SKILL.md
adds the per-change-type lookup, the validator commands, and decision rules
for the common cases.

## Required docs updates by change type

| Change type | Required updates |
| --- | --- |
| **Architecture / layering** | Update `AGENTS.md` (if contract-level) and relevant `docs/architecture/*`. |
| **Migration / path moves** | Update `docs/migration/*`, update **links** for moved files, refresh generated **inventories**. |
| **Task / process changes** | Update `tasks/*` records and `docs/agent/*` where process rules are affected. |
| **Method or benchmark infrastructure** | Update `docs/methods/*` or `docs/benchmarking/*`, plus validators and manifests. |
| **CI / workflow** | Update workflow docs / process checklists. |
| **Module surface changes** (`.cppm` interfaces, public API) | Regenerate `docs/api/generated/module_inventory.md`. |

## Module-inventory regeneration

After any change to module interfaces or the source tree, regenerate the
canonical inventory:

```bash
python3 tools/repo/generate_module_inventory.py \
    --root src \
    --out docs/api/generated/module_inventory.md
```

This is required by `AGENTS.md` §9 whenever public module surfaces change. The
inventory file is generated — don't hand-edit it.

## Quality gates

- **Docs describe current behavior/state, not aspirational plans**, unless
  clearly labeled as roadmap/migration. The weekly audit row 9 in
  `intrinsicengine-review` catches drift here — present-tense claims about
  unimplemented behavior are a flagged failure mode.
- **Cross-links must be valid.** Moving a file without updating its inbound
  links breaks the docs graph.
- **Generated inventories are refreshed when impacted by structure changes.**

## Validator commands

Warning-mode docs-sync validation against changed files (run before commit):

```bash
python3 tools/docs/check_docs_sync.py \
    --root . \
    --diff-mode \
    --base-ref origin/main
```

Strict changed-file mode (enforced by `ci-docs.yml` with fetched base history):

```bash
python3 tools/docs/check_docs_sync.py \
    --root . \
    --diff-mode \
    --base-ref origin/main \
    --strict
```

`ci-docs.yml` also runs
`python3 tools/agents/check_task_state_links.py --root . --strict` so lifecycle
claims and links stay synchronized with task locations.

Link integrity check:

```bash
python3 tools/docs/check_doc_links.py --root .
```

Rule mappings live in `tools/docs/docs_sync_rules.yaml` — when a new doc area
needs its own sync rule, edit that file rather than hardcoding paths in the
validator.

## Decision rules for common cases

### "I moved a file under `src/` — what now?"

1. Update `docs/migration/*` if the move is part of a tracked migration.
2. Update any `.md` files that link to the old path (use the link checker).
3. If module surface changed, regenerate `docs/api/generated/module_inventory.md`.
4. If layering edges changed, run `python3 tools/repo/check_layering.py --root src --strict`.

### "I changed an `AGENTS.md` rule"

1. Update the matching expanded doc in `docs/agent/` (if one exists).
2. Update any task templates in `tasks/templates/` that reference the rule.
3. The contract chain: `AGENTS.md` is authoritative; expanded docs are
   reference material; **do not let them disagree** without explicitly noting
   which wins.

### "I added a new module library with `intrinsic_add_module_library`"

1. Regenerate `docs/api/generated/module_inventory.md`.
2. If the module changes the public surface area of a layer, update the
   relevant `docs/architecture/*` doc.
3. Run `python3 tools/repo/check_layering.py --root src --strict` to confirm
   any new dependency edges are allowed.

### "I retired a legacy module"

1. Update `docs/migration/legacy-retirement.md`.
2. Update `docs/migration/nonlegacy-parity-matrix.md`.
3. Regenerate `docs/api/generated/module_inventory.md`.
4. Update `tools/repo/layering_allowlist.yaml` row count (rows for the retired
   module should disappear).

### "I'm just adding a new doc, no code change"

The validator may still flag the new file as needing an entry in
`docs/index.md` or the relevant section index — add it there in the same PR.

## What this skill does *not* govern

- **Reviewing whether the doc claim is factual.** That's the per-PR review
  checklist row "Documented-but-not-tested" — see `intrinsicengine-review`.
- **Whether a task file needs updating.** That's `intrinsicengine-task-workflow`.
- **Method/benchmark documentation specifics.** Those are owned by
  `intrinsicengine-method` and `intrinsicengine-benchmark` respectively;
  this skill just routes you to the right docs root.

## References

- `references/docs-sync-policy.md` — full source policy with per-change-type
  required updates, quality gates, and automation pointers.
