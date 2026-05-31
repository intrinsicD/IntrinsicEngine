module;

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>

module Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	namespace
	{
		// Upper bound on un-drained completed readbacks. The runtime drains the
		// queue every frame, so it normally holds at most frames-in-flight
		// entries; the cap only guards a never-draining consumer from unbounded
		// growth by dropping the oldest result.
		constexpr std::size_t kMaxCompletedPicks = 64u;
	}

	struct SelectionSystem::Impl
	{
		std::optional<PickRequest>           PendingPick;
		std::deque<PickReadbackResult>       CompletedPicks;
		SelectionSystemDiagnostics           Diagnostics{};
		bool                                 Initialized{false};
	};

	SelectionSystem::SelectionSystem()
		: m_Impl(std::make_unique<Impl>())
	{}

	SelectionSystem::~SelectionSystem() = default;

	void SelectionSystem::Initialize()
	{
		m_Impl->Initialized = true;
	}

	void SelectionSystem::Shutdown()
	{
		m_Impl->PendingPick.reset();
		m_Impl->CompletedPicks.clear();
		m_Impl->Diagnostics = {};
		m_Impl->Initialized = false;
	}

	void SelectionSystem::RequestPointIdPick(PointSelectionRequest request) noexcept
	{
		RequestPick(PickRequest{.PixelX = request.PixelX, .PixelY = request.PixelY});
	}

	bool SelectionSystem::HasPendingPointIdPick() const noexcept
	{
		return HasPendingPick();
	}

	std::optional<PointSelectionRequest> SelectionSystem::ConsumePointIdPick() noexcept
	{
		const std::optional<PickRequest> request = ConsumePick();
		if (!request.has_value())
		{
			return std::nullopt;
		}
		return PointSelectionRequest{.PixelX = request->PixelX, .PixelY = request->PixelY};
	}

	void SelectionSystem::PublishPointIdResult(PointSelectionResult result) noexcept
	{
		PublishPickResult(PickReadbackResult{
			.EncodedId = EncodeSelectionId(SelectionPrimitiveDomain::Point, result.PointID),
			.StableEntityId = result.EntityID,
			.Hit = result.PointID != 0u || result.EntityID != 0u,
		});
	}

	std::optional<PointSelectionResult> SelectionSystem::GetLastPointIdResult() const noexcept
	{
		const std::optional<PickReadbackResult> result = GetLastPickResult();
		if (!result.has_value() || result->EncodedId.Domain() != SelectionPrimitiveDomain::Point)
		{
			return std::nullopt;
		}
		return PointSelectionResult{.PointID = result->EncodedId.Payload(), .EntityID = result->StableEntityId};
	}

	void SelectionSystem::ClearLastPointIdResult() noexcept
	{
		ClearLastPickResult();
	}

	void SelectionSystem::RequestPick(PickRequest request) noexcept
	{
		m_Impl->PendingPick = request;
		++m_Impl->Diagnostics.PickRequestCount;
	}

	bool SelectionSystem::HasPendingPick() const noexcept
	{
		return m_Impl->PendingPick.has_value();
	}

	std::optional<PickRequest> SelectionSystem::ConsumePick() noexcept
	{
		std::optional<PickRequest> request = m_Impl->PendingPick;
		if (request.has_value())
		{
			++m_Impl->Diagnostics.PickConsumeCount;
		}
		m_Impl->PendingPick.reset();
		return request;
	}

	void SelectionSystem::PublishPickResult(PickReadbackResult result) noexcept
	{
		result.Hit = result.Hit && result.EncodedId.IsHit();
		m_Impl->CompletedPicks.push_back(result);
		while (m_Impl->CompletedPicks.size() > kMaxCompletedPicks)
		{
			m_Impl->CompletedPicks.pop_front();
		}
		if (result.Hit)
		{
			++m_Impl->Diagnostics.PickHitCount;
		}
		else
		{
			++m_Impl->Diagnostics.PickNoHitCount;
		}
	}

	void SelectionSystem::PublishNoHit() noexcept
	{
		PublishPickResult(PickReadbackResult{});
	}

	std::optional<PickReadbackResult> SelectionSystem::PopPickResult() noexcept
	{
		if (m_Impl->CompletedPicks.empty())
		{
			return std::nullopt;
		}
		const PickReadbackResult front = m_Impl->CompletedPicks.front();
		m_Impl->CompletedPicks.pop_front();
		return front;
	}

	std::optional<PickReadbackResult> SelectionSystem::GetLastPickResult() const noexcept
	{
		if (m_Impl->CompletedPicks.empty())
		{
			return std::nullopt;
		}
		return m_Impl->CompletedPicks.back();
	}

	void SelectionSystem::ClearLastPickResult() noexcept
	{
		m_Impl->CompletedPicks.clear();
	}

	SelectionSystemDiagnostics SelectionSystem::GetDiagnostics() const noexcept
	{
		return m_Impl->Diagnostics;
	}

	bool SelectionSystem::IsInitialized() const noexcept
	{
		return m_Impl->Initialized;
	}
}
