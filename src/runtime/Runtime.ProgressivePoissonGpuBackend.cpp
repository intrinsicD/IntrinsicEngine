module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.ProgressivePoissonGpuBackend;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] constexpr std::uint32_t CeilDiv(
            const std::uint32_t value,
            const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        [[nodiscard]] constexpr std::uint64_t FloatBytes(
            const std::uint32_t count) noexcept
        {
            return static_cast<std::uint64_t>(count) * sizeof(float);
        }

        [[nodiscard]] constexpr std::uint64_t Uint32Bytes(
            const std::uint32_t count) noexcept
        {
            return static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
        }

        [[nodiscard]] constexpr std::uint64_t Uint64Bytes(
            const std::uint32_t count) noexcept
        {
            return static_cast<std::uint64_t>(count) * sizeof(std::uint64_t);
        }

        [[nodiscard]] bool AddWouldOverflow(
            const std::uint64_t lhs,
            const std::uint64_t rhs) noexcept
        {
            return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
        }

        [[nodiscard]] bool AppendSpan(
            ProgressivePoissonGpuBufferLayout& layout,
            const ProgressivePoissonGpuBufferRole role,
            const std::uint64_t sizeBytes) noexcept
        {
            if (AddWouldOverflow(layout.WorkBufferBytes, sizeBytes))
            {
                return false;
            }

            layout.Spans.push_back(ProgressivePoissonGpuBufferSpan{
                .Role = role,
                .OffsetBytes = layout.WorkBufferBytes,
                .SizeBytes = sizeBytes,
            });
            layout.WorkBufferBytes += sizeBytes;
            return true;
        }

        [[nodiscard]] std::uint32_t PhaseCountForDimension(
            const std::uint32_t dimension) noexcept
        {
            return dimension == 3u ? 8u : 4u;
        }

        [[nodiscard]] std::uint32_t MixU32(std::uint32_t x) noexcept
        {
            x ^= x >> 16u;
            x *= 0x7feb352dU;
            x ^= x >> 15u;
            x *= 0x846ca68bU;
            x ^= x >> 16u;
            return x;
        }

        [[nodiscard]] float U32ToUnif01(const std::uint32_t x) noexcept
        {
            const std::uint32_t hi = x >> 8u;
            return static_cast<float>(hi) * (1.0f / 16777216.0f);
        }

        [[nodiscard]] float EffectiveRadiusAlpha(
            const ProgressivePoissonGpuConfig& config) noexcept
        {
            if (config.RadiusAlpha > 0.0f && config.RadiusAlpha < 1.0f)
            {
                return config.RadiusAlpha;
            }
            return config.Dimension == 3u ? 0.86602540378f : 0.70710678118f;
        }

        [[nodiscard]] std::uint32_t HashCapacityFor(
            const std::uint32_t inputCount,
            const float hashLoadFactor,
            bool& overflow) noexcept
        {
            overflow = false;
            if (inputCount == 0u)
            {
                return 0u;
            }

            const double capacity =
                std::ceil(static_cast<double>(inputCount) /
                          static_cast<double>(hashLoadFactor));
            if (!(capacity > 0.0) ||
                capacity >
                    static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
            {
                overflow = true;
                return 0u;
            }
            return std::max(1u, static_cast<std::uint32_t>(capacity));
        }

        [[nodiscard]] bool ConfigIsValid(
            const ProgressivePoissonGpuPlanDesc& desc) noexcept
        {
            return (desc.Config.Dimension == 2u || desc.Config.Dimension == 3u) &&
                   desc.Config.GridWidth > 0u &&
                   desc.Config.MaxLevels > 0u &&
                   desc.GroupSize > 0u &&
                   std::isfinite(desc.Config.HashLoadFactor) &&
                   desc.Config.HashLoadFactor > 0.0f;
        }

        [[nodiscard]] ProgressivePoissonGpuBufferLayout BuildLayout(
            const std::uint32_t inputCount,
            const std::uint32_t maxLevels,
            const std::uint32_t hashCapacity,
            const std::uint64_t compactionScratchBytes,
            bool& overflow)
        {
            overflow = false;
            ProgressivePoissonGpuBufferLayout layout{};
            layout.InputCount = inputCount;
            layout.MaxLevels = maxLevels;
            layout.HashTableCapacity = hashCapacity;
            layout.StateBytes = sizeof(ProgressivePoissonGpuStateBufferRecord);
            layout.CompactionScratchBytes = compactionScratchBytes;

            const auto add =
                [&layout, &overflow](const ProgressivePoissonGpuBufferRole role,
                                     const std::uint64_t sizeBytes) noexcept
                {
                    if (!AppendSpan(layout, role, sizeBytes))
                    {
                        overflow = true;
                    }
                };

            add(ProgressivePoissonGpuBufferRole::PositionX, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::PositionY, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::PositionZ, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::RemainingKeys, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::NextRemainingKeys,
                Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::AcceptedKeys, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::CellKeys, Uint64Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::CellPhases, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::AcceptFlags, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::CarryFlags, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::HashKeys, Uint64Bytes(hashCapacity));
            add(ProgressivePoissonGpuBufferRole::HashValues,
                Uint32Bytes(hashCapacity));
            add(ProgressivePoissonGpuBufferRole::LevelOffsets,
                Uint32Bytes(maxLevels + 1u));
            add(ProgressivePoissonGpuBufferRole::SplatRadii, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::OutputCount,
                Uint32Bytes(maxLevels + 1u));
            add(ProgressivePoissonGpuBufferRole::CompactionScratch,
                compactionScratchBytes);
            return layout;
        }

        [[nodiscard]] ProgressivePoissonGpuDispatchDesc MakeMethodDispatch(
            const ProgressivePoissonGpuPassKind kind,
            const std::uint32_t levelIndex,
            const std::uint32_t phaseIndex,
            const std::uint32_t elementCount,
            const std::uint32_t hashCapacity,
            const std::uint32_t groupSize) noexcept
        {
            return ProgressivePoissonGpuDispatchDesc{
                .Kind = kind,
                .LevelIndex = levelIndex,
                .PhaseIndex = phaseIndex,
                .ElementCount = elementCount,
                .HashTableCapacity = hashCapacity,
                .GroupSize = groupSize,
                .GroupCountX = CeilDiv(std::max(elementCount, hashCapacity),
                                        groupSize),
                .GroupCountY = 1u,
                .GroupCountZ = 1u,
            };
        }

        [[nodiscard]] ProgressivePoissonGpuResolveResult ResolveStatus(
            const ProgressivePoissonGpuStatus status,
            std::string diagnostic)
        {
            return ProgressivePoissonGpuResolveResult{
                .Status = status,
                .GpuExecutionAvailable = false,
                .CpuFallbackRecommended = true,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] ProgressivePoissonGpuRecordResult RecordStatus(
            const ProgressivePoissonGpuStatus status,
            const ProgressivePoissonGpuPlanDesc& desc,
            const bool cpuFallbackRecommended = true)
        {
            return ProgressivePoissonGpuRecordResult{
                .Status = status,
                .Recorded = false,
                .CpuFallbackRecommended = cpuFallbackRecommended,
                .Plan = ComputeProgressivePoissonGpuDispatchPlan(desc),
            };
        }

        [[nodiscard]] bool PipelinesAreRecordable(
            const ProgressivePoissonGpuPipelineSet& pipelines) noexcept
        {
            return pipelines.BuildCells.IsValid() &&
                   pipelines.AcceptPhase.IsValid() &&
                   pipelines.Compaction.PrefixScan.IsValid() &&
                   pipelines.Compaction.AddBlockOffsets.IsValid() &&
                   pipelines.Compaction.CompactByFlags.IsValid();
        }

        [[nodiscard]] bool ResourcesAreRecordable(
            const ProgressivePoissonGpuResourceSet& resources,
            const std::uint32_t dimension) noexcept
        {
            return resources.State.IsValid() &&
                   resources.PositionX.IsValid() &&
                   resources.PositionY.IsValid() &&
                   (dimension != 3u || resources.PositionZ.IsValid()) &&
                   resources.RemainingKeys.IsValid() &&
                   resources.NextRemainingKeys.IsValid() &&
                   resources.AcceptedKeys.IsValid() &&
                   resources.CellKeys.IsValid() &&
                   resources.CellPhases.IsValid() &&
                   resources.AcceptFlags.IsValid() &&
                   resources.CarryFlags.IsValid() &&
                   resources.HashKeys.IsValid() &&
                   resources.HashValues.IsValid() &&
                   resources.LevelOffsets.IsValid() &&
                   resources.SplatRadii.IsValid() &&
                   resources.OutputCount.IsValid() &&
                   resources.CompactionScratch.IsValid();
        }

        [[nodiscard]] ProgressivePoissonGpuExecutionResult ExecutionStatus(
            const ProgressivePoissonGpuStatus status,
            const ProgressivePoissonGpuPlanDesc& desc,
            const bool cpuFallbackRecommended = true)
        {
            return ProgressivePoissonGpuExecutionResult{
                .Status = status,
                .Recorded = false,
                .CpuFallbackRecommended = cpuFallbackRecommended,
                .Plan = ComputeProgressivePoissonGpuDispatchPlan(desc),
            };
        }

        [[nodiscard]] ProgressivePoissonGpuReadbackResult ReadbackStatus(
            const ProgressivePoissonGpuStatus status,
            std::string diagnostic)
        {
            return ProgressivePoissonGpuReadbackResult{
                .Status = status,
                .Read = false,
                .StructurallyValid = false,
                .CpuFallbackRecommended = true,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] ProgressivePoissonGpuParityDiagnostics ParityStatus(
            const ProgressivePoissonGpuStatus status,
            std::string diagnostic)
        {
            return ProgressivePoissonGpuParityDiagnostics{
                .Status = status,
                .Compared = false,
                .MatchesReference = false,
                .CpuFallbackRecommended = true,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] const ProgressivePoissonGpuBufferSpan* FindSpan(
            const ProgressivePoissonGpuBufferLayout& layout,
            const ProgressivePoissonGpuBufferRole role) noexcept
        {
            const auto it = std::find_if(
                layout.Spans.begin(),
                layout.Spans.end(),
                [role](const ProgressivePoissonGpuBufferSpan& span) noexcept
                {
                    return span.Role == role;
                });
            return it == layout.Spans.end() ? nullptr : &*it;
        }

        [[nodiscard]] std::uint64_t SizeForRole(
            const ProgressivePoissonGpuBufferLayout& layout,
            const ProgressivePoissonGpuBufferRole role) noexcept
        {
            const ProgressivePoissonGpuBufferSpan* span = FindSpan(layout, role);
            return span == nullptr ? 0u : span->SizeBytes;
        }

        [[nodiscard]] RHI::BufferDesc BuildRoleBufferDesc(
            const ProgressivePoissonGpuBufferRole role,
            const std::uint64_t sizeBytes) noexcept
        {
            return RHI::BufferDesc{
                .SizeBytes = sizeBytes,
                .Usage = RHI::BufferUsage::Storage |
                         RHI::BufferUsage::TransferSrc |
                         RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName = DebugNameForProgressivePoissonGpuBufferRole(role),
            };
        }

        [[nodiscard]] bool CreateOwnedBuffer(
            RHI::BufferManager& buffers,
            const RHI::BufferDesc& desc,
            RHI::BufferHandle& out,
            std::vector<RHI::BufferManager::BufferLease>& leases)
        {
            if (desc.SizeBytes == 0u)
            {
                return true;
            }

            auto leaseOr = buffers.Create(desc);
            if (!leaseOr.has_value())
            {
                return false;
            }

            RHI::BufferManager::BufferLease lease = std::move(leaseOr.value());
            out = lease.GetHandle();
            leases.push_back(std::move(lease));
            return true;
        }

        [[nodiscard]] bool CreateRoleBuffer(
            RHI::BufferManager& buffers,
            const ProgressivePoissonGpuBufferLayout& layout,
            const ProgressivePoissonGpuBufferRole role,
            RHI::BufferHandle& out,
            std::vector<RHI::BufferManager::BufferLease>& leases)
        {
            return CreateOwnedBuffer(buffers,
                                     BuildRoleBufferDesc(role,
                                                         SizeForRole(layout, role)),
                                     out,
                                     leases);
        }

        [[nodiscard]] ProgressivePoissonGpuExecutionResources
        BuildEmptyExecutionResources() noexcept
        {
            return ProgressivePoissonGpuExecutionResources{};
        }

        [[nodiscard]] bool AllocateExecutionResources(
            RHI::BufferManager& buffers,
            const ProgressivePoissonGpuDispatchPlan& plan,
            ProgressivePoissonGpuExecutionResources& out)
        {
            if (!CreateOwnedBuffer(
                    buffers,
                    BuildProgressivePoissonGpuStateBufferDesc(),
                    out.Resources.State,
                    out.Leases))
            {
                return false;
            }

            const ProgressivePoissonGpuBufferLayout& layout = plan.Layout;
            if (!CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::PositionX,
                                  out.Resources.PositionX,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::PositionY,
                                  out.Resources.PositionY,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::PositionZ,
                                  out.Resources.PositionZ,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::RemainingKeys,
                                  out.Resources.RemainingKeys,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::NextRemainingKeys,
                                  out.Resources.NextRemainingKeys,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::AcceptedKeys,
                                  out.Resources.AcceptedKeys,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::CellKeys,
                                  out.Resources.CellKeys,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::CellPhases,
                                  out.Resources.CellPhases,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::AcceptFlags,
                                  out.Resources.AcceptFlags,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::CarryFlags,
                                  out.Resources.CarryFlags,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::HashKeys,
                                  out.Resources.HashKeys,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::HashValues,
                                  out.Resources.HashValues,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::LevelOffsets,
                                  out.Resources.LevelOffsets,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::SplatRadii,
                                  out.Resources.SplatRadii,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::OutputCount,
                                  out.Resources.OutputCount,
                                  out.Leases) ||
                !CreateRoleBuffer(buffers, layout,
                                  ProgressivePoissonGpuBufferRole::CompactionScratch,
                                  out.Resources.CompactionScratch,
                                  out.Leases))
            {
                return false;
            }

            return CreateOwnedBuffer(
                       buffers,
                       BuildProgressivePoissonGpuReadbackBufferDesc(
                           Uint32Bytes(plan.InputCount),
                           "ProgressivePoissonGpu.Order.Readback"),
                       out.OrderReadback,
                       out.Leases) &&
                   CreateOwnedBuffer(
                       buffers,
                       BuildProgressivePoissonGpuReadbackBufferDesc(
                           Uint32Bytes(plan.Layout.MaxLevels + 1u),
                           "ProgressivePoissonGpu.LevelOffsets.Readback"),
                       out.LevelOffsetsReadback,
                       out.Leases) &&
                   CreateOwnedBuffer(
                       buffers,
                       BuildProgressivePoissonGpuReadbackBufferDesc(
                           FloatBytes(plan.InputCount),
                           "ProgressivePoissonGpu.SplatRadii.Readback"),
                       out.SplatRadiiReadback,
                       out.Leases);
        }

        void UploadExecutionInputs(
            RHI::IDevice& device,
            const std::span<const glm::vec3> positions,
            const ProgressivePoissonGpuExecutionResources& resources,
            ProgressivePoissonGpuExecutionResult& result)
        {
            std::vector<float> x(positions.size());
            std::vector<float> y(positions.size());
            std::vector<float> z(positions.size());
            std::vector<std::uint32_t> remaining(positions.size());
            for (std::size_t index = 0u; index < positions.size(); ++index)
            {
                x[index] = positions[index].x;
                y[index] = positions[index].y;
                z[index] = positions[index].z;
                remaining[index] = static_cast<std::uint32_t>(index);
            }

            const auto write = [&device, &result](
                                   const RHI::BufferHandle handle,
                                   const void* data,
                                   const std::uint64_t sizeBytes)
            {
                if (handle.IsValid() && sizeBytes > 0u)
                {
                    device.WriteBuffer(handle, data, sizeBytes, 0u);
                    ++result.UploadWriteCount;
                }
            };

            write(resources.Resources.PositionX, x.data(),
                  FloatBytes(static_cast<std::uint32_t>(x.size())));
            write(resources.Resources.PositionY, y.data(),
                  FloatBytes(static_cast<std::uint32_t>(y.size())));
            write(resources.Resources.PositionZ, z.data(),
                  FloatBytes(static_cast<std::uint32_t>(z.size())));
            write(resources.Resources.RemainingKeys, remaining.data(),
                  Uint32Bytes(static_cast<std::uint32_t>(remaining.size())));

            const std::uint32_t zero32 = 0u;
            write(resources.Resources.OutputCount, &zero32, sizeof(zero32));
            result.UploadedInputs = true;
        }

        void RecordExecutionReadbacks(
            RHI::ICommandContext& cmd,
            const ProgressivePoissonGpuDispatchPlan& plan,
            const ProgressivePoissonGpuExecutionResources& resources,
            ProgressivePoissonGpuExecutionResult& result)
        {
            if (plan.InputCount == 0u)
            {
                return;
            }

            cmd.BufferBarrier(resources.Resources.AcceptedKeys,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::TransferRead);
            cmd.BufferBarrier(resources.Resources.LevelOffsets,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::TransferRead);
            cmd.BufferBarrier(resources.Resources.SplatRadii,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::TransferRead);

            cmd.CopyBuffer(resources.Resources.AcceptedKeys,
                           resources.OrderReadback,
                           0u,
                           0u,
                           Uint32Bytes(plan.InputCount));
            ++result.ReadbackCopyCount;

            cmd.CopyBuffer(resources.Resources.LevelOffsets,
                           resources.LevelOffsetsReadback,
                           0u,
                           0u,
                           Uint32Bytes(plan.Layout.MaxLevels + 1u));
            ++result.ReadbackCopyCount;

            cmd.CopyBuffer(resources.Resources.SplatRadii,
                           resources.SplatRadiiReadback,
                           0u,
                           0u,
                           FloatBytes(plan.InputCount));
            ++result.ReadbackCopyCount;

            result.ReadbackCopiesRecorded = result.ReadbackCopyCount == 3u;
        }

        template <typename T>
        void ReadBufferVector(RHI::IDevice& device,
                              const RHI::BufferHandle handle,
                              std::vector<T>& values)
        {
            if (!values.empty())
            {
                device.ReadBuffer(
                    handle,
                    values.data(),
                    static_cast<std::uint64_t>(values.size()) * sizeof(T),
                    0u);
            }
        }

        [[nodiscard]] bool LevelOffsetsAreMonotonic(
            const std::span<const std::uint32_t> offsets) noexcept
        {
            if (offsets.empty() || offsets.front() != 0u)
            {
                return false;
            }
            for (std::size_t index = 1u; index < offsets.size(); ++index)
            {
                if (offsets[index] < offsets[index - 1u])
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] ProgressivePoissonGpuReadbackResult ValidateReadbackPayload(
            std::vector<std::uint32_t> order,
            std::vector<std::uint32_t> levelOffsets,
            std::vector<float> splatRadii,
            const std::uint32_t inputCount)
        {
            if (!LevelOffsetsAreMonotonic(std::span<const std::uint32_t>{
                    levelOffsets}))
            {
                return ReadbackStatus(
                    ProgressivePoissonGpuStatus::InvalidReadback,
                    "Progressive Poisson GPU readback has non-monotonic level offsets.");
            }

            const std::uint32_t acceptedCount = levelOffsets.back();
            if (acceptedCount > inputCount ||
                acceptedCount > order.size() ||
                acceptedCount > splatRadii.size())
            {
                return ReadbackStatus(
                    ProgressivePoissonGpuStatus::InvalidReadback,
                    "Progressive Poisson GPU readback accepted count exceeds buffer bounds.");
            }

            std::vector<bool> seen(inputCount, false);
            for (std::uint32_t rank = 0u; rank < acceptedCount; ++rank)
            {
                const std::uint32_t pointIndex = order[rank];
                if (pointIndex >= inputCount || seen[pointIndex])
                {
                    return ReadbackStatus(
                        ProgressivePoissonGpuStatus::InvalidReadback,
                        "Progressive Poisson GPU readback order contains an invalid or duplicate point index.");
                }
                seen[pointIndex] = true;

                const float radius = splatRadii[rank];
                if (!std::isfinite(radius) || radius < 0.0f)
                {
                    return ReadbackStatus(
                        ProgressivePoissonGpuStatus::InvalidReadback,
                        "Progressive Poisson GPU readback contains an invalid splat radius.");
                }
            }

            order.resize(acceptedCount);
            splatRadii.resize(acceptedCount);
            return ProgressivePoissonGpuReadbackResult{
                .Status = ProgressivePoissonGpuStatus::Success,
                .Read = true,
                .StructurallyValid = true,
                .CpuFallbackRecommended = false,
                .AcceptedCount = acceptedCount,
                .Order = std::move(order),
                .LevelOffsets = std::move(levelOffsets),
                .SplatRadii = std::move(splatRadii),
            };
        }

        [[nodiscard]] std::uint32_t CountSpanMismatches(
            const std::span<const std::uint32_t> lhs,
            const std::span<const std::uint32_t> rhs) noexcept
        {
            const std::size_t common = std::min(lhs.size(), rhs.size());
            std::uint32_t mismatches = static_cast<std::uint32_t>(
                lhs.size() > rhs.size() ? lhs.size() - rhs.size()
                                        : rhs.size() - lhs.size());
            for (std::size_t index = 0u; index < common; ++index)
            {
                if (lhs[index] != rhs[index])
                {
                    ++mismatches;
                }
            }
            return mismatches;
        }

        [[nodiscard]] float DistanceSq(const glm::vec3& a,
                                       const glm::vec3& b,
                                       const std::uint32_t dimension) noexcept
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = dimension == 3u ? a.z - b.z : 0.0f;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]] float MinPairwiseDistanceForPrefix(
            const std::span<const glm::vec3> positions,
            const std::span<const std::uint32_t> order,
            const std::uint32_t count,
            const std::uint32_t dimension) noexcept
        {
            if (count < 2u)
            {
                return std::numeric_limits<float>::max();
            }

            float bestSq = std::numeric_limits<float>::max();
            for (std::uint32_t i = 0u; i < count; ++i)
            {
                if (order[i] >= positions.size())
                {
                    return 0.0f;
                }
                for (std::uint32_t j = i + 1u; j < count; ++j)
                {
                    if (order[j] >= positions.size())
                    {
                        return 0.0f;
                    }
                    bestSq = std::min(bestSq,
                                      DistanceSq(positions[order[i]],
                                                 positions[order[j]],
                                                 dimension));
                }
            }
            return bestSq == std::numeric_limits<float>::max()
                ? bestSq
                : std::sqrt(bestSq);
        }

        [[nodiscard]] ProgressivePoissonGpuPassPushConstants MakePushConstants(
            RHI::IDevice& device,
            const ProgressivePoissonGpuPlanDesc& desc,
            const ProgressivePoissonGpuDispatchDesc& dispatch,
            const ProgressivePoissonGpuResourceSet& resources) noexcept
        {
            const std::uint32_t scale =
                dispatch.LevelIndex >= 31u ? (1u << 31u)
                                           : (1u << dispatch.LevelIndex);
            const float invCellSize =
                static_cast<float>(desc.Config.GridWidth) *
                static_cast<float>(scale);
            const float cellSize = invCellSize > 0.0f
                ? 1.0f / invCellSize
                : 1.0f;
            const float radius =
                cellSize * EffectiveRadiusAlpha(desc.Config);

            float originX = 0.0f;
            float originY = 0.0f;
            float originZ = 0.0f;
            if (desc.Config.RandomizeGridOrigin)
            {
                const std::uint32_t seed = desc.Config.GridOriginSeed;
                originX = U32ToUnif01(MixU32(
                              seed + 0x9e3779b9u *
                                         (3u * dispatch.LevelIndex + 1u))) *
                          cellSize;
                originY = U32ToUnif01(MixU32(
                              seed + 0x9e3779b9u *
                                         (3u * dispatch.LevelIndex + 2u))) *
                          cellSize;
                if (desc.Config.Dimension == 3u)
                {
                    originZ = U32ToUnif01(MixU32(
                                  seed + 0x9e3779b9u *
                                             (3u * dispatch.LevelIndex + 3u))) *
                              cellSize;
                }
            }

            return ProgressivePoissonGpuPassPushConstants{
                .StateBDA = device.GetBufferDeviceAddress(resources.State),
                .InputCount = desc.InputCount,
                .RemainingCount = desc.InputCount,
                .HashTableCapacity = dispatch.HashTableCapacity,
                .Dimension = desc.Config.Dimension,
                .GridWidth = desc.Config.GridWidth,
                .LevelIndex = dispatch.LevelIndex,
                .PhaseIndex = dispatch.PhaseIndex,
                .PhaseCount = PhaseCountForDimension(desc.Config.Dimension),
                .InvCellSize = invCellSize,
                .RadiusSquared = radius * radius,
                .OriginX = originX,
                .OriginY = originY,
                .OriginZ = originZ,
                .Reserved0 = 0.0f,
            };
        }

        void RecordMethodDispatch(
            RHI::ICommandContext& cmd,
            RHI::IDevice& device,
            const ProgressivePoissonGpuPlanDesc& desc,
            const ProgressivePoissonGpuDispatchDesc& dispatch,
            const ProgressivePoissonGpuResourceSet& resources,
            const RHI::PipelineHandle pipeline)
        {
            const ProgressivePoissonGpuPassPushConstants pc =
                MakePushConstants(device, desc, dispatch, resources);
            cmd.BindPipeline(pipeline);
            cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
            cmd.Dispatch(dispatch.GroupCountX,
                         dispatch.GroupCountY,
                         dispatch.GroupCountZ);
        }
    }

    const char* DebugNameForProgressivePoissonGpuStatus(
        const ProgressivePoissonGpuStatus status) noexcept
    {
        switch (status)
        {
        case ProgressivePoissonGpuStatus::Success:
            return "Success";
        case ProgressivePoissonGpuStatus::InvalidInput:
            return "InvalidInput";
        case ProgressivePoissonGpuStatus::MissingDevice:
            return "MissingDevice";
        case ProgressivePoissonGpuStatus::DeviceUnavailable:
            return "DeviceUnavailable";
        case ProgressivePoissonGpuStatus::PlanningOnly:
            return "PlanningOnly";
        case ProgressivePoissonGpuStatus::SizeOverflow:
            return "SizeOverflow";
        case ProgressivePoissonGpuStatus::InvalidGpuResource:
            return "InvalidGpuResource";
        case ProgressivePoissonGpuStatus::InvalidReadback:
            return "InvalidReadback";
        case ProgressivePoissonGpuStatus::ParityMismatch:
            return "ParityMismatch";
        }
        return "Unknown";
    }

    const char* DebugNameForProgressivePoissonGpuPassKind(
        const ProgressivePoissonGpuPassKind kind) noexcept
    {
        switch (kind)
        {
        case ProgressivePoissonGpuPassKind::BuildLevelCells:
            return "BuildLevelCells";
        case ProgressivePoissonGpuPassKind::AcceptPhase:
            return "AcceptPhase";
        case ProgressivePoissonGpuPassKind::CompactAccepted:
            return "CompactAccepted";
        case ProgressivePoissonGpuPassKind::CompactRemaining:
            return "CompactRemaining";
        }
        return "Unknown";
    }

    const char* DebugNameForProgressivePoissonGpuBufferRole(
        const ProgressivePoissonGpuBufferRole role) noexcept
    {
        switch (role)
        {
        case ProgressivePoissonGpuBufferRole::None:
            return "None";
        case ProgressivePoissonGpuBufferRole::State:
            return "State";
        case ProgressivePoissonGpuBufferRole::PositionX:
            return "PositionX";
        case ProgressivePoissonGpuBufferRole::PositionY:
            return "PositionY";
        case ProgressivePoissonGpuBufferRole::PositionZ:
            return "PositionZ";
        case ProgressivePoissonGpuBufferRole::RemainingKeys:
            return "RemainingKeys";
        case ProgressivePoissonGpuBufferRole::NextRemainingKeys:
            return "NextRemainingKeys";
        case ProgressivePoissonGpuBufferRole::AcceptedKeys:
            return "AcceptedKeys";
        case ProgressivePoissonGpuBufferRole::CellKeys:
            return "CellKeys";
        case ProgressivePoissonGpuBufferRole::CellPhases:
            return "CellPhases";
        case ProgressivePoissonGpuBufferRole::AcceptFlags:
            return "AcceptFlags";
        case ProgressivePoissonGpuBufferRole::CarryFlags:
            return "CarryFlags";
        case ProgressivePoissonGpuBufferRole::HashKeys:
            return "HashKeys";
        case ProgressivePoissonGpuBufferRole::HashValues:
            return "HashValues";
        case ProgressivePoissonGpuBufferRole::LevelOffsets:
            return "LevelOffsets";
        case ProgressivePoissonGpuBufferRole::SplatRadii:
            return "SplatRadii";
        case ProgressivePoissonGpuBufferRole::OutputCount:
            return "OutputCount";
        case ProgressivePoissonGpuBufferRole::CompactionScratch:
            return "CompactionScratch";
        }
        return "Unknown";
    }

    ProgressivePoissonGpuDispatchPlan ComputeProgressivePoissonGpuDispatchPlan(
        const ProgressivePoissonGpuPlanDesc& desc)
    {
        ProgressivePoissonGpuDispatchPlan plan{};
        plan.InputCount = desc.InputCount;
        plan.Dimension = desc.Config.Dimension;
        plan.GroupSize = desc.GroupSize;
        plan.PhaseCount = PhaseCountForDimension(desc.Config.Dimension);

        if (!ConfigIsValid(desc))
        {
            plan.Status = ProgressivePoissonGpuStatus::InvalidInput;
            return plan;
        }

        bool hashOverflow = false;
        const std::uint32_t hashCapacity =
            HashCapacityFor(desc.InputCount,
                            desc.Config.HashLoadFactor,
                            hashOverflow);
        if (hashOverflow)
        {
            plan.Status = ProgressivePoissonGpuStatus::SizeOverflow;
            return plan;
        }

        const Graphics::ParallelPrimitiveDispatchPlan compactionPlan =
            Graphics::ComputeStreamCompactionDispatchPlan(desc.InputCount,
                                                          desc.GroupSize);
        if (!compactionPlan.IsValid())
        {
            plan.Status = ProgressivePoissonGpuStatus::InvalidInput;
            return plan;
        }

        bool layoutOverflow = false;
        plan.Layout = BuildLayout(desc.InputCount,
                                  desc.Config.MaxLevels,
                                  hashCapacity,
                                  compactionPlan.ScratchBytes,
                                  layoutOverflow);
        if (layoutOverflow)
        {
            plan.Status = ProgressivePoissonGpuStatus::SizeOverflow;
            return plan;
        }

        if (desc.InputCount == 0u)
        {
            return plan;
        }

        for (std::uint32_t level = 0u; level < desc.Config.MaxLevels; ++level)
        {
            ProgressivePoissonGpuLevelPlan levelPlan{};
            levelPlan.LevelIndex = level;
            levelPlan.PhaseCount = plan.PhaseCount;
            levelPlan.RemainingUpperBound = desc.InputCount;
            levelPlan.HashTableCapacity = hashCapacity;
            levelPlan.BuildCells = MakeMethodDispatch(
                ProgressivePoissonGpuPassKind::BuildLevelCells,
                level,
                0u,
                desc.InputCount,
                hashCapacity,
                desc.GroupSize);
            levelPlan.AcceptedCompaction = compactionPlan;
            levelPlan.RemainingCompaction = compactionPlan;

            plan.Dispatches.push_back(levelPlan.BuildCells);
            for (std::uint32_t phase = 0u; phase < plan.PhaseCount; ++phase)
            {
                ProgressivePoissonGpuDispatchDesc accept = MakeMethodDispatch(
                    ProgressivePoissonGpuPassKind::AcceptPhase,
                    level,
                    phase,
                    desc.InputCount,
                    hashCapacity,
                    desc.GroupSize);
                levelPlan.AcceptPhases.push_back(accept);
                plan.Dispatches.push_back(accept);
            }

            plan.Dispatches.push_back(MakeMethodDispatch(
                ProgressivePoissonGpuPassKind::CompactAccepted,
                level,
                0u,
                desc.InputCount,
                hashCapacity,
                desc.GroupSize));
            plan.Dispatches.push_back(MakeMethodDispatch(
                ProgressivePoissonGpuPassKind::CompactRemaining,
                level,
                0u,
                desc.InputCount,
                hashCapacity,
                desc.GroupSize));
            plan.Levels.push_back(std::move(levelPlan));
        }

        return plan;
    }

    RHI::BufferDesc BuildProgressivePoissonGpuStateBufferDesc(
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeof(ProgressivePoissonGpuStateBufferRecord),
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = debugName,
        };
    }

    RHI::BufferDesc BuildProgressivePoissonGpuWorkBufferDesc(
        const ProgressivePoissonGpuBufferLayout& layout,
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = layout.WorkBufferBytes,
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = debugName,
        };
    }

    RHI::BufferDesc BuildProgressivePoissonGpuReadbackBufferDesc(
        const std::uint64_t sizeBytes,
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeBytes,
            .Usage = RHI::BufferUsage::TransferDst |
                     RHI::BufferUsage::TransferSrc,
            .HostVisible = true,
            .DebugName = debugName,
        };
    }

    RHI::PipelineDesc BuildProgressivePoissonBuildCellsPipelineDesc(
        const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ProgressivePoissonGpuPassPushConstants));
        desc.DebugName = "ProgressivePoisson.BuildCells";
        return desc;
    }

    RHI::PipelineDesc BuildProgressivePoissonAcceptPhasePipelineDesc(
        const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ProgressivePoissonGpuPassPushConstants));
        desc.DebugName = "ProgressivePoisson.AcceptPhase";
        return desc;
    }

    ProgressivePoissonGpuResolveResult ResolveProgressivePoissonGpuRequest(
        const ProgressivePoissonGpuResolveDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return ResolveStatus(
                ProgressivePoissonGpuStatus::MissingDevice,
                "Vulkan compute requested but no RHI device was available.");
        }

        if (!desc.Device->IsOperational())
        {
            return ResolveStatus(
                ProgressivePoissonGpuStatus::DeviceUnavailable,
                "Vulkan compute requested but the RHI device is not operational.");
        }

        ProgressivePoissonGpuDispatchPlan plan =
            ComputeProgressivePoissonGpuDispatchPlan(desc.Plan);
        if (!plan.IsValid())
        {
            return ProgressivePoissonGpuResolveResult{
                .Status = plan.Status,
                .GpuExecutionAvailable = false,
                .CpuFallbackRecommended = true,
                .Diagnostic =
                    std::string{"Vulkan compute planning failed: "} +
                    DebugNameForProgressivePoissonGpuStatus(plan.Status) + ".",
                .Plan = std::move(plan),
            };
        }

        return ProgressivePoissonGpuResolveResult{
            .Status = ProgressivePoissonGpuStatus::PlanningOnly,
            .GpuExecutionAvailable = false,
            .CpuFallbackRecommended = true,
            .Diagnostic =
                "Vulkan compute dispatch and readback/parity seams are available, "
                "but METHOD-013 has not enabled public parity-proven GPU sampling yet.",
            .Plan = std::move(plan),
        };
    }

    ProgressivePoissonGpuStateBufferRecord BuildProgressivePoissonGpuStateRecord(
        RHI::IDevice& device,
        const ProgressivePoissonGpuResourceSet& resources) noexcept
    {
        return ProgressivePoissonGpuStateBufferRecord{
            .PositionXBDA = device.GetBufferDeviceAddress(resources.PositionX),
            .PositionYBDA = device.GetBufferDeviceAddress(resources.PositionY),
            .PositionZBDA = device.GetBufferDeviceAddress(resources.PositionZ),
            .RemainingKeysBDA = device.GetBufferDeviceAddress(resources.RemainingKeys),
            .NextRemainingKeysBDA =
                device.GetBufferDeviceAddress(resources.NextRemainingKeys),
            .AcceptedKeysBDA = device.GetBufferDeviceAddress(resources.AcceptedKeys),
            .CellKeysBDA = device.GetBufferDeviceAddress(resources.CellKeys),
            .CellPhasesBDA = device.GetBufferDeviceAddress(resources.CellPhases),
            .AcceptFlagsBDA = device.GetBufferDeviceAddress(resources.AcceptFlags),
            .CarryFlagsBDA = device.GetBufferDeviceAddress(resources.CarryFlags),
            .HashKeysBDA = device.GetBufferDeviceAddress(resources.HashKeys),
            .HashValuesBDA = device.GetBufferDeviceAddress(resources.HashValues),
            .LevelOffsetsBDA = device.GetBufferDeviceAddress(resources.LevelOffsets),
            .SplatRadiiBDA = device.GetBufferDeviceAddress(resources.SplatRadii),
            .OutputCountBDA = device.GetBufferDeviceAddress(resources.OutputCount),
            .CompactionScratchBDA =
                device.GetBufferDeviceAddress(resources.CompactionScratch),
        };
    }

    ProgressivePoissonGpuRecordResult RecordProgressivePoissonGpuPasses(
        const ProgressivePoissonGpuRecordDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return RecordStatus(ProgressivePoissonGpuStatus::MissingDevice,
                                desc.Plan);
        }

        if (!desc.Device->IsOperational())
        {
            return RecordStatus(ProgressivePoissonGpuStatus::DeviceUnavailable,
                                desc.Plan);
        }

        ProgressivePoissonGpuDispatchPlan plan =
            ComputeProgressivePoissonGpuDispatchPlan(desc.Plan);
        if (!plan.IsValid())
        {
            return ProgressivePoissonGpuRecordResult{
                .Status = plan.Status,
                .Recorded = false,
                .CpuFallbackRecommended = true,
                .Plan = std::move(plan),
            };
        }

        if (desc.CommandContext == nullptr)
        {
            return ProgressivePoissonGpuRecordResult{
                .Status = ProgressivePoissonGpuStatus::InvalidInput,
                .Recorded = false,
                .CpuFallbackRecommended = true,
                .Plan = std::move(plan),
            };
        }

        if (!PipelinesAreRecordable(desc.Pipelines) ||
            !ResourcesAreRecordable(desc.Resources, desc.Plan.Config.Dimension))
        {
            return ProgressivePoissonGpuRecordResult{
                .Status = ProgressivePoissonGpuStatus::InvalidGpuResource,
                .Recorded = false,
                .CpuFallbackRecommended = true,
                .Plan = std::move(plan),
            };
        }

        ProgressivePoissonGpuRecordResult result{};
        result.Status = ProgressivePoissonGpuStatus::Success;
        result.CpuFallbackRecommended = false;
        result.Plan = plan;

        const ProgressivePoissonGpuStateBufferRecord state =
            BuildProgressivePoissonGpuStateRecord(*desc.Device, desc.Resources);
        desc.Device->WriteBuffer(desc.Resources.State,
                                 &state,
                                 sizeof(state),
                                 0u);
        result.StateRecordUploaded = true;

        if (plan.Levels.empty())
        {
            return result;
        }

        RHI::ICommandContext& cmd = *desc.CommandContext;
        for (const ProgressivePoissonGpuLevelPlan& level : plan.Levels)
        {
            RecordMethodDispatch(cmd,
                                 *desc.Device,
                                 desc.Plan,
                                 level.BuildCells,
                                 desc.Resources,
                                 desc.Pipelines.BuildCells);
            ++result.MethodDispatchCount;
            cmd.BufferBarrier(desc.Resources.CellKeys,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
            cmd.BufferBarrier(desc.Resources.CellPhases,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
            cmd.BufferBarrier(desc.Resources.HashKeys,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
            cmd.BufferBarrier(desc.Resources.HashValues,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);

            for (const ProgressivePoissonGpuDispatchDesc& accept :
                 level.AcceptPhases)
            {
                RecordMethodDispatch(cmd,
                                     *desc.Device,
                                     desc.Plan,
                                     accept,
                                     desc.Resources,
                                     desc.Pipelines.AcceptPhase);
                ++result.MethodDispatchCount;
            }
            cmd.BufferBarrier(desc.Resources.AcceptFlags,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
            cmd.BufferBarrier(desc.Resources.CarryFlags,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);

            const Graphics::GpuParallelPrimitiveRecordResult accepted =
                Graphics::RecordGpuStreamCompaction(
                    Graphics::GpuStreamCompactionRecordDesc{
                        .Device = desc.Device,
                        .CommandContext = desc.CommandContext,
                        .Buffers = desc.Buffers,
                        .Pipelines = desc.Pipelines.Compaction,
                        .Keys = desc.Resources.RemainingKeys,
                        .Flags = desc.Resources.AcceptFlags,
                        .OutputKeys = desc.Resources.AcceptedKeys,
                        .OutputCount = desc.Resources.OutputCount,
                        .Scratch = desc.Resources.CompactionScratch,
                        .ElementCount = desc.Plan.InputCount,
                    });
            if (!accepted.Succeeded())
            {
                result.Status = ProgressivePoissonGpuStatus::InvalidGpuResource;
                result.CpuFallbackRecommended = true;
                result.FirstCompactionFailure = accepted.Status;
                return result;
            }
            ++result.AcceptedCompactionCount;

            const Graphics::GpuParallelPrimitiveRecordResult remaining =
                Graphics::RecordGpuStreamCompaction(
                    Graphics::GpuStreamCompactionRecordDesc{
                        .Device = desc.Device,
                        .CommandContext = desc.CommandContext,
                        .Buffers = desc.Buffers,
                        .Pipelines = desc.Pipelines.Compaction,
                        .Keys = desc.Resources.RemainingKeys,
                        .Flags = desc.Resources.CarryFlags,
                        .OutputKeys = desc.Resources.NextRemainingKeys,
                        .OutputCount = desc.Resources.OutputCount,
                        .Scratch = desc.Resources.CompactionScratch,
                        .ElementCount = desc.Plan.InputCount,
                    });
            if (!remaining.Succeeded())
            {
                result.Status = ProgressivePoissonGpuStatus::InvalidGpuResource;
                result.CpuFallbackRecommended = true;
                result.FirstCompactionFailure = remaining.Status;
                return result;
            }
            ++result.RemainingCompactionCount;
        }

        result.Recorded = true;
        return result;
    }

    ProgressivePoissonGpuExecutionResult RecordProgressivePoissonGpuExecution(
        const ProgressivePoissonGpuExecutionDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return ExecutionStatus(ProgressivePoissonGpuStatus::MissingDevice,
                                   desc.Plan);
        }

        if (!desc.Device->IsOperational())
        {
            return ExecutionStatus(ProgressivePoissonGpuStatus::DeviceUnavailable,
                                   desc.Plan);
        }

        if (desc.CommandContext == nullptr || desc.Buffers == nullptr)
        {
            return ExecutionStatus(ProgressivePoissonGpuStatus::InvalidInput,
                                   desc.Plan);
        }

        if (desc.Positions.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            return ExecutionStatus(ProgressivePoissonGpuStatus::SizeOverflow,
                                   desc.Plan);
        }

        if (desc.Plan.InputCount !=
            static_cast<std::uint32_t>(desc.Positions.size()))
        {
            return ExecutionStatus(ProgressivePoissonGpuStatus::InvalidInput,
                                   desc.Plan);
        }

        ProgressivePoissonGpuDispatchPlan plan =
            ComputeProgressivePoissonGpuDispatchPlan(desc.Plan);
        if (!plan.IsValid())
        {
            return ProgressivePoissonGpuExecutionResult{
                .Status = plan.Status,
                .Recorded = false,
                .CpuFallbackRecommended = true,
                .Plan = std::move(plan),
            };
        }

        ProgressivePoissonGpuExecutionResult result{};
        result.Status = ProgressivePoissonGpuStatus::Success;
        result.CpuFallbackRecommended = true;
        result.Plan = plan;

        if (plan.InputCount == 0u)
        {
            result.Resources = BuildEmptyExecutionResources();
            return result;
        }

        if (!AllocateExecutionResources(*desc.Buffers,
                                        plan,
                                        result.Resources))
        {
            result.Status = ProgressivePoissonGpuStatus::InvalidGpuResource;
            return result;
        }

        UploadExecutionInputs(*desc.Device,
                              desc.Positions,
                              result.Resources,
                              result);

        result.Record = RecordProgressivePoissonGpuPasses(
            ProgressivePoissonGpuRecordDesc{
                .Device = desc.Device,
                .CommandContext = desc.CommandContext,
                .Buffers = desc.Buffers,
                .Pipelines = desc.Pipelines,
                .Resources = result.Resources.Resources,
                .Plan = desc.Plan,
            });
        if (!result.Record.Succeeded() || !result.Record.Recorded)
        {
            result.Status = result.Record.Status;
            result.CpuFallbackRecommended = true;
            return result;
        }

        RecordExecutionReadbacks(*desc.CommandContext,
                                 plan,
                                 result.Resources,
                                 result);
        result.Recorded = result.Record.Recorded && result.ReadbackCopiesRecorded;
        return result;
    }

    ProgressivePoissonGpuReadbackResult ReadProgressivePoissonGpuReadbacks(
        const ProgressivePoissonGpuReadbackDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return ReadbackStatus(ProgressivePoissonGpuStatus::MissingDevice,
                                  "Progressive Poisson GPU readback requires an RHI device.");
        }
        if (!desc.Device->IsOperational())
        {
            return ReadbackStatus(ProgressivePoissonGpuStatus::DeviceUnavailable,
                                  "Progressive Poisson GPU readback requires an operational RHI device.");
        }
        if (!desc.Plan.IsValid())
        {
            return ReadbackStatus(desc.Plan.Status,
                                  "Progressive Poisson GPU readback requires a valid dispatch plan.");
        }

        const std::uint32_t inputCount = desc.Plan.InputCount;
        if (inputCount == 0u)
        {
            return ProgressivePoissonGpuReadbackResult{
                .Status = ProgressivePoissonGpuStatus::Success,
                .Read = true,
                .StructurallyValid = true,
                .CpuFallbackRecommended = false,
                .AcceptedCount = 0u,
                .LevelOffsets = std::vector<std::uint32_t>{0u},
            };
        }

        if (!desc.OrderReadback.IsValid() ||
            !desc.LevelOffsetsReadback.IsValid() ||
            !desc.SplatRadiiReadback.IsValid())
        {
            return ReadbackStatus(ProgressivePoissonGpuStatus::InvalidGpuResource,
                                  "Progressive Poisson GPU readback requires valid readback buffers.");
        }

        std::vector<std::uint32_t> order(inputCount, 0u);
        std::vector<std::uint32_t> levelOffsets(desc.Plan.Layout.MaxLevels + 1u, 0u);
        std::vector<float> splatRadii(inputCount, 0.0f);

        ReadBufferVector(*desc.Device, desc.OrderReadback, order);
        ReadBufferVector(*desc.Device, desc.LevelOffsetsReadback, levelOffsets);
        ReadBufferVector(*desc.Device, desc.SplatRadiiReadback, splatRadii);

        return ValidateReadbackPayload(std::move(order),
                                       std::move(levelOffsets),
                                       std::move(splatRadii),
                                       inputCount);
    }

    ProgressivePoissonGpuParityDiagnostics
    CompareProgressivePoissonGpuOutputToReference(
        const ProgressivePoissonGpuParityDesc& desc)
    {
        if (desc.Dimension != 2u && desc.Dimension != 3u)
        {
            return ParityStatus(ProgressivePoissonGpuStatus::InvalidInput,
                                "Progressive Poisson GPU parity requires dimension 2 or 3.");
        }
        if (desc.GpuLevelOffsets.empty() ||
            desc.Reference.LevelOffsets.empty() ||
            desc.Reference.LevelRadii.size() + 1u !=
                desc.Reference.LevelOffsets.size())
        {
            return ParityStatus(ProgressivePoissonGpuStatus::InvalidInput,
                                "Progressive Poisson GPU parity requires reference level offsets and radii.");
        }
        if (!LevelOffsetsAreMonotonic(desc.GpuLevelOffsets) ||
            !LevelOffsetsAreMonotonic(desc.Reference.LevelOffsets))
        {
            return ParityStatus(ProgressivePoissonGpuStatus::InvalidInput,
                                "Progressive Poisson GPU parity requires monotonic level offsets.");
        }

        ProgressivePoissonGpuParityDiagnostics diagnostics{};
        diagnostics.Compared = true;
        diagnostics.GpuAcceptedCount = desc.GpuLevelOffsets.back();
        diagnostics.ReferenceAcceptedCount = desc.Reference.LevelOffsets.back();
        diagnostics.OrderMismatchCount =
            CountSpanMismatches(desc.GpuOrder, desc.Reference.Order);
        diagnostics.LevelOffsetMismatchCount =
            CountSpanMismatches(desc.GpuLevelOffsets, desc.Reference.LevelOffsets);

        diagnostics.OrderMatches = diagnostics.OrderMismatchCount == 0u;
        diagnostics.LevelOffsetsMatch =
            diagnostics.LevelOffsetMismatchCount == 0u;

        const std::size_t commonSplat =
            std::min(desc.GpuSplatRadii.size(), desc.Reference.SplatRadii.size());
        diagnostics.SplatRadiusMismatchCount = static_cast<std::uint32_t>(
            desc.GpuSplatRadii.size() > desc.Reference.SplatRadii.size()
                ? desc.GpuSplatRadii.size() - desc.Reference.SplatRadii.size()
                : desc.Reference.SplatRadii.size() - desc.GpuSplatRadii.size());
        for (std::size_t index = 0u; index < commonSplat; ++index)
        {
            const float delta =
                std::fabs(desc.GpuSplatRadii[index] -
                          desc.Reference.SplatRadii[index]);
            diagnostics.MaxSplatRadiusAbsDelta =
                std::max(diagnostics.MaxSplatRadiusAbsDelta, delta);
            if (!std::isfinite(delta) || delta > desc.SplatRadiusTolerance)
            {
                ++diagnostics.SplatRadiusMismatchCount;
            }
        }
        diagnostics.SplatRadiiWithinTolerance =
            diagnostics.SplatRadiusMismatchCount == 0u;

        bool sawPoissonPrefix = false;
        float minRatio = std::numeric_limits<float>::max();
        bool poissonHolds = true;
        const std::size_t levelCount =
            std::min(desc.GpuLevelOffsets.size(),
                     desc.Reference.LevelOffsets.size()) - 1u;
        for (std::size_t level = 0u; level < levelCount; ++level)
        {
            const std::uint32_t prefixCount = desc.GpuLevelOffsets[level + 1u];
            if (prefixCount < 2u)
            {
                continue;
            }
            if (prefixCount > desc.GpuOrder.size() ||
                prefixCount > desc.Positions.size())
            {
                poissonHolds = false;
                minRatio = 0.0f;
                break;
            }

            const float radius = desc.Reference.LevelRadii[level];
            if (!(radius > 0.0f) || !std::isfinite(radius))
            {
                poissonHolds = false;
                minRatio = 0.0f;
                break;
            }

            const float measured = MinPairwiseDistanceForPrefix(
                desc.Positions,
                desc.GpuOrder,
                prefixCount,
                desc.Dimension);
            const float ratio = measured == std::numeric_limits<float>::max()
                ? std::numeric_limits<float>::max()
                : measured / radius;
            minRatio = std::min(minRatio, ratio);
            sawPoissonPrefix = true;
            if (!std::isfinite(ratio) ||
                ratio + desc.PoissonRatioTolerance < 1.0f)
            {
                poissonHolds = false;
            }
        }
        diagnostics.MinPoissonRatio =
            sawPoissonPrefix && minRatio != std::numeric_limits<float>::max()
                ? minRatio
                : 1.0f;
        diagnostics.PoissonGuaranteeHolds = poissonHolds;

        diagnostics.MatchesReference =
            diagnostics.GpuAcceptedCount == diagnostics.ReferenceAcceptedCount &&
            diagnostics.OrderMatches &&
            diagnostics.LevelOffsetsMatch &&
            diagnostics.SplatRadiiWithinTolerance &&
            diagnostics.PoissonGuaranteeHolds;
        diagnostics.Status = diagnostics.MatchesReference
            ? ProgressivePoissonGpuStatus::Success
            : ProgressivePoissonGpuStatus::ParityMismatch;
        diagnostics.CpuFallbackRecommended = !diagnostics.MatchesReference;
        diagnostics.Diagnostic = diagnostics.MatchesReference
            ? "Progressive Poisson GPU output matches the CPU reference within tolerance."
            : "Progressive Poisson GPU output differs from the CPU reference; CPU fallback is required.";
        return diagnostics;
    }
}
