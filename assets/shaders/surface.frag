//surface.frag
#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTexID;
layout(location = 3) in vec4 fragVertexColor;

layout(location = 0) out vec4 outColor;

// Per-face color buffer (optional BDA — when PtrFaceAttr != 0).
layout(buffer_reference, scalar) readonly buffer FaceAttrBuf { uint color[]; };

// Push constant layout must match surface.vert exactly.
layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;
    uint     VisibilityBase;
    float    PointSizePx;
    uint64_t PtrFaceAttr;
    uint64_t PtrVertexAttr;
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

    // Color priority: per-vertex color > per-face color > texture.
    //
    // Per-vertex colors are interpolated across the triangle by the rasterizer,
    // providing smooth scalar field / RGB visualization on mesh surfaces.
    // Per-face colors are flat (one color per triangle via gl_PrimitiveID).
    vec4 baseColor;
    if (push.PtrVertexAttr != 0ul)
    {
        baseColor = fragVertexColor;
    }
    else if (push.PtrFaceAttr != 0ul)
    {
        FaceAttrBuf fBuf = FaceAttrBuf(push.PtrFaceAttr);
        baseColor = unpackUnorm4x8(fBuf.color[gl_PrimitiveID]);
    }
    else
    {
        baseColor = texture(globalTextures[nonuniformEXT(fragTexID)], fragTexCoord);
    }

    vec3 result = (ambient + diffuse) * baseColor.rgb;

    outColor = vec4(result, baseColor.a);
}
