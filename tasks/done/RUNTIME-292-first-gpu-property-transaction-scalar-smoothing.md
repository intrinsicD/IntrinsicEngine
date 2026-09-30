---
id: RUNTIME-292
theme: I
depends_on: [GRAPHICS-155]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision and two independent design reviews (2026-09-29, ADR 0030); implementation owes the contract tests and gpu;vulkan smokes listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# RUNTIME-292 — First end-to-end GPU property transaction: scalar smoothing with Accept

## Goal
- ADR 0030 decisions 5-7 on one scalar consumer before any position observation.
- Vulkan property smoothing (PropertyFilter, then implicit CG):
  - reads its input from the canonical slot;
  - writes a typed ring.
- The renderer observes it when the appearance shows that scalar. The ring front is bound
  directly through the visualization recipe `BufferBDA` (float presentation view for doubles),
  with no readback.
- When the method finishes or is stopped, the panel shows Accept / Discard:
  - Accept: one readback, the existing undoable publication, then the front becomes the
    canonical slot for the new revision (the next run uploads nothing).
  - Discard: nothing is published, and observation returns to the canonical slot.
- Stale while pending: Accept is disabled with the reason. Undo changes the CPU revision, and
  the next GPU use uploads once.
- Batch and agent commands keep automatic publication.

## Completion — 2026-09-30
Commit: on `claude/cpd-nystrom` (see RETIREMENT-LOG). First end-to-end GPU property transaction
(ADR 0030 decisions 5-7):
- `property_filter.comp` Load/Store modes: kernels gather the canonical slot in its own type and
  scatter into the typed ring. The ring starts as a copy of the output's canonical slot, so
  deleted, fixed and isolated rows are byte-identical to what CPU publication writes. Implicit
  CG seeds and RHS come from the canonical slot on the device. The mass diagonal and fixed-row
  coupling stay a declared CPU stage (ADR decision 8), and their bytes are reported.
- `EditorPropertySmoothingTransaction` in the job state goes Running -> ReadyToAccept ->
  Accepting -> Applied | Discarded | Failed.
  - Accept: readback of the front (token or leased framed fallback), then the undoable
    "Smooth property" publication, then `BindRevision`.
  - Discard and cancel release the ring.
  - Stale input or output watches disable Accept.
  - Stop ends a chunked CG solve with its latest preview.
  - Batch and agent commands auto-accept.
- The renderer observes the float ring front through `ScalarVisualizationRecipe::BufferBDA`
  (`RenderExtractionCache::SetGpuPropertyObserver`, `MarkObserved`); doubles get a
  presentation ring. A new output name can be shown before Accept (`PendingResidentScalar`,
  `ExternalElementCount`). Seam-split surface lanes keep the CPU upload (documented).
- Panel: Stop / Accept (disabled with its reason) / Discard.
- Engine fix found on the way: `Renderer::BeginFrame` flushes the bindless heap each frame. The
  colormap LUT descriptor was only written by the ImGui pass, so UI-less engines sampled black.
  The pre-existing descriptor-lifetime hazard is filed as GRAPHICS-157.

Evidence:
- Contract tests `PropertySmoothingTransaction.*` (Accept binds the revision, Discard, stale
  input and output, cancel while accepting, stale at publication, a second run waits for the
  decision, float precision, Show of a pending output), plus the panel test
  `SandboxProcessingPanels.PropertySmoothingAcceptsOrDiscardsAPendingGpuResult`.
- gpu;vulkan `RUNTIME292ScalarSmoothingTransaction.*`:
  - backbuffer pixels change before Accept;
  - after Accept, CPU == front bytewise == CPU reference within tolerance, including deleted
    rows, a pinned boundary and the implicit path;
  - a run right after Accept uploads 0 bytes; the implicit run uploads 0 input bytes;
  - Discard with device work outstanding; Stop accepts the intermediate result.
- Implemented by Fable 5.1, reviewed by Codex 6 Astra (medium) in three rounds. Findings fixed:
  - readback lease;
  - deleted-row bytes;
  - implicit canonical input;
  - output watch;
  - preview of a new output;
  - panel restart orphan;
  - Show with a baked texture.
- CPU gate 5281/5281 (1 pre-existing skip). GPU suite 125/126, only the environmental
  `VulkanShutdownLsanContract` red. Operational.
- Open gap: no panel regression test for a restart on the completion frame (needs a device in
  the panel harness).

## Acceptance criteria
- [x] Contract tests:
  - Accept binds the revision;
  - Discard, cancel, stale-while-pending and undo behave as ADR 0030 says;
  - "Applied" is reported only after the CPU publication.
- [x] gpu;vulkan smoke:
  - the observed colormap changes before Accept;
  - after Accept the CPU values equal the readback and the CPU reference within the existing
    tolerance;
  - a second run on the same revision uploads zero input bytes.
- [x] Panel test: Accept / Discard appear after finishing or stopping; Accept is disabled on a
      stale input.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
