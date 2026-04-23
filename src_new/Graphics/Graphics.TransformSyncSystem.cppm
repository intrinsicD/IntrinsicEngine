//
// Created by alex on 22.04.26.
//

module;

#include <memory>

export module Extrinsic.Graphics.TransformSyncSystem;

export namespace Extrinsic::Graphics
{
    class TransformSyncSystem
    {
    public:
        TransformSyncSystem();
        ~TransformSyncSystem();

        TransformSyncSystem(const TransformSyncSystem&)            = delete;
        TransformSyncSystem& operator=(const TransformSyncSystem&) = delete;

        void Initialize();
        void Shutdown();
        void SyncGpuBuffer();

        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
