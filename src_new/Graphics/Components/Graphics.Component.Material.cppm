module;

#include <glm/glm.hpp>

export module Extrinsic.Graphics.Component.Material;

export namespace Extrinsic::Graphics::Components
{
    struct MaterialInstance
    {
        glm::vec4 BaseColorFactor{1.0f};
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;

        // Bindless Indices (0 = default/error texture)
        uint32_t AlbedoID = 0;
        uint32_t NormalID = 0;
        uint32_t MetallicRoughnessID = 0;
    };
}