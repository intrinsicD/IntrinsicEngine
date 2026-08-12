# Curvature corpus differential

This directory contains a local-development diagnostic for comparing
`Geometry.Curvature` with an explicitly supplied PMP checkout on OBJ corpora.
It is not registered in CTest, does not download PMP or datasets, and does not
produce claim-eligible benchmark results.

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
