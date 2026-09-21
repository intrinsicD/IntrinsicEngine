---
id: GRAPHICS-146
theme: B
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog planning; implementation evidence is the diff, tests and matched compile measurements
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-146 — Reuse the compiled graphics command-recording test double

## Goal

Replace three local command-context implementations with the existing compiled
RHI test double, preserving command-order and payload coverage while reducing
code size and avoiding a compile-time regression.

## Evidence and reuse decision

At `7bdffefb3`, `RecordingCommandContext` is duplicated in
`tests/contract/graphics/Test.SurfacePassContracts.cpp:42`,
`Test.LinePointPassContracts.cpp:41` and
`Test.SelectionPassContracts.cpp:44` in the same directory.
These tests already include `tests/support/MockRHI.hpp` for `MockDevice`.
`MockCommandContext` in that header and `MockRHI.cpp` already records pipeline,
index-buffer, push-constant bytes, draw calls and ordered events. It lacks the
indirect argument/count buffer and offset record needed by these local doubles.
Extend that existing recorder minimally and migrate the three consumers.

No new mock base class, adapter hierarchy, event-filtering layer or test DSL is
needed. Leave unrelated specialized contexts and tiny `TinyWorldDesc` helpers
alone. GRAPHICS-105 owns material authority, not this test-only consolidation.
Operator direction on 2026-09-21 authorizes this cleanup outside the standing
Framework24 work-selection priority.

## Acceptance criteria

- [ ] Add one plain indirect-count argument record and separate last-call fields
      for indexed/nonindexed draws, with argument/count buffers, both offsets
      and max draw count, in the existing compiled recorder. No history vector
      or new event kind is required. Preserve
      old counters/events for all current MockCommandContext consumers.
- [ ] Migrate the three tests and delete their local context classes/event types.
      Preserve exact event sequence, missing-state no-draw assertions, index and
      indirect buffers, push-constant byte size/content, frame index, bucket and
      selection fullscreen vertex-count checks. Extra recorded events must be
      understood and asserted, never filtered away merely to keep tests green.
- [ ] Map payload checks to `PushConstantPayloads.back()` and also assert the
      payload count, which explicitly strengthens the existing assertion.
- [ ] Keep the mock implementation compiled once. Add only required plain record
      fields/declarations in its existing header; add no engine dependencies or
      production behavior changes.
- [ ] Exercise the shared recorder's new fields with distinct nonzero handles and
      offsets, retain independent expected payloads, and run all CPU consumers
      of MockRHI through the default gate. This task changes no Vulkan commands;
      CPU mock tests do not establish backend execution evidence.
- [ ] Preserve test registration/labels and update the existing mock synopsis or
      test support documentation only where the exposed recording contract changes.

## Event-stream pre-audit

Source inspection at `7bdffefb3` follows all selected pass `Execute` bodies.
`Graphics.CullingSystem.cpp::RecordOpaqueSurfaceBucket` emits the four surface
commands directly; the line/point/edge/outline pass implementations emit their
commands directly too. None calls Begin/End, scissor, dispatch, barriers or copy
on the test-supplied context. Thus the full mock stream matches existing event
counts; no filtering or weakened expectation is needed.

