module;

#include <cstdint>
#include <vector>
#include <span>
#include <cmath>

#include <glm/glm.hpp>

export module Graphics:DebugDraw;

import :GpuColor;

export namespace Graphics
{
    // -------------------------------------------------------------------------
    // DebugDraw — Immediate-Mode Debug Drawing Accumulator
    // -------------------------------------------------------------------------
    //
    // Contract:
    // - Main-thread only (respects the single-writer contract).
    // - Transient: all geometry is rebuilt each frame.
    // - Call Reset() at frame start, submit geometry, then the LineRenderPass
    //   reads GetLines()/GetDepthTestedLines() and uploads to GPU SSBO.
    //
    // GPU data layout:
    //   struct LineSegment { vec3 Start; uint32 ColorStart; vec3 End; uint32 ColorEnd; }
    //   Packed as 32 bytes per segment for GPU-friendly alignment (2 x vec4).
    //
    class DebugDraw
    {
    public:
        // GPU-aligned line segment: 32 bytes (2 x vec4).
        // Layout: [Start.x, Start.y, Start.z, ColorStart | End.x, End.y, End.z, ColorEnd]
        struct alignas(16) LineSegment
        {
            glm::vec3 Start;
            uint32_t ColorStart; // packed ABGR (Vulkan byte order: R in low bits)
            glm::vec3 End;
            uint32_t ColorEnd;
        };
        static_assert(sizeof(LineSegment) == 32, "LineSegment must be 32 bytes for GPU SSBO alignment");

        // ----------------------------------------------------------------
        // Color Utilities (delegate to GpuColor — single source of truth)
        // ----------------------------------------------------------------
        static constexpr uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept
        {
            return GpuColor::PackColor(r, g, b, a);
        }

        static constexpr uint32_t PackColorF(float r, float g, float b, float a = 1.0f) noexcept
        {
            return GpuColor::PackColorF(r, g, b, a);
        }

        // Predefined colors
        static constexpr uint32_t Red()     { return GpuColor::PackColor(255, 0, 0); }
        static constexpr uint32_t Green()   { return GpuColor::PackColor(0, 255, 0); }
        static constexpr uint32_t Blue()    { return GpuColor::PackColor(0, 0, 255); }
        static constexpr uint32_t Yellow()  { return GpuColor::PackColor(255, 255, 0); }
        static constexpr uint32_t Cyan()    { return GpuColor::PackColor(0, 255, 255); }
        static constexpr uint32_t Magenta() { return GpuColor::PackColor(255, 0, 255); }
        static constexpr uint32_t White()   { return GpuColor::PackColor(255, 255, 255); }
        static constexpr uint32_t Gray()    { return GpuColor::PackColor(128, 128, 128); }
        static constexpr uint32_t Orange()  { return GpuColor::PackColor(255, 153, 0); }

        // ----------------------------------------------------------------
        // Primitive Drawing API (depth-tested by default)
        // ----------------------------------------------------------------

        // Single line segment.
        void Line(const glm::vec3& from, const glm::vec3& to, uint32_t color);
        void Line(const glm::vec3& from, const glm::vec3& to, uint32_t colorStart, uint32_t colorEnd);

        // Axis-aligned bounding box (12 edges).
        void Box(const glm::vec3& min, const glm::vec3& max, uint32_t color);

        // Oriented bounding box: transform * [-halfExtents, +halfExtents].
        void WireBox(const glm::mat4& transform, const glm::vec3& halfExtents, uint32_t color);

        // Wireframe sphere approximated by 3 great circles.
        void Sphere(const glm::vec3& center, float radius, uint32_t color, uint32_t segments = 24);

        // Circle in a plane defined by center and normal.
        void Circle(const glm::vec3& center, const glm::vec3& normal, float radius,
                    uint32_t color, uint32_t segments = 32);

        // Directional arrow from → to, with arrowhead.
        void Arrow(const glm::vec3& from, const glm::vec3& to, float headSize, uint32_t color);

        // Axis cross (three colored lines: R=X, G=Y, B=Z).
        void Axes(const glm::vec3& origin, float size);
        void Axes(const glm::mat4& transform, float size);

        // Wireframe frustum from an inverse view-projection matrix.
        void Frustum(const glm::mat4& invViewProj, uint32_t color);

        // Grid on a plane (centered at origin, aligned to axisU/axisV).
        void Grid(const glm::vec3& origin, const glm::vec3& axisU, const glm::vec3& axisV,
                  int countU, int countV, float spacing, uint32_t color);

        // Cross-hair at a point (3 axis-aligned lines).
        void Cross(const glm::vec3& center, float size, uint32_t color);

        // ----------------------------------------------------------------
        // Overlay API (no depth test — always drawn on top)
        // ----------------------------------------------------------------

        void OverlayLine(const glm::vec3& from, const glm::vec3& to, uint32_t color);
        void OverlayLine(const glm::vec3& from, const glm::vec3& to, uint32_t colorStart, uint32_t colorEnd);
        void OverlayBox(const glm::vec3& min, const glm::vec3& max, uint32_t color);
        void OverlaySphere(const glm::vec3& center, float radius, uint32_t color, uint32_t segments = 24);
        void OverlayAxes(const glm::vec3& origin, float size);

        // ----------------------------------------------------------------
        // Frame Lifecycle
        // ----------------------------------------------------------------

        // Clear all accumulated geometry. Call at the start of each frame.
        void Reset();

        // Access accumulated geometry for GPU upload.
        [[nodiscard]] std::span<const LineSegment> GetLines() const;
        [[nodiscard]] std::span<const LineSegment> GetOverlayLines() const;
        [[nodiscard]] uint32_t GetLineCount() const { return static_cast<uint32_t>(m_Lines.size()); }
        [[nodiscard]] uint32_t GetOverlayLineCount() const { return static_cast<uint32_t>(m_OverlayLines.size()); }
        [[nodiscard]] bool HasContent() const { return !m_Lines.empty() || !m_OverlayLines.empty(); }

    private:
        // Depth-tested lines (rendered with depth test enabled).
        std::vector<LineSegment> m_Lines;

        // Overlay lines (rendered without depth test — always on top).
        std::vector<LineSegment> m_OverlayLines;

        // Shared implementation for sphere drawing.
        void SphereImpl(std::vector<LineSegment>& target,
                        const glm::vec3& center, float radius,
                        uint32_t color, uint32_t segments);

        // Shared implementation for box drawing.
        void BoxImpl(std::vector<LineSegment>& target,
                     const glm::vec3& min, const glm::vec3& max, uint32_t color);

        // Shared implementation for axes drawing.
        void AxesImpl(std::vector<LineSegment>& target,
                      const glm::vec3& origin, float size);
    };
}
