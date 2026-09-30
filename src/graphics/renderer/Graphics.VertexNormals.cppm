// Device-owned kernels for mesh vertex normals from weighted incident faces (RUNTIME-296,
// ADR 0030 decisions 8-9): a face pass over a corner-ring CSR and a per-vertex gather over a
// vertex->face incidence CSR, both in double precision in the CPU reference's order. Inputs
// are resident property buffers (the canonical float3 positions) and one resident topology
// bundle the caller packs once per topology revision; the result is stored into a float3 ring.
// No geometry or ECS types cross this boundary.
module;
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
export module Extrinsic.Graphics.VertexNormals;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    // The reference's AveragingMode values the kernel implements (the angle weightings need a
    // double-precision acos the device does not have; the caller keeps them on the CPU).
    enum class VertexNormalGpuWeighting : std::uint32_t { UniformFace, AreaWeighted, MaxWeighted };

    struct VertexNormalsGpuParams
    {
        VertexNormalGpuWeighting Weighting{VertexNormalGpuWeighting::AreaWeighted};
        double Epsilon{1.0e-12};                 // DegenerateNormalLengthEpsilon (> 0)
        std::array<float, 3> Fallback{0.f, 1.f, 0.f}; // the normalized fallback the reference writes
    };

    // Size limits of one run: the kernels index faces (six doubles), vertices (three floats)
    // and incidences (two words) with 32-bit arithmetic, so a bundle or a mesh above these
    // limits is refused before anything is allocated. A dispatch covers at most
    // MaxDispatchThreads threads (the guaranteed 65535 workgroups of 64); larger passes are
    // issued in chunks.
    inline constexpr std::uint32_t VertexNormalsMaxFaces = 1u << 24;
    inline constexpr std::uint32_t VertexNormalsMaxVertices = 1u << 24;
    inline constexpr std::uint32_t VertexNormalsMaxCorners = 1u << 26;
    inline constexpr std::uint32_t VertexNormalsMaxDispatchThreads = 65535u * 64u;

    // Where each section of a packed topology bundle sits (uint32 word offsets) and its
    // counts. The bundle is one uint32 array: face offsets (Faces + 1), corner vertices,
    // vertex offsets (Vertices + 1), incidences ((face, corner) pairs, ascending face order
    // per vertex) and the live rows the vertex pass writes.
    struct VertexNormalsTopologyLayout
    {
        std::uint32_t Faces{}, Vertices{}, LiveRows{}, Corners{}, Incidences{};
        std::uint32_t FaceOffsetsAt{}, CornersAt{}, VertexOffsetsAt{}, IncidenceAt{}, LiveRowsAt{};
        std::uint32_t Words{};
        [[nodiscard]] std::uint64_t Bytes() const noexcept { return std::uint64_t(Words) * 4u; }
    };

    // Packs the bundle from a corner-ring CSR (`faceOffsets` has Faces + 1 entries; corners are
    // vertex indices below `vertices`) and the live rows; the vertex->face incidence CSR is
    // derived here by a counting pass over the faces in ascending order. Returns an empty
    // layout (Words == 0) when the arrays are inconsistent or exceed the limits above (checked
    // before any allocation). The section order is fixed, so a
    // bundle already resident on the device is described again by
    // `UnpackVertexNormalsTopologyLayout` from its counts alone.
    [[nodiscard]] VertexNormalsTopologyLayout PackVertexNormalsTopology(
        std::span<const std::uint32_t> faceOffsets, std::span<const std::uint32_t> corners,
        std::uint32_t vertices, std::span<const std::uint32_t> liveRows, std::vector<std::uint32_t>& out);
    // The layout of a packed bundle of `words` words over `faces`, `vertices` and `liveRows`
    // (the corner count follows from the word count); empty when the counts do not fit or
    // exceed the limits.
    [[nodiscard]] VertexNormalsTopologyLayout UnpackVertexNormalsTopologyLayout(
        std::uint32_t faces, std::uint32_t vertices, std::uint32_t liveRows, std::uint32_t words);

    // A resident buffer endpoint (device address of tightly packed rows).
    struct VertexNormalsResidentView
    {
        RHI::BufferHandle Buffer{};
        std::uint64_t Address{};
        [[nodiscard]] bool Valid() const noexcept { return Buffer.IsValid() && Address != 0u; }
    };
    // The run's resident endpoints: canonical float3 positions over every vertex, the packed
    // topology bundle, the float3 output ring slot (first filled from `Base`, the output's
    // canonical slot when the property exists, or zeroed over `OutputBytes`, so rows outside
    // the live rows keep their published bytes).
    struct VertexNormalsResidentIo
    {
        VertexNormalsResidentView Positions{}, Topology{}, Output{}, Base{};
        std::uint64_t OutputBytes{};
        VertexNormalsTopologyLayout Layout{};
    };

    // The counters the kernels report (read back from the buffer Record returns).
    struct VertexNormalsGpuStats
    {
        std::uint32_t ProcessedFaces{}, ValidVertices{}, FallbackVertices{};
    };

    class VertexNormalsWorkspace
    {
    public:
        explicit VertexNormalsWorkspace(RHI::IDevice& device);
        ~VertexNormalsWorkspace();
        static constexpr std::uint64_t StatsReadbackBytes = 3u * sizeof(std::uint32_t);
        // Records both passes and returns the stats buffer (StatsReadbackBytes, transfer-
        // readable) or an invalid handle on refusal (non-operational device, no shader double
        // support, an invalid endpoint or layout). The caller keeps the workspace alive until
        // the submission completed.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext& commands, const VertexNormalsGpuParams& params,
                                               const VertexNormalsResidentIo& io);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
