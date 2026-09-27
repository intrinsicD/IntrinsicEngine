module;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <stb_image_write.h>

module Extrinsic.Runtime.ViewCapture;

import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::size_t kFinishedHistory = 8;

        void AppendPngBytes(void* context, void* data, int size)
        {
            auto* out = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            out->insert(out->end(), bytes, bytes + size);
        }
    }

    ViewCaptureImage CropViewCaptureImage(const ViewCaptureImage& image, const Core::Rect2D rect)
    {
        const int x0 = std::clamp(rect.Offset.X, 0, static_cast<int>(image.Width));
        const int y0 = std::clamp(rect.Offset.Y, 0, static_cast<int>(image.Height));
        const int x1 = std::clamp(rect.Offset.X + rect.Extent.Width, 0, static_cast<int>(image.Width));
        const int y1 = std::clamp(rect.Offset.Y + rect.Extent.Height, 0, static_cast<int>(image.Height));
        if (x1 <= x0 || y1 <= y0) return image;
        ViewCaptureImage cropped{.Width = static_cast<std::uint32_t>(x1 - x0),
                                 .Height = static_cast<std::uint32_t>(y1 - y0)};
        cropped.Rgba8.resize(std::size_t{4} * cropped.Width * cropped.Height);
        const std::size_t rowBytes = std::size_t{4} * cropped.Width;
        for (std::uint32_t row = 0; row < cropped.Height; ++row)
        {
            const std::size_t source = (std::size_t(y0) + row) * image.Width * 4 + std::size_t(x0) * 4;
            std::copy_n(image.Rgba8.begin() + static_cast<std::ptrdiff_t>(source), rowBytes,
                        cropped.Rgba8.begin() + static_cast<std::ptrdiff_t>(row * rowBytes));
        }
        return cropped;
    }

    std::vector<std::uint8_t> EncodeViewCapturePng(const ViewCaptureImage& image)
    {
        std::vector<std::uint8_t> png;
        if (image.Width == 0 || image.Height == 0 ||
            image.Rgba8.size() != std::size_t{4} * image.Width * image.Height)
            return png;
        const int stride = static_cast<int>(image.Width * 4);
        if (stbi_write_png_to_func(&AppendPngBytes, &png, static_cast<int>(image.Width),
                                   static_cast<int>(image.Height), 4, image.Rgba8.data(), stride) == 0)
            png.clear();
        return png;
    }

    std::optional<std::string> WriteViewCaptureFile(const std::filesystem::path& path,
                                                     const std::span<const std::uint8_t> bytes)
    {
        std::error_code error;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return "Cannot create " + path.parent_path().string() + ": " + error.message();
        }
        std::filesystem::path temporary = path;
        temporary += ".partial";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) return "Cannot open " + temporary.string() + " for writing";
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (!out)
            {
                std::filesystem::remove(temporary, error);
                return "Cannot write " + temporary.string();
            }
        }
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            const std::string message = "Cannot move the capture to " + path.string() + ": " + error.message();
            std::filesystem::remove(temporary, error);
            return message;
        }
        return std::nullopt;
    }

    std::filesystem::path DefaultViewCapturePath(const std::filesystem::path& directory, const std::uint64_t ticket)
    {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char stamp[32]{};
        (void)std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
        return directory / ("intrinsic-" + std::string(stamp) + "-" + std::to_string(ticket) + ".png");
    }

    std::string_view ToString(const ViewCaptureRegion region) noexcept
    {
        switch (region)
        {
        case ViewCaptureRegion::Viewport: return "viewport";
        case ViewCaptureRegion::Window: return "window";
        }
        return "unknown";
    }

    std::string_view ToString(const ViewCaptureState state) noexcept
    {
        switch (state)
        {
        case ViewCaptureState::Unknown: return "unknown";
        case ViewCaptureState::Queued: return "queued";
        case ViewCaptureState::Pending: return "pending";
        case ViewCaptureState::Completed: return "completed";
        case ViewCaptureState::Failed: return "failed";
        }
        return "unknown";
    }

    ViewCaptureModule::ViewCaptureModule(std::filesystem::path screenshotDirectory)
        : m_Directory(std::move(screenshotDirectory))
    {
        if (m_Directory.empty())
        {
            std::error_code error;
            m_Directory = std::filesystem::current_path(error) / "screenshots";
        }
    }

    RuntimeModuleResult ViewCaptureModule::OnRegister(EngineSetup& setup)
    {
        if (auto provided = setup.Services().Provide<ViewCaptureModule>(*this, Name()); !provided) return provided;
        m_Registered = true;
        // Maintenance runs before the frame's render; the renderer allocates the readback
        // before the next frame and records the copy after that frame's graph.
        return setup.RegisterFrameHook(FramePhase::Maintenance,
                                       [this](RuntimeFrameHookContext& frame) { Advance(frame); });
    }

    void ViewCaptureModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (!m_Registered) return;
        (void)context.Services.Withdraw<ViewCaptureModule>(*this);
        m_Registered = false;
        for (auto& entry : m_Active)
        {
            entry.Status.State = ViewCaptureState::Failed;
            entry.Status.Diagnostic = "Engine shut down before the capture completed.";
        }
        m_Active.clear();
    }

    std::uint64_t ViewCaptureModule::Request(ViewCaptureRequest request)
    {
        Entry entry{};
        entry.Status.Ticket = m_NextTicket++;
        entry.Status.State = ViewCaptureState::Queued;
        entry.Status.Region = request.Region;
        entry.Request = std::move(request);
        const std::uint64_t ticket = entry.Status.Ticket;
        m_Active.push_back(std::move(entry));
        return ticket;
    }

    ViewCaptureStatus ViewCaptureModule::Status(const std::uint64_t ticket) const
    {
        for (const auto& entry : m_Active)
            if (entry.Status.Ticket == ticket) return entry.Status;
        for (const auto& entry : m_Finished)
            if (entry.Status.Ticket == ticket) return entry.Status;
        return ViewCaptureStatus{};
    }

    std::optional<ViewCaptureStatus> ViewCaptureModule::LastFinished() const
    {
        if (m_Finished.empty()) return std::nullopt;
        const ViewCaptureStatus& last = m_Finished.back().Status;
        return ViewCaptureStatus{.State = last.State, .Ticket = last.Ticket, .Region = last.Region, .Path = last.Path,
                                 .Width = last.Width, .Height = last.Height, .Diagnostic = last.Diagnostic};
    }

    std::optional<std::string> ViewCaptureModule::UnavailableReason() const
    {
        if (!m_DeviceOperational)
            return "Screenshots need an operational render device (this run uses the Null backend or the device failed).";
        return std::nullopt;
    }

    void ViewCaptureModule::Advance(RuntimeFrameHookContext& frame)
    {
        auto* renderer = frame.Services.Find<Graphics::IRenderer>();
        auto* device = frame.Services.Find<RHI::IDevice>();
        m_DeviceOperational = renderer != nullptr && device != nullptr && device->IsOperational();
        while (!m_Active.empty())
        {
            Entry& entry = m_Active.front();
            if (entry.Status.State == ViewCaptureState::Queued)
            {
                if (!m_DeviceOperational)
                {
                    entry.Status.State = ViewCaptureState::Failed;
                    entry.Status.Diagnostic = *UnavailableReason();
                    Finish(entry);
                    continue;
                }
                entry.RendererTicket = renderer->RequestBackbufferCapture();
                if (entry.RendererTicket == 0)
                {
                    entry.Status.State = ViewCaptureState::Failed;
                    entry.Status.Diagnostic = "The renderer refused the capture request.";
                    Finish(entry);
                    continue;
                }
                entry.Status.State = ViewCaptureState::Pending;
                // The scene rectangle the user sees now, in framebuffer pixels.
                if (const auto* window = frame.Services.Find<Platform::IWindow>())
                    entry.Viewport = ResolveSceneViewportPixels(window->GetWindowExtent(),
                                                                window->GetFramebufferExtent(),
                                                                frame.EditorCapture);
                return;
            }
            if (renderer == nullptr)
            {
                entry.Status.State = ViewCaptureState::Failed;
                entry.Status.Diagnostic = "The renderer went away before the capture completed.";
                Finish(entry);
                continue;
            }
            Graphics::BackbufferCaptureResult result = renderer->TakeBackbufferCapture(entry.RendererTicket);
            if (result.State == Graphics::BackbufferCaptureState::Pending) return;
            if (result.State != Graphics::BackbufferCaptureState::Ready)
            {
                entry.Status.State = ViewCaptureState::Failed;
                entry.Status.Diagnostic = result.Diagnostic.empty() ? "The capture was lost." : result.Diagnostic;
                Finish(entry);
                continue;
            }
            ViewCaptureImage image{.Width = result.Width, .Height = result.Height, .Rgba8 = std::move(result.Rgba8)};
            if (entry.Request.Region == ViewCaptureRegion::Viewport) image = CropViewCaptureImage(image, entry.Viewport);
            entry.Status.Width = image.Width;
            entry.Status.Height = image.Height;
            std::vector<std::uint8_t> png = EncodeViewCapturePng(image);
            if (png.empty())
            {
                entry.Status.State = ViewCaptureState::Failed;
                entry.Status.Diagnostic = "PNG encoding failed.";
                Finish(entry);
                continue;
            }
            if (entry.Request.SaveToFile)
            {
                const std::filesystem::path path = entry.Request.OutputPath.empty()
                    ? DefaultViewCapturePath(entry.Request.OutputDirectory.empty()
                                                 ? m_Directory
                                                 : std::filesystem::path(entry.Request.OutputDirectory),
                                             entry.Status.Ticket)
                    : std::filesystem::path(entry.Request.OutputPath);
                if (auto error = WriteViewCaptureFile(path, png))
                {
                    entry.Status.State = ViewCaptureState::Failed;
                    entry.Status.Diagnostic = std::move(*error);
                    Finish(entry);
                    continue;
                }
                entry.Status.Path = path.string();
            }
            if (entry.Request.KeepPng) entry.Status.Png = std::move(png);
            entry.Status.State = ViewCaptureState::Completed;
            Finish(entry);
        }
    }

    void ViewCaptureModule::Finish(Entry& entry)
    {
        if (entry.Status.State == ViewCaptureState::Failed)
            Core::Log::Warn("[ViewCapture] capture {} failed: {}", entry.Status.Ticket, entry.Status.Diagnostic);
        else if (!entry.Status.Path.empty())
            Core::Log::Info("[ViewCapture] saved {} ({}x{})", entry.Status.Path, entry.Status.Width, entry.Status.Height);
        m_Finished.push_back(std::move(entry));
        m_Active.pop_front();
        while (m_Finished.size() > kFinishedHistory) m_Finished.pop_front();
    }
}
