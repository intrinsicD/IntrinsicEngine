module;
#include "RHI.Vulkan.hpp"
#include <glm/glm.hpp>
#include <vector>

export module RHI:Types;

export namespace RHI {
    struct GeometryPipelineSpec {
        static std::vector<VkVertexInputBindingDescription> GetBindings() {
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

        static std::vector<VkVertexInputAttributeDescription> GetAttributes() {
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

    struct CameraBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
    };

    struct MeshPushConstants {
        glm::mat4 model;
        uint32_t textureID;
        uint32_t _pad[3];
    };
}