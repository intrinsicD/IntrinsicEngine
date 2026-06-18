#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-031A — canonical missing-material fallback vertex shader.
//
// BDA-only contract: the promoted Vulkan pipeline layout binds only the
// global bindless descriptor set at set 0 (`setLayoutCount = 1`), so no
// per-frame camera UBO or material SSBO descriptor is available here.
// All data is read through `GpuScenePushConstants::SceneTableBDA` and the
// chain of `buffer_reference` pointers declared in `common/gpu_scene.glsl`.
//
// Reads the runtime surface vertex buffer authored by the mesh/procedural
// packers (vec3 position + vec2 uv + vec3 normal = 32 bytes/vertex) and
// forwards the per-instance material slot, resolved texture coordinates, and
// world-space normal. Clip-space transforms use the current camera matrix
// published through `GpuSceneTable`.

#include "../common/gpu_scene.glsl"

struct SurfaceVertex {
    vec3 Position;
    vec2 UV;
    vec3 Normal;
};

layout(buffer_reference, scalar) readonly buffer SurfaceVertexRef {
    SurfaceVertex Data[];
};

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat out uint fragMaterialSlot;
layout(location = 1) out vec2 fragUv;
layout(location = 2) out vec3 fragWorldNormal;
layout(location = 3) flat out uint fragConfigSlot;
layout(location = 4) out float fragVisualizationScalar;
layout(location = 5) out vec4 fragVisualizationColor;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];
    const GpuEntityConfig cfg = GpuEntityConfigRef(scene.EntityConfigBDA).Data[inst.ConfigSlot];

    // The culling indirect command supplies firstIndex + vertexOffset, so
    // gl_VertexIndex is already in managed-buffer vertex units.
    const uint vertexIndex = uint(gl_VertexIndex);
    const SurfaceVertex v = SurfaceVertexRef(geo.VertexBufferBDA).Data[vertexIndex];

    gl_Position = scene.CameraViewProj * dyn.Model * vec4(v.Position, 1.0);
    fragMaterialSlot = inst.MaterialSlot;
    fragUv = v.UV;
    fragConfigSlot = inst.ConfigSlot;
    fragVisualizationScalar = cfg.VisDomain == GpuVisualizationDomain_Vertex
        ? GpuVisualizationReadScalar(cfg, vertexIndex, cfg.ScalarRangeMin)
        : cfg.ScalarRangeMin;
    fragVisualizationColor = cfg.VisDomain == GpuVisualizationDomain_Vertex
        ? GpuVisualizationReadColor(cfg, vertexIndex, vec4(1.0))
        : vec4(1.0);

    const float localNormalLen = length(v.Normal);
    const vec3 localNormal = (localNormalLen > 1.0e-6)
        ? (v.Normal / localNormalLen)
        : vec3(0.0, 0.0, 1.0);
    const mat3 normalMatrix = transpose(inverse(mat3(dyn.Model)));
    const vec3 worldNormal = normalMatrix * localNormal;
    const float worldNormalLen = length(worldNormal);
    fragWorldNormal = (worldNormalLen > 1.0e-6)
        ? (worldNormal / worldNormalLen)
        : vec3(0.0, 0.0, 1.0);
}
