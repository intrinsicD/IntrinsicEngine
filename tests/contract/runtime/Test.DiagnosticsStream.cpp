#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

import Extrinsic.Core.Logging;
import Extrinsic.Core.Telemetry;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.DiagnosticsStream;

namespace R = Extrinsic::Runtime;
namespace Log = Extrinsic::Core::Log;
using Json = nlohmann::json;

TEST(DiagnosticsStream, RuntimeAndAgentShareCursorAndFilters)
{
    Log::ClearEntries();
    const auto cursor = Log::GetSequenceNumber();
    Log::Info("[Selected] one");
    Log::Warn("[Other] invisible");
    Log::Error("[Selected] two");
    R::EditorDiagnosticsStream stream;
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    R::AgentOperationContext context{.Diagnostics = &stream};
    const auto read = [&](std::uint64_t since) {
        return R::InvokeAgentOperation(registry, "diagnostics_read", context,
            Json{{"since_cursor", since}, {"categories", {"Selected"}}, {"levels", {0, 2}}, {"limit", 1}}.dump(), true);
    };
    const auto first = read(cursor);
    ASSERT_FALSE(first.IsError) << first.Text;
    const auto page = Json::parse(first.Text);
    ASSERT_EQ(page["entries"].size(), 1u);
    EXPECT_EQ(page["entries"][0]["message"], "[Selected] one");
    EXPECT_EQ(page["next_cursor"], cursor + 2);
    const auto second = read(page["next_cursor"].get<std::uint64_t>());
    ASSERT_FALSE(second.IsError);
    EXPECT_EQ(Json::parse(second.Text)["entries"][0]["message"], "[Selected] two");
    EXPECT_TRUE(stream.OperationRecords().empty()) << "Observation must not record itself";
}

TEST(DiagnosticsStream, AgentRejectsMalformedFiltersAndBounds)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    for (const std::string_view args : {R"({"limit":0})", R"({"limit":1001})", R"({"limit":-1})",
        R"({"since_cursor":-1})", R"({"since_cursor":1.5})", R"({"levels":[4]})", R"({"levels":["error"]})",
        R"({"categories":[2]})", R"({"levels":false})", R"({"unknown":1})", "[]"})
    {
        const auto result = R::InvokeAgentOperation(registry, "diagnostics_read", {}, args, true);
        EXPECT_TRUE(result.IsError) << args;
        EXPECT_EQ(result.ErrorCode, "invalid_arguments") << args;
    }
    const auto result = R::InvokeAgentOperation(registry, "device_status", {}, "{}", true);
    ASSERT_FALSE(result.IsError);
    EXPECT_EQ(Json::parse(result.Text)["actual_backend"], "unavailable");
}

TEST(DiagnosticsStream, InvocationRecordsIncludeFailuresSourceAllocationsAndBackend)
{
    R::EditorDiagnosticsStream stream;
    R::AgentOperationRegistry registry;
    ASSERT_TRUE(registry.Register({.Name = "compute", .ReadOnly = false,
        .Invoke = [](const R::AgentOperationContext&, std::string_view) {
            Extrinsic::Core::Telemetry::Alloc::RecordAlloc(91);
            return R::AgentOperationOutcome{.Text = R"({"requested_backend":"gpu_compute","actual_backend":"cpu","backend_fallback_reason":"device unavailable"})"};
        }}));
    R::AgentOperationContext context{.Diagnostics = &stream, .Source = R::DiagnosticOperationSource::Editor};
    EXPECT_FALSE(R::InvokeAgentOperation(registry, "compute", context, "{}", false).IsError);
    EXPECT_TRUE(R::InvokeAgentOperation(registry, "compute", context, "{}", true).IsError);
    EXPECT_TRUE(R::InvokeAgentOperation(registry, "unknown", context, "{}", false).IsError);
    const auto records = R::ReadEditorDiagnostics(&stream, 0).OperationRecords;
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].Name, "compute");
    EXPECT_EQ(records[0].Source, R::DiagnosticOperationSource::Editor);
    EXPECT_EQ(records[0].Status, R::DiagnosticOperationStatus::Succeeded);
    EXPECT_GE(records[0].AllocationDeltaBytes, 91u);
    EXPECT_EQ(records[0].RequestedBackend, "gpu_compute");
    EXPECT_EQ(records[0].ActualBackend, "cpu");
    EXPECT_EQ(records[0].BackendFallbackReason, "device unavailable");
    EXPECT_EQ(records[1].Status, R::DiagnosticOperationStatus::Failed);
    EXPECT_EQ(records[2].Status, R::DiagnosticOperationStatus::Failed);
}

TEST(DiagnosticsStream, DeferredInvocationUpdatesOneRecordAndDroppedContinuationsAreAbandoned)
{
    R::EditorDiagnosticsStream stream;
    R::AgentOperationRegistry registry;
    bool ready = false;
    ASSERT_TRUE(registry.Register({.Name = "later", .Invoke = [&](const R::AgentOperationContext&, std::string_view) {
        return R::AgentOperationOutcome{.Continuation = [&](const R::AgentOperationContext&, R::AgentOperationOutcome& out) {
            if (!ready) return false;
            out = {.Text = R"({"requested_backend":2,"backend":"cpu"})"};
            return true;
        }};
    }}));
    R::AgentOperationContext context{.Diagnostics = &stream};
    auto pending = R::InvokeAgentOperation(registry, "later", context, "{}", false);
    ASSERT_EQ(stream.OperationRecords().size(), 1u);
    EXPECT_EQ(stream.OperationRecords()[0].Status, R::DiagnosticOperationStatus::Pending);
    R::AgentOperationOutcome completed;
    EXPECT_FALSE(pending.Continuation(context, completed));
    ready = true;
    EXPECT_TRUE(pending.Continuation(context, completed));
    ASSERT_EQ(stream.OperationRecords().size(), 1u);
    EXPECT_EQ(stream.OperationRecords()[0].Status, R::DiagnosticOperationStatus::Succeeded);
    EXPECT_EQ(stream.OperationRecords()[0].RequestedBackend, "2");
    EXPECT_EQ(stream.OperationRecords()[0].ActualBackend, "cpu");
    { const auto abandoned = R::InvokeAgentOperation(registry, "later", context, "{}", false); }
    ASSERT_EQ(stream.OperationRecords().size(), 2u);
    EXPECT_EQ(stream.OperationRecords()[1].Status, R::DiagnosticOperationStatus::Abandoned);
}

