# tools/repo

Repository structure and policy scripts.

## Current scripts

- `check_root_hygiene.py`: reports root-level markdown and allowed/blocked status.
- `check_expected_top_level.py`: compares current top-level tree to configured expectations.
- `check_ui_contract_guard.sh`: UI boundary guard script pending migration into this directory.
- `generate_src_new_module_inventory.py`: legacy inventory generator pending rename/move.

## Planned moves

- `tools/check_ui_contract_guard.sh` -> `tools/repo/check_ui_contract_guard.sh` (RORG-071).
- `tools/generate_src_new_module_inventory.py` -> `tools/repo/generate_module_inventory.py` (RORG-071, RORG-075).
- Add `check_layering.py`, `layering_allowlist.yaml`, and root allowlist assets in later tasks.
