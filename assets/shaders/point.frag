// point.frag — Point cloud fragment shader with multi-mode rendering.
//
// Supports three rendering modes controlled by push constant:
//
//   Mode 0 (Flat Disc):
//     Circular splat with smooth anti-aliased edges via distance field.
//     Simple, fast, good for overview visualization.
//
//   Mode 1 (Surfel / Oriented Disc):
//     Same circular disc shape but with simple directional lighting based
//     on the world-space normal for depth perception. Uses a Lambertian
//     diffuse + ambient model.
//
//   Mode 2 (EWA Splatting):
//     Gaussian-weighted elliptical splat with smooth alpha falloff.
//     The reconstruction kernel is a 2D Gaussian evaluated from the
//     fragment's UV coordinate within the billboard quad.
//     References: Zwicker et al., "Surface Splatting" (SIGGRAPH 2001)
//
// All modes discard fragments outside the unit disc (UV radius > 1) for
// proper circular/elliptical shape, and apply anti-aliasing at edges.

#version 460

// ---- Inputs (from vertex shader) ----
layout(location = 0) in vec4  fragColor;
layout(location = 1) in vec2  fragUV;           // [-1,+1] within quad
layout(location = 2) flat in uint fragMode;
layout(location = 3) in vec3  fragNormalWorld;
layout(location = 4) in vec3  fragPosWorld;
layout(location = 5) in vec4  fragConic;        // (Qxx, Qxy, Qyx, Qyy)

// ---- Push Constants ----
layout(push_constant) uniform PushConsts {
    float SizeMultiplier;
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;
} push;

// ---- Camera UBO (for view-dependent lighting) ----
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

// ---- Output ----
layout(location = 0) out vec4 outColor;

void main()
{
    float r2 = dot(fragUV, fragUV); // squared distance from center (modes 0/1)

    // ------------------------------------------------------------------
    // Mode 0: Flat Disc
    // ------------------------------------------------------------------
    if (fragMode == 0)
    {
        // Discard outside unit circle.
        if (r2 > 1.0) discard;

        // Anti-aliased edge using smoothstep over ~1.5 pixel transition band.
        // fwidth gives the rate of change of r2 for AA width estimation.
        float r = sqrt(r2);
        float delta = fwidth(r);
        float alpha = 1.0 - smoothstep(1.0 - delta * 1.5, 1.0, r);

        if (alpha < 0.004) discard;

        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
        return;
    }

    // ------------------------------------------------------------------
    // Mode 1: Surfel (Oriented Disc with Lighting)
    // ------------------------------------------------------------------
    if (fragMode == 1)
    {
        if (r2 > 1.0) discard;

        float r = sqrt(r2);
        float delta = fwidth(r);
        float alpha = 1.0 - smoothstep(1.0 - delta * 1.5, 1.0, r);

        if (alpha < 0.004) discard;

        // Simple directional lighting for depth perception.
        vec3 normal = normalize(fragNormalWorld);

        // Extract camera position from view matrix (inverse view row 3).
        vec3 camPos = -transpose(mat3(camera.view)) * camera.view[3].xyz;
        vec3 viewDir = normalize(camPos - fragPosWorld);

        // Ensure normal faces camera.
        if (dot(normal, viewDir) < 0.0)
            normal = -normal;

        // Light direction: overhead + slight forward bias.
        vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));

        // Lambertian diffuse.
        float NdotL = max(dot(normal, lightDir), 0.0);

        // Hemisphere ambient: warm above, cool below.
        float hemiFactor = dot(normal, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
        vec3 ambient = mix(vec3(0.08, 0.08, 0.12), vec3(0.15, 0.15, 0.18), hemiFactor);

        // Rim light for silhouette definition.
        float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0) * 0.15;

        vec3 lit = fragColor.rgb * (ambient + NdotL * 0.7) + vec3(rim);
        outColor = vec4(lit, fragColor.a * alpha);
        return;
    }

    // ------------------------------------------------------------------
    // Mode 2: EWA Splatting (Gaussian Kernel)
    // ------------------------------------------------------------------
    {
        // Evaluate rotated ellipse using pixel-space conic.
        // d = pixel offset from center. We approximate d from fragUV by treating it as the
        // conservative bounding-quad parameterization, then remap to pixel offsets via
        // the same conic (scale-invariant under linear changes of basis).
        // Practically: use d = fragUV in the quadratic form defined by Q.
        mat2 Q = mat2(fragConic.x, fragConic.y,
                      fragConic.z, fragConic.w);
        float e2 = dot(fragUV, Q * fragUV);

        // Gaussian reconstruction kernel: exp(-r^2 / (2 * sigma^2))
        // With sigma chosen so the kernel has negligible weight at r=1.
        // sigma^2 = 1/3 gives ~95% energy within the unit disc.
        float sigma2 = 1.0 / 3.0;
        float weight = exp(-e2 / (2.0 * sigma2));

        // Discard negligible contributions.
        if (weight < 0.01) discard;

        // Hard clip outside ellipse for stability (prevents wide Gaussian tails from the conservative quad).
        if (e2 > 4.0) discard;

        // Simple directional lighting (same as surfel mode).
        vec3 normal = normalize(fragNormalWorld);
        vec3 camPos = -transpose(mat3(camera.view)) * camera.view[3].xyz;
        vec3 viewDir = normalize(camPos - fragPosWorld);
        if (dot(normal, viewDir) < 0.0)
            normal = -normal;

        vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
        float NdotL = max(dot(normal, lightDir), 0.0);

        float hemiFactor = dot(normal, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
        vec3 ambient = mix(vec3(0.08, 0.08, 0.12), vec3(0.15, 0.15, 0.18), hemiFactor);
        float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0) * 0.15;

        vec3 lit = fragColor.rgb * (ambient + NdotL * 0.7) + vec3(rim);
        outColor = vec4(lit, fragColor.a * weight);
    }
}
