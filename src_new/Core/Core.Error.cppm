module;

#include <cstdint>
#include <string_view>
#include <expected>

export module Extrinsic.Core.Error;

namespace Extrinsic::Core
{
    export enum class ErrorCode : std::uint32_t
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
        UnsupportedFormat = 205,

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
        AssetLoaderMissing = 502,
        AssetTypeMismatch = 503,
        AssetDecodeFailed = 504,
        AssetUploadFailed = 505,
        AssetUnsupportedFormat = 506,
        AssetInvalidData = 507,

        // Threading errors (600-699)
        ThreadViolation = 600,
        DeadlockDetected = 601,

        // Generic
        Unknown = 999
    };
}

namespace Extrinsic::Core::Error
{
    export constexpr auto ToUnderlying(const ErrorCode e) { return static_cast<std::uint32_t>(e); }

    export constexpr std::string_view ToString(const ErrorCode e)
    {
        switch (e)
        {
        case ErrorCode::Success: return "Success";
        case ErrorCode::OutOfMemory: return "OutOfMemory";
        case ErrorCode::ResourceNotFound: return "ResourceNotFound";
        case ErrorCode::ResourceBusy: return "ResourceBusy";
        case ErrorCode::ResourceCorrupted: return "ResourceCorrupted";
        case ErrorCode::FileNotFound: return "FileNotFound";
        case ErrorCode::FileReadError: return "FileReadError";
        case ErrorCode::FileWriteError: return "FileWriteError";
        case ErrorCode::InvalidPath: return "InvalidPath";
        case ErrorCode::PermissionDenied: return "PermissionDenied";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::InvalidState: return "InvalidState";
        case ErrorCode::InvalidFormat: return "InvalidFormat";
        case ErrorCode::OutOfRange: return "OutOfRange";
        case ErrorCode::TypeMismatch: return "TypeMismatch";
        case ErrorCode::DeviceLost: return "DeviceLost";
        case ErrorCode::OutOfDeviceMemory: return "OutOfDeviceMemory";
        case ErrorCode::ShaderCompilationFailed: return "ShaderCompilationFailed";
        case ErrorCode::PipelineCreationFailed: return "PipelineCreationFailed";
        case ErrorCode::SwapchainOutOfDate: return "SwapchainOutOfDate";
        case ErrorCode::AssetNotLoaded: return "AssetNotLoaded";
        case ErrorCode::AssetLoadFailed: return "AssetLoadFailed";
        case ErrorCode::AssetTypeMismatch: return "AssetTypeMismatch";
        case ErrorCode::AssetDecodeFailed: return "AssetDecodeFailed";
        case ErrorCode::AssetUploadFailed: return "AssetUploadFailed";
        case ErrorCode::AssetUnsupportedFormat: return "AssetUnsupportedFormat";
        case ErrorCode::AssetInvalidData: return "AssetInvalidData";
        case ErrorCode::ThreadViolation: return "ThreadViolation";
        case ErrorCode::DeadlockDetected: return "DeadlockDetected";
        default: return "Unknown";
        }
    }
}

export namespace Extrinsic::Core
{
    template <typename T>
    using Expected = std::expected<T, ErrorCode>;

    // Helper to create success result
    template <typename T>
    constexpr Expected<T> Ok(T&& value)
    {
        return Expected<T>(std::forward<T>(value));
    }

    // Helper to create error result
    template <typename T>
    constexpr Expected<T> Err(ErrorCode code)
    {
        return std::unexpected(code);
    }

    // Void success type for operations that don't return a value
    struct Unit
    {
    };

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
