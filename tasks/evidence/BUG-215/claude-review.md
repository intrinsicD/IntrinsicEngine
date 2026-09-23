**PASS.** Importing the owning module explicitly is the right fix. I couldn't show that a build passes: this was a read-only review, and your evidence logs don't have a result yet.

**Why the test couldn't see `operator==`:**
- `FrameResourceId` and its exported `operator==` / `operator!=` are in the `Extrinsic.Graphics.RenderGraph:Resources` partition (`src/graphics/framegraph/Graphics.RenderGraph.Resources.cppm:13-34`).
- `Graphics.RenderGraph.cppm:17` re-exports that partition with `export import :Resources`, so `import Extrinsic.Graphics.RenderGraph;` makes both operators visible.
- `DebugViewSystem.cppm:10` and `FrameRecipe.cppm:18` both import RenderGraph without `export`. FrameRecipe only re-exports `RenderRecipeConfig`. So before the fix, the test could reach the type but could not see the operators. Clang 20's lookup when instantiating gtest's `EXPECT_EQ` doesn't find a friendless namespace-scope operator from a module that was only imported indirectly. The CI log shows exactly this: `gtest.h:1394: invalid operands to binary expression ('const FrameResourceId' and 'const FrameResourceId')`. It's the only error in that log.

**Why the fix is correct and safe:**
- **It's a direct use.** The test compares `FrameResourceId` values itself (lines 78–79 and 95), so importing the module that owns the type follows the usual "import what you use" rule. It doesn't depend on quirks of lookup.
- **It adds nothing new to the build.** `Test.RenderGraphDebugDump.cpp` is in the same object library (`GraphicsRendererCpuUnitTestObjs`, `tests/CMakeLists.txt` ~1940–1946) and already imports `Extrinsic.Graphics.RenderGraph`. The module is already a dependency of that target, so the change adds no new link or build-order edge.
- **Nothing else changes.** Production code and the assertions are untouched.

**Rejected alternative:** changing `DebugViewSystem` or `FrameRecipe` to `export import` RenderGraph would also work. But it would widen those modules' public surface just to fix one test, so the test-only import is the better choice.

**Still open:** `tasks/evidence/BUG-215/build.log` stops at `[156/246]`, and there's no log from the focused test run yet. Don't mark BUG-215 or PR1044 complete until the local Clang 20 build finishes and the focused `DebugViewSystem` tests pass with logs in that directory.
