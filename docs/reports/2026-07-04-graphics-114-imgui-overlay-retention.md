# GRAPHICS-114 ImGui Overlay Retention Report — 2026-07-04

## Metadata

- Task: `GRAPHICS-114` — retained ImGui overlay copy/upload path.
- Baseline source: parent of `1752ed93` (`Reduce selected editor frame work`).
- Current evidence: GRAPHICS-114 diagnostics and tests on the current branch.
- Workflow: local PR-fast CPU contracts plus opt-in `gpu;vulkan` smoke.

## Scope

This report closes the GRAPHICS-114 measurement row for selected-entity ImGui
frames. It records observable copy/upload counters and source-derived baseline
deltas. It is not a benchmark manifest and makes no wall-clock speedup claim.

## Baseline Comparison

| Metric | Pre-retained baseline | Current GRAPHICS-114 behavior |
| --- | --- | --- |
| Font-atlas copy bytes | `Runtime.ImGuiAdapter::EndFrame()` copied the full atlas byte payload into every `ImGuiOverlayFrame`; `ImGuiOverlaySystem::SubmitFrame(const&)` then copied the frame again. | First frame or dirty atlas: `LastFrameFontAtlasCopyBytes == LastFontAtlasByteCount`. Steady unchanged frames: `LastFrameFontAtlasCopyBytes == 0`, `FontAtlasReuseCount` increments, and overlay diagnostics report `FontAtlasRetained`. |
| Draw-list POD copy bytes | Runtime still copied vertex/index/command POD payloads once when translating `ImDrawData`; graphics copied accepted draw lists again through the const submit path. | Runtime reports `LastFrameVertexCopyBytes`, `LastFrameIndexCopyBytes`, `LastFrameCommandCopyBytes`, and `LastFrameOverlayCopyBytes`; graphics accepts rvalue frames and moves accepted draw lists after validation. |
| Command upload construction | `ImGuiUploadHelper::UploadFrame(...)` built command upload lists during validation and again while producing draw-list upload records. | `ImGuiUploadResult::CommandUploadListBuilds` equals the accepted draw-list count; command upload records are reused for the final result. |
| Upload bytes | Vertex and index upload bytes were observable only by inspecting mock buffer writes. | `ImGuiUploadResult` reports `VertexUploadBytes`, `IndexUploadBytes`, and `TotalUploadBytes`. Full vertex/index payload upload remains per-frame; no reduction is claimed for those bytes. |
| Upload allocations | Buffer allocation count was observable through the helper. | `GetBufferAllocationCount()` remains the allocation counter; frame-slot reuse tests prove no extra allocation on a reused in-flight slot. |

## Captured Evidence

- `ImGuiAdapter.FontAtlasPayloadIsCopiedOnlyWhenDirty` captures the atlas
  transition: first frame copies atlas bytes, steady frame reports zero atlas
  copy bytes and retained atlas diagnostics.
- `ImGuiAdapter.EditorPanelDrawProducesNonEmptyDrawList` captures per-frame
  vertex/index/command copy bytes and their sum for a non-empty editor frame.
- `ImGuiPassContract.UploadHelperPacksTwoDrawListsAndPassRecordsPerList`
  captures a two-list upload: command-upload builds equal `2`, vertex upload
  bytes are `7 * sizeof(ImGuiOverlayVertex)`, index upload bytes are
  `9 * sizeof(std::uint32_t)`, and total upload bytes are their sum.
- `ImGuiSurfaceGpuSmoke.LargeSelectedEntityPayloadRetainsAtlasOnOperationalVulkan`
  is the opt-in selected-entity payload smoke. It runs six operational Vulkan
  frames, requires one font-atlas copy followed by at least five retained
  reuses, requires the final steady frame to report zero atlas copy bytes, and
  requires a materially large overlay payload (`>= 900` vertices and
  `>= 1200` indices) with `ImGuiPass` recorded.

## Limitations

- CPU flatten/copy time is not asserted as a test threshold because local
  wall-clock timing would be noisy in PR-fast smoke. The copied byte counters
  are the deterministic proxy used for this task.
- The retained path does not eliminate per-frame vertex/index upload of changed
  ImGui draw data. Future work that wants to cache static UI geometry should
  open a separate renderer task with a benchmark manifest and baseline.
