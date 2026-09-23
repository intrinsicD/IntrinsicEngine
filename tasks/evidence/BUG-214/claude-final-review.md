**PASS.** I found no problems with the final routing delta. Remote CI is still pending.

**Routing change (`tests/regression/tooling/Test.ConcurrentCTestDiscovery.py:112-120`)**
- **3.28.3:** the generated include file uses `_GOOGLETEST_DISCOVER_TESTS_SCRIPT` (`/usr/share/cmake-3.28/Modules/GoogleTest.cmake:529`), so it already points at `private_module`. Line 101 passes that value in unchanged, so the private path matches and the replace does nothing.
- **3.31.6:** line 708 of that version's `GoogleTest.cmake` writes `include("${CMAKE_ROOT}/Modules/GoogleTestAddTests.cmake")`. `${CMAKE_ROOT}` is filled in at configure time, so the file holds the same path the fixture builds from `cmake -P`. That makes `installed_include` match exactly, and it gets swapped for the private path.
- **Which file is edited:** on single-config generators, both versions produce `${TARGET}[1]_include.cmake` with `file(GENERATE)`, and that output replaces the placeholder `file(WRITE)`. The file therefore has its final content once `cmake -S/-B` returns, and ctest never regenerates it before the fixture edits it. Only this one fixture file is changed. The installed module and `cmake/IntrinsicTestDiscovery.cmake` are untouched: `git status` shows nothing under `cmake/`, and the helper still only wraps `TEST_INCLUDE_FILES`.
- **The "exactly one known include" check** fails hard if a layout ever has neither form or both. For example, a multi-config generator would leave only the dispatch stub, so the count would be 0.
- **Side effect of the rewrite:** the generated guard includes `IS_NEWER_THAN ${CMAKE_CURRENT_LIST_FILE}`. Rewriting the file after configure makes it newer, but the tests file doesn't exist yet at that point, so first discovery always runs. The nested-CTest step later finds a tests file newer than the edited include, so it doesn't rediscover, as the test intends.
- **Private module location:** neither version's `GoogleTestAddTests.cmake` depends on `CMAKE_CURRENT_LIST_DIR` or includes sibling files. 3.31 only sets `cmake_policy(SET CMP0174 NEW)`. Loading it from the fixture source directory is safe.
- **Proof it runs:** all three tests wait for the `a-enter` barrier, and only the instrumented module creates it. A pass therefore shows the private module actually ran. That is the gap my earlier review flagged, now closed. The unlocked control test also still requires duplicates to appear.

**Evidence**
- `cmake328-after.log` (05:09:20) and `cmake331-after.log` (05:09:22) are both newer than the test file's last change (05:09:17). Each shows `Ran 3 tests … OK`.
- The 3.31.6 tarball in `/tmp` matches the SHA-256 in `cmake-source.json`.
- The initial-WRITE and final-flush instrumentation is identical to the part I approved before.

**Not a finding:** line 101 now matters only on 3.28, and the new comment says so correctly.

I made no edits and ran no builds or tests; I only read files, checked timestamps and a checksum, and printed the CMake version.
