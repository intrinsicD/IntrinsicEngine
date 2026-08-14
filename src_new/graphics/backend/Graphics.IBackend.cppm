module;

export module Graphics.IBackend;

namespace Extrinsic::Graphics
{
    export class IBackend
    {
    public:
        virtual ~IBackend() = default;
    };
}