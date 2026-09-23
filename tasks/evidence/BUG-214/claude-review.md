**Result: not PASS.** There is one blocking finding.

### Blocking: on 3.31.6 the instrumented module is never loaded
`tasks/evidence/BUG-214/cmake331-after.log` was written at 05:07:01, after the test file's last change at 05:06:57, so it reflects the current diff. All three tests still fail there, each with `CTest exited before barrier …/a-enter: 0`.

The cause is in 3.31.6's `GoogleTest.cmake`. Its PRE_TEST include file has the installed module's path built in: `include("${CMAKE_ROOT}/Modules/GoogleTestAddTests.cmake")`. It does not read `_GOOGLETEST_DISCOVER_TESTS_SCRIPT` at all. 3.28 did read it (`/usr/share/cmake-3.28/Modules/GoogleTest.cmake:529`), and the fixture relies on that at line 101. So on 3.31 the checks that the text was found (write count, final-flush count, entry count) all pass against a private copy that ctest never runs. Discovery uses the unmodified module, no barrier is ever hit, and ctest exits 0 before `a-enter`. The count checks prove the text was found, not that the patched copy is what runs.

**Suggested fix (test-only):** after `cmake -S/-B` in `project()`, edit the generated `build/Fake[1]_include.cmake`, the same way the fixture already edits `CTestTestfile.cmake`:
- Replace `include("<CMAKE_ROOT>/Modules/GoogleTestAddTests.cmake")` with the private module path.
- Assert that exactly one of the two known forms is present: the 3.28 private path, or the 3.31 `CMAKE_ROOT` path.
- If you do this for both versions, the variable at line 101 is no longer needed. The production helper can stay untouched, because it only wraps `TEST_INCLUDE_FILES`.

### Checked and fine once the module actually loads
- **Barrier timing, both forms:** each process does exactly one WRITE, either the first chunk inside the loop or the final write if the loop never flushed. The if-guard on the mode is checked at run time, so APPEND chunks are never paused. The handshake cannot deadlock, because each side creates its own `after-write` file before waiting on the other's.
- **Completion marker on 3.31:** the final-flush text is replaced before the write is wrapped, so `done` ends up after the wrapped final write's `after-write` block. That order is correct.
- **Negative control:** both versions write a chunk once the script passes 50000 characters. 600 tests with properties go well past that, so APPEND chunks exist after B's truncating WRITE and duplicates can form.
- **Exact counts:** 3.31.6 has 2 write sites and 1 final site; 3.28 has 1 macro write and 1 `flush_script()` final call. Any other layout fails hard.
- **Duplicate preservation and nested CTest lock release:** the diff doesn't change the assertions or the flow. Once discovery really runs through the instrumented module, the checks behave as before. On the nested run the tests file is already fresh, so discovery doesn't re-run and nothing waits on the lock.

The 3.28.3 after-log shows 3 tests passing. The 3.31.6 result is a real failure, not a pending run, so the acceptance criterion "pass on 3.28.3 and 3.31.6" isn't met.
