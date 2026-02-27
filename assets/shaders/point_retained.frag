// point_retained.frag — Point cloud fragment shader for retained-mode BDA points.
//
// Push constant layout matches point_retained.vert. The fragment shader only
// reads RenderMode for mode-dependent shading.

#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

// Push constant layout must match point_retained.vert exactly.
layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    float    PointSize;
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     RenderMode;       // 0 = FlatDisc, 1 = Surfel
    uint     Color;
} push;

layout(location = 0) out vec4 outColor;

void main()
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
