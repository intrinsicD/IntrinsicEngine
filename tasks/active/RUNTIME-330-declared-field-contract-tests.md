---
id: RUNTIME-330
theme: F
depends_on: [ARCH-020, RUNTIME-327, RUNTIME-329]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: test generation over the existing field tables plus an internal flat read/write seam for the lane style (the scene reader's own check-and-apply core); no runtime behaviour, scene format or agent surface changes; no catalog contract covers these tables
---
# RUNTIME-330 — Contract tests generated from the declared field tables (moonshot M2, slice 5)

## Operator questions (verbatim, 2026-10-28)
3. "start improving the engine by implementing the Moonshot.md. Keep properly track of where you
   and your subagents are at so we can continue anytime properly."
8. "Then check 328 and 329 in a running sandbox and then we’ll choose the next M2 step"
9. "Do Both please." (answer to: generated contract tests from the field tables, or the vector-field
   GPU record and shader struct generated from one declaration; this note is the first)

## Goal
- Moonshot M2 lists generated **contract tests** among the products of one declaration (save/load
  round trip, a refused value changes nothing, the offered values equal what the validator accepts).
  Today the two tables (`RUNTIME-327` vector-field style, 13 rows; `RUNTIME-329` lane style, 17 rows)
  are tested by hand-listed cases (`Test.VectorFieldStyle.cpp` lists 17 bound cases by hand); a new
  row gets no boundary test unless someone writes one.
- What generated cases can and cannot catch: they catch consumers that disagree with the declaration
  (a reader, the agent schema or a command check ignoring a bound, token, type or field; a setter
  dropping a value; a refused value applied in part). They cannot show that a declaration itself is
  right, find an undeclared field, or catch a getter and setter that are wrong in the same way. The
  hand-written fixtures stay for that.

## Plan (revised after the Codex plan review, see Log)
1. **Case generator** (local free function in the new test file; a shared header waits for a
   second consumer). From one `ConfigFieldSpec` and the field's default JSON value it returns
   cases `{value, accepted, wireOnly/directOnly}`, the verdict computed from the spec alone. It
   covers the six types the two tables use and fails the test on anything else (Int, Enum,
   UIntArray, PropertyRef, Object, `Nullable`, `NonEmpty`), so a new row cannot be skipped silently.
   - Bool: `true`, `false` accepted; `0`, `"true"`, `null` refused.
   - UInt: effective bounds lo/hi (exclusive bounds one step inside, default 0 and `UINT32_MAX`);
     lo and hi accepted; lo-1 (or `-1` at lo 0), hi+1 (`4294967296` at `UINT32_MAX`), `-1`, a
     fractional number, an integer-valued float (`1.0`), `"1"`, `true` refused.
   - Float: an inclusive bound accepted and its float neighbour outside refused; an exclusive bound
     refused and its float neighbour inside accepted; an open side gets `±1e30f` accepted; an
     integer number inside the range accepted; `"1"`, `true`, `null` refused; NaN and infinity as
     direct-JSON probes only (JSON text cannot carry them). Every accepted numeric value is exactly
     float-representable, so the stored value can equal the request; the integer probe keeps its
     integer JSON storage.
   - String: `""` and a new string accepted; `1`, `null`, `[]` refused.
   - StringEnum: every token accepted; a case-changed token (when it is not itself a token), `""`,
     `0`, `null` refused.
   - Array: wrong container (`{}`, a number) refused; `[]` accepted only when the effective
     minimum (absent MinItems = 0) is zero; length MinItems-1 (only when MinItems > 0) and
     MaxItems+1 refused, MinItems and MaxItems accepted (filled with an accepted item); every
     item case at the last position of a valid array and one refused item at the first position,
     with the item's verdict.
   Wire cases go through text (dump and parse) before every check, so `1` is the parsed unsigned
   integer an agent or a scene would send, not a directly constructed signed one.
2. **One generated test per table** (`DeclaredFieldContracts.VectorFieldStyle`,
   `DeclaredFieldContracts.LaneVisualizationStyle`), every row, every case:
   - reader: the table's flat reader (vector field: `ReadGeometryVectorFieldStyleJson` with agent
     keys; lane: the new seam) gives the expected verdict;
   - refused: the input also carries a changed accepted value of another row, and the target stays
     equal to the defaults (all or nothing);
   - accepted: the stored value equals the request (read back through the flat writer) and the
     target survives the scene encoding into a fresh default object. The commands' per-field checks
     (`CheckGeometryVectorFieldStyle`, module-internal, and `CheckLaneVisualizationStyle`) are the
     same declared check over the flat writer's output, so a stored value equal to its accepted
     request passes them; no separate call (decided while implementing, see Log);
   - every row has at least one accepted case that changes its value (else a dropped setter passes);
   - vector field only (the lane style has no agent surface): the registered `vector_field` tool
     schema, inside a valid `update` envelope, gives the same verdict through
     `ValidateAgentArgumentsJson`; asserted supported first (`IsSupportedAgentSchemaJson`).
   Scope of the agreement claim: declared per-field rules only. Cross-field and context checks
   (`color_range[0] < color_range[1]`, the property binding) stay with the command and its existing
   tests; scene-only rules (required keys, unknown keys ignored) stay with the scene tests.
