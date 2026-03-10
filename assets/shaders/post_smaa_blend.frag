#version 450

// SMAA Blend Weight Calculation (Jimenez et al. 2012).
// Searches for edge patterns and computes per-pixel blend weights.
// Uses area and search lookup textures for pattern classification.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outWeights;

layout(set = 0, binding = 0) uniform sampler2D uEdges;
layout(set = 0, binding = 1) uniform sampler2D uAreaTex;
layout(set = 0, binding = 2) uniform sampler2D uSearchTex;

layout(push_constant) uniform Push
{
    vec2  InvResolution;
    int   MaxSearchSteps;     // Max edge search distance (default 16)
    int   MaxSearchStepsDiag; // Max diagonal search distance (default 8)
} pc;

// Area texture constants — SMAA reference defines these.
#define SMAA_AREATEX_MAX_DISTANCE    16
#define SMAA_AREATEX_PIXEL_SIZE      (1.0 / vec2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE     (1.0 / 7.0)

// Search texture size: 66x33
#define SMAA_SEARCHTEX_SIZE          vec2(66.0, 33.0)

// --------------------------------------------------------------------------
// Helper: decode the search texture to get sub-pixel edge positions.
// --------------------------------------------------------------------------
float SearchLength(vec2 e, float offset)
{
    // Scale and bias to access search texture properly.
    vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
    vec2 bias  = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);
    scale += vec2(-1.0, 1.0);
    bias  += vec2(0.5, -0.5);
    scale *= 1.0 / SMAA_SEARCHTEX_SIZE;
    bias  *= 1.0 / SMAA_SEARCHTEX_SIZE;
    return textureLod(uSearchTex, scale * e + bias, 0.0).r;
}

// --------------------------------------------------------------------------
// Horizontal/vertical edge search along the edge direction.
// Returns the distance to the edge end.
// --------------------------------------------------------------------------
float SearchXLeft(vec2 texcoord, float end)
{
    vec2 rcp = pc.InvResolution;
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < pc.MaxSearchSteps; i++)
    {
        if (texcoord.x <= end || e.y <= 0.0 || e.x > 0.0)
            break;
        e = textureLod(uEdges, texcoord, 0.0).rg;
        texcoord -= vec2(2.0, 0.0) * rcp;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e, 0.0) + 3.25;
    return texcoord.x + offset * rcp.x;
}

float SearchXRight(vec2 texcoord, float end)
{
    vec2 rcp = pc.InvResolution;
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < pc.MaxSearchSteps; i++)
    {
        if (texcoord.x >= end || e.y <= 0.0 || e.x > 0.0)
            break;
        e = textureLod(uEdges, texcoord, 0.0).rg;
        texcoord += vec2(2.0, 0.0) * rcp;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e, 0.5) + 3.25;
    return texcoord.x - offset * rcp.x;
}

float SearchYUp(vec2 texcoord, float end)
{
    vec2 rcp = pc.InvResolution;
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < pc.MaxSearchSteps; i++)
    {
        if (texcoord.y <= end || e.r <= 0.0 || e.g > 0.0)
            break;
        e = textureLod(uEdges, texcoord, 0.0).rg;
        texcoord -= vec2(0.0, 2.0) * rcp;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e.gr, 0.0) + 3.25;
    return texcoord.y + offset * rcp.y;
}

float SearchYDown(vec2 texcoord, float end)
{
    vec2 rcp = pc.InvResolution;
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < pc.MaxSearchSteps; i++)
    {
        if (texcoord.y >= end || e.r <= 0.0 || e.g > 0.0)
            break;
        e = textureLod(uEdges, texcoord, 0.0).rg;
        texcoord += vec2(0.0, 2.0) * rcp;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e.gr, 0.5) + 3.25;
    return texcoord.y - offset * rcp.y;
}

// --------------------------------------------------------------------------
// Area texture lookup: maps (dist_left, dist_right) to blend weights.
// --------------------------------------------------------------------------
vec2 Area(vec2 dist, float e1, float e2)
{
    vec2 texcoord = float(SMAA_AREATEX_MAX_DISTANCE) * round(4.0 * vec2(e1, e2)) + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
    // Note: area texture uses second row group for subpixel offsets (0.0 for no subpixel).
    return textureLod(uAreaTex, texcoord, 0.0).rg;
}

void main()
{
    vec2 uv  = vUV;
    vec2 rcp = pc.InvResolution;

    vec4 weights = vec4(0.0);
    vec2 e = texture(uEdges, uv).rg;

    if (e.g > 0.0) // Horizontal edge (top edge exists)
    {
        // Search left and right along the edge.
        // Search start offsets match SMAA reference: (-0.25, -0.125) and (1.25, -0.125).
        vec2 d;
        vec2 coords;

        coords.x = SearchXLeft(uv + vec2(-0.25, -0.125) * rcp, -0.5 * rcp.x);
        // Bilinear offset: sample at -0.25 pixel Y to encode 2x2 edge neighborhood
        // into {0, 0.25, 0.5, 0.75, 1.0} for area texture tile selection.
        coords.y = uv.y - 0.25 * rcp.y;
        d.x = coords.x;

        float e1 = textureLod(uEdges, coords, 0.0).r;

        coords.x = SearchXRight(uv + vec2(1.25, -0.125) * rcp, 1.0 + 0.5 * rcp.x);
        d.y = coords.x;

        // Convert to pixel distances.
        d = abs(round(d / rcp.x - uv.x / rcp.x));

        // Right endpoint: +1 texel in X, same bilinear Y offset.
        float e2 = textureLod(uEdges, vec2(coords.x + 0.5 * rcp.x, uv.y - 0.25 * rcp.y), 0.0).r;

        weights.rg = Area(sqrt(d), e1, e2);
    }

    if (e.r > 0.0) // Vertical edge (left edge exists)
    {
        // Search up and down along the edge.
        // Search start offsets match SMAA reference: (-0.125, -0.25) and (-0.125, 1.25).
        vec2 d;
        vec2 coords;

        coords.y = SearchYUp(uv + vec2(-0.125, -0.25) * rcp, -0.5 * rcp.y);
        // Bilinear offset: sample at -0.25 pixel X to encode 2x2 edge neighborhood.
        coords.x = uv.x - 0.25 * rcp.x;
        d.x = coords.y;

        float e1 = textureLod(uEdges, coords, 0.0).g;

        coords.y = SearchYDown(uv + vec2(-0.125, 1.25) * rcp, 1.0 + 0.5 * rcp.y);
        d.y = coords.y;

        d = abs(round(d / rcp.y - uv.y / rcp.y));

        // Bottom endpoint: +1 texel in Y, same bilinear X offset.
        float e2 = textureLod(uEdges, vec2(uv.x - 0.25 * rcp.x, coords.y + 0.5 * rcp.y), 0.0).g;

        weights.ba = Area(sqrt(d), e1, e2);
    }

    outWeights = weights;
}
