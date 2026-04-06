# Active Debugging Notes

Date: 2026-04-07

## Current task
We are debugging a crash that appears when `Runtime::EditorUI::SpatialDebugController` is touched after adding:

```cpp
entt::entity m_BVHOverlaySourceEntity = entt::null;
```

The user asked to keep a full handoff record here so work can resume tomorrow without re-deriving the state.

---

## TL;DR

The new `entt::entity` member is **not** the root cause by itself.

The first reproducible failure we captured with ASan is a **heap-buffer-overflow** during construction of `Geometry::PropertyRegistry`, called from `Geometry::Halfedge::Mesh::Mesh()`, which is reached while constructing `SpatialDebugController`.

That means the bug is either:

1. a real bug in the geometry property / halfedge mesh construction path, or
2. a remaining module/object mismatch issue in the geometry build artifacts.

A clean rebuild of `IntrinsicGeometry`, `IntrinsicRuntime`, and `Sandbox` in the existing `cmake-build-debug` tree **did not remove the crash**.

---

## Exact crash evidence from ASan

Reproduced by running `Sandbox` under ASan/UBSan in `cmake-build-debug`:

```bash
ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0:fast_unwind_on_malloc=0 \
  timeout 20s /home/alex/Documents/IntrinsicEngine/cmake-build-debug/bin/Sandbox
```

### First failing stack

- `Geometry::PropertyRegistry::PropertyRegistry()`
  - `src/Runtime/Geometry/Geometry.Properties.cppm:142`
- called from `Geometry::Halfedge::Mesh::Mesh()`
  - `src/Runtime/Geometry/Geometry.HalfedgeMesh.cpp:151`
- called while constructing `Runtime::EditorUI::SpatialDebugController`
  - `src/Runtime/EditorUI/Runtime.EditorUI.cppm:796`
- reached from `SandboxApp::SandboxApp(...)`
  - `src/Apps/Sandbox/main.cpp:312`

### ASan summary

- **heap-buffer-overflow**
- write of **4 bytes**
- write occurs in `std::__detail::_Prime_rehash_policy::_Prime_rehash_policy(float)`
- inside `std::unordered_map` construction for `Geometry::PropertyRegistry`
- the allocation was **168 bytes**, and the write happened **0 bytes past the end**

### Important interpretation

The overflow is not in BVH drawing logic yet. It is happening much earlier, during geometry object construction.

---

## What we verified

### 1) The crash survives a clean rebuild
We performed a clean rebuild of the geometry library and then rebuilt runtime + Sandbox:

```bash
cmake --build /home/alex/Documents/IntrinsicEngine/cmake-build-debug --target IntrinsicGeometry --clean-first --parallel $(nproc)
cmake --build /home/alex/Documents/IntrinsicEngine/cmake-build-debug --target IntrinsicRuntime Sandbox --parallel $(nproc)
```

Then re-ran the ASan repro. The crash still occurred.

So this is **not** just a stale incremental build artifact in the current tree.

### 2) Clang 22 is installed on this machine
These tools exist:

- `/usr/bin/clang++-22`
- `/usr/bin/clang-22`
- `/usr/bin/clang-scan-deps-22`

Version reported:

- Ubuntu Clang 22.0.0

We did **not** complete a full Clang 22 reconfigure/rebuild before the session was interrupted, but the toolchain is available.

### 3) The current sanitized build tree is `cmake-build-debug`
That tree is configured with ASan/UBSan in `CMakeCache.txt`.

The older `build/` tree is pinned to Clang 18 in cache and had unrelated `std::expected` compile failures during a build attempt.

---

## Key source locations

### Editor UI controller
- `src/Runtime/EditorUI/Runtime.EditorUI.cppm`
  - `SpatialDebugController` definition near line ~733
  - members of interest:
    - `m_BVHOverlaySourceEntity`
    - `m_SelectedColliderHullMesh`

### Controller implementation
- `src/Runtime/EditorUI/Runtime.EditorUI.SpatialDebugController.cpp`
  - `EnsureRetainedBVHOverlay(...)`
  - `ReleaseAll(...)`
  - `Update(...)`

### Geometry construction path
- `src/Runtime/Geometry/Geometry.HalfedgeMesh.cpp`
  - `Mesh::Mesh()` at line ~151
