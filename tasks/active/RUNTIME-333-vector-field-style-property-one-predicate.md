---
id: RUNTIME-333
theme: F
depends_on: [ARCH-020, RUNTIME-327, RUNTIME-331]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: one predicate decides which properties can drive a vector-field layer's per-arrow length or color, for the editor model, the command and extraction; the only behaviour change is that, in the CPU binding without a GPU front, a topology property bound by a scene file now gives the layer its uniform style instead of a per-arrow one; no catalog contract covers this rule
---
# RUNTIME-333 — One predicate for per-arrow style properties (moonshot M2, slice 9)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
14. (2026-10-28) "Mach bitte mit M2 weiter"

## Goal
- Moonshot M2's central point: "the UI's list of candidates and the command's validator come from
  the same predicate", and its contract test "the candidates the UI offers equal exactly what the
  validator accepts". The UI-083 bug this targets: the panel offered properties that extraction then
  rejected.
- Today the rule "which property can drive a layer's per-arrow length (a scalar) or color (a scalar,
  vec3 or vec4)" exists twice: `VectorFieldStyleOption` in
  `Runtime.VisualizationEditingOperations.Actions.cpp` (the model's candidates, the agent's
  `vector_fields` listing and the command's check), and extraction's own conversion in
  `Runtime.RenderExtraction.VectorFields.cpp` (`ConvertVisualizationScalars`, then `Get<vec3/vec4>`,
  with an element-count check). They agree on kinds (both reject Vec2 and Int64) and on counts
  (both against the raw property-set size, deleted slots included) but not on topology
  properties: the editor excludes `v:deleted`, `e:v0`, ... (`IsTopologyProperty`); extraction binds
  them when a scene file names one. `SandboxEditorUi.VectorFieldStyleCandidatesAreExactlyWhatTheCommandAccepts` already
  ties offered to accepted; nothing ties offered to extraction's binding.

## Plan
1. **One rule**, exported from `Extrinsic.Runtime.GeometryPresentation` (declared beside
   `GeometryVectorFieldStyleFields`, defined in `Runtime.GeometryVectorFieldStyle.cpp`):
   `FindGeometryVectorFieldStyleProperty(properties, domain, name)` returns the property's value
   kind and whether it is a scalar, or nothing: not a topology property, one value per element, a
   scalar kind (Bool, Int32, UInt32, UInt64, Float, Double) or Vec3/Vec4. The body is the editor's
   `VectorFieldStyleOption` moved, not rewritten.
2. **Editor**: `VectorFieldStyleOption` builds its `EditorVectorFieldStyleOption` from the rule
   (candidates, listing and command check unchanged).
3. **Extraction**: the CPU style binding asks the rule first; length needs a scalar, color any;
   otherwise the layer draws its uniform length or color and counts a fallback, as for a property
   that does not convert today. Unchanged and outside the claim: the value conversion
   (`EnsureStyleCache`), and the GPU-preview path, which binds an observed GPU front (a pending
   output, or an existing scalar property that has one) before the CPU rule is asked; the command
   also keeps accepting an unchanged unresolved name and a pending output.
4. **Contract test**, generated over the entity's property sets
   (`RuntimeVectorFieldExtraction.CpuStyleBindingMatchesOfferedProperties`, no GPU observer): a mesh
   with a deleted triangle (deleted rows kept) whose vertices carry every scalar kind, Int64, Vec2,
   Vec3, Vec4, a property one value short and `v:deleted`, and whose edges carry `e:f` and their
   topology rows; for every property of the vertex and the edge domain, one layer binding it as
   length and one as color. Every packet exists and is renderable. Offered as a scalar ⇔ a length
   buffer address, no fallback; else no address, uniform, one fallback. Offered ⇔ a non-uniform
   color mode with a color buffer address, no fallback; else uniform, no address, one fallback. A
   scene-bound `v:deleted` length keeps its name and its row reports "Per-arrow length 'v:deleted'
   not found or not usable". This is CPU binding, not what the shader finally shows (it still skips
   non-positive lengths, invalid vectors and colors).
- Not in this slice: the GPU-preview path's own rule (pending outputs are floats by construction),
  the lane visualization's color property (its own rule, `RUNTIME-329`), other offered/accepted
  pairs.

## Tests (predictions as assertions)
- The new contract test fails before steps 1-3 on the topology properties (bound, not offered) and
  passes after; `SandboxEditorUi.VectorFieldStyleCandidatesAreExactlyWhatTheCommandAccepts` and the extraction
  style tests pass before and after.
- Mutations, one at a time, each must turn a test red: the rule accepts vec2 (offered/accepted and
  offered/bound tests red: offered but not bound); extraction skips the rule (topology bound again);
  the rule drops `UInt64` from the scalars (the existing offered/accepted count red).

## Acceptance criteria
- [x] One exported rule used by the editor model, the command check and extraction.
- [x] The CPU-binding-equals-offered contract test, red before and green after.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(RuntimeVectorFieldExtraction|SandboxEditorUi|VectorFieldStyle|DeclaredFieldContracts|AgentOperations)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Log
- 2026-10-28: Codex plan review (`plan333.answer.md` in this session's scratchpad): revise. The
  divergence confirmed (topology only). B1 assert packet presence and renderability, color address,
  uniform/zero/fallback for refused bindings; B2 name it CPU binding, keep the GPU-preview and
  command exceptions; O1 Int64, the edge domain, deleted rows; O2 a scene-bound refused name is kept
  and reported. All taken (test renamed `CpuStyleBindingMatchesOfferedProperties`).
- 2026-10-28: Implemented. Before the extraction change the first test version failed exactly on
  `v:deleted` (bound as length and color, not offered). Final test green. Mutations, each alone and
  restored, all red: the rule accepts Vec2 (offered/accepted and the new test); extraction skips the
  rule (the new test); the rule drops UInt64 (the offered/accepted counts). The scalar kind list
  remains once more in extraction's GPU-front observer (`IsScalarKind`), the GPU path this slice
  leaves alone. Focused CPU `RuntimeVectorFieldExtraction|SandboxEditorUi|VectorFieldStyle|
  DeclaredFieldContracts|AgentOperations|RuntimeSceneSerialization` 326/326; `SourceRules` 8/8;
  layering clean; module inventory unchanged; changed lines formatted with clang-format 20.1.8.
- 2026-10-28: Codex review of the revision and the change (diff sha256 in this session's
  scratchpad, `review333.diff`): approve; B1, B2, O1, O2 resolved, the moved rule unchanged in
  meaning. Optional: the test now checks the full fallback text (": uniform length"); summaries say
  "CPU binding without a GPU front" (a valid GPU front can still bind a topology name; packet binding
  does not prove visible glyphs).
