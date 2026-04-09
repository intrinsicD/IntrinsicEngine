// Graphics.ShaderCompiler.cpp
// Standalone shader compilation utility (not module-exported).
#include <filesystem>
#include <string>

import Core.Logging;
import Core.Process;

namespace Graphics::Internal
{
    bool CompileShader(const std::filesystem::path& srcPath,
                       const std::filesystem::path& dstPath,
                       const std::string& includeDir)
    {
        Core::Process::ProcessConfig config;
        config.Executable = "glslc";
        config.Args = {srcPath.string(), "-o", dstPath.string(), "--target-env=vulkan1.3"};
        if (!includeDir.empty())
        {
            config.Args.push_back("-I");
            config.Args.push_back(includeDir);
        }
        config.Timeout = std::chrono::milliseconds(30'000);
        config.CaptureOutput = true;

        const auto result = Core::Process::Run(config);

        if (result.SpawnFailed)
        {
            Core::Log::Error("[ShaderCompiler] Failed to spawn glslc: {}", result.StdErr);
            return false;
        }

        if (result.TimedOut)
        {
            Core::Log::Error("[ShaderCompiler] glslc timed out compiling: {}", srcPath.string());
            return false;
        }

        if (result.ExitCode != 0)
        {
            if (!result.StdErr.empty())
                Core::Log::Error("[ShaderCompiler] glslc stderr:\n{}", result.StdErr);
            return false;
        }

        return true;
    }
}
