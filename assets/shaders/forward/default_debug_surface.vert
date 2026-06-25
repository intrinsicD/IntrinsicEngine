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
// Reads the runtime surface vertex channels authored by the mesh/procedural
// packers (position, optional uv, optional normal, optional packed color) and
// forwards the per-instance material slot, resolved texture coordinates, and
// world-space normal. Clip-space transforms use the current camera matrix
// published through `GpuSceneTable`.

#include "../common/gpu_scene.glsl"

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
layout(location = 6) out vec4 fragVertexColor;
layout(location = 7) flat out uint fragHasVertexColor;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];
    const GpuEntityConfig cfg = GpuEntityConfigRef(scene.EntityConfigBDA).Data[inst.ConfigSlot];

    // The culling indirect command supplies firstIndex + vertexOffset, so
    // gl_VertexIndex is already in managed-buffer vertex units. Channel BDAs
    // point at this geometry's first element, so shader fetches use the local
    // vertex index inside the record.
    const uint vertexIndex = uint(gl_VertexIndex);
    const uint localVertexIndex = vertexIndex - geo.VertexOffset;
    const vec3 localPosition = GpuReadPackedVec3(geo.VertexBufferBDA, localVertexIndex);
    const vec2 localUv = geo.TexcoordBufferBDA != uint64_t(0)
        ? GpuReadPackedVec2(geo.TexcoordBufferBDA, localVertexIndex)
        : vec2(0.0);
    const vec3 authoredNormal = geo.NormalBufferBDA != uint64_t(0)
        ? GpuReadPackedVec3(geo.NormalBufferBDA, localVertexIndex)
        : vec3(0.0, 0.0, 1.0);

    gl_Position = scene.CameraViewProj * dyn.Model * vec4(localPosition, 1.0);
    fragMaterialSlot = inst.MaterialSlot;
    fragUv = localUv;
    fragConfigSlot = inst.ConfigSlot;
    fragVisualizationScalar = cfg.VisDomain == GpuVisualizationDomain_Vertex
        ? GpuVisualizationReadScalar(cfg, localVertexIndex, cfg.ScalarRangeMin)
        : cfg.ScalarRangeMin;
    fragVisualizationColor = cfg.VisDomain == GpuVisualizationDomain_Vertex
        ? GpuVisualizationReadColor(cfg, localVertexIndex, vec4(1.0))
        : vec4(1.0);
    if (geo.ColorBufferBDA != uint64_t(0)) {
        fragVertexColor = unpackUnorm4x8(GpuUIntBufferRef(geo.ColorBufferBDA).Data[localVertexIndex]);
        fragHasVertexColor = 1u;
    } else {
        fragVertexColor = vec4(1.0);
        fragHasVertexColor = 0u;
    }

    const float localNormalLen = length(authoredNormal);
    const vec3 localNormal = (localNormalLen > 1.0e-6)
        ? (authoredNormal / localNormalLen)
        : vec3(0.0, 0.0, 1.0);
    const mat3 normalMatrix = transpose(inverse(mat3(dyn.Model)));
    const vec3 worldNormal = normalMatrix * localNormal;
    const float worldNormalLen = length(worldNormal);
    fragWorldNormal = (worldNormalLen > 1.0e-6)
        ? (worldNormal / worldNormalLen)
        : vec3(0.0, 0.0, 1.0);
}
