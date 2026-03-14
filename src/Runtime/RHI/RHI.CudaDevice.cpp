module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <string_view>
#include <cuda.h>

module RHI:CudaDevice.Impl;

import :CudaDevice;
import :CudaError;
import Core.Logging;

namespace RHI
{
    // Map CUresult to our domain error type.
    static CudaError MapDriverError(CUresult result)
    {
        switch (result)
        {
            case CUDA_SUCCESS:                         return CudaError::Success;
            case CUDA_ERROR_NOT_INITIALIZED:
            case CUDA_ERROR_DEINITIALIZED:             return CudaError::NotInitialized;
            case CUDA_ERROR_NO_DEVICE:                 return CudaError::NoDevice;
            case CUDA_ERROR_INVALID_DEVICE:            return CudaError::InvalidDevice;
            case CUDA_ERROR_OUT_OF_MEMORY:             return CudaError::OutOfMemory;
            case CUDA_ERROR_INVALID_VALUE:             return CudaError::InvalidPointer;
            case CUDA_ERROR_NOT_FOUND:                 return CudaError::KernelNotFound;
            case CUDA_ERROR_LAUNCH_FAILED:             return CudaError::LaunchFailed;
            case CUDA_ERROR_INVALID_IMAGE:             return CudaError::ModuleLoadFailed;
            default:                                   return CudaError::Unknown;
        }
    }

    // Convenience: check CUresult and return unexpected on failure.
    static std::expected<void, CudaError> Check(CUresult result)
    {
        if (result == CUDA_SUCCESS)
            return {};
        return std::unexpected(MapDriverError(result));
    }

    class ScopedCudaContext
    {
    public:
        ScopedCudaContext() = default;
        ScopedCudaContext(const ScopedCudaContext&) = delete;
        ScopedCudaContext& operator=(const ScopedCudaContext&) = delete;

        ScopedCudaContext(ScopedCudaContext&& other) noexcept
            : m_Context(other.m_Context)
            , m_Operation(other.m_Operation)
            , m_Active(other.m_Active)
        {
            other.m_Context = nullptr;
            other.m_Operation = {};
            other.m_Active = false;
        }

        ScopedCudaContext& operator=(ScopedCudaContext&&) = delete;

        ~ScopedCudaContext()
        {
            (void)Pop();
        }

        [[nodiscard]] static std::expected<ScopedCudaContext, CudaError>
        Push(CUcontext context, std::string_view operation)
        {
            if (context == nullptr)
            {
                Core::Log::Error("{}: CUDA context is null.", operation);
                return std::unexpected(CudaError::NotInitialized);
            }

            const CUresult result = cuCtxPushCurrent(context);
            if (result != CUDA_SUCCESS)
            {
                Core::Log::Error("{}: cuCtxPushCurrent failed (CUresult={}).",
                                 operation,
                                 static_cast<int>(result));
                return std::unexpected(MapDriverError(result));
            }

            ScopedCudaContext guard;
            guard.m_Context = context;
            guard.m_Operation = operation;
            guard.m_Active = true;
            return guard;
        }

        [[nodiscard]] std::expected<void, CudaError> Finish()
        {
            if (const CUresult result = Pop(); result != CUDA_SUCCESS)
                return std::unexpected(MapDriverError(result));
            return {};
        }

    private:
        [[nodiscard]] CUresult Pop()
        {
            if (!m_Active)
                return CUDA_SUCCESS;

            CUcontext poppedContext = nullptr;
            const CUresult result = cuCtxPopCurrent(&poppedContext);
            m_Active = false;

            if (result != CUDA_SUCCESS)
            {
                Core::Log::Error("{}: cuCtxPopCurrent failed (CUresult={}).",
                                 m_Operation,
                                 static_cast<int>(result));
                return result;
            }

            if (poppedContext != m_Context)
                Core::Log::Warn("{}: cuCtxPopCurrent returned an unexpected context handle.",
                                m_Operation);

            m_Context = nullptr;
            m_Operation = {};
            return CUDA_SUCCESS;
        }

        CUcontext m_Context = nullptr;
        std::string_view m_Operation{};
        bool m_Active = false;
    };

