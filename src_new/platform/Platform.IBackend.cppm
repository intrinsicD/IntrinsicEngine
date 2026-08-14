module;

export module Platform.IBackend;

namespace Extrinsic::Platform
{
    export class IBackend
    {
    public:
        virtual ~IBackend() = default;

        virtual bool ShouldClose() const;
        virtual bool IsPaused() const;
        virtual void Pause();
        virtual void Resume();
        virtual void PollEvents();
        virtual void Close();
        virtual void SwapBuffers();
    };
}