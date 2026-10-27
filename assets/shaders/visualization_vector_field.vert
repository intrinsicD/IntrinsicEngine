// visualization_vector_field.vert — instanced vector-field arrow glyphs for
// the default-recipe `VisualizationOverlayPass`.
//
// One instance per sampled live source row, nine vertices per instance: a
// screen-space shaft quad (vertices 0-5) and an arrow head (vertices 6-8).
// Anchors, vectors and the optional live-row list are read through the draw
// record's buffer addresses; camera matrices and viewport come from the
// GpuScene table. Nothing is expanded on the CPU.
//
// Contract with `VisualizationVectorFieldDrawRecord` (scalar layout):
//   * vectors are ordinary object-space vectors; the tip is
//     anchor + Scale * (normalize ? normalize(v) : v), and both endpoints are
//     transformed by ObjectToWorld (no normal-vector transform);
//   * only exactly-zero, non-finite or out-of-range rows collapse to a
//     degenerate triangle; normalization rescales by the largest component so
//     tiny and huge finite vectors keep their direction;
//   * non-finite transformed endpoints are rejected before projection;
//   * a segment crossing the near plane (clip z >= 0, standard [0, 1] depth)
//     is clipped in homogeneous space before the perspective divide, and a
//     clipped tip drops the head;
//   * a vector parallel to the view direction, or any arrow shorter than its
//     width on screen, draws a width-sized cap instead of vanishing;
//   * glyphs are pulled toward the camera by a distance-relative view-space
//     bias so tangent fields stay visible on the surface they annotate
//     (standard [0, 1] depth with LessEqual);
//   * UI-083: an optional per-element length `s` (read at the glyph's source
//     row) scales the arrow by `Scale * s`; `s` not > 0 or infinite draws
//     nothing. An optional per-element color is a colormap scalar (an infinite
//     or NaN value, or an invalid colormap index, keeps PackedColor) or an RGBA
//     color (a negative component keeps PackedColor).

#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common/gpu_scene.glsl"

struct VectorFieldDrawRecord
{
    mat4 ObjectToWorld;
    uint64_t PositionBufferBDA;
    uint64_t VectorBufferBDA;
    uint64_t RowBufferBDA;
    uint ElementCount;
    uint RowCount;
    uint RowStride;
    uint PackedColor;
    float Scale;
    float LineWidthPx;
    uint Flags;
    uint ColormapID;
    uint64_t LengthBufferBDA;
    uint64_t ColorBufferBDA;
    float ColorRangeMin;
    float ColorRangeMax;
};

layout(buffer_reference, scalar) readonly buffer VectorFieldDrawRecordRef
{
    VectorFieldDrawRecord Data[];
};

layout(push_constant, scalar) uniform PushConsts
{
    uint64_t SceneTableBDA;
    uint64_t RecordBufferBDA;
    uint RecordIndex;
    uint Reserved;
} pc;

layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(location = 0) out vec4 fragColor;

const uint kNormalizeFlag = 1u;
const uint kColormapFlag = 2u;
const uint kRgbaFlag = 4u;
const float kMinClipW = 1.0e-7;
const float kDepthBiasFraction = 1.0e-3;
const float kHeadLengthFraction = 0.35;
const float kHeadLengthPerWidth = 4.0;
const float kHeadHalfWidthPerWidth = 1.75;

void EmitDegenerate()
{
    // Every vertex of the instance lands on the same clipped point, so the
    // rasterizer discards the zero-area triangles.
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    fragColor = vec4(0.0);
}

