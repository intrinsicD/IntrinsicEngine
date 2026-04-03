//deferred_lighting.frag — Fullscreen deferred lighting composition.
//
// Reads G-buffer (SceneNormal, Albedo, Material0) + SceneDepth and produces
// final lit color into SceneColorHDR. Uses the same Blinn-Phong model as the
// forward surface.frag for visual parity.
//
// Paired with post_fullscreen.vert (fullscreen triangle, no vertex buffers).
#version 460

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 outColor;

// G-buffer inputs (set = 0).
layout(set = 0, binding = 0) uniform sampler2D uNormal;    // SceneNormal (RGBA16F)
layout(set = 0, binding = 1) uniform sampler2D uAlbedo;    // Albedo (RGBA8)
layout(set = 0, binding = 2) uniform sampler2D uMaterial;  // Material0 (RGBA16F)
layout(set = 0, binding = 3) uniform sampler2D uDepth;     // SceneDepth

// Global set (set = 1): Camera UBO + Shadow Atlas.
layout(set = 1, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 _lightDirAndIntensity;
    vec4 _lightColor;
    vec4 _ambientColorAndIntensity;
    mat4 shadowCascadeMatrices[4];
    vec4 shadowCascadeSplitsAndCount;
    vec4 shadowBiasAndFilter;
} camera;

layout(set = 1, binding = 1) uniform sampler2DShadow shadowAtlas;

layout(push_constant) uniform Push
{
    mat4  InvViewProj;    // Inverse of (Proj * View) for position reconstruction
    vec4  ClearColor;     // Background clear color (matches forward path)
    vec4  LightDirAndIntensity;    // xyz = direction to light, w = intensity
    vec4  LightColor;              // xyz = light color
    vec4  AmbientColorAndIntensity; // xyz = ambient color, w = ambient intensity
} pc;

#include "shadow_sampling.glsl"

vec3 ReconstructWorldPos(vec2 uv, float depth)
{
    // NDC: x,y in [-1,1], z in [0,1] (Vulkan convention).
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = pc.InvViewProj * ndc;
    return world.xyz / world.w;
}

void main()
{
    float depth = texture(uDepth, vUV).r;

    // Sky pixels: depth == 1.0 (cleared value) → output clear color.
    if (depth >= 1.0)
    {
        outColor = pc.ClearColor;
        return;
    }

    vec3 normal   = texture(uNormal, vUV).rgb;
    vec4 albedo   = texture(uAlbedo, vUV);
    vec4 material = texture(uMaterial, vUV);

    // Epsilon-guarded renormalization (may have been interpolated by MSAA).
    float nLen = length(normal);
    vec3 norm = (nLen > 1e-6) ? (normal / nLen) : vec3(0.0, 0.0, 1.0);

    // Blinn-Phong — matches forward surface.frag.
    vec3 lightDir = normalize(pc.LightDirAndIntensity.xyz);
    float lightIntensity = pc.LightDirAndIntensity.w;
    vec3 lColor = pc.LightColor.xyz * lightIntensity;
    float ambientStrength = pc.AmbientColorAndIntensity.w;
    vec3 ambient = ambientStrength * pc.AmbientColorAndIntensity.xyz;
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lColor;

    // Reconstruct world position for shadow sampling.
    vec3 worldPos = ReconstructWorldPos(vUV, depth);

    // Shadow attenuation (PCF cascaded shadow maps).
    float shadow = ComputeShadowFactor(
        shadowAtlas, worldPos, norm,
        camera.view, camera.proj,
        camera.shadowCascadeMatrices,
        camera.shadowCascadeSplitsAndCount,
        camera.shadowBiasAndFilter);

    vec3 result = (ambient + diffuse * shadow) * albedo.rgb;

    outColor = vec4(result, albedo.a);
}
