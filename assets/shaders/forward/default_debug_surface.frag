#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-031A — canonical missing-material fallback fragment shader.
//
// Reads the per-instance MaterialBuffer slot at set = 3, binding = 0 and
// multiplies BaseColorFactor by the interpolated vertex colour. No textures,
// no lighting, no normals — the unlit recorded colour is the only output.

#include "../common/gpu_scene.glsl"

layout(std430, set = 3, binding = 0) readonly buffer MaterialBuffer {
    GpuMaterialSlot Slots[];
} materials;

layout(location = 0) in vec4 fragVertexColor;
layout(location = 1) flat in uint fragMaterialSlot;

layout(location = 0) out vec4 outColor;

void main() {
    const GpuMaterialSlot mat = materials.Slots[fragMaterialSlot];
    outColor = vec4(mat.BaseColorFactor.rgb * fragVertexColor.rgb, mat.BaseColorFactor.a);
}
