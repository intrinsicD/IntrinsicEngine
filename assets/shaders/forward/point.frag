#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common/gpu_scene.glsl"
#include "common/point_quad.glsl"

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

void main() {
    // BUG-254 — the point pick keeps and discards the same fragments, at the same depth.
    vec3 color = vColor.rgb;
    float r2;
    float depth;
    if (!GpuPointFragmentCoverage(vPointMode, vViewNormal, vDiscUV, vViewCenter, vViewRadius,
                                  pc.SceneTableBDA, gl_FragCoord.z, color, r2, depth)) {
        discard;
    }
    gl_FragDepth = depth;

    float edge = sqrt(max(r2, 0.0));
    float alpha = 1.0 - smoothstep(0.85, 1.0, edge);

    if (vPointMode == GpuPointMode_Surfel) {
        vec3 n = normalize(vViewNormal);
        float facing = abs(n.z);
        color *= 0.7 + 0.3 * facing;
    }

    outColor = vec4(color, vColor.a * alpha);
}
