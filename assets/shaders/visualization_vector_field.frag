// visualization_vector_field.frag — vector-field arrow glyph color for the
// default-recipe `VisualizationOverlayPass`. The packed per-field RGBA is
// written to `SceneColorHDR`; the pipeline alpha-blends it over the scene.

#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = fragColor;
}
