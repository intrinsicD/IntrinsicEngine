---
id: GRAPHICS-165
theme: F
depends_on: [ARCH-020, RUNTIME-328, RUNTIME-330]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, the Vulkan smokes and CI
maturity_target: Operational
contract_schema: 1
contracts: []
contract_review: the vector-field draw record's C++ struct, GLSL struct and fill come from one declaration; record bytes, shader behaviour and the drawn result are unchanged; no catalog contract covers this record
---
# GRAPHICS-165 — The vector-field draw record is declared once (moonshot M2, slice 6)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
9. (2026-10-28) "Do Both please." (answer to: generated contract tests from the field tables, or
   the vector-field GPU draw record and shader struct generated from one declaration; this note is
   the second)
10. (2026-10-28) "Ja bitte. Mach 1 - 3." (3 = start this slice)

## Goal
- Moonshot M2 (`ARCH-020` row 3). The vector-field draw record is written three times by hand:
  the C++ struct (`Graphics.VisualizationOverlayUploadHelper.cppm`), the GLSL struct
  (`assets/shaders/visualization_vector_field.vert`) and the fill
  (`Graphics.VisualizationOverlayUploadHelper.cpp`). Only five `static_assert`s on size and offsets
  keep the two structs together, and the size literal (144) is itself a second declaration that
  every new field must edit. After this slice one list of rows is the record: both compilers'
  preprocessors read it, so the structs cannot drift and pass-through fields fill themselves; the
  layout is guarded by asserting each row type's GLSL scalar size and alignment.
