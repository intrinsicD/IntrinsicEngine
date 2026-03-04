#version 450

// FXAA 3.11 Quality — simplified single-pass implementation.
// Operates on LDR input (post-tonemap), writes anti-aliased LDR output.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform Push
{
    vec2  InvResolution;   // 1.0 / screen dimensions
    float ContrastThreshold;  // FXAA edge detection threshold (default 0.0312)
    float RelativeThreshold;  // FXAA relative contrast threshold (default 0.063)
    float SubpixelBlending;   // Subpixel AA quality (0.0 = off, 0.75 = default, 1.0 = max)
} pc;

float Luma(vec3 c)
{
    // Perceptual luminance — matches FXAA reference
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec2 uv = vUV;
    vec2 rcpFrame = pc.InvResolution;

    // Sample center and 4 neighbors
    vec3 rgbM  = texture(uInput, uv).rgb;
    vec3 rgbN  = texture(uInput, uv + vec2( 0.0, -1.0) * rcpFrame).rgb;
    vec3 rgbS  = texture(uInput, uv + vec2( 0.0,  1.0) * rcpFrame).rgb;
    vec3 rgbW  = texture(uInput, uv + vec2(-1.0,  0.0) * rcpFrame).rgb;
    vec3 rgbE  = texture(uInput, uv + vec2( 1.0,  0.0) * rcpFrame).rgb;

    float lumaM = Luma(rgbM);
    float lumaN = Luma(rgbN);
    float lumaS = Luma(rgbS);
    float lumaW = Luma(rgbW);
    float lumaE = Luma(rgbE);

    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range = rangeMax - rangeMin;

    // Skip pixels with low contrast
    if (range < max(pc.ContrastThreshold, rangeMax * pc.RelativeThreshold))
    {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // Sample corners
    vec3 rgbNW = texture(uInput, uv + vec2(-1.0, -1.0) * rcpFrame).rgb;
    vec3 rgbNE = texture(uInput, uv + vec2( 1.0, -1.0) * rcpFrame).rgb;
    vec3 rgbSW = texture(uInput, uv + vec2(-1.0,  1.0) * rcpFrame).rgb;
    vec3 rgbSE = texture(uInput, uv + vec2( 1.0,  1.0) * rcpFrame).rgb;

    float lumaNW = Luma(rgbNW);
    float lumaNE = Luma(rgbNE);
    float lumaSW = Luma(rgbSW);
    float lumaSE = Luma(rgbSE);

    // Determine edge direction
    float edgeH = abs(-2.0 * lumaW + lumaNW + lumaSW)
                + abs(-2.0 * lumaM + lumaN  + lumaS ) * 2.0
                + abs(-2.0 * lumaE + lumaNE + lumaSE);
    float edgeV = abs(-2.0 * lumaN + lumaNW + lumaNE)
                + abs(-2.0 * lumaM + lumaW  + lumaE ) * 2.0
                + abs(-2.0 * lumaS + lumaSW + lumaSE);

    bool isHorizontal = (edgeH >= edgeV);

    // Choose edge normal direction
    float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;

    bool is1Steeper = abs(gradient1) >= abs(gradient2);
    float scaledGrad = 0.25 * max(abs(gradient1), abs(gradient2));

    if (!is1Steeper) stepLength = -stepLength;

    // Compute edge-perpendicular UV for 1px step
    vec2 edgeUV = uv;
    if (isHorizontal)
        edgeUV.y += stepLength * 0.5;
    else
        edgeUV.x += stepLength * 0.5;

    // Walk along edge in both directions
    vec2 edgeStep = isHorizontal ? vec2(rcpFrame.x, 0.0) : vec2(0.0, rcpFrame.y);

    float edgeLuma = 0.5 * (luma1 + luma2);
    bool atEnd1 = false, atEnd2 = false;
    vec2 uv1 = edgeUV - edgeStep;
    vec2 uv2 = edgeUV + edgeStep;

    float luma1End = Luma(texture(uInput, uv1).rgb) - edgeLuma;
    float luma2End = Luma(texture(uInput, uv2).rgb) - edgeLuma;

    atEnd1 = abs(luma1End) >= scaledGrad;
    atEnd2 = abs(luma2End) >= scaledGrad;

    // Walk up to 10 steps (quality preset)
    for (int i = 0; i < 10 && !(atEnd1 && atEnd2); i++)
    {
        if (!atEnd1) {
            uv1 -= edgeStep;
            luma1End = Luma(texture(uInput, uv1).rgb) - edgeLuma;
            atEnd1 = abs(luma1End) >= scaledGrad;
        }
        if (!atEnd2) {
            uv2 += edgeStep;
            luma2End = Luma(texture(uInput, uv2).rgb) - edgeLuma;
            atEnd2 = abs(luma2End) >= scaledGrad;
        }
    }

    // Calculate distances
    float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    float minDist = min(dist1, dist2);
    float totalDist = dist1 + dist2;

    // Determine blend factor
    float pixelBlend = 0.5 - minDist / totalDist;
    bool correctSide = (lumaM - edgeLuma < 0.0) != (((dist1 < dist2) ? luma1End : luma2End) < 0.0);
    pixelBlend = correctSide ? pixelBlend : 0.0;

    // Subpixel blending
    float lumaAvg = (lumaN + lumaS + lumaW + lumaE
                   + lumaNW + lumaNE + lumaSW + lumaSE) / 8.0;
    float subpixel = clamp(abs(lumaAvg - lumaM) / range, 0.0, 1.0);
    subpixel = smoothstep(0.0, 1.0, subpixel);
    subpixel = subpixel * subpixel * pc.SubpixelBlending;

    float finalBlend = max(pixelBlend, subpixel);

    // Final UV offset for blend
    vec2 finalUV = uv;
    if (isHorizontal)
        finalUV.y += stepLength * finalBlend;
    else
        finalUV.x += stepLength * finalBlend;

    outColor = vec4(texture(uInput, finalUV).rgb, 1.0);
}
