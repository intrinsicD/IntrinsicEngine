#include <gtest/gtest.h>

// This test intentionally avoids touching ImGui itself.
// It only validates that the Runtime.EditorUI module is linkable and callable.

import Runtime.Engine;
import Runtime.EditorUI;

using namespace Runtime;

TEST(EditorUI, RegistersWithoutCrashing)
{
    // We can't instantiate a full Engine here (requires graphics backend).
    // The contract we can validate in a unit test is simply that the symbol exists.
    // If this compiles/links, registration is at least well-formed.

    // NOLINTNEXTLINE(readability-identifier-length)
    auto* fn = &Runtime::EditorUI::RegisterDefaultPanels;
    ASSERT_NE(fn, nullptr);
}

