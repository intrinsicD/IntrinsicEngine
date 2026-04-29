# Test taxonomy audit — 2026-04-29 (HARDEN-040)

## Scope

Audit remaining non-taxonomic test directories after RORG/HARDEN taxonomy adoption. This report is inventory-only and does not move or edit test source files.

## Canonical taxonomy roots (current)

The active taxonomy-aligned roots currently used by `tests/CMakeLists.txt` source resolution are:

- `tests/unit/`
- `tests/contract/`
- `tests/integration/`
- `tests/regression/`
- `tests/gpu/`
- `tests/benchmark/`

Supporting directory (not a test category):

- `tests/support/`

## Remaining non-taxonomic directories

The following top-level directories remain from the prior subsystem-wrapper layout:

- `tests/Asset/`
- `tests/Core/`
- `tests/ECS/`
- `tests/Graphics/`
- `tests/Runtime/`

Per `tests/CMakeLists.txt`, these are intentionally not registered into the supported post-RORG CTest suite because some sources still depend on removed `Extrinsic.*` modules or can produce `_NOT_BUILT` placeholders.

### File inventory summary

| Directory | `*.cpp` count | Primary naming pattern | Current disposition |
|---|---:|---|---|
| `tests/Asset/` | 6 | `Test_*.cpp` | Candidate migrate/remove set for HARDEN-041/042 |
| `tests/Core/` | 19 | `Test_*.cpp` | Candidate migrate/remove set for HARDEN-041/042 |
| `tests/ECS/` | 1 | `Test_*.cpp` | Candidate migrate/remove set for HARDEN-041/042 |
| `tests/Graphics/` | 9 | `Test_*.cpp` | Candidate migrate/remove set for HARDEN-041/042 |
| `tests/Runtime/` | 2 | `Test_*.cpp` | Candidate migrate/remove set for HARDEN-041/042 |

Total non-taxonomic wrapper files observed: **37** `*.cpp` files.

## Active registration status

- Active CTest registration is taxonomy-driven via categorized source lists and targets (`IntrinsicCoreTests`, `IntrinsicECSTests`, `IntrinsicContractBuildTests`, `IntrinsicBenchmarkTests`, `IntrinsicGeometryTests`, `IntrinsicRuntimeTests`).
- Non-taxonomic wrapper directories remain on disk but are intentionally excluded from registration in the current policy.

## Follow-up mapping

### HARDEN-041 (move remaining test sources into taxonomy directories)

- For each wrapper-directory file, classify as one of:
  1. **Already duplicated in taxonomy target lists** (remove stale duplicate wrapper source after confirmation).
  2. **Needs relocation** to `unit|contract|integration|regression|gpu|benchmark` path with no semantic test rewrite.
  3. **Obsolete/unsupported** and should be retired or archived with explicit rationale.
- Update `tests/CMakeLists.txt` source resolution lists after file moves are complete.

### HARDEN-042 (remove or formalize old subsystem test subdirectories)

- Remove empty wrapper directories once HARDEN-041 relocation/removal decisions are complete.
- If any wrapper directory must remain temporarily, formalize it with documented exception policy and explicit removal task.

## Commands and evidence

The following commands were used for this audit:

```bash
find tests -maxdepth 3 -type d | sort
find tests/Asset tests/Core tests/ECS tests/Graphics tests/Runtime -type f -name '*.cpp' | wc -l
for d in tests/Asset tests/Core tests/ECS tests/Graphics tests/Runtime; do echo "$(find "$d" -type f -name '*.cpp' | wc -l) $d"; done
```

Observed counts from command output are reflected in the inventory table above.
