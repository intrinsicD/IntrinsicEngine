module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.GpuReadbackJob;

import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.DerivedJobGraph;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        template <class T>
        [[nodiscard]] Core::Result ValidateTypedBinding(
            const GpuReadbackPropertyBinding& binding) noexcept
        {
            if (binding.TargetProperties == nullptr ||
                binding.TargetProperty.empty() ||
                binding.SourceRange.SizeBytes == 0u)
            {
                return Core::Err(Core::ErrorCode::InvalidArgument);
            }

            auto property = binding.TargetProperties->Get<T>(binding.TargetProperty);
            if (!property.has_value())
            {
                return binding.TargetProperties->Contains(binding.TargetProperty)
                    ? Core::Err(Core::ErrorCode::TypeMismatch)
                    : Core::Err(Core::ErrorCode::ResourceNotFound);
            }

            auto dimensions = RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
                .ElementCount = static_cast<std::uint64_t>(property->Span().size()),
                .ComponentBytes = sizeof(T),
                .Region = binding.SourceRange,
                .StrideBytes = binding.SourceStrideBytes,
                .Mode = RHI::BufferDimensionMatchMode::Exact,
            });
            if (!dimensions.has_value())
            {
                return Core::Err(dimensions.error());
            }

            return Core::Ok();
        }

        template <class T>
        [[nodiscard]] Core::Result WriteTypedProperty(
            const GpuReadbackPropertyBinding& binding,
            const std::span<const std::byte> bytes) noexcept
        {
            auto validation = ValidateTypedBinding<T>(binding);
            if (!validation.has_value())
            {
                return validation;
            }
            if (bytes.size_bytes() != static_cast<std::size_t>(binding.SourceRange.SizeBytes))
            {
                return Core::Err(Core::ErrorCode::TypeMismatch);
            }

            auto property = binding.TargetProperties->Get<T>(binding.TargetProperty);
            if (!property.has_value())
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }

            std::span<T> destination = property->Span();
            const std::uint64_t stride = binding.SourceStrideBytes == 0u
                ? sizeof(T)
                : binding.SourceStrideBytes;
            if (stride < sizeof(T))
            {
                return Core::Err(Core::ErrorCode::InvalidArgument);
            }

            for (std::size_t index = 0; index < destination.size(); ++index)
            {
                const std::uint64_t sourceOffset = static_cast<std::uint64_t>(index) * stride;
                if (sourceOffset + sizeof(T) > bytes.size_bytes())
                {
                    return Core::Err(Core::ErrorCode::OutOfRange);
                }
                std::memcpy(&destination[index],
                            bytes.data() + static_cast<std::ptrdiff_t>(sourceOffset),
                            sizeof(T));
            }

            return Core::Ok();
        }

        [[nodiscard]] Core::Result ValidateBindingByType(
            const GpuReadbackPropertyBinding& binding) noexcept
        {
            switch (binding.TargetType)
            {
            case GpuReadbackPropertyType::Float32:
                return ValidateTypedBinding<float>(binding);
            case GpuReadbackPropertyType::Float64:
                return ValidateTypedBinding<double>(binding);
            case GpuReadbackPropertyType::Int32:
                return ValidateTypedBinding<std::int32_t>(binding);
            case GpuReadbackPropertyType::UInt32:
                return ValidateTypedBinding<std::uint32_t>(binding);
            case GpuReadbackPropertyType::Vec2Float32:
                return ValidateTypedBinding<glm::vec2>(binding);
            case GpuReadbackPropertyType::Vec3Float32:
                return ValidateTypedBinding<glm::vec3>(binding);
            case GpuReadbackPropertyType::Vec4Float32:
                return ValidateTypedBinding<glm::vec4>(binding);
            }
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        [[nodiscard]] Core::Result WriteBindingByType(
            const GpuReadbackPropertyBinding& binding,
            const std::span<const std::byte> bytes) noexcept
        {
            switch (binding.TargetType)
            {
            case GpuReadbackPropertyType::Float32:
                return WriteTypedProperty<float>(binding, bytes);
            case GpuReadbackPropertyType::Float64:
                return WriteTypedProperty<double>(binding, bytes);
            case GpuReadbackPropertyType::Int32:
                return WriteTypedProperty<std::int32_t>(binding, bytes);
            case GpuReadbackPropertyType::UInt32:
                return WriteTypedProperty<std::uint32_t>(binding, bytes);
            case GpuReadbackPropertyType::Vec2Float32:
                return WriteTypedProperty<glm::vec2>(binding, bytes);
            case GpuReadbackPropertyType::Vec3Float32:
                return WriteTypedProperty<glm::vec3>(binding, bytes);
            case GpuReadbackPropertyType::Vec4Float32:
                return WriteTypedProperty<glm::vec4>(binding, bytes);
            }
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
    }

    Core::Result ValidateGpuReadbackPropertyBinding(
        const GpuReadbackPropertyBinding& binding) noexcept
    {
        return ValidateBindingByType(binding);
    }

    Core::Result WriteGpuReadbackProperty(
        const GpuReadbackPropertyBinding& binding,
        const std::span<const std::byte> bytes) noexcept
    {
        return WriteBindingByType(binding, bytes);
    }

    DerivedJobHandle SubmitGpuReadbackJob(
        DerivedJobRegistry& registry,
        GpuReadbackJobDesc desc)
    {
        auto bytes = std::make_shared<std::vector<std::byte>>(
            static_cast<std::size_t>(desc.Binding.SourceRange.SizeBytes));
        auto ticket = std::make_shared<Graphics::GpuTransferReadbackTicket>();

        Graphics::GpuTransfer* transfer = desc.Transfer;
        RHI::ICommandContext* commandContext = desc.CommandContext;
        const RHI::BufferHandle source = desc.Source;
        const RHI::BufferDesc sourceDesc = desc.SourceDesc;
        const RHI::MemoryAccess sourceAccess = desc.SourceAccess;
        const GpuReadbackPropertyBinding binding = std::move(desc.Binding);

        DerivedJobDesc derived{};
        derived.Key = std::move(desc.Key);
        derived.Name = std::move(desc.Name);
        derived.Priority = desc.Priority;
        derived.EstimatedCost = std::max<std::uint32_t>(1u, desc.EstimatedCost);
        derived.CancellationGeneration = desc.CancellationGeneration;
        derived.Scope = desc.Scope;
        derived.HasPreviousOutput = desc.HasPreviousOutput;
        derived.IsReadbackJob = true;
        derived.ReadbackByteSize = binding.SourceRange.SizeBytes;
        derived.DependsOn = std::move(desc.DependsOn);
        derived.ValidateOnMainThread = std::move(desc.ValidateOnMainThread);

        derived.Execute =
            [transfer, commandContext, source, sourceDesc, sourceAccess, binding, bytes, ticket]()
            mutable -> DerivedJobWorkerResult
        {
            if (transfer == nullptr ||
                commandContext == nullptr ||
                !source.IsValid() ||
                bytes == nullptr ||
                ticket == nullptr)
            {
                return std::unexpected(Core::ErrorCode::InvalidArgument);
            }

            bytes->assign(static_cast<std::size_t>(binding.SourceRange.SizeBytes), std::byte{0});
            *ticket = transfer->ScheduleReadback(
                *commandContext,
                Graphics::GpuTransferReadbackDesc{
                    .Source = source,
                    .SourceDesc = sourceDesc,
                    .SourceRange = binding.SourceRange,
                    .SourceAccess = sourceAccess,
                    .Sink = RHI::ReadbackSink::CopyTo(
                        std::span<std::byte>{bytes->data(), bytes->size()}),
                });

            if (!ticket->IsValid())
            {
                return std::unexpected(Core::ErrorCode::InvalidState);
            }

            return DerivedJobOutput{
                .PayloadToken = ticket->Id,
                .NormalizedProgress = 0.5f,
                .ProgressDeterminate = true,
                .Diagnostic = "readback issued",
            };
        };

        derived.IsReadbackReady = [transfer, ticket]() noexcept -> bool
        {
            return transfer != nullptr &&
                   ticket != nullptr &&
                   ticket->IsValid() &&
                   transfer->IsDelivered(*ticket);
        };

        auto afterWrite = std::move(desc.ApplyAfterWrite);
        derived.ApplyOnMainThread =
            [binding, bytes, afterWrite = std::move(afterWrite)](DerivedJobApplyContext& context)
            mutable -> Core::Result
        {
            if (bytes == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }

            auto write = WriteGpuReadbackProperty(
                binding,
                std::span<const std::byte>{bytes->data(), bytes->size()});
            if (!write.has_value())
            {
                return write;
            }

            if (afterWrite)
            {
                return afterWrite(context);
            }
            return Core::Ok();
        };

        return registry.Submit(std::move(derived));
    }
}
