# WORKSHOP-001 — Make layer enforcement module- and CMake-aware

## Goal
- Make `tools/repo/check_layering.py --strict` catch promoted-layer dependency violations in both C++23 module imports and CMake target links, so the written architecture contract cannot silently drift away from the actual build graph.

## Non-goals
- Do not change production C++ architecture in this task except for test fixtures under the checker's own test data.
- Do not remove existing layering allowlist entries unless they are proven obsolete by the updated checker.
- Do not weaken any dependency rule from `/AGENTS.md`.
- Do not make broad formatting or mechanical moves.

## Context
- `/AGENTS.md` is the authoritative layer contract.
- The current checker scans includes/imports but is likely blind to `Extrinsic.Platform.*` module imports and CMake `target_link_libraries(...)` edges.
- Known current issue to expose: `src/graphics/rhi/RHI.Device.cppm` imports `Extrinsic.Platform.Window`, and `src/graphics/rhi/CMakeLists.txt` links `ExtrinsicPlatform`, even though the contract says `graphics/rhi -> core` only.
- This task should intentionally make that violation visible before WORKSHOP-002 fixes it.

## Required changes
- [ ] Extend `tools/repo/check_layering.py` target-layer detection to recognize C++23 module imports using promoted module prefixes:
  - `Extrinsic.Core.*` -> `core`
  - `Extrinsic.Geometry.*` and `Geometry.*` promoted modules -> `geometry`
  - `Extrinsic.Asset.*` -> `assets`
  - `Extrinsic.ECS.*` -> `ecs`
  - `Extrinsic.RHI.*` -> `graphics_rhi`
  - `Extrinsic.Graphics.*` -> `graphics`
  - `Extrinsic.Backends.Vulkan*` -> `graphics`
  - `Extrinsic.Platform.*` -> `platform`
  - `Extrinsic.Runtime.*` -> `runtime`
- [ ] Add CMake target dependency scanning for promoted targets in `target_link_libraries(...)` calls.
- [ ] Map promoted CMake targets to layers, at minimum:
  - `ExtrinsicCore`, `IntrinsicCore` -> `core`
  - `IntrinsicGeometry` -> `geometry`
  - `ExtrinsicAssets` -> `assets`
  - `ExtrinsicECS`, `IntrinsicECS` -> `ecs`
  - `ExtrinsicRHI` -> `graphics_rhi`
  - `ExtrinsicGraphics`, `ExtrinsicGraphicsAssets`, `ExtrinsicGraphicsRenderGraph`, `ExtrinsicBackendsVulkan` -> `graphics`
  - `ExtrinsicPlatform` -> `platform`
  - `ExtrinsicRuntime`, `IntrinsicRuntime` -> `runtime`
- [ ] Make the checker report source file path, line number, source layer, target layer, reference, and whether the violation came from a C++ import/include or a CMake link edge.
- [ ] Add a small fixture directory under `tests/contract/repo/layering_fixtures/` or another repo-appropriate test fixture location.
- [ ] Add regression fixtures proving these fail:
  - `graphics/rhi` importing `Extrinsic.Platform.Window`
  - `graphics/rhi` linking `ExtrinsicPlatform`
  - `graphics` importing `Extrinsic.ECS.*`
  - `platform` importing `Extrinsic.Graphics.*`
  - `core` importing anything promoted above core
- [ ] Add regression fixtures proving these pass:
  - `runtime` importing `Extrinsic.ECS.*`, `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Platform.*`, and `Extrinsic.Asset.*`
  - `graphics` importing `Extrinsic.RHI.*`, `Extrinsic.Asset.Registry`, `Extrinsic.Core.*`, and allowed geometry GPU-view/value modules
- [ ] Ensure allowlist handling still works and still requires task/expiry/reason metadata.
- [ ] Update failure text so agents know whether to fix by moving the dependency downward, introducing a seam, or adding a temporary tracked allowlist entry.

## Tests
- [ ] Add or update a repo-tooling test for `check_layering.py` fixture cases.
- [ ] Run the updated checker against fixtures and assert expected pass/fail behavior.
- [ ] Run the updated checker against the real `src/` tree and confirm it reports the known `graphics/rhi -> platform` violation before WORKSHOP-002 lands.
- [ ] Keep existing task-policy and docs-link checks passing.

## Docs
- [ ] Update `docs/agent/architecture-review-checklist.md` to state that layer checks cover both C++23 imports and CMake target links.
- [ ] Update `docs/agent/review-checklist.md` if needed so agents treat CMake link edges as architecture edges.
- [ ] Update any `tools/repo/check_layering.py` usage docs or comments to mention module-prefix and CMake-edge coverage.

## Acceptance criteria
- [ ] `tools/repo/check_layering.py --root src --strict` fails on the current RHI/platform dependency until WORKSHOP-002 fixes it; the task's verification block wraps this call as an expected-failure check that asserts both non-zero exit and the specific `Extrinsic.Platform.Window` violation message.
- [ ] The checker catches C++ module import violations involving `Extrinsic.*` module names.
- [ ] The checker catches CMake `target_link_libraries(...)` violations between promoted targets.
- [ ] Fixture tests prove both positive and negative cases.
- [ ] No architecture rule in `/AGENTS.md` is weakened.

## Verification

The strict layer check against the real `src/` tree is an **expected-failure**
invocation for this task: WORKSHOP-001 must surface the known
`graphics/rhi -> platform` violation, which only WORKSHOP-002 is allowed to
fix. Wrap the strict run so the verification step succeeds when the checker
exits non-zero and the expected violation is reported, and fails otherwise.

```bash
# Expected-failure check: succeeds iff the checker exits non-zero AND
# reports the known graphics/rhi -> Extrinsic.Platform.Window violation.
# The fixture-only run below must pass normally.
out=$(python3 tools/repo/check_layering.py --root src --strict 2>&1); status=$?; \
  printf '%s\n' "$out"; \
  if [ $status -eq 0 ]; then \
    echo "WORKSHOP-001: expected strict layer check to fail before WORKSHOP-002 lands" >&2; exit 1; \
  fi; \
  printf '%s\n' "$out" | grep -q 'Extrinsic\.Platform\.Window' || { \
    echo "WORKSHOP-001: expected graphics/rhi -> Extrinsic.Platform.Window violation not reported" >&2; exit 1; \
  }

# Fixture-only strict run must pass: positive fixtures clean, negative
# fixtures excluded or otherwise scoped so the checker exits zero on the
# fixture root alone.
python3 tools/repo/check_layering.py --root tests/contract/repo/layering_fixtures --strict

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "unit|contract" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

The unguarded strict run against `src/` is intentionally not part of this
task's verification; it returns to the standard "must pass" form in
WORKSHOP-002's verification block, where the fix lands.

## Forbidden changes
- Do not edit `/AGENTS.md` to legalize current violations.
- Do not add broad permanent allowlist entries for promoted layers.
- Do not change runtime/RHI/renderer behavior in this task.
- Do not rename promoted targets or modules.
