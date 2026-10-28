// RUNTIME-331: the pass-through style fields of a vector-field layer, declared
// once. A pass-through field is a float the layer hands unchanged to the shader.
// One row each:
//   VF_STYLE_ROW(Member, Default, agent_name, sceneKey, SceneRequired,
//                "Panel label", "Description", Min, Max)
// The includer defines VF_STYLE_ROW. Each row becomes a float member of
// GeometryVectorFieldLayerRecipe and of VectorFieldGlyphStyle (initialized to
// Default), a style-table row (agent name, scene key, inclusive bounds,
// description), the glyph-style check, the extraction copy, a panel slider, and
// a record member (through vector_field_draw_record.glsl) that
// visualization_vector_field.vert reads by Member. Bounds are finite float
// literals; a scene written before a row existed needs SceneRequired false.
VF_STYLE_ROW(LineWidthPx, 2.0f, line_width, lineWidthPx, true, "Width (px)", "Arrow width in pixels.", 0.5f, 32.0f)
