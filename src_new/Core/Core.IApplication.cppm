export module Extrinsic.Core.Application;

namespace Extrinsic::Runtime
{
    class Engine;
}

namespace Extrinsic::Core
{
    export class IApplication
    {
    public:
        virtual ~IApplication() = default;

        virtual void OnInitialize(Runtime::Engine& engine) = 0;
        virtual void OnUpdate(Runtime::Engine& engine, double deltaSeconds) = 0;
        virtual void OnShutdown(Runtime::Engine& engine) = 0;
    };
}