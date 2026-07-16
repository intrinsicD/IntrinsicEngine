---
id: ASSETIO-010
theme: F
depends_on: [BUG-093]
maturity_target: Operational
---
# ASSETIO-010 — Async model companion preflight

## Goal

- Add a non-blocking, stale-safe GLTF/GLB dependency preview that enables
  Sandbox import only after the primary source and required external buffers
  are readable, while reporting missing external images as non-blocking
  warnings and revalidating all dependencies in the real import path.

## Non-goals

- No file IO, manifest parse, or companion probe in the app/ImGui callback or
  in per-frame facade-model construction.
- No GPU upload, renderer, material-residency, or visibility-policy change;
  this task stops at CPU asset/model dependency readiness and runtime import
  dispatch.
- No remote URI fetch, virtual filesystem, archive/pak locator, file watcher,
  native file dialog, or generalized hot-reload system.
- No new `*Service`, registry, queue framework, or duplicate executor; reuse
  `AssetImportPipeline`, `SandboxEditorSession`, and the engine-owned
  `StreamingExecutor`.
- No decoded-payload cache or promise that preview avoids the import worker's
  authoritative reread/decode.
- No promotion of missing external images to hard failures; the current
  TinyGLTF contract treats those image reads as optional and preserves their
  URI for diagnostics.

## Context

- Dependency: `BUG-093` supplies the runtime-owned `CanImport`/disabled-reason
  model and app tooltip presentation. This task adds the asynchronous source
  and companion state consumed by that model; it must not add a second UI
  validation path.
- Ownership/layering: `assets` remains CPU-only and may own plain external-path
  and dependency data over core IO; runtime owns TinyGLTF integration,
  background scheduling, request-generation/stale-result handling, facade
  composition, and dispatch. The app consumes copied runtime records only.
- The checked-in `assets/models/Duck.gltf` is a deterministic repository
  fixture: it references required buffer `Duck0.bin` and optional image
  `DuckCM.png`. `assets/models/Duck0.bin` exists beside the model;
  `assets/models/DuckCM.png` does not, while a same-named image exists under
  `assets/textures/`. GLTF URI resolution is relative to the model directory,
  so this fixture must preview as required-buffer-ready with an optional-image
  warning, not search unrelated asset directories. The checked-in
  `assets/models/Duck.glb` is the embedded/self-contained comparison case.
- Live baseline on 2026-07-16 exercised the current Sandbox File / Import
  controls with absolute paths. `Duck0.bin` correctly kept payload/import
  disabled as a standalone unsupported extension. `Duck.glb` and `Duck.gltf`
  both auto-resolved to ModelScene, queued, completed, selected, and focused
  one visible primitive; GLB reported one embedded texture and two texture
  upload requests, while GLTF reported no embedded texture and one upload
  request. The GLTF UI exposed neither a Pending dependency preview nor the
  missing optional adjacent `DuckCM.png` warning, and the absolute-path run
  does not close the repository-relative exactly-once resolver regression.
- Temporary screenshots/videos under
  `/tmp/intrinsic-live-ui-completion/` record that manual audit; durable
  acceptance remains the deterministic tests in this task, never those local
  artifacts.
- TinyGLTF treats an external `buffers[].uri` as required and validates its
  declared byte length. An external `images[].uri` load is non-critical to
  overall GLTF loading; `data:` URIs and images backed by `bufferView` require
  no separate loose file, although a buffer-view image inherits the readiness
  of its owning buffer.
- Current `Asset.ModelTextureIOBridge` resolves external paths relative to the
  source parent, while the runtime TinyGLTF callbacks also receive/join a base
  directory. Preview and import must share one resolution rule and resolve a
  repository-relative URI exactly once; existing absolute-path-only tests are
  not sufficient evidence for that contract.
- Runtime architecture already requires model/texture reads and decode to run
  through the persistent `StreamingExecutor`, with only bounded main-thread
  result apply mutating editor/runtime state. Preview is advisory UI state;
  the import worker remains authoritative when files change after preview.

## Slice plan

- **Slice A — CPU dependency contract.** Add plain preview/dependency records,
  one shared external-URI resolver, GLTF/GLB manifest classification, required
  buffer checks, optional image warnings, and deterministic fake-IO plus
  checked-in Duck fixture coverage.
