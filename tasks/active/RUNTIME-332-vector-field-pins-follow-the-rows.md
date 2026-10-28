---
id: RUNTIME-332
theme: F
depends_on: [ARCH-020, GRAPHICS-165, RUNTIME-331]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, the count experiment and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: test-only change of two vector-field pins (record layout literals, scene golden); no production code, format or behaviour changes; no catalog contract covers these tests
---
# RUNTIME-332 — The vector-field test pins follow the rows (moonshot M2, slice 8)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
12. (2026-10-28) "Was empfiehlst du denn bzgl q1?" (answer: drop the record-layout literals, turn
    the scene golden into a superset-plus-old-scene-loads check, re-measure)
13. (2026-10-28) "Ja ok, bitte mach runtime 332"

## Goal
- Moonshot M2 Q1 ("3 or fewer files", tests included) for the class `RUNTIME-331` built: a
  pass-through vector-field style float. Measured after `RUNTIME-331`: production 2 (row, shader
  use), tests 3 (the feature's own GPU check, the record-layout literals, the scene golden) = 5.
  The two pins fail on every new row although nothing is wrong; this slice makes them follow the
  rows without losing a defect they can still catch. Target: row, shader use, the feature's own
  test = 3. Other option kinds (enums, colors, property-bound values) are not covered by this
  claim.

## Plan
1. **Record-layout literals** (`Test.VisualizationOverlayPass.cpp`, the `static_assert`s on size 144
   and four offsets in `VectorFieldRecordsCarryLengthAndColorBuffers`): delete. Since
   `GRAPHICS-165` the C++ and GLSL structs come from one row file, and
   `Graphics.VisualizationOverlayUploadHelper.cppm` asserts each row type's GLSL scalar size and
   alignment: for the supported types, with both structs generated solely from the shared rows,
   the layouts agree. The literals mostly flag intentional layout changes; what they would still
   catch is a member or alignment change on the C++ side only, which the shared rows leave no
   place for (a GLSL-only member escapes them today as well). No in-tree consumer reads fixed
   offsets: uploads and the CPU decode use `sizeof`, the shader the shared rows. They served `GRAPHICS-165`'s "bytes unchanged" claim, which that
   slice also showed by SPIR-V identity and record-content tests; a future refactor that must keep
   bytes shows it the same way. The stale "128-byte"/"144-byte" comments lose the number.
2. **Scene golden** (`RuntimeSceneSerialization.VectorFieldLayerJsonIsUnchanged`, renamed
   `VectorFieldLayerJsonKeepsTheGoldenKeys`): keep the golden text and the fixture. Replace the
   exact equality by:
   - every golden key is written with the golden value (a renamed, removed or re-tokenized key
     fails, as now);
   - the written object has exactly one key per declared style field plus `property` (an
     undeclared extra key, e.g. `line_width` beside `lineWidthPx`, fails as it does now; the
     count grows with the rows);
   - the scene with its vector-field object replaced by the golden loads, and the loaded layer
     equals the fixture: this golden's omitted optional fields take their defaults (not a
     promise that every older scene loads; AGENTS §5 keeps format changes open);
   - the written scene loads back equal (unchanged).
   A new row's own round trip is covered by `DeclaredFieldContracts`.
3. **Count experiment**, as in `RUNTIME-331` (scripts kept in the session scratchpad): the
   `ColorGain` row plus its shader use plus its GPU check, with the strengthened check
   (`IsMagenta(dark)`); build CPU, shaders and Vulkan smoke targets; run the focused CPU suites and
   the Vulkan vector-field smokes; record every file that had to change; revert byte for byte.
   Prediction: production 2, tests 1 (the feature's own check) = 3, reported as "ColorGain trial:
   two production files plus one feature-test file" for the pass-through-float path (generic
   slider, one shader stage); other field kinds, more shader stages or custom controls can need
   more files. The trial keeps its controls: shader not reading the field, extraction copy
   emptied, and the black-arrow check (`IsMagenta(dark)`) with its `floor` mutation.

