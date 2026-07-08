---
name: intrinsicengine-stale-build-triage
description: Rule out stale C++23-module/ccache build artifacts before diagnosing any unexplained crash in IntrinsicEngine. Use whenever a test or tool fails with an unexplained SEGV, PC=0x0 virtual dispatch, vtable/slot mismatch, AddressSanitizer report that nobody can explain, clang frontend bus error/ICE during module compilation, or a failure that appeared right after a `.cppm` interface change, virtual-function addition, or module split — especially when the failure is "not reproducible" for someone else. Trigger phrases include "stale build", "ccache", "clean rebuild", "BMI", "works on a fresh clone", "SEGV in tests after module change".
---

# IntrinsicEngine Stale-Build Triage

C++23 modules + ccache make one specific failure mode cheap to hit and
expensive to misdiagnose: **stale module artifacts (BMIs) that leave two
translation units disagreeing about a class layout or vtable**. This repo has
lost whole sessions to phantom bugs of this class (`BUG-013`, the
HARDEN-079/GEOM-021/022 module-split ASan ghost, the `BUG-016` retirement
build's clang bus error). The rule this skill encodes:

> **Before diagnosing any unexplained SEGV, ASan report, vtable anomaly, or
> compiler crash, verify the failure survives a clean build.** If it
> disappears, it was never a source defect — do not "fix" the source.

## When to suspect a stale artifact

Reach for this skill *first* when any of these hold:

- The failing code path recently had a `.cppm` module interface change — a
  new/removed virtual function, reordered members, or a module split/rename.
- The crash is a **null-PC virtual dispatch** (`PC = 0x0`) or lands in a
  virtual call that "cannot" be null (classic cross-TU vtable-slot mismatch —
  `BUG-013`).
- Working around one missing vtable slot surfaces a *different* slot mismatch
  (`BUG-013` attempt 2: adding an override "fixed" `BindFrameSampledTexture`
  and broke `PushConstants`). Two shifting slot mismatches are the tell for a
  cross-TU layout disagreement, i.e. stale BMIs — not a source defect.
- The compiler itself dies: clang frontend **bus error / ICE during module
  compilation** (`BUG-016` retirement: failed with ccache, passed identically
  with `CCACHE_DISABLE=1`).
- An ASan/TSan report appears only on an incremental tree and nobody can
  explain the write (module-split batch: "stale incremental C++23 module
  layout state from ccache/module artifacts").
- The failure reproduces on your incremental tree but **not on a fresh clone
  or in CI** (or vice versa).

## The triage ladder (cheapest first)

```bash
# 1. Rebuild the failing target with ccache out of the loop.
CCACHE_DISABLE=1 cmake --build --preset ci --target <failing-target>

# 2. If unchanged, reconfigure and rebuild (clears CMake-level module maps).
cmake --preset ci && CCACHE_DISABLE=1 cmake --build --preset ci --target <failing-target>

# 3. Last resort: full clean tree. This is the authoritative check.
rm -rf build/ci && cmake --preset ci && cmake --build --preset ci --target <failing-target>
```

Re-run the failing test after each rung. The clean-preset rebuild rule is
documented as the authoritative prevention in `src/graphics/rhi/README.md`
(mandatory after RHI interface/vtable changes) — cite that section rather than
re-litigating it.

## Interpretation

- **Failure vanished on a clean build** → record the incident as a stale-BMI
  artifact in the task/bug file (so the next agent does not repeat the
  diagnosis), and do **not** land speculative source "hardening" as a fix.
  `BUG-013` preserves the anti-pattern: two naive source workarounds were
  attempted against a phantom vtable bug before the clean build closed it as
  not-reproducible.
- **Failure survives a clean build** → it is real. Hand off to
  `intrinsicengine-diagnose` (generic loop) or
  `intrinsicengine-vulkan-frame-triage` (frame-content defects) with the
  clean-build evidence recorded — that evidence is what makes the later
  bisection trustworthy.

## Related gotchas that masquerade as bugs

- `IntrinsicBenchmarkSmoke` must be **built explicitly** before its ctest
  entry can pass; a missing binary is a build-orchestration gap, not a test
  regression (`BUG-012` resolution notes, `BUG-031`).
- `Testing/Temporary/LastTestsFailed.log` is historical state only; current
  pass/fail comes from the CTest command just run (AGENTS.md §7).
- Stale non-preset build trees, or trees configured with an older compiler,
  are **not valid verification** for module changes (AGENTS.md §5) — a "pass"
  there is as untrustworthy as a phantom failure.
- CI dependent-step noise: a missing upstream artifact can present as a
  primary failure in downstream steps (`BUG-005`); check the first failing
  step, not the loudest one.

## Exit criteria

- The failure is either closed as stale-artifact (with the clean-build
  command and result recorded in the task file) or confirmed real on a clean
  tree and routed to the appropriate diagnosis skill.
- No source change ships whose only justification is a failure that a clean
  build made disappear.
