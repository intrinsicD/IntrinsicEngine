---
id: GRAPHICS-095
theme: B
depends_on: []
maturity_target: CPUContracted
---
# GRAPHICS-095 — CPU-testable buffer transfer math and validation helper

## Goal
- Add a backend-neutral, CPU-pure `RHI::BufferTransfer` module that provides
  sub-range validation, offset/size alignment, partial-write region planning, and
  **typed/dimension-match validation** for buffer transfers, mirroring the
  existing `RHI::TextureUpload` math module, so every upload/readback/binding call
  site shares one fail-closed buffer-range contract — including the single
  "do the dimensions match this range?" check that the property↔buffer-range
  binding layer (RUNTIME-126) depends on — instead of re-deriving it.

## Non-goals
- No new device/queue surface and no backend code (that is GRAPHICS-096+).
- No change to `IDevice::WriteBuffer` / `ReadBuffer` signatures or behavior.
- No per-channel vertex layout decisions (owned by RUNTIME-122 / ADR-0022).
- No knowledge of geometry property names/types (that is RUNTIME-126); this
  module works purely in (elementCount, componentBytes, offset, size, stride).

## Context
- Status: retired on 2026-06-22 at maturity `CPUContracted`.
- Selected because the then-current `tasks/SESSION-BRIEF.md` listed
  `GRAPHICS-095` as the next unblocked Theme B rendering modernization task; it
  gates `GRAPHICS-096` and downstream transfer/readback facade work.
- Owning subsystem/layer: `src/graphics/rhi/` (`graphics/rhi -> core` only).
- `RHI::TextureUpload` (`src/graphics/rhi/RHI.TextureUpload.cppm`) is the
  exemplar: CPU-pure free functions + a packed layout struct, fully covered on
  the default CPU gate, with alignment encoded as
  `RequiredBufferOffsetAlignment(Format)`. There is no buffer equivalent.
- Buffer sub-range correctness is today re-checked ad hoc: `WriteBuffer` /
  `ReadBuffer` document "offset + size must fit within SizeBytes"
  (`RHI.Device.cppm:135-163`) but expose no shared validator; `BufferManager::View`
  separately clamps ranges (`RHI.BufferManager.cppm:117-119`).
- ADR-0023 makes this the base layer for the readback ring (GRAPHICS-096),
  ADR-0022 / RUNTIME-124's per-channel partial writes, and the RUNTIME-126
  property↔buffer-range binding — all three need the same "is this (offset,size)
  legal, how is it aligned, and do the dimensions match?" contract.
- GRAPHICS-084 already proved the dimension-match rule for the visualization path
  (`ExpectedVisualizationValueStride` + `ElementCount × stride == bytes` in
  `Graphics.VisualizationPackets.cpp`); this task lifts the *math* of that rule
  into a reusable, property-agnostic primitive so RUNTIME-126 can generalize it.
- ADR-0023 records the cross-task transfer foundation and names this task as the
  CPU-pure lower-layer prerequisite.

## Implementation plan
- Add `src/graphics/rhi/RHI.BufferTransfer.cppm` and
  `src/graphics/rhi/RHI.BufferTransfer.cpp` to the existing `ExtrinsicRHI`
  module library. The interface exports value records and function declarations;
  validation, sort/coalesce, overflow, and packing logic stays in the
  implementation unit.
- Export only backend-neutral helpers in `Extrinsic::RHI`: `BufferRange`,
  `BufferCopyRegion`, alignment options, partial-write planner options/result,
  dimension-match descriptors/result, and `Core::Expected`-returning functions.
  Import only `Extrinsic.Core.Error` and `Extrinsic.RHI.Descriptors`.
- Implement sub-range validation as strict bounds checking against
  `BufferDesc::SizeBytes`: zero-sized buffers or transfer ranges fail closed,
  and `offset + size` is checked without overflow by comparing
  `size <= capacity - offset`.
- Implement alignment helpers with division-based rounding, not bit masking, so
  non-power-of-two alignments remain valid. Alignment expansion rounds the
  destination start down and end up, then revalidates against buffer capacity.