## Tests (predictions as assertions)
- Before the change, the trial needs the two pin edits (measured in `RUNTIME-331`); after, it does
  not (the experiment).
- The rewritten golden test passes now (the written JSON equals the golden, so every key matches;
  the golden loads).
- Mutations, one at a time, each must turn the golden test red at the named assertion: the row's
  scene key `lineWidthPx` renamed (golden key missing); the length-mode scene tokens change case
  (golden value differs); the layer writer adds an undeclared key (key count); with the trial row,
  `ColorGain` marked scene-required (the golden-load assertion only).
- Dropping the record literals: the type guard still fails a row type whose C++ size or alignment
  differs from GLSL (shown in `GRAPHICS-165`); no new mutation.

## Acceptance criteria
- [x] Record-layout literals gone; the record-content tests and the type guard remain.
- [x] Golden test: every golden key kept, one key per declared field, the golden still loads, the
      written scene loads back.
- [x] Count experiment run and recorded here and in `ARCH-020`.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(RuntimeSceneSerialization|VisualizationOverlayPassContract|DeclaredFieldContracts|VectorFieldStyle)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
# count experiment: also the RUNTIME-331 CPU targets, IntrinsicShaderOutputs, and on ci-vulkan
# IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests with
# DISPLAY=:1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json ctest --test-dir build/ci-vulkan -L gpu -L vulkan --no-tests=error -R '^(VisualizationOverlaySurfaceGpuSmoke\.|RuntimeSandboxAcceptanceGpuSmoke\.VectorField)'
```

## Log
- 2026-10-28: Codex plan review (`plan332.answer.md` in this session's scratchpad, task sha256
  `d26a820f…`): revise. B1 exact equality also caught an undeclared extra key: added the key-count
  assertion (declared fields + `property`) and its mutation. O1 layout wording qualified; O2 test
  renamed and the golden-load guarantee kept narrow; O3 mutation expectations named, trial controls
  kept; O4 measured scope worded.
- 2026-10-28: Implemented. `Test.VisualizationOverlayPass.cpp`: the size and offset literals and
  the record-size numbers in two comments removed. `Test.RuntimeSceneSerialization.cpp`:
  `VectorFieldLayerJsonKeepsTheGoldenKeys` (every golden key and value, key count = declared
  fields + `property`, the written scene and the golden both load into the fixture). Formatted
  with clang-format 20.1.8. Mutations of the golden test, each alone and restored, all red at the
  named check: scene key `lineWidthPx` renamed in the row file (golden key missing); length-mode
  scene tokens lower-cased (golden value, and the golden no longer loads); the layer writer adds
  `line_width` (key count).
- Count experiment (scripts from `RUNTIME-331`, reverted byte for byte): the `ColorGain` row, its
  shader use and its GPU check, nothing else. CPU focused 384/384 and Vulkan 11/11 with no further
  edit. ColorGain trial: two production files plus one feature-test file = 3, for the
  pass-through-float path (generic slider, one shader stage). Controls, each red: `ColorGain`
  scene-required (only the golden-load check); the shader not reading it (both arrows 245);
  `floor(record.ColorGain)` (dark arrow (0,0,0), `IsMagenta(dark)`); the extraction copy emptied
  (the visual check and `EveryPassThroughRowReachesThePacket`).
- Final tree: CPU focused 384/384 (`DeclaredFieldContracts|VectorFieldStyle|RuntimeVectorFieldExtraction|
  GraphicsVisualizationPackets|VisualizationOverlayPassContract|RuntimeSceneSerialization|AgentOperations|
  SandboxEditorUi|RenderCompilationLocality`), Vulkan vector-field smokes 10/10, task policy and doc
  links pass.
- 2026-10-28: Codex review of the revision and the change (diff sha256 `30f084cd…`): approve; B1
  and O1-O4 resolved, the Q1 result supported by the logged experiment. Optional (single
  geometry-presentation entity assumed) done: the test asserts exactly one.
