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
    // ---- Mode 0: Flat Disc — screen-aligned, no lighting ----
    // Simple hard-edge AA disc. No surface shading needed since it's
    // a screen-aligned billboard; lighting would require a fake normal.
    if (fragMode == 0) {
        float r2 = dot(fragDiscUV, fragDiscUV);
        if (r2 > 1.0) discard;
        float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));
        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
        return;
    }

    // ---- Mode 1: Surfel — surface-aligned disc with Lambertian lighting ----
    if (fragMode == 1) {
        float r2 = dot(fragDiscUV, fragDiscUV);
        if (r2 > 1.0) discard;

        float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));


        // Reconstruct world-space camera position from the view matrix.
        // view = [R | t] where t = -R*camPos, so camPos = -R^T * t.
        // camera.view[3].xyz is the translation column t.
        vec3 camPos  = -(transpose(mat3(camera.view)) * camera.view[3].xyz);
        vec3 normal  = normalize(fragNormalWorld);
        vec3 viewDir = normalize(camPos - fragPosWorld);

        // Flip normal toward camera so back faces still light correctly.
        if (dot(normal, viewDir) < 0.0) normal = -normal;

        vec3  lightDir = normalize(vec3(0.3, 1.0, 0.5));
        float diff     = max(dot(normal, lightDir), 0.0) * 0.7 + 0.3;

        outColor = vec4(fragColor.rgb * diff, fragColor.a * alpha);
        return;
    }

    // ---- Mode 2: EWA Gaussian — perspective-correct elliptical splat ----
    if (fragMode == 2) {
        // Pixel offset from the projected surfel center.
        vec2 d  = gl_FragCoord.xy - fragScreenCenterPx;

        // Mahalanobis distance: e2 = d^T * Sigma_inv * d
        // fragConic = (Sigma_inv_xx, Sigma_inv_xy, Sigma_inv_xy, Sigma_inv_yy)
        float e2 = fragConic.x * d.x * d.x
                 + (fragConic.y + fragConic.z) * d.x * d.y
                 + fragConic.w * d.y * d.y;

        if (e2 > 9.0) discard;   // 3-sigma cutoff: exp(-0.5 * 9) ≈ 0.011

        float weight = exp(-0.5 * e2);

        vec3  camPos  = -(transpose(mat3(camera.view)) * camera.view[3].xyz);
        vec3  normal  = normalize(fragNormalWorld);
        vec3  viewDir = normalize(camPos - fragPosWorld);
        if (dot(normal, viewDir) < 0.0) normal = -normal;

        float diff = max(dot(normal, normalize(vec3(0.3, 1.0, 0.5))), 0.0) * 0.7 + 0.3;

        outColor = vec4(fragColor.rgb * diff, fragColor.a * weight);
        return;
    }

    // ---- Mode 3: Gaussian Splat — isotropic Gaussian, no lighting ----
    // fragDiscUV in [-1, 1]; weight = exp(-2*r²) fades smoothly to near-zero
    // at the quad edge. No hard discard — suitable for density/volumetric viz.
    if (fragMode == 3) {
        float r2     = dot(fragDiscUV, fragDiscUV);
        if (r2 > 4.0) discard;   // cull well past Gaussian tail (2x radius)
        float weight = exp(-2.0 * r2);
        outColor = vec4(fragColor.rgb, fragColor.a * weight);
        return;
    }

    // Fallback — should never reach here in practice.
    discard;
}