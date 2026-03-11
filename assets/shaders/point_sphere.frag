// point_sphere.frag — Impostor sphere fragment shader.
//
// Ray-traces a sphere against the billboard quad to compute:
// - Per-pixel normal for Phong shading
// - Correct gl_FragDepth for sphere-sphere depth occlusion
//
// This mode disables early-Z optimization but provides geometrically correct
// sphere rendering without tessellation.
//
// Part of the PointPass pipeline array in the three-pass rendering architecture.

#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;
layout(location = 2) in vec3 fragViewCenter;   // view-space sphere center
layout(location = 3) in float fragRadiusView;  // view-space sphere radius

layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;
    float    PointSize;
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     Color;
    uint     Flags;
    uint64_t PtrRadii;
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    // Discard fragments outside the projected sphere footprint.
    float r2 = dot(fragDiscUV, fragDiscUV);
    if (r2 > 1.0) discard;

    // Front hemisphere of the centered unit sphere.
    float zSphere = sqrt(max(1.0 - r2, 0.0));
    vec3 N = vec3(fragDiscUV, zSphere);

    // Exact sphere surface point in view space.
    vec3 surfaceViewPos = fragViewCenter + vec3(fragDiscUV * fragRadiusView,
                                                zSphere * fragRadiusView);

    // Camera looks down -Z in the engine's right-handed view space.
    if (surfaceViewPos.z >= -1e-6) discard;

    // Project the true sphere surface point to obtain exact Vulkan depth.
    vec4 clipPos = camera.proj * vec4(surfaceViewPos, 1.0);
    if (clipPos.w <= 0.0) discard;

    float depth = clipPos.z / clipPos.w;
    if (depth < 0.0 || depth > 1.0) discard;
    gl_FragDepth = depth;

    // Simple Phong shading in view space.
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float ambient = 0.15;
    float diffuse = max(abs(dot(N, lightDir)), 0.0);

    vec3 viewDir = normalize(-surfaceViewPos);
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(N, halfVec), 0.0);
    float specular = pow(NdotH, 32.0) * 0.3;

    vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse) + vec3(specular);
    lit = clamp(lit, 0.0, 1.0);

    outColor = vec4(lit, fragColor.a);
}
