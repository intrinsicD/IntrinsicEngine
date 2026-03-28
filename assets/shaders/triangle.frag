//triangle.frag
#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTexID;

layout(location = 0) out vec4 outColor;

// Per-face color buffer (optional BDA — when PtrFaceAttr != 0).
layout(buffer_reference, scalar) readonly buffer FaceAttrBuf { uint color[]; };

// Push constant layout must match triangle.vert exactly.
layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;
    uint     VisibilityBase;
    float    PointSizePx;
    uint64_t PtrFaceAttr;
} push;

// Binding 0 = Camera + Lighting (UBO), Binding 1 = Bindless Array
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

void main() {
    vec3 lightDir = normalize(camera.lightDirAndIntensity.xyz);
    float lightIntensity = camera.lightDirAndIntensity.w;
    vec3 lColor = camera.lightColor.xyz * lightIntensity;
    float ambientStrength = camera.ambientColorAndIntensity.w;
    vec3 ambient = ambientStrength * camera.ambientColorAndIntensity.xyz;

    vec3 norm = normalize(fragNormal);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lColor;

    // Per-face color: when PtrFaceAttr is valid, read per-face packed ABGR color
    // indexed by gl_PrimitiveID. The face color replaces the texture color,
    // but lighting is still applied on top.
    vec4 baseColor;
    if (push.PtrFaceAttr != 0ul)
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