- Count, representative change as in `RUNTIME-328` (a new uniform float the shader draws with,
  passed through from the style), production files and tests counted apart (Q1 counts both):
  - today, production 8: `GeometryPresentation.cppm` (layer member),
    `Runtime.GeometryVectorFieldStyle.cpp` (style row), `Sandbox.DomainPanels.cpp` (panel widget),
    `Graphics.VisualizationPackets.cppm` (glyph style member), `Runtime.RenderExtraction.VectorFields.cpp`
    (one line), `Graphics.VisualizationOverlayUploadHelper.cppm` (record member and the 144 size
    literal), `Graphics.VisualizationOverlayUploadHelper.cpp` (fill), `visualization_vector_field.vert`
    (struct member and use); tests: `Test.VisualizationOverlayPass.cpp` (its own 144 literal) when the
    size changes.
  - after, production 7: the upload helper's two files leave the list and the row file
    `assets/shaders/common/vector_field_draw_record.glsl` joins; the shader is edited for the use
    only. Tests unchanged in count: `Test.VisualizationOverlayPass.cpp` still changes its size
    literal (144 → 152 for an appended float: tail padding to the 8-byte alignment, the same in C++
    and in GLSL's scalar layout).
  Q1's "29 files become 3 or fewer" stays unmet; it needs the authoring side and the glyph style
  generated from the same rows (layer member, style row, style member, extraction copy), recorded as
  the next step for the operator to choose. This slice removes repeated declarations; the count
  moves by one.

## Plan
1. **Declaration** `assets/shaders/common/vector_field_draw_record.glsl` (the shader build already
   rebuilds on every `*.glsl` there): one row per record field in record order, types spelled in
   GLSL (`mat4`, `uint64_t`, `uint`, `float`), one of three kinds:
   - `VF_RECORD_PACKET(type, Name)`: copied from `packet.Name`;
   - `VF_RECORD_STYLE(type, Name)`: copied from `packet.Style.Name`;
   - `VF_RECORD_DERIVED(type, Name)`: computed by the upload helper (`PackedColor`, `Flags`,
     `ColormapID`, `ColorBufferBDA`).
   No C++ expression lives in the shader asset; the file is plain macro rows plus a synopsis.
2. **GLSL**: `visualization_vector_field.vert` defines the three macros as `type Name;`, includes
   the rows inside `struct VectorFieldDrawRecord`, undefines them. The shader body is unchanged.
3. **C++ struct**: `VisualizationVectorFieldDrawRecord` declares member aliases for the GLSL type
   names (`using mat4 = glm::mat4; using uint = std::uint32_t; using uint64_t = std::uint64_t;`,
   member scope so nothing leaks) and includes the rows as value-initialized members. The guard
   is on the types, not on a size literal: each alias (and `float`) is asserted to have its GLSL
   scalar size and alignment (`mat4` 64/4, `uint64_t` 8/8, `uint` 4/4, `float` 4/4). Then C++'s
   layout (each member aligned to its type, the struct to its largest member) is GLSL's scalar
   layout, inner and tail padding and array stride included, for any rows of these types; a type
   without an alias (`vec3`, ...) does not compile in C++. The four literal offset asserts move to
   the existing record test beside its 144 literal: they check that this slice keeps the bytes.
   (Revision 2, after the delta review: the first revision's "no padding" guard would have refused
   a valid appended float, whose stride is 152, and counted aligned GLM padding as data.) The current
   non-zero member defaults (`ObjectToWorld`, `RowStride`, `Scale`, `LineWidthPx`,
   `ColorRangeMax`) go: every record is filled from its packet, and the only other users (two tests)
   `memcpy` over a value-initialized record.
4. **Fill**: the upload helper includes the rows with `record.Name = packet.Name;` /
   `record.Name = packet.Style.Name;` / nothing, then assigns the four derived fields by hand.
5. **Build**: the graphics renderer library gets `assets/shaders` as a PRIVATE include directory
   (the record's interface unit and the fill are its own sources). No change to
   `cmake/CompileShaders.cmake`.
- Unchanged: record layout and bytes (same rows, same order), shader logic, packets, extraction,
  scene, agent, panel.
- Not in this slice: generating the glyph style, the layer member, the style row and the extraction
  copy from the same rows (the Q1 step), and other hand-mirrored GPU structs.

## Tests (predictions as assertions)
- Refactoring tests, pass before and after: `VisualizationOverlayPassContract.*` record contents
  (every field, including the four derived ones) and the test's own size literal (144, unchanged:
  same rows, same order).
- The shader set compiles with glslc (`IntrinsicShaderOutputs`), and the opt-in `gpu;vulkan`
  vector-field smokes pass on the promoted Vulkan path (the GPU reads the record through the
  generated struct); executed passes and capability skips are reported apart.
- Mutations, one at a time, each must fail: `Scale` marked `PACKET` instead of `STYLE` (does not
  compile: the packet has no `Scale`); the `PackedColor` assignment removed (a record-contents test
  red, it checks a nonzero color); the `uint64_t` alias bound to a 4-byte type (the type guard
  fails); `LengthBufferBDA` and `ColorBufferBDA` swapped (both structs follow, but the bytes move:
  the test's offset asserts fail).
- Dependencies: after touching only the row file, `ninja -n` lists both C++ consumers (the upload
  helper interface and implementation units) and the shader's `.spv` for rebuild.

## Acceptance criteria
- [x] One declaration produces the C++ struct, the GLSL struct and the pass-through fill.
- [x] Record bytes unchanged; record tests and Vulkan vector-field smokes pass; the generated layout
      guard and the row-file dependencies shown by the mutations and `ninja -n`.
- [x] The count recorded here and in `ARCH-020`, with the Q1 gap and its next step.
- [x] Codex plan and code review; focused suites, structural checks and the Vulkan smokes pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeGraphicsCpuTests IntrinsicShaderOutputs
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(VisualizationOverlayPassContract|VisualizationPackets|RuntimeVectorFieldExtraction)\.'
# promoted Vulkan path, as in RUNTIME-328's live check (RTX 4090, X11 :1)
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
DISPLAY=:1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json ctest --test-dir build/ci-vulkan -L gpu -L vulkan --no-tests=error -R '^(VisualizationOverlaySurfaceGpuSmoke\.|RuntimeSandboxAcceptanceGpuSmoke\.VectorField)' --output-on-failure --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Log
- 2026-10-28: Codex plan review (packet in this session's scratchpad, `plan165.answer.md`, task
  sha256 `3e838870…`): revise. B1 the count missed the 144 literal; B2 verification too broad
  (use `IntrinsicShaderOutputs`, the two GPU smoke executables, an anchored selection with
  `--no-tests=error`); O1 name mutations that really trip; O2 show the row file's rebuild
  dependencies; O3 mark the slice started in `ARCH-020`. B2 and O1-O3 taken; B1 answered by
  generating the layout guard from the rows (see Plan 3, disagreement stated) and counting
  production and tests apart.
- 2026-10-28: Codex delta answer (task sha256 `fe225fbc…`): revise. B2, O2, O3 resolved; B1 path
  count resolved, but the "no padding" guard was wrong twice: N1 it refuses a valid appended float
  (sum 148, stride 152 on both sides), and with aligned GLM gentypes it counts inner padding as
  data; dropping the literal offsets lost the unchanged-bytes check (a swap of two 8-byte rows
  passes everything else). Taken: type-representation asserts instead (Plan 3), offsets moved to the
  record test, mutations corrected (O1).
- 2026-10-28: Implemented as in revision 2. Row file `assets/shaders/common/vector_field_draw_record.glsl`
  (16 rows: 8 packet, 4 style, 4 derived); the shader and the C++ struct include it; the fill
  copies packet and style rows by macro and assigns the four derived fields; `ExtrinsicGraphics`
  gets `assets/shaders` as a PRIVATE include directory. Verification on the final tree: build of
  `IntrinsicGraphicsContractCpuTests IntrinsicRuntimeGraphicsCpuTests IntrinsicShaderOutputs`
  (glslc compiles the shader with the rows); CTest
  `VisualizationOverlayPassContract|VisualizationPackets|RuntimeVectorFieldExtraction|RenderCompilationLocality`
  63/63; `SourceRules` 8/8; task policy, layering, doc links pass; module inventory unchanged.
  Vulkan (RTX 4090, driver 580.159.04, X11 :1): `VisualizationOverlaySurfaceGpuSmoke.*` and
  `RuntimeSandboxAcceptanceGpuSmoke.VectorField*` 10/10 passed, no skips. Mutations, each alone and
  restored: `Scale` as `PACKET` → build error; `PackedColor` assignment removed → record test red;
  `uint64_t` alias to 4 bytes → type assert fails; `LengthBufferBDA`/`ColorBufferBDA` swapped → the
  test's offset asserts fail. Touching only the row file rebuilt both upload-helper units and the
  shader (and every other shader, which already depends on every include). jscpd: no new clone.
- 2026-10-28: Codex code review (diff sha256 `fb530d05…`): approve, no blocking finding; revision 2
  resolves N1, B1 and O1 of the delta answer. Optional O1 (describe the guard as explicit
  representation asserts, in the row file and the Goal) and O2 (tracker) done.
