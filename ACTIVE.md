# Active Debugging Notes — RESOLVED

Date: 2026-04-07 (resolved 2026-04-06)

## Status: **CLOSED — Clang 18 module ABI bug**

---

## Root Cause

The heap-buffer-overflow crash in `PropertyRegistry::PropertyRegistry()` was caused by a **Clang 18 C++23 module ABI bug** that generated incorrect `sizeof` information for types defined in module interface units.

### Evidence

On **Clang 20** (the project's minimum supported compiler), the actual type sizes are:

| Type | sizeof (Clang 20) |
|---|---|
| `PropertyRegistry` | 88 bytes |
| `PropertySet` | 88 bytes |
| `MeshProperties` | 376 bytes |
| `Mesh` | 360 bytes |
| `optional<Mesh>` | 368 bytes |

`make_shared<MeshProperties>()` should allocate ~392 bytes (376 + ~16 control block).

On **Clang 18**, ASan reported the allocation was only **168 bytes** — less than half the correct size. This means Clang 18's module BMI (binary module interface) presented `sizeof(MeshProperties)` as approximately 152 bytes (168 minus ~16 for the shared_ptr control block). That would imply `sizeof(PropertySet)` ≈ 32 bytes instead of the correct 88 — the `std::unordered_map` member's size was being miscalculated by the module system.

When the fourth `PropertyRegistry` was constructed in the undersized allocation, the `unordered_map`'s `_Prime_rehash_policy` float (stored at the end of the hashtable internals) wrote past the allocation boundary.

### Why adding `m_BVHOverlaySourceEntity` triggered the crash

Adding any new member to `SpatialDebugController` changes the class layout. With Clang 18's incorrect layout calculations, this shifted offsets enough to make the heap corruption immediately detectable by ASan. The new field was the trigger, not the cause.

### Reproduction

- **Crashes:** Clang 18 + any ASan-enabled build → heap-buffer-overflow during Sandbox startup
- **Clean:** Clang 20 + ASan → all 890 geometry tests pass, Sandbox starts without crash
- **Clean:** Clang 20 + ASan → `std::optional<Mesh>{}` correctly produces disengaged optional

### Fix

No code changes needed — the code is correct. The project already requires Clang 20+ (CLAUDE.md, `.claude/setup.sh`). The previous machine's `cmake-build-debug` tree was configured with Clang 18, violating this requirement.

Compile-time layout canary tests were added to `Test_HalfedgeMeshPropertyAccess.cpp` to catch future compiler regressions:

1. `PropertyLayout_Canary.PropertyRegistrySizeIncludesUnorderedMap` — verifies `sizeof(PropertyRegistry) >= 64`
2. `PropertyLayout_Canary.MeshPropertiesContainsFourPropertySets` — verifies `sizeof(MeshProperties) >= 4*sizeof(PropertySet) + 3*sizeof(size_t)`
3. `PropertyLayout_Canary.OptionalMeshDefaultIsDisengaged` — verifies `std::optional<Mesh>{}` does NOT engage the Mesh constructor

---

## Previous investigation notes (retained for reference)

The crash was first observed in `cmake-build-debug` (Clang 18) on the development machine. A clean rebuild with Clang 18 did not fix the crash. The session was interrupted before testing with a newer compiler. This session confirmed the diagnosis by building and testing with Clang 20 on a clean environment.
