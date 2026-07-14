---
id: BUG-033
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-033 — Mesh IO trusts header-declared counts: unbounded reserve aborts and wrap-prone bounds checks

## Goal

- Malformed or hostile OFF/PLY files fail with `InvalidMeshFormat` instead of aborting the process or desynchronizing the binary cursor: every header-declared count is validated against the actually-available payload before any allocation, and every byte-size computation is overflow-safe.

## Non-goals

- No new format features (no new PLY properties, no OBJ/STL surface changes).
- No parser rewrite or IO-API redesign; this is targeted hardening of `Geometry.HalfedgeMesh.IO.cpp`.
- No exception enablement; the engine stays `-fno-exceptions` (`CMakeLists.txt` `INTRINSIC_BASE_FLAGS`).
- No fuzzing infrastructure (worth a separate task if wanted; this task ships deterministic regression cases).

## Context

- Symptom: the engine builds with `-fno-exceptions` (CMakeLists.txt:119), so any `std::vector::reserve` failure calls `abort()`. The PLY/OFF parsers reserve vectors sized by header counts **before** validating them against the data actually present:
  - Binary PLY: `vertices/normals/colors/texcoords.reserve(vertexElement->Count)` and `faces.reserve(faceElement->Count)` at `src/geometry/Geometry.HalfedgeMesh.IO.cpp:853-870`, where `Count` is parsed from header text as an arbitrary `std::size_t` (`element` handling at 1687-1701). A ~60-byte file declaring `element vertex 9999999999999999999` aborts the process.
  - ASCII PLY: same pattern at 561-576 and 656.
  - OFF: `vertices.reserve(*vertexCount)` at 1472 (plus 1476/1481) and `faces.reserve(*faceCount)` at 1567, counts parsed at 1464-1465.
- Symptom: the binary-PLY bounds checks multiply untrusted counts: `element.Count * vertexStride` (882) and `count * elemBytes` (963). `std::size_t` wrap can make `total` small, pass the `end - cursor < total` check, and desynchronize/overrun the cursor. With the currently accepted ≤32-bit integer count types the face-list product cannot wrap on 64-bit hosts, but `element.Count` is a full `size_t` and `vertexStride` is unbounded (header may declare arbitrarily many properties), so the row-block check at 882-886 is wrap-reachable in principle and the idiom is fragile either way.
- Symptom: list-count properties accept floating scalar types — `ParsePlyScalarType` (343-354) is used for `ListCountType` without an integrality check (1710-1721), then `ReadScalarAs<std::uint64_t>(cursor, prop.ListCountType, …)` (960) converts a `double` to `uint64_t`; for out-of-range/negative values that conversion is undefined behavior. The PLY spec requires integral count types. Negative signed counts (e.g. `int32 -1`) sign-extend to ~2^64 and are only caught incidentally by the size check.
- Symptom: the OFF face loop silently skips rows whose vertex count is missing or `< 3` (1584-1588, plain `continue`), so a file declaring N faces can import N−k faces and report success — silent geometry loss. Binary PLY hard-rejects `count < 3` (971-974); the loader family is inconsistent.
- Not a bug (do not "fix"): the `--i; continue;` comment-line pattern in the OFF loops (1491, 1576) is correct — `continue` runs `++i`, so the unsigned wrap round-trips. Regression tests must keep covering comment-before-first-row files.
- The correct pattern already exists in this file: `ParseBinarySTL` (1078-1098) validates `data.size() < 84 + triCount*50` with non-wrapping arithmetic **before** reserving. Use it as the model.
- Impact: crash-by-file denial of service on the user-facing import path — mesh payloads flow through `Runtime.AssetModelTextureIO.cpp` (`BuildMeshPayload`) from sandbox drop-import (BUG-021/BUG-022/BUG-023 lineage); a corrupt download kills the editor with no diagnostic. Secondary: cursor desync / out-of-bounds reads from wrap-bypassed bounds checks; silent data corruption from dropped OFF faces.
- Owner/layer: `geometry` (`Geometry.HalfedgeMesh.IO` module implementation unit only). No public module-surface change expected.

## Required changes

