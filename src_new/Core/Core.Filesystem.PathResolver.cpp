module;

#include <filesystem>
#include <mutex>
#include <vector>

module Extrinsic.Core.Filesystem.PathResolver;

import Extrinsic.Core.Logging;

namespace Extrinsic::Core::Filesystem
{
    namespace
    {
        [[nodiscard]] bool HasAssetsDir(const std::filesystem::path& root)
        {
            std::error_code ec;
            const auto p = root / "assets";
            return std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec);
        }

        [[nodiscard]] std::filesystem::path CanonicalOrAbsolute(const std::filesystem::path& p)
        {
            std::error_code ec;
            auto c = std::filesystem::weakly_canonical(p, ec);
            if (!ec) return c;
            return std::filesystem::absolute(p, ec);
        }

        [[nodiscard]] std::filesystem::path GetExecutablePath()
        {
#if defined(__linux__)
            std::error_code ec;
            auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
            if (!ec && !p.empty())
                return CanonicalOrAbsolute(p);
            return {};
#elif defined(_WIN32)
            wchar_t buffer[MAX_PATH] = {};
            const DWORD len = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
            if (len == 0 || len >= std::size(buffer)) return {};
            return CanonicalOrAbsolute(std::filesystem::path(buffer));
#elif defined(__APPLE__)
            uint32_t size = 0;
            (void)_NSGetExecutablePath(nullptr, &size);
            if (size == 0) return {};

            std::string buf(size, '\0');
            if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
            return CanonicalOrAbsolute(std::filesystem::path(buf));
#else
            return {};
#endif
        }

        [[nodiscard]] std::filesystem::path ResolveRoot()
        {
            // 1) Dev mode: Prefer the source root from CMake, but only if it actually contains assets/.
#ifdef ENGINE_ROOT_DIR
            {
                const std::filesystem::path macroRoot = CanonicalOrAbsolute(std::filesystem::path(ENGINE_ROOT_DIR));
                if (!macroRoot.empty() && HasAssetsDir(macroRoot))
                    return macroRoot;
            }
#endif

            // 2) Installed/packaged mode: Search relative to the executable.
            //    Candidate order: <exe/..>, <exe/../..>, <exe>, plus a couple common layouts.
            //    (Bounded search to keep this deterministic.)
            {
                const auto exe = GetExecutablePath();
                const auto exeDir = exe.empty() ? std::filesystem::path{} : exe.parent_path();
                if (!exeDir.empty())
                {
                    const std::vector<std::filesystem::path> candidates = {
                        exeDir.parent_path(),
                        exeDir.parent_path().parent_path(),
                        exeDir,
                        exeDir / ".." / "share" / "IntrinsicEngine",
                        exeDir.parent_path() / "share" / "IntrinsicEngine",
                    };

                    for (const auto& c : candidates)
                    {
                        const auto root = CanonicalOrAbsolute(c);
                        if (!root.empty() && HasAssetsDir(root))
                            return root;
                    }
                }
            }

            // 3) Legacy behavior: Look in/around the current working directory.
            {
                const auto cwd = std::filesystem::current_path();
                if (HasAssetsDir(cwd)) return cwd;
                if (HasAssetsDir(cwd.parent_path())) return cwd.parent_path();
            }

            // 4) Final fallback: return CWD (callers will log missing file errors)
            return std::filesystem::current_path();
        }
    }

    inline std::filesystem::path GetRoot()
    {
                // Cache result; root doesn't change during process lifetime.
        static std::once_flag s_Once;
        static std::filesystem::path s_Root;

        std::call_once(s_Once, []
        {
            s_Root = ResolveRoot();
        });

        return s_Root;
    }

    std::string ResolveShaderPathOrExit(ShaderPathLookup lookup, Hash::StringID name)
    {
        auto path = lookup(name);
        if (!path)
        {
            Log::Error("CRITICAL: Missing shader configuration for ID: 0x{:08X}", name.Value);
            std::exit(-1);
        }
        return GetShaderPath(*path);
    }

    std::string GetAssetPath(const std::string& relativePath)
    {
        auto path = GetRoot() / "assets" / relativePath;
        return path.string();
    }

    std::string GetShaderPath(const std::string& relativePath)
    {
     // Normalize input like "shaders/foo.spv".
        const std::filesystem::path rel = relativePath;

        // Absolute path passthrough.
        if (rel.is_absolute() && std::filesystem::exists(rel))
            return rel.string();

        // 0) Try relative to executable location first.
        // This keeps runtime robust even when IDE run configurations use an
        // unexpected working directory.
        {
            const auto exe = GetExecutablePath();
            const auto exeDir = exe.empty() ? std::filesystem::path{} : exe.parent_path();
            if (!exeDir.empty())
            {
                const std::vector<std::filesystem::path> candidates = {
                    exeDir / rel,
                    exeDir.parent_path() / "bin" / rel,
                    exeDir.parent_path() / rel,
                    exeDir.parent_path().parent_path() / "bin" / rel,
                };

                for (const auto& c : candidates)
                {
                    if (std::filesystem::exists(c))
                        return c.string();
                }
            }
        }

        // 1) Try as-given relative to CWD.
        if (std::filesystem::exists(rel))
        {
            return rel.string();
        }

        // 2) Common dev: launched from build dir (e.g. cmake-build-debug/)
        //    shaders are copied to <build>/bin/shaders/*.
        {
            const auto p = std::filesystem::path("bin") / rel;
            if (std::filesystem::exists(p)) return p.string();
        }

        // 3) Common dev: launched from repo root, shaders live in cmake-build-*/bin/shaders.
        //    Search a couple of typical build folders.
        {
            static const std::vector<std::filesystem::path> buildDirs = {
                "cmake-build-debug", "cmake-build-release", "build", "out/build"
            };

            for (const auto& buildDir : buildDirs)
            {
                const auto p = std::filesystem::path(buildDir) / "bin" / rel;
                if (std::filesystem::exists(p)) return p.string();
            }
        }

        // 4) When launched from bin/, go up.
        {
            const auto p = std::filesystem::path("..") / "bin" / rel;
            if (std::filesystem::exists(p)) return p.string();
        }

        // 5) Last resort: allow SPV to be placed in assets/ (not typical, but convenient).
        {
            auto asset = GetAssetPath(relativePath);
            if (std::filesystem::exists(asset)) return asset;
        }

        return relativePath; // let ShaderModule print a clear error
    }

    std::string GetAbsolutePath(const std::string& relativePath)
    {
        auto path = std::filesystem::path(relativePath);
        return CanonicalOrAbsolute(path).string();
    }
}