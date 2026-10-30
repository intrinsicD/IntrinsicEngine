# Density weights on the operation runtime: footprint before and after (RUNTIME-353)

Operator question 31 (2026-10-30): "do 1." — after the phase 1 gate's size miss
([report](2026-10-30-phase-1-gate-footprint.md)), move compact density weights, an unfactored near copy
of the point scalar lifecycle, onto the operation runtime, to measure the saving where the duplication
is real.

This is a static line count (no timing or memory run). Before is commit `f4e369d9b`; after is the
RUNTIME-353 change. Every message, status, delivery rule and computed value of density weights is
kept. One measured quantity moved: `CpuComputeMilliseconds` now also covers formatting the success
message and freeing the CPU LBVH radius rows. The tests and mutations are listed in
`tasks/active/RUNTIME-353-density-weights-on-the-operation-runtime.md`.

## Result

| | before (`f4e369d9b`) | after |
|---|---:|---:|
| operation-specific lines (tests apart) | 883 | 701 |
| files with operation-specific lines (+ test file + `tests/CMakeLists.txt`) | 12 (+2) = 14 | 11 (+2) = 13 |
| operation runtime (`Runtime.OperationRuntime.cppm`, framework) | 518 | 632 |

- The operation's own footprint fell by 182 lines (21 %) and one file.
- The runtime grew by 114 lines to take the operation's divergences as optional definition members:
  - wording;
  - reference backend and minimum live count;
  - a capture step (`Prepare`);
  - radius rows;
  - a two-argument `GpuParams`;
  - the inline-delivery and finalize flags;
  - an in-place refusal.

  Kernel density and point spacing are unchanged.
- Net for this slice: 68 fewer lines (footprint plus framework). Over `src`, `tests` and `docs`: 631 lines added and 615 removed in 16 files.
- The first proxy (`dw_footprint.py`, 859 lines before) counted only identifier-bearing lines of the
  result struct and the config wrappers, and none of the panel's registration block. The plan review
  (B4) asked for whole blocks on both sides; 883 is that count.

Per file:

| file | before | after |
|---|---:|---:|
| `Editor/Operations/Runtime.PointAnalysisOperations.DensityWeights.cpp` (removed) | 453 | – |
| `Modules/DensityWeight/Runtime.DensityWeightOperation.cpp` (new) | – | 244 |
| `Modules/DensityWeight/Runtime.DensityWeightOperation.cppm` (new) | – | 73 |
| `Modules/DensityWeight/Runtime.DensityWeightConfig.cppm` | 38 | 38 |
| `Config/internal/Runtime.PointNeighborhoodConfigCodecs.cpp` (hand-written codec, kept) | 178 | 178 |
| `app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp` (window and registration) | 153 | 154 |
| `Editor/Operations/Runtime.PointAnalysisOperations.cppm` (result struct, slot, sink, declarations) | 33 | 1 |
| `Editor/Operations/Runtime.PointAnalysisOperations.cpp` (config wrappers) | 14 | – |
| `Editor/internal/Runtime.EditorWorkspaceSession.cpp` (sink, dismiss case) | 3 | – |
| `Agent/Runtime.AgentOperations.Operations.cpp` | 4 | 4 |
| `runtime/CMakeLists.txt` | 2 | 3 |
| `app/Sandbox/Sandbox.ConfigSections.cpp` | 2 | 2 |
| generated (`module_inventory.md`, agent tool schemas) | 3 | 4 |

The new operation unit, by section:

| section | lines |
|---|---:|
| module header, includes, imports | 31 |
| constants and the definition's declarations | 22 |
| wording (nine kept texts) | 12 |
| output, request, refusal, validation, GPU parameters, GPU statistics | 37 |
| support radius resolution | 41 |
| capture step | 13 |
| `Compute` | 35 |
| entry points and config wrappers | 53 |

## Reading

- Where the lifecycle was really duplicated, the runtime removes it: 500 lines collapse to 317 lines of
  definition and interface. Those 500 lines are the editor copy (453), its result struct and
  declarations (33) and its wrappers (14).
- The rest of the footprint does not shrink with the runtime:
  - the hand-written codec (178);
  - the hand-written panel (154);
  - the module and entry-point boilerplate (about 85 lines in the unit, 73 in the interface).
- With 701 lines, density weights stay far above ADR 0031's phase 1 target of 150 lines per operation.
- Each divergence kept for behaviour preservation costs lines in the definition and the runtime. Normalizing them (for example, one wording scheme, or delivery to the sink on the inline path as for the pilot) would be a behaviour change and is the operator's decision.

## Method

`tasks/evidence/RUNTIME-353/dw_footprint2.py` (local; its rules are reproduced here) is run as
`python3 dw_footprint2.py f4e369d9b` for before and without arguments for after.

**Files.** Every file under `src`, `docs/api/generated` and `tests/data/agent_declared_tool_schemas.json` with a line matching
`DensityWeight|density_weight|DrawDensityWeightsWindow|WeightsState|WeightTransaction|weights_progress`.

**Excluded:**

- `src/geometry` and `src/graphics` (method code);
- hand-written documentation;
- the consolidation module (its uses of the density kernels are not this operation).

**Counted whole:**

- own files: the removed editor file and `Modules/DensityWeight/*`;
- in shared files, these blocks:
  - the codec's field rows and its density weight namespace block;
  - the panel window and its `RegisterWindow("view.density_weights", …)` statement;
  - the `EditorDensityWeightResult` struct;
  - the definitions of `ApplyEditorDensityWeightConfig` and `GetEditorDensityWeightConfig`.

**Counted once per line:** every other line matching the pattern. Tests are not counted.

**Framework size:** `Runtime.OperationRuntime.cppm` line count at both revisions.
