// debug_surface.vert — Lightweight vertex shader for transient debug triangles.
// Reads position, normal, and packed color from BDA buffers.
// No instance/visibility SSBOs — only set=0 camera UBO + push-constant BDA pointers.
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf   { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer NormBuf  { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer ColorBuf { uint v[]; };

layout(push_constant) uniform PushConsts {
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrColors;
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec4 fragColor;

void main() {
    vec3 pos  = PosBuf(push.PtrPositions).v[gl_VertexIndex];
    vec3 norm = NormBuf(push.PtrNormals).v[gl_VertexIndex];
    uint col  = ColorBuf(push.PtrColors).v[gl_VertexIndex];

    gl_Position = camera.proj * camera.view * vec4(pos, 1.0);

    // Epsilon-guarded renormalization with camera-facing fallback.
    float nLen = length(norm);
    if (nLen > 1e-6) {
        fragNormal = norm / nLen;
    } else {
        fragNormal = -vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    }

    fragColor = unpackUnorm4x8(col);
}
