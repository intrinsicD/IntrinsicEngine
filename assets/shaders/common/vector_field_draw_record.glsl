// GRAPHICS-165: the vector-field draw record, declared once. Read by
// visualization_vector_field.vert (its struct) and by
// Graphics.VisualizationOverlayUploadHelper (the C++ struct, its layout guard
// and its fill). The includer defines one macro per row kind:
//   VF_RECORD_PACKET(type, Name)   copied from packet.Name;
//   VF_RECORD_STYLE(type, Name)    copied from packet.Style.Name;
//   VF_RECORD_DERIVED(type, Name)  computed by the upload helper.
// Row order is the scalar layout. Types: mat4, uint64_t, uint, float. Another
// type needs an alias in the C++ struct and an assert there that its size and
// alignment are its GLSL scalar ones (no bool: GLSL stores it in 4 bytes).
VF_RECORD_PACKET(mat4, ObjectToWorld)
VF_RECORD_PACKET(uint64_t, PositionBufferBDA)
VF_RECORD_PACKET(uint64_t, VectorBufferBDA)
VF_RECORD_PACKET(uint64_t, RowBufferBDA)
VF_RECORD_PACKET(uint, ElementCount)
VF_RECORD_PACKET(uint, RowCount)
VF_RECORD_PACKET(uint, RowStride)
VF_RECORD_DERIVED(uint, PackedColor)
VF_RECORD_STYLE(float, Scale)
// The pass-through style rows (RUNTIME-331), copied from packet.Style as well.
#define VF_STYLE_ROW(member, fallback, agentName, sceneKey, sceneRequired, label, help, lo, hi) \
    VF_RECORD_STYLE(float, member)
#include "common/vector_field_style_rows.glsl"
#undef VF_STYLE_ROW
VF_RECORD_DERIVED(uint, Flags)
VF_RECORD_DERIVED(uint, ColormapID)
// Per-element float multipliers / colormap scalars or vec4 colors; 0 = none.
VF_RECORD_PACKET(uint64_t, LengthBufferBDA)
VF_RECORD_DERIVED(uint64_t, ColorBufferBDA)
VF_RECORD_STYLE(float, ColorRangeMin)
VF_RECORD_STYLE(float, ColorRangeMax)
