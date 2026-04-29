# tools/repo

Repository structure and policy scripts.

## Current scripts

- `check_root_hygiene.py`: reports root-level markdown and allowed/blocked status; local build/vendor/IDE artifact directories are ignored when comparing the root allowlist.
- `check_expected_top_level.py`: compares current top-level source tree entries to configured expectations; local build/vendor/IDE artifact directories are ignored.
- `check_layering.py`: validates layer dependency boundaries in warning mode by default, with `--strict` for CI enforcement.
- `check_ui_contract_guard.sh`: UI boundary guard script (canonical path).
- `generate_module_inventory.py`: module inventory generator for both `src/` (final layout) and `src_new/` (migration snapshot); defaults to `docs/api/generated/module_inventory.md`.

## Config files

- `layering_allowlist.yaml`: temporary exceptions for `check_layering.py`; each entry must include task and expiry notes.

## Compatibility entrypoints

To avoid breaking historical docs/scripts during migration, legacy wrappers are temporarily retained at:

- `tools/check_ui_contract_guard.sh`
- `tools/repo/generate_module_inventory.py`

These wrappers should be removed in the compatibility cleanup phase (RORG-112).
