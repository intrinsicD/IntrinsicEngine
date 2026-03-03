// point_surfel.frag — Surfel point fragment shader: Lambertian-shaded disc.
//
// Part of the PointPass pipeline family (PLAN.md Phase 4).
// Normal-oriented disc with two-sided Lambertian + ambient lighting.
// Discards fragments outside the unit circle with an anti-aliased edge.

#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    // Disc test: discard fragments outside the unit circle.
    float r2 = dot(fragDiscUV, fragDiscUV);
    if (r2 > 1.0) discard;

    // Anti-aliased edge via smoothstep.
    float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));

    // Lambertian + ambient lighting.
    vec3 N = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    // Two-sided lighting: flip normal if facing away from light.
    float NdotL = dot(N, lightDir);
    float diffuse = max(abs(NdotL), 0.0);

    float ambient = 0.15;
    vec3 lit = fragColor.rgb * (ambient + (1.0 - ambient) * diffuse);

    outColor = vec4(lit, fragColor.a * alpha);
}
