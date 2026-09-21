---
id: GEOIO-004
theme: E
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive rejected consolidation; exact candidate sources, review, tests and negative compile measurements are retained
contract_schema: 1
contracts: [repo.source-documentation, io.geometry-format-capabilities]
---
# GEOIO-004 — Rejected: shared compiled PLY header parser


## Completion — 2026-09-22: rejected

Rejected candidate commit: `478e32e0a27b9f7569710d8ddd0f503c5840d60a` passed independent
Claude Sonnet review and focused behavioral checks, but fails the frozen
private-header compiler-work allowance: 4.967 → 6.174 seconds (+24.30%).
All three candidate samples exceed the entire baseline range plus 2%; no
threshold was changed. The [negative decision and raw evidence](../evidence/GEOIO-004/measurements.md)
retain the six matched samples, exact source commits and rejected patch.

All task source, CMake, owner-route and regression-test changes are restored.
The restored canonical ci IO build and all 217 original IO cases pass.
The resulting `src`, `tests` and `cmake` trees match
`a4509e0e115c9bca7c6d6fe0f23e7460bb3b1b50`; retained code delta is zero.
The attempted consolidation was -74 implementation lines, +150 regression-test
lines, +76 total. No replacement implementation is part of this retirement.

The candidate passed 224 GeometryIO cases before/after, the expanded 319-case
selection and all 3,237 ASan entries. Full ci and UBSan each failed only the
baseline-reproduced [BUG-206](../backlog/bugs/BUG-206-uv-duplicate-submit-phase-race.md).
That independent gate defect remains owned by BUG-206 and is not represented
as fixed by this rejected experiment. The task is retired as **rejected**, not
implemented; no new geometry capability or maturity claim is made.

## Goal

Remove the two PLY header loops while preserving mesh and point-cloud import
behavior, reducing maintained code without increasing compile time.

## Evidence and reuse decision

