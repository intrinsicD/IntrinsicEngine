#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[];

layout(push_constant) uniform Push
{
    uint SourceTextureSlot;
} pc;

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    uint sourceTextureSlot = nonuniformEXT(pc.SourceTextureSlot);
    vec4 center = texelFetch(uTextures[sourceTextureSlot], coord, 0);
    if (center.a > 0.5)
    {
        outColor = center;
        return;
    }

    ivec2 size = textureSize(uTextures[sourceTextureSlot], 0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            ivec2 sampleCoord = coord + ivec2(x, y);
            if (sampleCoord.x < 0 ||
                sampleCoord.y < 0 ||
                sampleCoord.x >= size.x ||
                sampleCoord.y >= size.y)
            {
                continue;
            }

            vec4 neighbor =
                texelFetch(uTextures[sourceTextureSlot], sampleCoord, 0);
            if (neighbor.a > 0.5)
            {
                outColor = vec4(neighbor.rgb, 1.0);
                return;
            }
        }
    }

    outColor = vec4(0.5, 0.5, 1.0, 0.0);
}
