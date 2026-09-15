---
id: GRAPHICS-144
theme: B
depends_on: [BUILD-009]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive task; unattended workflow completion reports are exempt, but this task owns its benchmark manifests, results and source identities alongside review and test evidence.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-144 — Reduce remaining renderer interface and implementation dependencies

## Goal
Reduce the current renderer's remaining compile cost using narrower real
consumer contracts and less repeated work while preserving rendering capabilities.

## Scope and starting point
- Explicit operator-directed continuation. GRAPHICS-138–143 are complete:
  unused renderer accessors and registry getters are gone, device borrowing is
  established, scene handles have one small owner, and named buffer metadata
  has one map. These mechanisms must not be recreated or counted again.
- Start from `src/graphics/renderer/Graphics.Renderer.cppm` and `.cpp` plus their
  actual runtime/test consumers. BUILD-009 supplies the current hotspot ranking;
  the old roughly 15-second producer measurements are historical.
- Compare unused dependencies, narrower existing contracts and private state
  before splitting files or adding Pimpl. Any new boundary must remove actual
  dependency work without extra forwarding, allocation or repeated parsing.
- Reuse `Graphics.SceneHandles`, canonical RHI borrow declarations, snapshot
  types, and existing subsystem lifetime owners. Keep required texture AssetIds.
  Follow `docs/architecture/graphics.md`; preserve recipes, diagnostics, config,
  resource ordering, operational-device fallback and app → runtime ownership.
- The upload helpers are only a candidate if the measured renderer boundary
  selects them: compare ImGui UploadBytes with UploadPackedColorVertices using
  byte/vertex caps, empty-input behavior, usage flags, lease failure/reset,
  allocation counters and frame lifetime. Do not blindly route either through
  asynchronous GpuTransfer or add a broad helper import to share a short loop.
- GRAPHICS-105 owns material/visualization behavior; LEGACY-043 shader deletion;
  GRAPHICS-135 runtime scheduling; BUILD-006 backend/cache selection. This task
  does not absorb those independent changes.

## Acceptance criteria
- [ ] Audit current interface types, private state, imports and consumers;
      select a bounded cost-backed change and compare simpler alternatives with Claude.
- [ ] Implement without feature loss, new live-ECS/AssetService dependencies,
      compatibility wrappers or public subsystem forwarding. Record source/file
      and ownership deltas, including any justified new private boundary.
- [ ] Reuse the compiler-boundary test helper and retain existing guards;
      demonstrate the selected forbidden dependency on original metadata and its
      absence afterward. Preserve real material/UV/extraction consumers.
- [ ] Pass focused and full CPU checks. Verify changed module attachment with
      fresh cache-off minimum-supported Clang; if GPU resource lifetime or upload
      behavior changes, run the affected promoted Vulkan tests under ci-vulkan.
- [ ] Compare matched renderer implementation/interface edit probes and the
      graphics-library target with the existing runner. Keep negative results;
      reject extra complexity without benefit or record an evidence-backed
      no-change verdict. Do not extrapolate target timings to the whole engine.
- [ ] Resolve Claude's fixed-diff review, synchronize graphics documentation and
      changed inventory, and retire with exact source/test/measurement evidence.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality|Renderer|RenderWorld|GpuWorld|GeometryResidency|Visualization|ImGui|UvView' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
