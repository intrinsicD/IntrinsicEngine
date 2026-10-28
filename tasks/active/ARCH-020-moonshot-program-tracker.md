---
id: ARCH-020
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: program tracker; evidence lives in each workstream task
contract_schema: 1
contracts: []
contract_review: tracker only; each workstream task declares its own contracts
---
# ARCH-020 — Moonshot program tracker

## Operator questions (verbatim, 2026-10-27)
1. "what would be an absolute moonshot to improve the engine? Assume unlimited dev resources and
   12 months. use many tokens, be ambitious. Let it rip."
2. "Write it to MOONSHOT.md"
3. "start improving the engine by implementing the Moonshot.md. Keep properly track of where you
   and your subagents are at so we can continue anytime properly."

## Goal
- Implement [the moonshot](../../docs/architecture/moonshot.md) (pillars M1–M11) in reviewed
  slices, starting with its "first 30 days" list. This note is the one place that says which
  workstream is where, who works on it, and what comes next. Detail lives in each workstream task.
- Framework24 (theme J, `REVIEW-004`) stays the floor: no workstream may block it, and research
  spending (Act IV) waits for the `REVIEW-004` verdict (moonshot §9).

## How to resume (any session)
1. Read this note's **Workstreams** and **Agents** tables, then the task of the row you pick.
2. Check `git log --oneline -10` and CI on main (global rule) before continuing.
3. An agent row marked `running` whose session is gone counts as stopped: inspect its output path,
   record what it left, and set the row to `stopped`.
4. Update both tables and the Log in the same commit as the work they describe.

