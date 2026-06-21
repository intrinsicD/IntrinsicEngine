---
id: GRAPHICS-095
theme: B
depends_on: []
maturity_target: CPUContracted
---
# GRAPHICS-095 — CPU-testable buffer transfer math and validation helper

## Goal
- Add a backend-neutral, CPU-pure `RHI::BufferTransfer` module that provides
  sub-range validation, offset/size alignment, and partial-write region planning
  for buffer transfers, mirroring the existing `RHI::TextureUpload` math module,
  so every upload/readback call site shares one fail-closed buffer-range
  contract instead of re-deriving it.

## Non-goals
- No new device/queue surface and no backend code (that is GRAPHICS-096+).
- No change to `IDevice::WriteBuffer` / `ReadBuffer` signatures or behavior.
- No per-channel vertex layout decisions (owned by RUNTIME-122 / ADR-0022).

## Context
- Owning subsystem/layer: `src/graphics/rhi/` (`graphics/rhi -> core` only).
- `RHI::TextureUpload` (`src/graphics/rhi/RHI.TextureUpload.cppm`) is the
  exemplar: CPU-pure free functions + a packed layout struct, fully covered on
  the default CPU gate, with alignment encoded as
  `RequiredBufferOffsetAlignment(Format)`. There is no buffer equivalent.
- Buffer sub-range correctness is today re-checked ad hoc: `WriteBuffer` /
  `ReadBuffer` document "offset + size must fit within SizeBytes"
  (`RHI.Device.cppm:135-163`) but expose no shared validator; `BufferManager::View`
  separately clamps ranges (`RHI.BufferManager.cppm:117-119`).
- ADR-0023 makes this the base layer for the readback ring (GRAPHICS-096) and
  ADR-0022 / RUNTIME-124's per-channel partial writes both need the same
  "is this (offset,size) legal and how is it aligned?" contract.

## Required changes
- [ ] Add `src/graphics/rhi/RHI.BufferTransfer.cppm` (module
      `Extrinsic.RHI.BufferTransfer`) with CPU-pure helpers:
      sub-range validation against a `BufferDesc` (offset+size fit, no overflow),
      copy-region alignment helpers, and a partial-write region planner that
      turns a set of dirty (offset,size) sub-ranges into validated, optionally
      coalesced copy regions. Return `Core::Expected` / explicit status, never
      silent truncation.
- [ ] Keep non-trivial bodies in a matching `.cpp` implementation unit per
      AGENTS.md §5; register both via `intrinsic_add_module_library` /
      `FILE_SET CXX_MODULES`.
- [ ] Do not import any backend or non-`core` module.

## Tests
- [ ] CPU contract test `tests/contract/graphics/Test.BufferTransfer.cpp`
      (labels `unit;graphics`): valid/invalid sub-ranges, overflow at the
      `SizeBytes` boundary, alignment rounding, and partial-write region
      planning (including coalescing and rejection of out-of-range regions).
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/graphics/rhi/README.md` to list the new module and its role.
- [ ] Cross-link ADR-0023 from this task; no architecture-doc changes required
      (no new dependency edge).
- [ ] Refresh `docs/api/generated/module_inventory.md` for the new module surface.

## Acceptance criteria
- [ ] `Extrinsic.RHI.BufferTransfer` exists, imports only `core`, and is CPU-only.
- [ ] All validation/alignment/planning functions are fail-closed and covered by
      the new contract test on the default CPU gate.
- [ ] Module inventory and `rhi/README.md` reflect the new surface.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'BufferTransfer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work or any backend/device code.
- Adding a dependency edge from `graphics/rhi` to anything but `core`.

## Maturity
- Target: `CPUContracted`. This is the intended endpoint: the module is CPU-pure
  math/validation with no backend behavior to operate, exactly like
  `RHI::TextureUpload`. No `Operational` follow-up is owed; operational use is
  exercised by the consumers GRAPHICS-096 and RUNTIME-124.
