# Build Troubleshooting (Container / CI)

This document explains the most common reason `src/core`, `src/geometry`,
`src/ecs`, and test targets fail to configure/compile in minimal container
environments.

## Symptom

`cmake --preset dev` fails during **Generate** with errors like:

- `... has C++ sources that may use modules, but the compiler does not provide a way to discover the import graph dependencies ...`

Affected targets typically include `ExtrinsicCore`, `IntrinsicGeometry`,
`ExtrinsicECS`, and test object targets.

## Root cause

Intrinsic uses C++23 modules and relies on CMake's module dependency scanning.

When CMake auto-selects **GCC** (`/usr/bin/c++`), module scanning is unavailable/incomplete for this project configuration, so generation fails before compilation starts.

In contrast, Clang 20 or newer with matching `clang-scan-deps` supports the required dependency scanning flow used by this build. The repository presets select the highest complete installed Clang 20+ toolchain automatically.

## Verify quickly

```bash
cmake --version
c++ --version
for v in $(seq 99 -1 20); do
  if command -v "clang++-$v" >/dev/null && command -v "clang-scan-deps-$v" >/dev/null; then
    "clang++-$v" --version
    "clang-scan-deps-$v" --version
    break
  fi
done
cmake --preset dev
```

If configure output shows `The CXX compiler identification is GNU ...` and then module import-graph errors, you are hitting this issue.

## Working options

### Option A (recommended): use the repository preset

```bash
cmake --preset dev
cmake --build --preset dev --target ExtrinsicCore IntrinsicGeometry ExtrinsicECS
cmake --build --preset dev --target IntrinsicCoreTests IntrinsicECSTests IntrinsicGeometryTests
ctest --test-dir build/dev --output-on-failure
```

`cmake/IntrinsicClangToolchain.cmake` chooses the highest complete installed
toolchain at major version 20 or newer. A complete toolchain means matching
`clang`, `clang++`, and `clang-scan-deps` binaries, such as
`clang-22`/`clang++-22`/`clang-scan-deps-22`.

### Option B: override the compiler explicitly for one configure

```bash
cmake --preset dev \
  -DCMAKE_C_COMPILER=/usr/bin/clang-20 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-20 \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-20
```

Use this only when reproducing an issue against a specific Clang version. The
root CMake configure still rejects Clang older than 20 and warns when the
scanner major version does not match the selected compiler major version.

### Option C: install the minimum toolchain

If no complete Clang 20+ toolchain is installed, install at least Clang 20 and
its tools package (for example `clang-20`, `clang++-20`, and
`clang-scan-deps-20`). Newer complete toolchains are preferred automatically.

## Additional container caveat (non-blocking for libs/tests)

Some containers do not provide X11 RandR headers; CMake enables `INTRINSIC_HEADLESS_NO_GLFW=ON` in that case. This disables GLFW-dependent runtime/sandbox modules but still allows core libraries and headless tests to build.

## Dependency bootstrap and recovery

Repository presets resolve third-party C/C++ libraries through the root
`vcpkg.json` manifest. On a fresh checkout, bootstrap the repository-local vcpkg
tool first:

```bash
tools/setup/bootstrap_vcpkg.sh
cmake --preset ci
```

`CMakePresets.json` points `CMAKE_TOOLCHAIN_FILE` at
`external/vcpkg/scripts/buildsystems/vcpkg.cmake` and chainloads
`cmake/IntrinsicClangToolchain.cmake`, so the same Clang 20+ module-scanning
requirements still apply. Installed packages live under
`external/vcpkg-installed/<preset>/`.

### Blocked vcpkg tool download (egress 403)

Some managed/cloud environments allow git-protocol clones and the GitHub API but
deny general HTTPS GETs to `github.com` with HTTP 403. vcpkg's bootstrap
downloads a prebuilt tool binary (`vcpkg-glibc`) from GitHub release assets — the
blocked class — so `cmake --preset ci` aborts during `project()` with
`vcpkg install failed`, and `build/<preset>/vcpkg-bootstrap.log` shows only:

