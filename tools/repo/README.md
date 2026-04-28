# tools/repo

Repository structure and policy scripts.

## Current scripts

- `check_root_hygiene.py`: reports root-level markdown and allowed/blocked status.
- `check_expected_top_level.py`: compares current top-level tree to configured expectations.
- `check_layering.py`: validates layer dependency boundaries in warning mode by default, with `--strict` for CI enforcement.
- `check_ui_contract_guard.sh`: UI boundary guard script (canonical path).
- `generate_module_inventory.py`: current module inventory generator (canonical path; final-layout expansion tracked by RORG-075).

## Config files

- `layering_allowlist.yaml`: temporary exceptions for `check_layering.py`; each entry must include task and expiry notes.

## Compatibility entrypoints

To avoid breaking historical docs/scripts during migration, legacy wrappers are temporarily retained at:

- `tools/check_ui_contract_guard.sh`
- `tools/generate_src_new_module_inventory.py`

These wrappers should be removed in the compatibility cleanup phase (RORG-112).
