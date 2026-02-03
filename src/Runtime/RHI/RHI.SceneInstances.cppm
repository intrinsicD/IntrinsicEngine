module;
#include <cstdint>
#include <glm/glm.hpp>

export module RHI:SceneInstances;

export namespace RHI
{
    // std430-friendly. Mat4 is 16-byte aligned with glm defaults in this project.
    struct GpuInstanceData
    {
        glm::mat4 Model{1.0f};
        uint32_t TextureID = 0;
        uint32_t EntityID = 0;
        uint32_t GeometryID = 0;
        uint32_t Padding1 = 0;
    };

    // Stage 1: identity map for visibility indirection.
    // Stage 3: compute shader populates this compacted addressing.
    using GpuVisibleInstanceID = uint32_t;
}