```text
Downloading vcpkg-glibc...
curl: (22) The requested URL returned error: 403
```

This is the **environment egress policy, not a repository defect** (`BUG-065`).
Confirm and classify it:

```bash
tools/setup/vcpkg_preflight.sh                              # prints diagnosis; exit 3 when blocked
git ls-remote https://github.com/microsoft/vcpkg-tool HEAD  # git protocol still works
curl -sS "$HTTPS_PROXY/__agentproxy/status"                 # 403 = egress denial
```

`tools/setup/agent_session_setup.sh` runs this preflight at session start and
records the result to `/tmp/intrinsic-session-setup.vcpkg`. Resolve it at the
environment level:

1. Allow the vcpkg tool download host (github.com release assets /
   `objects.githubusercontent.com`) in the environment network policy; then
   `cmake --preset ci` bootstraps normally. Network policies are chosen per
   environment — see <https://code.claude.com/docs/en/claude-code-on-the-web>.
2. Or pre-bake vcpkg into the environment snapshot: with network available run
   `tools/setup/bootstrap_vcpkg.sh` (or `agent_session_setup.sh
   --bootstrap-vcpkg`) and ship the bootstrapped `external/vcpkg` plus a
   populated `external/vcpkg-bincache`, then export `VCPKG_BINARY_SOURCES` at
   that cache so sessions start build-ready and offline.

Even with the tool present, a fully blocked environment also fails to fetch
dependency sources (most as `github.com/.../archive/*.tar.gz`), so option 1 or a
populated binary cache is required for an actual build.

Raw IDE-generated configure commands are supported after bootstrap when they do
not set `CMAKE_TOOLCHAIN_FILE`: the top-level `CMakeLists.txt` selects the
repository vcpkg toolchain before `project()` and uses
`external/vcpkg-installed/<build-tree-name>/` for packages. If an IDE profile
sets a toolchain manually, point it at
`external/vcpkg/scripts/buildsystems/vcpkg.cmake`; do not point it directly at
`cmake/IntrinsicClangToolchain.cmake`, because that file is chainloaded by
vcpkg.

Build trees that were first configured before the vcpkg toolchain was active
must be reset once. Use CLion's "Reset Cache and Reload" action or rerun the
same configure command with `cmake --fresh`; after that, normal reloads reuse
the generated cache. A warning about an ignored path containing only spaces is
an IDE argument-list issue and is non-fatal; remove the blank argument from the
profile to silence it.

For repeat local builds, use a filesystem binary cache:

```bash
export VCPKG_BINARY_SOURCES="clear;files,$PWD/external/vcpkg-bincache,readwrite"
cmake --preset ci
```

CI workflows run the same bootstrap before configure and cache
`external/vcpkg-bincache/` with `actions/cache`. The cache key is derived from
`vcpkg.json`, `vcpkg-configuration.json`, `tools/vcpkg/**`, and
`tools/setup/bootstrap_vcpkg.sh`; a manifest, overlay, or bootstrap-policy edit
therefore starts a new binary-cache line while older archives remain available
through restore keys.

The `pr-fast` workflow also pilots a separate ccache store at
`${{ runner.temp }}/intrinsic-pr-fast-ccache`. That cache is deliberately outside
the repository and build tree; `build/`, CMake state, Ninja metadata, object
directories, Clang BMI files, and the ccache config file are not cached. Ccache
4.9.1 passes named-module interface compiles through. Cacheable implementation
and importer units therefore use preprocessor mode with direct and depend modes
disabled, while `CCACHE_EXTRAFILES` adds a CMake-generated digest of every
repository `.cppm` to each key. CMake regenerates that digest when an existing
interface changes or the interface inventory changes, deliberately invalidating
all keyed consumers rather than risking stale BMI/layout reuse. The workflow
validates the generated launcher and digest, zeros statistics immediately before
compilation, and reports hit/miss, cache-size, error, availability, and health
diagnostics in the CI gate-latency result. Only an exact-key or
compatibility-prefix content store is restored; each successful commit saves a
new immutable SHA-suffixed key after all guards pass. To clear the pilot, delete
GitHub Actions caches whose key starts
with `ccache-v1-Linux-X64-pr-fast-`; do not delete or repoint the vcpkg binary
cache as a ccache rollback. If a cached or incremental module build produces
unexplained SEGV, vtable, ASan, or compiler frontend symptoms, follow the
stale-build triage playbook at
`tools/agents/skills/intrinsicengine-stale-build-triage/SKILL.md`
before changing engine source.

