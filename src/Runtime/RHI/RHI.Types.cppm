module;
#include "RHI.Vulkan.hpp"
#include <glm/glm.hpp>
#include <vector>

export module RHI:Types;

export namespace RHI
{
    struct GeometryPipelineSpec
    {
        static std::vector<VkVertexInputBindingDescription> GetBindings()
        {
            std::vector<VkVertexInputBindingDescription> bindings(3);

            // Binding 0: Positions
            bindings[0].binding = 0;
            bindings[0].stride = sizeof(glm::vec3);
            bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            // Binding 1: Normals
            bindings[1].binding = 1;
            bindings[1].stride = sizeof(glm::vec3);
            bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            // Binding 2: Aux (Vec4)
            bindings[2].binding = 2;
            bindings[2].stride = sizeof(glm::vec4);
            bindings[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindings;
        }

        static std::vector<VkVertexInputAttributeDescription> GetAttributes()
        {
            std::vector<VkVertexInputAttributeDescription> attributes(3);

            // Location 0: Position
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[0].offset = 0;

            // Location 1: Normal
            attributes[1].binding = 1;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[1].offset = 0;

            // Location 2: Aux (UV + Extra)
            attributes[2].binding = 2;
            attributes[2].location = 2;
            attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributes[2].offset = 0;

            return attributes;
        }
    };

    struct CameraBufferObject
    {
        alignas(16) glm::mat4 View;
        alignas(16) glm::mat4 Proj;
    };

    struct MeshPushConstants
    {
        glm::mat4 Model;
        uint64_t PtrPositions; // Pointer to start of Positions block
        uint64_t PtrNormals; // Pointer to start of Normals block
        uint64_t PtrAux; // Pointer to start of Aux block
        uint32_t VisibilityBase; // Base offset into VisibleRemap[] for multi-geometry batching
        float    PointSizePx = 1.0f; // Used by Forward pass when drawing point-list topology.
        uint32_t _pad[2];
    };

    struct VertexInputDescription
    {
        std::vector<VkVertexInputBindingDescription> Bindings;
        std::vector<VkVertexInputAttributeDescription> Attributes;
        VkPipelineVertexInputStateCreateFlags Flags = 0;
    };

    struct StandardLayoutFactory
    {
        static VertexInputDescription Get()
        {
            VertexInputDescription desc;
            // Binding 0: Positions
            desc.Bindings.push_back({0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
            // Binding 1: Normals
            desc.Bindings.push_back({1, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
            // Binding 2: Aux (Vec4)
            desc.Bindings.push_back({2, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX});

            //Attribute 0: Position
            desc.Attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            //Attribute 1: Normal
            desc.Attributes.push_back({1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0});
            //Attribute 2: Aux (UV + Extra)
            desc.Attributes.push_back({2, 2, VK_FORMAT_R32G32B32A32_SFLOAT, 0});

            return desc;
        }
    };
}
