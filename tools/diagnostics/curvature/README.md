# Curvature corpus differential

This directory contains local-development diagnostics for comparing
`Geometry.Curvature` with actual Framework24 and explicitly supplied PMP
checkouts on OBJ corpora. The shipped implementation targets Framework24
`CurvatureTaubin(mesh, 0, false, Policy::Sequential)` directly; PMP is a useful
different estimator, not the parity oracle. These harnesses are not registered
in CTest, do not download reference code or datasets, and do not produce
claim-eligible benchmark results.

## Framework24 parity probe

`Framework24CurvatureParityProbe.cpp` invokes the actual reference with
`CurvatureTaubin(mesh, 0, false, Policy::Sequential)` and prints positions,
principal scalars, and both directions in vertex order. Compile it against an
existing Framework24 build; for the repository's usual checkout layout:

```bash
FRAMEWORK24_REFERENCE_ROOT=/path/to/framework24
FRAMEWORK24_REFERENCE_BUILD="$FRAMEWORK24_REFERENCE_ROOT/cmake-build-debug"
/usr/bin/clang++ -O2 -DNDEBUG -std=c++17 \
  -I"$FRAMEWORK24_REFERENCE_ROOT/lib_bcg_framework/include" \
  -I"$FRAMEWORK24_REFERENCE_ROOT/lib_bcg_framework/src/cuda/include" \
  -isystem "$FRAMEWORK24_REFERENCE_ROOT/lib_bcg_framework/ext/eigen" \
  -isystem "$FRAMEWORK24_REFERENCE_ROOT/lib_bcg_framework/ext/EigenRand" \
  -isystem "$FRAMEWORK24_REFERENCE_ROOT/lib_bcg_framework/ext/happly" \
  -isystem "$FRAMEWORK24_REFERENCE_ROOT/lib_bcg_framework/ext/nanoflann/include" \
  -isystem "$FRAMEWORK24_REFERENCE_ROOT/lib_bcg_framework/ext/spectra/include" \
  tools/diagnostics/curvature/Framework24CurvatureParityProbe.cpp \
  "$FRAMEWORK24_REFERENCE_BUILD/lib_bcg_framework/liblib_bcg_framework.a" \
  -ltbb -lpthread -o /tmp/Framework24CurvatureParityProbe
/tmp/Framework24CurvatureParityProbe tests/data/sculpt.obj \
  > /tmp/framework24-sculpt.txt
```

Framework24 `MeshIo` centers coordinates and divides them by maximum AABB
extent before this call. The committed parity fixtures and `sculpt.obj` all
have maximum extent one, so translation is the only loader change and does not
affect curvature. For another mesh, normalize the input supplied to both probes
or explicitly convert the inverse-length units before comparing.

The explicit sequential policy makes the diagnostic invocation stable even
though the current zero-step default performs no scalar smoothing.
When comparing directions, map Framework24 `min_direction` to IntrinsicEngine
`v:principal_dir2` and `max_direction` to `v:principal_dir1`; those are the
existing public minimum/maximum direction names in the engine. Compare them as
unoriented lines because an eigenvector and its negation are equivalent.

## PMP comparison probe

Enable and build the Intrinsic probe in a Release preset tree:

```bash
cmake --preset ci-release -DINTRINSIC_BUILD_DIAGNOSTIC_TOOLS=ON
cmake --build build/ci-release --target IntrinsicCurvatureCorpusProbe
```

Build PMP separately and compile `PmpCurvatureCorpusProbe.cpp` against that
exact checkout. A compiler-matched local reference build is:

```bash
PMP_REFERENCE_ROOT=/home/alex/Documents/Engine24/ext/pmp-library
PMP_REFERENCE_BUILD=/tmp/bug154-pmp-clang
cmake -S "$PMP_REFERENCE_ROOT" -B "$PMP_REFERENCE_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/bin/clang++-23 \
  -DPMP_BUILD_DOCS=OFF \
  -DPMP_BUILD_EXAMPLES=OFF \
  -DPMP_BUILD_REGRESSIONS=OFF \
  -DPMP_BUILD_TESTS=OFF \
  -DPMP_BUILD_VIS=OFF
cmake --build "$PMP_REFERENCE_BUILD" -j 2
/bin/clang++-23 -O3 -DNDEBUG -std=c++20 \
  -I"$PMP_REFERENCE_ROOT/src" \
  -I"$PMP_REFERENCE_ROOT/external/eigen-3.4.0" \
  tools/diagnostics/curvature/PmpCurvatureCorpusProbe.cpp \
  "$PMP_REFERENCE_BUILD/libpmp.a" \
  -o /tmp/PmpCurvatureCorpusProbe-clang23
```

Keep the compiler, optimization mode, PMP revision, and compile command with
any timing interpretation; scalar differential results depend on the PMP
revision, while low-repetition timings are only diagnostic.

The PMP probe defaults to its one-ring hinge support with no post-smoothing
(`smoothing_steps = 0`, `two_ring = 0`). Pass `... <repetitions> 3 1` to select
PMP's own two-ring/three-pass path. PMP is not the declared Framework24 oracle:
its variants retain distinct boundary and value/direction-pairing behavior, and
the two-ring variant also changes support and smoothing. The corrected probe
argument order is `(smoothing_steps, use_tensor=true, use_two_ring)`.

For the historical two-ring comparison, see
[`curvature_framework24_parity_2026-08-12.md`](../../../ara/evidence/tables/curvature_framework24_parity_2026-08-12.md).
The current-revision default comparison is recorded in
[`curvature_framework24_current_parity_2026-09-03.md`](../../../ara/evidence/tables/curvature_framework24_current_parity_2026-09-03.md).

Run a deterministic, size-stratified sample:

```bash
python3 tools/diagnostics/curvature/compare_curvature_corpus.py \
  --dataset-root /path/to/Dataset/obj \
  --intrinsic build/ci-release/bin/IntrinsicCurvatureCorpusProbe \
  --pmp /path/to/PmpCurvatureCorpusProbe \
  --output /tmp/curvature-corpus.json \
  --max-models 96 \
  --max-source-bytes 50000000
```

The script normalizes both inputs to the same position-only triangle OBJ before
running either probe. It records full-field scalar differentials, realized-mesh
direction invariants, and a quality-uncontaminated subset more than four
adjacency rings from a triangle at or below the production quality threshold.
The subset does not replace the full result: it separates reference parity on
well-conditioned support from deliberate Intrinsic fail-closed zeros where PMP
can emit non-finite or extreme values. Source OBJ files are never modified.

Use repeated named controls for a less noisy local timing diagnostic or a
uniform scale differential:

```bash
python3 tools/diagnostics/curvature/compare_curvature_corpus.py \
  --dataset-root /path/to/Dataset/obj \
  --intrinsic build/ci-release/bin/IntrinsicCurvatureCorpusProbe \
  --pmp /tmp/PmpCurvatureCorpusProbe-clang23 \
  --output /tmp/curvature-controls.json \
  --max-models 3 \
  --model bunny_decimated.obj \
  --model inputmodels/armadillo.obj \
  --model inputmodels/fandisk.obj \
  --repetitions 5 \
  --scale 1e-6
```
