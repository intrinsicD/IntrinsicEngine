---
id: GEOIO-004
theme: E
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog planning; implementation evidence is the diff, tests and matched compile measurements
contract_schema: 1
contracts: [repo.source-documentation, io.geometry-format-capabilities]
---
# GEOIO-004 — Share the compiled PLY header parser

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

- [ ] Both public `LoadPLY` functions call one compiled header parser and the
      duplicated header loops are deleted in the same change. Reuse existing
      `PlyFormat`, `PlyElement`, `PlyProperty` and text helpers. Move the two
      identical `IsPlyIntegralScalar` definitions to the private header as a
      small constexpr predicate; keep the header parser body out of the header.
- [ ] Preserve byte-accurate body offsets, comments/obj_info, unknown directives,
      token arity, repeated format/element/property handling, scalar aliases,
      malformed/truncated header rejection and mesh/cloud diagnostic differences.
      Preserve nonintegral list-count acceptance/rejection for each caller;
      characterize it before extraction rather than silently tightening parsing.
- [ ] Keep ASCII/binary body parsing, endian decoding, unknown-element skipping,
      negative/overflow/truncated list checks, mesh topology, point-cloud
      publication, UV/alpha policies and all exporters at their current owners.
- [ ] Exercise both loaders with the same shared header cases through
      `tests/unit/geometry/Test.GeometryIO.cpp`; keep mesh-only and cloud-only
      fixtures and expected errors explicit. Existing
      `GeometryIO_MeshIO.LoadPLYRejectsNonIntegralListCountType` and
      `GeometryIO_PointCloudIO.BinaryPLYPointCloudRejectsFloatingListCountType`
      cover rejection; add/confirm the ASCII cloud acceptance and simultaneous
      nonintegral/truncated header cases before refactoring. Retain asset routing tests.
- [ ] Keep the compiled helper private to geometry, with no new exported module
      dependency or asset/runtime imports. Update the existing geometry IO owner
      route and synopsis only as needed; regenerate skill mirrors if its canonical
      `docs/agent/` source changes.

## Size and compilation acceptance

- [ ] Record before/after physical lines for the complete affected implementation,
      helper, declaration, caller and CMake set, including newly added files.
      Require a net reduction; report added regression tests separately and also
      report the total diff. Moving bodies or compressing formatting is insufficient.
- [ ] Reuse `tools/analysis/benchmark_compile_iteration.py` with a task-specific
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
- [ ] Run focused tests and the default CPU gate; preserve test names/labels and
      existing assertions. Validate the measured manifest/results. Any repeatable
      performance claim follows AGENTS.md §8/§8b; this task asserts no speedup.

## Verification

The compile manifest and disposable source worktree are implementation deliverables;
create the worktree from the measured revision before invoking the runner. Keep
compiler timing separate from concurrent builds/tests.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
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
filed. The [shared planning review](../../evidence/GEOM-099/claude-planning-review.md)
records the agreed scope, corrections, rejected job-lifecycle candidate and
compile-regression stop rules. Implementation and timing evidence remain due.
