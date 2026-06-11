---
id: GRAPHICS-084C
theme: F
depends_on: [GRAPHICS-084]
---
# GRAPHICS-084C — Visualization property-buffer Vulkan smoke

## Goal
- Add opt-in Vulkan smoke coverage proving graphics-owned visualization property buffers are consumed through the promoted packet/BDA path on a Vulkan-capable host.

## Non-goals
- No new visualization packet family.
- No arbitrary property-array editor UI.
- No runtime-owned GPU resource lifetime.
- No RHI/CUDA retirement decision; `GRAPHICS-086` owns that audit.

## Context
- Owner/layer: `graphics/vulkan` consumes renderer-owned visualization packets and buffers; runtime remains a data-only producer of copied property arrays and packet metadata.
- `GRAPHICS-084` closes the backend-neutral residency seam at `CPUContracted`: runtime adapters can emit property-buffer upload descriptors, renderer-owned residency uploads them through `RHI::BufferManager`, and scalar/color/vector/isoline packets receive published BDAs before validation.
- This task owns only the `Operational` proof for a Vulkan-capable host. It should reuse the existing visualization-overlay GPU smoke infrastructure and avoid broadening the property-buffer scope beyond current promoted adapters.

## Required changes
- [x] Add or extend a `gpu;vulkan` visualization smoke that submits a graphics-owned property buffer and consumes the published BDA through a concrete Vulkan frame.
- [x] Keep packet validation and residency diagnostics on the existing `VisualizationPackets` and renderer stats surfaces.
- [x] Preserve runtime/ECS layering by submitting immutable runtime snapshots only.

## Tests
- [x] Add labelled `gpu;vulkan` coverage for the property-buffer-backed visualization packet path.
- [x] Keep the default CPU gate unchanged; Vulkan smoke remains opt-in.

## Docs
- [x] Update `src/graphics/renderer/README.md`, `docs/architecture/graphics.md`, and `docs/migration/nonlegacy-parity-matrix.md` for the Vulkan proof.
- [x] Update `tasks/backlog/rendering/README.md` and regenerate `tasks/SESSION-BRIEF.md` when this task retires.

## Acceptance criteria
- [x] A Vulkan-labelled smoke exercises at least one graphics-owned visualization property buffer through packet BDA publication and backend command execution.
- [x] Diagnostics distinguish skipped/non-operational Vulkan hosts from property-buffer validation or upload failures.
- [x] No runtime/ECS live data or GPU-resource ownership is introduced below `runtime`.

## Status
- Completed 2026-06-11 at maturity `Operational`.
- PR/commit: this retirement commit.
- Scope: the existing visualization-overlay `gpu;vulkan` smoke now submits renderer-owned property-buffer descriptors for vector-field position/vector data, verifies property-buffer upload diagnostics, and records the Vulkan `VisualizationOverlayPass` only after packet BDA publication succeeds.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Visualization.*PropertyBuffer|PropertyBuffer.*Visualization' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Verification results
```bash
cmake --build --preset ci --target IntrinsicTests
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
bash -lc "set -o pipefail; ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Visualization.*PropertyBuffer|PropertyBuffer.*Visualization' --timeout 120 | tee /tmp/intrinsic_graphics084c_ctest_vulkan.log"
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
tools/ci/run_clean_workshop_review.sh . --strict
git diff --check
python3 tools/repo/check_root_hygiene.py --root .
```

Result: `ci` built `IntrinsicTests`; `ci-vulkan` configured and built `IntrinsicTests`;
`VisualizationOverlaySurfaceGpuSmoke.PropertyBuffersPublishBdasBeforeOperationalVulkanCommandStream`
passed 1/1 on the promoted Vulkan path. Layering, test layout, doc links, task
policy, task-state links, session-brief freshness, docs-sync diff checks,
clean-workshop automated scorecard rows, and `git diff --check` passed. Root
hygiene remains warning-mode with pre-existing `.agents/` and `imgui.ini` root
entries.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Importing runtime/ECS into graphics or Vulkan backend code.
- Expanding visualization residency beyond selected property arrays used by promoted adapters.

## Maturity
- Target: `Operational` on Vulkan-capable hosts.
- This task provides the Vulkan-capable-host `Operational` proof for the `GRAPHICS-084` property-buffer residency seam.
