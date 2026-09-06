# BUG-174 isolated symbolizer diagnosis — 2026-09-06

The synthetic leak-control timeout on this host is caused by symbolizer debuginfod lookup inherited from the environment. Disabling that lookup restores the unchanged leak-detection predicate without changing the binary, symbolizer, suppressions, or time budget. The network endpoint itself was not probed separately, so its particular failure mechanism is not established.

## Reproduction and discriminating probes

- Exact helper: `build/ci-asan/bin/IntrinsicGlfwLifecycleLsanProcess synthetic-engine-leak`; working directory `/tmp/bug174-diagnosis`. Binary SHA-256: `bc65d8ea2bcf6921e6b6a10813259cf805968c93f9766cc7a690cbc0f6abbc31`.
- Exact harness environment: `ASAN_OPTIONS=detect_leaks=1:symbolize=1:fast_unwind_on_malloc=0:halt_on_error=1`; `LSAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0:exitcode=86:suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp`; `ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-18`.
- Inherited relevant environment: `DEBUGINFOD_URLS=https://debuginfod.ubuntu.com `, including the trailing space. `LLVM_SYMBOLIZER_OPTS`, `DEBUGINFOD_TIMEOUT`, and `DEBUGINFOD_MAXTIME` were unset. No broader environment was recorded.
- Exact-environment isolated reproduction: the named 4096-byte allocation marker appeared, but no leak report or exit occurred after 12.02 seconds. The diagnostic parent terminated its process group with SIGTERM at that observation bound. This was an observation run, not a changed or passing ten-second gate. Evidence: `exact.stderr`, `exact.stdout`, `exact.proc.json`.
- During the stall, the helper had one thread, `TracerPid=0`, and slept in `pipe_read`, with syscall `read` on fd3. Its sole child was `/usr/bin/llvm-symbolizer-18 --demangle --inlines --default-arch=x86_64`, sleeping in `poll`. Both used little CPU. Kernel stack reads were denied; `/proc` status, wait channel, and syscall reads succeeded. No debugger or ptrace attachment altered LeakSanitizer.
- Change only `DEBUGINFOD_URLS` to the empty string: three fresh helper processes each exited 86 within the original ten-second bound, in 0.0986, 0.0729, and 0.0764 seconds. Each emitted `LeakSanitizer: detected memory leaks`, `Direct leak of 4096 byte(s)`, and symbolized engine allocation source lines. Evidence: `no-debuginfod.json`.
- Independent confirmation: retain the original inherited URL, set only `LLVM_SYMBOLIZER_OPTS=--no-debuginfod`; the helper exited 86 in 0.0962 seconds with the same required leak report. Evidence: `explicit-no-debuginfod.json`.

The runs were sequential and no debug cache was cleared. They establish the inherited debuginfod dependency on this host, not a broad cold-start timing claim. These probes never invoked CTest or GLFW initialization and do not establish the clean GLFW lifetime half of the harness; that still requires normal focused/full verification after the harness fix.

## Repair scope

Set `DEBUGINFOD_URLS=` only in the harness subprocess environment. Keep local symbolization enabled, the exact suppression set, both subprocess modes, the ten-second subprocess timeout, the required allocation/report text, and expected exit86 unchanged. CMake intentionally selects llvm-symbolizer18 for the earlier BUG-083 driver diagnosis; Clang23 versus symbolizer18 is not required to explain this failure and need not be changed.
