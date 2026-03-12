module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
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
        if (m_DefaultStream)
        {
            cuStreamSynchronize(m_DefaultStream);
            cuStreamDestroy(m_DefaultStream);
            m_DefaultStream = nullptr;
        }

        if (m_Context)
        {
            cuCtxDestroy(m_Context);
            m_Context = nullptr;
        }

        Core::Log::Info("CudaDevice: Shutdown complete.");
    }

    CudaExpected<CudaBufferHandle> CudaDevice::AllocateBuffer(size_t bytes)
    {
        if (bytes == 0)
            return std::unexpected(CudaError::InvalidPointer);

        CUdeviceptr ptr = 0;
        CUresult result = cuMemAlloc(&ptr, bytes);
        if (result != CUDA_SUCCESS)
        {
            Core::Log::Error("CudaDevice::AllocateBuffer(): cuMemAlloc({} bytes) failed "
                             "(CUresult={}).", bytes, static_cast<int>(result));
            return std::unexpected(MapDriverError(result));
        }

        return CudaBufferHandle{ptr, bytes};
    }

    void CudaDevice::FreeBuffer(CudaBufferHandle& handle)
    {
        if (!handle)
            return;

        cuMemFree(handle.Ptr);
        handle.Ptr = 0;
        handle.Size = 0;
    }

    CudaExpected<CUstream> CudaDevice::CreateStream()
    {
        CUstream stream = nullptr;
        CUresult result = cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING);
        if (result != CUDA_SUCCESS)
        {
            Core::Log::Error("CudaDevice::CreateStream(): cuStreamCreate failed "
                             "(CUresult={}).", static_cast<int>(result));
            return std::unexpected(CudaError::StreamCreateFailed);
        }
        return stream;
    }

    void CudaDevice::DestroyStream(CUstream stream)
    {
        if (stream)
        {
            cuStreamSynchronize(stream);
            cuStreamDestroy(stream);
        }
    }

    CudaError CudaDevice::SynchronizeDefaultStream()
    {
        CUresult result = cuStreamSynchronize(m_DefaultStream);
        return (result == CUDA_SUCCESS) ? CudaError::Success : MapDriverError(result);
    }

    CudaExpected<size_t> CudaDevice::GetFreeMemory() const
    {
        size_t freeMem = 0;
        size_t totalMem = 0;
        CUresult result = cuMemGetInfo(&freeMem, &totalMem);
        if (result != CUDA_SUCCESS)
            return std::unexpected(MapDriverError(result));
        return freeMem;
    }
}
