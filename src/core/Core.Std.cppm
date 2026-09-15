// Shared standard declarations for the renderer and workspace snapshot interfaces.
// Keeps merged header definitions out of those measured serialization hotspots.
module;

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Core.Std;

export namespace Extrinsic::Core::Std
{
    using std::array;
    using std::function;
    using std::shared_ptr;
    using std::unique_ptr;
    using std::optional;
    using std::span;
    using std::nullopt;
    using std::string;
    using std::vector;
}
