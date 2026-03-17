module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <cuda.h>

export module RHI:CudaDevice;

import :CudaError;

export namespace RHI
{
    // Strongly-typed handle for CUDA device memory allocations.
    // Wraps CUdeviceptr to prevent accidental misuse with raw integers.
    struct CudaBufferHandle
    {
        CUdeviceptr Ptr = 0;
        size_t Size = 0;

        [[nodiscard]] explicit operator bool() const { return Ptr != 0; }
    };

    // CUDA device wrapper — lifecycle-managed CUDA driver context.
    //
    // Owns a CUDA context and default stream. Follows the VulkanDevice pattern:
    // non-copyable, non-movable, created via factory method.
    //
    // Thread safety: CUDA driver state is thread-local. Every public method that
    // issues driver calls scopes m_Context current on the caller thread and
    // restores the previous current context before returning. Concurrent work
    // submission should still use separate streams (via CreateStream).
    class CudaDevice
    {
    public:
        // Factory: attempts to initialize the CUDA driver API and create a
        // context on the best available device. Returns CudaError on failure
        // (no driver, no device, etc.) — never throws.
        [[nodiscard]] static std::expected<std::unique_ptr<CudaDevice>, CudaError>
        Create(int deviceOrdinal = 0);

        ~CudaDevice();

        // Non-copyable, non-movable (matches VulkanDevice).
        CudaDevice(const CudaDevice&) = delete;
        CudaDevice& operator=(const CudaDevice&) = delete;
        CudaDevice(CudaDevice&&) = delete;
        CudaDevice& operator=(CudaDevice&&) = delete;

        // ----- Memory Management -----

        // Allocate device-local memory. Returns a typed handle.
        [[nodiscard]] CudaExpected<CudaBufferHandle> AllocateBuffer(size_t bytes);

        // Free a previously allocated buffer. No-op if handle is null.
        void FreeBuffer(CudaBufferHandle& handle);

        // ----- Stream Management -----

        // The default compute stream (created at device init).
        [[nodiscard]] CUstream GetDefaultStream() const { return m_DefaultStream; }

        // Create an additional stream for concurrent kernel launches.
        [[nodiscard]] CudaExpected<CUstream> CreateStream();

        // Destroy a stream created via CreateStream.
        void DestroyStream(CUstream stream);

        // ----- Event Management -----

        [[nodiscard]] CudaExpected<CUevent> CreateEvent();
        void DestroyEvent(CUevent event);
        [[nodiscard]] CudaError RecordEvent(CUevent event, CUstream stream);
        [[nodiscard]] CudaExpected<bool> IsEventComplete(CUevent event) const;
        [[nodiscard]] CudaExpected<float> GetElapsedMilliseconds(CUevent start, CUevent end) const;

        // ----- Memory Transfer -----

        [[nodiscard]] CudaExpected<void> CopyHostToBufferAsync(
            CudaBufferHandle destination,
            const void* source,
            size_t bytes,
            CUstream stream,
            size_t destinationOffset = 0);

        [[nodiscard]] CudaExpected<void> CopyBufferToHost(
            void* destination,
            CudaBufferHandle source,
            size_t bytes,
            size_t sourceOffset = 0) const;

        // Synchronize the default stream (blocks until all queued work completes).
        [[nodiscard]] CudaError SynchronizeDefaultStream();

        // ----- Device Queries -----

        [[nodiscard]] std::string_view GetDeviceName() const { return m_DeviceName; }
        [[nodiscard]] int GetComputeCapabilityMajor() const { return m_ComputeCapabilityMajor; }
        [[nodiscard]] int GetComputeCapabilityMinor() const { return m_ComputeCapabilityMinor; }
        [[nodiscard]] size_t GetTotalMemory() const { return m_TotalMemory; }

        // Query currently free device memory (runtime call, not cached).
        [[nodiscard]] CudaExpected<size_t> GetFreeMemory() const;

        // ----- Low-level Access (for interop) -----

        [[nodiscard]] CUdevice GetDevice() const { return m_Device; }
        [[nodiscard]] CUcontext GetContext() const { return m_Context; }

    private:
        CudaDevice() = default;

        CUdevice m_Device{};
        CUcontext m_Context{};
        CUstream m_DefaultStream{};

        char m_DeviceName[256]{};
        int m_ComputeCapabilityMajor = 0;
        int m_ComputeCapabilityMinor = 0;
        size_t m_TotalMemory = 0;
    };
}
