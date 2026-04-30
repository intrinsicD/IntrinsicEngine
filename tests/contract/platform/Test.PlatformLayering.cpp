#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef INTRINSIC_SOURCE_DIR
#error "INTRINSIC_SOURCE_DIR must be defined for platform layering tests"
#endif

namespace
{
    bool IsSourceFile(const std::filesystem::path& path)
    {
        const auto ext = path.extension().string();
        return ext == ".cpp" || ext == ".cppm" || ext == ".h" || ext == ".hpp" || ext == ".inl";
    }

    bool StartsWithImport(const std::string& line)
    {
        const auto first = line.find_first_not_of(" \t");
        return first != std::string::npos && line.compare(first, 7, "import ") == 0;
    }
}

TEST(PlatformLayering, PlatformDoesNotImportGraphicsOrRuntime)
{
    const auto platformRoot = std::filesystem::path{INTRINSIC_SOURCE_DIR} / "src" / "platform";
    ASSERT_TRUE(std::filesystem::exists(platformRoot)) << platformRoot;

    std::vector<std::string> violations;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(platformRoot))
    {
        if (!entry.is_regular_file() || !IsSourceFile(entry.path()))
            continue;

        std::ifstream file{entry.path()};
        ASSERT_TRUE(file.good()) << entry.path();

        std::string line;
        int lineNo = 0;
        while (std::getline(file, line))
        {
            ++lineNo;
            if (!StartsWithImport(line))
                continue;

            if (line.find("Extrinsic.Graphics") != std::string::npos ||
                line.find("Extrinsic.Runtime") != std::string::npos)
            {
                violations.push_back(entry.path().string() + ":" + std::to_string(lineNo) + ": " + line);
            }
        }
    }

    EXPECT_TRUE(violations.empty()) << testing::PrintToString(violations);
}

