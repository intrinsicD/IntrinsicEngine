// Shared surface color resolution helpers for both forward and deferred surface passes.
// These helpers stay source-level so the same code path works with external build-time
// compilation today and runtime shader recompilation / hot reload later.

// Centroid buffer: entry 0 stores the centroid count in .w, followed by
// {vec3 position, uint packedColor} payloads at indices [1, count].
layout(buffer_reference, scalar) readonly buffer CentroidBuf {
    vec4 entries[];  // .xyz = position, floatBitsToUint(.w) = packedColor
};

vec4 ResolveSurfaceBaseColor()
{
    // Priority 1: Centroid-based Voronoi (PtrCentroids != 0).
    // Vertex labels are in PtrVertexAttr (uint32 per vertex).
    // For each fragment, read the 3 vertex labels of this triangle,
    // look up the corresponding centroid positions, and pick the nearest
    // centroid.  This gives a true Voronoi diagram of the KMeans centroids
    // rather than the vertex-proximity approximation.
    if (push.PtrCentroids != 0ul && push.PtrVertexAttr != 0ul && push.PtrIndices != 0ul)
    {
        IndexBuf    iBuf = IndexBuf(push.PtrIndices);
        VertexAttrBuf labelBuf = VertexAttrBuf(push.PtrVertexAttr);  // labels, not colors
        CentroidBuf cBuf = CentroidBuf(push.PtrCentroids);
        uint centroidCount = floatBitsToUint(cBuf.entries[0].w);

        uint i0 = iBuf.idx[gl_PrimitiveID * 3 + 0];
        uint i1 = iBuf.idx[gl_PrimitiveID * 3 + 1];
        uint i2 = iBuf.idx[gl_PrimitiveID * 3 + 2];

        // Read vertex labels (cluster IDs).
        uint label0 = labelBuf.color[i0];
        uint label1 = labelBuf.color[i1];
        uint label2 = labelBuf.color[i2];

        if (centroidCount == 0u || label0 >= centroidCount || label1 >= centroidCount || label2 >= centroidCount)
        {
            // Fall through to the next fallback path when labels are stale or out of range.
        }
        else
        {
            // Read centroid positions from the centroid buffer.
            // Each payload entry is 16 bytes: {float x, float y, float z, uint packedColor}.
            // Stored as vec4 — xyz = position, w bits = packed ABGR color.
            vec4 ce0 = cBuf.entries[label0 + 1u];
            vec4 ce1 = cBuf.entries[label1 + 1u];
            vec4 ce2 = cBuf.entries[label2 + 1u];

            vec3 c0 = ce0.xyz;
            vec3 c1 = ce1.xyz;
            vec3 c2 = ce2.xyz;

            // Compute squared distance from fragment to each centroid in object space.
            vec3 d0 = fragObjectPos - c0;
            vec3 d1 = fragObjectPos - c1;
            vec3 d2 = fragObjectPos - c2;

            float dist0 = dot(d0, d0);
            float dist1 = dot(d1, d1);
            float dist2 = dot(d2, d2);

            // Pick the nearest centroid.
            uint nearestColor;
            if (dist0 <= dist1 && dist0 <= dist2)
                nearestColor = floatBitsToUint(ce0.w);
            else if (dist1 <= dist2)
                nearestColor = floatBitsToUint(ce1.w);
            else
                nearestColor = floatBitsToUint(ce2.w);

            return unpackUnorm4x8(nearestColor);
        }
    }

    // Priority 2: Nearest-vertex Voronoi (PtrIndices != 0, PtrVertexAttr != 0, no centroids).
    // Each fragment picks the color of the closest triangle vertex by Euclidean
    // distance in object space, producing sharp Voronoi-like cell boundaries.
    if (push.PtrVertexAttr != 0ul && push.PtrIndices != 0ul)
    {
        IndexBuf iBuf = IndexBuf(push.PtrIndices);
        PosBuf pBuf = PosBuf(push.PtrPositions);
        VertexAttrBuf vaBuf = VertexAttrBuf(push.PtrVertexAttr);

        uint i0 = iBuf.idx[gl_PrimitiveID * 3 + 0];
        uint i1 = iBuf.idx[gl_PrimitiveID * 3 + 1];
        uint i2 = iBuf.idx[gl_PrimitiveID * 3 + 2];

        vec3 p0 = pBuf.v[i0];
        vec3 p1 = pBuf.v[i1];
        vec3 p2 = pBuf.v[i2];

        vec3 d0 = fragObjectPos - p0;
        vec3 d1 = fragObjectPos - p1;
        vec3 d2 = fragObjectPos - p2;

        float dist0 = dot(d0, d0);
        float dist1 = dot(d1, d1);
        float dist2 = dot(d2, d2);

        uint nearestIdx;
        if (dist0 <= dist1 && dist0 <= dist2)
            nearestIdx = i0;
        else if (dist1 <= dist2)
            nearestIdx = i1;
        else
            nearestIdx = i2;

        return unpackUnorm4x8(vaBuf.color[nearestIdx]);
    }

    // Priority 3: Interpolated per-vertex colors.
    if (push.PtrVertexAttr != 0ul)
    {
        return fragVertexColor;
    }

    // Priority 4: Per-face colors.
    if (push.PtrFaceAttr != 0ul)
    {
        FaceAttrBuf fBuf = FaceAttrBuf(push.PtrFaceAttr);
        return unpackUnorm4x8(fBuf.color[gl_PrimitiveID]);
    }

    // Fallback: material (texture * base color factor).
    MaterialData mat = materials.Materials[fragMaterialSlot];
    vec4 albedoTex = texture(globalTextures[nonuniformEXT(fragTexID)], fragTexCoord);
    return albedoTex * mat.BaseColorFactor;
}