- Implement partial-write planning over destination dirty ranges: validate each
  input, optionally expand to requested alignment, sort by destination offset,
  optionally coalesce overlapping/adjacent ranges, and pack source staging
  offsets monotonically with source-offset alignment.
- Implement typed/dimension-match validation with a property-agnostic
  `elementCount`, `componentBytes`, target region, optional stride, and exact vs
  fit-within mode. The helper reports the effective stride and footprint while
  failing closed on overflow, zero component size, stride smaller than component
  bytes, and exact/fit mismatches.
- Add a focused CPU test file under `tests/contract/graphics/` with a dedicated
  CPU-only graphics test target so the requested `BufferTransfer` filter is
  available without GPU/Vulkan labels.
- Update `src/graphics/rhi/README.md`, refresh
  `docs/api/generated/module_inventory.md`, regenerate
  `tasks/SESSION-BRIEF.md`, and retire the task after focused and full CPU
  verification.

## Completion note
- PR/commit: this retirement commit.
- Completed on 2026-06-22 at maturity `CPUContracted` in the retirement
  changeset for GRAPHICS-095. No backend or device behavior was added; the
  operational consumers remain GRAPHICS-096, GRAPHICS-098, and RUNTIME-126.

## Required changes
- [x] Add `src/graphics/rhi/RHI.BufferTransfer.cppm` (module
      `Extrinsic.RHI.BufferTransfer`) with CPU-pure helpers:
      sub-range validation against a `BufferDesc` (offset+size fit, no overflow),
      copy-region alignment helpers, and a partial-write region planner that
      turns a set of dirty (offset,size) sub-ranges into validated, optionally
      coalesced copy regions. Return `Core::Expected` / explicit status, never
      silent truncation.
- [x] Add a typed/dimension-match validator: given an element count and a
      per-element component size in bytes (the source shape) and a target region
      (offset, size, optional stride), confirm the source exactly fills/fits the
      region (`elementCount × componentBytes` vs region size, stride honored) and
      fail closed on mismatch. This is the property-agnostic "dimensions match"
      primitive RUNTIME-126 builds on; it must not reference any property or
      `ValueType` enum.
- [x] Keep non-trivial bodies in a matching `.cpp` implementation unit per
      AGENTS.md §5; register both via `intrinsic_add_module_library` /
      `FILE_SET CXX_MODULES`.
- [x] Do not import any backend or non-`core` module.

## Tests
- [x] CPU contract test `tests/contract/graphics/Test.BufferTransfer.cpp`
      (labels `unit;graphics`): valid/invalid sub-ranges, overflow at the
      `SizeBytes` boundary, alignment rounding, partial-write region planning
      (including coalescing and rejection of out-of-range regions), and
      dimension-match validation (exact fill, fit-within, stride honored, and
      fail-closed on element-count / component-size mismatch).
- [x] Default CPU gate stays green.

## Docs
- [x] Update `src/graphics/rhi/README.md` to list the new module and its role.
- [x] Cross-link ADR-0023 from this task; no architecture-doc changes required
      (no new dependency edge).
- [x] Refresh `docs/api/generated/module_inventory.md` for the new module surface.

## Acceptance criteria
- [x] `Extrinsic.RHI.BufferTransfer` exists, imports only `core`, and is CPU-only.
- [x] All validation/alignment/planning functions are fail-closed and covered by
      the new contract test on the default CPU gate.
- [x] Module inventory and `rhi/README.md` reflect the new surface.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsBufferTransferTests
ctest --test-dir build/ci --output-on-failure -R 'BufferTransfer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --files <changed-files>
```

Result on 2026-06-22: all commands above passed. The focused BufferTransfer
CTest ran 14/14 tests, and the full default CPU gate ran 3012/3012 tests with
zero failures.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work or any backend/device code.
- Adding a dependency edge from `graphics/rhi` to anything but `core`.

## Maturity
- Target: `CPUContracted`. This is the intended endpoint: the module is CPU-pure
  math/validation with no backend behavior to operate, exactly like
  `RHI::TextureUpload`. No `Operational` follow-up is owed; operational use is
  exercised by the consumers GRAPHICS-096 and RUNTIME-124.
