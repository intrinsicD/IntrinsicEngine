---
id: GRAPHICS-139
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive dependency cleanup; reviewed diff, compiler metadata and preset verification.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-139 — Keep device APIs out of resource-manager interfaces

## Goal
Reuse the established borrowed-device declaration in the four RHI resource
manager interfaces, preserving their implementation, ownership and behavior.
Operator explicitly requests continued compile/reuse cleanup with Claude.

## Decision
- Baseline `e8dfe9185`. BufferManager, TextureManager, SamplerManager and
  PipelineManager import Device only for an `IDevice&` constructor declaration.
  Their implementation units already import Device directly. Other public
  records have direct owning imports; TextureManager keeps Bindless.
- Reuse the non-exported `extern "C++"` declaration from EditorProcessing;
  Device already defines the matching globally attached class. Do not change
  manager attachment, lifetime rules, device behavior or add a wrapper/file.
- All four proposed compiler-boundary checks reject Device on original ci
  metadata. Baseline dependency counts are 17 for BufferManager, 16 for the
  others. Counts describe interface dependencies, not elapsed build time.
- Claude accepts the linkage and scope. Check constructor consumers for lost
  transitive reachability; rebuild all in-tree tests. Supplement canonical
  Clang 23 verification with a fresh, cache-disabled Clang 20 RHI build.
- A complete device import belongs here again only if a public value or inline
  implementation actually needs it. ImGui/debug upload sharing remains separate:
  byte/vertex limits and empty-input behavior differ and need runtime evidence.

## Acceptance criteria
- [x] Four interfaces use the existing device borrow, with implementation bytes
      unchanged and all public records resolved through their owners.
- [x] Boundary guards pass on final metadata; original negative controls retained.
- [x] Claude source review resolved, canonical ci build/focused/full CPU tests
      pass, and clean Clang 20 producer verification passes.
- [x] Documentation and inventory current; no unmeasured timing claim.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'RenderCompilationLocality|BufferManager|TextureManager|SamplerManager|PipelineManager|RenderSubsystemRegistry|RendererFrameLifecycle|GpuAssetCache' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Fresh Clang 20 uses the `ci` preset with explicit matching compilers/scanner,
Null/headless, tests/benchmarks/cache off and package installation disabled,
in an owned disposable build directory, targeting `ExtrinsicRHI`. This verifies
the changed producers on the minimum compiler; it is not a Clang 20 full-suite
or GPU execution claim. Exact commands and review output stay in session scratch.

## Review and structural verification
- All four implementation files are byte-identical to baseline. Interface
  changes are exactly the borrowed declaration plus one synopsis each:
  543 → 547 production lines; no new production file or module.
- Compiler interface dependency counts are BufferManager 17 → 6,
  TextureManager 16 → 6, SamplerManager 16 → 5 and PipelineManager 16 → 5,
  with no added modules. These are not consumer rebuild or elapsed-time counts.
- All 29 directly spelled constructor-use files obtain the device owner:
  12 import it directly and 17 include MockRHI.hpp, which imports it before
  defining MockDevice. The registry's emplace path also imports Device directly.
- Claude's source review is resolved. Corrected misplaced architecture prose
  and limited the README's guard statement to the interface dependency. Exact
  owner declaration, mock provenance and the guard's transitive traversal
  settle the remaining questions; no speculative source-format changes needed.
- Fresh cache-disabled Clang 20 ci-derived ExtrinsicRHI build passes all 184
  steps, including four implementation objects and the static archive. All four
  compiler-boundary checks also pass on this fresh tree. This does not claim
  full Clang 20 tests or any GPU execution.
- Strict clean-workshop bundle and changed-path documentation sync pass;
  source documentation audit has zero errors (30 existing comment review
  findings are outside this dependency-only change). Inventory remains 417.
  Manual scorecard: rows 1–3 pass (layer, target and public ownership preserved),
  rows 4–7 n/a (no renderer growth, recipe/pass or capability change), row 8 pass
  (no temporary exceptions). No resource lifetime or backend code changed.

## Completion — 2026-09-15
- Commit reference: the enclosing resource-manager dependency commit.
- Canonical ci Clang 23 configure and complete IntrinsicTests build pass.
  Focused CTest: 167 passed. Full CPU: 4,645 passed, zero failures, one
  expected ASan-only GLFW skip (4,646 selected, 137.23 seconds).
- Claude's final conditional review is satisfied by the completed link/tests;
  fresh Clang 20 evidence remains scoped to RHI producers. No GPU/sanitizer
  runtime or measured compile-time improvement is claimed.
+