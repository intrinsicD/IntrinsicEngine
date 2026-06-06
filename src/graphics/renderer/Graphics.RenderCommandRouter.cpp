module;

#include <cstddef>
#include <utility>
#include <vector>

module Extrinsic.Graphics.RenderCommandRouter;

namespace Extrinsic::Graphics
{
    RenderCommandPassStatus MissingRenderCommandRouteStatus(
        const bool deviceOperational) noexcept
    {
        return deviceOperational
            ? RenderCommandPassStatus::SkippedUnavailable
            : RenderCommandPassStatus::SkippedNonOperational;
    }

    void RenderCommandRouter::Clear()
    {
        m_Entries.clear();
    }

    void RenderCommandRouter::Register(const FramePassId passId, Recorder recorder)
    {
        if (!passId.IsValid() || !recorder)
        {
            return;
        }

        for (Entry& entry : m_Entries)
        {
            if (entry.PassId == passId)
            {
                entry.Record = std::move(recorder);
                return;
            }
        }

        m_Entries.push_back(Entry{
            .PassId = passId,
            .Record = std::move(recorder),
        });
    }

    bool RenderCommandRouter::Dispatch(const RenderCommandRoute& route,
                                       RHI::ICommandContext& cmd,
                                       void* context) const
    {
        if (!route.PassId.IsValid())
        {
            return false;
        }

        for (const Entry& entry : m_Entries)
        {
            if (entry.PassId == route.PassId)
            {
                entry.Record(route, cmd, context);
                return true;
            }
        }
        return false;
    }

    bool RenderCommandRouter::HasRoute(const FramePassId passId) const noexcept
    {
        if (!passId.IsValid())
        {
            return false;
        }

        for (const Entry& entry : m_Entries)
        {
            if (entry.PassId == passId)
            {
                return true;
            }
        }
        return false;
    }

    std::size_t RenderCommandRouter::RouteCount() const noexcept
    {
        return m_Entries.size();
    }
}
