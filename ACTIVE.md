# Active Debugging Notes

*(No active debugging tasks. Last resolved: 2026-04-07.)*

## Resolved: SpatialDebugController heap-buffer-overflow (2026-04-07)

### Root Cause

**Stale C++23 module BMI (Binary Module Interface)** after PR #502 changed `SpatialDebugController::m_SelectedColliderHullMesh` from `Geometry::Halfedge::Mesh` (value member) to `std::optional<Geometry::Halfedge::Mesh>`.

The class layout changed — the old layout default-constructed a `Mesh` (calling `make_shared<MeshProperties>()`), while the new layout stores a disengaged `std::optional` (no `Mesh` constructed). Some downstream TUs (`main.cpp` via `SandboxApp`) retained a stale BMI with the old layout, causing a size/alignment mismatch. The `PropertyRegistry::PropertyRegistry()` default constructor (inside `MeshProperties`) wrote past the end of the under-sized allocation, triggering ASan's heap-buffer-overflow.

### Why Previous "Clean Rebuild" Failed

The previous session cleaned only `IntrinsicGeometry` (geometry library), not the `IntrinsicRuntime` BMIs. CMake's C++23 module dependency scanner (`clang-scan-deps`) did not propagate the interface change from `Runtime.EditorUI.cppm` to all consumers. Ninja believed the runtime BMIs were up-to-date.

### Fix

A full `--clean-first` rebuild of `IntrinsicGeometry` forced all geometry BMIs to regenerate. Ninja then detected the cascading dependency changes and rebuilt `Runtime.EditorUI.cppm` and `main.cpp` against fresh BMIs. The layout mismatch was resolved.

### Verification

- Sandbox runs cleanly under ASan (Clang 22, `cmake-build-debug`): no heap-buffer-overflow.
- All 887 geometry tests pass under ASan.
- All 320 core tests pass under ASan (2 skipped: benchmark SLOs).
- No code changes required — the existing code (PR #502) was correct.

### Preventive Measure

When changing exported class layouts in `.cppm` module interfaces (especially adding/removing/retyping members), always rebuild the owning target with `--clean-first` to force BMI regeneration:

```bash
cmake --build cmake-build-debug --target IntrinsicRuntime --clean-first --parallel $(nproc)
cmake --build cmake-build-debug --target Sandbox --parallel $(nproc)
```

Or, when in doubt, nuke the entire build directory's module cache.
