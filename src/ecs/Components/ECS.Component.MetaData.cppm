module;

#include <string>

export module Extrinsic.ECS.Component.MetaData;

export namespace Extrinsic::ECS::Components
{
    struct MetaData
    {
        std::string EntityName;
    };
}