For a local clean no-ccache comparison build, keep the `ci` preset inputs but
configure a separate tree with `-DINTRINSIC_ENABLE_CCACHE=OFF`, for example:

```bash
cmake --preset ci -B build/ci-no-ccache -DINTRINSIC_ENABLE_CCACHE=OFF
cmake --build build/ci-no-ccache --target IntrinsicTests
ctest --test-dir build/ci-no-ccache --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Initial cache-backed configure steps in CI run through
`tools/ci/time_command.py`, which records the wall-clock configure duration in
the GitHub step summary. When `actions/cache` reports an exact vcpkg
binary-cache hit, the wrapper enforces the INFRA-001 warm-cache budget by
failing configure runs that exceed 10 seconds.

Version bumps flow through the manifest:

```bash
external/vcpkg/vcpkg x-update-baseline
external/vcpkg/vcpkg format-manifest vcpkg.json
```

The vcpkg manifest is now the only supported third-party dependency path.
`cmake/Dependencies.cmake` fails closed when configure is not running in vcpkg
manifest mode; use the repository presets, or leave `CMAKE_TOOLCHAIN_FILE`
unset in IDE profiles so the top-level configure can select the repository
vcpkg toolchain. Retired CMake dependency-cache flags are unsupported.

`xatlas` is supplied by the repository overlay port at
`tools/vcpkg/overlay-ports/xatlas` because the pinned baseline does not provide
a first-class port for the GEOM-025 UV atlas backend. If configure fails while
installing `xatlas`, delete the failed package/buildtree under
`external/vcpkg-installed/<preset>/` and `external/vcpkg/buildtrees/xatlas`, then
rerun `cmake --preset ci`; do not add a FetchContent fallback or write into
`external/cache`.

## Blocked follow-on CI steps

Some local CI sweeps intentionally continue after a build failure to collect logs.
CTest, SLO, and benchmark-result validation are dependent steps: they require the
producer targets and output directories to exist. Workflows use
`tools/ci/check_prerequisites.py` to report these as `BLOCKED` prerequisite
failures instead of presenting missing binaries or missing benchmark directories
as the root cause.

Specialized test gates pass the generated aggregate inventory instead of
maintaining another executable list:

```bash
python3 tools/ci/check_prerequisites.py test-binaries \
  --build-dir build/ci \
  --inventory build/ci/test-inventories/IntrinsicPrFastTests.txt
```

The check requires every selected executable and ignores targets absent from the
inventory. Missing, empty, malformed, or duplicate inventory entries fail
closed. `--targets ... --skip-undeclared` remains available for legacy or
configuration-conditional checks; it cannot be combined with `--inventory`.

## Fast touched-scope verification

For local iteration on small, well-scoped changes, use the touched-scope helper
to plan or run the strongest relevant subset without waiting for the full CPU
gate every time:

```bash
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --print
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --run
```

The helper maps changed paths to conservative CMake targets, CTest labels, and
repository structural checks. For example, `src/geometry/` changes select
`IntrinsicGeometryTests`, `-L geometry`, and the layering check; docs/tasks-only
changes select docs/task validators without a C++ build. Build-system changes,
foundational `src/core/` changes, and unknown source paths fall back to the broad
CPU-supported gate.

This helper is an iteration aid, not a replacement for the canonical PR/merge
verification from `AGENTS.md`:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
