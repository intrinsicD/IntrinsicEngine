module;

#include <cstdint>

export module Extrinsic.ECS.Components.AssetInstance;

namespace Extrinsic::ECS::Components::AssetInstance
{
    struct Source
    {
        std::uint32_t AssetId;
    };
}