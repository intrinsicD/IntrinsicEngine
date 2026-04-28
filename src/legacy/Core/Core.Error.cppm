module;

#include <cstdint>
#include <string_view>
#include <expected>
#include <utility>

export module Core.Error;

export namespace Core
{
    // -------------------------------------------------------------------------
    // Error Handling Strategy
    // -------------------------------------------------------------------------
    // This module defines the standardized error handling pattern for the engine:
    //
    // 1. std::expected<T, E>  - For FALLIBLE operations where failure is expected
    //                          and the caller MUST handle it. Use when:
    //                          - File I/O, parsing, validation
    //                          - Resource allocation that can fail
    //                          - API calls that return error codes
    //
    // 2. std::optional<T>    - For QUERIES where "not found" is a valid outcome,
    //                          not an error. Use when:
    //                          - Lookup by key/name that might not exist
    //                          - Geometric intersection that might not hit
    //                          - Cache lookups
    //
    // 3. Raw pointers (T*)   - ONLY for non-owning observation of existing objects
    //                          where nullptr means "no reference". Use when:
    //                          - Accessing components that may not be attached
    //                          - Getting optional references to parents/children
    //                          NEVER return raw pointers for newly allocated resources!
    //
    // 4. Assertions          - For INVARIANTS that should never be violated.
    //                          If violated, indicates a bug, not a runtime error.
    // -------------------------------------------------------------------------

    // Generic error code for operations that can fail for multiple reasons
    enum class ErrorCode : uint32_t
    {
        Success = 0,

        // Resource errors (100-199)
        OutOfMemory = 100,
        ResourceNotFound = 101,
        ResourceBusy = 102,
        ResourceCorrupted = 103,

        // I/O errors (200-299)
        FileNotFound = 200,
        FileReadError = 201,
        FileWriteError = 202,
        InvalidPath = 203,
        PermissionDenied = 204,

        // Validation errors (300-399)
        InvalidArgument = 300,
        InvalidState = 301,
        InvalidFormat = 302,
        OutOfRange = 303,
        TypeMismatch = 304,

        // Graphics/RHI errors (400-499)
        DeviceLost = 400,
        OutOfDeviceMemory = 401,
        ShaderCompilationFailed = 402,
        PipelineCreationFailed = 403,
        SwapchainOutOfDate = 404,

        // Asset errors (500-599)
        AssetNotLoaded = 500,
        AssetLoadFailed = 501,
        AssetTypeMismatch = 502,

        // Threading errors (600-699)
        ThreadViolation = 600,
        DeadlockDetected = 601,

        // Generic
        Unknown = 999
    };

    // Convert error code to string for logging
    constexpr std::string_view ErrorCodeToString(ErrorCode code)
    {
        switch (code)
        {
            case ErrorCode::Success:                return "Success";
            case ErrorCode::OutOfMemory:            return "OutOfMemory";
            case ErrorCode::ResourceNotFound:       return "ResourceNotFound";
            case ErrorCode::ResourceBusy:           return "ResourceBusy";
            case ErrorCode::ResourceCorrupted:      return "ResourceCorrupted";
            case ErrorCode::FileNotFound:           return "FileNotFound";
            case ErrorCode::FileReadError:          return "FileReadError";
            case ErrorCode::FileWriteError:         return "FileWriteError";
            case ErrorCode::InvalidPath:            return "InvalidPath";
            case ErrorCode::PermissionDenied:       return "PermissionDenied";
            case ErrorCode::InvalidArgument:        return "InvalidArgument";
            case ErrorCode::InvalidState:           return "InvalidState";
            case ErrorCode::InvalidFormat:          return "InvalidFormat";
            case ErrorCode::OutOfRange:             return "OutOfRange";
            case ErrorCode::TypeMismatch:           return "TypeMismatch";
            case ErrorCode::DeviceLost:             return "DeviceLost";
            case ErrorCode::OutOfDeviceMemory:      return "OutOfDeviceMemory";
            case ErrorCode::ShaderCompilationFailed: return "ShaderCompilationFailed";
            case ErrorCode::PipelineCreationFailed: return "PipelineCreationFailed";
            case ErrorCode::SwapchainOutOfDate:     return "SwapchainOutOfDate";
            case ErrorCode::AssetNotLoaded:         return "AssetNotLoaded";
            case ErrorCode::AssetLoadFailed:        return "AssetLoadFailed";
            case ErrorCode::AssetTypeMismatch:      return "AssetTypeMismatch";
            case ErrorCode::ThreadViolation:        return "ThreadViolation";
            case ErrorCode::DeadlockDetected:       return "DeadlockDetected";
            default:                                return "Unknown";
        }
    }

    // Type alias for common expected patterns
    template<typename T>
    using Expected = std::expected<T, ErrorCode>;

    // Helper to create success result
    template<typename T>
    constexpr Expected<T> Ok(T&& value)
    {
        return Expected<T>(std::forward<T>(value));
    }

    // Helper to create error result
    template<typename T>
    constexpr Expected<T> Err(ErrorCode code)
    {
        return std::unexpected(code);
    }

    // Void success type for operations that don't return a value
    struct Unit {};
    constexpr Unit unit{};

    using Result = Expected<Unit>;

    constexpr Result Ok()
    {
        return Result(unit);
    }

    constexpr Result Err(ErrorCode code)
    {
        return std::unexpected(code);
    }
}

