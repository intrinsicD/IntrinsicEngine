# GRAPHICS-138 renderer surface measurement

C96 records this bounded local comparison. The change removes four unused
renderer accessors, six unused interface imports and 34 trivial registry getter
methods. The same 17 optional fields retain their types, order and lifecycle
algorithms. Existing mutable getters already allowed storage mutation. All
in-tree users rebuild after the public C++ surface change; no ABI compatibility
is claimed. Four production files shrink by 222 lines, with no new production file.

| Scenario | Before samples 1 / 2 (seconds) | After samples 1 / 2 (seconds) | Compiler invocations before → after |
|---|---:|---:|---:|
| Clean graphics-library build | 89.402 / 89.877 | 85.373 / 86.213 | 266 → 266 |
| No-op | 0.032 / 0.034 | 0.035 / 0.031 | 0 → 0 |
| Forward-system interface touch | 29.749 / 29.734 | 16.174 / 16.279 | 12 → 11 |

The interface touch avoids exactly one producer, `Graphics.Renderer.cppm`,
which was on the measured critical path. Its implementation still rebuilds
and takes about 13 seconds in this probe. The renderer interface's import
closure narrows from 66 to 59 modules; these modules remain in the target build.
**Every clean build still compiles 266 producers.**

Observed clean ranges are 89.402–89.877 → 85.373–86.213 seconds; interface-touch
ranges are 29.734–29.749 → 16.174–16.279 seconds. These are two observations per
arm, without statistical confidence or an established general speedup. The
combined patch does not isolate timing effects of getter removal and import
narrowing. Clean weighted critical paths are 48.283 / 48.508 → 40.893 / 41.247
seconds; interface-touch paths are 29.714 / 29.697 → 16.140 / 16.237 seconds.
They describe the measured target DAG, not a whole-engine critical path.

| Other measurement | Before samples 1 / 2 | After samples 1 / 2 |
|---|---:|---:|
| Initial configure (seconds) | 6.679 / 6.393 | 6.708 / 7.147 |
| Clean peak single-process RSS (MiB) | 1802.676 / 1802.188 | 1800.039 / 1799.949 |
| Interface-touch peak single-process RSS (MiB) | 1802.242 / 1801.844 | 1796.184 / 1796.711 |
| No-op peak single-process RSS (MiB) | 17.469 / 17.781 | 17.773 / 17.621 |

Configure and no-op observations do not demonstrate improvement. RSS is a
single-process maximum, not aggregate concurrent memory; no memory gain is
claimed. All unrounded observations and min/max ranges remain in the summary.

## Fixed inputs and scope

- Before `7ec9673797ed581feebae9b3cae9d93bb27d670c`; after
  `1f84ec611120b8a0c5f9428be015284a40c68a03`. Both exact clean commits.
- Clang 23.0.0, Debug `ci` with Null/headless overrides, four jobs, cache off,
  `ExtrinsicGraphics` only. Runtime, Sandbox and test-suite compile times were
  not measured. Host/compiler details and actual commands are in the inputs.
- ABBA order, two samples per arm, zero discarded warmups; identical source and
  dependency pre-read, no filesystem-cache flushing or cold-cache claim. One
  owned tmpfs build is recreated between samples. Each sample runs configure,
  clean, no-op, then an mtime-only ForwardSystem interface touch and rebuild.
- Existing BUILD-007 runner SHA256
  `256fa3e5d4b85bda3b244328aa70904f5f0bbd7eb295db1e687132523561e423`
  matches both source and recorded protocol. It forces
  `-DVCPKG_MANIFEST_INSTALL=OFF`. Dependency path/content/mode/link fingerprint
  `0ecb7332b14479eabffbc061dba9d94492bfb494a936e9be5bf2caf384e97c54`
  matches all four samples and independent pre/post-run checks.
- All four canonical results validate and remain `claim_eligible: false`.
  No sample is rejected or excluded. No runtime/GPU, sanitizer, cross-host,
  publication-qualified or broader C92 conclusion follows.

## Review and verification

Claude reviewed the plan, source and results. Final wording distinguishes
the one avoided critical-path compilation from the seven modules removed from
the renderer import closure, preserves unchanged clean compilation counts and
keeps timings descriptive. No source correctness blocker remained.

Canonical Clang 23 ci configure and complete `IntrinsicTests` build pass.
Focused CTest: 151 passed. Full CPU: 4,641 passed, zero failures, one expected
ASan-only GLFW skip (4,642 selected, 133.31 seconds). No GPU or sanitizer runtime
execution is claimed. The new compiler-boundary guard rejected all six roots on
original metadata before editing and passes on the final metadata. Lifecycle
tokens, optional field types/order and the 319 renderer / 16 test call rewrites
were checked mechanically. Strict structural and documentation checks pass.

## Evidence

Canonical result JSON files are retained once inside the archive under
`accepted-cohort/results/`; their individual hashes are indexed.

- [Raw values, ranges and changed rebuild producers](../diagnostics/graphics138_20260915/summary.json)
- [Protocol, source and dependency identities](../diagnostics/graphics138_20260915/inputs.json)
- [Verification and review resolutions](../diagnostics/graphics138_20260915/verification.json)
- [Evidence hashes](../diagnostics/graphics138_20260915/evidence-index.json)
- [Complete cohort and review/test logs](../diagnostics/graphics138_20260915/raw-evidence.tar.gz)
- [Manifest](../../../benchmarks/ci/manifests/engine_compile_iteration_renderer_surface.yaml)
