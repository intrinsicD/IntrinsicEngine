---
id: RUNTIME-334
theme: F
depends_on: [ARCH-020, RUNTIME-333]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the GPU-preview binding of a vector-field layer's per-arrow length and color asks the same rule as the CPU binding, the editor model's status and the command; the behaviour changes, all for names the rule refuses (a topology row, an unsupported or non-scalar kind, a short CPU property), are that extraction no longer binds a GPU front for them, the command no longer accepts them as pending GPU outputs, and the layer status no longer calls them GPU previews; no catalog contract covers this rule
---
# RUNTIME-334 — The GPU-preview binding shares the per-arrow style rule (moonshot M2, slice 10)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
15. (2026-10-28) "Bitte die drei Punkte nacheinander machen" (1: the GPU-preview path onto the
    shared rule; 2: the same one-rule check for the lane style's color property; 3: the larger M2
    goals)

## Goal
- `RUNTIME-333` made `FindGeometryVectorFieldStyleProperty` the one rule for the editor's
  candidates, the command and extraction's CPU binding, and left the GPU-preview binding out. That
  path still decides eligibility its own way: extraction's `observeFront` asks the GPU observer for
  any CPU property of a scalar kind (`IsScalarKind`, a second copy of the scalar list), with no
  topology or CPU-property element-count check (the front's own count is checked), and for a name whose kind it cannot read (a missing name, but
  also an existing Int64 property) as a pending Float; the editor's status and the command treat
  any name that does not resolve on the CPU as a possible pending Float (`PendingStyleRef`), so a
  topology row, a Vec2 or a short property passes as "GPU preview" whenever the lookup answers yes.
- This slice consolidates eligibility, not ring selection: the observer and the pending lookup
  key every scalar by its Float presentation ring (`GpuPropertyPresentationRef`), so a Double
  property's Float preview stays bound; the CPU kind passed to the observer is logical metadata
  and stays what it is today.

## Plan
1. **One front rule** beside the style rule in `Extrinsic.Runtime.GeometryPresentation`:
   `GeometryVectorFieldStyleFrontKind(properties, domain, name)`: for a name the CPU has, the style
   rule's kind when it is a scalar, else nothing (an existing Int64, Vec2/3/4, short or topology
   property); for a name it lacks (or no property set), Float unless it is a topology name.
2. **Extraction** `observeFront` asks it and deletes `IsScalarKind`; the observer's own front-count
   check stays. The GPU color auto range is left as it is (O1: the front is only bound for an
   eligible name, so the CPU cache lookup there needs no second gate).
3. **Editor**: `PendingStyleRef` takes the availability and returns the front rule's ref or nothing;
   the model's status and the command consult the pending lookup only for an eligible name.
4. **Tests**, with expectations written out case by case (B2):
   - `VectorFieldStyle.FrontKindFollowsTheStyleRule`: a standalone property set; Double → Double,
     Float → Float, absent → Float, Int64/Vec3/short/`v:deleted`/absent `v:halfedge` → none; null
     set: absent → Float, `v:connectivity` → none.
   - `RuntimeVectorFieldExtraction.GpuFrontBindingFollowsTheRule`: an observer answering every
     request with a front and recording the refs; per case (`v:d` Double, `v:pending` Float, `v:i64`,
     `v:short`, `v:deleted`, `v:halfedge` none) the length is bound from the front, and the name is
     requested under that kind, exactly when a kind is expected.
   - `SandboxEditorUi.VectorFieldPendingStyleNamesFollowTheFrontRule`: every pending lookup answers
     yes; a new length or color name is accepted only for `v:pending` (not `v:uv`, `v:deleted`,
     `v:halfedge`), and a scene-bound length is reported as a GPU preview only for `v:pending`.
- B3 (Codex: add a residency-backed case with the real observer for a Double property's Float
  presentation front): not added. The slice does not touch `ObserveGpuPropertyFront`, the pending
  adapter or `GpuPropertyPresentationRef`; a CPU Double is still passed as Double (asserted in the
  extraction test), so the real observer sees the same request as today. `PropertySmoothingTransaction.*`
  (part of the verification) runs the pending lookup against real residency, but for a lane
  visualization recipe, not vector-field extraction: a future change to the observer's
  Double-to-Float normalization would not be caught here (Codex).
  Stated for the review to judge.
- Not in this slice: the GPU path's own count check, other style consumers.

## Tests (predictions as assertions)
- The extraction and editor tests compile against the pre-change code and fail there; the rule
  test exercises the new function. All pass after; the `RUNTIME-333` contract test,
  `GpuPreviewFrontsDriveScalarStyleBuffers` and the `SandboxEditorUi` vector-field tests pass
  before and after.
- Mutations, each must turn a test red: the front rule skips the topology check; extraction asks the
  observer without the rule; the front rule returns Float for a CPU scalar.

## Acceptance criteria
- [x] One front rule used by extraction's GPU binding, the model's status and the command.
- [x] `IsScalarKind` gone; the behaviour tests red before, green after.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicSandboxEditorIntegrationTests ExtrinsicSandboxEditor
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(RuntimeVectorFieldExtraction|SandboxEditorUi|VectorFieldStyle|DeclaredFieldContracts|AgentOperations|PropertySmoothingTransaction)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-28: Codex plan review (`plan334.answer.md` in this session's scratchpad): revise. B1 the
  Float-vs-Double divergence is not real (presentation-ring normalization), the author had found the
  same; goal reworded as eligibility consolidation. B2 independent expectations and a separate rule
  test: taken. B3 a real-observer case: answered in the Plan (unchanged observer, kind asserted,
  smoothing transaction tests run). O1 no second gate in the auto-range branch: taken.
- 2026-10-28: Implemented. Against the pre-change code (production diff reverted, rule test left
  out) both behaviour tests failed: the command accepted `v:uv`, `v:deleted`, `v:halfedge` as
  pending for length and color and the status called them GPU previews; extraction requested and
  bound `v:i64` as Float. With the change all three tests pass. Mutations, each alone and restored,
  all red: the topology check skipped (all three tests); extraction without the rule (the extraction
  test); Float for a CPU scalar (the rule and extraction tests). Focused CPU
  `RuntimeVectorFieldExtraction|SandboxEditorUi|VectorFieldStyle|DeclaredFieldContracts|AgentOperations|
  PropertySmoothingTransaction` 304/304; `SourceRules` 8/8; layering clean; changed lines
  clang-format 20.1.8.
- 2026-10-28: Codex review of the revision and the change (`review334.diff` in this session's
  scratchpad): approve; B1, B2, O1 resolved, B3 accepted for this slice (caveat now in the Plan).
  Optional, all done: the pending callbacks assert entity, domain, name and Float kind (only an
  eligible name may reach them), the observer asserts entity and domain; refused GPU cases assert a
  zero length address and one fallback; task wording made precise.
