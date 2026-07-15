module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <imgui.h>
#include <implot.h>

module Extrinsic.Runtime.ImGuiAdapter;

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.RHI.Bindless;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kBundledFontAsset = "fonts/Roboto-Medium.ttf";
        constexpr float kBaseFontSizePixels = 16.0f;

        // Apply the window's logical size and HiDPI scale onto ImGui IO. ImGui
        // expects `DisplaySize` in window coordinates and `DisplayFramebufferScale`
        // as framebuffer-pixels-per-window-unit, so the overlay frame can report
        // true pixel dimensions to the renderer.
        void ApplyDisplayMetrics(ImGuiIO& io, const Platform::Extent2D win, const Platform::Extent2D fb)
        {
            const float winW = win.Width > 0 ? static_cast<float>(win.Width) : 0.0f;
            const float winH = win.Height > 0 ? static_cast<float>(win.Height) : 0.0f;
            io.DisplaySize = ImVec2(winW, winH);

            float scaleX = 1.0f;
            float scaleY = 1.0f;
            if (win.Width > 0 && fb.Width > 0)
                scaleX = static_cast<float>(fb.Width) / winW;
            if (win.Height > 0 && fb.Height > 0)
                scaleY = static_cast<float>(fb.Height) / winH;
            io.DisplayFramebufferScale = ImVec2(scaleX, scaleY);
        }

        [[nodiscard]] std::string ResolveBundledFontPath()
        {
            const std::string path = Extrinsic::Core::Filesystem::GetAssetPath(kBundledFontAsset);
            std::error_code ec;
            return std::filesystem::exists(path, ec) ? path : std::string{};
        }

        [[nodiscard]] bool TextureRefMatchesAtlas(
            const ImTextureRef texture,
            const ImTextureRef atlas) noexcept
        {
            if (atlas._TexData != nullptr)
            {
                return texture._TexData == atlas._TexData;
            }

            if (texture._TexData != nullptr)
            {
                return false;
            }

            return texture._TexID == atlas._TexID;
        }

        void ConfigureFonts(ImGuiIO& io)
        {
            const float dpiScale =
                std::max(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            const float rasterizerDensity = dpiScale > 0.0f ? dpiScale : 1.0f;
            const std::string fontPath = ResolveBundledFontPath();
            ImFontConfig fontConfig{};
            fontConfig.Flags |= ImFontFlags_NoLoadError;
            fontConfig.RasterizerDensity = rasterizerDensity;
            if (!fontPath.empty() &&
                io.Fonts->AddFontFromFileTTF(
                    fontPath.c_str(),
                    kBaseFontSizePixels,
                    &fontConfig) != nullptr)
            {
                return;
            }

            fontConfig.SizePixels = kBaseFontSizePixels;
            io.Fonts->AddFontDefault(&fontConfig);
        }

        void BuildLegacyFontAtlas(ImGuiIO& io)
        {
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            int bytesPerPixel = 0;
            io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height, &bytesPerPixel);
            (void)pixels;
            (void)width;
            (void)height;
            (void)bytesPerPixel;
        }

        [[nodiscard]] std::uint32_t ToPixelDimension(const float value)
        {
            if (!std::isfinite(value) || value <= 0.0f)
                return 0u;
            constexpr float kMaxDimension =
                static_cast<float>(std::numeric_limits<std::uint32_t>::max());
            if (value >= kMaxDimension)
                return std::numeric_limits<std::uint32_t>::max();
            return static_cast<std::uint32_t>(value + 0.5f);
        }

        [[nodiscard]] std::optional<Graphics::ImGuiOverlayScissor>
        BuildOverlayScissor(
            const ImVec4 clipRect,
            const ImVec2 displayPos,
            const ImVec2 framebufferScale,
            const std::uint32_t pixelWidth,
            const std::uint32_t pixelHeight) noexcept
        {
            if (pixelWidth == 0u || pixelHeight == 0u ||
                !std::isfinite(clipRect.x) ||
                !std::isfinite(clipRect.y) ||
                !std::isfinite(clipRect.z) ||
                !std::isfinite(clipRect.w) ||
                !std::isfinite(displayPos.x) ||
                !std::isfinite(displayPos.y) ||
                !std::isfinite(framebufferScale.x) ||
                !std::isfinite(framebufferScale.y) ||
                framebufferScale.x <= 0.0f || framebufferScale.y <= 0.0f)
            {
                return std::nullopt;
            }

            double clipMinX =
                (static_cast<double>(clipRect.x) - displayPos.x) *
                framebufferScale.x;
            double clipMinY =
                (static_cast<double>(clipRect.y) - displayPos.y) *
                framebufferScale.y;
            double clipMaxX =
                (static_cast<double>(clipRect.z) - displayPos.x) *
                framebufferScale.x;
            double clipMaxY =
                (static_cast<double>(clipRect.w) - displayPos.y) *
                framebufferScale.y;
            if (!std::isfinite(clipMinX) || !std::isfinite(clipMinY) ||
                !std::isfinite(clipMaxX) || !std::isfinite(clipMaxY))
            {
                return std::nullopt;
            }

            clipMinX = std::clamp(
                clipMinX, 0.0, static_cast<double>(pixelWidth));
            clipMinY = std::clamp(
                clipMinY, 0.0, static_cast<double>(pixelHeight));
            clipMaxX = std::clamp(
                clipMaxX, 0.0, static_cast<double>(pixelWidth));
            clipMaxY = std::clamp(
                clipMaxY, 0.0, static_cast<double>(pixelHeight));
            if (clipMaxX <= clipMinX || clipMaxY <= clipMinY ||
                clipMinX >
                    static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
                clipMinY >
                    static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            {
                return std::nullopt;
            }

            Graphics::ImGuiOverlayScissor scissor{
                .X = static_cast<std::int32_t>(clipMinX),
                .Y = static_cast<std::int32_t>(clipMinY),
                .Width = static_cast<std::uint32_t>(clipMaxX - clipMinX),
                .Height = static_cast<std::uint32_t>(clipMaxY - clipMinY),
            };
            if (scissor.IsEmpty())
                return std::nullopt;
            return scissor;
        }

        [[nodiscard]] std::optional<Graphics::ImGuiOverlayDrawCommand>
        BuildOverlayDrawCommand(
            const ImDrawCmd& command,
            const ImDrawData& drawData,
            const std::uint32_t pixelWidth,
            const std::uint32_t pixelHeight,
            const ImTextureData* atlasTexData) noexcept
        {
            const std::optional<Graphics::ImGuiOverlayScissor> scissor =
                BuildOverlayScissor(
                    command.ClipRect,
                    drawData.DisplayPos,
                    drawData.FramebufferScale,
                    pixelWidth,
                    pixelHeight);
            if (!scissor.has_value())
                return std::nullopt;

            Graphics::ImGuiOverlayDrawCommand out{};
            out.IndexOffset = command.IdxOffset;
            out.VertexOffset = command.VtxOffset;
            out.IndexCount = command.ElemCount;
            out.Scissor = *scissor;
            ImTextureRef atlasTexRef = ImGui::GetIO().Fonts->TexRef;
            if (atlasTexData != nullptr)
            {
                atlasTexRef._TexData = const_cast<ImTextureData*>(atlasTexData);
                atlasTexRef._TexID = ImTextureID_Invalid;
            }
            out.UsesUserTexture = !TextureRefMatchesAtlas(command.TexRef, atlasTexRef);

            if (out.UsesUserTexture)
            {
                const ImTextureID texId =
                    command.TexRef._TexData != nullptr
                        ? command.TexRef._TexData->TexID
                        : command.TexRef._TexID;
                if (texId != ImTextureID_Invalid &&
                    texId <= static_cast<ImTextureID>(std::numeric_limits<RHI::BindlessIndex>::max()))
                {
                    out.TextureBindlessIndex = static_cast<RHI::BindlessIndex>(texId);
                }
            }

            return out;
        }

        [[nodiscard]] bool FontAtlasMetadataMatches(
            const Graphics::ImGuiOverlayFontAtlas& atlas,
            const std::uint32_t width,
            const std::uint32_t height,
            const std::uint32_t bytesPerPixel,
            const bool useColors) noexcept
        {
            return atlas.Valid &&
                   atlas.Width == width &&
                   atlas.Height == height &&
                   atlas.BytesPerPixel == bytesPerPixel &&
                   atlas.UseColors == useColors;
        }

        [[nodiscard]] std::uint64_t ElapsedMicros(
            const std::chrono::steady_clock::time_point start) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count());
        }
    }

    ImGuiAdapter::ImGuiAdapter(Platform::IWindow& window, Graphics::ImGuiOverlaySystem& overlaySystem)
        : m_Window(window)
        , m_OverlaySystem(overlaySystem)
    {
    }

    ImGuiAdapter::~ImGuiAdapter()
    {
        Shutdown();
    }

    bool ImGuiAdapter::Initialize()
    {
        if (m_Context != nullptr)
            return false;

        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_Context);
        m_PlotContext = ImPlot::CreateContext();
        ConfigureIo();
        m_OverlaySystem.Initialize();

        m_Diagnostics.Initialized = true;
        m_CaptureSnapshot = {};
        m_FrameStarted = false;
        m_FontAtlasCache = {};
        m_FontAtlasRevision = 0u;
        return true;
    }

    void ImGuiAdapter::ConfigureIo()
    {
        ImGui::SetCurrentContext(m_Context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // the engine owns persistence; never write imgui.ini
        io.LogFilename = nullptr;
        io.BackendPlatformName = "Extrinsic.Runtime.ImGuiAdapter";
        io.BackendRendererName = "Extrinsic.Graphics.ImGuiOverlaySystem";
        // The promoted renderer consumes a copied CPU font-atlas payload and
        // uploads it through `ImGuiOverlaySystem`, so do not advertise ImGui
        // 1.92 dynamic texture handling until graphics processes
        // ImDrawData::Textures[] and acknowledges ImTextureData requests.
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        platformIo.Platform_ClipboardUserData = this;
        platformIo.Platform_GetClipboardTextFn = &ImGuiAdapter::ClipboardGet;
        platformIo.Platform_SetClipboardTextFn = &ImGuiAdapter::ClipboardSet;

        ApplyDisplayMetrics(io, m_Window.GetWindowExtent(), m_Window.GetFramebufferExtent());
        ConfigureFonts(io);
        BuildLegacyFontAtlas(io);
    }

    void ImGuiAdapter::BeginFrame(double deltaSeconds)
    {
        if (m_Context == nullptr)
            return;

        const auto begin = std::chrono::steady_clock::now();
        ImGui::SetCurrentContext(m_Context);
        ImGuiIO& io = ImGui::GetIO();
        // Refresh from the window first; queued resize events pumped below win.
        ApplyDisplayMetrics(io, m_Window.GetWindowExtent(), m_Window.GetFramebufferExtent());
        io.DeltaTime = deltaSeconds > 0.0 ? static_cast<float>(deltaSeconds) : (1.0f / 60.0f);

        PumpEvents();

        ImGui::NewFrame();
        m_FrameStarted = true;
        m_Diagnostics.LastBeginFrameMicros = ElapsedMicros(begin);
    }

    void ImGuiAdapter::PumpEvents()
    {
        ImGuiIO& io = ImGui::GetIO();
        const std::vector<Platform::Event> events = m_Window.DrainEvents();
        for (const Platform::Event& event : events)
        {
            ++m_Diagnostics.PumpedEventCount;
            std::visit(
                [&io](const auto& e)
                {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, Platform::CursorEvent>)
                    {
                        io.AddMousePosEvent(static_cast<float>(e.XPos), static_cast<float>(e.YPos));
                    }
                    else if constexpr (std::is_same_v<T, Platform::MouseButtonEvent>)
                    {
                        if (e.ButtonCode >= 0 && e.ButtonCode < ImGuiMouseButton_COUNT)
                            io.AddMouseButtonEvent(e.ButtonCode, e.IsPressed);
                    }
                    else if constexpr (std::is_same_v<T, Platform::ScrollEvent>)
                    {
                        io.AddMouseWheelEvent(static_cast<float>(e.XOffset), static_cast<float>(e.YOffset));
                    }
                    else if constexpr (std::is_same_v<T, Platform::CharEvent>)
                    {
                        io.AddInputCharacter(e.Character);
                    }
                    // WindowResizeEvent is intentionally NOT written into
                    // io.DisplaySize. Display metrics are refreshed every frame
                    // from the authoritative window port (logical
                    // GetWindowExtent + framebuffer GetFramebufferExtent) at the
                    // top of BeginFrame, keeping io.DisplaySize logical and
                    // io.DisplayFramebufferScale carrying the pixel ratio. The
                    // resize payload is in backend-specific framebuffer pixels
                    // (the GLFW backend emits framebuffer width/height for both
                    // the resize and content-scale callbacks), so writing it into
                    // io.DisplaySize would double-scale the overlay frame on
                    // HiDPI displays (DisplaySize * DisplayFramebufferScale).
                    // WindowCloseEvent / KeyEvent / WindowDropEvent are likewise
                    // counted as pumped but not translated: GLFW key-code ->
                    // ImGuiKey mapping is owned by the editor input-binding slice
                    // (Slice A non-goal), and close/drop have no ImGui IO
                    // equivalent.
                },
                event);
        }
    }

    void ImGuiAdapter::EndFrame()
    {
        if (m_Context == nullptr || !m_FrameStarted)
            return;

        const auto endFrameBegin = std::chrono::steady_clock::now();
        m_Diagnostics.LastEditorCallbackMicros = 0u;
        m_Diagnostics.LastImGuiRenderMicros = 0u;
        m_Diagnostics.LastDrawDataCopyMicros = 0u;
        m_Diagnostics.LastEndFrameMicros = 0u;

        ImGui::SetCurrentContext(m_Context);
        if (m_EditorVisible && m_EditorCallback)
        {
            const auto callbackBegin = std::chrono::steady_clock::now();
            m_EditorCallback();
            m_Diagnostics.LastEditorCallbackMicros = ElapsedMicros(callbackBegin);
            ++m_Diagnostics.EditorCallbackInvocations;
        }

        if (m_EditorVisible)
        {
            const ImGuiIO& io = ImGui::GetIO();
            m_CaptureSnapshot = EditorInputCaptureSnapshot{
                .CapturedKeyboard = io.WantCaptureKeyboard,
                .CapturedMouse = io.WantCaptureMouse,
                .WidgetsActive = ImGui::IsAnyItemActive(),
            };
        }
        else
        {
            m_CaptureSnapshot = {};
        }
        ++m_Diagnostics.CaptureSnapshots;

        const auto renderBegin = std::chrono::steady_clock::now();
        ImGui::Render();
        m_Diagnostics.LastImGuiRenderMicros = ElapsedMicros(renderBegin);
        const auto copyBegin = std::chrono::steady_clock::now();
        const ImDrawData* drawData = ImGui::GetDrawData();
        unsigned char* fontPixels = nullptr;
        int fontWidth = 0;
        int fontHeight = 0;
        int fontBytesPerPixel = 0;
        ImGui::GetIO().Fonts->GetTexDataAsAlpha8(
            &fontPixels,
            &fontWidth,
            &fontHeight,
            &fontBytesPerPixel);
        // The font atlas texture reference. User-texture detection compares each
        // draw command's TexRef against this without calling GetTexID(): the
        // promoted renderer consumes a copied CPU atlas through GRAPHICS-079,
        // so the atlas has no ImGui-owned GPU id here.
        const ImTextureData* atlasTexData = ImGui::GetIO().Fonts->TexRef._TexData;

        Graphics::ImGuiOverlayFrame frame;
        frame.Enabled = true;
        m_Diagnostics.LastFrameFontAtlasCopyBytes = 0u;
        m_Diagnostics.LastFrameVertexCopyBytes = 0u;
        m_Diagnostics.LastFrameIndexCopyBytes = 0u;
        m_Diagnostics.LastFrameCommandCopyBytes = 0u;
        m_Diagnostics.LastFrameOverlayCopyBytes = 0u;
        if (fontPixels != nullptr && fontWidth > 0 && fontHeight > 0 &&
            (fontBytesPerPixel == 1 || fontBytesPerPixel == 4))
        {
            const std::uint64_t byteCount =
                static_cast<std::uint64_t>(fontWidth) *
                static_cast<std::uint64_t>(fontHeight) *
                static_cast<std::uint64_t>(fontBytesPerPixel);
            const std::uint32_t width = static_cast<std::uint32_t>(fontWidth);
            const std::uint32_t height = static_cast<std::uint32_t>(fontHeight);
            const std::uint32_t bytesPerPixel =
                static_cast<std::uint32_t>(fontBytesPerPixel);
            const bool useColors = fontBytesPerPixel == 4;
            const bool cacheMetadataMatches =
                FontAtlasMetadataMatches(
                    m_FontAtlasCache,
                    width,
                    height,
                    bytesPerPixel,
                    useColors);
            const bool cacheBytesMatch =
                cacheMetadataMatches &&
                static_cast<std::uint64_t>(m_FontAtlasCache.Pixels.size()) == byteCount &&
                std::memcmp(m_FontAtlasCache.Pixels.data(),
                            fontPixels,
                            static_cast<std::size_t>(byteCount)) == 0;

            m_Diagnostics.LastFontAtlasByteCount = byteCount;
            if (!cacheBytesMatch)
            {
                ++m_FontAtlasRevision;
                m_FontAtlasCache.Valid = true;
                m_FontAtlasCache.Width = width;
                m_FontAtlasCache.Height = height;
                m_FontAtlasCache.BytesPerPixel = bytesPerPixel;
                m_FontAtlasCache.UseColors = useColors;
                m_FontAtlasCache.Dirty = true;
                m_FontAtlasCache.Revision = m_FontAtlasRevision;
                m_FontAtlasCache.Pixels.resize(static_cast<std::size_t>(byteCount));
                std::memcpy(m_FontAtlasCache.Pixels.data(),
                            fontPixels,
                            static_cast<std::size_t>(byteCount));
                frame.FontAtlas = m_FontAtlasCache;
                ++m_Diagnostics.FontAtlasCopyCount;
                m_Diagnostics.LastFrameFontAtlasCopied = true;
                m_Diagnostics.LastFrameFontAtlasCopyBytes = byteCount;
            }
            else
            {
                frame.FontAtlas.Valid = true;
                frame.FontAtlas.Width = width;
                frame.FontAtlas.Height = height;
                frame.FontAtlas.BytesPerPixel = bytesPerPixel;
                frame.FontAtlas.UseColors = useColors;
                frame.FontAtlas.Dirty = false;
                frame.FontAtlas.Revision = m_FontAtlasRevision;
                ++m_Diagnostics.FontAtlasReuseCount;
                m_Diagnostics.LastFrameFontAtlasCopied = false;
                m_Diagnostics.LastFrameFontAtlasCopyBytes = 0u;
            }
        }
        else
        {
            m_Diagnostics.LastFontAtlasByteCount = 0u;
            m_Diagnostics.LastFrameFontAtlasCopied = false;
            m_Diagnostics.LastFrameFontAtlasCopyBytes = 0u;
        }

        std::uint32_t drawListCount = 0u;
        std::uint32_t vertexCount = 0u;
        std::uint32_t indexCount = 0u;
        std::uint32_t commandCount = 0u;
        bool usesUserTexture = false;
        std::uint32_t pixelWidth = 0u;
        std::uint32_t pixelHeight = 0u;

        if (drawData != nullptr && drawData->Valid)
        {
            pixelWidth = ToPixelDimension(drawData->DisplaySize.x * drawData->FramebufferScale.x);
            pixelHeight = ToPixelDimension(drawData->DisplaySize.y * drawData->FramebufferScale.y);
            frame.DrawLists.reserve(static_cast<std::size_t>(drawData->CmdListsCount));
            for (int i = 0; i < drawData->CmdListsCount; ++i)
            {
                const ImDrawList* cmdList = drawData->CmdLists[i];
                if (cmdList == nullptr)
                    continue;

                Graphics::ImGuiOverlayDrawList overlayList;
                overlayList.CommandCount = static_cast<std::uint32_t>(cmdList->CmdBuffer.Size);
                overlayList.VertexCount = static_cast<std::uint32_t>(cmdList->VtxBuffer.Size);
                overlayList.IndexCount = static_cast<std::uint32_t>(cmdList->IdxBuffer.Size);
                overlayList.Vertices.reserve(static_cast<std::size_t>(cmdList->VtxBuffer.Size));
                for (int v = 0; v < cmdList->VtxBuffer.Size; ++v)
                {
                    const ImDrawVert& src = cmdList->VtxBuffer[v];
                    overlayList.Vertices.push_back(Graphics::ImGuiOverlayVertex{
                        .Position = {
                            (src.pos.x - drawData->DisplayPos.x) *
                                drawData->FramebufferScale.x,
                            (src.pos.y - drawData->DisplayPos.y) *
                                drawData->FramebufferScale.y,
                        },
                        .UV = {src.uv.x, src.uv.y},
                        .Color = src.col,
                    });
                }
                overlayList.Indices.reserve(static_cast<std::size_t>(cmdList->IdxBuffer.Size));
                for (int idx = 0; idx < cmdList->IdxBuffer.Size; ++idx)
                {
                    overlayList.Indices.push_back(
                        static_cast<std::uint32_t>(cmdList->IdxBuffer[idx]));
                }
                overlayList.Commands.reserve(static_cast<std::size_t>(cmdList->CmdBuffer.Size));
                for (int c = 0; c < cmdList->CmdBuffer.Size; ++c)
                {
                    const ImDrawCmd& drawCommand = cmdList->CmdBuffer[c];
                    if (drawCommand.UserCallback != nullptr || drawCommand.ElemCount == 0u)
                    {
                        continue;
                    }

                    // A command referencing a texture other than the font atlas
                    // is a user texture (e.g. ImGui::Image). Preserve the
                    // command's bindless slot so graphics can push it directly
                    // without importing ImGui types or adding descriptor APIs.
                    const auto overlayCommand = BuildOverlayDrawCommand(
                        drawCommand,
                        *drawData,
                        pixelWidth,
                        pixelHeight,
                        atlasTexData);
                    if (!overlayCommand.has_value())
                        continue;
                    overlayList.Commands.push_back(*overlayCommand);
                    if (overlayCommand->UsesUserTexture)
                    {
                        overlayList.UsesUserTexture = true;
                        usesUserTexture = true;
                    }
                }
                overlayList.CommandCount = static_cast<std::uint32_t>(overlayList.Commands.size());

                drawListCount += 1u;
                vertexCount += overlayList.VertexCount;
                indexCount += overlayList.IndexCount;
                commandCount += overlayList.CommandCount;
                frame.DrawLists.push_back(overlayList);
            }
        }

        frame.DisplayWidth = pixelWidth;
        frame.DisplayHeight = pixelHeight;
        m_OverlaySystem.SubmitFrame(std::move(frame));

        m_Diagnostics.FramesProduced += 1u;
        m_Diagnostics.LastDrawListCount = drawListCount;
        m_Diagnostics.LastVertexCount = vertexCount;
        m_Diagnostics.LastIndexCount = indexCount;
        m_Diagnostics.LastCommandCount = commandCount;
        m_Diagnostics.LastFrameUsedUserTexture = usesUserTexture;
        m_Diagnostics.DisplayWidth = pixelWidth;
        m_Diagnostics.DisplayHeight = pixelHeight;
        m_Diagnostics.LastFrameVertexCopyBytes =
            static_cast<std::uint64_t>(vertexCount) *
            sizeof(Graphics::ImGuiOverlayVertex);
        m_Diagnostics.LastFrameIndexCopyBytes =
            static_cast<std::uint64_t>(indexCount) *
            sizeof(std::uint32_t);
        m_Diagnostics.LastFrameCommandCopyBytes =
            static_cast<std::uint64_t>(commandCount) *
            sizeof(Graphics::ImGuiOverlayDrawCommand);
        m_Diagnostics.LastFrameOverlayCopyBytes =
            m_Diagnostics.LastFrameFontAtlasCopyBytes +
            m_Diagnostics.LastFrameVertexCopyBytes +
            m_Diagnostics.LastFrameIndexCopyBytes +
            m_Diagnostics.LastFrameCommandCopyBytes;
        m_Diagnostics.LastDrawDataCopyMicros = ElapsedMicros(copyBegin);
        m_Diagnostics.LastEndFrameMicros = ElapsedMicros(endFrameBegin);
        m_FrameStarted = false;
    }

    void ImGuiAdapter::Shutdown()
    {
        if (m_Context == nullptr)
            return;

        m_OverlaySystem.Shutdown();
        DestroyContext();
        m_Diagnostics.Initialized = false;
        m_CaptureSnapshot = {};
        m_FontAtlasCache = {};
        m_FontAtlasRevision = 0u;
        m_FrameStarted = false;
    }

    void ImGuiAdapter::RebuildForDisplayChange()
    {
        if (m_Context == nullptr)
            return;

        // DPI / font rebuild: tear the overlay system + context down and back up
        // exactly once. The overlay system reallocates its (graphics-owned) font
        // atlas on Initialize() once GRAPHICS-079 lands; here it is a CPU no-op.
        m_OverlaySystem.Shutdown();
        DestroyContext();
        m_FontAtlasCache = {};
        m_FontAtlasRevision = 0u;

        m_Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_Context);
        m_PlotContext = ImPlot::CreateContext();
        ConfigureIo();
        m_OverlaySystem.Initialize();

        m_FrameStarted = false;
        m_CaptureSnapshot = {};
        m_Diagnostics.ContextRebuilds += 1u;
    }

    void ImGuiAdapter::DestroyContext()
    {
        if (m_Context == nullptr)
            return;
        if (m_PlotContext != nullptr)
        {
            ImPlot::DestroyContext(m_PlotContext);
            m_PlotContext = nullptr;
        }
        ImGui::DestroyContext(m_Context);
        m_Context = nullptr;
    }

    void ImGuiAdapter::SetEditorCallback(std::function<void()> callback)
    {
        m_EditorCallback = std::move(callback);
    }

    void ImGuiAdapter::SetEditorVisible(const bool visible) noexcept
    {
        m_EditorVisible = visible;
        if (!visible)
            m_CaptureSnapshot = {};
    }

    const char* ImGuiAdapter::ClipboardGet(ImGuiContext* /*ctx*/)
    {
        auto* self = static_cast<ImGuiAdapter*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
        if (self == nullptr)
            return "";
        self->m_ClipboardScratch = self->m_Window.GetClipboardText();
        return self->m_ClipboardScratch.c_str();
    }

    void ImGuiAdapter::ClipboardSet(ImGuiContext* /*ctx*/, const char* text)
    {
        auto* self = static_cast<ImGuiAdapter*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
        if (self == nullptr || text == nullptr)
            return;
        self->m_Window.SetClipboardText(text);
    }
}
