---
id: RUNTIME-139
theme: B
depends_on:
  - RUNTIME-125
maturity_target: Operational
---
# RUNTIME-139 — Static AoS storage and shader operational path

## Goal
- Implement the optional profile-gated AoS fast lane for proven-static geometry:
  AoS managed storage, shader/pipeline variants, promote-on-edit conversion back
  to SoA, and opt-in Vulkan parity evidence.

## Non-goals
- No default storage-model change; ADR-0022's uniform SoA lane remains default.
- No adoption claim without benchmark baseline comparison.
- No unrelated renderer/runtime storage refactor.

## Context
- Owning subsystem/layer: `graphics/renderer` for `GpuWorld` storage allocation,
  geometry record flags, and shader variants; `runtime` extraction owns the
  static-to-dynamic edit trigger.
- RUNTIME-125 retired at `CPUContracted`: benchmark smoke and planning-only
  storage/promotion contracts exist, but no AoS GPU lane is allocated and no
  shader variants are selected.
- RUNTIME-129 makes `GpuGeometryResidencyView` the factual live-content
  contract for object-space normal bakes. Its current provider accepts only the
  truthfully advertised uniform-SoA lane with tightly packed position,
  texcoord, normal, and managed-index inputs; `StaticInterleavedAoS` returns the
  deterministic `UnsupportedStorageLane` result.
- This task owns the `Operational` follow-up.

## Required changes
- [ ] Add per-geometry storage-class hints that select Static=AoS only for
      benchmark-justified, proven-static geometry; Dynamic remains SoA.
- [ ] Add an AoS managed sub-allocation path in `GpuWorld` with explicit
      geometry-record lane flags.
- [ ] Preserve the RUNTIME-129 bake-residency contract: an AoS allocation must
      either keep and truthfully advertise equivalent separate bake-readable
      channels through `GpuGeometryResidencyView`, or remain
      `StaticInterleavedAoS` and return the deterministic unsupported-lane
      result. Never advertise interleaved addresses/strides as tightly packed
      SoA.
- [ ] Add shader/pipeline variants for affected passes and keep the SoA path as
      default.
- [ ] Implement first streaming edit promotion from AoS to SoA, including
      de-interleave/rebind behavior and diagnostics.
- [ ] Preserve partial-update behavior for the default SoA lane.

## Tests
- [ ] CPU/default contract tests for AoS allocation decisions, geometry-record
      flags, and promote-on-edit planning/application.
- [ ] CPU/default contract proving object-space normal-bake residency either
      validates the exact retained separate channels or rejects the AoS lane as
      `UnsupportedStorageLane` without cache allocation or command recording.
- [ ] Opt-in `gpu;vulkan` smoke proving AoS and SoA render equivalently on a
      representative static scene.
- [ ] Benchmark manifest/result validation with baseline comparison before any
      adoption or speedup claim.

## Docs
- [ ] Update `src/graphics/renderer/README.md` for the realized optional AoS
      lane, shader variants, and promotion behavior.
- [ ] Update ADR-0022 only if the fast lane is adopted; keep the default SoA
      policy explicit.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces
      change.

## Acceptance criteria
- [ ] Static geometry can render through the AoS lane when explicitly selected
      by the profile-gated policy.
- [ ] First streaming edit promotes the geometry back to SoA with no visual
      change.
- [ ] AoS selection cannot make the normal-bake provider consume channel
      addresses, formats, or strides that do not describe the retained bytes.
- [ ] `gpu;vulkan` parity smoke and benchmark validation are cited.
- [ ] No default SoA regression or unsubstantiated performance claim.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GpuWorld|GeometryPacker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'GpuWorld|GeometryPacker' --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Making AoS the default storage model.
- Silently treating interleaved AoS residency as bake-readable uniform SoA or
  falling back to a different normal-bake input without a deterministic
  unsupported-lane result.
- Landing shader variants without benchmark and parity evidence.
- Mixing unrelated renderer/runtime features.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for CPU/null
  storage/promotion contracts.
