// point.frag — Point cloud fragment shader with render mode support.
//
// Render modes (selected via push.RenderMode):
//   0 = FlatDisc:  Unlit circular disc with anti-aliased edge.
//   1 = Surfel:    Normal-oriented disc with Lambertian + ambient lighting.
//   2 = EWA:       Elliptical Gaussian splat with Lambertian shading (Zwicker et al. 2001).

#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) flat in vec3 fragEwaCovInv;  // UV-space inverse covariance (xx, xy, yy)

layout(push_constant) uniform PushConsts {
    float SizeMultiplier;
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;       // 0 = FlatDisc, 1 = Surfel, 2 = EWA
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    if (push.RenderMode == 2u)
    {
        // ---- EWA mode: Gaussian splat evaluation ----
        vec2 uv = fragDiscUV;
        float mahal = fragEwaCovInv.x * uv.x * uv.x
                    + 2.0 * fragEwaCovInv.y * uv.x * uv.y
                    + fragEwaCovInv.z * uv.y * uv.y;

        if (mahal > 9.0) discard;

        float weight = exp(-0.5 * mahal);

        vec3 N = normalize(fragNormal);
        vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
        float NdotL = dot(N, lightDir);
        float diffuse = max(abs(NdotL), 0.0);

        float ambient = 0.15;
        vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

        outColor = vec4(lit, fragColor.a * weight);
    }
    else
    {
        // Disc test: discard fragments outside the unit circle.
        float r2 = dot(fragDiscUV, fragDiscUV);
        if (r2 > 1.0) discard;

        // Anti-aliased edge via smoothstep.
        float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));

        if (push.RenderMode == 1u)
        {
            // ---- Surfel mode: Lambertian + ambient lighting ----
            vec3 N = normalize(fragNormal);
            vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

            // Two-sided lighting: flip normal if facing away from light.
            float NdotL = dot(N, lightDir);
            float diffuse = max(abs(NdotL), 0.0);

            float ambient = 0.15;
            vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

            outColor = vec4(lit, fragColor.a * alpha);
        }
        else
        {
            // ---- FlatDisc mode: unlit ----
            outColor = vec4(fragColor.rgb, fragColor.a * alpha);
        }
    }
}
