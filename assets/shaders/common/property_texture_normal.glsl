#ifndef INTRINSIC_PROPERTY_TEXTURE_NORMAL_GLSL
#define INTRINSIC_PROPERTY_TEXTURE_NORMAL_GLSL

// Property-bake targets store alpha-one covered texels over a transparent
// black clear. Linear texture filtering therefore premultiplies encoded RGB
// by geometric coverage. Recover the encoded value before mapping [0,1] back
// to the signed normal domain; otherwise a chart-edge sample rotates toward
// the clear color.
bool DecodePropertyTextureNormal(vec4 filteredSample, out vec3 unitNormal)
{
    if (filteredSample.a <= 1.0e-6)
    {
        unitNormal = vec3(0.0);
        return false;
    }

    const vec3 encoded = clamp(
        filteredSample.rgb / filteredSample.a,
        vec3(0.0),
        vec3(1.0));
    const vec3 decoded = encoded * 2.0 - vec3(1.0);
    const float decodedLength = length(decoded);
    if (decodedLength <= 1.0e-6)
    {
        unitNormal = vec3(0.0);
        return false;
    }

    unitNormal = decoded / decodedLength;
    return true;
}

#endif
