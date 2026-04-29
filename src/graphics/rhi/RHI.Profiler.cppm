module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <expected>

export module Extrinsic.RHI.Profiler;

// ============================================================
// RHI.Profiler — API-agnostic GPU timestamp profiler interface.
//
// Provides per-frame GPU timestamp scopes (begin/end pairs).
// Results are resolved non-blockingly: if the GPU hasn't
// completed the frame yet, Resolve() returns ProfilerError::NotReady.
//
// Usage pattern (one frame):
//   profiler.BeginFrame(frameIndex);
//   auto scope = profiler.BeginScope("ShadowPass");
//   ... record commands ...
//   profiler.EndScope(scope);
//   ...
//   profiler.EndFrame();
//
//   // N frames later (results are ready):
//   auto result = profiler.Resolve(frameIndex);
// ============================================================

export namespace Extrinsic::RHI
{
    // ----------------------------------------------------------
    // Error type for profiler operations
    // ----------------------------------------------------------
    enum class ProfilerError : std::uint32_t
    {
        NotReady   = 0, // GPU hasn't finished the requested frame yet
        DeviceLost = 1, // unrecoverable device error
        InvalidState = 2, // e.g. Resolve called before BeginFrame
    };

    // ----------------------------------------------------------
    // Per-scope result
    // ----------------------------------------------------------
    struct GpuTimestampScope
    {
        std::string   Name;
        std::uint64_t DurationNs = 0; // GPU-elapsed nanoseconds for this scope
    };

    // ----------------------------------------------------------
    // Per-frame resolved result
    // ----------------------------------------------------------
    struct GpuTimestampFrame
    {
        std::uint64_t FrameNumber     = 0;
        std::uint64_t GpuFrameTimeNs  = 0;  // total GPU frame time (first begin → last end)
        std::uint64_t CpuSubmitTimeNs = 0;  // CPU time at vkQueueSubmit (monotonic ns)
        std::vector<GpuTimestampScope> Scopes{};

        [[nodiscard]] std::uint64_t FindScopeDurationNs(std::string_view name) const
        {
            for (const auto& s : Scopes)
                if (s.Name == name) return s.DurationNs;
            return 0;
        }
    };

    // ----------------------------------------------------------
    // IProfiler — pure-virtual GPU profiler interface.
    //
    // Obtained via IDevice::GetProfiler().
    // Lifetime is tied to the IDevice that created it.
    // Returns nullptr when profiling is disabled (release builds,
    // or when the device was created without profiling support).
    // ----------------------------------------------------------
    class IProfiler
    {
    public:
        virtual ~IProfiler() = default;

        // ---- Frame lifecycle -----------------------------------------
        // Call once per frame before recording any profiled commands.
        // frameIndex must be in [0, framesInFlight).
        virtual void BeginFrame(std::uint32_t frameIndex,
                                std::uint32_t maxScopesHint = 64) = 0;
        virtual void EndFrame() = 0;

        // ---- Scope management ----------------------------------------
        // Returns an opaque scope handle (index into per-frame scope list).
        // Must be called between BeginFrame / EndFrame.
        [[nodiscard]] virtual std::uint32_t BeginScope(std::string_view name) = 0;
        virtual void EndScope(std::uint32_t scopeHandle) = 0;

        // ---- Result resolution ---------------------------------------
        // Non-blocking.  If the GPU hasn't finished the frame yet, returns
        // std::unexpected(ProfilerError::NotReady).
        // frameIndex must be an older frame index (>= framesInFlight behind current).
        [[nodiscard]] virtual std::expected<GpuTimestampFrame, ProfilerError>
        Resolve(std::uint32_t frameIndex) const = 0;

        // ---- Diagnostics ---------------------------------------------
        [[nodiscard]] virtual std::uint32_t GetFramesInFlight() const = 0;
    };
}

