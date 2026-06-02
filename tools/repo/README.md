# tools/repo

Repository structure and policy scripts.

## Current scripts

- `check_root_hygiene.py`: reports root-level markdown and allowed/blocked status; local build/vendor/IDE artifact directories are ignored when comparing the root allowlist.
- `check_expected_top_level.py`: compares current top-level source tree entries to configured expectations; local build/vendor/IDE artifact directories are ignored.
- `check_layering.py`: validates layer dependency boundaries in warning
  mode by default, with `--strict` for CI enforcement. Covers
  ``#include`` directives, C++23 module imports (including promoted
  ``Extrinsic.<Layer>.*`` prefixes), and CMake
  ``target_link_libraries(...)`` edges between promoted targets. Use
  ``--exclude PATTERN`` to skip fixture or generated paths. Fixture cases
  live under ``tests/contract/repo/layering_fixtures/`` and are exercised
  by ``tests/regression/tooling/Test.CheckLayering.py``.
- `check_ui_contract_guard.sh`: UI boundary guard script (canonical path).
- `check_layering_allowlist_quality.py`: validates layering allowlist entry hygiene (required metadata, duplicate keys, and broad legacy wildcard bans).
- `check_test_layout.py`: enforces taxonomy-owned test source layout and forbids legacy wrapper test directories.
- `generate_module_inventory.py`: module inventory generator for `src/`; defaults to `docs/api/generated/module_inventory.md`.

## Config files

- `layering_allowlist.yaml`: temporary path-scoped exceptions for `check_layering.py`; each entry must include task and expiry notes, avoid broad `src/legacy/**` wildcards, and point at an open removal owner. Current legacy `Interface` rows point at `LEGACY-001`; the remaining legacy subtree rows point at `LEGACY-002` until that task seeds per-subtree deletion owners.

## Compatibility entrypoints

To avoid breaking historical docs/scripts during migration, legacy wrappers are temporarily retained at:

- `tools/check_ui_contract_guard.sh`
- `tools/repo/generate_module_inventory.py`

These wrappers should be removed in the compatibility cleanup phase (RORG-112).
