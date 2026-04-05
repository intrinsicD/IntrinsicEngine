# Commit Review — 2026-04-04 to 2026-04-05

## Scope and method

- Read all repository markdown files first (`*.md`, including docs and tools baselines).
- Reviewed all commits authored on **2026-04-04** and **2026-04-05** (non-merge and merge context).
- Focused on correctness risks: UB/crash paths, lifetime hazards, release/debug behavior skew, and regression-prone API changes.

## Commit window summary

- Total commits in window: 41 (including merges).
- Non-merge commits reviewed in depth: 22.
- Highest-risk areas touched:
  - RHI error handling + queue-family access (`4a19a2f`, `475fce5`, `b568945`)
  - Shader hot reload plumbing (`94e7261`)
  - Editor multi-selection transform command path (`597568a`, `9d4ceff`)
  - New edge loop/ring selection traversal (`dcf96a2`)
  - DenseGrid migration path (`24e1217`, `f619a15`, `18e27f0`)

---

## Critical findings

### 1) Release-mode Vulkan failures are logged but execution continues in potentially invalid state

**Where:** `src/Runtime/RHI/RHI.Vulkan.hpp` (release `VK_CHECK`)  
**Commits:** `475fce5` (changed behavior)

In release builds, `VK_CHECK` now logs and continues instead of aborting. This creates a correctness hazard because many call sites assume success and immediately consume output objects/handles.

**Why this is risky:**
- For resource creation/recording/submission calls, continuing after a failed `VkResult` can propagate invalid handles or stale state deeper into frame execution.
- This can convert deterministic fail-fast into delayed corruption/crash at unrelated sites, making production issues harder to triage.

**Recommendation:**
- Use graded policy instead of blanket “log-and-continue”:
  - hard-fail for object creation, queue submit, command buffer begin/end, swapchain transitions,
  - soft-fail only where explicit recovery logic exists.
- Prefer `VK_CHECK_OR_RETURN` / `VK_CHECK_OR_FALSE` style wrappers at call sites that can recover.

---

### 2) Optional queue-family access is still unsafe in release path (assert-only guard)

**Where:** `src/Runtime/RHI/RHI.Transfer.cpp`  
**Commits:** `4a19a2f`

`TransferManager` now falls back from `TransferFamily` to `GraphicsFamily`, but still uses `.value()` without runtime validation:

- constructor path for `queueFamilyIndex`
- thread-local command pool creation path

These are guarded only by optional presence assumptions; if violated (device discovery regression/platform quirk), release behavior remains abrupt termination.

**Recommendation:**
- Replace `.value()` with explicit branch:
  - if neither family is present, log + mark transfer manager unavailable (or fail init deterministically),
  - avoid implicit termination paths in production.

---

### 3) Command-buffer leak on staging allocation failure (error path)

**Where:** `src/Runtime/RHI/RHI.Transfer.cpp` (`UploadBuffer`)  
**Commits involved in area:** `4a19a2f`

`UploadBuffer()` calls `Begin()` (allocates + begins command buffer), then may early-return when staging allocation fails, without freeing or recycling that command buffer.

**Impact:**
- Repeated allocation failures can steadily leak command buffers from transient pool.
- Under memory pressure (exactly when staging may fail), this worsens recovery and can snowball into device instability.

**Recommendation:**
- On allocation failure after `Begin()`, explicitly `vkFreeCommandBuffers` (or submit to a recycle path) before returning.

---

### 4) Shader hot-reload compilation uses shell command assembly via `std::system`

**Where:** `src/Runtime/Graphics/Graphics.ShaderHotReload.cpp`  
**Commits:** `94e7261`

The hot-reload path constructs a shell command string and invokes `std::system(...)` for each changed shader.

**Risks:**
- Shell-escaping fragility if any path contains quotes or shell-special characters.
- Blocking behavior on watcher thread with unbounded external process latency.
- Platform portability and diagnosability limits compared to direct process spawn APIs.

**Recommendation:**
- Use a structured process API (`execve`-style argv on POSIX / dedicated process runner abstraction) instead of shell-string execution.
- Capture compiler stderr/stdout directly for deterministic diagnostics.

---

### 5) Edge loop/ring traversal semantics are underconstrained on irregular valence and can produce non-intuitive selections

**Where:** `src/Runtime/Geometry/Geometry.MeshUtils.cpp`  
**Commits:** `dcf96a2`

Loop traversal defines continuation using `valence / 2` CW rotations and advertises triangle support. On odd/irregular valence vertices this “straight continuation” is not topologically well-defined.

**Why this matters:**
- Behavior can be stable yet semantically surprising in production tools (especially mixed tri/quad meshes).
- Test coverage currently validates no-duplicates/termination and a few canonical strips, but not user-facing consistency invariants for irregular stars.

**Recommendation:**
- Document tie-break policy explicitly for odd valence.
- Add tests for mixed-valence vertices with expected deterministic continuation.
- Consider exposing strategy enum (strict-quad, permissive-tri, shortest-turn heuristic).

---

## Additional observations (lower severity)

- `vkDeviceWaitIdle` result handling was improved in several places, but some paths still ignore return values (e.g., transfer manager destructor). Standardizing this would improve postmortems.
- Multi-selection transform batching appears functionally improved, but command coalescing behavior remains frame-timing sensitive; fuzz-style UI interaction tests would help avoid regressions.
- DenseGrid/SparseGrid API is a strong migration direction, but sparse key packing assumes bounded block coordinates; bounds assertions or checked constructors would make this contract explicit.

## Environment check

- `cmake --preset dev` currently fails in this environment due to missing X11 RandR dev headers (`libxrandr`), so full compile/test validation could not be executed here.