3. **Lane seam**: the scene reader already gathers present keys into a flat object by spec name,
   checks it and applies all or nothing. Factor that check-and-apply core into
   `ReadLaneVisualizationStyleFields(flat, config)` (called by the scene reader; scene gathering and
   its leniency unchanged) and expose the existing `Flat` (strict) as
   `LaneVisualizationStyleFieldsJson(config)` in the internal header. No new behaviour.
4. **Undo**: attach an `EditorCommandHistory` to the existing refusal tests
   (`SandboxEditorUi.VectorFieldCommandRejectsBadColormapsAndRanges`,
   `SandboxEditorUi.VisualizationConfigCommandRefusesUndeclaredValues`) and assert that refusals
   leave `UndoCount()` and the state unchanged.
- Kept, not replaced: the hand-listed `VectorFieldStyle.AgentSchemaBoundsMatchTheCommand` cases
  (they feed invalid typed values to `ValidateGeometryVectorFieldLayer`, which the generated test
  does not) and the fixture round trip.
- Recorded, not changed (B2): a finite double outside float range (`1e39`) passes the declared
  check, then the setter stores infinity, which the command check refuses. The generated accepted
  cases stay float-representable; the gap goes to the Log and the operator decides on a fix.
- Not in this slice: generated typed-invalid commands per row; agent ≡ panel (no panel seam).

## Tests (predictions as assertions)
- Both generated tests pass on the current tables (they test agreement, not new behaviour); every
  row yields at least one accepted, one refused and one value-changing case (asserted).
- `DeclaredFieldContracts.GeneratorCases`: one hand-picked spec per supported type yields the
  listed cases with the listed verdicts; an unsupported spec is reported.
- The two undo assertions pass on the current commands.
- Mutations, one at a time, each must turn a test red (assertion failure, not a crash):
  1. the vector-field `stride` setter drops its value;
  2. `BuildFieldSchema` emits no `maximum` for Float (`line_width` above 32 then passes the schema);
  3. `CheckField` skips the Float range check (reader accepts `line_width` above 32);
  4. `ReadGeometryVectorFieldStyleJson` applies each row before checking the rest;
  5. the lane flat reader ignores `isoline_values`;
  6. the vector-field command records its undo step before the check.

## Acceptance criteria
- [x] Every row of both tables is checked by generated cases on reader, all-or-nothing, stored value
      and scene round trip; the vector field also on its agent schema.
