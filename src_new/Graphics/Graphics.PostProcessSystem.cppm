module;

#include <memory>

export module Extrinsic.Graphics.PostProcessSystem;

export namespace Extrinsic::Graphics
{
    class PostProcessSystem
    {
    public:
        PostProcessSystem();
        ~PostProcessSystem();

        PostProcessSystem(const PostProcessSystem&)            = delete;
        PostProcessSystem& operator=(const PostProcessSystem&) = delete;

        void Initialize();
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
