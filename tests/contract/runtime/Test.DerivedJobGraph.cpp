#include <gtest/gtest.h>

#include <expected>
#include <string>
#include <utility>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StreamingExecutor;

namespace Runtime = Extrinsic::Runtime;
namespace Core = Extrinsic::Core;

namespace
{
    [[nodiscard]] Runtime::DerivedJobKey MakeKey(
        const std::uint32_t entityId,
        const Runtime::ProgressiveSlotSemantic semantic =
            Runtime::ProgressiveSlotSemantic::Normal,
        const std::uint64_t sourceGeneration = 1u,
        const std::uint64_t bindingGeneration = 1u)
    {
        return Runtime::DerivedJobKey{
            .EntityId = entityId,
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .OutputSemantic = semantic,
            .EntityGeneration = 1u,
            .GeometryGeneration = 1u,
            .SourcePropertyGeneration = sourceGeneration,
            .BindingGeneration = bindingGeneration,
            .OutputName = std::string{Runtime::ToString(semantic)},
        };
    }

    [[nodiscard]] Runtime::DerivedJobDesc MakeTokenJob(
        const std::uint32_t entityId,
        const std::string& name,
        const std::uint64_t token)
    {
        return Runtime::DerivedJobDesc{
            .Key = MakeKey(entityId),
            .Name = name,
            .Execute = [token]() -> Runtime::DerivedJobWorkerResult
            {
                return Runtime::DerivedJobOutput{
                    .PayloadToken = token,
                    .Diagnostic = "ready",
                };
            },
            .ApplyOnMainThread = [](Runtime::DerivedJobApplyContext&) -> Core::Result
            {
                return Core::Ok();
            },
        };
    }
}

TEST(RuntimeDerivedJobGraph, DependencyAndFollowUpJobsAreObservable)
{
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    std::vector<std::string> order{};

    auto normal = Runtime::DerivedJobDesc{
        .Key = MakeKey(17u, Runtime::ProgressiveSlotSemantic::Normal),
        .Name = "compute normals",
        .Execute = []() -> Runtime::DerivedJobWorkerResult
        {
            return Runtime::DerivedJobOutput{.PayloadToken = 11u};
        },
        .ApplyOnMainThread = [&order](Runtime::DerivedJobApplyContext& context) -> Core::Result
        {
            order.push_back("normal");
            Runtime::DerivedJobDesc bake{
                .Key = MakeKey(17u, Runtime::ProgressiveSlotSemantic::Normal, 2u, 1u),
                .Name = "bake normal map",
                .Execute = []() -> Runtime::DerivedJobWorkerResult
                {
                    return Runtime::DerivedJobOutput{.PayloadToken = 22u};
                },
                .ApplyOnMainThread = [&order](Runtime::DerivedJobApplyContext&) -> Core::Result
                {
                    order.push_back("bake");
                    return Core::Ok();
                },
            };
            EXPECT_NE(context.Registry, nullptr);
            (void)context.Registry->SubmitFollowUp(
                context.Handle,
                std::move(bake),
                "normal property ready");
            return Core::Ok();
        },
    };

    const Runtime::DerivedJobHandle normalHandle = jobs.Submit(std::move(normal));
    ASSERT_TRUE(normalHandle.IsValid());

    jobs.Pump(1);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.GetStatus(normalHandle), Runtime::DerivedJobStatus::Applying);
    jobs.ApplyMainThreadResults();
    EXPECT_EQ(jobs.GetStatus(normalHandle), Runtime::DerivedJobStatus::Complete);

    auto entitySnapshot = jobs.SnapshotEntity(17u);
    ASSERT_EQ(entitySnapshot.Entries.size(), 2u);
    const Runtime::DerivedJobSnapshot& followUp = entitySnapshot.Entries[1];
    EXPECT_EQ(followUp.Name, "bake normal map");
    ASSERT_EQ(followUp.Dependencies.size(), 1u);
    EXPECT_EQ(followUp.Dependencies[0].Job, normalHandle);
    EXPECT_EQ(followUp.Dependencies[0].Reason, "normal property ready");
    EXPECT_EQ(followUp.Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1);
    jobs.DrainCompletions();
    jobs.ApplyMainThreadResults();

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "normal");
    EXPECT_EQ(order[1], "bake");
    EXPECT_EQ(jobs.SnapshotEntity(17u).Entries[1].Status, Runtime::DerivedJobStatus::Complete);
}

