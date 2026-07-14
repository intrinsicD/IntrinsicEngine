---
id: BUG-060
theme: G
depends_on: []
maturity_target: CPUContracted
completed: 2026-07-06
---
# BUG-060 — Scalar/isoline surfaces render black on GPU: colormap LUT created as Tex1D but sampled as sampler2D

## Status
- Retired on 2026-07-06 at `CPUContracted` on local `main`; PR not opened.
- PR/commit: the original fix was authored on
  `claude/intrinsic-scalar-black-mesh-yevo9v`; this retirement commit adds the
  deterministic promoted-Vulkan surface/isoline readback smoke and records the
  local verification that was blocked in the authoring environment.
- Diagnosis: BUG-057/058/059 verified the UI → extraction → sync →
  `GpuEntityConfig` contract healthy at the CPU/null seam, yet the sandbox
  still rendered scalar/isoline surfaces black on a real Vulkan host. The
  remaining defect is GPU-only and invisible to every CPU/null test:
  `ColormapSystem::Initialize` created each 256×1 LUT with
  `TextureDimension::Tex1D`, which the Vulkan backend maps to
  `VK_IMAGE_VIEW_TYPE_1D`. Every consumer (`forward/default_debug_surface.frag`,
  `forward/line.frag`, `forward/point.frag`, `deferred/gbuffer.frag`) samples
  the bindless heap declared as `sampler2D globalTextures[]`, and sampling a
  1D image view through a 2D sampler is undefined in Vulkan
  (VUID-vkCmdDraw-viewType-07752) — it reads back zero on common drivers, so
  `GpuResolveVisualizationColorWithColormap` returned opaque black for every
  fragment. Core validation cannot flag it because the heap is
  update-after-bind/partially-bound (GPU-AV only). The Isolines preset
  defaults to black line color, so black-on-black kept the isoline lane
  invisible too.
- Secondary defect: `GpuVisualizationApplyIsolines` received the *binned*
  normalized scalar. With `BinCount > 0` the binned t is piecewise constant,
  so the contour test painted whole bins (the entire surface when
  `BinCount == IsolineCount`) instead of lines. The UI-032 explicit-isovalue
  path already used raw t for exactly this reason.
- Fix: create the LUT as `Tex2D` (256×1); feed the un-binned normalized
  scalar to the evenly-spaced isoline helper and place contours at the
  interior level boundaries `t = k / (N + 1), k = 1..N` (the placement of the
  proven framework24 `apply_color_map` implementation, which this refines
  with smoothstep anti-aliasing).

## Goal
- Scalar-field and isoline surface visualization produce colormapped (non-black)
  output on the operational Vulkan path, with the GPU-side defect identified
  end to end rather than re-patched at the already-verified CPU seam.

## Non-goals
- No changes to the CPU contract seams verified by BUG-057/058/059 (packet
  ranges, robust auto range, degenerate-range sanitization stay as-is).
- No removal of the unreferenced `deferred/gbuffer.{vert,frag}` shaders and no
  deferred-composition albedo integration (`deferred/lighting.frag` remains a
  light-accumulation debug stub) — both are documented existing gaps outside
  this fix's scope.
- No isoline UI/control changes (owned by UI-032 lineage).

## Context
- Symptom: clicking the Appearance panel's "Scalar" or "Isolines" preset for a
  computed property (e.g. `v:mean_curvature`) still rendered the mesh black on
  the user's Vulkan host after BUG-059 landed.
- The default frame recipe forces `FrameRecipeLightingPath::Forward`
  (`BuildDefaultFrameRecipe`), so the live scalar path is
  `forward/default_debug_surface.frag` → `GpuResolveVisualizationColorWithColormap`
  → `texture(globalTextures[cfg.ColormapID], vec2(t, 0.5))`.
- The colormap LUT is the only `Tex1D` texture in the engine; all other
  bindless textures are `Tex2D`, which is why every other textured path works.
- Owning layers: `graphics/renderer` (ColormapSystem) and shared shader
  helpers (`assets/shaders/common/gpu_scene.glsl`). No layering changes.

## Required changes
- [x] `Graphics.ColormapSystem.cpp`: create LUT textures as
      `TextureDimension::Tex2D` (256×1) with a comment recording the
      sampler2D-heap constraint.
- [x] `assets/shaders/common/gpu_scene.glsl`: pass the un-binned normalized
      scalar to `GpuVisualizationApplyIsolines`; place evenly spaced contours
      at interior level boundaries `t = k / (N + 1)`.
- [x] `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`: add an opt-in
      promoted-Vulkan surface/isoline readback smoke that configures the same
      surface-lane Scalar/Isolines state the Appearance presets author on a
      loaded mesh.

## Tests
- [x] `GraphicsColormapSystem.InitializeSubmitsLutUploadsThroughTransferQueue`
      extended to pin `CreatedTextureDescs` to `Tex2D` 256×1 (regression guard
      at the CPU seam that can see the descriptor).
- [x] Focused visualization suites + default CPU-supported correctness gate.
- [x] Opt-in `gpu;vulkan` smokes:
      `RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleScalarFieldColormapResolvesOnLineAndPointLanes`
      and
      `RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleScalarFieldSurfaceAndIsolinesResolveOnGpu`.

## Docs
- [x] This task record; no doc-schema or module-surface change.

## Acceptance criteria
- [x] LUT texture descriptor is `Tex2D` and pinned by a unit test.
- [x] Evenly spaced isolines are computed from the raw normalized scalar and
      are independent of `BinCount`.
- [x] CPU gate green.
- [x] Operational Vulkan sandbox check: automated promoted-Vulkan readback
      smokes prove the shared LUT sampling path for line/point lanes and the
      surface Scalar/Isolines preset-equivalent state on `ReferenceTriangle`.

## Verification
Run in the authoring environment (2026-07-06):
```bash
# All shaders consuming the edited common/gpu_scene.glsl compile clean
# (includes flattened, since plain glslangValidator lacks glslc's -I):
glslangValidator -V --target-env vulkan1.2 <flattened shader>   # default_debug_surface.{vert,frag}, line.frag, point.frag, gbuffer.{vert,frag}
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

Focused CPU verification run locally (2026-07-06):
```bash
ctest --test-dir build/ci --output-on-failure -R 'ColormapSystem|RuntimeRenderExtraction|VisualizationAdapters|VisualizationPackets|RendererFrameLifecycle|MinimalTriangleAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

Opt-in Vulkan verification run locally (2026-07-06):
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleScalarField(ColormapResolvesOnLineAndPointLanes|SurfaceAndIsolinesResolveOnGpu)' -L 'gpu' -L 'vulkan' --timeout 120
```

Final verification run locally (2026-07-06):
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Re-declaring the bindless heap as `sampler1D` per-slot or forking per-lane
  colormap sampling helpers (GRAPHICS-091 unified them deliberately).
- "Fixing" the symptom by writing CPU-baked colors into vertex attributes.

## Maturity
- Target achieved: `CPUContracted` via the descriptor-level regression guard,
  focused visualization suites, and the default CPU gate. The original manual
  Vulkan click-check was superseded by deterministic opt-in `gpu;vulkan`
  readback smokes: the existing line/point scalar smoke proves shared LUT heap
  sampling beyond the surface path, and the new surface/isoline smoke proves a
  preset-equivalent loaded-mesh state renders colormapped output and does not
  regress to the binned-scalar false-contour behavior.
