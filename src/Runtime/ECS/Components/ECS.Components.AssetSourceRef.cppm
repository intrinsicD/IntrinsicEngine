module;
#include <string>

export module ECS:Components.AssetSourceRef;

export namespace ECS::Components::AssetSourceRef
{
    // Tracks the filesystem path that an entity's geometry or data was loaded from.
    // Used by scene serialization to know which asset file to re-import on scene load.
    //
    // Attached by Engine::LoadDroppedAsset() and SpawnModel() when geometry is loaded
    // from an external file. Not present on runtime-created entities (procedural
    // geometry, demo point clouds, etc.) — the serializer handles absence gracefully.
    struct Component
    {
        std::string SourcePath;
    };
}