    std::expected<std::unique_ptr<CudaDevice>, CudaError> CudaDevice::Create(int deviceOrdinal)
    {
        // Step 1: Initialize the CUDA driver API.
        CUresult initResult = cuInit(0);
        if (initResult != CUDA_SUCCESS)
        {
            Core::Log::Warn("CudaDevice::Create(): cuInit failed (CUresult={}). "
                            "CUDA compute backend unavailable.",
                            static_cast<int>(initResult));
            return std::unexpected(CudaError::DriverNotFound);
        }

        // Step 2: Check device count.
        int deviceCount = 0;
        if (auto r = Check(cuDeviceGetCount(&deviceCount)); !r)
            return std::unexpected(r.error());

        if (deviceCount == 0)
        {
            Core::Log::Warn("CudaDevice::Create(): No CUDA-capable devices found.");
            return std::unexpected(CudaError::NoDevice);
        }

        if (deviceOrdinal < 0 || deviceOrdinal >= deviceCount)
        {
            Core::Log::Error("CudaDevice::Create(): Device ordinal {} out of range "
                             "(available: {}).", deviceOrdinal, deviceCount);
            return std::unexpected(CudaError::InvalidDevice);
        }

        // Step 3: Get device handle and query properties.
        auto device = std::unique_ptr<CudaDevice>(new CudaDevice());

        if (auto r = Check(cuDeviceGet(&device->m_Device, deviceOrdinal)); !r)
            return std::unexpected(r.error());

        cuDeviceGetName(device->m_DeviceName, sizeof(device->m_DeviceName), device->m_Device);

        cuDeviceGetAttribute(&device->m_ComputeCapabilityMajor,
                             CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
                             device->m_Device);
        cuDeviceGetAttribute(&device->m_ComputeCapabilityMinor,
                             CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
                             device->m_Device);

        size_t totalMem = 0;
        cuDeviceTotalMem(&totalMem, device->m_Device);
        device->m_TotalMemory = totalMem;

        // Step 4: Create CUDA context.
        CUresult ctxResult = cuCtxCreate(&device->m_Context, 0, device->m_Device);
        if (ctxResult != CUDA_SUCCESS)
        {
            Core::Log::Error("CudaDevice::Create(): cuCtxCreate failed (CUresult={}).",
                             static_cast<int>(ctxResult));
            return std::unexpected(CudaError::ContextCreateFailed);
        }

        // Step 5: Create default stream.
        CUresult streamResult = cuStreamCreate(&device->m_DefaultStream, CU_STREAM_NON_BLOCKING);
        if (streamResult != CUDA_SUCCESS)
        {
            cuCtxDestroy(device->m_Context);
            Core::Log::Error("CudaDevice::Create(): cuStreamCreate failed (CUresult={}).",
                             static_cast<int>(streamResult));
            return std::unexpected(CudaError::StreamCreateFailed);
        }

        Core::Log::Info("CudaDevice: Initialized '{}' (SM {}.{}, {} MB VRAM)",
                        device->m_DeviceName,
                        device->m_ComputeCapabilityMajor,
                        device->m_ComputeCapabilityMinor,
                        device->m_TotalMemory / (1024 * 1024));

        return device;
    }

    CudaDevice::~CudaDevice()
    {
        std::expected<ScopedCudaContext, CudaError> contextGuard =
            ScopedCudaContext::Push(m_Context, "CudaDevice::~CudaDevice()");

        if (m_DefaultStream)
        {
            if (contextGuard)
            {
                if (const CUresult syncResult = cuStreamSynchronize(m_DefaultStream);
                    syncResult != CUDA_SUCCESS)
                {
                    Core::Log::Warn("CudaDevice::~CudaDevice(): cuStreamSynchronize failed "
                                    "(CUresult={}).",
                                    static_cast<int>(syncResult));
                }

                if (const CUresult destroyStreamResult = cuStreamDestroy(m_DefaultStream);
                    destroyStreamResult != CUDA_SUCCESS)
                {
                    Core::Log::Warn("CudaDevice::~CudaDevice(): cuStreamDestroy failed "
                                    "(CUresult={}).",
                                    static_cast<int>(destroyStreamResult));
                }

                if (auto popResult = contextGuard->Finish(); !popResult)
                {
                    Core::Log::Warn("CudaDevice::~CudaDevice(): failed to restore previous CUDA "
                                    "context ({}).",
                                    CudaErrorToString(popResult.error()));
                }
            }
            else
            {
                Core::Log::Warn("CudaDevice::~CudaDevice(): skipping explicit default-stream "
                                "cleanup because context activation failed ({}).",
                                CudaErrorToString(contextGuard.error()));
            }

            m_DefaultStream = nullptr;
        }

        if (m_Context)
        {
            if (const CUresult destroyContextResult = cuCtxDestroy(m_Context);
                destroyContextResult != CUDA_SUCCESS)
            {
                Core::Log::Warn("CudaDevice::~CudaDevice(): cuCtxDestroy failed (CUresult={}).",
                                static_cast<int>(destroyContextResult));
            }
            m_Context = nullptr;
        }

        Core::Log::Info("CudaDevice: Shutdown complete.");
    }

