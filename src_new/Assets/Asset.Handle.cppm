module;

#include <entt/entity/entity.hpp>

export module Extrinsic.Asset.Handle;

namespace Extrinsic::Assets
{
    export using AssetHandle = entt::entity;
    export constexpr AssetHandle INVALID_ASSET_HANDLE = entt::null;
}
