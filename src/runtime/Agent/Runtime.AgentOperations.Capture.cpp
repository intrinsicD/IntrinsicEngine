// Screenshot operations of the agent lane (RUNTIME-281): thin callers of ViewCaptureModule,
// the same queue the File menu and the Camera / Render window use. The reply is deferred
// until the renderer's readback lands a few frames later.
module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.AgentOperations;

import Extrinsic.Runtime.ViewCapture;

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;

        // Larger images are saved but not returned inline (MCP clients downscale or reject them).
        constexpr std::uint32_t kMaxInlineImageSide = 2048;

        std::string Dump(const Json& value) { return value.dump(-1, ' ', false, Json::error_handler_t::replace); }
        AgentOperationOutcome Fail(std::string message) { return {.IsError = true, .Text = std::move(message)}; }

        struct CaptureArguments
        {
            ViewCaptureRegion Region{ViewCaptureRegion::Viewport};
            std::string Path{};
            bool PathOnly{false};
        };

        std::optional<CaptureArguments> ParseArguments(std::string_view text, std::string& error)
        {
            const Json args = Json::parse(text, nullptr, false);
            if (args.is_discarded() || !args.is_object()) { error = "Arguments must be a JSON object."; return std::nullopt; }
            CaptureArguments out{};
            if (const auto it = args.find("region"); it != args.end())
            {
                if (!it->is_string() || (*it != "viewport" && *it != "window"))
                { error = "region must be \"viewport\" or \"window\"."; return std::nullopt; }
                out.Region = *it == "window" ? ViewCaptureRegion::Window : ViewCaptureRegion::Viewport;
            }
            if (const auto it = args.find("path"); it != args.end())
            {
                if (!it->is_string()) { error = "path must be a string."; return std::nullopt; }
                out.Path = it->get<std::string>();
            }
            if (const auto it = args.find("path_only"); it != args.end())
            {
                if (!it->is_boolean()) { error = "path_only must be a boolean."; return std::nullopt; }
                out.PathOnly = it->get<bool>();
            }
            return out;
        }

        AgentOperationOutcome StartCapture(const AgentOperationContext& context, ViewCaptureRequest request,
                                           const bool returnImage)
        {
            if (context.ViewCapture == nullptr) return Fail("Screenshots are not available in this Sandbox build.");
            if (auto reason = context.ViewCapture->UnavailableReason()) return Fail(std::move(*reason));
            request.KeepPng = returnImage;
            const std::uint64_t ticket = context.ViewCapture->Request(std::move(request));
            AgentOperationOutcome outcome{};
            outcome.Continuation = [ticket, returnImage](const AgentOperationContext& ctx, AgentOperationOutcome& out)
            {
                if (ctx.ViewCapture == nullptr) { out = Fail("The capture service went away."); return true; }
                const ViewCaptureStatus status = ctx.ViewCapture->Status(ticket);
                if (status.State == ViewCaptureState::Queued || status.State == ViewCaptureState::Pending) return false;
                if (status.State != ViewCaptureState::Completed)
                {
                    out = Fail(status.Diagnostic.empty() ? "The capture was lost." : status.Diagnostic);
                    return true;
                }
                const bool inline_ = returnImage && !status.Png.empty() && status.Width <= kMaxInlineImageSide &&
                                     status.Height <= kMaxInlineImageSide;
                Json result{{"ticket", status.Ticket}, {"region", std::string(ToString(status.Region))},
                            {"width", status.Width}, {"height", status.Height}, {"image_returned", inline_}};
                if (!status.Path.empty()) result["path"] = status.Path;
                out = AgentOperationOutcome{.Text = Dump(result)};
                if (inline_) out.Images.push_back({.MimeType = "image/png", .Base64Data = EncodeBase64(status.Png)});
                return true;
            };
            return outcome;
        }

        constexpr std::string_view kRegionSchema =
            R"("region":{"type":"string","enum":["viewport","window"],"description":"viewport: the 3D scene rectangle (default); window: the whole Sandbox window with its panels"})";
    }

    std::string EncodeBase64(const std::span<const std::uint8_t> bytes)
    {
        static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        std::size_t i = 0;
        for (; i + 3 <= bytes.size(); i += 3)
        {
            const std::uint32_t v = (std::uint32_t(bytes[i]) << 16) | (std::uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
            out += kAlphabet[(v >> 18) & 63]; out += kAlphabet[(v >> 12) & 63];
            out += kAlphabet[(v >> 6) & 63];  out += kAlphabet[v & 63];
        }
        if (const std::size_t rest = bytes.size() - i; rest > 0)
        {
            std::uint32_t v = std::uint32_t(bytes[i]) << 16;
            if (rest == 2) v |= std::uint32_t(bytes[i + 1]) << 8;
            out += kAlphabet[(v >> 18) & 63]; out += kAlphabet[(v >> 12) & 63];
            out += rest == 2 ? kAlphabet[(v >> 6) & 63] : '=';
            out += '=';
        }
        return out;
    }

    void RegisterViewCaptureAgentOperations(AgentOperationRegistry& registry)
    {
        (void)registry.Register({
            .Name = "view_screenshot",
            .Title = "Look at the Sandbox",
            .Description = "Returns a PNG of what the Sandbox shows right now (the 3D viewport by default, or the "
                           "whole window with its panels) without writing a file. Completes a few frames later.",
            .InputSchemaJson = std::string(R"({"type":"object","properties":{)") + std::string(kRegionSchema) +
                               R"(},"additionalProperties":false})",
            .ReadOnly = true,
            .Invoke = [](const AgentOperationContext& context, std::string_view argumentsJson)
            {
                std::string error;
                const auto args = ParseArguments(argumentsJson, error);
                if (!args) return Fail(std::move(error));
                if (!args->Path.empty() || args->PathOnly) return Fail("view_screenshot takes only 'region'; use view_capture to save a file.");
                return StartCapture(context, ViewCaptureRequest{.Region = args->Region, .SaveToFile = false}, true);
            },
        });
        (void)registry.Register({
            .Name = "view_capture",
            .Title = "Save a screenshot",
            .Description = "Saves a PNG of the viewport or window, like File > Save Screenshot, and returns it. "
                           "'path' must stay inside the Sandbox's allowed roots (default: screenshots/ with a "
                           "timestamped name); path_only skips returning the image.",
            .InputSchemaJson = std::string(R"({"type":"object","properties":{)") + std::string(kRegionSchema) +
                               R"(,"path":{"type":"string","description":"PNG file path, relative to the first allowed root"},)"
                               R"("path_only":{"type":"boolean","description":"Return only the saved path and size"}},"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [](const AgentOperationContext& context, std::string_view argumentsJson)
            {
                std::string error;
                const auto args = ParseArguments(argumentsJson, error);
                if (!args) return Fail(std::move(error));
                // A given path names the file; otherwise a timestamped file goes to screenshots/.
                const auto resolved = ResolveAgentPath(context, args->Path.empty() ? "screenshots" : args->Path);
                if (!resolved) return Fail("path '" + args->Path + "' is outside the Sandbox's allowed roots.");
                ViewCaptureRequest request{.Region = args->Region, .SaveToFile = true};
                (args->Path.empty() ? request.OutputDirectory : request.OutputPath) = *resolved;
                return StartCapture(context, std::move(request), !args->PathOnly);
            },
        });
    }
}