- **Slice B — async runtime composition.** Reuse `AssetImportPipeline` and its
  `StreamingExecutor` dependency to submit preview work on path/hint changes,
  cancel superseded work where possible, and discard every completion whose
  attachment epoch or request generation is stale.
- **Slice C — editor operational proof.** Feed pending/ready/warning/failure
  state through the `BUG-093` facade model, revalidate on dispatch/worker read,
  and exercise the real `File / Import` window during Null-window
  `Engine::Run()`.

## Required changes

- [ ] Add plain CPU dependency-preview records for source state, resolved
      payload/format, external URI, resolved path, resource kind
      (`Buffer`/`Image`), requirement (`Required`/`Optional`), availability,
      core error, and diagnostic text. Keep implementation/control-flow bodies
      out of `.cppm` surfaces.
- [ ] Make `Asset.ModelTextureIOBridge` the single external-resource path
      resolution owner: runtime preview/import callbacks pass the unresolved
      URI plus source-model path, the bridge resolves it exactly once and then
      reads the resulting path, and no caller pre-joins or re-resolves the
      bridge result. Relative URI resolves against the model's parent, absolute
      paths remain absolute, `data:` URIs remain embedded, and no fallback
      search path is introduced.
- [ ] Implement manifest-only GLTF/GLB inspection in the existing promoted
      model IO implementation: classify `buffers[].uri`, `images[].uri`, data
      URIs, buffer-view images, and embedded GLB chunks without materializing
      ECS, `AssetService`, graphics, or GPU state.
- [ ] Check required external buffers for readability and declared byte-length
      sufficiency; a missing, unreadable, empty/short, or unsupported remote
      buffer makes preview fail closed with its URI and resolved path.
- [ ] Check external images as optional dependencies. Missing/unreadable images
      append deterministic warnings but do not clear readiness; embedded/data
      and buffer-view images do not request a separate loose-file check.
- [ ] Extend the existing runtime import pipeline/session rather than adding a
      new service: submit preview only when the effective `(path, payload hint)`
      request changes, represent queued/running state as `Pending`, and deliver
      the result through the existing main-thread apply drain.
- [ ] Give each preview request a monotonically increasing generation tied to
      the editor attachment epoch. On path/hint change or detach, cancel prior
      work when possible and unconditionally discard late results whose
      generation/epoch no longer matches; stale work must never re-enable
      import or replace the current reason/warnings.
- [ ] Merge preview state into the `BUG-093` file-import model: pending preview,
      missing primary source, and missing required buffer disable import with
      one highest-priority reason; optional image warnings remain visible while
      `CanImport` stays true.
- [ ] Revalidate the preview request key immediately before dispatch, then let
      the queued import worker reread and decode the primary source and
      companions authoritatively. A post-preview file change must fail the
      normal import event/queue path rather than consume stale preview data.
- [ ] Preserve existing geometry/standalone-texture routing: formats without a
      model companion manifest become ready after the asynchronous primary
      source check and do not acquire GLTF-specific dependency records.

## Tests

- [ ] Add assets/runtime unit coverage proving repository-relative external
      paths resolve exactly once, absolute paths remain unchanged, data URIs do
      not become filesystem paths, and a URI cannot fall back from the model
      directory into `assets/textures`.
- [ ] Instrument the fake IO/bridge boundary to assert one resolver invocation
      and one final read path per external URI; the runtime callback must pass
      the original unresolved URI and must not pre-join the model directory.
- [ ] Use `assets/models/Duck.gltf` and `assets/models/Duck0.bin` directly as a
      checked-in mixed-dependency fixture: assert the required buffer is ready,
      the absent adjacent `DuckCM.png` is an optional warning, the resolved
      image path remains under `assets/models`, and overall preview is ready.
- [ ] Use `assets/models/Duck.glb` as the checked-in self-contained comparison:
      assert no external required dependency and ready preview.
- [ ] Add deterministic fake-IO cases for missing/short/unreadable required
      buffer, missing optional image, embedded buffer data URI, image
      `bufferView`, malformed JSON/GLB, unsupported remote URI, and primary
      source failure; do not create `/tmp` fixtures.
