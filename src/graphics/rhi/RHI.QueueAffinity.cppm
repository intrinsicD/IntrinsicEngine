module;

#include <cstdint>

export module Extrinsic.RHI.QueueAffinity;

export namespace Extrinsic::RHI
{
    enum class QueueAffinity : std::uint8_t
    {
        Graphics = 0,
        AsyncCompute,
        Transfer,
    };

    struct QueueCapabilityProfile
    {
        bool SupportsAsyncCompute = false;
        bool SupportsTransfer = false;
    };

    struct QueueAffinityResolution
    {
        QueueAffinity Requested = QueueAffinity::Graphics;
        QueueAffinity Resolved = QueueAffinity::Graphics;
        bool Demoted = false;
    };

    [[nodiscard]] constexpr QueueAffinityResolution ResolveQueueAffinity(const QueueAffinity requested,
                                                                         const QueueCapabilityProfile profile) noexcept
    {
        switch (requested)
        {
        case QueueAffinity::Graphics:
            return QueueAffinityResolution{
                .Requested = requested,
                .Resolved = QueueAffinity::Graphics,
                .Demoted = false,
            };
        case QueueAffinity::AsyncCompute:
            return QueueAffinityResolution{
                .Requested = requested,
                .Resolved = profile.SupportsAsyncCompute ? QueueAffinity::AsyncCompute : QueueAffinity::Graphics,
                .Demoted = !profile.SupportsAsyncCompute,
            };
        case QueueAffinity::Transfer:
            return QueueAffinityResolution{
                .Requested = requested,
                .Resolved = profile.SupportsTransfer ? QueueAffinity::Transfer : QueueAffinity::Graphics,
                .Demoted = !profile.SupportsTransfer,
            };
        }

        return QueueAffinityResolution{
            .Requested = requested,
            .Resolved = QueueAffinity::Graphics,
            .Demoted = requested != QueueAffinity::Graphics,
        };
    }

    [[nodiscard]] constexpr const char* QueueAffinityName(const QueueAffinity affinity) noexcept
    {
        switch (affinity)
        {
        case QueueAffinity::Graphics: return "graphics";
        case QueueAffinity::AsyncCompute: return "async_compute";
        case QueueAffinity::Transfer: return "transfer";
        }
        return "unknown";
    }
}