- `src/Runtime/Geometry/Geometry.Properties.cppm`
  - `PropertyRegistry::PropertyRegistry()` at line ~142
  - `PropertySet`/`PropertyRegistry` declarations and layout

### Sandbox ownership
- `src/Apps/Sandbox/main.cpp`
  - `SandboxApp` owns `EditorUI::SpatialDebugController m_SpatialDebug{}` by value at line ~312

---

## Current hypothesis ranking

### Most likely
A real bug in the geometry property / halfedge mesh construction path that ASan exposes when `SpatialDebugController` is instantiated.

### Still possible
A compiler/module ABI issue in the currently built artifacts, but the clean rebuild makes this less likely than before.

### Less likely
A direct bug in the new `entt::entity` member. The member itself is just a trigger for reaching the construction path.

---

## Notes on the `m_BVHOverlaySourceEntity` change

The added field is in the exported class `Runtime::EditorUI::SpatialDebugController`. That means the class layout changed.

However, the ASan trace does **not** point to an out-of-bounds access involving that field. The first hard fault is in `Geometry::PropertyRegistry` construction.

So the field is best thought of as the change that made the latent issue show up, not the source of the corruption.

---

## What we should do next

### A. Reproduce under Clang 22
Since Clang 22 is installed, the next best step is to build a fresh tree with explicit Clang 22 and ASan/UBSan, then rerun the same repro.

Suggested fresh configure command:

```bash
cmake -S /home/alex/Documents/IntrinsicEngine \
  -B /home/alex/Documents/IntrinsicEngine/build/clang22 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINTRINSIC_BUILD_SANDBOX=ON \
  -DINTRINSIC_BUILD_TESTS=ON \
  -DINTRINSIC_ENABLE_SANITIZERS=ON \
  -DINTRINSIC_ENABLE_CUDA=OFF \
  -DCMAKE_C_COMPILER=clang-22 \
  -DCMAKE_CXX_COMPILER=clang++-22 \
  -DCMAKE_C_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22 \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22
```

Then:

```bash
cmake --build /home/alex/Documents/IntrinsicEngine/build/clang22 --target IntrinsicGeometry IntrinsicRuntime Sandbox --parallel $(nproc)
ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0:fast_unwind_on_malloc=0 \
  timeout 20s /home/alex/Documents/IntrinsicEngine/build/clang22/bin/Sandbox
```

### B. If the crash reproduces under Clang 22
Then it is almost certainly a real geometry bug, not a Clang 18 module artifact.

At that point inspect:
- `Geometry.Properties.cppm`
- `Geometry.HalfedgeMesh.cpp`
- `Geometry.HalfedgeMeshFwd.cppm`

The immediate suspicious zone is the construction of `MeshProperties` / `PropertySet` / `PropertyRegistry`.

### C. If the crash disappears under Clang 22
Then we likely have a toolchain-specific module/BMI issue.

In that case:
- compare the generated module artifacts between Clang 18 and Clang 22
- inspect whether the `PropertyRegistry` and `MeshProperties` layout changes are reflected consistently across module partitions
- verify that all consumers are rebuilt from a single compiler family

---

## Minimal reproduction command used during this session

```bash
ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0:fast_unwind_on_malloc=0 \
  timeout 20s /home/alex/Documents/IntrinsicEngine/cmake-build-debug/bin/Sandbox
```

This reproduces the crash reliably in the current sanitized tree.

---

## Important caveats

- The `build/` tree currently has unrelated compile problems involving `std::expected` when rebuilt from scratch.
- `cmake-build-debug` is the useful tree right now for the ASan repro.
- The session was interrupted before the Clang 22 reconfigure/rebuild step could be completed.

---

## Suggested first steps for tomorrow

1. Create a fresh `build/clang22` tree with Clang 22.
2. Build `IntrinsicGeometry`, `IntrinsicRuntime`, and `Sandbox`.
3. Rerun the ASan repro.
4. If it still crashes, focus on `Geometry.Properties.cppm` and `Geometry.HalfedgeMesh.cpp`.
5. If it does not crash, diff Clang 18 vs Clang 22 generated module artifacts.

---

## Quick reminder

The current mystery is **not** “why does adding an `entt::entity` field crash directly?”

It is:

> Why does constructing `SpatialDebugController` reach a heap-buffer-overflow inside `Geometry::PropertyRegistry` construction?

That is the root issue to solve next.

