module;

#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <cmath>
#include <memory>
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

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <stb_image_write.h>

module Extrinsic.Runtime.ViewCapture;

import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Geometry.Properties.Types;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.ServiceRegistry;

#include "Editor/internal/Runtime.EditorVisualizationHelpers.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::size_t kFinishedHistory = 8;
        // Frames between moving the camera and requesting the capture, so the new view
        // (and any upload it triggers) is on screen.
        constexpr std::uint32_t kPresetSettleFrames = 3;
        constexpr std::array<std::string_view, 6> kColormapNames{"Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};

        std::uint8_t LinearToSrgb(std::uint8_t value)
        {
            const double c = value / 255.0;
            const double encoded = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
            return static_cast<std::uint8_t>(std::clamp(encoded * 255.0 + 0.5, 0.0, 255.0));
        }

        std::optional<ECS::EntityHandle> ResolveEntity(const entt::registry& raw, std::uint32_t stableId)
        {
            const ECS::EntityHandle entity = StableEntityLookup::ToEntityHandle(stableId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity)) return std::nullopt;
            return entity;
        }

        // `Current` never reaches staging; it maps to Front like the old axes table did.
        CameraViewPreset ToCameraViewPreset(const ViewCapturePreset preset) noexcept
        {
            switch (preset)
            {
            case ViewCapturePreset::Current:
            case ViewCapturePreset::Front: return CameraViewPreset::Front;
            case ViewCapturePreset::Back: return CameraViewPreset::Back;
            case ViewCapturePreset::Left: return CameraViewPreset::Left;
            case ViewCapturePreset::Right: return CameraViewPreset::Right;
            case ViewCapturePreset::Top: return CameraViewPreset::Top;
            case ViewCapturePreset::Bottom: return CameraViewPreset::Bottom;
            case ViewCapturePreset::Isometric: return CameraViewPreset::Isometric;
            }
            return CameraViewPreset::Front;
        }

        // Moves the main camera to the preset and returns how to put it back.
        std::optional<std::string> StageCamera(RuntimeFrameHookContext& frame, const ViewCaptureRequest& request,
                                               std::function<void()>& restore)
        {
            auto* cameras = frame.Services.Find<CameraControllerRegistry>();
            ICameraController* controller = cameras ? cameras->ResolveOrNull(CameraControllerSlot::Main) : nullptr;
            if (controller == nullptr) return "Camera presets need a main camera (camera module).";
            const auto& raw = frame.ActiveWorld.Raw();
            std::vector<ECS::EntityHandle> entities;
            if (request.FitEntity != 0)
            {
                const auto entity = ResolveEntity(raw, request.FitEntity);
                if (!entity) return "Unknown fit entity " + std::to_string(request.FitEntity) + ".";
                entities.push_back(*entity);
            }
            else
                for (const auto entity : raw.view<ECS::Components::Culling::World::Bounds>()) entities.push_back(entity);
            const auto target = ComputeFocusTargetForEntities(frame.ActiveWorld, entities);
            if (!target) return "Nothing with world bounds to frame (imported geometry has bounds); use the current view.";

            const auto* window = frame.Services.Find<Platform::IWindow>();
            const Core::Extent2D extent = window ? window->GetFramebufferExtent() : Core::Extent2D{1, 1};
            std::shared_ptr<ICameraController> saved = controller->Clone();
            const Graphics::CameraViewInput current = controller->GetView(extent);
            restore = [cameras, saved, current] {
                if (saved) cameras->Replace(CameraControllerSlot::Main, saved->Clone());
                else if (auto* main = cameras->ResolveOrNull(CameraControllerSlot::Main)) main->Seed(current);
                cameras->MarkCameraTransition(CameraControllerSlot::Main);
            };
            ApplyCameraPreset(*cameras, CameraControllerSlot::Main, ToCameraViewPreset(request.Preset), *target, extent);
            return std::nullopt;
        }

        // The scalar visualization shown on `stableId`, with the range the renderer uses
        // (auto ranges are resolved through the same recipe encoding as extraction).
        std::optional<ViewCaptureLegend> ResolveLegend(const entt::registry& raw, std::uint32_t stableId, std::string& error)
        {
            using Config = Graphics::Components::VisualizationConfig;
            const auto entity = ResolveEntity(raw, stableId);
            // The surface, edge or point lane that colors by a scalar field (Show / Appearance).
            std::optional<Config> effective;
            if (entity)
                for (const auto target : {EditorVisualizationTarget::Surface, EditorVisualizationTarget::Edges,
                                          EditorVisualizationTarget::Points, EditorVisualizationTarget::Entity})
                {
                    effective = EditorFeatureDetail::EffectiveVisualizationConfigForTarget(raw, *entity, target);
                    if (effective && effective->Source == Config::ColorSource::ScalarField) break;
                    effective.reset();
                }
            const Config* config = effective ? &*effective : nullptr;
            if (config == nullptr || config->ScalarFieldName.empty())
            {
                error = "Entity " + std::to_string(stableId) + " shows no scalar field; color it by a property first (show_property).";
                return std::nullopt;
            }
            const auto map = static_cast<std::size_t>(config->Scalar.Map);
            ViewCaptureLegend legend{.Map = config->Scalar.Map, .Property = config->ScalarFieldName,
                                     .Colormap = std::string(map < kColormapNames.size() ? kColormapNames[map] : "Unknown"),
                                     .Min = config->Scalar.RangeMin, .Max = config->Scalar.RangeMax,
                                     .AutoRange = config->Scalar.AutoRange};
            if (!legend.AutoRange) return legend;
            using D = GeometryElementDomain;
            using K = Geometry::PropertyValueKind;
            std::vector<D> domains;
            switch (config->ScalarDomain)
            {
            case Config::Domain::Vertex: domains = {D::MeshVertex, D::GraphNode, D::PointCloudPoint}; break;
            case Config::Domain::Edge: domains = {D::MeshEdge, D::GraphEdge}; break;
            case Config::Domain::Face: domains = {D::MeshFace}; break;
            }
            const auto availability = BuildGeometryAvailability(raw, *entity);
            for (const auto domain : domains)
                for (const auto kind : {K::Float, K::Double, K::Int32, K::UInt32, K::UInt64, K::Bool})
                {
                    const auto encoded = EncodeVisualizationRecipe(availability, VisualizationRecipe{
                        .Data = ScalarVisualizationRecipe{.Source = {domain, config->ScalarFieldName, kind},
                                                          .OutputName = config->ScalarFieldName}});
                    if (encoded.Succeeded() && !encoded.Batch.Scalars.empty())
                    {
                        legend.Min = encoded.Batch.Scalars.front().RangeMin;
                        legend.Max = encoded.Batch.Scalars.front().RangeMax;
                        return legend;
                    }
                }
            error = "Cannot resolve the automatic range of '" + config->ScalarFieldName + "'.";
            return std::nullopt;
        }

        void AppendPngBytes(void* context, void* data, int size)
        {
            auto* out = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            out->insert(out->end(), bytes, bytes + size);
        }
    }

    void AppendViewCaptureLegend(ViewCaptureImage& image, const std::span<const std::uint8_t> lutRgb)
    {
        const std::size_t entries = lutRgb.size() / 3;
        if (image.Width == 0 || entries == 0) return;
        constexpr std::uint32_t kMargin = 4;
        const std::uint32_t margin = std::min(kMargin, image.Width / 4);
        image.Rgba8.resize(std::size_t{4} * image.Width * (image.Height + kViewCaptureLegendHeight));
        for (std::uint32_t row = 0; row < kViewCaptureLegendHeight; ++row)
            for (std::uint32_t x = 0; x < image.Width; ++x)
            {
                std::uint8_t* pixel = image.Rgba8.data() + (std::size_t(image.Height + row) * image.Width + x) * 4;
                const bool strip = row >= kMargin && row < kViewCaptureLegendHeight - kMargin && x >= margin &&
                                   x < image.Width - margin;
                if (strip)
                {
                    const double t = image.Width - 2 * margin > 1 ? double(x - margin) / double(image.Width - 2 * margin - 1) : 0.0;
                    const std::size_t entry = std::min(entries - 1, static_cast<std::size_t>(t * double(entries - 1) + 0.5));
                    pixel[0] = lutRgb[entry * 3 + 0];
                    pixel[1] = lutRgb[entry * 3 + 1];
                    pixel[2] = lutRgb[entry * 3 + 2];
                }
                else
                    pixel[0] = pixel[1] = pixel[2] = 24;
                pixel[3] = 255;
            }
        image.Height += kViewCaptureLegendHeight;
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

    std::optional<ViewCaptureWriteError> WriteViewCaptureFile(const std::filesystem::path& path,
                                                              const std::span<const std::uint8_t> bytes, const bool overwrite)
    {
        namespace fs = std::filesystem;
        const auto exists = [](const fs::path& candidate) {
            std::error_code status;
            const fs::file_status s = fs::symlink_status(candidate, status); // a dangling symlink occupies the path
            return !status && s.type() != fs::file_type::not_found;
        };
        const auto fileExists = [&] {
            return ViewCaptureWriteError{ViewCaptureFailure::FileExists, path.string() + " already exists"};
        };
        std::error_code error;
        if (!overwrite && exists(path)) return fileExists();
        if (path.has_parent_path())
        {
            fs::create_directories(path.parent_path(), error);
            if (error) return ViewCaptureWriteError{ViewCaptureFailure::None,
                                                    "Cannot create " + path.parent_path().string() + ": " + error.message()};
        }
        // A unique name per write: concurrent captures and user files named *.partial stay untouched.
        static std::atomic<std::uint64_t> counter{0};
        fs::path temporary;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            temporary = path;
            temporary += ".partial-" + std::to_string(counter.fetch_add(1) + 1) + "-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            if (!exists(temporary)) break;
            temporary.clear();
        }
        if (temporary.empty()) return ViewCaptureWriteError{ViewCaptureFailure::None, "Cannot pick a temporary name next to " + path.string()};
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) return ViewCaptureWriteError{ViewCaptureFailure::None, "Cannot open " + temporary.string() + " for writing"};
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (!out)
            {
                fs::remove(temporary, error);
                return ViewCaptureWriteError{ViewCaptureFailure::None, "Cannot write " + temporary.string()};
            }
        }
        if (!overwrite)
        {
            fs::create_hard_link(temporary, path, error);
            const std::error_code linkError = error;
            fs::remove(temporary, error);
            if (!linkError) return std::nullopt;
            if (linkError == std::errc::file_exists) return fileExists();
#if !defined(_WIN32)
            // Filesystems without hard links (FAT, some network mounts): create the target
            // exclusively and write it in place. Not atomic for readers, but it never replaces.
            const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
            if (fd < 0)
            {
                if (errno == EEXIST) return fileExists();
                return ViewCaptureWriteError{ViewCaptureFailure::None, "Cannot create " + path.string() + ": " +
                                                                           std::error_code(errno, std::generic_category()).message()};
            }
            std::size_t written = 0;
            while (written < bytes.size())
            {
                const ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                written += static_cast<std::size_t>(n);
            }
            const bool closed = ::close(fd) == 0;
            if (written != bytes.size() || !closed)
            {
                std::error_code ignored;
                fs::remove(path, ignored);
                return ViewCaptureWriteError{ViewCaptureFailure::None, "Cannot write " + path.string()};
            }
            return std::nullopt;
#else
            return ViewCaptureWriteError{ViewCaptureFailure::None,
                                         "Cannot publish " + path.string() + " without replacing a file: " + linkError.message()};
#endif
        }
        fs::rename(temporary, path, error);
        if (error)
        {
            ViewCaptureWriteError failure{ViewCaptureFailure::None,
                                          "Cannot move the capture to " + path.string() + ": " + error.message()};
            fs::remove(temporary, error);
            return failure;
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

    std::string_view ToString(const ViewCapturePreset preset) noexcept
    {
        switch (preset)
        {
        case ViewCapturePreset::Current: return "current";
        case ViewCapturePreset::Front: return "front";
        case ViewCapturePreset::Back: return "back";
        case ViewCapturePreset::Left: return "left";
        case ViewCapturePreset::Right: return "right";
        case ViewCapturePreset::Top: return "top";
        case ViewCapturePreset::Bottom: return "bottom";
        case ViewCapturePreset::Isometric: return "isometric";
        }
        return "current";
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
        entry.Status.Preset = request.Preset;
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
                if (entry.Request.Preset != ViewCapturePreset::Current)
                {
                    if (auto error = StageCamera(frame, entry.Request, entry.Restore))
                    {
                        entry.Status.State = ViewCaptureState::Failed;
                        entry.Status.Diagnostic = std::move(*error);
                        Finish(entry);
                        continue;
                    }
                    entry.SettleFrames = kPresetSettleFrames;
                }
                entry.Status.State = ViewCaptureState::Pending;
            }
            if (entry.RendererTicket == 0)
            {
                if (entry.SettleFrames > 0)
                {
                    --entry.SettleFrames;
                    return;
                }
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
            if (entry.Request.LegendEntity != 0)
            {
                std::string error;
                entry.Status.Legend = ResolveLegend(frame.ActiveWorld.Raw(), entry.Request.LegendEntity, error);
                if (!entry.Status.Legend)
                {
                    entry.Status.State = ViewCaptureState::Failed;
                    entry.Status.Diagnostic = std::move(error);
                    Finish(entry);
                    continue;
                }
                // The strip shows the LUT the way the frame shows it: LUT bytes are linear,
                // an sRGB backbuffer encodes them on write.
                const auto& colormaps = renderer->GetColormapSystem();
                std::vector<std::uint8_t> lut(256 * 3);
                for (int i = 0; i < 256; ++i)
                {
                    const auto c = colormaps.SampleCpu(entry.Status.Legend->Map, float(i) / 255.0f);
                    lut[std::size_t(i) * 3 + 0] = result.Srgb ? LinearToSrgb(c.R) : c.R;
                    lut[std::size_t(i) * 3 + 1] = result.Srgb ? LinearToSrgb(c.G) : c.G;
                    lut[std::size_t(i) * 3 + 2] = result.Srgb ? LinearToSrgb(c.B) : c.B;
                }
                AppendViewCaptureLegend(image, lut);
            }
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
                if (auto error = WriteViewCaptureFile(path, png, entry.Request.Overwrite || entry.Request.OutputPath.empty()))
                {
                    entry.Status.State = ViewCaptureState::Failed;
                    entry.Status.Diagnostic = std::move(error->Message);
                    entry.Status.Failure = error->Kind;
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
        if (entry.Restore) std::exchange(entry.Restore, {})();
        if (entry.Status.State == ViewCaptureState::Failed)
            Core::Log::Warn("[ViewCapture] capture {} failed: {}", entry.Status.Ticket, entry.Status.Diagnostic);
        else if (!entry.Status.Path.empty())
            Core::Log::Info("[ViewCapture] saved {} ({}x{})", entry.Status.Path, entry.Status.Width, entry.Status.Height);
        m_Finished.push_back(std::move(entry));
        m_Active.pop_front();
        while (m_Finished.size() > kFinishedHistory) m_Finished.pop_front();
    }
}