- [x] Add an overflow-safe size-guard helper (file-local, in the module implementation unit) for "does the remaining payload hold `count` records of `stride` bytes": reject when `stride != 0 && count > remaining / stride` — division, never multiplication.
- [x] Binary PLY: validate `vertexElement->Count` / `faceElement->Count` against `end - cursor` with the helper **before** the reserves at 853-870; replace the multiply-checks at 882-886 and 955-967 with the helper (face rows have a ≥1-byte/record lower bound via the list-count scalar; use that as the stride floor for `faceElement->Count`).
- [x] ASCII PLY and OFF: before reserving (561-576, 656, 1472-1481, 1567), clamp the reserve argument by a conservative payload bound (remaining text bytes / minimal bytes-per-row, e.g. 2 for "0\n") so a hostile count cannot drive allocation; keep the parse loops themselves driven by the declared counts so truncated files still fail with `InvalidMeshFormat`.
- [x] Reject non-integral `ListCountType` at header parse (1710-1721) per PLY spec, and reject negative list counts for signed integral count types explicitly at 960 before any use.
- [x] OFF face rows with unparseable or `< 3` vertex counts return `InvalidMeshFormat` (1584-1588) instead of silently skipping, matching the binary-PLY policy. If a deliberate lenient mode is ever wanted it must be opt-in and diagnosed, not default-silent.
- [x] Sweep the remaining `reserve(` sites in this file (561, 565, 570, 575, 656, 674, 853, 857, 862, 867, 870, 975, 1101, 1103, 1279, 1594…) and confirm each is bounded by validated data (STL at 1101/1103 is already safe; document per-site disposition in the PR description).

## Tests

- [x] Extend `tests/unit/geometry/Test.GeometryIO.cpp` (or add a sibling `Test.GeometryIOUntrustedInput.cpp`, label `unit;geometry`) with in-memory/temp-file cases:
  - [x] PLY (ascii + binary) declaring a huge `element vertex` count with a tiny body → `InvalidMeshFormat`, process survives.
  - [x] Binary PLY whose `Count * stride` would wrap `size_t` → `InvalidMeshFormat`.
  - [x] PLY `property list float32 …` (non-integral count type) → `InvalidMeshFormat` at header parse.
  - [x] Binary PLY with a negative `int32` list count → `InvalidMeshFormat`.
  - [x] OFF declaring a huge vertex/face count with a tiny body → `InvalidMeshFormat`.
  - [x] OFF with a `2 0 1` face row → `InvalidMeshFormat` (new strict policy).
  - [x] Guard cases that must keep passing: comment/empty line before the first OFF vertex and first OFF face row still parse (pins the correct `--i` wrap behavior).
- [x] Default CPU gate stays green: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Docs

- [x] Note the untrusted-input policy (counts validated against payload before allocation; strict degenerate-face rejection) in the geometry IO module README or the module's header comment, whichever exists — keep it factual current-state.

## Acceptance criteria

- [x] All listed malformed inputs return `InvalidMeshFormat`; none abort, hang, or read out of bounds (verify the worst cases under the sanitizer preset if available locally).
- [x] Valid OFF/PLY/OBJ/STL fixtures that parsed before still parse identically (existing `Test.GeometryIO` cases stay green).
- [x] No public module-surface change; no layering change.
- [x] OFF/PLY degenerate-face policy is consistent and documented.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

2026-06-12 results:
- Commit: pending local BUG loop closure commit.
- `cmake --build --preset ci --target IntrinsicTests` passed.
- Focused BUG regression set passed the new OFF/PLY malformed-count, non-integral list-count, negative binary-list-count, degenerate OFF face, and comment-preservation cases.
- Default CPU-supported CTest gate passed after the parser hardening.

## Forbidden changes

- Enabling exceptions or adding try/catch as the "fix".
- Loosening any currently-strict validation (e.g. binary-PLY `count < 3` rejection) for symmetry.
- Touching importer call sites in `runtime`/`assets` — the fix is contained in the geometry IO implementation unit.
- Shipping without the malformed-input regression tests.

## Maturity

- Target: `CPUContracted` — mesh IO is CPU-only; the default CPU gate fully exercises the fix. No `Operational` follow-up is owed.
