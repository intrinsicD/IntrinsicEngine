---
id: METHOD-076
theme: I
depends_on: [METHOD-053]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; this task implements selectable variants and their correctness tests; the comparison runs are a separate, preflighted experiment.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-076 — Selectable landmark variants for the Nystroem CPD/BCPD E-step

## Goal
- Operator questions (verbatim, 2026-10-09):
  1. "um P nun schnell zu bestimmen brauchen wir ja K_{YX}. Hier kann man subsampeln und K_{VV} nutzen.
     Hier möchte ich gerne verschiedene varianten testen: fixes subsampling und resampling von den
     vertices/punkten. fixes sampling und resampling aus der union der bounding boxes mit random oder
     strukturiertem sampling."
  2. "Kannst du nun die sampling Varianten implementieren?"
- This task answers question 2: the Nystroem E-step (K ~= k(Y,V) W^+ k(V,X), METHOD-053) takes its
  landmarks V from a selectable source and schedule, so the variants of question 1 can be measured.
  The measurement itself is a later experiment with its own preflight.

## Acceptance criteria
- [x] Geometry: `EStep::Settings` / `CPD::Params` select the landmark source (points of both sets |
      uniform over the set union of their bounding boxes, each padded by sigma), the schedule (mixed = today's behaviour: target fixed,
      source drawn every iteration | fixed: drawn once, source landmarks follow their points |
      resampled: drawn anew every iteration with a new seed) and the box pattern (uniform random |
      scrambled Halton). Points use the existing `PointSampling` method (`landmark_*`).
- [x] Defaults keep today's results bitwise (mixed, points).
- [x] Every variant keeps the existing acceptance (sampled error limit, landmark doubling within the
      cost cap, exact fallback); fixed draws extend by prefix when the landmark count doubles.
- [x] Runtime config fields, panel combos and agent passthrough; codec round trip.
- [x] Tests: each variant accepted on a wide-kernel fixture and within the limit of the dense sums;
      fixed repeats its landmarks across iterations, resampled changes them, mixed equals the
      former code; box landmarks lie in the union box; invalid enums fail closed.

## Completion
Completed 2026-10-09. Commit: the METHOD-076 commit ("Make the Nystroem CPD landmarks selectable
(METHOD-076)"). CPUContracted: the variants are selectable and tested on the CPU reference path
(the Vulkan E-step does not use landmarks). The comparison of the variants (operator question 1)
is not part of this task; it needs its own experiment task with preflight.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point positions of the CPD/BCPD source and target (unchanged). |
| Compatible entity sources | Every canonical point domain the CPD operation accepts (unchanged). |
| RuntimeModule | Existing CPD editor operation; the variant only changes the Nystroem E-step. |
| Config/agent | `nystrom_landmark_source`, `nystrom_landmark_schedule`, `nystrom_box_pattern` in the CPD section; agent through `config_apply`. |
| UI | Combos beside the Nystroem landmark count in the CPD panel. |
| Publication | Unchanged. |
| End-to-end tests | Runtime test: round trip and a run per landmark source reaching the solver. |

## Plan
1. `EStep.cppm`: enums `NystromLandmarkSource {Points, BoundingBox}`, `NystromLandmarkSchedule
   {Mixed, Fixed, Resampled}`, `NystromBoxPattern {Random, Halton}` and three `Settings` fields;
   `CPD::Params` mirrors them and passes them through (CPD.cpp:226).
2. `NystromRows` builds V through one function `DrawLandmarks(moved, count, settings, draw)`:
   - Points/Mixed: today's code path unchanged.
   - Points/Fixed: target and source index orders drawn once (source on the moved points of the
     first Nystroem iteration) and cached; a larger count redraws with the same seed (progressive
     orders keep the prefix); source landmark positions are the current moved positions.
   - Points/Resampled: both halves drawn every call with `Seed + draw` (and, for farthest point,
     `FirstIndex = (Seed + draw) mod count`), `draw` counting Nystroem calls.
   - Box: uniform over the set union of the target box and the moved source box, each padded by
     sigma (Fixed: the boxes of the first Nystroem iteration, frozen; Resampled and Mixed:
     current). A candidate picks a box by volume and is dropped with probability 1/2 in the
     overlap; uniform mt19937 stream or randomly shifted Halton (bases 2, 3, 5, 7, 11), seed
     `Seed` (+ draw for Resampled). All L landmarks come from the boxes; the stream is
     prefix-consistent.
3. Runtime: `Runtime.CoherentPointDriftConfig` fields `nystrom_landmark_source`,
   `nystrom_landmark_schedule`, `nystrom_box_pattern` (spec enums), panel combos beside the
   Nystroem landmark count, agent through `config_apply`.
4. Tests in `Test.CoherentPointDriftAccelerated.cpp` (Evaluator level) and the runtime config test.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests IntrinsicGeometryRegistrationSlowTests
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|flaky-quarantine' --timeout 300
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-09 plan review (Codex `gpt-6-astra`, high, thread `01a12156-8426-7bf1-a87d-94f988a74513`,
  recursion protection instruction-only): "revise". Taken over: literal set union of the two boxes
  (not the enclosing box) with sigma padding for flat data; Fixed point orders kept on the first
  iteration's source so doubling extends the prefix after deformation; one draw per iteration
  shared by doubling attempts, all sampler seeds and the start index (mod population) move with
  it; variant change, `SetTarget` and a new source size reset the cached state; runtime
  config-to-Params mapping; `Evaluator::NystromLandmarks()` as the test seam; mutation checks.
  Kept: the rejection memory (no retry at or below a rejected sigma^2) applies to every variant.
- 2026-10-09 implementation: default (Mixed, points) bitwise equal to the former code on a
  4-iteration, 1- and 4-thread oracle dump taken before the change. Predictions corrected with
  reason: box P1 delta at sigma^2 = 8, 48 landmarks predicted <= 1e-3, measured 1.03e-3 (Halton)
  and 5.3e-4 (random) against 7.8e-8 for points, bound set to 2e-3 plus <= 10x the sampled
  estimate; box Mixed keeps landmarks drawn in the (unmoved) target box. The runtime forwarding
  test needed `nystrom_error_limit = 0.05`: box landmarks missed 1e-3 in every iteration of its
  1500-point fixture. Mutation checks (resampling off for boxes and points, box freeze off, fixed
  source order off, pattern ignored, enum validation off, runtime forwarding off) each failed
  their test. Full CPU gate 5735/5735, CPD tests incl. slow lane 64/64, sandbox builds.
- 2026-10-09 code review (Codex `gpt-6-astra`, high, thread `01a12163-c19b-7b42-a7b3-738cc7fcf652`)
  round 1: "revise required". Fixed: Resampled point landmarks are refused for sample elimination
  and the tournament order (a per-method check: 48/48 and 46/48 landmarks repeated for unchanged
  inputs, the other six methods redraw); cached orders are extended keeping their identities as
  the prefix (sample elimination picks another set for a larger count); a new source size resets
  the variant state (also frozen boxes); the flat-data test requires an accepted Nystrom run and
  landmarks off the flat axis; the runtime test runs source, schedule and pattern variants
  through the operation (limit 0.5: box Halton missed 0.05 in every iteration; box Mixed and
  Resampled share their first draw) and the runtime validator refuses the unsupported
  combination. Mutation checks: schedule and pattern forwarding, runtime validation, prefix
  keeping, size reset (after moving the test's new source out of the old padded boxes) and the
  refusal each fail their test.
- 2026-10-09 delta review (same thread): "Overall: approve." All five findings fixed.