| Selected test / successful invocation | Complete ordered events |
| --- | --- |
| Surface: DepthPrepassRecordsSurfaceOpaqueIndirectDraw | BindPipeline, BindIndexBuffer, PushConstants, DrawIndexedIndirectCount |
| Surface: ForwardSurfaceRequiresInitializedSystemAndRecordsSurfaceOpaqueDraw | BindPipeline, BindIndexBuffer, PushConstants, DrawIndexedIndirectCount |
| Surface: DeferredGBufferRequiresInitializedSystemAndRecordsSurfaceOpaqueDraw | BindPipeline, BindIndexBuffer, PushConstants, DrawIndexedIndirectCount |
| LinePoint: LinePassSkipsInvalidStateAndDrawsLineBucket | BindPipeline, PushConstants, DrawIndirectCount |
| LinePoint: PointPassSkipsInvalidStateAndDrawsPointBucket | BindPipeline, PushConstants, DrawIndirectCount |
| Selection: EntityAndFaceIdPassesDrawSurfaceBucket (both invocations) | BindPipeline, BindIndexBuffer, PushConstants, DrawIndexedIndirectCount |
| Selection: EdgeAndPointIdPassesDrawLineAndPointBuckets (edge) | BindPipeline, BindIndexBuffer, PushConstants, DrawIndexedIndirectCount |
| Selection: EdgeAndPointIdPassesDrawLineAndPointBuckets (point) | BindPipeline, PushConstants, DrawIndirectCount |
| Selection: SelectionOutlinePassDrawsFullscreenTriangle | BindPipeline, PushConstants, Draw |
| Every existing invalid-pipeline/uninitialized/invalid-bucket invocation | Empty |

`Surface`, `LinePoint`, and `Selection` abbreviate the three `Graphics...PassContracts`
suites. All ten successful invocations carry one push-constant payload. Recheck
this frozen table against current source at implementation time; any difference
becomes an explicit, reviewed change to test expectations. The minimum payload
count assertion and previously partial selection event assertions can be made
complete in this task; record those test-strengthening edits explicitly.

## Size and compilation acceptance

- [ ] Record before/after physical lines for the complete affected implementation,
      helper, declaration, caller and CMake set, including newly added files.
      Require a net reduction; report added regression tests separately and also
      report the total diff. Moving bodies or compressing formatting is insufficient.
- [ ] Reuse `tools/analysis/benchmark_compile_iteration.py` with a task-specific
      manifest `benchmarks/ci/manifests/graphics146_reuse_compile.yaml`, stable ID
      `build.reuse.graphics146.v1`, exact clean before/after revisions, identical
      dependencies, Clang >=20, ci-derived Null/headless preset, disabled ccache,
      fixed jobs and at least three alternating samples per arm. Use the focused
      IntrinsicTests target with tests enabled, including MockRHI consumers
      outside the focused graphics target. Measure clean,
      no-op and the following edit probes: a migrated test implementation, MockRHI.cpp and MockRHI.hpp; measure IntrinsicTests so the shared-header probe covers all mock consumers.
      Record wall time, compiler-duration sum and observed rebuild fan-out.
      Stop if the clean-build median regresses by more than 2%; for no-op
      and edit probes allow at most max(2% of the before median, 100 ms).
      Apply the same relative limit to compiler-duration sums (zero-work
      no-op must stay zero); do not relax limits after viewing results. Preserve all raw runs; inconclusive evidence
      keeps this task open. On a confirmed gate failure, restore only this
      task's implementation changes, retain the raw runs and a negative decision
      record, and retire explicitly as rejected, not implemented. Do not discard
      unrelated work. Do not infer compile speed from line count.
- [ ] Run focused tests and the default CPU gate; preserve test names/labels and
      existing assertions. Validate the measured manifest/results. Any repeatable
      performance claim follows AGENTS.md §8/§8b; this task asserts no speedup.

## Verification

The compile manifest and disposable source worktree are implementation deliverables;
create the worktree from the measured revision before invoking the runner. Keep
compiler timing separate from concurrent builds/tests.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsSurfacePassContracts|GraphicsLinePointPassContracts|GraphicsSelectionPassContracts|GraphicsTestSupport' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/analysis/benchmark_compile_iteration.py --source /tmp/intrinsic-graphics146/source --build /tmp/intrinsic-graphics146/build --output /tmp/intrinsic-graphics146/results --manifest benchmarks/ci/manifests/graphics146_reuse_compile.yaml
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic-graphics146/results/results --manifests-root benchmarks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Planning review

Codex and Claude Code reached consensus on 2026-09-21 before this task was
filed. The [shared planning review](../../evidence/GEOM-099/claude-planning-review.md)
records the agreed scope, corrections, rejected job-lifecycle candidate and
compile-regression stop rules. Implementation and timing evidence remain due.