TEST(DiagnosticsStream, OperationHistoryIsBoundedAndEvictedPendingRecordsStayEvicted)
{
    R::EditorDiagnosticsStream stream;
    const auto first = stream.AppendOperation({.Name = "pending"});
    for (int i = 0; i < 300; ++i)
        (void)stream.AppendOperation({.Name = std::to_string(i), .Status = R::DiagnosticOperationStatus::Succeeded});
    stream.UpdateOperation(first, {.Name = "late completion"});
    ASSERT_EQ(stream.OperationRecords().size(), 256u);
    EXPECT_EQ(stream.OperationRecords().front().Name, "44");
    EXPECT_EQ(stream.OperationRecords().back().Name, "299");
}

TEST(DiagnosticsStream, DeferredAllocationDeltaSurvivesFrameCounterReset)
{
    namespace Alloc = Extrinsic::Core::Telemetry::Alloc;
    R::EditorDiagnosticsStream stream;
    R::AgentOperationRegistry registry;
    ASSERT_TRUE(registry.Register({.Name = "later", .Invoke = [](const R::AgentOperationContext&, std::string_view) {
        Alloc::RecordAlloc(50);
        return R::AgentOperationOutcome{.Continuation = [](const R::AgentOperationContext&, R::AgentOperationOutcome& out) {
            out = {};
            return true;
        }};
    }}));
    R::AgentOperationContext context{.Diagnostics = &stream};
    Alloc::Reset();
    Alloc::RecordAlloc(100);
    auto pending = R::InvokeAgentOperation(registry, "later", context, "{}", false);
    ASSERT_TRUE(pending.Continuation);
    const auto totalBeforeReset = Alloc::SnapshotCumulativeBytes();
    Alloc::Reset();
    EXPECT_EQ(Alloc::SnapshotBytes(), 0u);
    EXPECT_EQ(Alloc::SnapshotCount(), 0u);
    EXPECT_EQ(Alloc::SnapshotCumulativeBytes(), totalBeforeReset);
    Alloc::RecordAlloc(10);
    R::AgentOperationOutcome completed;
    ASSERT_TRUE(pending.Continuation(context, completed));
    ASSERT_EQ(stream.OperationRecords().size(), 1u);
    EXPECT_EQ(stream.OperationRecords()[0].Status, R::DiagnosticOperationStatus::Succeeded);
    EXPECT_EQ(stream.OperationRecords()[0].AllocationDeltaBytes, 60u);
}

TEST(DiagnosticsStream, AgentReportsAheadCursorRecovery)
{
    Log::ClearEntries();
    Log::Info("[Restart] current process");
    const auto newest = Log::GetSequenceNumber();
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    const auto result = R::InvokeAgentOperation(registry, "diagnostics_read", {},
        Json{{"since_cursor", newest + 1000}, {"limit", 1}}.dump(), true);
    ASSERT_FALSE(result.IsError) << result.Text;
    const auto page = Json::parse(result.Text);
    EXPECT_TRUE(page["cursor_reset"].get<bool>());
    EXPECT_EQ(page["next_cursor"], newest);
    ASSERT_EQ(page["entries"].size(), 1u);
    EXPECT_EQ(page["entries"][0]["message"], "[Restart] current process");
    const auto next = R::InvokeAgentOperation(registry, "diagnostics_read", {},
        Json{{"since_cursor", page["next_cursor"]}}.dump(), true);
    ASSERT_FALSE(next.IsError);
    EXPECT_FALSE(Json::parse(next.Text)["cursor_reset"].get<bool>());
}

TEST(DiagnosticsStream, PropertyValuePageKeepsItsResponseAndInvocationAccounting)
{
    R::EditorDiagnosticsStream stream;
    R::AgentOperationRegistry registry;
    const std::string payload = R"({"values":[[1,2,3],[4,5,6]],"next_offset":2})";
    ASSERT_TRUE(registry.Register({.Name = "property_values", .Invoke = [&](const R::AgentOperationContext&, std::string_view) {
        Extrinsic::Core::Telemetry::Alloc::RecordAlloc(37);
        return R::AgentOperationOutcome{.Text = payload};
    }}));
    const auto outcome = R::InvokeAgentOperation(registry, "property_values", {.Diagnostics = &stream}, "{}", true);
    EXPECT_FALSE(outcome.IsError);
    EXPECT_EQ(outcome.Text, payload);
    ASSERT_EQ(stream.OperationRecords().size(), 1u);
    const auto& record = stream.OperationRecords().front();
    EXPECT_EQ(record.Name, "property_values");
    EXPECT_EQ(record.Status, R::DiagnosticOperationStatus::Succeeded);
    EXPECT_EQ(record.AllocationDeltaBytes, 37u);
    EXPECT_TRUE(record.RequestedBackend.empty());
    EXPECT_TRUE(record.ActualBackend.empty());
    EXPECT_TRUE(record.BackendFallbackReason.empty());
}