- [ ] Add runtime contract coverage for one submission per effective request,
      pending state, main-thread publish, cancellation, path-change and
      hint-change stale discard, detach/reattach epoch rejection, and optional
      warnings that do not disable import.
- [ ] Add a dispatch-time mutation regression: after a ready preview, make the
      fake required buffer unavailable and prove the real import fails through
      its normal event/queue diagnostic without materializing an asset/entity.
- [ ] Add an app-linked Null-window `Engine::Run()` integration that opens
      `File / Import`, waits boundedly for the Duck preview, and observes ready
      import plus the optional-image warning through the real facade/editor
      callback path.
- [ ] Preserve app-to-runtime-only source/link coverage and prove neither
      assets nor the preview implementation imports ECS, graphics, platform,
      app, or live runtime ownership into the assets layer.

## Docs

- [ ] Update `src/assets/README.md` with external-resource resolution,
      required-buffer versus optional-image semantics, and the CPU-only data
      ownership boundary.
- [ ] Update `src/runtime/README.md` and `docs/architecture/runtime.md` with
      preview submission, generation/epoch stale rejection, main-thread apply,
      warning propagation, and dispatch-time authoritative reread.
- [ ] Update `src/app/Sandbox/README.md` with pending/ready/warning behavior in
      `File / Import`; keep the app documented as presentation-only.
- [ ] Regenerate `docs/api/generated/module_inventory.md` for changed module
      surfaces, and refresh `tasks/SESSION-BRIEF.md` when the task changes
      lifecycle state.

## Acceptance criteria

- [ ] No source or companion IO/parse runs in the ImGui callback or per-frame
      model builder; one effective request schedules bounded worker work and
      applies only through the main-thread drain.
- [ ] Missing/unreadable/short required buffers disable import with stable URI,
      resolved-path, and core-error diagnostics; missing optional images warn
      without disabling import.
- [ ] Checked-in `Duck.gltf` previews ready with `Duck0.bin` satisfied and the
      absent adjacent `DuckCM.png` reported as optional; `Duck.glb` previews as
      self-contained.
- [ ] Every superseded or detached preview completion is rejected and cannot
      mutate the current file-import model.
- [ ] Dispatch never trusts preview bytes: request identity is checked before
      queueing and the real worker reread/decode fails closed on post-preview
      changes without partial asset/ECS materialization.
- [ ] The canonical app-linked Null-window integration exercises pending to
      ready/warning state through `Engine::Run()`, satisfying `Operational`
      without a GPU/Vulkan requirement.
- [ ] Layering remains `assets -> core`, runtime owns scheduling/composition,
      and `app -> runtime` only; no new service/registry/executor is introduced.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict

cmake --preset ci
cmake --build --preset ci --target IntrinsicAssetUnitTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'AssetModelTextureIOBridge|RuntimeAssetModelTextureIO|SandboxEditorUi\.FileImportPreview|SandboxEditorPresentation\.FileImportPreview' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120

cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes

- Polling/statting/reading/parsing the candidate source or companions once per
  frame, or blocking the app/ImGui callback on worker completion.
- Allowing a stale preview completion to mutate readiness, warnings, selected
  hint, import result, assets, or ECS state.
- Searching unrelated directories for a missing relative URI or treating
  `assets/textures/DuckCM.png` as satisfying `Duck.gltf`'s adjacent
  `DuckCM.png` reference.
- Treating optional external images as hard buffer prerequisites, or treating
  missing/short external buffers as warnings.
- Moving TinyGLTF parsing, IO backend access, `AssetService`, ECS, graphics, or
  runtime ownership into the app or assets layer.
- Resolving one external URI in both runtime and the bridge, or exposing an API
  whose caller cannot tell whether its input is unresolved or canonical.
- Adding a parallel executor, generic dependency registry/service, decoded
  payload cache, remote fetcher, file watcher, or unrelated import format.
- Treating preview readiness as authoritative after dispatch or permitting
  partial materialization after authoritative reread/decode fails.

## Maturity

- Target: `Operational` through a bounded app-linked Null-window
  `Engine::Run()` integration that observes the real asynchronous preview from
  `Pending` to ready/warning. CPU unit/contract tests pin URI classification,
  hard/optional dependency semantics, cancellation, stale discard, and
  dispatch-time revalidation; no Vulkan follow-up is owed for this CPU asset
  workflow.
