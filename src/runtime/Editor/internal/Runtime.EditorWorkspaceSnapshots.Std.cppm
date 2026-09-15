// Compiles the snapshot standard-library declarations without editor imports.
// Keeps their header definitions out of the snapshot interface's serialization.
module;

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.Private.EditorSnapshotStd;

export namespace Extrinsic::Runtime::SnapshotStd
{
    using std::array;
    using std::shared_ptr;
    using std::optional;
    using std::nullopt;
    using std::string;
    using std::vector;
}