bool AllFinite(vec3 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

bool AllFinite(vec4 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

vec4 ToBiasedClip(GpuSceneTable scene, vec3 world)
{
    vec4 view = scene.CameraView * vec4(world, 1.0);
    const bool perspective = abs(scene.CameraProj[2][3]) > 0.5;
    if (perspective)
    {
        // Scaling along the eye ray keeps the projected position fixed.
        view.xyz *= (1.0 - kDepthBiasFraction);
    }
    else
    {
        view.z += kDepthBiasFraction * max(abs(view.z), scene.CameraNearPlane);
    }
    return scene.CameraProj * view;
}

// x * 2^100, exact also for a denormal x, which a GPU may flush to zero as an arithmetic operand:
// its bits are m * 2^-149, so the product is m * 2^-49.
float ScaleUp100(float x)
{
    const uint bits = floatBitsToUint(x);
    if ((bits & 0x7F800000u) != 0u)
        return x * exp2(100.0);
    const float scaled = float(bits & 0x007FFFFFu) * exp2(-49.0);
    return (bits & 0x80000000u) != 0u ? -scaled : scaled;
}

// t in [0, 1] of `value` in [lo, hi] (lo < hi). The operands are scaled by a power of two so the
// width and the reciprocal the GPU divides with stay normal floats: a width above ~8.5e37 has a
// denormal reciprocal, flushed to zero, and a tiny width is itself denormal.
float NormalizedColorScalar(float value, float lo, float hi)
{
    const float magnitude = max(abs(lo), abs(hi));
    if (magnitude > 1.0e30)
    {
        value *= exp2(-100.0);
        lo *= exp2(-100.0);
        hi *= exp2(-100.0);
    }
    else if (magnitude < 1.0e-30)
    {
        value = ScaleUp100(value);
        lo = ScaleUp100(lo);
        hi = ScaleUp100(hi);
    }
    // `precise`: the compiler may otherwise cancel the scale out of the quotient again.
    precise const float offset = value - lo;
    precise const float width = hi - lo;
    return width > 0.0 ? clamp(offset / width, 0.0, 1.0) : 0.0;
}

vec4 ArrowColor(VectorFieldDrawRecord record, uint row)
{
    const vec4 uniformColor = unpackUnorm4x8(record.PackedColor);
    if (record.ColorBufferBDA == uint64_t(0))
        return uniformColor;
    if ((record.Flags & kColormapFlag) != 0u)
    {
        const float s = GpuFloatBufferRef(record.ColorBufferBDA).Data[row];
        if (isinf(s) || isnan(s) || !GpuVisualizationHasValidBindless(record.ColormapID))
            return uniformColor;
        const float t = NormalizedColorScalar(s, record.ColorRangeMin, record.ColorRangeMax);
        return vec4(textureLod(globalTextures[nonuniformEXT(record.ColormapID)], vec2(t, 0.5), 0.0).rgb, 1.0);
    }
    if ((record.Flags & kRgbaFlag) != 0u)
    {
        const vec4 c = GpuVec4BufferRef(record.ColorBufferBDA).Data[row];
        return any(lessThan(c, vec4(0.0))) ? uniformColor : c;
    }
    return uniformColor;
}

void main()
{
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const VectorFieldDrawRecord record =
        VectorFieldDrawRecordRef(pc.RecordBufferBDA).Data[pc.RecordIndex];

    const uint liveIndex = uint(gl_InstanceIndex) * record.RowStride;
    if (liveIndex >= record.RowCount)
    {
        EmitDegenerate();
        return;
    }
    const uint row = record.RowBufferBDA != uint64_t(0)
        ? GpuUIntBufferRef(record.RowBufferBDA).Data[liveIndex]
        : liveIndex;
    if (row >= record.ElementCount)
    {
        EmitDegenerate();
        return;
    }

    float scale = record.Scale;
    if (record.LengthBufferBDA != uint64_t(0))
    {
        const float s = GpuFloatBufferRef(record.LengthBufferBDA).Data[row];
        if (!(s > 0.0) || isinf(s))
        {
            EmitDegenerate();
            return;
        }
        scale *= s;
    }

    const vec3 anchor = GpuReadPackedVec3(record.PositionBufferBDA, row);
    vec3 vector = GpuReadPackedVec3(record.VectorBufferBDA, row);
    if (!AllFinite(anchor) || !AllFinite(vector))
    {
        EmitDegenerate();
        return;
    }
    const vec3 magnitude = abs(vector);
    const float largest = max(magnitude.x, max(magnitude.y, magnitude.z));
    if (largest == 0.0)
    {
        EmitDegenerate();
        return;
    }
    if ((record.Flags & kNormalizeFlag) != 0u)
    {
        // Rescale first: length() of a huge finite vector overflows and of a
        // tiny one underflows, while the rescaled length lies in [1, sqrt(3)].
        const vec3 unit = vector / largest;
        vector = unit * (scale / length(unit));
    }
    else
    {
        vector *= scale;
    }

    const vec3 tip = anchor + vector;
    const vec3 worldA = (record.ObjectToWorld * vec4(anchor, 1.0)).xyz;
    const vec3 worldB = (record.ObjectToWorld * vec4(tip, 1.0)).xyz;
    if (!AllFinite(tip) || !AllFinite(worldA) || !AllFinite(worldB))
    {
        EmitDegenerate();
        return;
    }
    vec4 clipA = ToBiasedClip(scene, worldA);
    vec4 clipB = ToBiasedClip(scene, worldB);
    if (!AllFinite(clipA) || !AllFinite(clipB))
    {
        EmitDegenerate();
        return;
    }

    // Clip against the near plane (z >= 0) before dividing by w.
    if (clipA.z < 0.0 && clipB.z < 0.0)
    {
        EmitDegenerate();
        return;
    }
    bool tipVisible = true;
    if (clipA.z < 0.0)
    {
        clipA = mix(clipA, clipB, clipA.z / (clipA.z - clipB.z));
        clipA.z = max(clipA.z, 0.0);
    }
    else if (clipB.z < 0.0)
    {
        clipB = mix(clipA, clipB, clipA.z / (clipA.z - clipB.z));
        clipB.z = max(clipB.z, 0.0);
        tipVisible = false;
    }
    if (clipA.w < kMinClipW || clipB.w < kMinClipW)
    {
        EmitDegenerate();
        return;
    }

    const vec2 viewport = max(vec2(scene.CameraViewportWidth, scene.CameraViewportHeight), vec2(1.0));
    const vec2 pixelA = (clipA.xy / clipA.w) * 0.5 * viewport;
    const vec2 pixelB = (clipB.xy / clipB.w) * 0.5 * viewport;
    const vec2 delta = pixelB - pixelA;
    const float screenLength = length(delta);
    const float width = clamp(record.LineWidthPx, 0.5, 32.0);
    const float halfWidth = 0.5 * width;

    // A vector along the view ray has no screen direction, and any arrow
    // shorter than its width would be a sliver: both draw a width-sized cap.
    const bool degenerateAxis = screenLength < 1.0e-3;
    const bool shortArrow = screenLength < width;
    const vec2 tangent = degenerateAxis ? vec2(1.0, 0.0) : delta / screenLength;
    const vec2 normal = vec2(-tangent.y, tangent.x);

    float headLength = 0.0;
    if (tipVisible && !shortArrow)
    {
        headLength = min(kHeadLengthFraction * screenLength, kHeadLengthPerWidth * width);
    }
    // Perspective-correct clip-space point where the shaft meets the head.
    const float screenFraction = shortArrow ? 1.0 : 1.0 - headLength / screenLength;
    const float clipFraction = screenFraction * clipA.w /
        max((1.0 - screenFraction) * clipB.w + screenFraction * clipA.w, kMinClipW);
    const vec4 clipBase = mix(clipA, clipB, clipFraction);

    const uint corner = uint(gl_VertexIndex);
    vec4 center;
    vec2 offsetPx;
    if (corner < 6u)
    {
        const uint quadCorner = uint[6](0u, 1u, 2u, 0u, 2u, 3u)[corner];
        const bool atBase = quadCorner >= 2u;
        const float side = (quadCorner == 0u || quadCorner == 3u) ? -1.0 : 1.0;
        center = atBase ? clipBase : clipA;
        const float cap = shortArrow ? (atBase ? halfWidth : -halfWidth) : 0.0;
        offsetPx = normal * (side * halfWidth) + tangent * cap;
    }
    else if (headLength > 0.0)
    {
        const uint headCorner = corner - 6u;
        const float headHalfWidth = kHeadHalfWidthPerWidth * width;
        if (headCorner == 2u)
        {
            center = clipB;
            offsetPx = vec2(0.0);
        }
        else
        {
            center = clipBase;
            offsetPx = normal * (headCorner == 0u ? -headHalfWidth : headHalfWidth);
        }
    }
    else
    {
        EmitDegenerate();
        return;
    }

    const vec2 offsetNdc = offsetPx * 2.0 / viewport;
    gl_Position = center + vec4(offsetNdc * center.w, 0.0, 0.0);
    fragColor = ArrowColor(record, row);
}
