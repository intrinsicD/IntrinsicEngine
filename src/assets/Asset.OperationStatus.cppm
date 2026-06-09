module;

#include <cstdint>

export module Extrinsic.Asset.OperationStatus;

import Extrinsic.Core.Error;

export namespace Extrinsic::Assets
{
    enum class AssetOperation : std::uint8_t
    {
        Load,
        Reload,
        Destroy,
        Import,
        Export,
    };

    enum class AssetOperationStatus : std::uint8_t
    {
        Succeeded,
        InvalidArgument,
        ResourceNotFound,
        InvalidState,
        TypeMismatch,
        LoaderMissing,
        CallbackFailed,
        ValidationFailed,
        UnsupportedFormat,
        IoFailed,
        UploadFailed,
        ResourceBusy,
        UnknownFailure,
    };

    struct AssetOperationDiagnostic
    {
        AssetOperation Operation{AssetOperation::Load};
        AssetOperationStatus Status{AssetOperationStatus::Succeeded};
        Core::ErrorCode Error{Core::ErrorCode::Success};
    };

    [[nodiscard]] constexpr AssetOperationStatus ClassifyAssetOperationStatus(
        const Core::ErrorCode error) noexcept
    {
        switch (error)
        {
        case Core::ErrorCode::Success:
            return AssetOperationStatus::Succeeded;
        case Core::ErrorCode::InvalidArgument:
        case Core::ErrorCode::InvalidPath:
        case Core::ErrorCode::OutOfRange:
            return AssetOperationStatus::InvalidArgument;
        case Core::ErrorCode::ResourceNotFound:
        case Core::ErrorCode::FileNotFound:
        case Core::ErrorCode::AssetNotLoaded:
            return AssetOperationStatus::ResourceNotFound;
        case Core::ErrorCode::InvalidState:
            return AssetOperationStatus::InvalidState;
        case Core::ErrorCode::TypeMismatch:
        case Core::ErrorCode::AssetTypeMismatch:
            return AssetOperationStatus::TypeMismatch;
        case Core::ErrorCode::AssetLoaderMissing:
            return AssetOperationStatus::LoaderMissing;
        case Core::ErrorCode::AssetLoadFailed:
        case Core::ErrorCode::AssetDecodeFailed:
            return AssetOperationStatus::CallbackFailed;
        case Core::ErrorCode::InvalidFormat:
        case Core::ErrorCode::ResourceCorrupted:
        case Core::ErrorCode::AssetInvalidData:
            return AssetOperationStatus::ValidationFailed;
        case Core::ErrorCode::UnsupportedFormat:
        case Core::ErrorCode::AssetUnsupportedFormat:
            return AssetOperationStatus::UnsupportedFormat;
        case Core::ErrorCode::FileReadError:
        case Core::ErrorCode::FileWriteError:
        case Core::ErrorCode::PermissionDenied:
            return AssetOperationStatus::IoFailed;
        case Core::ErrorCode::AssetUploadFailed:
        case Core::ErrorCode::DeviceNotOperational:
        case Core::ErrorCode::DeviceLost:
        case Core::ErrorCode::OutOfDeviceMemory:
            return AssetOperationStatus::UploadFailed;
        case Core::ErrorCode::ResourceBusy:
        case Core::ErrorCode::OutOfMemory:
            return AssetOperationStatus::ResourceBusy;
        case Core::ErrorCode::ShaderCompilationFailed:
        case Core::ErrorCode::PipelineCreationFailed:
        case Core::ErrorCode::SwapchainOutOfDate:
        case Core::ErrorCode::ThreadViolation:
        case Core::ErrorCode::DeadlockDetected:
        case Core::ErrorCode::Unknown:
            return AssetOperationStatus::UnknownFailure;
        }
        return AssetOperationStatus::UnknownFailure;
    }

    [[nodiscard]] constexpr AssetOperationDiagnostic DiagnoseAssetOperation(
        const AssetOperation operation,
        const Core::ErrorCode error) noexcept
    {
        return AssetOperationDiagnostic{
            .Operation = operation,
            .Status = ClassifyAssetOperationStatus(error),
            .Error = error,
        };
    }

    [[nodiscard]] constexpr const char* DebugNameForAssetOperation(
        const AssetOperation operation) noexcept
    {
        switch (operation)
        {
        case AssetOperation::Load:
            return "Load";
        case AssetOperation::Reload:
            return "Reload";
        case AssetOperation::Destroy:
            return "Destroy";
        case AssetOperation::Import:
            return "Import";
        case AssetOperation::Export:
            return "Export";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr const char* DebugNameForAssetOperationStatus(
        const AssetOperationStatus status) noexcept
    {
        switch (status)
        {
        case AssetOperationStatus::Succeeded:
            return "Succeeded";
        case AssetOperationStatus::InvalidArgument:
            return "InvalidArgument";
        case AssetOperationStatus::ResourceNotFound:
            return "ResourceNotFound";
        case AssetOperationStatus::InvalidState:
            return "InvalidState";
        case AssetOperationStatus::TypeMismatch:
            return "TypeMismatch";
        case AssetOperationStatus::LoaderMissing:
            return "LoaderMissing";
        case AssetOperationStatus::CallbackFailed:
            return "CallbackFailed";
        case AssetOperationStatus::ValidationFailed:
            return "ValidationFailed";
        case AssetOperationStatus::UnsupportedFormat:
            return "UnsupportedFormat";
        case AssetOperationStatus::IoFailed:
            return "IoFailed";
        case AssetOperationStatus::UploadFailed:
            return "UploadFailed";
        case AssetOperationStatus::ResourceBusy:
            return "ResourceBusy";
        case AssetOperationStatus::UnknownFailure:
            return "UnknownFailure";
        }
        return "Unknown";
    }
}
