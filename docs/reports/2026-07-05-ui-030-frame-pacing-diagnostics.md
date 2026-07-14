# UI-030 Frame-Pacing Diagnostics Report — 2026-07-05

## Metadata

- Task: `UI-030` — Sandbox EditorUI frame-pacing diagnostics.
- Capture source: `ExtrinsicSandbox --frame-pacing-report`.
- Preset: `ci-vulkan` on a local Vulkan-capable host.
- Artifact schema: `intrinsic.frame_pacing.v1`.
- Scope: bounded diagnostic capture and cause ranking only. This report makes
  no performance-improvement claim.

## Capture Method

The bounded capture was produced after building the sandbox with promoted
Vulkan enabled:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicRuntimeContractTests
LSAN_OPTIONS='suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp:fast_unwind_on_malloc=0:detect_leaks=0' \
ASAN_OPTIONS='symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0' \
timeout 60s build/ci-vulkan/bin/ExtrinsicSandbox \
  --frame-pacing-report /tmp/ui030-frame-pacing.json \
  --frame-pacing-frames 16
```

The sanitizer environment matches the CTest environment used by the
`ci-vulkan` preset. Without that environment, the same sanitizer build can keep
running under leak detection after the report is written.

The automated schema check also passed:

```bash
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R 'ExtrinsicSandbox.FramePacingDiagnosticCapture' \
  -L 'gpu' -L 'vulkan' --timeout 180
```

## Backend Conditions

The capture requested promoted Vulkan, but the default sandbox app logged:

- `VulkanRequestedButNotOperational status=RequestedButIncompleteGate
  reason=BarrierValidationFailed`
- validation warnings for vertex shader output locations `4`, `8`, and `3`
  not consumed by the fragment shader.

That means this default CLI capture is a valid frame-pacing diagnostic sample,
but it is not proof of an operational Vulkan present/acquire/fence bottleneck.
`BUG-056` owns making the default `ExtrinsicSandbox` frame-pacing capture
validation-clean and operational, or deterministically skipped for environment
reasons.

## Bottleneck Ranking

The capture contains 16 frame samples. Aggregate frame time was
`15019322us`, with mean frame time `938707us` and max frame time `1002060us`.

The phase percentages below use `summary.total_micros` as context. Some phase
timers are nested diagnostic scopes, so the table is a ranking aid rather than
an additive budget.

| Rank | Phase | Total microseconds | Share of reported total |
| --- | --- | ---: | ---: |
| 1 | `present_micros` | 12939630 | 86.15% |
| 2 | `render_contract_micros` | 1683850 | 11.21% |
| 3 | `render_prepare_micros` | 1503129 | 10.01% |
| 4 | `operational_transition_micros` | 338177 | 2.25% |
| 5 | `render_execute_micros` | 168249 | 1.12% |
| 6 | `render_graph_execute_micros` | 95364 | 0.64% |
| 7 | `imgui_end_micros` | 33983 | 0.23% |
| 8 | `imgui_editor_callback_micros` | 31974 | 0.21% |
| 9 | `fixed_step_micros` | 10269 | 0.07% |
| 10 | `platform_begin_micros` | 8457 | 0.06% |

The top measured phase is `present_micros`. Because the run fell through the
Vulkan operational gate, the evidence-backed conclusion is narrower: the
default capture is dominated by frame lifecycle/present fallback pacing, not by
Sandbox EditorUI CPU work.

## Ruled-Out Causes

- Editor callback/model work is not dominant in this run:
  `imgui_editor_callback_micros` totals `31974us` (`0.21%`).
- ImGui draw-data copying is not dominant:
  `imgui_draw_data_copy_micros` totals `1865us` (`0.01%`).
- Render-graph compile/execute work is not dominant:
  compile totals `4487us` (`0.03%`) and execute totals `95364us` (`0.64%`).
- Selection readback and pick draining are not dominant:
  `selection_readback_micros` totals `649us` and
  `selection_pick_drain_micros` totals `19us`.
- Fixed-step simulation work is not dominant:
  `fixed_step_micros` totals `10269us` (`0.07%`).

## Follow-Ups

- Open
  [`RUNTIME-138`](../../tasks/backlog/runtime/RUNTIME-138-nonblocking-selected-entity-editor-cache-pipeline.md)
  owns the nonblocking selected-entity editor/cache/job pipeline, so UI-030
  does not expand into model-construction rewrites.
- Retired
  [`GRAPHICS-110`](../../tasks/archive/GRAPHICS-110-imgui-upload-buffer-in-flight-safety.md)
  owns ImGui upload buffer in-flight safety.
- Retired
  [`GRAPHICS-113`](../../tasks/archive/GRAPHICS-113-selection-outline-id-work-pruning.md)
  owns selected-outline ID work pruning.
- Retired
  [`GRAPHICS-114`](../../tasks/archive/GRAPHICS-114-retained-imgui-overlay-copy-upload-path.md)
  owns retained ImGui overlay copy/upload cleanup.
- Retired
  [`BUG-056`](../../tasks/archive/BUG-056-extrinsic-sandbox-default-vulkan-validation-gate.md)
  owns the default `ExtrinsicSandbox` Vulkan validation-gate fallback found by
  this capture.

## Conclusion

UI-030 provides the repeatable capture loop and a concrete ranking for the
default sandbox run. The current measured problem is not editor CPU rebuild,
ImGui copy/upload, render-graph compile/execute, selected-entity readback, or
fixed-step simulation. The dominant measured phase is present/fallback frame
lifecycle time, and the default app's operational Vulkan validation gate issue
is split to `BUG-056` before any Vulkan present-path conclusion is claimed.