## Workstreams (first 30 days, moonshot §10)
| # | Pillar | Item | Task | State | Next step |
|---|---|---|---|---|---|
| 1 | M3/§6 | Baseline the scorecard | not opened | pending | Open a BENCH task; light experiment pre-flight (calibration run) |
| 2 | M1 | Kernel-language decision (Slang) | `GRAPHICS-123` (backlog) | spike done, no-go on 2024.13 | Repeat the spike with the pinned port 2026.7.1 (two blocking defects, see Log); then GPU timing under an experiment pre-flight |
| 3 | M2 | Declare once: operation schema | `RUNTIME-326` (done), `RUNTIME-327`, `RUNTIME-328`, `RUNTIME-329` (active), `RUNTIME-330` (active), `GRAPHICS-165` (active), `RUNTIME-331` (active), `RUNTIME-332` (active), `RUNTIME-333` (active), `RUNTIME-334` (active), `RUNTIME-335` (active), `RUNTIME-336` (active), `RUNTIME-337` (active) | slices 3 and 4 pushed and checked live on Vulkan; slice 5 `RUNTIME-330` pushed (Codex approved); slice 6 `GRAPHICS-165` done (Codex approved, Vulkan smokes 10/10); slice 7 `RUNTIME-331` done (Codex approved, Vulkan smokes 10/10, count experiment 2 + 3 = 5) | Slice 1 landed (`7d967065e`): every agent call is validated against its advertised schema. Slice 2 `RUNTIME-327` (`d667f5d12`): the vector-field style as one C++23 table row per field (agent schema/read/list, scene keys, command checks, panel names and bounds); authoring files for a style field 5 → 3. Slice 3 `RUNTIME-328`: one `VectorFieldGlyphStyle` carried from extraction to the draw record, one style check, one width-bound declaration; for a new uniform field the shader draws with, rendering files 7 → 5 (8 → 6 with a range check), total without tests 10–11 → 8–9. Q1's "29 files become 3 or fewer" (tests included) is not met; the remaining step would be generating the shader struct and record from one declaration. Slice 4 `RUNTIME-329`: the lane color style (`VisualizationConfig`, 17 fields) carried once by the editor model and command and declared once in `Extrinsic.Runtime.LaneVisualizationStyle` (scene keys, command check, no-change equality, panel cache signature, panel colormap names); a new scalar-style field: 7 places → 3 (component, row, panel widget), plus the preset policy or the point-set comparator when they need it; it also fixed a stale panel model (the old signature missed the baked flag and highlight values). Operator chose both next steps (2026-10-28, "Do Both please."): slice 5 `RUNTIME-330`, contract tests generated from the field tables (every row: reader, all or nothing, stored value, scene round trip, agent schema for the vector field; refusals leave no undo step), then slice 6 `GRAPHICS-165`: the vector-field draw record declared once (one row file read by C++ and GLSL; C++ struct, GLSL struct and pass-through fill from it; layout guarded by asserting each row type's GLSL scalar size and alignment). A new pass-through drawn float: production files 8 → 7 (`Graphics.VisualizationOverlayUploadHelper.cppm`/`.cpp` out, the row file in; the shader edits only its use), tests unchanged (`Test.VisualizationOverlayPass.cpp` still updates its size literal). Q1 (≤ 3 files, tests included) is still unmet. Slice 7 `RUNTIME-331` (operator: "Bitte slice 7"): one row file declares a pass-through style float for the layer, style table (agent schema, scene key, bounds), glyph style and its check, extraction copy, record and a generic panel slider. Measured with a trial field: production 2 files (row, shader use), tests 3 (the feature's own GPU check, the record-layout pins, the scene golden) = 5 against Q1's 3; all glue is generated or covered by generic tests. Remaining Q1 gap: the two deliberate pins. Slice 8 `RUNTIME-332` (operator: "Ja ok, bitte mach runtime 332"): the record-layout literals dropped (the type guard covers CPU/GPU agreement for the shared rows), the scene golden now checks every golden key, one key per declared field and that the golden still loads. Re-measured: ColorGain trial = two production files plus one feature-test file = 3, so Q1 holds for pass-through vector-field style floats (generic slider, one shader stage); other field kinds, more shader stages or custom controls can need more files. Slice 9 `RUNTIME-333` (operator: "Mach bitte mit M2 weiter"): one predicate for per-arrow style properties shared by the editor model (candidates, agent listing), the command check and extraction's CPU binding; contract test "CPU binding equals offered" over every vertex and edge property (a scene-bound topology name now draws uniform; a GPU front is outside the claim). Operator ("Bitte die drei Punkte nacheinander machen"): next, in order, slice 10 `RUNTIME-334` the GPU-preview binding on the same rule (done: `GeometryVectorFieldStyleFrontKind` for extraction's front binding, the layer status and the command; Codex approved), then slice 11 `RUNTIME-335` the lane style's scalar and color property (done: `FindLaneVisualizationProperty` for the lane panel, show_property and the encoder; measured divergence was topology rows only; Codex approved), then the larger M2 goals (generated panels, generated MCP tools, a smaller `src/runtime/Editor`). Baseline 2026-10-28: 47 agent tools, none generated whole (partly generated: the `vector_field` style, the operation families' `params`, attribute/domain enums); `src/runtime/Editor` 52,077 lines in 133 files (target 25k). First step done: slice 12 `RUNTIME-336`, `lane_style`, an agent tool whose style arguments, reading and checks come from the lane style table (an agent can now set a lane's colormap, range, bins and isolines; Codex approved). Next (operator: "Dann fang mit den Agenten Tools an"): slice 13 `RUNTIME-337`, tool arguments declared once: 36 of 48 tools have a generated input schema, equal to the hand-written baseline (committed test); deferred for scope: capture, diagnostics, device status; not expressible yet: `bind_attribute`, `jobs_wait`, `set_camera`, `property_compare`, `config_preview`/`config_apply`, `run_`/`preview_operation` (Codex approved); then the deferred four and the tools needing richer constraints; later generated panels and Editor size |
| 4 | M4 | Scene graph and provenance prototype | not opened | pending | ADR after M2 slice 1 |
| 5 | M3 | Build bake-off and content-addressed cache | `BUILD-005`/`BUILD-006`, `CI-016` | pending | Needs item 1's baseline first |
| 6 | M5 | Visibility buffer spike, `GRAPHICS-125` evidence | not opened | pending | — |
| 7 | M9 | Arena v0 + engine harness host | not opened | blocked | Waits for the `REVIEW-004` verdict (Act IV) |
| 8 | floor | Framework24 squad | `REVIEW-004` dependencies | pending | — |
| 9 | M11 | Python spike (nanobind, five operations) | not opened | pending | — |
| 10 | M8 | Agent evaluation v0 (50 tasks) | not opened | pending | — |

## Agents
| Agent / session | Workstream | Started | State | Output |
|---|---|---|---|---|
| main session `849ae7d0` | 3 (`RUNTIME-327`) | 2026-10-28 | stopped (login expired during the `RUNTIME-330` plan review) | this tracker, `RUNTIME-326`..`RUNTIME-329`, `RUNTIME-330` plan |
| main session `e4afcc21` | 3 (`RUNTIME-330`, `GRAPHICS-165`, `RUNTIME-331`..`RUNTIME-337`) | 2026-10-28 | running (slice 13) | `RUNTIME-330` (`cdbb3b885`, `8be58c2ec`), `GRAPHICS-165` |
| subagent of `849ae7d0` ("M1 Slang feasibility spike") | 2 (`GRAPHICS-123`) | 2026-10-27 | finished | `tasks/evidence/GRAPHICS-123/spike/FINDINGS.md` + `run_all.sh` (git-ignored, local) |

## Acceptance criteria
- [ ] Every row of the first-30-days list has an owning task or a recorded decision not to do it.
- [ ] The scorecard (moonshot §6) has measured baselines for every row it can measure.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

## Log
- 2026-10-27: Moonshot committed as `docs/architecture/moonshot.md` (`48e152f19`); root hygiene
  forbids a root `MOONSHOT.md`.
- 2026-10-27: Toolchain facts for M1/M2. Clang 23.0.0 (Ubuntu, 2026-05-17) has no C++26
  reflection (`^^int` is a syntax error, `-freflection` is ignored, no `<meta>`), so M2's kill
  criterion applies from the start: no reflection-based generator; reuse the existing
  declarative tables (`Extrinsic.Runtime.ConfigFieldSpec`, RUNTIME-276) and add a schema compiler
  only where tables cannot reach. `slangc` 2024.13 is installed at `/usr/bin/slangc`.
- 2026-10-27: M1 feasibility spike (subagent, no engine build, no Vulkan run, no independent
  review; evidence `tasks/evidence/GRAPHICS-123/spike/`, reproducible with `run_all.sh`). Slang
  2024.13 ports of `kmeans_assign` and `parallel_prefix_scan` validate and keep workgroup sizes and
  push-constant offsets, but two defects block adoption on this version: `precise` is dropped
  (NoContraction 4 → 0 in kmeans; 18 of 42 shaders use `precise`) and every pointer use adds the
  `VariablePointers` capability, which the engine never enables (40 of 42 shaders use buffer
  references); both spot-checked with `spirv-dis` by the main session. `-target cpp` reproduces
  kmeans labels exactly (0/1000 mismatches, negative control 68/1000) but has no atomics or
  subgroup lowering. Slang autodiff matches a double central difference (gradient max rel. error
  2.08e-5, negative control 824). Next: repeat on the vcpkg port 2026.7.1 before any decision.
- 2026-10-27: M2 slice 1 (`RUNTIME-326`) landed: the dispatch validates every agent call against
  the tool's advertised schema (47 tools; before it, none named a bare unknown key with
  `invalid_params`). Codex plan review "revise" → folded in; code review "revise required"
  (regex termination, inexact numbers) → fixed; delta "approve". Next M2 step: slice 2 (row 3).
