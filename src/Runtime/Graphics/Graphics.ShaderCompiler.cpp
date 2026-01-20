// Graphics.ShaderCompiler.cpp
// (Internal implementation detail, doesn't need its own module export usually)
#include <cstdlib>
#include <filesystem>
#include <format>

namespace Graphics::Internal
{
    bool CompileShader(const std::filesystem::path& srcPath, const std::filesystem::path& dstPath)
    {
        // Construct command: glslc "src" -o "dst"
        // Ensure glslc is in your PATH
        std::string cmd = std::format("glslc \"{}\" -o \"{}\"", srcPath.string(), dstPath.string());

        int result = std::system(cmd.c_str());
        return result == 0;
    }
}