    CudaExpected<CudaBufferHandle> CudaDevice::AllocateBuffer(size_t bytes)
    {
        if (bytes == 0)
            return std::unexpected(CudaError::InvalidPointer);

        auto contextGuard = ScopedCudaContext::Push(m_Context, "CudaDevice::AllocateBuffer()");
        if (!contextGuard)
            return std::unexpected(contextGuard.error());

        CUdeviceptr ptr = 0;
        CUresult result = cuMemAlloc(&ptr, bytes);
        if (result != CUDA_SUCCESS)
        {
            Core::Log::Error("CudaDevice::AllocateBuffer(): cuMemAlloc({} bytes) failed "
                             "(CUresult={}).", bytes, static_cast<int>(result));
            return std::unexpected(MapDriverError(result));
        }

        if (auto popResult = contextGuard->Finish(); !popResult)
            return std::unexpected(popResult.error());

        return CudaBufferHandle{ptr, bytes};
    }

    void CudaDevice::FreeBuffer(CudaBufferHandle& handle)
    {
        if (!handle)
            return;

        auto contextGuard = ScopedCudaContext::Push(m_Context, "CudaDevice::FreeBuffer()");
        if (!contextGuard)
            return;

        const CUresult result = cuMemFree(handle.Ptr);
        if (result != CUDA_SUCCESS)
        {
            Core::Log::Error("CudaDevice::FreeBuffer(): cuMemFree failed (CUresult={}).",
                             static_cast<int>(result));
            return;
        }

        handle.Ptr = 0;
        handle.Size = 0;

        (void)contextGuard->Finish();
    }

    CudaExpected<CUstream> CudaDevice::CreateStream()
    {
        auto contextGuard = ScopedCudaContext::Push(m_Context, "CudaDevice::CreateStream()");
        if (!contextGuard)
            return std::unexpected(contextGuard.error());

        CUstream stream = nullptr;
        CUresult result = cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING);
        if (result != CUDA_SUCCESS)
        {
            Core::Log::Error("CudaDevice::CreateStream(): cuStreamCreate failed "
                             "(CUresult={}).", static_cast<int>(result));
            return std::unexpected(CudaError::StreamCreateFailed);
        }

        if (auto popResult = contextGuard->Finish(); !popResult)
            return std::unexpected(popResult.error());
        return stream;
    }

    void CudaDevice::DestroyStream(CUstream stream)
    {
        if (stream)
        {
            auto contextGuard = ScopedCudaContext::Push(m_Context, "CudaDevice::DestroyStream()");
            if (!contextGuard)
                return;

            if (const CUresult syncResult = cuStreamSynchronize(stream);
                syncResult != CUDA_SUCCESS)
            {
                Core::Log::Warn("CudaDevice::DestroyStream(): cuStreamSynchronize failed "
                                "(CUresult={}).",
                                static_cast<int>(syncResult));
            }

            if (const CUresult destroyResult = cuStreamDestroy(stream);
                destroyResult != CUDA_SUCCESS)
            {
                Core::Log::Warn("CudaDevice::DestroyStream(): cuStreamDestroy failed "
                                "(CUresult={}).",
                                static_cast<int>(destroyResult));
            }

            (void)contextGuard->Finish();
        }
    }

    CudaError CudaDevice::SynchronizeDefaultStream()
    {
        auto contextGuard = ScopedCudaContext::Push(m_Context, "CudaDevice::SynchronizeDefaultStream()");
        if (!contextGuard)
            return contextGuard.error();

        CUresult result = cuStreamSynchronize(m_DefaultStream);
        if (result != CUDA_SUCCESS)
            return MapDriverError(result);

        if (auto popResult = contextGuard->Finish(); !popResult)
            return popResult.error();
        return CudaError::Success;
    }

    CudaExpected<size_t> CudaDevice::GetFreeMemory() const
    {
        auto contextGuard = ScopedCudaContext::Push(m_Context, "CudaDevice::GetFreeMemory()");
        if (!contextGuard)
            return std::unexpected(contextGuard.error());

        size_t freeMem = 0;
        size_t totalMem = 0;
        CUresult result = cuMemGetInfo(&freeMem, &totalMem);
        if (result != CUDA_SUCCESS)
            return std::unexpected(MapDriverError(result));

        if (auto popResult = contextGuard->Finish(); !popResult)
            return std::unexpected(popResult.error());
        return freeMem;
    }
}