- 2026-10-27: Main CI was red on `7d967065e`: `DiagnosticsStream.AgentRejectsMalformedFiltersAndBounds`
  expected the diagnostics tools' own `invalid_arguments`, the dispatch now answers the lane-wide
  `invalid_params` (the focused RUNTIME-326 run did not include that suite). Fixed by using
  `invalid_params` in the two diagnostics tools too; full CPU gate 5924/5924 locally.
- 2026-10-27: M2 slice 2 opened as `RUNTIME-327`; Codex plan review "revise", folded in.
- 2026-10-27: `RUNTIME-327` implemented: authoring surface of a vector-field style field 5 → 3 files
  (`GeometryPresentation.cppm` member, `GeometryVectorFieldStyle.cpp` row, `DomainPanels.cpp`
  widget); rendering (up to 7 files) and tests unchanged, so Q1's whole-option target is still open.
  Codex code review "revise required" (scene accepted the agent's length-mode tokens) → fixed.
- 2026-10-28: Session `849ae7d0` stopped on an expired login right after Codex answered the
  `RUNTIME-330` plan review; session `e4afcc21` resumed from that answer (revise, B1-B6), revised
  the plan, got the delta answer and implemented slice 5.
- 2026-10-28: Slice 6 `GRAPHICS-165` done: the vector-field draw record declared once for C++ and
  GLSL; production count for a pass-through drawn float 8 → 7; Codex plan review revise twice
  (count, guard), code review approve; Vulkan vector-field smokes 10/10.
- 2026-10-28: Slice 7 `RUNTIME-331` done: a pass-through vector-field style float is one row in
  `assets/shaders/common/vector_field_style_rows.glsl` plus its shader use (production 2 files);
  measured with a trial field, tests add 3 (the feature's own GPU check, record-layout pins, scene
  golden) = 5 against Q1's 3. Codex plan review revise three times, code review revise then approve.
- 2026-10-28: Slice 8 `RUNTIME-332`: the two test pins follow the rows; Q1 measured at 3 files
  (row, shader use, the feature's own GPU check) for a pass-through vector-field style float.
- 2026-10-28: Slice 9 `RUNTIME-333`: the per-arrow style property rule exists once
  (`FindGeometryVectorFieldStyleProperty`); extraction's CPU binding now refuses topology properties
  like the editor; contract test "CPU binding equals offered". Codex plan revise, review approve.
- 2026-10-28: Slice 10 `RUNTIME-334`: the GPU-preview binding of vector-field style properties asks
  the shared rule (`GeometryVectorFieldStyleFrontKind`); topology rows, unsupported kinds and short
  properties are no longer bound or accepted as pending GPU outputs.
- 2026-10-28: Slice 11 `RUNTIME-335`: one rule for a lane's scalar and color properties
  (`FindLaneVisualizationProperty`); the encoder now refuses topology rows like the panel.
- 2026-10-28: Slice 12 `RUNTIME-336`: `lane_style`, the first agent tool whose style arguments,
  reading and checks come from a declared table (the lane style); the generated lane contract test
  gained the agent-schema leg.