- [x] The lane flat reader is the scene reader's core, not a second implementation.
- [x] Refused commands leave no undo step (both refusal tests).
- [x] The six mutations each turn a test red (eight run, see Log).
- [x] Codex plan and code review; the focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(DeclaredFieldContracts|VectorFieldStyle|SandboxEditorUi|RuntimeSceneSerialization)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/generate_session_brief.py --check
```

## Log
- 2026-10-28: Codex plan review (session of `849ae7d0`, answer `plan330.answer.md` in its
  scratchpad): revise, B1-B6 (case matrix, float representation, schema scope, observable
  mutations, keep negative command cases and add undo, focused gate). This revision answers them.
- 2026-10-28: Codex delta answer (this session, `plan330-delta.answer.md` in its scratchpad,
  packet sha256 `0be517ba…`): revise on two remnants, no new blocking issue; B2-B5 resolved. Done:
  `[]` generated for every Array (B1), doc-link, layering and session-brief checks added (B6),
  the float wording (O). While implementing: the separate command-check call was dropped
  (`CheckGeometryVectorFieldStyle` is module-internal, and both per-field checks are the declared
  check over the flat writer, which stored == request already covers).
- 2026-10-28: Implemented. `Test.DeclaredFieldContracts.cpp` (generator, `GeneratorCases`, one test
  per table), lane seam `ReadLaneVisualizationStyleFields` / `LaneVisualizationStyleFieldsJson`
  (scene reader calls it), undo counts in the two refusal tests. Focused gate: build
  `IntrinsicRuntimeContractTests`, CTest `DeclaredFieldContracts|VectorFieldStyle|SandboxEditorUi|
  RuntimeSceneSerialization` 273/273 passed; task policy, doc links, layering, session brief and
  `SourceRules` (8/8) pass; jscpd finds no clone in the new test (the one hit is the existing
  `kSpecs` lambda shared by both style tables, not touched). Mutations, each alone, all red: by
  assertion for the stride setter dropping its value; schema without Float `maximum`; `CheckField`
  without the Float range; reader applying the valid fields of a refused object; reader checking
  and applying row by row; lane flat reader ignoring `isoline_values`; refused vector-field command
  recording an undo step (6 steps instead of 1, state unchanged, so only the new undo assertion
  sees it). `CheckField` checking only the first array item aborts the lane test instead
  (presumably an unchecked non-number item reaching the setter's `get<float>` without exceptions).
- 2026-10-28: Codex code review (packet digest `99cedfea…`): approve, no blocking finding. Optional
  O1-O4 done: item probes use two positions where the length allows; UInt ranges are cut to the
  32-bit domain; `GeneratorCases` covers Bool and String; a refused case carries the changes of the
  rows before and after it. Re-verified after them (focused suites and all eight mutations).
- Gap (B2), recorded at first: a finite double outside float range, e.g. `"length": 1e39`, passed
  the declared check, then the setter stored infinity, which the command check refused later; the
  same for the lane's unbounded floats in scene files.
- 2026-10-28: Operator: fix the gap ("Mach 1 - 3."). Not in the shared `CheckField`: some Float
  rows are double-backed (`CoherentPointDriftConfig` `Tolerance`, `Sigma2Floor`). The two float-backed
  readers (`ReadGeometryVectorFieldStyleJson`, `ReadLaneVisualizationStyleFields`) now apply to a
  copy and check the applied fields once more as stored, then commit; this also refuses a double just
  above an exclusive 0 (`length` 5e-324 would store 0). The generator adds these doubles (±1e39 on an
  open side, `nextafter` inside an exclusive bound when the stored float lands on it) as reader-only
  cases: the agent schema's `number` still offers them. Deferred, not inexpressible (Codex): exact
  per-row bounds would be `-T < x < T` with `T = 2^128 - 2^103` (binary32 round to nearest), and
  for `length` also `x > 2^-150`; plain `FLT_MAX`/`denorm_min` would refuse values that round
  validly. The reader refuses before any change, so the broader schema loses nothing but an early
  message. Before the fix both table tests failed on exactly these cases (`length` 1e39 and
  5e-324, `color_range`, `range_min`/`range_max`, `isoline_width`, lane `color`, `isoline_color`,
  `isoline_values`), `GeneratorCases` passed; after it the focused suites
  `DeclaredFieldContracts|VectorFieldStyle|SandboxEditorUi|RuntimeSceneSerialization|AgentOperations`
  pass 310/310. Other float-backed config owners were not surveyed for the same gap.
- 2026-10-28: Codex review of the gap fix and of O1-O4 as committed in `cdbb3b885` (gap-fix diff
  sha256 `5908f5c7…`): approve, no blocking finding. Optional: O1 an empty or non-finite UInt
  range now fails the generator with a diagnostic instead of a cast (asserted in `GeneratorCases`);
  O2 the schema gap described as deferred above.
