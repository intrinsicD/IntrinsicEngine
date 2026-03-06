#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform usampler2D uPickID;

layout(push_constant) uniform Push
{
    vec4 OutlineColor;     // RGBA color for selected entities
    vec4 HoverColor;       // RGBA color for hovered entity
    float OutlineWidth;    // Width in texels (1-10 recommended)
    uint SelectedCount;    // Number of valid entries in SelectedIds
    uint HoveredId;        // PickID of the hovered entity (0 = none)
    uint OutlineMode;      // 0=Solid, 1=Pulse, 2=Glow
    float SelectionFillAlpha; // Fill overlay alpha for selected entities
    float HoverFillAlpha;     // Fill overlay alpha for hovered entity
    float PulsePhase;         // Current pulse animation phase (0..2*PI)
    float PulseMin;           // Minimum alpha during pulse
    float PulseMax;           // Maximum alpha during pulse
    float GlowFalloff;       // Exponential falloff rate for glow mode
    uint _pad0;
    uint _pad1;
    uint SelectedIds[16]; // PickIDs of selected entities
} pc;

bool IsSelected(uint id)
{
    if (id == 0u) return false;
    for (uint i = 0u; i < min(pc.SelectedCount, 16u); i++)
    {
        if (pc.SelectedIds[i] == id) return true;
    }
    return false;
}

void main()
{
    ivec2 texSize = textureSize(uPickID, 0);
    ivec2 coord = ivec2(vUV * vec2(texSize));
    coord = clamp(coord, ivec2(0), texSize - ivec2(1));

    uint centerId = texelFetch(uPickID, coord, 0).x;
    bool centerSelected = IsSelected(centerId);
    bool centerHovered = (centerId != 0u && centerId == pc.HoveredId);

    // Early out: if no selection or hover, skip neighbor sampling
    if (!centerSelected && !centerHovered && pc.SelectedCount == 0u && pc.HoveredId == 0u)
    {
        outColor = vec4(0.0);
        return;
    }

    // Sample 8 neighbors at distances from 1 to OutlineWidth.
    // Track edge detection and minimum edge distance for glow mode.
    bool selectionEdge = false;
    bool hoverEdge = false;
    float minSelDist = float(max(int(pc.OutlineWidth), 1) + 1);
    float minHoverDist = minSelDist;

    const ivec2 offsets[8] = ivec2[8](
        ivec2(-1,  0), ivec2( 1,  0), ivec2( 0, -1), ivec2( 0,  1),
        ivec2(-1, -1), ivec2( 1, -1), ivec2(-1,  1), ivec2( 1,  1)
    );

    int width = max(int(pc.OutlineWidth), 1);

    for (int r = 1; r <= width; r++)
    {
        for (int i = 0; i < 8; i++)
        {
            ivec2 nc = coord + offsets[i] * r;
            nc = clamp(nc, ivec2(0), texSize - ivec2(1));
            uint neighborId = texelFetch(uPickID, nc, 0).x;

            bool neighborSelected = IsSelected(neighborId);
            if (centerSelected != neighborSelected)
            {
                selectionEdge = true;
                float dist = (i < 4) ? float(r) : float(r) * 1.41421356;
                minSelDist = min(minSelDist, dist);
            }

            bool neighborHovered = (neighborId != 0u && neighborId == pc.HoveredId);
            if (centerHovered != neighborHovered)
            {
                hoverEdge = true;
                float dist = (i < 4) ? float(r) : float(r) * 1.41421356;
                minHoverDist = min(minHoverDist, dist);
            }
        }
    }

    // Compute outline contribution based on mode
    vec4 result = vec4(0.0);

    if (selectionEdge)
    {
        vec4 col = pc.OutlineColor;
        if (pc.OutlineMode == 1u) // Pulse
        {
            float t = sin(pc.PulsePhase) * 0.5 + 0.5; // [0, 1]
            col.a *= mix(pc.PulseMin, pc.PulseMax, t);
        }
        else if (pc.OutlineMode == 2u) // Glow
        {
            float normDist = minSelDist / max(float(width), 1.0);
            col.a *= exp(-pc.GlowFalloff * normDist);
        }
        result = col;
    }
    else if (hoverEdge)
    {
        vec4 col = pc.HoverColor;
        if (pc.OutlineMode == 1u) // Pulse
        {
            float t = sin(pc.PulsePhase) * 0.5 + 0.5;
            col.a *= mix(pc.PulseMin, pc.PulseMax, t);
        }
        else if (pc.OutlineMode == 2u) // Glow
        {
            float normDist = minHoverDist / max(float(width), 1.0);
            col.a *= exp(-pc.GlowFalloff * normDist);
        }
        result = col;
    }

    // Fill overlay: tint inside selected/hovered entities
    if (result.a < 0.001)
    {
        if (centerSelected && pc.SelectionFillAlpha > 0.0)
        {
            result = vec4(pc.OutlineColor.rgb, pc.SelectionFillAlpha);
        }
        else if (centerHovered && pc.HoverFillAlpha > 0.0)
        {
            result = vec4(pc.HoverColor.rgb, pc.HoverFillAlpha);
        }
    }

    outColor = result;
}
