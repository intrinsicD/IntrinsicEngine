module;
export module Extrinsic.RHI.CommandContext;

namespace Extrinsic::RHI
{
    export class ICommandContext
    {
    public:
        virtual ~ICommandContext() = default;

        virtual void Begin() = 0;
        virtual void End() = 0;
    };
}