TEST(RuntimeDerivedJobGraph, StaleApplyIsDiscardedBeforeMutation)
{
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    bool applied = false;

    auto desc = MakeTokenJob(23u, "stale normals", 100u);
    desc.Key = MakeKey(23u, Runtime::ProgressiveSlotSemantic::Normal, 1u, 9u);
    desc.ValidateOnMainThread = []()
    {
        return Runtime::DerivedJobApplyValidation::StaleSourcePropertyGeneration;
    };
    desc.ApplyOnMainThread = [&applied](Runtime::DerivedJobApplyContext&) -> Core::Result
    {
        applied = true;
        return Core::Ok();
    };

    const Runtime::DerivedJobHandle handle = jobs.Submit(std::move(desc));
    jobs.Pump(1);
    jobs.DrainCompletions();
    jobs.ApplyMainThreadResults();

    const auto snapshot = jobs.Snapshot(handle);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(applied);
    EXPECT_EQ(snapshot->Status, Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(snapshot->Diagnostic.find("StaleSourcePropertyGeneration"), std::string::npos);
}

TEST(RuntimeDerivedJobGraph, FailureRetainsPreviousOutput)
{
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};

    auto desc = Runtime::DerivedJobDesc{
        .Key = MakeKey(31u, Runtime::ProgressiveSlotSemantic::Albedo),
        .Name = "replace albedo",
        .HasPreviousOutput = true,
        .Execute = []() -> Runtime::DerivedJobWorkerResult
        {
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        },
        .ApplyOnMainThread = [](Runtime::DerivedJobApplyContext&) -> Core::Result
        {
            return Core::Ok();
        },
    };

    const Runtime::DerivedJobHandle handle = jobs.Submit(std::move(desc));
    jobs.Pump(1);
    jobs.DrainCompletions();
    jobs.ApplyMainThreadResults();

    const auto snapshot = jobs.Snapshot(handle);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->Status, Runtime::DerivedJobStatus::Failed);
    EXPECT_TRUE(snapshot->PreviousOutputRetained);
    EXPECT_NE(snapshot->Diagnostic.find("InvalidArgument"), std::string::npos);
}

TEST(RuntimeDerivedJobGraph, CancellationPreventsPendingMutation)
{
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    bool ran = false;

    auto desc = MakeTokenJob(41u, "cancelled normal", 1u);
    desc.Execute = [&ran]() -> Runtime::DerivedJobWorkerResult
    {
        ran = true;
        return Runtime::DerivedJobOutput{.PayloadToken = 1u};
    };

    const Runtime::DerivedJobHandle handle = jobs.Submit(std::move(desc));
    jobs.Cancel(handle);
    jobs.Pump(1);
    jobs.DrainCompletions();
    jobs.ApplyMainThreadResults();

    const auto snapshot = jobs.Snapshot(handle);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(ran);
    EXPECT_EQ(snapshot->Status, Runtime::DerivedJobStatus::Cancelled);
    EXPECT_EQ(snapshot->Diagnostic, "cancelled");
}

TEST(RuntimeDerivedJobGraph, UnsupportedGpuDomainsFailClosed)
{
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    bool ran = false;

    auto desc = MakeTokenJob(51u, "gpu normal bake", 9u);
    desc.RequestedJobDomain = Runtime::ProgressiveJobDomain::GpuCompute;
    desc.Execute = [&ran]() -> Runtime::DerivedJobWorkerResult
    {
        ran = true;
        return Runtime::DerivedJobOutput{.PayloadToken = 9u};
    };

    const Runtime::DerivedJobHandle handle = jobs.Submit(std::move(desc));
    const auto snapshot = jobs.Snapshot(handle);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(ran);
    EXPECT_EQ(snapshot->Status, Runtime::DerivedJobStatus::Failed);
    EXPECT_EQ(snapshot->RequestedJobDomain, Runtime::ProgressiveJobDomain::GpuCompute);
    EXPECT_NE(snapshot->Diagnostic.find("GpuCompute"), std::string::npos);
}
