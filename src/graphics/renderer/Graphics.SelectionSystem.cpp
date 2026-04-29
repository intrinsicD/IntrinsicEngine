module;

#include <memory>
#include <optional>

module Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	struct SelectionSystem::Impl
	{
		std::optional<PointSelectionRequest> PendingPointPick;
		std::optional<PointSelectionResult>  LastPointPick;
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
		m_Impl->PendingPointPick.reset();
		m_Impl->LastPointPick.reset();
		m_Impl->Initialized = false;
	}

	void SelectionSystem::RequestPointIdPick(PointSelectionRequest request) noexcept
	{
		m_Impl->PendingPointPick = request;
	}

	bool SelectionSystem::HasPendingPointIdPick() const noexcept
	{
		return m_Impl->PendingPointPick.has_value();
	}

	std::optional<PointSelectionRequest> SelectionSystem::ConsumePointIdPick() noexcept
	{
		std::optional<PointSelectionRequest> request = m_Impl->PendingPointPick;
		m_Impl->PendingPointPick.reset();
		return request;
	}

	void SelectionSystem::PublishPointIdResult(PointSelectionResult result) noexcept
	{
		m_Impl->LastPointPick = result;
	}

	std::optional<PointSelectionResult> SelectionSystem::GetLastPointIdResult() const noexcept
	{
		return m_Impl->LastPointPick;
	}

	void SelectionSystem::ClearLastPointIdResult() noexcept
	{
		m_Impl->LastPointPick.reset();
	}

	bool SelectionSystem::IsInitialized() const noexcept
	{
		return m_Impl->Initialized;
	}
}
