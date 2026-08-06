# Concurrent CTest discovery diagnostic

The first evidence pass launched the focused and complete CTest selectors at
the same time against `build/ci`. Both selectors passed, but this build uses
GoogleTest `PRE_TEST` discovery, so the two CTest processes concurrently wrote
the same generated discovery files. The immediately following live routing
receipts therefore failed on duplicate `IntrinsicCoreWrapperUnitTests`
registrations.

A serial `cmake --preset ci` regenerated the discovery includes and the live
CPU routing check passed without any source change. The replacement `v2`
receipts run configure, focused CTest, complete CTest, and routing serially.
The original failed receipts and raw logs are retained in this directory as
diagnostic evidence. Their JSON `stdout_path` and `stderr_path` fields record
the original pre-archive `commands/` locations; the adjacent files are the
hash-identical archived logs. Do not run CTest processes concurrently against
the same PRE_TEST-discovery build tree.
