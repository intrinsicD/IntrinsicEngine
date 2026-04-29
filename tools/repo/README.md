# tools/repo

Repository structure and policy scripts.

## Current scripts

- `check_root_hygiene.py`: reports root-level markdown and allowed/blocked status; local build/vendor/IDE artifact directories are ignored when comparing the root allowlist.
- `check_expected_top_level.py`: compares current top-level source tree entries to configured expectations; local build/vendor/IDE artifact directories are ignored.
- `check_layering.py`: validates layer dependency boundaries in warning mode by default, with `--strict` for CI enforcement.
- `check_ui_contract_guard.sh`: UI boundary guard script (canonical path).
- `check_stale_src_new_references.py`: enforces no new stale `src_new` naming outside an explicit migration/historical allowlist.
- `check_layering_allowlist_quality.py`: validates layering allowlist entry hygiene (required metadata, duplicate keys, and broad legacy wildcard bans).
- `check_test_layout.py`: enforces taxonomy-owned test source layout and forbids legacy wrapper test directories.
- `generate_module_inventory.py`: module inventory generator for both `src/` (final layout) and `src_new/` (migration snapshot); defaults to `docs/api/generated/module_inventory.md`.

## Config files

- `layering_allowlist.yaml`: temporary path-scoped exceptions for `check_layering.py`; each entry must include task and expiry notes and avoid broad `src/legacy/**` wildcards.
- `src_new_reference_allowlist.txt`: explicit migration/historical path allowlist used by `check_stale_src_new_references.py`.

## Compatibility entrypoints

To avoid breaking historical docs/scripts during migration, legacy wrappers are temporarily retained at:

- `tools/check_ui_contract_guard.sh`
- `tools/repo/generate_module_inventory.py`

These wrappers should be removed in the compatibility cleanup phase (RORG-112).
