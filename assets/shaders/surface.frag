//surface.frag
#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTexID;

layout(location = 0) out vec4 outColor;

// Per-face color buffer (optional BDA — when ptrFaceAttr != 0).
layout(buffer_reference, scalar) readonly buffer FaceAttrBuf { uint color[]; };

// Push constant layout must match surface.vert exactly.
layout(push_constant) uniform PushConsts {
    mat4 _unusedModel;
    uint64_t ptrPos;
    uint64_t ptrNorm;
    uint64_t ptrAux;
    uint VisibilityBase;
    float PointSizePx;
    uint64_t ptrFaceAttr;
} push;

// Binding 0 = Camera (UBO), Binding 1 = Bindless Array
// Note: We don't declare Binding 0 here if we don't use it in Frag,
// but usually it's good practice to keep set layouts consistent.
layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    // Epsilon-guarded renormalization: interpolation across a triangle can
    // produce near-zero normals when adjacent vertices have opposing directions.
    float nLen = length(fragNormal);
    vec3 norm = (nLen > 1e-6) ? (fragNormal / nLen) : vec3(0.0, 0.0, 1.0);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Per-face color: when ptrFaceAttr is valid, read per-face packed ABGR color
    // indexed by gl_PrimitiveID. The face color replaces the texture color,
    // but lighting is still applied on top.
    vec4 baseColor;
    if (push.ptrFaceAttr != 0ul)
    {
        FaceAttrBuf fBuf = FaceAttrBuf(push.ptrFaceAttr);
        baseColor = unpackUnorm4x8(fBuf.color[gl_PrimitiveID]);
    }
    else
    {
        baseColor = texture(globalTextures[nonuniformEXT(fragTexID)], fragTexCoord);
    }

    vec3 result = (ambient + diffuse) * baseColor.rgb;

    outColor = vec4(result, baseColor.a);
}
