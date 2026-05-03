module;

#include <memory>
#include <optional>

module Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	struct SelectionSystem::Impl
	{
		std::optional<PickRequest>           PendingPick;
		std::optional<PickReadbackResult>    LastPick;
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
		m_Impl->LastPick.reset();
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
		m_Impl->LastPick = result;
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

	std::optional<PickReadbackResult> SelectionSystem::GetLastPickResult() const noexcept
	{
		return m_Impl->LastPick;
	}

	void SelectionSystem::ClearLastPickResult() noexcept
	{
		m_Impl->LastPick.reset();
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
