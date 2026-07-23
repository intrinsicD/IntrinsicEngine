// GRAPHICS-036C — structural composition coverage for the runtime-owned
// RenderWorldPool. Runtime behavior and slot lifecycle are covered directly by
// Test.RenderWorldPool.cpp; this file pins the two private Engine wiring edges
// without keeping a public pool/statistics forwarding accessor for tests.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace
{
    [[nodiscard]] std::filesystem::path RepoRoot()
    {
        return std::filesystem::path(__FILE__)
            .parent_path()
            .parent_path()
            .parent_path()
            .parent_path();
    }

    [[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        EXPECT_TRUE(input.good()) << "Unable to open: " << path.string();
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }
}

TEST(RenderWorldPoolEngineWiring, ConfiguresPrivatePoolFromExtractionMode)
{
    const auto root = RepoRoot();
    const std::string engine =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const std::string service =
        ReadFile(root / "src/runtime/Runtime.RenderExtractionService.cpp");

    EXPECT_NE(engine.find(
                  "m_Impl->m_RenderExtractionService.ConfigurePool(\n"
                  "            m_Impl->m_Config.Render.SynchronousExtraction);"),
              std::string::npos);
    EXPECT_NE(service.find(
                  "synchronousExtraction ? 1u : RenderWorldPool::kDefaultBuffers"),
              std::string::npos);
}

TEST(RenderWorldPoolEngineWiring, FrameLoopOwnsPoolLifecycleWithoutPublicForwarders)
{
    const auto root = RepoRoot();
    const std::string engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const std::string engine =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");

    EXPECT_NE(engine.find("m_Impl->m_RenderExtractionService.Pool(),"),
              std::string::npos);
    EXPECT_NE(engine.find(
                  "m_Impl->m_RenderExtractionService.ReleaseFrontSlot("),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetRenderWorldPool"), std::string::npos);
    EXPECT_EQ(engineInterface.find("GetLastRenderExtractionStats"),
              std::string::npos);
}
