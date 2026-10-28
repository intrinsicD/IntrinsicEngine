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
| 3 | M2 | Declare once: operation schema | `RUNTIME-326` (done), `RUNTIME-327` (active) | slice 2 done and pushed (CPUContracted, Codex approved) | Slice 1 landed (`7d967065e`): every agent call is validated against its advertised schema. Slice 2 `RUNTIME-327`: the vector-field style as one C++23 table row per field (agent schema/read/list, scene keys, command checks, panel names and bounds). Count for a style field that changes drawing: authoring files 5 → 3, rendering unchanged (up to 7) plus tests, so Q1's "29 files become 3 or fewer" is not met by this slice. Next M2 step: the rendering half (extraction's request copy, packet inputs) or the next recipe |
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
| main session `849ae7d0` | 3 (`RUNTIME-327`) | 2026-10-28 | paused after slice 2 (pushed); next M2 step not started | this tracker, `RUNTIME-326`, `RUNTIME-327` |
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
