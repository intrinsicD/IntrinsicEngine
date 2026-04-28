module;
#include <string>

export module ECS:Components.NameTag;

export namespace ECS::Components::NameTag
{
    struct Component
    {
        std::string Name = "Unnamed Entity";
    };
}