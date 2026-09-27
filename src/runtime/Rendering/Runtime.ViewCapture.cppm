// Screenshots of the running editor (GRAPHICS-109, RUNTIME-281): one capture queue used by
// the File menu, the Camera / Render window and the agent `view_capture` tools. A request
// becomes a one-shot renderer backbuffer capture; the pixels are cropped to the scene
// viewport (or kept as the whole window), PNG-encoded and written without partial files.
module;

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

export module Extrinsic.Runtime.ViewCapture;

import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleLifecycle;

export namespace Extrinsic::Runtime
{
    enum class ViewCaptureRegion : std::uint8_t
    {
        Viewport = 0, // the scene rectangle, as shown (gizmos and overlapping windows included)
        Window,       // the whole window with the editor UI
    };

    // Camera for the capture; the current view is restored afterwards. Presets look
    // along a world axis (+Y up) or the (-1,-1,-1) isometric diagonal and frame the
    // fit entity, or every entity with world bounds when none is given.
    enum class ViewCapturePreset : std::uint8_t
    {
        Current = 0,
        Front,  // looking along -Z
        Back,   // +Z
        Left,   // +X
        Right,  // -X
        Top,    // -Y
        Bottom, // +Y
        Isometric,
    };

    enum class ViewCaptureState : std::uint8_t
    {
        Unknown = 0, // no such ticket (or evicted from the recent results)
        Queued,
        Pending,
        Completed,
        Failed,
    };

    struct ViewCaptureRequest
    {
        ViewCaptureRegion Region{ViewCaptureRegion::Viewport};
        bool SaveToFile{true};
        std::string OutputPath{};      // empty: DefaultViewCapturePath(OutputDirectory, ticket)
        std::string OutputDirectory{}; // empty: the module's screenshot directory
        bool KeepPng{false};      // keep the encoded bytes in the status (agents)
        ViewCapturePreset Preset{ViewCapturePreset::Current};
        std::uint32_t FitEntity{0};    // stable entity id framed by a preset; 0 frames the scene
        std::uint32_t LegendEntity{0}; // stable id whose scalar colormap is appended as a strip
    };

    // Colormap legend of a scalar visualization: the strip spans Min (left) to Max (right).
    struct ViewCaptureLegend
    {
        Graphics::Colormap::Type Map{Graphics::Colormap::Type::Viridis};
        std::string Property{};
        std::string Colormap{};
        float Min{0.0f};
        float Max{1.0f};
        bool AutoRange{true};
    };

    struct ViewCaptureStatus
    {
        ViewCaptureState State{ViewCaptureState::Unknown};
        std::uint64_t Ticket{0};
        ViewCaptureRegion Region{ViewCaptureRegion::Viewport};
        std::string Path{}; // written file; empty when not saved
        std::uint32_t Width{0};
        std::uint32_t Height{0};
        std::vector<std::uint8_t> Png{};
        ViewCapturePreset Preset{ViewCapturePreset::Current};
        std::optional<ViewCaptureLegend> Legend{};
        std::string Diagnostic{};
    };

    struct ViewCaptureImage
    {
        std::uint32_t Width{0};
        std::uint32_t Height{0};
        std::vector<std::uint8_t> Rgba8{}; // top-down, tightly packed
    };

    struct ViewCapturePresetAxes
    {
        glm::vec3 Forward{0.0f, 0.0f, -1.0f};
        glm::vec3 Up{0.0f, 1.0f, 0.0f};
    };
    // Viewing direction and up vector of a preset (Current returns the defaults).
    [[nodiscard]] ViewCapturePresetAxes ViewCapturePresetAxesFor(ViewCapturePreset preset) noexcept;

    inline constexpr std::uint32_t kViewCaptureLegendHeight = 24; // 4 px margins around a 16 px strip
    // Appends a legend band below the image: dark margins and a colormap strip sampled
    // left to right from `lut` (RGB triples, at least one entry).
    void AppendViewCaptureLegend(ViewCaptureImage& image, std::span<const std::uint8_t> lutRgb);

    // Clips `rect` to the image; an empty or disjoint rectangle keeps the whole image.
    [[nodiscard]] ViewCaptureImage CropViewCaptureImage(const ViewCaptureImage& image, Core::Rect2D rect);
    // PNG bytes, or empty on failure (empty or inconsistent image).
    [[nodiscard]] std::vector<std::uint8_t> EncodeViewCapturePng(const ViewCaptureImage& image);
    // Writes through a sibling temporary file and a rename, creating parent directories;
    // returns a diagnostic on failure, after which no file (partial or temporary) remains.
    [[nodiscard]] std::optional<std::string> WriteViewCaptureFile(
        const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
    // <directory>/intrinsic-<YYYYmmdd-HHMMSS>-<ticket>.png in local time.
    [[nodiscard]] std::filesystem::path DefaultViewCapturePath(
        const std::filesystem::path& directory, std::uint64_t ticket);
    [[nodiscard]] std::string_view ToString(ViewCaptureRegion region) noexcept;
    [[nodiscard]] std::string_view ToString(ViewCaptureState state) noexcept;
    [[nodiscard]] std::string_view ToString(ViewCapturePreset preset) noexcept;

    class ViewCaptureModule final : public IRuntimeModule
    {
    public:
        // Default captures land in `<screenshotDirectory>`; empty selects
        // `<working directory>/screenshots`.
        explicit ViewCaptureModule(std::filesystem::path screenshotDirectory = {});

        [[nodiscard]] std::string_view Name() const noexcept override { return "Runtime.ViewCapture"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        // Queues a capture; the ticket is valid even when the capture later fails.
        [[nodiscard]] std::uint64_t Request(ViewCaptureRequest request);
        [[nodiscard]] ViewCaptureStatus Status(std::uint64_t ticket) const;
        // The most recently finished capture without its PNG bytes (UI status line).
        [[nodiscard]] std::optional<ViewCaptureStatus> LastFinished() const;
        // Why captures cannot succeed right now (no operational render device), or nullopt.
        [[nodiscard]] std::optional<std::string> UnavailableReason() const;
        [[nodiscard]] const std::filesystem::path& ScreenshotDirectory() const noexcept { return m_Directory; }

    private:
        struct Entry
        {
            ViewCaptureRequest Request{};
            ViewCaptureStatus Status{};
            std::uint64_t RendererTicket{0};
            Core::Rect2D Viewport{};
            std::uint32_t SettleFrames{0};  // frames left before a preset view is captured
            std::function<void()> Restore{}; // puts the pre-capture camera back
        };

        void Advance(RuntimeFrameHookContext& frame);
        void Finish(Entry& entry);

        std::filesystem::path m_Directory{};
        std::deque<Entry> m_Active{};   // front is the one in the renderer
        std::deque<Entry> m_Finished{}; // most recent last, bounded
        std::uint64_t m_NextTicket{1};
        bool m_DeviceOperational{false};
        bool m_Registered{false};
    };
}
