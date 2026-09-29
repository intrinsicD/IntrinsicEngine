// Push block shared by transient_debug_sphere.vert/.frag (UI-067): one draw covers
// a run of point packets with one radius, six billboard vertices per point.
layout(push_constant, scalar) uniform TransientDebugSpherePush
{
    uint64_t VertexBufferBDA;
    uint     FirstVertex;   // first point of the run
    float    Radius;        // world units
    mat4x3   View;          // world -> view
    mat4     Projection;    // view -> clip
} push;
