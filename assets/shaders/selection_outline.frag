#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform usampler2D uPickID;

layout(push_constant) uniform Push
{
    vec4 OutlineColor;     // RGBA color for selected entities
    vec4 HoverColor;       // RGBA color for hovered entity
    float OutlineWidth;    // Width in texels (1-4 recommended)
    uint SelectedCount;    // Number of valid entries in SelectedIds
    uint HoveredId;        // PickID of the hovered entity (0 = none)
    uint _pad;
    uint SelectedIds[16];  // PickIDs of selected entities
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
    // If the selection/hover state differs from the center, it's an edge.
    bool selectionEdge = false;
    bool hoverEdge = false;

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
                selectionEdge = true;

            bool neighborHovered = (neighborId != 0u && neighborId == pc.HoveredId);
            if (centerHovered != neighborHovered)
                hoverEdge = true;
        }
    }

    // Selection outline takes priority over hover
    if (selectionEdge)
        outColor = pc.OutlineColor;
    else if (hoverEdge)
        outColor = pc.HoverColor;
    else
        outColor = vec4(0.0);
}
