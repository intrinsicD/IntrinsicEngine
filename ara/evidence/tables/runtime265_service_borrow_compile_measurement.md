# RUNTIME-265 service-borrow compile measurement

C95 records only this local comparison. The retained patch removes pointer-only
service imports from the editor's shared processing interface. Existing device
and spatial-cache owners and their definitions use matching C++ linkage; all
in-tree consumers must rebuild. No external ABI compatibility is claimed.

| Scenario | Before sample 1 / 2 (seconds) | After sample 1 / 2 (seconds) | Compiler invocations before → after |
|---|---:|---:|---:|
| Clean engine library build | 348.404 / 347.646 | 342.833 / 342.775 | 775 → 775 |
| No-op build | 0.078 / 0.074 | 0.071 / 0.072 | 0 → 0 |
| Spatial-cache interface touch | 84.456 / 84.131 | 39.477 / 39.179 | 61 → 22 |

Ranges: clean 347.646–348.404 → 342.775–342.833 seconds; interface touch
84.131–84.456 → 39.179–39.477 seconds. Thirty-nine source files leave the
interface-triggered rebuild, with none added. The snapshot and processing
discovery interfaces are among them. The observed incremental difference is
specific to this edit path. **There is no established clean-build speedup**:
two observations per revision cannot distinguish its small timing difference
from drift or establish a general performance result. No median or statistical
confidence estimate is inferred. Peak single-process RSS remains about 2.8 GiB;
this is not aggregate concurrent memory.

## Fixed inputs and scope

- Before `6570bc106ec13e9e9be3220cf8ca302306499c69`; after
  `ee647ec91b677fc1051be3b97405e3cd07b06aaf`. Exact clean source revisions isolate
  this patch from RUNTIME-264 and the prepared renderer follow-up.
- Local **Clang 23.0.0**, four jobs, Debug `ci` with Null/headless overrides,
  `ExtrinsicRuntime`, tests and benchmark targets off, compiler cache disabled.
  The canonical ci correctness build also uses Clang 23; Clang 20 is the
  repository minimum, not this measurement's compiler. No transfer to another
  compiler or host is asserted.
- ABBA order, two samples per arm, zero discarded warmups. Source/dependency
  inputs pre-read identically; no cold-filesystem claim. One owned tmpfs build
  directory recreated between complete samples; configure → clean → no-op →
  interface-touch ordering retained within each sample. All samples retained.
- Corrected runner source: `1412c572def53ef18c28acac389e35bc214b4cc8`,
  `tools/analysis/benchmark_compile_iteration.py`, run from the primary checkout
  outside both measured revisions. Its SHA256
  `256fa3e5d4b85bda3b244328aa70904f5f0bbd7eb295db1e687132523561e423`
  matches the recorded protocol. The companion inputs record preserves every
  actual configure argv, including forced `-DVCPKG_MANIFEST_INSTALL=OFF`.
- All four samples match the independently captured restored dependency digest
  `0ecb7332b14479eabffbc061dba9d94492bfb494a936e9be5bf2caf384e97c54`.
  Path/content/mode/link identity is checked before samples and after configure
  and every build. These checks establish stability between checks, not package
  correctness before the first check. No earlier known-good digest exists;
  the restored canonical ci build and full CPU tests provide correctness checks.
- All four canonical results remain `claim_eligible: false`. No runtime, GPU,
  sanitizer, Sandbox/test-suite compile-time, cross-host or publication-qualified
  performance claim. Broader C92 and BUILD-006 remain open.

## Rejected attempt and verification

The first incomplete cohort is retained separately and excluded in full. Its
initial configure restored 18 packages into the shared vcpkg installation; the
next-sample guard caught the changed dependency digest. BUG-197 now prevents
package installation in the borrowed tree and checks identity immediately after
configure and builds. The entire ABBA cohort restarted after canonical ci
restoration. No sample from that failed attempt enters the table above.

Claude reviewed the source, harness and interpretation. Review corrections
include matching class-owner attachment, explicit `<string_view>`, broader
dependency fingerprints, runner provenance and the clean-build caveat. The
source/compiler graph guard rejects nine scanner-level injected forbidden
imports on real ci metadata and accepts the unchanged metadata; this is not
a recompiled negative-control experiment.

Final canonical configure and complete `IntrinsicTests` reconciliation pass.
The full CPU gate selects 4,641 cases: 4,640 pass, zero fail, one expected
ASan-only GLFW skip, 133.88 seconds. Earlier focused CTest passes 335 cases;
26 tooling tests and all four canonical benchmark results validate. Layering,
test layout, task policy, documentation links and clean-workshop checks pass.
Four production files change from 1,274 to 1,284 lines: this boundary repair
adds ten declaration/linkage lines, with no new production file, module,
service, allocation or duplicate implementation.

## Retained evidence

The four canonical result JSON files are retained once inside the archive under
`accepted-cohort/results/`; their individual hashes are in the evidence index.

- [Summary and source-file differences](../diagnostics/runtime265_20260915/summary.json)
- [Protocol, configure commands and dependency identity](../diagnostics/runtime265_20260915/inputs.json)
- [Verification record](../diagnostics/runtime265_20260915/verification.json)
- [Evidence hashes](../diagnostics/runtime265_20260915/evidence-index.json)
- [Raw accepted/rejected cohorts and verification/review logs](../diagnostics/runtime265_20260915/raw-evidence.tar.gz)
- [Manifest](../../../benchmarks/ci/manifests/engine_compile_iteration_service_borrows.yaml)
