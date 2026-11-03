# ARCH-022 S3: the triple buffer and the append log under ThreadSanitizer, built apart from the main tree (whose
# sanitizer modes are address and undefined). tests/regression/tooling/Test.TsanHarness.py (CTest
# Core.TripleBufferThreadSanitizer) configures, builds and runs it.
cmake_minimum_required(VERSION 3.28)
project(IntrinsicTsanHarness CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(INTRINSIC_SOURCE_DIR "" CACHE PATH "The repository root")
add_executable(tsan_harness TsanHarness.cpp)
target_sources(tsan_harness PRIVATE FILE_SET CXX_MODULES BASE_DIRS ${INTRINSIC_SOURCE_DIR}/src/core
        FILES ${INTRINSIC_SOURCE_DIR}/src/core/Core.TripleBuffer.cppm ${INTRINSIC_SOURCE_DIR}/src/core/Core.AppendLog.cppm)
target_compile_options(tsan_harness PRIVATE -fsanitize=thread -O1 -g)
target_link_options(tsan_harness PRIVATE -fsanitize=thread)
