module;

#include <string>
#include <functional>
#include <optional>

export module Extrinsic.Core.Filesystem.PathResolver;

import Extrinsic.Core.Hash;
import Extrinsic.Core.Error;

namespace Extrinsic::Core::Filesystem
{
    using ShaderPathLookup = std::function<std::optional<std::string>(Hash::StringID)>;
    export [[nodiscard]] Expected<std::string> TryResolveShaderPath(ShaderPathLookup lookup, Hash::StringID name);

    export std::string GetAssetPath(const std::string& relativePath);

    export std::string GetShaderPath(const std::string& relativePath);

    export std::string GetAbsolutePath(const std::string& relativePath);
}
