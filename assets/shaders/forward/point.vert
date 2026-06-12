#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common/gpu_scene.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

struct PackedVertex {
    float px;
    float py;
    float pz;
    float u;
    float v;
};
layout(buffer_reference, scalar) readonly buffer PackedVertexRef { PackedVertex Data[]; };

layout(location = 0) out vec4 vColor;
layout(location = 1) flat out uint vPointMode;
layout(location = 2) out vec3 vViewNormal;
layout(location = 3) out vec2 vDiscUV;
layout(location = 4) out vec3 vViewCenter;
layout(location = 5) out float vViewRadius;

float SignNotZero(float value) {
    return value < 0.0 ? -1.0 : 1.0;
}

bool HasEncodedNormal(vec2 encoded) {
    return abs(encoded.x) <= 1.0001 && abs(encoded.y) <= 1.0001;
}

vec3 DecodeOctNormal(vec2 encoded) {
    vec3 n = vec3(encoded.xy, 1.0 - abs(encoded.x) - abs(encoded.y));
    if (n.z < 0.0) {
        n.xy = vec2(
            (1.0 - abs(n.y)) * SignNotZero(n.x),
            (1.0 - abs(n.x)) * SignNotZero(n.y));
    }
    float len = length(n);
    return (len > 1.0e-6) ? (n / len) : vec3(0.0, 0.0, 1.0);
}

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

    PackedVertexRef vertices = PackedVertexRef(geo.VertexBufferBDA);
    const uint sourceVertexIndex = uint(gl_VertexIndex) / 6u;
    const uint vertexInQuad = uint(gl_VertexIndex) % 6u;
    const PackedVertex pv = vertices.Data[sourceVertexIndex];

    const uint cornerIndex = uint[6](0u, 1u, 2u, 0u, 2u, 3u)[vertexInQuad];
    const vec2 localOffset = vec2[4](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0))[cornerIndex];

    const vec4 localPos = vec4(pv.px, pv.py, pv.pz, 1.0);
    const vec4 worldPos = dyn.Model * localPos;
    const vec4 viewCenter4 = scene.CameraView * worldPos;
    const vec4 centerClip = scene.CameraViewProj * dyn.Model * localPos;

    const float pointSizePx = clamp(cfg.PointSize, 0.5, 32.0);
    const float radiusPx = pointSizePx * 0.5;
    const float viewportWidth = max(scene.CameraViewportWidth, 1.0);
    const float viewportHeight = max(scene.CameraViewportHeight, 1.0);
    const vec2 clipOffset = localOffset *
        vec2((2.0 * radiusPx) / viewportWidth, (2.0 * radiusPx) / viewportHeight) *
        centerClip.w;

    gl_Position = centerClip + vec4(clipOffset, 0.0, 0.0);
    vPointMode = cfg.PointMode;
    vDiscUV = localOffset;
    vViewCenter = viewCenter4.xyz;

    const float projectionScaleY = abs(scene.CameraProj[1][1]);
    const float depth = max(abs(viewCenter4.z), 1.0e-4);
    const float ndcRadiusY = (2.0 * radiusPx) / viewportHeight;
    vViewRadius = projectionScaleY > 1.0e-6
        ? max((ndcRadiusY * depth) / projectionScaleY, 1.0e-5)
        : 1.0e-3;

    vColor = (cfg.ColorSourceMode == 1u) ? cfg.UniformColor : vec4(1.0);
    vViewNormal = vec3(0.0, 0.0, 1.0);
    if (HasEncodedNormal(vec2(pv.u, pv.v))) {
        const vec3 localNormal = DecodeOctNormal(vec2(pv.u, pv.v));
        const mat3 normalMatrix = transpose(inverse(mat3(dyn.Model)));
        const vec3 worldNormal = normalize(normalMatrix * localNormal);
        const vec3 viewNormal = mat3(scene.CameraView) * worldNormal;
        const float viewNormalLength = length(viewNormal);
        if (viewNormalLength > 1.0e-6) {
            vViewNormal = viewNormal / viewNormalLength;
        }
    }
}
