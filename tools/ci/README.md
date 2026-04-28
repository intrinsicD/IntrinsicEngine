# tools/ci

CI helper scripts and workflow validation tools.

## Current scripts

- `run_repo_hygiene_checks.sh`: warning-mode wrapper for repository hygiene checks.
- `check_workflow_names.py`: validates workflow file allowlist, `name` consistency, explicit `on` triggers, and readability (no one-line compressed YAML).

## Notes

- `check_workflow_names.py --strict` reserves enforcement for the full canonical workflow set, including `nightly-deep.yml`.
