module;

#include <cstdint>
#include <expected>
#include <string_view>

export module RHI:CudaError;

export namespace RHI
{
    // Domain-specific error codes for the CUDA compute backend.
    // Maps 1:1 to the most common CUresult/cudaError values we care about,
    // plus engine-specific codes for driver detection and interop failures.
    enum class CudaError : uint32_t
    {
        Success = 0,

        // Driver/initialization errors
        DriverNotFound = 1,     // libcuda.so / nvcuda.dll not loadable
        NoDevice = 2,           // cuDeviceGetCount returned 0
        InvalidDevice = 3,      // Requested device ordinal out of range
        ContextCreateFailed = 4,
        NotInitialized = 5,

        // Memory errors
        OutOfMemory = 6,
        InvalidPointer = 7,
        AlreadyFreed = 8,

        // Stream errors
        StreamCreateFailed = 9,

        // Kernel/module errors
        ModuleLoadFailed = 10,
        KernelNotFound = 11,
        LaunchFailed = 12,
        InvalidKernelArgs = 13,

        // Interop errors (Phase 2)
        ExternalMemoryImportFailed = 20,
        ExternalSemaphoreImportFailed = 21,
        SyncFailed = 22,

        // Catch-all
        Unknown = 999
    };

    constexpr std::string_view CudaErrorToString(CudaError error)
    {
        switch (error)
        {
            case CudaError::Success:                      return "Success";
            case CudaError::DriverNotFound:               return "DriverNotFound";
            case CudaError::NoDevice:                     return "NoDevice";
            case CudaError::InvalidDevice:                return "InvalidDevice";
            case CudaError::ContextCreateFailed:          return "ContextCreateFailed";
            case CudaError::NotInitialized:               return "NotInitialized";
            case CudaError::OutOfMemory:                  return "OutOfMemory";
            case CudaError::InvalidPointer:               return "InvalidPointer";
            case CudaError::AlreadyFreed:                 return "AlreadyFreed";
            case CudaError::StreamCreateFailed:           return "StreamCreateFailed";
            case CudaError::ModuleLoadFailed:             return "ModuleLoadFailed";
            case CudaError::KernelNotFound:               return "KernelNotFound";
            case CudaError::LaunchFailed:                 return "LaunchFailed";
            case CudaError::InvalidKernelArgs:            return "InvalidKernelArgs";
            case CudaError::ExternalMemoryImportFailed:   return "ExternalMemoryImportFailed";
            case CudaError::ExternalSemaphoreImportFailed: return "ExternalSemaphoreImportFailed";
            case CudaError::SyncFailed:                   return "SyncFailed";
            default:                                      return "Unknown";
        }
    }

    // Convenience alias for CUDA-fallible operations.
    template <typename T>
    using CudaExpected = std::expected<T, CudaError>;
}
