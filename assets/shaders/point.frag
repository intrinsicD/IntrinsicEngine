#version 460

layout(location = 0) in vec4  fragColor;
layout(location = 1) in vec2  fragUV;
layout(location = 2) flat in uint fragMode;
layout(location = 3) in vec3  fragNormalWorld;
layout(location = 4) in vec3  fragPosWorld;
layout(location = 5) in vec4  fragConic;
layout(location = 6) in vec2  fragPixelOffset;
layout(location = 7) in vec2  fragDiscUV;
layout(location = 8) in vec2  fragScreenCenterPx;

layout(push_constant) uniform PushConsts {
    float SizeMultiplier;
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;
} push;

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) out vec4 outColor;

void main()
{
    // Mode 0 & 1: Simple circle discard
    if (fragMode <= 1) {
        float r2 = dot(fragDiscUV, fragDiscUV);
        if (r2 > 1.0) discard;

        float alpha = 1.0 - smoothstep(0.9, 1.0, sqrt(r2));

        vec3 normal = normalize(fragNormalWorld);
        vec3 camPos = -transpose(mat3(camera.view)) * camera.view[3].xyz;
        vec3 viewDir = normalize(camPos - fragPosWorld);
        if (dot(normal, viewDir) < 0.0) normal = -normal;

        vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
        float diff = max(dot(normal, lightDir), 0.0) * 0.7 + 0.2;

        outColor = vec4(fragColor.rgb * diff, fragColor.a * alpha);
        return;
    }

    // Mode 2: EWA Gaussian
    if (fragMode == 2) {
        // True pixel delta from projected center.
        vec2 d = gl_FragCoord.xy - fragScreenCenterPx;

        float e2 = fragConic.x * d.x * d.x + (fragConic.y + fragConic.z) * d.x * d.y + fragConic.w * d.y * d.y;
        if (e2 > 9.0) discard; // 3-sigma in the metric exp(-0.5 * e2)

        float weight = exp(-0.5 * e2);

        vec3 normal = normalize(fragNormalWorld);
        vec3 camPos = -transpose(mat3(camera.view)) * camera.view[3].xyz;
        vec3 viewDir = normalize(camPos - fragPosWorld);
        if (dot(normal, viewDir) < 0.0) normal = -normal;

        float diff = max(dot(normal, normalize(vec3(0.3, 1.0, 0.5))), 0.0) * 0.7 + 0.2;

        outColor = vec4(fragColor.rgb * diff, fragColor.a * weight);
        return;
    }

    // Mode 3: GaussianSplat — isotropic Gaussian blob, smooth opacity, no hard disc edge.
    // fragDiscUV in [-1,1]; treat radius=1 as 2-sigma boundary (sigma=0.5 in UV space).
    // weight = exp(-2*r²) gives ~0.135 alpha at the quad edge — smoothly fades to zero.
    // No surface lighting: intended for volumetric/density visualization.
    if (fragMode == 3) {
        float r2 = dot(fragDiscUV, fragDiscUV);
        if (r2 > 4.0) discard; // cull beyond 2x radius (well past Gaussian tail)

        float weight = exp(-2.0 * r2);
        outColor = vec4(fragColor.rgb, fragColor.a * weight);
    }
}