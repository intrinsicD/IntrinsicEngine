module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.RenderCommandRouter;

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    export enum class RenderCommandPassStatus : std::uint8_t
    {
        Recorded,
        SkippedNonOperational,
        SkippedUnavailable,
    };

    export [[nodiscard]] RenderCommandPassStatus MissingRenderCommandRouteStatus(
        bool deviceOperational) noexcept;

    export struct RenderCommandRoute
    {
        FramePassId PassId{};
        std::string_view DebugName{};
    };

    export class RenderCommandRouter final
    {
    public:
        using Recorder = std::function<void(const RenderCommandRoute& route,
                                            RHI::ICommandContext& cmd,
                                            void* context)>;

        void Clear();
        void Register(FramePassId passId, Recorder recorder);
        [[nodiscard]] bool Dispatch(const RenderCommandRoute& route,
                                    RHI::ICommandContext& cmd,
                                    void* context = nullptr) const;
        [[nodiscard]] bool HasRoute(FramePassId passId) const noexcept;
        [[nodiscard]] std::size_t RouteCount() const noexcept;

    private:
        struct Entry
        {
            FramePassId PassId{};
            Recorder Record{};
        };

        std::vector<Entry> m_Entries{};
    };
}
