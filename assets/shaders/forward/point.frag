#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common/gpu_scene.glsl"
#include "common/point_sphere_impostor.glsl"

layout(location = 0) in vec4 vColor;
layout(location = 1) flat in uint vPointMode;
layout(location = 2) in vec3 vViewNormal;
layout(location = 3) in vec2 vDiscUV;
layout(location = 4) in vec3 vViewCenter;
layout(location = 5) in float vViewRadius;
layout(location = 0) out vec4 outColor;

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

float DiscMetric(vec2 uv) {
    if (vPointMode != 2u) {
        return dot(uv, uv);
    }

    vec3 n = normalize(vViewNormal);
    float nz = max(abs(n.z), 0.15);
    float lifted = dot(n.xy, uv) / nz;
    return dot(uv, uv) + lifted * lifted;
}

void main() {
    vec2 uv = vDiscUV;
    float r2 = DiscMetric(uv);
    if (r2 > 1.0) {
        discard;
    }

    gl_FragDepth = gl_FragCoord.z;

    float edge = sqrt(max(r2, 0.0));
    float alpha = 1.0 - smoothstep(0.85, 1.0, edge);
    vec3 color = vColor.rgb;

    if (vPointMode == 1u) {
        const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
        float depth;
        if (!ShadeSphereImpostor(uv, vViewCenter, vViewRadius, scene.CameraProj, color, depth)) {
            discard;
        }
        gl_FragDepth = depth;
    } else if (vPointMode == 2u) {
        vec3 n = normalize(vViewNormal);
        float facing = abs(n.z);
        color *= 0.7 + 0.3 * facing;
    }

    outColor = vec4(color, vColor.a * alpha);
}