At `7bdffefb3`, `LoadPLY` in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`
(lines 1688–1820) and `src/geometry/Geometry.PointCloud.IO.cpp`
(lines 1731–1863) independently tokenize format, element and property records,
track header completion and calculate the body offset. `Geometry.IOText.hpp`
already owns the shared lexical helpers and PLY records from RORG-134; it does
not own these loops. Extend that private owner with a declaration and an
ordinary non-module `Geometry.IOText.cpp` implementation in the geometry target.
`cmake/IntrinsicModule.cmake` uses ordinary `add_library` plus module scanning;
`src/geometry/CMakeLists.txt` already privately compiles the non-module
`CurvatureBoundaryGraph.cpp`, so this route requires no new library or module. No parser framework,
public loader unification or large inline/template helper is needed.

The contracts differ: the mesh header rejects nonintegral list-count types;
the cloud header currently accepts their spelling and later body validation
has its own rules. Share lexical parsing, retain explicit caller validation
and each public error type. Use an optional `PlyHeader` containing format,
elements and body cursor, with no policy parameter/callback. The mesh validates
list-count integrality after lexical parsing. Both old failures return the same
invalid-mesh error, including a nonintegral list header that is also truncated,
so postponing that check changes no returned diagnostic. This is a bounded follow-up to completed RORG-134,
not a reopening of its scalar-decoding work.

Operator direction on 2026-09-21 explicitly requests duplication cleanup outside
the standing Framework24 work-selection priority. No new format or capability
is proposed.

## Acceptance criteria

These criteria record the evaluated candidate and the completed stop-rule
procedure. The candidate was rejected and removed; checked items do not claim
that a shared parser remains in the source tree. Its original implementation,
characterizations and review remain available in the source bundle.

- [x] Both public `LoadPLY` functions call one compiled header parser and the
      duplicated header loops are deleted in the same change. Reuse existing
      `PlyFormat`, `PlyElement`, `PlyProperty` and text helpers. Move the two
      equivalent-for-parsed-values `IsPlyIntegralScalar` definitions to the private header as a
      small constexpr predicate; keep the header parser body out of the header.
- [x] Preserve byte-accurate body offsets, comments/obj_info, unknown directives,
      token arity, repeated format/element/property handling, scalar aliases,
      malformed/truncated header rejection and mesh/cloud diagnostic differences.
      Preserve nonintegral list-count acceptance/rejection for each caller;
      characterize it before extraction rather than silently tightening parsing.
- [x] Keep ASCII/binary body parsing, endian decoding, unknown-element skipping,
      negative/overflow/truncated list checks, mesh topology, point-cloud
      publication, UV/alpha policies and all exporters at their current owners.
- [x] Exercise both loaders with the same shared header cases through
      `tests/unit/geometry/Test.GeometryIO.cpp`; keep mesh-only and cloud-only
      fixtures and expected errors explicit. Existing
      `GeometryIO_MeshIO.LoadPLYRejectsNonIntegralListCountType` and
      `GeometryIO_PointCloudIO.BinaryPLYPointCloudRejectsFloatingListCountType`
      cover rejection; add/confirm the ASCII cloud acceptance and simultaneous
      nonintegral/truncated header cases before refactoring. Retain asset routing tests.
- [x] Keep the compiled helper private to geometry, with no new exported module
      dependency or asset/runtime imports. Update the existing geometry IO owner
      route and synopsis only as needed; regenerate skill mirrors if its canonical
      `docs/agent/` source changes.

## Size and compilation acceptance

- [x] Record before/after physical lines for the complete affected implementation,
      helper, declaration, caller and CMake set, including newly added files.
      Require a net reduction; report added regression tests separately and also
      report the total diff. Moving bodies or compressing formatting is insufficient.
- [x] Reuse `tools/analysis/benchmark_compile_iteration.py` with a task-specific
      manifest `benchmarks/ci/manifests/geoio004_reuse_compile.yaml`, stable ID
      `build.reuse.geoio004.v1`, exact clean before/after revisions, identical
      dependencies, Clang >=20, ci-derived Null/headless preset, disabled ccache,
      fixed jobs and at least three alternating samples per arm. Use the focused
      target below with tests enabled. Measure clean,
      no-op and the following edit probes: each loader implementation and the shared private declaration header.
      Record wall time, compiler-duration sum and observed rebuild fan-out.
      Stop if the clean-build median regresses by more than 2%; for no-op
      and edit probes allow at most max(2% of the before median, 100 ms).
      Apply the same relative limit to compiler-duration sums (zero-work
      no-op must stay zero); do not relax limits after viewing results. Preserve all raw runs; inconclusive evidence
      keeps this task open. On a confirmed gate failure, restore only this
      task's implementation changes, retain the raw runs and a negative decision
      record, and retire explicitly as rejected, not implemented. Do not discard
      unrelated work. Do not infer compile speed from line count.
- [x] Run focused tests and the default CPU gate; preserve test names/labels and
      existing assertions. Validate the measured manifest/results. Any repeatable
      performance claim follows AGENTS.md §8/§8b; this task asserts no speedup.

## Verification

The compile manifest and disposable source worktree are implementation deliverables;
create the worktree from the measured revision before invoking the runner. Keep
compiler timing separate from concurrent builds/tests.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryIoTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/analysis/benchmark_compile_iteration.py --source /tmp/intrinsic-geoio004/source --build /tmp/intrinsic-geoio004/build --output /tmp/intrinsic-geoio004/results --manifest benchmarks/ci/manifests/geoio004_reuse_compile.yaml
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic-geoio004/results/results --manifests-root benchmarks --strict
python3 tools/agents/sync_skills.py --write
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Planning review

Codex and Claude Code reached consensus on 2026-09-21 before this task was
filed. The [shared planning review](../evidence/GEOM-099/claude-planning-review.md)
records the agreed scope, corrections, rejected job-lifecycle candidate and
compile-regression stop rules. Implementation and timing evidence remain due.

## Execution plan — 2026-09-21

Current source confirms two header loops in the mesh and point-cloud LoadPLY
implementations. Move their lexical grammar into a single compiled
`Geometry.IOText.cpp` function declared in the existing private IOText header.
Return only format, ordered element/property records and the exact body byte
offset. Both public APIs retain their current error mapping and body parsers;
mesh alone validates every list-count type as integral after parsing.

The two integral-scalar predicates are equivalent for all parsed enum values,
but their spelling differs: mesh negates the floating predicate; cloud lists
six integral kinds. Keep the existing cloud constexpr form as the shared small
predicate. Do not change binary reads, attributes, alpha, UVs, exporters or
asset routing. The new ordinary compilation unit belongs to the existing
geometry library; no new module, target, abstraction or configuration axis.

Add seven public-loader characterizations before extraction, covering all
scalar aliases, CRLF/whitespace, comments and unknown directives, repeated
format/property/element records, malformed/truncated headers, the ASCII cloud
floating-count exception, and both binary body offsets. Run them on the original
loaders before freezing the before-source identity. Existing body/attribute and
asset-routing tests remain intact. Review the fixed extraction with Claude
Sonnet at medium effort, then run full CPU and sanitizer gates.

The task's focused producer was corrected before baseline measurement:
`Test.GeometryIO.cpp` is owned by `GeometryIoTestObjs` and linked into
`IntrinsicGeometryIoTests`, not `IntrinsicGeometryTests`. Use that actual target
for the focused build and matched compile manifest. This corrects coverage;
all original compile allowances remain unchanged. Freeze Clang 23, ci-derived
Debug Null/headless, four jobs, disabled ccache, exact source commits and
identical dependencies before extraction. Preserve the pilot and at least
three alternating matched samples per arm.
