module;
#include <filesystem>
#include <string>
#include <optional>

export module Core.Filesystem;

export namespace Core::Filesystem {
    
    std::filesystem::path GetRoot() {
        // 1. Check if "assets" exists in current working directory (Production/Binary Release)
        if (std::filesystem::exists("assets")) {
            return std::filesystem::current_path();
        }

        // 2. Check if we are in "bin" and need to go up (Common dev scenario)
        if (std::filesystem::exists("../assets")) {
            return std::filesystem::current_path().parent_path();
        }

        // 3. Fallback: Use the hardcoded CMake source path (Debug / Research only)
        // This ensures it works even if you run it from /tmp/
#ifdef ENGINE_ROOT_DIR
        return std::filesystem::path(ENGINE_ROOT_DIR);
#else
        return std::filesystem::current_path(); // Pray
#endif
    }

    std::string GetAssetPath(const std::string& relativePath) {
        auto path = GetRoot() / relativePath;
        return path.string();
    }
}