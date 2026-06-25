#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common/gpu_scene.glsl"

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) flat out uint vInstanceSlot;
layout(location = 3) flat out uint vEntityId;
layout(location = 4) out float vVisualizationScalar;
layout(location = 5) out vec4 vVisualizationColor;

void main() {
    const uint instanceSlot = gl_InstanceIndex;

    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    GpuInstanceStaticRef instanceStatic = GpuInstanceStaticRef(scene.InstanceStaticBDA);
    GpuInstanceDynamicRef instanceDynamic = GpuInstanceDynamicRef(scene.InstanceDynamicBDA);
    GpuGeometryRecordRef geometryRecords = GpuGeometryRecordRef(scene.GeometryRecordBDA);
    GpuEntityConfigRef entityConfigs = GpuEntityConfigRef(scene.EntityConfigBDA);

    const GpuInstanceStatic inst = instanceStatic.Data[instanceSlot];
    const GpuInstanceDynamic dyn = instanceDynamic.Data[instanceSlot];
    const GpuGeometryRecord geo = geometryRecords.Data[inst.GeometrySlot];
    const GpuEntityConfig cfg = entityConfigs.Data[inst.ConfigSlot];

    // The culling indirect command supplies firstIndex + vertexOffset, so
    // gl_VertexIndex is already in managed-buffer vertex units. Channel BDAs
    // point at this geometry's first element, so fetch with the local index.
    const uint vertexIndex = uint(gl_VertexIndex);
    const uint localVertexIndex = vertexIndex - geo.VertexOffset;

    vec3 localPos = GpuReadPackedVec3(geo.VertexBufferBDA, localVertexIndex);
    vec4 worldPos = dyn.Model * vec4(localPos, 1.0);

    gl_Position = camera.proj * camera.view * worldPos;

    vec3 localNormal = geo.NormalBufferBDA != uint64_t(0)
        ? GpuReadPackedVec3(geo.NormalBufferBDA, localVertexIndex)
        : vec3(0.0, 0.0, 1.0);
    const float localNormalLength = length(localNormal);
    localNormal = (localNormalLength > 1.0e-6) ? (localNormal / localNormalLength) : vec3(0.0, 0.0, 1.0);
    const mat3 normalMatrix = transpose(inverse(mat3(dyn.Model)));
    vec3 worldNormal = normalMatrix * localNormal;
    const float worldNormalLength = length(worldNormal);
    vWorldNormal = (worldNormalLength > 1.0e-6) ? (worldNormal / worldNormalLength) : vec3(0.0, 0.0, 1.0);
    vUv = geo.TexcoordBufferBDA != uint64_t(0)
        ? GpuReadPackedVec2(geo.TexcoordBufferBDA, localVertexIndex)
        : vec2(0.0);
    vInstanceSlot = instanceSlot;
    vEntityId = inst.EntityID;
    vVisualizationScalar = cfg.VisDomain == GpuVisualizationDomain_Vertex
        ? GpuVisualizationReadScalar(cfg, localVertexIndex, cfg.ScalarRangeMin)
        : cfg.ScalarRangeMin;
    vVisualizationColor = cfg.VisDomain == GpuVisualizationDomain_Vertex
        ? GpuVisualizationReadColor(cfg, localVertexIndex, vec4(1.0))
        : vec4(1.0);
}
